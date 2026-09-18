#include "model/catalog.h"

#include "model/session.h"

#include "jelly/api.h"
#include "base/log.h"
#include "base/worker.h"
#include "port/platform.h"

#include <stdio.h>
#include <string.h>

/* Asking is idempotent for loaded and in flight but not for a failure, which
 * without this re-asks sixty times a second. Long enough that the error is
 * legible; short enough that switching the radio on is noticed. */
#define RETRY_AFTER_US 5000000u

#define IDLE_US 20000u

/* The one before, the one asked about, and the one after. */
#define ADJACENT_MAX 3

/* In the order the worker serves them: the write before the reads it would
   invalidate, and the synopsis before the listing, because the page asking for
   one is the page on the panel. */
typedef enum { W_MARK = 0, W_ITEM, W_ADJACENT, W_ITEMS, W_HOME, W_N } want_kind;

/* Built zeroed, so two compare whole. */
typedef struct {
    char        id[JF_ID_LEN]; /* the item, the episode, or the listing's parent */
    char        series[JF_ID_LEN];
    item_sort   sort;
    int         desc;
    item_filter filter;
    int         mark, on;
} ask;

typedef struct {
    const char *what;
    /* Not the HTTP timeout: a want can be several requests (the front page is
       four). Unbounded, one wedged fetch left every later mark refused for the
       rest of the run. */
    unsigned bound_us, retry_us;

    volatile lib_state state;
    /* Moved by a give-up or a forget, so an answer arriving late drops itself
       rather than writing over what was asked for since. */
    volatile unsigned attempt, gen;
    volatile int      pending; /* posted, not yet taken by the worker */
    unsigned          sent_us, retry_at;
    ask               want, have;
    char              error[160];
} slot;

static slot           g_slot[W_N];
static platform_lock *g_lock;
static worker         g_worker;

/* One: one request is in flight at a time. */
static char   g_reply[JF_LIST_BUF];
static jf_buf g_rb = JF_BUF(g_reply);

static item      g_rail[LIB_RAIL_N][LIB_RAIL_MAX];
static int       g_rail_n[LIB_RAIL_N];
static lib_state g_rail_state[LIB_RAIL_N];

static item g_items[LIB_ITEM_MAX];
static int  g_items_n, g_items_total;
static char g_items_title[64];
/* The worker writes straight into g_items, so a give-up moves the state on
   while the array is still being filled. */
static volatile int g_items_busy;

/* The listing before this one, so backing out of a season is free. One slot:
   a second would be 25 kB for going three deep and back out twice. */
static item g_stash[LIB_ITEM_MAX];
static int  g_stash_n, g_stash_total;
static ask  g_stash_ask;

/* 512 is what the longest on this library needs; the field is truncated,
   never refused. */
static char g_over[512];
static char      g_over_id[JF_ID_LEN];
static item      g_item_now;
static jf_tracks g_tracks;

static item g_adjacent[ADJACENT_MAX];
static int  g_adjacent_n;

static platform_lock *lock(void) {
    if (!g_lock) g_lock = platform_lock_new("library");
    return g_lock;
}

static int ready(void) { return worker_running(&g_worker) && session_stage() == LIB_STAGE_READY; }

/* The clock wraps every ~71 minutes, so this compares a difference. */
static int due(unsigned at) { return at == 0 || (platform_clock_us() - at) < 0x80000000u; }

static int same(const ask *a, const ask *b) { return memcmp(a, b, sizeof(*a)) == 0; }

static int post(want_kind k, const ask *a) {
    slot *s = &g_slot[k];

    if (!ready()) return 0;
    if (same(a, &s->want) &&
        (s->state == LIB_BUSY || (s->state == LIB_READY && same(a, &s->have)) || (s->state == LIB_FAILED && !due(s->retry_at))))
        return 0;
    platform_lock_take(lock());
    s->want    = *a;
    s->pending = 1;
    s->sent_us = platform_clock_us();
    s->state   = LIB_BUSY;
    platform_lock_give(lock());
    return 1;
}

static void done(want_kind k, const ask *a, unsigned mine, const char *why) {
    slot *s = &g_slot[k];

    platform_lock_take(lock());
    if (mine == s->attempt) {
        snprintf(s->error, sizeof(s->error), "%s", why ? why : "");
        if (why) {
            s->retry_at = platform_clock_us() + s->retry_us;
        } else {
            s->have = *a;
            s->gen++;
        }
        s->state = s->pending ? LIB_BUSY : why ? LIB_FAILED : LIB_READY;
    }
    platform_lock_give(lock());
    if (why) log_printf("library: %s -- %s", s->what, why);
}

static void forget(want_kind k) {
    slot *s = &g_slot[k];

    platform_lock_take(lock());
    s->attempt++;
    s->pending  = 0;
    s->state    = LIB_IDLE;
    s->retry_at = 0;
    memset(&s->have, 0, sizeof(s->have));
    platform_lock_give(lock());
}

/* The worker is inside a blocking call and cannot time itself out. */
static void watch(want_kind k) {
    slot *s = &g_slot[k];

    if (s->state != LIB_BUSY || platform_clock_us() - s->sent_us < s->bound_us) return;
    platform_lock_take(lock());
    s->attempt++;
    s->pending  = 0;
    s->state    = LIB_FAILED;
    s->retry_at = platform_clock_us() + s->retry_us;
    snprintf(s->error, sizeof(s->error), "The server did not answer in time.");
    platform_lock_give(lock());
    log_printf("library: %s gave up after %u ms", s->what, s->bound_us / 1000u);
}

/* Fetched into a scratch and copied in under the lock: fetched straight in,
   the rails showed torn rows for the seconds a Reload took. */
static jf_err rail_fetch(lib_rail rail) {
    static item stage[LIB_RAIL_MAX];
    int         n = 0;
    jf_err      e;

    switch (rail) {
    case LIB_RAIL_VIEWS: e = jf_views(&g_rb, stage, LIB_RAIL_MAX, &n); break;
    case LIB_RAIL_RESUME: e = jf_resume(&g_rb, stage, LIB_RAIL_ASK, &n); break;
    case LIB_RAIL_NEXT: e = jf_next_up(&g_rb, stage, LIB_RAIL_ASK, &n); break;
    default: e = jf_latest(&g_rb, 0, stage, LIB_RAIL_ASK, &n); break;
    }
    if (e != JF_OK) n = 0;

    /* ponytail: the page reads without the lock, so a frame can still
       straddle this copy -- a window of microseconds, not a fetch. */
    platform_lock_take(lock());
    memcpy(g_rail[rail], stage, (unsigned)n * sizeof(item));
    g_rail_n[rail]     = n;
    g_rail_state[rail] = e == JF_OK ? LIB_READY : LIB_FAILED;
    platform_lock_give(lock());
    return e;
}

static jf_err fetch_home(void) {
    int    i;
    jf_err e = rail_fetch(LIB_RAIL_VIEWS);

    /* Alone: if the libraries fail there is nothing to draw. The rest may fail
       without taking the page: a new account has nothing to continue. */
    if (e != JF_OK) return e;
    rail_fetch(LIB_RAIL_RESUME);
    rail_fetch(LIB_RAIL_NEXT);
    rail_fetch(LIB_RAIL_LATEST);
    for (i = 0; i < LIB_RAIL_N; i++) log_printf("library: rail %d has %d", i, g_rail_n[i]);
    return JF_OK;
}

static void fetch(want_kind k, const ask *a, unsigned mine) {
    jf_err e;

    switch (k) {
    case W_MARK: e = a->mark == LIB_MARK_PLAYED ? jf_set_played(&g_rb, a->id, a->on) : jf_set_favorite(&g_rb, a->id, a->on); break;
    case W_ITEM: {
        /* Staged: a refresh of the film on the panel would empty an open
           track list for the length of the fetch. */
        static jf_tracks stage;

        e = jf_item(&g_rb, a->id, &g_item_now, g_over, (unsigned)sizeof(g_over), &stage);
        if (e == JF_OK) g_tracks = stage;
        break;
    }
    case W_ADJACENT: e = jf_adjacent(&g_rb, a->series, a->id, g_adjacent, ADJACENT_MAX, &g_adjacent_n); break;
    case W_ITEMS:
        g_items_busy = 1;
        e = jf_items(&g_rb, a->id[0] ? a->id : 0, a->sort, a->desc, a->filter, g_items, LIB_ITEM_MAX, &g_items_n, &g_items_total);
        g_items_busy = 0;
        break;
    default: e = fetch_home(); break;
    }

    if (e != JF_OK) {
        session_note_failure(e);
        if (k == W_ITEMS) g_items_n = 0;
        if (k == W_ADJACENT) g_adjacent_n = 0;
        done(k, a, mine, jf_err_text(e));
        return;
    }
    session_note_reached();

    if (mine == g_slot[k].attempt) {
        if (k == W_ITEM) snprintf(g_over_id, sizeof(g_over_id), "%s", a->id);
        if (k == W_ITEMS)
            log_printf("library: \"%s\" has %d of %d, %s%s%s%s", g_items_title, g_items_n, g_items_total, item_sort_label(a->sort),
                       a->desc ? " desc" : "", item_filter_key(a->filter) ? " " : "",
                       item_filter_key(a->filter) ? item_filter_label(a->filter) : "");
        if (k == W_MARK) {
            /* The held copy too: a stale "not watched" turned a press of
               Unwatch back into Watched and sent a second POST. */
            if (strcmp(g_item_now.id, a->id) == 0) {
                if (a->mark == LIB_MARK_PLAYED) {
                    g_item_now.played       = a->on;
                    g_item_now.resume_ticks = 0; /* the server drops it either way */
                } else {
                    g_item_now.favorite = a->on;
                }
            }
            forget(W_HOME);
            forget(W_ITEMS);
        }
    }
    done(k, a, mine, 0);
}

/* 0 when there was nothing to do, and base/worker then sleeps. */
static int serve(void *arg) {
    int k, did = 0;

    (void)arg;
    if (session_wants_connect()) {
        session_connect();
        did = 1;
    }
    for (k = 0; k < W_N; k++) {
        ask      a;
        unsigned mine;
        int      go;

        platform_lock_take(lock());
        go                = g_slot[k].pending;
        a                 = g_slot[k].want;
        mine              = g_slot[k].attempt;
        g_slot[k].pending = 0;
        platform_lock_give(lock());
        if (go) {
            fetch((want_kind)k, &a, mine);
            did = 1;
        }
    }
    return did;
}

static void slot_init(want_kind k, const char *what, unsigned bound_us, unsigned retry_us) {
    memset(&g_slot[k], 0, sizeof(g_slot[k]));
    g_slot[k].what     = what;
    g_slot[k].bound_us = bound_us;
    g_slot[k].retry_us = retry_us;
}

int library_start(void) {
    if (worker_running(&g_worker)) return 0;
    if (!lock()) return -1;

    session_init();
    slot_init(W_HOME, "the front page", 40000000u, RETRY_AFTER_US);
    slot_init(W_ITEMS, "the listing", 30000000u, RETRY_AFTER_US);
    slot_init(W_ITEM, "the synopsis", 20000000u, RETRY_AFTER_US);
    slot_init(W_ADJACENT, "the neighbours", 20000000u, RETRY_AFTER_US);
    /* No backoff: a mark is an action, not something re-asked on its own. */
    slot_init(W_MARK, "the mark", 8000000u, 0);

    if (worker_start(&g_worker, "library", serve, 0, IDLE_US) != 0) {
        session_note_stopped("The library worker would not start.");
        return -1;
    }
    return 0;
}

void library_stop(void) {
    /* Long: a pass can be inside a request that takes the whole HTTP timeout,
       still writing into these arrays. */
    (void)worker_stop(&g_worker, 20000000u);
}

void library_watch_link(void) {
    int k;

    if (!worker_running(&g_worker)) return;
    session_watch();
    for (k = 0; k < W_N; k++) watch((want_kind)k);

    /* What is in hand predates the outage, and nothing re-asked while the
       offline screen was up. */
    if (session_link_returned()) {
        library_forget_home();
        library_forget_items();
        library_forget_item();
    }
}

const char *library_state_text(lib_state s) {
    switch (s) {
    case LIB_BUSY: return "loading";
    case LIB_READY: return "ready";
    case LIB_FAILED: return "failed";
    default: return "idle";
    }
}

static const char *error_of(want_kind k) { return g_slot[k].error[0] ? g_slot[k].error : session_error(); }

void library_home(lib_home *out) {
    ask a;
    int r;

    memset(&a, 0, sizeof(a));
    post(W_HOME, &a);
    out->state = g_slot[W_HOME].state;
    out->error = error_of(W_HOME);
    for (r = 0; r < LIB_RAIL_N; r++) {
        out->rail[r]       = g_rail[r];
        out->n[r]          = g_rail_n[r];
        out->rail_state[r] = g_rail_state[r];
    }
}

void library_forget_home(void) { forget(W_HOME); }

static ask listing(const char *parent_id, item_sort sort, int descending, item_filter filter) {
    ask a;

    memset(&a, 0, sizeof(a));
    snprintf(a.id, sizeof(a.id), "%s", parent_id ? parent_id : "");
    a.sort   = sort;
    a.desc   = descending;
    a.filter = filter;
    return a;
}

static void want_items(const ask *a, const char *title) {
    slot *s = &g_slot[W_ITEMS];

    if (!ready() || (s->state == LIB_READY && same(a, &s->have))) return;

    /* The one before it, back without asking -- but not over a fetch that has
       been given up on and is still writing. */
    if (!g_items_busy && g_stash_n > 0 && same(a, &g_stash_ask)) {
        memcpy(g_items, g_stash, (unsigned)g_stash_n * sizeof(item));
        g_items_n     = g_stash_n;
        g_items_total = g_stash_total;
        g_stash_n     = 0;
        if (title) snprintf(g_items_title, sizeof(g_items_title), "%s", title);
        platform_lock_take(lock());
        s->attempt++;
        s->pending = 0;
        s->want = s->have = *a;
        s->gen++;
        s->state = LIB_READY;
        platform_lock_give(lock());
        return;
    }

    /* Stashed before the request goes out: the reply is written over these
       rows. */
    if (!g_items_busy && s->state == LIB_READY && g_items_n > 0) {
        memcpy(g_stash, g_items, (unsigned)g_items_n * sizeof(item));
        g_stash_n     = g_items_n;
        g_stash_total = g_items_total;
        g_stash_ask   = s->have;
    }
    if (post(W_ITEMS, a) && title) snprintf(g_items_title, sizeof(g_items_title), "%s", title);
}

void library_items(const char *parent_id, const char *title, item_sort sort, int descending, item_filter filter, lib_items *out) {
    const slot *s = &g_slot[W_ITEMS];
    ask         a = listing(parent_id, sort, descending, filter);
    int         answer;

    want_items(&a, title);
    answer     = s->state == LIB_READY && same(&a, &s->have);
    out->state = s->state;
    out->error = error_of(W_ITEMS);
    out->total = g_items_total;
    out->gen   = s->gen;
    out->n     = answer ? g_items_n : 0;
    out->rows  = answer ? g_items : 0;
}

void library_forget_items(void) {
    forget(W_ITEMS);
    /* Or Reload would hand back the very rows it was pressed to replace. */
    g_stash_n = 0;
}

void library_item(const item *it, lib_item *out) {
    ask a;

    memset(out, 0, sizeof(*out));
    if (!it || !it->id[0]) return;

    memset(&a, 0, sizeof(a));
    snprintf(a.id, sizeof(a.id), "%s", it->id);
    post(W_ITEM, &a);
    if (it->kind == ITEM_KIND_EPISODE && it->series_id[0]) {
        snprintf(a.series, sizeof(a.series), "%s", it->series_id);
        post(W_ADJACENT, &a);
    }

    out->state = g_slot[W_ITEM].state;
    out->item  = (out->state == LIB_READY && strcmp(g_item_now.id, it->id) == 0) ? &g_item_now : 0;
    /* Keyed on the id alone, not the state: gating on READY blanked the
       synopsis for the round trip of a refresh of the item already showing. */
    out->overview = strcmp(g_over_id, it->id) == 0 ? g_over : 0;
    out->tracks   = out->overview ? &g_tracks : 0;
}

void library_forget_item(void) {
    forget(W_ITEM);
    g_over_id[0] = 0;
}

const item *library_adjacent(const char *id, int after) {
    int i;

    if (g_slot[W_ADJACENT].state != LIB_READY || !id || !id[0]) return 0;
    for (i = 0; i < g_adjacent_n; i++)
        if (strcmp(g_adjacent[i].id, id) == 0) {
            int at = after ? i + 1 : i - 1;

            return (at >= 0 && at < g_adjacent_n) ? &g_adjacent[at] : 0;
        }
    return 0;
}

int library_mark(const char *id, lib_mark which, int on) {
    ask a;

    if (!id || !id[0] || g_slot[W_MARK].state == LIB_BUSY) return 0;
    memset(&a, 0, sizeof(a));
    snprintf(a.id, sizeof(a.id), "%s", id);
    a.mark = (int)which;
    a.on   = on ? 1 : 0;
    /* Not idempotent: the same mark twice is two presses. */
    g_slot[W_MARK].state = LIB_IDLE;
    return post(W_MARK, &a);
}

lib_state library_mark_state(void) { return g_slot[W_MARK].state; }

void library_note_stopped(const char *id, unsigned long long ticks) {
    if (strcmp(g_item_now.id, id) == 0) g_item_now.resume_ticks = ticks;
    forget(W_HOME);
    forget(W_ITEMS);
}
