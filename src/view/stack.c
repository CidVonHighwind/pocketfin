/* See stack.h. */

#include "view/stack.h"

#include "port/input.h"
#include "port/platform.h"

#include "base/log.h"

#include <stdio.h>
#include <string.h>

/* Focus is saved per level: ids are small integers each screen picks, so one
   live focus lands a popped-back list on whatever id the screen above had. */
static struct {
    const screen_def *def;
    void             *state;
    ui_id             focus;
    int               has_focus;
} g_stack[SCREEN_STACK_MAX];

static int      g_depth;
static ui_frame g_ui;

static union {
    void         *p;
    double        d;
    unsigned char bytes[SCREEN_ARENA_BYTES];
} g_arena;
static unsigned g_arena_used;

/* Applied inside the frame, a pop released the arena the popping screen was
   still drawing out of, and a push ran enter() halfway through the pusher's
   layout. Two in one frame is a screen that has lost track. */
typedef enum { NAV_NONE = 0, NAV_PUSH, NAV_POP, NAV_RESET } nav_kind;

static struct {
    nav_kind          what;
    const screen_def *def;
    unsigned char     arg[SCREEN_ARG_MAX];
    unsigned          arg_len;
} g_nav;

static int g_frame_took_back;

/* Outside a frame nothing is drawing out of the state, and deferring would make
   the caller draw a frame before its screen existed. */
static int g_in_frame;

static void nav_now(nav_kind what, const screen_def *def, const void *arg, unsigned arg_len);

static void nav_ask(nav_kind what, const screen_def *def, const void *arg, unsigned arg_len) {
    /* Refused, not trimmed: a receiver tests `arg_len == sizeof(mine)`. Before
       the deferral, because only the deferred path copies. */
    if (arg_len > SCREEN_ARG_MAX) {
        log_printf("screen: \"%s\" REFUSED -- handed %u bytes, %u is the most", def && def->name ? def->name : "?", arg_len,
                   (unsigned)SCREEN_ARG_MAX);
        return;
    }
    if (!g_in_frame) {
        nav_now(what, def, arg, arg_len);
        return;
    }
    if (g_nav.what != NAV_NONE) {
        log_printf("screen: \"%s\" navigated twice in one frame; keeping the first", screen_top_name());
        return;
    }
    g_nav.what    = what;
    g_nav.def     = def;
    g_nav.arg_len = arg_len;
    if (arg && arg_len) memcpy(g_nav.arg, arg, arg_len);
}

static unsigned aligned(unsigned n) { return (n + 7u) & ~7u; }

static void push_now(const screen_def *def, const void *arg, unsigned arg_len) {
    unsigned want;
    void    *st = 0;

    if (!def) return;
    if (g_depth >= SCREEN_STACK_MAX) {
        log_printf("screen: stack full at %d, \"%s\" refused", SCREEN_STACK_MAX, def->name ? def->name : "?");
        return;
    }
    want = aligned(def->state_bytes);
    if (g_arena_used + want > SCREEN_ARENA_BYTES) {
        log_printf("screen: no room for \"%s\" (%u of %u used)", def->name ? def->name : "?", g_arena_used, (unsigned)SCREEN_ARENA_BYTES);
        return;
    }
    if (arg_len && arg_len != def->arg_bytes) {
        log_printf("screen: \"%s\" REFUSED -- handed %u bytes, takes %u", def->name ? def->name : "?", arg_len, def->arg_bytes);
        return;
    }
    if (!arg_len && def->arg_bytes) log_printf("screen: \"%s\" opened without its argument", def->name ? def->name : "?");
    if (def->state_bytes) {
        st = g_arena.bytes + g_arena_used;
        memset(st, 0, def->state_bytes);
    }

    if (g_depth > 0) {
        g_stack[g_depth - 1].focus     = g_ui.focus;
        g_stack[g_depth - 1].has_focus = g_ui.has_focus;
    }

    g_arena_used += want;
    g_stack[g_depth].def   = def;
    g_stack[g_depth].state = st;
    g_depth++;
    log_printf("screen: -> %s (depth %d, %u of %u bytes)", def->name ? def->name : "?", g_depth, g_arena_used,
               (unsigned)SCREEN_ARENA_BYTES);

    g_ui.focus     = 0;
    g_ui.has_focus = 0;
    if (def->enter) def->enter(st, arg, arg_len);
}

static void pop_now(void) {
    const screen_def *under;

    if (g_depth <= 1) return;
    g_arena_used -= aligned(g_stack[g_depth - 1].def->state_bytes);
    g_depth--;

    g_ui.focus     = g_stack[g_depth - 1].focus;
    g_ui.has_focus = g_stack[g_depth - 1].has_focus;

    under = g_stack[g_depth - 1].def;
    log_printf("screen: <- %s (depth %d)", under->name ? under->name : "?", g_depth);
    if (under->resumed) under->resumed(g_stack[g_depth - 1].state);
}

static void reset_now(const screen_def *def) {
    g_depth      = 0;
    g_arena_used = 0;
    push_now(def, 0, 0);
}

void screen_push_with(const screen_def *def, const void *arg, unsigned arg_len) { nav_ask(NAV_PUSH, def, arg, arg_len); }

void screen_push(const screen_def *def) { nav_ask(NAV_PUSH, def, 0, 0); }
void screen_pop(void) { nav_ask(NAV_POP, 0, 0, 0); }
void screen_reset(const screen_def *def) { nav_ask(NAV_RESET, def, 0, 0); }

static void nav_now(nav_kind what, const screen_def *def, const void *arg, unsigned arg_len) {
    switch (what) {
    case NAV_PUSH: push_now(def, arg, arg_len); break;
    case NAV_POP: pop_now(); break;
    case NAV_RESET: reset_now(def); break;
    default: break;
    }
}

static void nav_apply(void) {
    nav_kind what = g_nav.what;

    g_nav.what = NAV_NONE;
    nav_now(what, g_nav.def, g_nav.arg_len ? g_nav.arg : 0, g_nav.arg_len);
}

void screen_back(void) {
    const screen_def *top;

    if (g_depth <= 0) return;
    top = g_stack[g_depth - 1].def;
    if (top->back && top->back(g_stack[g_depth - 1].state)) return;
    screen_pop();
}

void screen_frame_took_back(void) { g_frame_took_back = 1; }

/* Work is this code; the gap is everything else, so a blocking call on the
   drawing thread shows as a long gap with ordinary work. */
#define STALL_US 100000u

static struct {
    unsigned frames, worst_work, worst_gap, stalls;
    unsigned last_end, second_at;
} g_timing;

void screen_timing_skip_gap(void) { g_timing.last_end = 0; }

static void timing_note(unsigned t0, unsigned t1) {
    unsigned work = t1 - t0;
    unsigned gap  = g_timing.last_end ? t0 - g_timing.last_end : 0;

    g_timing.frames++;
    if (work > g_timing.worst_work) g_timing.worst_work = work;
    if (gap > g_timing.worst_gap) g_timing.worst_gap = gap;
    if (gap > STALL_US) g_timing.stalls++;
    g_timing.last_end = t1;

    if (!g_timing.second_at) g_timing.second_at = t1;
    if (t1 - g_timing.second_at < 1000000u) return;

    log_printf("screen: %u frames, work %u us, gap %u us%s", g_timing.frames, g_timing.worst_work, g_timing.worst_gap,
               g_timing.stalls ? " STALL" : "");
    g_timing.frames = g_timing.worst_work = g_timing.worst_gap = 0;
    g_timing.stalls                                            = 0;
    g_timing.second_at                                         = t1;
}

void screen_run_frame(ui_rect area, unsigned held, unsigned pressed) {
    const screen_def *top;
    void             *st;

    if (g_depth <= 0) return;
    top = g_stack[g_depth - 1].def;
    st  = g_stack[g_depth - 1].state;

    g_frame_took_back = 0;
    g_in_frame        = 1;
    {
        unsigned t0 = platform_clock_us();

        ui_frame_begin(&g_ui, area, held, pressed, t0);
        if (top->frame) top->frame(&g_ui, st);
        ui_frame_end(&g_ui);
        timing_note(t0, platform_clock_us());
    }
    /* Here so a page can refuse it, and still in-frame so it queues behind what
       the page asked for. */
    if ((pressed & PAD_CIRCLE) && !g_frame_took_back) screen_back();
    g_in_frame = 0;

    nav_apply();
}

const char *screen_top_name(void) { return g_depth > 0 ? g_stack[g_depth - 1].def->name : ""; }

int screen_depth(void) { return g_depth; }

void *screen_top_state(void) { return g_depth > 0 ? g_stack[g_depth - 1].state : 0; }

ui_frame *screen_ui(void) { return &g_ui; }

void screen_describe(char *out, unsigned n) {
    const screen_def *top = 0;
    void             *st  = 0;
    unsigned          len;

    if (!out || n < sizeof("end\n")) return;

    if (g_depth > 0) {
        top = g_stack[g_depth - 1].def;
        st  = g_stack[g_depth - 1].state;
    }

    len = (unsigned)snprintf(out, n, "page: %s\ndepth: %d\n", top ? top->name : "none", g_depth);
    if (len >= n) len = n - 1;

    /* Room for the terminator is reserved first, so a truncated description
       still ends in it. */
    if (top && top->describe && n - len > sizeof("end\n")) top->describe(st, out + len, n - len - (unsigned)sizeof("end\n"));

    len = (unsigned)strlen(out);
    snprintf(out + len, n - len, "end\n");
}
