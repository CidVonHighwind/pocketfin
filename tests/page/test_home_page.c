#include "base/align.h"
#include "model/catalog.h"
#include "port/platform.h"
#include "page/home.h"

#include "page/detail.h"
#include "port/gfx.h"
#include "port/input.h"
#include "view/stack.h"
#include "view/scroll.h"
#include "tools/selftest.h"
#include "tools/shot.h"

#include "test_page.h"
#include "../model/test_catalog.h"

#include <stdio.h>
#include <string.h>

static int panel_ready(char *note, unsigned n) {
    if (page_panel(note, n) != 0) return -1;
    if (real_library(note, n) != 0) return -1;
    screen_reset(&home_page_screen);
    return 0;
}

static int t_the_home_page_draws_both_rails(char *note, unsigned n) {
    int pass;

    if (panel_ready(note, n) != 0) return -1;
    if (home_page_library_count() <= 0 || home_page_watching_count() <= 0) {
        snprintf(note, n, "%d libraries, %d watching -- the server needs both", home_page_library_count(), home_page_watching_count());
        return -1;
    }
    for (pass = 0; pass < 2; pass++) page_step(0);
    if (shot_write("home.ppm") != 0) {
        snprintf(note, n, "the picture did not land");
        return 1;
    }
    snprintf(note, n, "%d libraries, %d watching", home_page_library_count(), home_page_watching_count());
    return 0;
}

static int t_right_then_down_crosses_rails(char *note, unsigned n) {
    if (panel_ready(note, n) != 0) return -1;

    page_step(0);
    if (ui_frame_focus(screen_ui(), 0) != 0) {
        snprintf(note, n, "fresh page focused %u, wanted library 0", ui_frame_focus(screen_ui(), 0));
        return 1;
    }

    page_step(PAD_RIGHT);
    if (ui_frame_focus(screen_ui(), 0) != 1) {
        snprintf(note, n, "right from library 0 gave %u, wanted library 1", ui_frame_focus(screen_ui(), 0));
        return 1;
    }

    page_step(PAD_DOWN);
    if ((int)ui_frame_focus(screen_ui(), 0) < HOME_RAIL_WATCHING_BASE) {
        snprintf(note, n, "down from the Libraries rail gave %u, still in that rail", ui_frame_focus(screen_ui(), 0));
        return 1;
    }
    return 0;
}

/* Circle returns to that same card, not the rail's start. */
static int t_cross_on_watching_opens_that_item_circle_returns(char *note, unsigned n) {
    int   pass;
    ui_id id;

    if (panel_ready(note, n) != 0) return -1;
    if (home_page_watching_count() < 2) {
        snprintf(note, n, "%d cards in Continue Watching -- the check needs two", home_page_watching_count());
        return -1;
    }

    /* A box has to be registered before focus_index can find it. */
    page_step(0);
    ui_frame_focus_index(screen_ui(), home_page_library_count() + 1);
    id = ui_frame_focus(screen_ui(), 0);
    if ((int)id != HOME_RAIL_WATCHING_BASE + 1) {
        snprintf(note, n, "focus_index landed on %u, wanted watching card 1 (id %d)", id, HOME_RAIL_WATCHING_BASE + 1);
        return 1;
    }

    page_step(PAD_CROSS);
    if (strcmp(screen_top_name(), "detail") != 0) {
        snprintf(note, n, "cross gave top \"%s\", wanted \"detail\"", screen_top_name());
        return 1;
    }
    for (pass = 0; pass < 2; pass++) page_step(0);
    if (shot_write("home-detail.ppm") != 0) {
        snprintf(note, n, "the detail picture did not land");
        return 1;
    }

    page_step(PAD_CIRCLE);
    screen_back();
    if (strcmp(screen_top_name(), "home") != 0) {
        snprintf(note, n, "circle gave top \"%s\", wanted back to \"home\"", screen_top_name());
        return 1;
    }
    if (ui_frame_focus(screen_ui(), 0) != id) {
        snprintf(note, n, "back on home focused %u, wanted the same card %u", ui_frame_focus(screen_ui(), 0), id);
        return 1;
    }
    return 0;
}

static int t_stepping_right_reaches_a_card_that_started_off_screen(char *note, unsigned n) {
    int watch_n, steps;

    if (panel_ready(note, n) != 0) return -1;
    /* Next Up, not Continue Watching: the rail under test has to be one that
       overflows. */
    watch_n = home_page_next_count();
    if (watch_n * (WATCH_CARD_BOX + 8) <= GFX_W - 8) {
        snprintf(note, n, "%d Next Up cards fit the panel without scrolling", watch_n);
        return -1;
    }

    page_step(0); /* a box has to be registered before focus_index can find it */
    ui_frame_focus_index(screen_ui(), home_page_library_count() + home_page_watching_count());
    if ((int)ui_frame_focus(screen_ui(), 0) != HOME_RAIL_NEXT_BASE) {
        snprintf(note, n, "focus_index landed on %u, wanted the first Next Up card", ui_frame_focus(screen_ui(), 0));
        return 1;
    }
    if (home_page_next_scroll() != 0) {
        snprintf(note, n, "the rail is already scrolled (%d) at its own first card", home_page_next_scroll());
        return 1;
    }

    /* One step draws with the offset from the previous frame's focus, then
       moves focus by exactly one. */
    for (steps = 0; steps < watch_n - 1; steps++) page_step(PAD_RIGHT);
    /* The offset eases rather than snapping. */
    for (steps = 0; steps < UI_SCROLL_SETTLE; steps++) page_step(0);

    if ((int)ui_frame_focus(screen_ui(), 0) != HOME_RAIL_NEXT_BASE + watch_n - 1) {
        snprintf(note, n, "%d right presses from the first card landed on %u, wanted the last (id %d)", watch_n - 1,
                 ui_frame_focus(screen_ui(), 0), HOME_RAIL_NEXT_BASE + watch_n - 1);
        return 1;
    }
    /* The picker reaches an off-screen box on its own, geometrically, so only
       the offset having moved proves the rail scrolled. */
    if (home_page_next_scroll() <= 0) {
        snprintf(note, n, "reached the last card with scroll offset %d -- the picker found it without the rail ever scrolling",
                 home_page_next_scroll());
        return 1;
    }
    if (shot_write("home-scrolled.ppm") != 0) {
        snprintf(note, n, "the scrolled picture did not land");
        return 1;
    }
    snprintf(note, n, "%d cards, scrolled %d px to show the last", watch_n, home_page_next_scroll());
    return 0;
}

static int t_cross_on_a_library_opens_that_library(char *note, unsigned n) {
    const char *want;

    if (panel_ready(note, n) != 0) return -1;
    page_step(0);
    ui_frame_focus_index(screen_ui(), 1);
    want = home_page_library_name(1);

    /* A library is a folder: it opens a listing, never detail. */
    page_step(PAD_CROSS);
    if (strcmp(screen_top_name(), "listing") != 0) {
        snprintf(note, n, "cross on a library gave top \"%s\", wanted \"listing\"", screen_top_name());
        return 1;
    }
    snprintf(note, n, "opened \"%s\" as a listing", want);
    return 0;
}

static int t_the_page_scrolls_to_the_last_rail(char *note, unsigned n) {
    int i;

    if (panel_ready(note, n) != 0) return -1;
    page_step(0);
    ui_frame_focus_index(screen_ui(), 0);

    for (i = 0; i < 3; i++) {
        page_step(PAD_DOWN);
        page_step(0);
    }
    for (i = 0; i < UI_SCROLL_SETTLE; i++) page_step(0);

    if (home_page_scroll() <= 0) {
        snprintf(note, n, "focus reached the last rail and the page never scrolled (%d)", home_page_scroll());
        return 1;
    }
    if (shot_write("home-vscroll.ppm") != 0) {
        snprintf(note, n, "the picture did not land");
        return 1;
    }
    snprintf(note, n, "the page scrolled %d px to the last rail", home_page_scroll());
    return 0;
}

/* A card's width follows its artwork, so with no picture yet it must take the
   nominal width: cards must not resize under the cursor as pictures arrive. */
static int t_a_loading_rail_keeps_its_card_width(char *note, unsigned n) {
    int i, count;

    if (panel_ready(note, n) != 0) return -1;
    page_step(0);
    count = home_page_watching_count();
    if (count <= 0) {
        snprintf(note, n, "no watching cards to measure");
        return -1;
    }
    for (i = 0; i < count; i++) {
        int drew = home_page_watching_card_w(i);

        if (drew != WATCH_CARD_BOX) {
            snprintf(note, n, "card %d drew %d px while loading, wanted the nominal %d", i, drew, WATCH_CARD_BOX);
            return 1;
        }
    }
    snprintf(note, n, "%d cards, all %d px while loading", count, WATCH_CARD_BOX);
    return 0;
}

/* The focus id of the first card of `kind` on a rail below the libraries, or
   -1 when none holds one. */
static int card_of(int kind) {
    static const struct {
        lib_rail rail;
        int      base;
    } RAILS[] = {
        {LIB_RAIL_RESUME, HOME_RAIL_WATCHING_BASE}, {LIB_RAIL_NEXT, HOME_RAIL_NEXT_BASE}, {LIB_RAIL_LATEST, HOME_RAIL_RECENT_BASE}};
    int      r, i;
    lib_home home;

    library_home(&home);
    for (r = 0; r < 3; r++)
        for (i = 0; i < home.n[RAILS[r].rail]; i++)
            if ((int)home.rail[RAILS[r].rail][i].kind == kind) return RAILS[r].base + i;
    return -1;
}

/* A card opens by what it is, on every rail and not only the libraries: a
   series in Recently Added once opened a detail page instead of a listing,
   leaving its seasons and episodes unreachable. */
static int t_a_rail_card_opens_by_kind(char *note, unsigned n) {
    int series, film;

    if (panel_ready(note, n) != 0) return -1;
    series = card_of(ITEM_KIND_SERIES);
    film   = card_of(ITEM_KIND_MOVIE);
    if (series < 0 || film < 0) {
        snprintf(note, n, "no rail holds both a series and a film");
        return -1;
    }

    page_step(0); /* a box has to be registered before focus_set can find it */
    ui_frame_focus_set(screen_ui(), (ui_id)series);
    page_step(PAD_CROSS);
    if (strcmp(screen_top_name(), "listing") != 0) {
        snprintf(note, n, "a series card opened as \"%s\" -- its seasons are unreachable", screen_top_name());
        return 1;
    }

    /* Without this the check passes just as well with everything routed to a
       listing. */
    if (panel_ready(note, n) != 0) return -1;
    page_step(0);
    ui_frame_focus_set(screen_ui(), (ui_id)film);
    page_step(PAD_CROSS);
    if (strcmp(screen_top_name(), "detail") != 0) {
        snprintf(note, n, "a film card opened as \"%s\", not a detail page", screen_top_name());
        return 1;
    }
    snprintf(note, n, "series -> listing, film -> detail, from the rails");
    return 0;
}

void test_home_page_register(void) {
    selftest_add("home_page", "the page scrolls to the last rail", t_the_page_scrolls_to_the_last_rail);
    selftest_add("home_page", "a loading rail keeps its card width", t_a_loading_rail_keeps_its_card_width);
    selftest_add("home_page", "the home page draws both rails", t_the_home_page_draws_both_rails);
    selftest_add("home_page", "right then down crosses rails", t_right_then_down_crosses_rails);
    selftest_add("home_page", "cross on watching opens that item, circle returns", t_cross_on_watching_opens_that_item_circle_returns);
    selftest_add("home_page", "stepping right reaches an off-screen card", t_stepping_right_reaches_a_card_that_started_off_screen);
    selftest_add("home_page", "cross on a library opens that library", t_cross_on_a_library_opens_that_library);
    selftest_add("home_page", "a rail card opens by kind", t_a_rail_card_opens_by_kind);
}
