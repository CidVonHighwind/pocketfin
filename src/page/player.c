/* See player.h. */

#include "page/player.h"

#include "jelly/api.h"
#include "model/catalog.h"
#include "model/reports.h"
#include "model/stream.h"
#include "model/session.h"
#include "base/log.h"
#include "page/item_text.h"
#include "port/decode.h"
#include "port/gfx.h"
#include "port/input.h"
#include "port/platform.h"
#include "view/element.h"

#include <stdio.h>
#include <string.h>

/* 59.94 in truth; neither repeat below is sensitive to that. */
#define TICKS_PER_SECOND 60u

/* ~0.4 s to the first repeat, then every 8 ticks. */
#define SEEK_DELAY 24u
#define SEEK_RATE  8u

#define SEEK_STEP (10ull * ITEM_TICKS_PER_S)
#define SEEK_JUMP (60ull * ITEM_TICKS_PER_S) /* the shoulders */
#define SEEK_TAIL (5ull * ITEM_TICKS_PER_S)  /* the last 5 s is never asked for */

#define AUTOHIDE_S 3u

#define COMMIT    (PAD_CROSS | PAD_START)
#define SEEK_MASK (PAD_LEFT | PAD_RIGHT | PAD_L | PAD_R)

/* The page reached by name with no item. Past an hour, so the clock takes its
   wider shape. */
#define DEMO_DUR   (91ull * 60ull * ITEM_TICKS_PER_S)
#define DEMO_START (12ull * 60ull * ITEM_TICKS_PER_S + 34ull * ITEM_TICKS_PER_S)

/* The only copy of any of it. Three faults in one evening were each a second
   copy synced by hand. */
typedef struct {
    /* What the band draws, kept in its own shape. seek_armed is aiming only:
       playback has NOT moved yet. ctl is never an absent neighbour. */
    ui_band b;
    char    title[ITEM_ROW_TEXT];

    int      leave_as; /* PLAYER_* when leaving is a step, 0 when it is a stop */
    unsigned hide_at;
    unsigned seek_held, seek_rep_at;
    int      seek_rep;
    unsigned last_us;

    /* False with no decoder and after the stream ends; advance() is the clock
       then. */
    int            playing, have;
    stream_picture shown;
    char           id[JF_ID_LEN];
    int            audio, sub; /* kept for every seek, which reopens the stream */

    /* A seek opens a new session, so the old one is stopped first or the server
       shows the film being watched twice. */
    char     session[JF_SESSION_LEN];
    unsigned told_at;
    int      told_paused;
} player_state;

typedef struct {
    item               it;
    unsigned long long start_ticks;
    int                has_prev, has_next;
    int                audio, sub;
} player_arg;

SCREEN_ARG_FITS(player_arg);

static player_state *now(void) {
    static player_state none;

    return screen_top_state() ? (player_state *)screen_top_state() : &none;
}

int                player_page_showing(void) { return now()->b.up; }
int                player_page_paused(void) { return now()->b.paused; }
int                player_page_seeking(void) { return now()->b.seek_armed; }
unsigned long long player_page_position(void) { return now()->b.pos; }

const char *player_result_text(player_result r) {
    switch (r) {
    case PLAYER_ENDED: return "the film ended";
    case PLAYER_STOPPED: return "stopped by the viewer";
    case PLAYER_PREV: return "previous item";
    case PLAYER_NEXT: return "next item";
    case PLAYER_FAILED: return "failed";
    default: return "?";
    }
}

int player_result_is_watched(player_result r) { return r == PLAYER_ENDED; }
int player_result_may_advance(player_result r) { return r == PLAYER_ENDED || r == PLAYER_NEXT; }

/* File statics: read after the pop, when the arena is no longer this page's. */
static int                g_result = (int)PLAYER_STOPPED;
static unsigned long long g_end_ticks;

int                player_page_result(void) { return g_result; }
unsigned long long player_page_end_ticks(void) { return g_end_ticks; }

static void open_at(player_state *p, unsigned long long at);

static void disarm(player_state *p) {
    p->b.seek_armed = 0;
    p->b.seek_to    = 0;
}

static int ctl_there(const player_state *p, int which) {
    if (which == UI_CTL_PREV) return p->b.has_prev;
    if (which == UI_CTL_NEXT) return p->b.has_next;
    return which == UI_CTL_PLAY;
}

/* Stops at the ends rather than wrapping. */
static int ctl_step(const player_state *p, int from, int dir) {
    int at = from;

    for (;;) {
        at += dir;
        if (at < UI_CTL_PREV || at > UI_CTL_NEXT) return from;
        if (ctl_there(p, at)) return at;
    }
}

static void ctl_set(player_state *p, int which) {
    p->b.ctl = (which == UI_CTL_NONE || ctl_there(p, which)) ? which : UI_CTL_PLAY;
    /* No key on the row can commit or cancel a seek. Here rather than per
       branch -- square's reveal leaked one once. */
    if (p->b.ctl != UI_CTL_NONE) disarm(p);
}

/* Only Playing expires: a still picture with no caption reads as a hang. */
static void show(player_state *p, ui_band_cap cap, unsigned long long target) {
    p->b.up  = 1;
    p->b.cap = cap;
    if (cap == UI_CAP_SEEKING || cap == UI_CAP_BUFFERING) p->b.seek_to = target;
    p->hide_at = (cap == UI_CAP_PLAYING) ? p->b.tick + AUTOHIDE_S * TICKS_PER_SECOND : 0u;
}

static void say_state(player_state *p) { show(p, p->b.paused ? UI_CAP_PAUSED : UI_CAP_PLAYING, 0); }

/* Non-zero when the page is to leave. */
static int act(player_state *p, unsigned pad, unsigned pressed) {
    unsigned  mask;
    long long step = 0;
    int       fire = 0;

    /* Circle cancels an armed seek instead, or a seek could only be committed. */
    if ((pressed & PAD_SELECT) || (!p->b.seek_armed && (pressed & PAD_CIRCLE))) return 1;

    if (pressed & PAD_TRIANGLE) p->b.stats = !p->b.stats;

    /* The caption does not change with visibility, so a reveal lands on
       play/pause. */
    if (pressed & PAD_SQUARE) {
        p->b.up = !p->b.up;
        if (p->b.up) {
            show(p, p->b.cap, p->b.seek_to);
            ctl_set(p, UI_CTL_PLAY);
        } else {
            disarm(p); /* an aim nobody can see is one nobody can cancel */
        }
        return 0;
    }

    if (!p->b.seek_armed && !p->b.ctl && (pressed & PAD_UP)) {
        say_state(p);
        ctl_set(p, UI_CTL_PLAY);
        return 0;
    }

    /* Otherwise a hidden band with the selection on the row answers a seek by
       walking buttons nobody can see. Falls through to aim the jump. */
    if (!p->b.up && (pressed & (PAD_LEFT | PAD_RIGHT))) {
        p->b.up  = 1;
        p->b.ctl = UI_CTL_NONE;
    }

    if (p->b.ctl) {
        if (!ctl_there(p, p->b.ctl)) ctl_set(p, UI_CTL_PLAY);

        if (pressed & PAD_LEFT) ctl_set(p, ctl_step(p, p->b.ctl, -1));
        if (pressed & PAD_RIGHT) ctl_set(p, ctl_step(p, p->b.ctl, +1));
        if (pressed & PAD_DOWN) ctl_set(p, UI_CTL_NONE);

        disarm(p);
        p->seek_held = 0;

        if (p->b.ctl && (pressed & COMMIT)) {
            /* The page underneath owns the neighbours and opens the next run;
               leaving without a reason reads as the viewer stopping. */
            if (p->b.ctl == UI_CTL_PREV) {
                p->leave_as = (int)PLAYER_PREV;
                return 1;
            }
            if (p->b.ctl == UI_CTL_NEXT) {
                p->leave_as = (int)PLAYER_NEXT;
                return 1;
            }
            p->b.paused = !p->b.paused;
            say_state(p);
        }
        return 0;
    }

    mask = pad & SEEK_MASK;
    if (mask != p->seek_held) { /* an edge always acts at once */
        p->seek_held   = mask;
        p->seek_rep_at = p->b.tick;
        p->seek_rep    = 0;
        fire           = (mask != 0);
    } else if (mask && (int)(p->b.tick - p->seek_rep_at) >= (int)(p->seek_rep ? SEEK_RATE : SEEK_DELAY)) {
        p->seek_rep_at = p->b.tick;
        p->seek_rep++;
        fire = 1;
    }

    if (fire) {
        unsigned long long unit = SEEK_STEP;

        if (p->seek_rep > 24)
            unit = 300ull * ITEM_TICKS_PER_S;
        else if (p->seek_rep > 14)
            unit = 60ull * ITEM_TICKS_PER_S;
        else if (p->seek_rep > 6)
            unit = 30ull * ITEM_TICKS_PER_S;

        if (mask & PAD_L)
            step = -(long long)SEEK_JUMP;
        else if (mask & PAD_R)
            step = (long long)SEEK_JUMP;
        else if (mask & PAD_LEFT)
            step = -(long long)unit;
        else if (mask & PAD_RIGHT)
            step = (long long)unit;

        if (step) {
            if (!p->b.seek_armed) {
                p->b.seek_to    = p->b.pos;
                p->b.seek_armed = 1;
            }
            if (step < 0 && p->b.seek_to < (unsigned long long)-step)
                p->b.seek_to = 0;
            else
                p->b.seek_to += (unsigned long long)step;
            if (p->b.dur > SEEK_TAIL && p->b.seek_to > p->b.dur - SEEK_TAIL) p->b.seek_to = p->b.dur - SEEK_TAIL;
            show(p, UI_CAP_SEEKING, p->b.seek_to);
        }
    }

    if (p->b.seek_armed && (pressed & COMMIT)) {
        p->b.pos        = p->b.seek_to;
        p->b.seek_armed = 0;
        /* A seek that landed paused would need cross again every time. */
        p->b.paused = 0;
        show(p, UI_CAP_BUFFERING, p->b.pos);
        open_at(p, p->b.pos);
        return 0;
    }
    if (p->b.seek_armed && (pressed & PAD_CIRCLE)) {
        disarm(p);
        say_state(p);
        return 0;
    }

    /* Unconditional, so there is no state in which it does something else. */
    if (pressed & COMMIT) {
        p->b.paused = !p->b.paused;
        say_state(p);
        ctl_set(p, UI_CTL_PLAY);
    }
    return 0;
}

static void enter(void *st, const void *arg, unsigned arg_len) {
    player_state *p = (player_state *)st;
    item          it;

    memset(p, 0, sizeof(*p));
    memset(&it, 0, sizeof(it));

    (void)arg_len;
    if (arg) {
        const player_arg *a = (const player_arg *)arg;

        it            = a->it;
        p->b.dur      = it.run_ticks ? it.run_ticks : DEMO_DUR;
        p->b.has_prev = a->has_prev;
        p->b.has_next = a->has_next;
        p->audio      = a->audio;
        p->sub        = a->sub;
        item_label(&it, 1, p->title, sizeof(p->title));
        snprintf(p->id, sizeof(p->id), "%s", it.id);
        p->b.pos = a->start_ticks < p->b.dur ? a->start_ticks : 0;
    } else {
        p->b.dur      = DEMO_DUR;
        p->b.has_next = 1;
        snprintf(p->title, sizeof(p->title), "%s", "Aguirre, the Wrath of God");
        p->b.pos = DEMO_START;
        p->audio = p->sub = -1;
    }
    p->b.title   = p->title;
    p->b.id_prev = PLAYER_ID_PREV;
    p->b.id_play = PLAYER_ID_PLAY;
    p->b.id_next = PLAYER_ID_NEXT;
    p->b.id_bar  = PLAYER_ID_BAR;
    p->b.tick    = platform_clock_us() / (1000000u / TICKS_PER_SECOND);
    p->last_us   = platform_clock_us();

    ctl_set(p, UI_CTL_PLAY);
    show(p, UI_CAP_BUFFERING, p->b.pos);

    open_at(p, p->b.pos);
}

static void end_session(player_state *p, unsigned long long at) {
    /* The worker may have opened a session this page has not copied yet; a
       viewer backing out mid-buffering would leave that transcode running. */
    if (!p->session[0] && stream_session()[0]) snprintf(p->session, sizeof(p->session), "%s", stream_session());
    if (!p->session[0]) return;
    reports_send(REPORT_STOP, p->id, p->session, at, 0);
    /* The server's ffmpeg outlives a client that walked away, and enough of
       those slow every later start. */
    reports_send(REPORT_ENCODING_OFF, p->id, p->session, 0, 0);
    p->session[0] = 0;
}

static void leave(player_state *p, player_result r) {
    g_result    = (int)r;
    g_end_ticks = p->b.pos;
    end_session(p, p->b.pos);
    stream_close();
    screen_back();
}

/* Returns at once: PlaybackInfo, the first segment and the decoder run on the
   stream's worker, so circle still works while "Buffering" shows. */
static void open_at(player_state *p, unsigned long long at) {
    p->have = 0;
    end_session(p, p->b.pos);

    p->playing = stream_open(p->id, at, p->b.dur, p->audio, p->sub) == 0;
    if (!p->playing) log_printf("play: %s -- %s", p->title, stream_error());
}

/* With a film the decoder owns the position: after a seek the wall clock and
   the picture disagree until the stream catches up. */
static void advance(player_state *p) {
    unsigned at = platform_clock_us(), dt = at - p->last_us;

    p->last_us = at;
    if (p->playing || p->b.paused || p->b.seek_armed) return;

    p->b.pos += (unsigned long long)dt * (ITEM_TICKS_PER_S / 1000000ull);
    if (p->b.dur && p->b.pos > p->b.dur) p->b.pos = p->b.dur;
}

/* A segment is a second of film, so fetch and demux together must stay well
   under 1000 ms, and a picture must decode in well under a frame. */
#define STAT_MAX   5
#define STAT_CHARS 64

static int stat_lines(const player_state *p, const char *const **out) {
    static char        buf[STAT_MAX][STAT_CHARS];
    static const char *ptr[STAT_MAX];
    stream_stats       f;
    int                n = 0, i;

    *out = ptr;
    if (!p->playing) {
        snprintf(buf[n++], STAT_CHARS, "no stream: the band only");
    } else {
        stream_get_stats(&f);
        snprintf(buf[n++], STAT_CHARS, "%dx%d  %u.%02u fps  %us", f.w, f.h, f.fps_centi / 100u, f.fps_centi % 100u,
                 (unsigned)(p->b.pos / ITEM_TICKS_PER_S));
        snprintf(buf[n++], STAT_CHARS, "pics %u  late %u  starved %u  buf %ux %u.%us", f.pictures, f.late, f.starved, f.stalls,
                 f.stall_ms / 1000u, (f.stall_ms / 100u) % 10u);
        snprintf(buf[n++], STAT_CHARS, "vid %u/%uk dry %u  aud %u/%uk dry %u", f.ring_used >> 10, f.ring_cap >> 10, f.starved,
                 f.aring_used >> 10, f.aring_cap >> 10, f.audio_dry);
        snprintf(buf[n++], STAT_CHARS, "seg %u max %uk nr %u  %u kbit/s", f.segments, f.segment_largest >> 10, f.not_ready, f.kbit);
        snprintf(buf[n++], STAT_CHARS, "fetch %u ms  demux %u ms  dec %u us", f.fetch_ms, f.demux_ms, f.decode_us);
    }
    for (i = 0; i < n; i++) ptr[i] = buf[i];
    return n;
}

static void frame(ui_frame *ui, void *st) {
    player_state *p = (player_state *)st;

    advance(p);
    /* The firmware's idle timer counts from the last button, so without this a
       film dims and suspends. Anywhere else it would be a battery bug. */
    if (p->playing) platform_activity_tick();

    /* From the clock: the panel's rate varies, and counting frames would
       stretch the auto-hide and the seek repeat. */
    p->b.tick = platform_clock_us() / (1000000u / TICKS_PER_SECOND);

    /* Not under a picture: decode_blit() fills its own margins, and with two
       surfaces and NEXTFRAME (port/decode.h) a surface blacked before the copy
       shows as a black band across the film. */
    if (!p->playing || !p->have) gfx_fill(0, 0, GFX_W, GFX_H, 0x000000u);
    if (p->playing) {
        stream_picture pic = {0};
        int            got = stream_frame(&pic, p->b.paused);

        if (got > 0) {
            p->shown = pic;
            p->have  = 1;
            /* Only a picture can say the wait is over. */
            if (p->b.cap == UI_CAP_BUFFERING) say_state(p);
        } else if (got < 0) {
            /* A failure must not mark it watched and walk on, or a link that
               stumbles walks a season in seconds. */
            leave(p, stream_error()[0] ? PLAYER_FAILED : PLAYER_ENDED);
            return;
        }
        if (p->have) decode_blit((GFX_W - p->shown.w) / 2, (GFX_H - p->shown.h) / 2);
        if (got > 0) p->b.pos = p->shown.at;
    }

    if (p->b.up && p->hide_at && (int)(p->b.tick - p->hide_at) >= 0) p->b.up = 0;

    /* Otherwise the stack pops on circle too: leaving popped two pages, and
       cancelling a seek popped this one. */
    if (ui->pressed & PAD_CIRCLE) screen_frame_took_back();

    /* Every frame: after a step the answer lands late. Kept only when the run
       holds this item, so a fetch in flight does not blink the row away. */
    if (p->id[0] && (library_adjacent(p->id, 0) || library_adjacent(p->id, 1))) {
        p->b.has_prev = library_adjacent(p->id, 0) != 0;
        p->b.has_next = library_adjacent(p->id, 1) != 0;
    }

    if (act(p, ui->held, ui->pressed)) {
        leave(p, p->leave_as ? (player_result)p->leave_as : PLAYER_STOPPED);
        return;
    }

    if (p->playing && !p->session[0] && stream_session()[0]) {
        snprintf(p->session, sizeof(p->session), "%s", stream_session());
        reports_send(REPORT_START, p->id, p->session, p->b.pos, 0);
        p->told_at     = p->b.tick;
        p->told_paused = p->b.paused;
    }

    /* Continue Watching is built from these, and an untold pause leaves the
       film playing on every other client. */
    if (p->session[0] && (p->b.paused != p->told_paused || (unsigned)(p->b.tick - p->told_at) >= 10u * TICKS_PER_SECOND)) {
        reports_send(REPORT_PROGRESS, p->id, p->session, p->b.pos, p->b.paused);
        p->told_at     = p->b.tick;
        p->told_paused = p->b.paused;
    }

    /* act() has answered all four directions; the frame must not move the
       selection too. */
    ui_frame_take_fired(ui);

    p->b.buffered   = p->playing ? stream_buffered() : 0;
    p->b.stat_count = p->b.stats ? stat_lines(p, &p->b.stat_lines) : 0;
    ui_player_band(ui, &p->b);
}

/* A starving run is diagnosed from these, not from a photograph of the
   readout. */
static void describe(void *st, char *out, unsigned n) {
    const player_state *p = (const player_state *)st;
    stream_stats        s;

    stream_get_stats(&s);
    snprintf(out, n,
             "band: %s\nstate: %s\nposition: %llu of %llu\nseek: %s %llu\n"
             "fps: %u.%02u buffering: %d (%u times, %u ms)\nstarved: %u late: %u pics: %u\n"
             "ring: %u/%u audio: %u/%u dry: %u frames: %u dropped: %u underruns: %u\n"
             "fetch_ms: %u (ttfb %u body %u) demux_ms: %u decode_us: %u kbit: %u due_ms: %d\n"
             "segments: %u pipelined: %u not_ready: %u\n",
             p->b.up ? "up" : "hidden", p->b.paused ? "paused" : "playing", (unsigned long long)p->b.pos, (unsigned long long)p->b.dur,
             p->b.seek_armed ? "armed" : "none", (unsigned long long)(p->b.seek_armed ? p->b.seek_to : 0), s.fps_centi / 100u,
             s.fps_centi % 100u, s.buffering, s.stalls, s.stall_ms, s.starved, s.late, s.pictures, s.ring_used, s.ring_cap, s.aring_used,
             s.aring_cap, s.audio_dry, s.audio_frames, s.audio_dropped, s.audio_underruns, s.fetch_ms, s.ttfb_ms, s.body_ms, s.demux_ms,
             s.decode_us, s.kbit, s.due_ms, s.segments, s.pipelined, s.not_ready);
}

const screen_def player_page_screen = {"player", sizeof(player_state), sizeof(player_arg), enter, frame, 0, 0, describe};

void player_page_show(const item *it, unsigned long long start_ticks, int has_prev, int has_next, int audio, int sub) {
    player_arg a;

    memset(&a, 0, sizeof(a));
    if (it) a.it = *it;
    a.start_ticks = start_ticks;
    a.has_prev    = has_prev;
    a.has_next    = has_next;
    a.audio       = audio;
    a.sub         = sub;
    screen_push_with(&player_page_screen, &a, (unsigned)sizeof(a));
}
