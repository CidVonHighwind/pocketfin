/* See music.h. */

#include "model/music.h"

#include "base/log.h"
#include "base/prefs.h"
#include "base/worker.h"
#include "io/http.h"
#include "jelly/api.h"
#include "port/audio.h"
#include "port/platform.h"

#include <stdio.h>
#include <string.h>

/* 160 kbit/s AAC: well under what the console's Wi-Fi carries beside the
   pictures, and clean at 44.1 kHz. */
#define MUSIC_BITRATE 160000u

/* A reverse proxy drops a stream it cannot write to for a minute, which a long
   pause is. The song resumes from where it was, this many times. */
#define RESUME_TRIES 3

typedef enum { CMD_NONE = 0, CMD_NEXT, CMD_PREV, CMD_STOP, CMD_RESTART } music_cmd;

static platform_lock *g_lock; /* the queue, the status and the command */
static item           g_queue[MUSIC_QUEUE_MAX];
static int            g_count;
static volatile int   g_want = -1; /* the song the worker is to start next */
static volatile int   g_cmd;
static volatile int   g_paused;
static volatile int   g_busy; /* inside a song, the output open */
static music_status   g_status;
static worker         g_worker;

static platform_lock *lock(void) {
    if (!g_lock) g_lock = platform_lock_new("music");
    return g_lock;
}

int music_adts_frame(const unsigned char *p, unsigned avail, unsigned *frame_len, unsigned *hdr_len, unsigned char asc[2]) {
    unsigned len, hdr, profile, rate, ch;

    if (avail < 7) return 0;
    if (p[0] != 0xFF || (p[1] & 0xF6) != 0xF0) return -1; /* sync, layer 0 */
    hdr     = (p[1] & 1) ? 7 : 9;                          /* protection_absent */
    profile = (p[2] >> 6) & 3;                             /* object type - 1   */
    rate    = (p[2] >> 2) & 0xF;
    ch      = ((p[2] & 1) << 2) | (p[3] >> 6);
    len     = ((unsigned)(p[3] & 3) << 11) | ((unsigned)p[4] << 3) | (p[5] >> 5);
    if (rate > 12 || len <= hdr || len > 8192) return -1;
    if (avail < len) return 0;
    if (asc) {
        asc[0] = (unsigned char)(((profile + 1) << 3) | (rate >> 1));
        asc[1] = (unsigned char)(((rate & 1) << 7) | (ch << 3));
    }
    *frame_len = len;
    *hdr_len   = hdr;
    return 1;
}

/* --- the worker ------------------------------------------------------------ */

typedef struct {
    unsigned char buf[16 * 1024];
    unsigned      have;
    int           opened;
    unsigned long frames; /* played this connection */
    uint64_t      base_ticks;
    char          session[24];
    char          item_id[ITEM_ID_LEN];
    char          why[64];
} play_ctx;

static uint64_t ticks_of(const play_ctx *c) {
    return c->base_ticks + (uint64_t)c->frames * 1024u * ITEM_TICKS_PER_S / AUDIO_RATE;
}

/* Start and stop only, both while the output is closed: a request from inside
   the frame loop holds the next frame back, and that is an audible gap. A stop
   near the end is what marks the song played. */
static void report(play_ctx *c, int stop) {
    static char reply[2048];
    jf_buf      b = JF_BUF(reply);

    if (!pref(PREF_REPORT_PLAYBACK)) return;
    if (stop)
        (void)jf_report_stop(&b, c->item_id, c->session, ticks_of(c));
    else
        (void)jf_report_start(&b, c->item_id, c->session, ticks_of(c));
}

/* Called with the body as it arrives. Non-zero stops the request. */
static int sink(const unsigned char *data, unsigned len, void *user) {
    play_ctx *c = (play_ctx *)user;

    while (len) {
        unsigned take = sizeof(c->buf) - c->have, at = 0;

        if (take > len) take = len;
        memcpy(c->buf + c->have, data, take);
        c->have += take;
        data += take;
        len -= take;

        for (;;) {
            unsigned      flen, hlen;
            unsigned char asc[2];
            int           r;

            /* audio_play() blocks while the hardware is full, which is what
               paces the download; a pause waits here with the socket open. */
            while (g_paused && g_cmd == CMD_NONE) platform_sleep_us(20000);
            if (g_cmd != CMD_NONE) return 1;

            r = music_adts_frame(c->buf + at, c->have - at, &flen, &hlen, asc);
            if (r == 0) break;
            if (r < 0) {
                at++; /* resynchronise */
                continue;
            }
            if (!c->opened) {
                if (audio_open(asc, 2, AUDIO_RATE, AUDIO_CHANNELS) != 0) {
                    snprintf(c->why, sizeof(c->why), "%s", audio_error());
                    return 1;
                }
                c->opened = 1;
                platform_lock_take(lock());
                g_status.state = MUSIC_PLAYING;
                platform_lock_give(lock());
            }
            (void)audio_play(c->buf + at + hlen, flen - hlen);
            c->frames++;
            /* About once a second: an album is an hour of not pressing anything. */
            if ((c->frames % 43u) == 0) platform_keep_awake();
            at += flen;

            platform_lock_take(lock());
            g_status.pos_s = (unsigned)(ticks_of(c) / ITEM_TICKS_PER_S);
            platform_lock_give(lock());
        }
        memmove(c->buf, c->buf + at, c->have - at);
        c->have -= at;
    }
    return 0;
}

/* Plays the queue's song `idx` to its end, a command, or a failure. Returns
   the command that ended it, CMD_NONE for the end of the song. */
static int play_one(int idx) {
    static play_ctx c;
    char            host[JF_HOST_LEN], path[JF_URL_LEN], err[96];
    int             port, status, tries = 0, cmd;
    http_result     rc;
    item            it;

    platform_lock_take(lock());
    it              = g_queue[idx];
    g_status.index  = idx;
    g_status.now    = it;
    g_status.pos_s  = 0;
    g_status.dur_s  = (unsigned)(it.run_ticks / ITEM_TICKS_PER_S);
    g_status.state  = MUSIC_LOADING;
    g_status.error[0] = 0;
    platform_lock_give(lock());

    memset(&c, 0, sizeof(c));
    snprintf(c.item_id, sizeof(c.item_id), "%s", it.id);
    snprintf(c.session, sizeof(c.session), "pfm%08x", platform_clock_us());
    g_busy          = 1;
    report(&c, 0);
    log_printf("music: %s (%d of %d)", it.name, idx + 1, g_count);

    for (;;) {
        unsigned long before = c.frames;

        jf_address(host, sizeof(host), &port);
        jf_audio_path(it.id, ticks_of(&c), MUSIC_BITRATE, path, sizeof(path));
        c.base_ticks = ticks_of(&c);
        c.frames     = 0;
        c.have       = 0;
        status       = 0;
        err[0]       = 0;
        rc           = http_stream(HTTP_SOCK_NONE, host, port, path, sink, &c, 0, 0, &status, 0, err, sizeof(err));
        if (g_cmd != CMD_NONE || c.why[0]) break;
        if (rc == HTTP_OK && status == 200) break; /* the end of the song */
        /* Dropped mid-song (a long pause through a proxy): carry on from here. */
        if ((c.frames || before) && status != 401 && status != 404 && ++tries <= RESUME_TRIES) {
            log_printf("music: the stream dropped at %us (%s) -- resuming", g_status.pos_s, err[0] ? err : http_result_text(rc));
            continue;
        }
        snprintf(c.why, sizeof(c.why), "%s", status ? (status == 401 ? "not signed in" : "the server refused it")
                                                    : (err[0] ? err : http_result_text(rc)));
        break;
    }

    if (c.opened) audio_close();
    report(&c, 1);
    g_busy = 0;
    cmd    = g_cmd;
    g_cmd  = CMD_NONE;
    if (c.why[0]) {
        log_printf("music: %s would not play -- %s", it.name, c.why);
        platform_lock_take(lock());
        snprintf(g_status.error, sizeof(g_status.error), "%.30s: %.44s", it.name, c.why);
        platform_lock_give(lock());
    }
    return cmd;
}

static int next_song(int from, int step) {
    int i;

    for (i = from + step; i >= 0 && i < g_count; i += step)
        if (g_queue[i].kind == ITEM_KIND_SONG) return i;
    return -1;
}

static int pass(void *user) {
    int idx, cmd;

    (void)user;
    idx = g_want;
    if (idx < 0) return 0;
    g_want = -1;

    cmd = play_one(idx);
    if (g_want >= 0) return 1; /* a new queue arrived meanwhile */

    switch (cmd) {
    case CMD_STOP: idx = -1; break;
    case CMD_RESTART: break;
    case CMD_PREV: idx = next_song(idx, -1) >= 0 ? next_song(idx, -1) : idx; break;
    default: idx = next_song(idx, 1); break; /* the end of a song, or Next */
    }
    platform_lock_take(lock());
    /* Unless a new queue was handed over while this song wound down. */
    if (g_want < 0) {
        g_want = idx;
        if (idx < 0) {
            g_status.state = MUSIC_STOPPED;
            g_status.pos_s = 0;
            g_paused       = 0;
        }
    }
    platform_lock_give(lock());
    return 1;
}

/* --- the drawing thread's side --------------------------------------------- */

void music_play(const item *items, int n, int start) {
    int i, k = 0, first = -1;

    if (!items || n <= 0) return;
    music_stop();
    platform_lock_take(lock());
    for (i = 0; i < n && k < MUSIC_QUEUE_MAX; i++) {
        if (items[i].kind != ITEM_KIND_SONG) continue;
        if (i == start || (first < 0 && i > start)) first = k;
        g_queue[k++] = items[i];
    }
    g_count          = k;
    g_status.count   = k;
    g_status.error[0] = 0;
    platform_lock_give(lock());
    if (first < 0) first = 0;
    if (!k) return;
    g_paused = 0;
    if (!worker_running(&g_worker)) worker_start(&g_worker, "music", pass, 0, 50000);
    platform_lock_take(lock());
    g_want = first;
    platform_lock_give(lock());
}

void music_toggle_pause(void) {
    if (!music_active()) return;
    g_paused = !g_paused;
    platform_lock_take(lock());
    if (g_status.state != MUSIC_LOADING) g_status.state = g_paused ? MUSIC_PAUSED : MUSIC_PLAYING;
    platform_lock_give(lock());
}

void music_next(void) {
    if (g_busy) g_cmd = CMD_NEXT;
    g_paused = 0;
}

void music_prev(void) {
    if (g_busy) g_cmd = g_status.pos_s > 3 ? CMD_RESTART : CMD_PREV;
    g_paused = 0;
}

void music_stop(void) {
    unsigned began = platform_clock_us();

    g_want = -1;
    if (g_busy) g_cmd = CMD_STOP;
    g_paused = 0;
    /* Bounded: a song stuck in a read gives the output up at its timeout. */
    while (g_busy && platform_clock_us() - began < 3000000u) platform_sleep_us(10000);
    platform_lock_take(lock());
    g_status.state = MUSIC_STOPPED;
    platform_lock_give(lock());
}

void music_status_get(music_status *out) {
    platform_lock_take(lock());
    *out = g_status;
    platform_lock_give(lock());
}

int music_active(void) { return g_status.state != MUSIC_STOPPED || g_busy || g_want >= 0; }
