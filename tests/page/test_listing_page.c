#include "page/listing.h"

#include "view/element.h"
#include "model/catalog.h"
#include "port/gfx.h"
#include "port/input.h"
#include "page/home.h"
#include "page/item_text.h"
#include "jelly/query.h"
#include "view/page_chrome.h"
#include "view/stack.h"
#include "view/scroll.h"
#include "tools/selftest.h"
#include "test_page.h"
#include "tools/shot.h"
#include "port/platform.h"

#include "../model/test_catalog.h"

#include <stdio.h>
#include <string.h>

/* UI_SCROLL_SETTLE is only the ceiling: a row that does not scroll settles in
   one frame, and paying 24 for each of them was 8 s of the suite on a software
   rasteriser. */
static void settle_scroll(void) {
    int was = listing_page_scroll(), quiet = 0, i;

    for (i = 0; i < UI_SCROLL_SETTLE && quiet < 2; i++) {
        int now;

        page_step(0);
        now   = listing_page_scroll();
        quiet = (now == was) ? quiet + 1 : 0;
        was   = now;
    }
}

/* The worker answers between frames. */
static int rows_landed(void) {
    unsigned t0 = platform_clock_us();

    while (listing_page_count() <= 0 && platform_clock_us() - t0 < 30000000u) {
        page_step(0);
        platform_sleep_us(10000);
    }
    return listing_page_count() > 0;
}

/* Over the front page, as the application opens one. */
static int open_listing(int kind, int *at, char *note, unsigned n) {
    char parent[JF_ID_LEN], title[ITEM_NAME_LEN];
    int  count = 0;

    if (real_listing(kind, parent, title, &count, at, note, n) != 0) return -1;
    screen_reset(&home_page_screen);
    listing_page_show(parent, title);
    if (!rows_landed()) {
        snprintf(note, n, "\"%s\" was fetched and the page drew no rows", title);
        return 1;
    }
    return 0;
}

static int a_listing(char *note, unsigned n) {
    int rc;

    if (page_panel(note, n) != 0) return -1;
    if ((rc = open_listing(-1, 0, note, n)) != 0) return rc;
    ui_frame_focus_set(screen_ui(), 0);
    page_step(0);
    return 0;
}

static int t_the_listing_shows_what_it_was_given(char *note, unsigned n) {
    if (a_listing(note, n) != 0) return -1;

    page_step(0);
    if (shot_write("listing.ppm") != 0) {
        snprintf(note, n, "the picture did not land");
        return 1;
    }
    if (listing_page_count() <= 1) {
        snprintf(note, n, "%d rows -- not a list", listing_page_count());
        return 1;
    }
    /* A cursor that cannot find an off-screen row can never scroll to it, and
       the list then stops dead a screenful in. */
    if (ui_frame_entries(screen_ui()) != listing_page_count()) {
        snprintf(note, n, "%d rows but %d registered", listing_page_count(), ui_frame_entries(screen_ui()));
        return 1;
    }
    snprintf(note, n, "%d rows, all registered", listing_page_count());
    return 0;
}

/* A cursor that walks off the panel is invisible rather than wrong-looking. */
static int t_the_cursor_stays_on_the_panel(char *note, unsigned n) {
    ui_rect body, box;
    int     i, rows;

    if (a_listing(note, n) != 0) return -1;

    rows = listing_page_count() < 24 ? listing_page_count() : 24;
    body = ui_page_body();

    for (i = 1; i < rows; i++) {
        page_step(PAD_DOWN);
        settle_scroll();

        if (!ui_frame_focus_box(screen_ui(), &box)) {
            snprintf(note, n, "row %d registered nothing to focus", i);
            return 1;
        }
        if (box.y < body.y || box.y + box.h > body.y + body.h) {
            snprintf(note, n, "row %d drew at %d..%d, outside the body %d..%d", i, box.y, box.y + box.h, body.y, body.y + body.h);
            return 1;
        }
    }
    snprintf(note, n, "all %d rows stayed inside %d..%d", rows, body.y, body.y + body.h);
    return 0;
}

static int t_the_view_holds_then_follows(char *note, unsigned n) {
    int at_top, after;

    if (a_listing(note, n) != 0) return -1;

    at_top = listing_page_scroll();
    if (at_top != 0) {
        snprintf(note, n, "the list opened already scrolled to %d", at_top);
        return 1;
    }

    page_step(PAD_DOWN);
    settle_scroll();
    if (listing_page_scroll() != 0) {
        snprintf(note, n, "one row down already scrolled the view to %d", listing_page_scroll());
        return 1;
    }

    /* Never past the last row: the cursor wraps, and would find the view at
       zero again. */
    {
        int i, last = listing_page_count() < 24 ? listing_page_count() : 24;

        for (i = 2; i < last; i++) {
            page_step(PAD_DOWN);
            page_step(0);
        }
    }
    settle_scroll();
    after = listing_page_scroll();
    if (after <= 0) {
        snprintf(note, n, "walking to the last of %d rows left the view at %d", listing_page_count(), after);
        return 1;
    }
    snprintf(note, n, "held at 0 for the first row, %d after the list", after);
    return 0;
}

static int t_square_sorts_without_moving_the_cursor(char *note, unsigned n) {
    int was_sort, has, before;

    if (a_listing(note, n) != 0) return -1;

    was_sort = listing_page_sort();
    before   = (int)ui_frame_focus(screen_ui(), &has);

    page_step(PAD_SQUARE | PAD_RIGHT);
    page_step(PAD_SQUARE);

    if (listing_page_sort() == was_sort) {
        snprintf(note, n, "square-right left the sort at %s", item_sort_label(was_sort));
        return 1;
    }
    if ((int)ui_frame_focus(screen_ui(), &has) != before) {
        snprintf(note, n, "square-right moved the cursor from %d to %d", before, (int)ui_frame_focus(screen_ui(), &has));
        return 1;
    }
    snprintf(note, n, "%s became %s, cursor stayed", item_sort_label(was_sort), item_sort_label(listing_page_sort()));
    return 0;
}

static int t_cross_opens_the_row_under_the_cursor(char *note, unsigned n) {
    if (a_listing(note, n) != 0) return -1;

    page_step(PAD_DOWN);
    settle_scroll();
    page_step(PAD_CROSS);

    if (listing_page_opened() != 1) {
        snprintf(note, n, "cross on row 1 opened %d", listing_page_opened());
        return 1;
    }
    snprintf(note, n, "opened \"%s\"", listing_page_row_name(1));
    return 0;
}

/* Opening a series as a detail page once made every season and episode on the
   server unreachable, with nothing on screen to say so. */
static int t_a_series_opens_a_listing_and_a_film_opens_a_detail(char *note, unsigned n) {
    int at = 0, rc;

    if (page_panel(note, n) != 0) return -1;
    if ((rc = open_listing(ITEM_KIND_SERIES, &at, note, n)) != 0) return rc;
    ui_frame_focus_set(screen_ui(), (ui_id)at);
    page_step(0);

    if (screen_depth() != 2) {
        snprintf(note, n, "the listing opened %d deep, wanted 2", screen_depth());
        return 1;
    }

    page_step(PAD_CROSS);
    if (strcmp(screen_top_name(), "listing") != 0) {
        snprintf(note, n, "a series opened as \"%s\" -- its seasons are unreachable", screen_top_name());
        return 1;
    }
    /* Pushed over the series, not the same page rewritten. */
    if (screen_depth() != 3) {
        snprintf(note, n, "a series opened without pushing a screen (%d deep)", screen_depth());
        return 1;
    }

    screen_back();
    page_step(0);
    if (strcmp(screen_top_name(), "listing") != 0 || screen_depth() != 2) {
        snprintf(note, n, "circle out of a season left \"%s\" %d deep", screen_top_name(), screen_depth());
        return 1;
    }

    /* Without this the check passes just as well with everything routed to a
       listing. */
    if ((rc = open_listing(ITEM_KIND_MOVIE, &at, note, n)) != 0) return rc;
    ui_frame_focus_set(screen_ui(), (ui_id)at);
    page_step(0);
    page_step(PAD_CROSS);
    if (strcmp(screen_top_name(), "detail") != 0) {
        snprintf(note, n, "row %d is \"%s\" and it opened as \"%s\", not a detail page", at, listing_page_row_name(at), screen_top_name());
        return 1;
    }
    snprintf(note, n, "series -> listing, film -> detail, circle -> level 1");
    return 0;
}

/* The rules below are the stack's and not any page's, so the screens hold
   nothing but a canary and a record of what they saw while drawing. */
#define PROBE_ARG 24

typedef struct {
    unsigned canary;
    int      frames;
} probe_state;

typedef enum { ASK_NONE = 0, ASK_PUSH, ASK_PUSH_ARG, ASK_POP, ASK_TWICE } probe_ask;

static probe_ask g_ask;
static char      g_mid_top[16];
static int       g_mid_depth;
static void     *g_mid_state;
static int       g_mid_canary_ok;
static int       g_mid_entries_after;
static char      g_b_arg[PROBE_ARG];
static unsigned  g_b_len;

extern const screen_def probe_a_screen;
extern const screen_def probe_b_screen;

static void probe_a_enter(void *st, const void *arg, unsigned arg_len) {
    probe_state *p = (probe_state *)st;

    (void)arg;
    (void)arg_len;
    p->canary = 0xC0FFEEu;
}

/* Over the stack the argument was built on: a push that kept a pointer to a
   caller's local instead of copying it reads this back. */
static void scribble(void) {
    volatile unsigned char over[256];
    int                    i;

    for (i = 0; i < (int)sizeof(over); i++) over[i] = 0xEEu;
}

static void probe_a_frame(ui_frame *ui, void *st) {
    probe_state *p   = (probe_state *)st;
    probe_ask    ask = g_ask;

    p->frames++;
    (void)ui_touch(ui, 0, ui_rect_make(0, 0, 40, 20));

    g_ask = ASK_NONE;
    switch (ask) {
    case ASK_PUSH: screen_push(&probe_b_screen); break;
    case ASK_PUSH_ARG: {
        char msg[PROBE_ARG];
        int  i;

        for (i = 0; i < PROBE_ARG; i++) msg[i] = (char)(0x40 + i);
        screen_push_with(&probe_b_screen, msg, (unsigned)sizeof(msg));
        scribble();
        break;
    }
    case ASK_POP: screen_pop(); break;
    case ASK_TWICE:
        screen_push(&probe_b_screen);
        screen_pop();
        break;
    default: return;
    }

    snprintf(g_mid_top, sizeof(g_mid_top), "%s", screen_top_name());
    g_mid_depth     = screen_depth();
    g_mid_state     = screen_top_state();
    g_mid_canary_ok = (p->canary == 0xC0FFEEu);

    /* The frame goes on drawing after the ask, which is why the move waits. */
    (void)ui_touch(ui, 1, ui_rect_make(0, 20, 40, 20));
    g_mid_entries_after = ui_frame_entries(ui);
}

static void probe_b_enter(void *st, const void *arg, unsigned arg_len) {
    (void)st;
    g_b_len = arg_len;
    memset(g_b_arg, 0, sizeof(g_b_arg));
    if (arg && arg_len && arg_len <= sizeof(g_b_arg)) memcpy(g_b_arg, arg, arg_len);
}

static void probe_b_frame(ui_frame *ui, void *st) {
    (void)st;
    (void)ui_touch(ui, 0, ui_rect_make(0, 0, 40, 20));
}

const screen_def probe_a_screen = {"probe-a", sizeof(probe_state), 0, probe_a_enter, probe_a_frame, 0, 0, 0};
const screen_def probe_b_screen = {"probe-b", sizeof(probe_state), PROBE_ARG, probe_b_enter, probe_b_frame, 0, 0, 0};

static int probe_ready(char *note, unsigned n) {
    if (page_panel(note, n) != 0) return -1;
    g_ask               = ASK_NONE;
    g_mid_top[0]        = 0;
    g_mid_depth         = 0;
    g_mid_state         = 0;
    g_mid_canary_ok     = 0;
    g_mid_entries_after = 0;
    memset(g_b_arg, 0, sizeof(g_b_arg));
    g_b_len = 0;
    screen_reset(&probe_a_screen);
    return 0;
}

/* A navigation asked while drawing happens after the frame: applied inside
   it, a pop once released the arena region the popping screen was still
   drawing out of, and a push ran the new screen's enter() mid-layout. */
static int t_a_push_asked_while_drawing_lands_after_the_frame(char *note, unsigned n) {
    void *before;

    if (probe_ready(note, n) != 0) return -1;
    page_step(0);
    before = screen_top_state();

    g_ask = ASK_PUSH;
    page_step(0);

    if (strcmp(g_mid_top, "probe-a") != 0 || g_mid_depth != 1) {
        snprintf(note, n, "mid-frame the top was \"%s\" %d deep -- the push landed inside the frame", g_mid_top, g_mid_depth);
        return 1;
    }
    if (g_mid_state != before) {
        snprintf(note, n, "the pusher's own state moved under it mid-frame");
        return 1;
    }
    if (!g_mid_canary_ok) {
        snprintf(note, n, "the pusher's state was overwritten before its frame ended");
        return 1;
    }
    if (g_mid_entries_after != 2) {
        snprintf(note, n, "the pusher registered %d boxes after asking, wanted 2", g_mid_entries_after);
        return 1;
    }
    if (strcmp(screen_top_name(), "probe-b") != 0 || screen_depth() != 2) {
        snprintf(note, n, "after the frame the top is \"%s\" %d deep, wanted probe-b 2 deep", screen_top_name(), screen_depth());
        return 1;
    }
    snprintf(note, n, "probe-a drew 2 boxes still on top, and probe-b was on top %d deep afterwards", screen_depth());
    return 0;
}

static int t_a_pop_asked_while_drawing_lands_after_the_frame(char *note, unsigned n) {
    if (probe_ready(note, n) != 0) return -1;
    page_step(0);
    g_ask = ASK_PUSH;
    page_step(0);
    page_step(0);

    screen_push(&probe_a_screen); /* outside a frame: at once, see below */
    if (screen_depth() != 3) {
        snprintf(note, n, "a push from outside a frame left the stack %d deep", screen_depth());
        return 1;
    }
    page_step(0);
    g_ask = ASK_POP;
    page_step(0);

    if (strcmp(g_mid_top, "probe-a") != 0 || g_mid_depth != 3) {
        snprintf(note, n, "mid-frame the top was \"%s\" %d deep -- the pop landed inside the frame it was asked from", g_mid_top,
                 g_mid_depth);
        return 1;
    }
    if (!g_mid_canary_ok) {
        snprintf(note, n, "the popping screen's state was released while it was still drawing out of it");
        return 1;
    }
    if (screen_depth() != 2 || strcmp(screen_top_name(), "probe-b") != 0) {
        snprintf(note, n, "after the frame the top is \"%s\" %d deep, wanted probe-b 2 deep", screen_top_name(), screen_depth());
        return 1;
    }
    snprintf(note, n, "the popping screen drew its whole frame 3 deep and the stack was 2 deep after it");
    return 0;
}

/* Outside a frame there is nothing drawing to protect, and a caller that had
   to run a frame first could not put a screen up at start-up. */
static int t_a_reset_outside_a_frame_lands_at_once(char *note, unsigned n) {
    if (probe_ready(note, n) != 0) return -1;
    page_step(0);
    g_ask = ASK_PUSH;
    page_step(0);
    if (screen_depth() != 2) {
        snprintf(note, n, "the stack was %d deep before the reset, wanted 2", screen_depth());
        return 1;
    }

    screen_reset(&probe_b_screen);
    if (screen_depth() != 1 || strcmp(screen_top_name(), "probe-b") != 0) {
        snprintf(note, n, "after the reset the top is \"%s\" %d deep before any frame was drawn", screen_top_name(), screen_depth());
        return 1;
    }
    snprintf(note, n, "a reset from 2 deep left probe-b alone on the stack with no frame in between");
    return 0;
}

/* Two is a screen that has lost track of what it is doing, so the second is
   refused rather than run after the first. */
static int t_one_navigation_a_frame(char *note, unsigned n) {
    if (probe_ready(note, n) != 0) return -1;
    page_step(0);

    g_ask = ASK_TWICE; /* a push, then a pop */
    page_step(0);

    if (screen_depth() != 2 || strcmp(screen_top_name(), "probe-b") != 0) {
        snprintf(note, n, "push-then-pop in one frame left \"%s\" %d deep -- both were applied", screen_top_name(), screen_depth());
        return 1;
    }
    snprintf(note, n, "the push was kept and the pop after it refused: probe-b, %d deep", screen_depth());
    return 0;
}

/* The push does not happen until the pusher's frame has returned, and by then
   its local is gone. */
static int t_a_pushed_argument_survives_its_pushers_stack(char *note, unsigned n) {
    int i;

    if (probe_ready(note, n) != 0) return -1;
    page_step(0);

    g_ask = ASK_PUSH_ARG;
    page_step(0);

    if (g_b_len != PROBE_ARG) {
        snprintf(note, n, "the pushed screen was handed %u bytes, wanted %d", g_b_len, PROBE_ARG);
        return 1;
    }
    for (i = 0; i < PROBE_ARG; i++)
        if (g_b_arg[i] != (char)(0x40 + i)) {
            snprintf(note, n, "byte %d of the argument arrived as 0x%02X, wanted 0x%02X", i, (unsigned char)g_b_arg[i],
                     (unsigned char)(0x40 + i));
            return 1;
        }
    snprintf(note, n, "all %d bytes arrived intact from a local the pusher's frame had left", PROBE_ARG);
    return 0;
}

/* Circle read outside the frame once popped the whole listing out from under
   a held square. */
static int t_square_held_keeps_circle_from_popping(char *note, unsigned n) {
    if (a_listing(note, n) != 0) return -1;
    if (screen_depth() != 2 || strcmp(screen_top_name(), "listing") != 0) {
        snprintf(note, n, "the listing opened as \"%s\" %d deep, wanted listing 2 deep", screen_top_name(), screen_depth());
        return 1;
    }

    page_step(PAD_SQUARE | PAD_CIRCLE);
    if (screen_depth() != 2 || strcmp(screen_top_name(), "listing") != 0) {
        snprintf(note, n, "square-circle left \"%s\" %d deep -- the modifier did not keep the button", screen_top_name(), screen_depth());
        return 1;
    }

    page_step(PAD_CIRCLE);
    if (screen_depth() != 1) {
        snprintf(note, n, "circle alone left the stack %d deep on \"%s\"", screen_depth(), screen_top_name());
        return 1;
    }
    snprintf(note, n, "square-circle held at 2 deep, circle alone came back to 1");
    return 0;
}

/* A listing cut off at the limit reads exactly like a short one, so the bar
   says how many of how many -- only when they differ, because "78 of 78" on
   every folder is noise. */
static int t_a_truncated_listing_says_how_many_of_how_many(char *note, unsigned n) {
    char      want[24];
    lib_items li;

    if (a_listing(note, n) != 0) return -1;
    page_step(0);
    listing_page_items(&li);

    if (li.total <= listing_page_count()) {
        if (strstr(listing_page_status(), " of ")) {
            snprintf(note, n, "an untruncated listing says \"%s\"", listing_page_status());
            return 1;
        }
        snprintf(note, n, "\"%s\" for all %d -- no library here is big enough to truncate", listing_page_status(), listing_page_count());
        return 0;
    }
    snprintf(want, sizeof(want), " of %d", li.total);
    if (!strstr(listing_page_status(), want)) {
        snprintf(note, n, "%d held and %d sent, and the bar says \"%s\"", li.total, listing_page_count(), listing_page_status());
        return 1;
    }
    snprintf(note, n, "\"%s\"", listing_page_status());
    return 0;
}

/* Overwriting the status with "Loading" left the sort arrow beside it, so the
   bar said "sorted by Loading". */
static int t_the_sort_label_survives_a_fetch(char *note, unsigned n) {
    char      quiet[64], busy[64];
    unsigned  t0;
    lib_items li;

    if (a_listing(note, n) != 0) return -1;
    page_step(0);
    snprintf(quiet, sizeof(quiet), "%s", listing_page_status());

    /* Reload: the status is written in the frame the fetch goes out. */
    page_step(PAD_TRIANGLE);
    snprintf(busy, sizeof(busy), "%s", listing_page_status());
    t0 = platform_clock_us();
    for (listing_page_items(&li); li.state == LIB_BUSY && platform_clock_us() - t0 < 30000000u; listing_page_items(&li)) {
        page_step(0);
        platform_sleep_us(10000);
    }

    if (!quiet[0]) {
        snprintf(note, n, "a settled listing named no sort at all");
        return 1;
    }
    if (strcmp(quiet, busy) != 0) {
        snprintf(note, n, "the bar said \"%s\" settled and \"%s\" fetching", quiet, busy);
        return 1;
    }
    snprintf(note, n, "\"%s\" either way", quiet);
    return 0;
}

void test_listing_page_register(void) {
    selftest_add("listing", "the listing shows what it was given", t_the_listing_shows_what_it_was_given);
    selftest_add("listing", "the cursor stays on the panel", t_the_cursor_stays_on_the_panel);
    selftest_add("listing", "the view holds then follows", t_the_view_holds_then_follows);
    selftest_add("listing", "square sorts without moving the cursor", t_square_sorts_without_moving_the_cursor);
    selftest_add("listing", "a series opens a listing and a film opens a detail", t_a_series_opens_a_listing_and_a_film_opens_a_detail);
    selftest_add("listing", "a truncated listing says how many of how many", t_a_truncated_listing_says_how_many_of_how_many);
    selftest_add("listing", "the sort label survives a fetch", t_the_sort_label_survives_a_fetch);
    selftest_add("listing", "cross opens the row under the cursor", t_cross_opens_the_row_under_the_cursor);
    selftest_add("listing", "square held keeps circle from popping", t_square_held_keeps_circle_from_popping);
    selftest_add("screen", "a push asked while drawing lands after the frame", t_a_push_asked_while_drawing_lands_after_the_frame);
    selftest_add("screen", "a pop asked while drawing lands after the frame", t_a_pop_asked_while_drawing_lands_after_the_frame);
    selftest_add("screen", "a reset outside a frame lands at once", t_a_reset_outside_a_frame_lands_at_once);
    selftest_add("screen", "one navigation a frame", t_one_navigation_a_frame);
    selftest_add("screen", "a pushed argument survives its pusher's stack", t_a_pushed_argument_survives_its_pushers_stack);
}
