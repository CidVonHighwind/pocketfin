#include "model/stream.h"

#include "model/fmp4.h"
#include "model/play_clock.h"
#include "model/session.h"

#include "jelly/segments.h"
#include "io/net.h"

#include "base/log.h"
#include "base/standby.h"
#include "base/prefs.h"
#include "base/worker.h"

#include "port/audio.h"
#include "port/decode.h"
#include "port/gfx.h"
#include "port/mem.h"
#include "port/platform.h"

#include <stdio.h>
#include <string.h>

/* The stage takes one segment off the link whole -- up to 824 kB, measured
 * (King Kong, 2026-08-28); a truncated one kills playback at the same spot
 * every time. The ring holds only the sample bytes cut out of it: 3 MB held
 * 3 s of film kept whole, 9 s packed. */
#define SEG_MAX (1024u * 1024u)

/* Nine seconds at 25 fps is 225 pictures; sound runs at 43 frames a second. */
#define SAMPLES  256
#define ASAMPLES 512

/* Kept free before a segment is cut up, so take_sample's refusal stays a
   backstop. A second of film is 24-30 pictures. */
#define SEG_SLOTS 64

#define TRIES 3

typedef struct {
    unsigned long long at;  /* where it sits in the FILM, in ticks    */
    unsigned long long abs; /* its first byte, counted from the start */
    unsigned           len;
} sample_rec;

/* One a track: video is consumed on the picture's turn and audio whenever the
   hardware takes a buffer, so one ring would have each wait on the other. */
typedef struct {
    unsigned char *buf;
    unsigned       cap;
    /* Monotonic counts, not offsets: a sample is never split across the join,
       so the write cursor sometimes skips the tail, and an offset pair cannot
       tell full from empty. */
    unsigned long long wr, rd;
    sample_rec        *samp;
    int                max, head, tail; /* [head, tail) is what is waiting */
    /* A segment cut up again after a refusal hands over everything before it
       a second time; this says what the ring already took. */
    unsigned long long took_at;
    int                took;
} ring;

static sample_rec g_vsamp[SAMPLES];
static sample_rec g_asamp[ASAMPLES];
static ring       g_video = {0, 0, 0, 0, g_vsamp, SAMPLES, 0, 0, 0, 0};
static ring       g_audio = {0, 0, 0, 0, g_asamp, ASAMPLES, 0, 0, 0, 0};

static unsigned char *g_stage;

static int    g_refused;
static worker g_aworker;
static int    g_audio_on; /* the stream has a sound track and it opened */

static platform_lock *g_lock;
static worker         g_worker;

static segments g_seg;
static fmp4     g_mp4;

static char   g_reply[32768];
static jf_buf g_rb = JF_BUF(g_reply);

static char               g_item[64];
static char               g_session[JF_SESSION_LEN];
static unsigned long long g_from, g_run_ticks;
static int                g_audio_track, g_sub_track;
static unsigned long long g_last_dts;
/* The one zero both tracks are measured from, taken once so it cannot move
   under samples already stamped against it. */
static unsigned long long g_zero;
static int                g_zero_set;

/* The frame the hardware took last, and when. Under g_lock: a pair read
   half-written is a wrong position. 0 us is nothing played this film. */
static unsigned long long g_sound_at;
static unsigned           g_sound_us;

static volatile int g_opened; /* the worker has the segment URLs */
static volatile int g_ended;  /* every segment fetched */
static volatile int g_failed;
static volatile int g_running;
static int          g_next_seg;

/* Two marks, not one: resuming on the first sample that arrives stalls on the
   next; waiting for the ring to empty makes every stall a visible hitch. Half
   a second to stop, three to go. */
#define BUFFER_LOW_SAMPLES 12
/* A segment takes ~1.27 s to fetch until the pipeline warms. Measured,
   starting on two seconds stalled ~1 s on every start, and on one segment
   1.2-1.5 s. */
#define BUFFER_GO_SAMPLES 72

static play_clock g_clock;

static stream_stats g_stats;
static unsigned     g_demux_us, g_demux_n;
static unsigned     g_decode_us, g_decode_n;
static unsigned     g_open_us, g_first_us;
static unsigned     g_stall_at; /* 0 while the clock is running */
/* The sound is the clock only while the picture side is there to follow it:
   with the offline screen over the film the sound ran the whole ring out
   alone, and on reconnect the picture raced to catch up. */
static volatile unsigned g_frame_us;
#define SOUND_ALONE_US 250000u
static char g_err[128];

const char *stream_error(void) { return g_err; }
const char *stream_session(void) { return g_opened ? g_session : ""; }

static unsigned long long elapsed(void) { return play_clock_pos(&g_clock); }

static unsigned long long in_ticks(unsigned long long dts) {
    if (!g_mp4.video.timescale) return 0;
    return dts * ITEM_TICKS_PER_S / g_mp4.video.timescale;
}

/* Each track in its own units: 44,100/s against the picture's 12,288.
   Signed, because an AAC track opens with priming samples whose dts is
   negative; multiplied out unsigned it becomes a position nothing ever comes
   due for. Priming lands at zero and is dropped as stale. */
static unsigned long long ticks_at(unsigned long long dts, unsigned scale) {
    long long d = (long long)dts;

    if (!scale || d < 0) return 0;
    return (unsigned long long)d * ITEM_TICKS_PER_S / (unsigned long long)scale;
}

/* Both tracks subtract the SAME zero, so their real offset is kept: the
   sound's first sample sat 40 ms before the picture's -- measured, King Kong
   at 1867 s. Clamped at the film's start. */
static unsigned long long stamp(unsigned long long ticks) {
    if (!g_zero_set) return g_from;
    if (ticks >= g_zero) return g_from + (ticks - g_zero);
    {
        unsigned long long back = g_zero - ticks;

        return g_from > back ? g_from - back : 0;
    }
}

/* The first sample of EITHER track, never a priming one: its negative dts
   would put the zero a whole film ahead of every real sample. */
static void take_zero(unsigned long long dts, unsigned scale) {
    if (g_zero_set || (long long)dts < 0) return;
    g_zero     = ticks_at(dts, scale);
    g_zero_set = 1;
}

static int queued(const ring *q) { return q->tail - q->head; }

/* Skipped tails included: that space is not free until the samples in front
   of it are gone. */
static unsigned used(const ring *q) { return (unsigned)(q->wr - q->rd); }

static void ring_reset(ring *q) {
    q->wr = q->rd = 0;
    q->head = q->tail = 0;
    q->took_at        = 0;
    q->took           = 0;
}

/* Under g_lock. The decoder is handed one pointer and one length, so the tail
   is skipped. The skip is checked with the sample, not after advancing the
   write mark: that let the free space underflow, and the memcpy went over
   unread samples -- black bands through a picture, at a ring 97% full. */
static int put(ring *q, const fmp4_sample *s, unsigned long long film) {
    unsigned    at   = (unsigned)(q->wr % q->cap);
    unsigned    skip = (at + s->len > q->cap) ? q->cap - at : 0u;
    sample_rec *r;

    if (queued(q) >= q->max || (unsigned long long)s->len + skip > q->cap - used(q)) return -1;
    q->wr += skip;
    memcpy(q->buf + (skip ? 0u : at), s->data, s->len);
    r      = &q->samp[q->tail % q->max];
    r->abs = q->wr;
    r->len = s->len;
    r->at  = film;
    q->wr += s->len;
    q->tail++;
    q->took_at = film;
    q->took    = 1;
    return 0;
}

static int peek(const ring *q, sample_rec *r) {
    int have;

    platform_lock_take(g_lock);
    have = queued(q) > 0;
    if (have) *r = q->samp[q->head % q->max];
    platform_lock_give(g_lock);
    return have;
}

static void pop(ring *q, const sample_rec *r, unsigned *count) {
    platform_lock_take(g_lock);
    q->head++;
    q->rd = r->abs + r->len;
    if (count) (*count)++;
    platform_lock_give(g_lock);
}

static int take_sample(const fmp4_sample *s, void *user) {
    int                video = s->track == FMP4_VIDEO;
    ring              *q     = video ? &g_video : &g_audio;
    unsigned           scale = video ? g_mp4.video.timescale : g_mp4.audio.timescale;
    unsigned long long film;
    int                full;

    (void)user;
    /* A stream whose audio would not open plays silent rather than fill a
       ring nobody drains, which stops the fetch thread. */
    if (!video && !g_audio_on) return 0;

    take_zero(s->dts, scale);
    film = stamp(ticks_at(s->dts, scale));
    if (q->took && film <= q->took_at) return 0;

    platform_lock_take(g_lock);
    full = put(q, s, film) != 0;
    /* The sound drops: catching up is a click, where waiting is a stall. */
    if (full && !video) g_stats.audio_dropped++;
    platform_lock_give(g_lock);
    if (!video) return 0;

    /* The skip is the one thing the segment's own length cannot count.
       fetch_one cuts the segment up again. */
    if (full) {
        g_refused = 1;
        return 1;
    }
    /* A discontinuity stamps everything after it into the future: the ring
       fills, nothing comes due, and no counter reports it. */
    if (g_last_dts && (long long)(s->dts - g_last_dts) > (long long)scale)
        log_printf("stream: picture clock jumped %lld ticks (%u a second) at %llu ms", (long long)(s->dts - g_last_dts), scale,
                   film / (ITEM_TICKS_PER_S / 1000ull));
    g_last_dts = s->dts;
    return 0;
}

/* Its own worker because audio_play() blocks until the hardware will take
 * another buffer. */
static int serve_audio(void *arg) {
    static int urgent;
    sample_rec r = {0};

    (void)arg;
    if (!urgent) {
        platform_thread_urgent();
        urgent = 1;
    }
    /* Silent while the picture is frozen, or sound comes back out of step. */
    if (!g_running || !g_audio_on || !play_clock_running(&g_clock)) return 0;
    if (platform_clock_us() - g_frame_us > SOUND_ALONE_US) return 0;

    if (!peek(&g_audio, &r)) {
        /* Not before the first picture, or the wait for it reads as a fault. */
        if (g_first_us && !g_ended) g_stats.audio_dry++;
        return 0;
    }

    /* Only the film's first frame waits for the picture. After that the sound
       IS the clock, and a frame held back is a channel left to run dry. */
    if (!g_sound_us && (!g_first_us || r.at > elapsed())) return 0;

    /* Priming, or what a stall or a wake left in the ring: dropped, never
       played late. */
    if (r.at + (unsigned long long)PLAY_CLOCK_SOUND_FRAME_US * (ITEM_TICKS_PER_S / 1000000ull) < elapsed()) {
        pop(&g_audio, &r, &g_stats.audio_dropped);
        return 1;
    }

    /* Returns as this frame starts to play: the moment the stamp is true. */
    if (audio_play(g_audio.buf + (unsigned)(r.abs % g_audio.cap), r.len) == 0) {
        unsigned now = platform_clock_us();

        platform_lock_take(g_lock);
        g_sound_at = r.at;
        g_sound_us = now ? now : 1u;
        platform_lock_give(g_lock);

        /* Rate-limited: the card log restarts at its ceiling, and noise there
           wipes the start of a run. */
        {
            static unsigned said_at, said;
            unsigned        dry = 0;

            audio_stats(0, &dry);
            if (dry < said) said = 0;
            if (dry != said && now - said_at > 10000000u) {
                log_printf("audio: the channel ran dry %u more times, %u this film", dry - said, dry);
                said_at = now;
                said    = dry;
            }
        }
    }
    pop(&g_audio, &r, 0);
    return 1;
}

static int open_urls(void) {
    jf_hls h;
    jf_err e;

    /* Read at open, so a settings change takes on the next film. */
    e = jf_hls_open(&g_rb, g_item, g_from, (unsigned)pref(PREF_BITRATE_KBPS) * 1000u, g_audio_track, g_sub_track, &h, g_session,
                    sizeof(g_session));
    if (e != JF_OK) {
        snprintf(g_err, sizeof(g_err), "no stream: %s", jf_err_text(e));
        return -1;
    }
    if (seg_open(&g_seg, &h, g_run_ticks, g_stage, SEG_MAX) != 0) {
        snprintf(g_err, sizeof(g_err), "the segment stage would not take");
        return -1;
    }
    g_next_seg = h.first_seg;
    g_opened   = 1;
    return 0;
}

static int g_staged; /* the stage holds a segment nobody has cut up yet */

static int fetch_tries(int index) {
    int tries;

    for (tries = 0; tries < TRIES && g_running; tries++)
        if (seg_fetch(&g_seg, index) == SEG_ARRIVED) return 1;
    return 0;
}

static int open_init(void) {
    /* The first segment before the init: see seg_fetch(). */
    if (!fetch_tries(g_next_seg) || !fetch_tries(-1)) {
        if (g_running) snprintf(g_err, sizeof(g_err), "the stream would not start: %s", g_seg.err);
        return -1;
    }
    g_staged = 1;

    /* stream_close may have handed the panel and video memory back to the
       browser while that was on the wire, and decode_open memsets video
       memory: the screen tore up on a page with no film open. */
    if (!g_running) return -1;

    if (fmp4_init(&g_mp4, g_seg.init, g_seg.init_len) != 0) {
        snprintf(g_err, sizeof(g_err), "%s", g_mp4.err);
        return -1;
    }
    /* Audio before video: both decoders share the Media Engine's EDRAM, and
       the other order gives video with no chroma. */
    g_audio_on = 0;
    if (g_mp4.audio.asc_len && audio_available()) {
        if (audio_open(g_mp4.audio.asc, g_mp4.audio.asc_len, g_mp4.audio.rate, g_mp4.audio.channels) == 0)
            g_audio_on = 1;
        else
            log_printf("stream: no sound (%s) -- carrying on silent", audio_error());
    }

    if (decode_open(g_mp4.video.sps, g_mp4.video.sps_len, g_mp4.video.pps, g_mp4.video.pps_len, g_mp4.video.nal_len_size,
                    (int)g_mp4.video.width, (int)g_mp4.video.height) != 0) {
        snprintf(g_err, sizeof(g_err), "%s", decode_error());
        return -1;
    }

    g_stats.w = (int)g_mp4.video.width;
    g_stats.h = (int)g_mp4.video.height;
    log_printf("stream: %ux%u, timescale %u, init %u bytes, first segment %d", g_mp4.video.width, g_mp4.video.height,
               g_mp4.video.timescale, g_seg.init_len, g_next_seg);
    return 0;
}

static int fetch_one(void) {
    unsigned room;
    int      n, slots;

    if (!g_staged) {
        seg_result r;

        /* Every attempt is refused before it reaches the stack, and three
           tries went by in a moment, which ended the film while the console
           was still waking up. The last state, because the firmware only
           answers the drawing thread. */
        if (net_state_last() != NET_UP) return 0;

        r = seg_fetch(&g_seg, g_next_seg);
        if (r != SEG_ARRIVED) {
            if (r == SEG_NOT_READY) {
                g_stats.not_ready++;
                /* What was asked for ahead is worth nothing and holds a
                   connection. */
                seg_close(&g_seg);
                return 0;
            }
            if (r == SEG_END) {
                g_ended = 1;
                log_printf("stream: past the last segment at %d", g_next_seg);
                return 0;
            }
            return -1;
        }
        g_staged = 1;
    }

    /* Room means bytes AND sample slots: checking only bytes let take_sample
       stop the demux part way, losing the rest of that second. Measured on
       Cowboy Bebop: "picture clock jumped 24024 ticks", exactly 1.001 s. 256
       slots is 10.6 s at 24 fps, while 3 MB is 8.6 s at 3.5 Mbit and 12 s at
       2.5 Mbit -- the lower the bitrate, the sooner slots run out first. */
    platform_lock_take(g_lock);
    room  = g_video.cap - used(&g_video);
    slots = SAMPLES - queued(&g_video);
    platform_lock_give(g_lock);
    if (room < g_seg.len || slots < SEG_SLOTS) return 0;

    {
        unsigned t0 = platform_clock_us();

        g_refused = 0;
        n         = fmp4_fragment(&g_mp4, g_stage, g_seg.len, take_sample, 0);
        g_demux_us += platform_clock_us() - t0;
        g_demux_n++;
    }
    /* Turned away part way, it stays staged; what already went in is known by
       its time. Dropped instead, the sound traf after the pictures never came
       and 998 ms of sound was gone: the same 73 late pictures on every run of
       one episode. */
    if (g_refused) return 0;
    g_staged = 0;
    if (n <= 0) {
        log_printf("stream: segment %d: %s", g_next_seg, g_mp4.err);
        return -1;
    }

    g_stats.segments++;
    if (g_seg.len > g_stats.segment_largest) g_stats.segment_largest = g_seg.len;
    g_next_seg++;
    return 1;
}

static int serve(void *arg) {
    static int fails;

    (void)arg;
    if (!g_running || g_failed || g_ended) return 0;

    if (!g_opened) {
        if (open_urls() != 0 || open_init() != 0) {
            g_failed = 1;
            log_printf("stream: %s", g_err);
            return 1;
        }
        fails = 0;
        return 1;
    }

    /* The ring is the only bound: a paused film keeps filling, so pausing
       lets a bad link catch up. */
    switch (fetch_one()) {
    case 1: fails = 0; return 1;
    case 0: return 0;
    default:
        if (++fails >= TRIES) {
            snprintf(g_err, sizeof(g_err), "the stream stopped: %s", g_seg.err[0] ? g_seg.err : "no segment");
            g_failed = 1;
            log_printf("stream: %s", g_err);
        }
        return 1;
    }
}

/* No clock change here: sceNetInit pins it, so scePowerSetClockFrequency
   returns success and leaves it where it was. Measured, the clock is worth
   7-13% on the link; the processor dial is the viewer's. */
int stream_open(const char *item_id, unsigned long long from, unsigned long long run_ticks, int audio, int sub) {
    stream_close();

    if (!decode_available()) {
        snprintf(g_err, sizeof(g_err), "this build has no decoder");
        return -1;
    }
    if (!item_id || !item_id[0]) {
        snprintf(g_err, sizeof(g_err), "no item");
        return -1;
    }
    if (!worker_running(&g_worker)) {
        snprintf(g_err, sizeof(g_err), "the stream worker is not running");
        return -1;
    }
    /* Before there is a picture: the decoder's buffers start at the base of
       video memory, where the browser's 5-6-5 pages live, and waiting for the
       first picture drew a screenful of garbage instead of black. */
    decode_take_panel();

    if (!g_video.buf) g_video.buf = (unsigned char *)mem_reserve(MEM_VIDEO_RING);
    if (!g_stage) g_stage = (unsigned char *)mem_reserve(MEM_SEG_STAGE);
    if (!g_audio.buf) g_audio.buf = (unsigned char *)mem_reserve(MEM_AUDIO_RING);
    if (!g_lock || !g_video.buf || !g_stage || !g_audio.buf) {
        snprintf(g_err, sizeof(g_err), "no room for a film");
        return -1;
    }
    g_video.cap = mem_size(MEM_VIDEO_RING);
    g_audio.cap = mem_size(MEM_AUDIO_RING);

    memset(&g_stats, 0, sizeof(g_stats));
    memset(&g_mp4, 0, sizeof(g_mp4));
    g_err[0]     = 0;
    g_session[0] = 0;
    g_last_dts   = 0;
    g_sound_at   = 0;
    g_sound_us   = 0;
    g_zero       = 0;
    g_zero_set   = 0;
    g_audio_on   = 0;
    g_demux_us = g_demux_n = 0;
    g_decode_us = g_decode_n = 0;
    g_opened = g_ended = g_failed = 0;
    g_from      = from;
    g_run_ticks   = run_ticks;
    g_audio_track = audio;
    g_sub_track   = sub;
    g_open_us     = platform_clock_us();
    g_first_us  = 0;
    g_frame_us  = 0;
    g_stall_at  = 0;
    play_clock_start(&g_clock, from, g_open_us);
    snprintf(g_item, sizeof(g_item), "%s", item_id);

    g_running = 1;
    return 0;
}

/* Creating the worker on open failed with "the stream worker would not start"
 * in front of a viewer who had just pressed Play. */
int stream_start(void) {
    if (worker_running(&g_worker)) return 0;
    if (!g_lock) g_lock = platform_lock_new("stream");
    if (!g_lock) return -1;
    if (worker_start(&g_worker, "stream", serve, 0, 20000u) != 0) return -1;
    if (worker_start(&g_aworker, "sound", serve_audio, 0, 5000u) != 0) log_line("stream: no sound worker -- films will be silent");
    return 0;
}

void stream_stop(void) {
    g_running  = 0;
    g_audio_on = 0;
    if (worker_running(&g_aworker)) (void)worker_stop(&g_aworker, 20000000u);
    if (worker_running(&g_worker)) (void)worker_stop(&g_worker, 20000000u);

    /* Before sceNetInetTerm: stopping only the workers left the pipelined
       socket open and the Media Engine claimed. */
    stream_close();
}

void stream_close(void) {
    /* The workers must be OUT of a pass first: tearing down under them reset
       the ring counts while take_sample was mid-write, the unsigned difference
       wrapped, and the next memcpy landed outside the ring. */
    g_running = 0;
    worker_quiet(&g_worker, 4000000u);
    worker_quiet(&g_aworker, 500000u);

    seg_close(&g_seg);
    g_audio_on = 0;
    /* Reverse of open_init's order: released in claim order, the next
       audio_open succeeds but plays nothing, so only the first film of a run
       has sound. */
    decode_close();
    audio_close();

    platform_lock_take(g_lock);
    ring_reset(&g_video);
    ring_reset(&g_audio);
    platform_lock_give(g_lock);

    g_staged = 0;
    g_opened = 0;
}

unsigned long long stream_buffered(void) {
    unsigned long long ahead;

    /* Not "last sample's time minus clock": that stops growing when paused,
       while a paused film keeps filling. */
    platform_lock_take(g_lock);
    ahead = (unsigned long long)queued(&g_video) * in_ticks(g_mp4.sample_duration);
    platform_lock_give(g_lock);
    return ahead;
}

int stream_frame(stream_picture *out, int paused) {
    sample_rec         r = {0};
    decode_picture     pic;
    int                got, have, held;
    unsigned long long sound_at;
    unsigned           sound_us;

    if (!out || !g_running) return -1;
    play_clock_pause(&g_clock, paused);
    g_frame_us = platform_clock_us();
    if (g_failed) return -1;

    platform_lock_take(g_lock);
    held = queued(&g_video);
    have = held > 0;
    if (have) r = g_video.samp[g_video.head % SAMPLES];
    sound_at = g_sound_at;
    sound_us = g_sound_us;
    platform_lock_give(g_lock);

    /* The picture ring alone decides. Requiring the sound ring too deadlocked
       the run: nothing was consumed while stopped, the demuxer stopped once
       its ring refused one, so sound could never get deeper. Measured: stuck
       at 14 pictures, fetcher 107 segments ahead. */
    if (play_clock_running(&g_clock)) {
        if (held < BUFFER_LOW_SAMPLES && !g_ended) {
            play_clock_playing(&g_clock, 0);
            g_stats.stalls++;
            g_stall_at = platform_clock_us();
        }
    } else if (held >= BUFFER_GO_SAMPLES || (g_ended && have)) {
        if (g_stall_at) g_stats.stall_ms += (platform_clock_us() - g_stall_at) / 1000u;
        g_stall_at = 0;
        play_clock_playing(&g_clock, 1);
    }

    /* Once a frame, only here. */
    play_clock_step(&g_clock, platform_clock_us());
    if (g_audio_on && sound_us) play_clock_follow(&g_clock, sound_at, platform_clock_us() - sound_us);

    if (!have) {
        if (g_ended) return -1;
        if (g_first_us) g_stats.starved++;
        return 0;
    }
    if (!play_clock_running(&g_clock)) return 0;

    /* One picture at most a frame: decoding until the film catches up would
       spend a whole frame here after a stall. */
    if (r.at > elapsed()) return 0;

    {
        unsigned t0 = platform_clock_us();

        got = decode_sample(g_video.buf + (unsigned)(r.abs % g_video.cap), r.len, &pic);
        g_decode_us += platform_clock_us() - t0;
        g_decode_n++;
    }
    /* The decoder copies what it is sent. */
    pop(&g_video, &r, 0);

    if (got < 0) {
        snprintf(g_err, sizeof(g_err), "%s", decode_error());
        g_failed = 1;
        return -1;
    }
    if (got == 0) return 0;

    g_stats.pictures++;
    if (!g_first_us) {
        /* The server rounds an open down to a segment boundary, so the first
           picture says where the film is. Before g_first_us, which the sound
           starts on. */
        g_from      = r.at;
        g_clock.pos = r.at;
        g_first_us  = platform_clock_us();
    }
    if (r.at + ITEM_TICKS_PER_S / 10ull < elapsed()) g_stats.late++;

    out->w  = pic.w;
    out->h  = pic.h;
    out->at = r.at;
    return 1;
}

void stream_get_stats(stream_stats *out) {
    if (!out) return;
    *out = g_stats;

    platform_lock_take(g_lock);
    out->buffering  = !g_clock.playing;
    out->fetch_ms   = g_seg.fetch_n ? g_seg.fetch_ms / g_seg.fetch_n : 0u;
    out->ttfb_ms    = g_seg.fetch_n ? g_seg.ttfb_ms / g_seg.fetch_n : 0u;
    out->body_ms    = g_seg.fetch_n ? g_seg.body_ms / g_seg.fetch_n : 0u;
    out->pipelined  = g_seg.pipelined;
    out->demux_ms   = g_demux_n ? g_demux_us / g_demux_n / 1000u : 0u;
    out->decode_us  = g_decode_n ? g_decode_us / g_decode_n : 0u;
    out->ring_used  = used(&g_video);
    out->ring_cap   = g_video.cap;
    out->due_ms =
        queued(&g_video) > 0
            ? (int)(((long long)g_video.samp[g_video.head % SAMPLES].at - (long long)elapsed()) / (long long)(ITEM_TICKS_PER_S / 1000ull))
            : 0;
    out->aring_used = used(&g_audio);
    out->aring_cap  = g_audio_on ? g_audio.cap : 0u;
    platform_lock_give(g_lock);
    audio_stats(&out->audio_frames, &out->audio_underruns);

    if (g_running) {
        /* Against film time: a clock that ran through a two-minute pause once
           reported a 24 fps stream as 6. */
        unsigned run_ms = (unsigned)((elapsed() - g_from) / (ITEM_TICKS_PER_S / 1000ull));

        /* Against wall time, for the same reason reversed: bytes keep arriving
           while the film's clock is stopped, and a pause read as a link four
           times faster than it is. */
        unsigned wall_ms = (platform_clock_us() - g_open_us) / 1000u;

        if (run_ms > 500u) out->fps_centi = out->pictures * 100000u / run_ms;
        if (wall_ms > 500u) out->kbit = (unsigned)(g_seg.bytes * 8ull / wall_ms);
    }
}
