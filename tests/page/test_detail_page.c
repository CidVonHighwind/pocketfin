#include "page/detail.h"

#include "view/element.h"
#include "model/catalog.h"
#include "port/gfx.h"
#include "port/input.h"
#include "port/platform.h"
#include "view/layout.h"
#include "page/home.h"
#include "page/listing.h"
#include "view/page_chrome.h"
#include "view/stack.h"
#include "tools/remote.h"
#include "tools/selftest.h"
#include "test_page.h"
#include "tools/shot.h"

#include "../model/test_catalog.h"

#include <stdio.h>
#include <string.h>

static int g_row;

/* Opened from the list, so the item is the row under the cursor rather than
   one handed in. */
static int a_detail(char *note, unsigned n) {
    char     parent[JF_ID_LEN], title[ITEM_NAME_LEN];
    int      count = 0;
    unsigned t0;

    if (page_panel(note, n) != 0) return -1;
    if (real_listing(ITEM_KIND_MOVIE, parent, title, &count, &g_row, note, n) != 0) return -1;
    screen_reset(&home_page_screen);
    listing_page_show(parent, title);
    t0 = platform_clock_us();
    while (listing_page_count() <= g_row && platform_clock_us() - t0 < 30000000u) {
        page_step(0);
        platform_sleep_us(10000);
    }
    ui_frame_focus_set(screen_ui(), (ui_id)g_row);
    page_step(0);
    page_step(PAD_CROSS);
    page_step(0);

    if (strcmp(screen_top_name(), "detail") != 0) {
        snprintf(note, n, "cross on a film left the stack on \"%s\"", screen_top_name());
        return 1;
    }
    return 0;
}

/* The worker answers between frames. */
static int synopsis_landed(void) {
    char     said[REMOTE_STATE_MAX];
    unsigned t0 = platform_clock_us();

    for (;;) {
        page_step(0);
        screen_describe(said, sizeof(said));
        if (strstr(said, "synopsis: yes")) return 1;
        if (platform_clock_us() - t0 > 30000000u) return 0;
        platform_sleep_us(10000);
    }
}

static int t_the_detail_page_draws(char *note, unsigned n) {
    int rc = a_detail(note, n);

    if (rc != 0) return rc;

    page_step(0);
    if (shot_write("detail.ppm") != 0) {
        snprintf(note, n, "the picture did not land");
        return 1;
    }
    return 0;
}

/* A page with more on it than the body holds draws a plausible screen with its
   synopsis missing and says nothing about it. */
static int t_the_page_fits_the_body(char *note, unsigned n) {
    int rc = a_detail(note, n), used, body;

    if (rc != 0) return rc;

    used = ui_used(&screen_ui()->layout);
    body = ui_here(&screen_ui()->layout).h;
    if (used > body) {
        snprintf(note, n, "the page puts %d px into a %d px body -- %d over", used, body, used - body);
        return 1;
    }
    snprintf(note, n, "%d px of page in a %d px body", used, body);
    return 0;
}

/* Nothing here presses it: a mark writes to the real library with no undo. */
static int t_the_mark_button_says_what_the_item_is(char *note, unsigned n) {
    item it;
    int  rc = a_detail(note, n);

    if (rc != 0) return rc;

    memset(&it, 0, sizeof(it));
    snprintf(it.id, sizeof(it.id), "one");
    snprintf(it.name, sizeof(it.name), "Something");
    it.kind      = ITEM_KIND_MOVIE;
    it.season_no = it.episode_no = -1;

    it.played = 1;
    detail_page_show(&it);
    page_step(0);
    if (strcmp(detail_page_mark_label(), "Unwatch") != 0) {
        snprintf(note, n, "a watched item's button reads \"%s\"", detail_page_mark_label());
        return 1;
    }

    it.played = 0;
    detail_page_show(&it);
    page_step(0);
    if (strcmp(detail_page_mark_label(), "Watched") != 0) {
        snprintf(note, n, "an unwatched item's button reads \"%s\"", detail_page_mark_label());
        return 1;
    }
    snprintf(note, n, "\"Unwatch\" for a watched item, \"Watched\" otherwise");
    return 0;
}

static int t_circle_returns_to_the_listing(char *note, unsigned n) {
    int rc = a_detail(note, n);

    if (rc != 0) return rc;

    screen_back();
    page_step(0);
    if (strcmp(screen_top_name(), "listing") != 0) {
        snprintf(note, n, "back from detail landed on \"%s\"", screen_top_name());
        return 1;
    }
    return 0;
}

/* Only an IN-FRAME push is copied through the deferral, so an argument the
   stack will not take shows up in the app and in no check that pushes from
   outside one. */
static int t_the_item_arrives_from_the_row(char *note, unsigned n) {
    char        said[REMOTE_STATE_MAX];
    const char *want;
    int         rc = a_detail(note, n);

    if (rc != 0) return rc;

    want = listing_page_row_name(g_row);
    if (!want[0]) {
        snprintf(note, n, "the row opened has no name");
        return -1;
    }

    screen_describe(said, sizeof(said));
    if (!strstr(said, want)) {
        snprintf(note, n, "opened on \"%s\" and the page does not name it", want);
        return 1;
    }
    snprintf(note, n, "\"%s\" arrived whole", want);
    return 0;
}

/* One buffer holds whatever overview was fetched last, so a page opened over
   another one showed the wrong film's synopsis until its own landed. */
static int t_the_synopsis_belongs_to_the_item(char *note, unsigned n) {
    char said[REMOTE_STATE_MAX];
    item a;
    int  rc = a_detail(note, n);

    if (rc != 0) return rc;
    if (!synopsis_landed()) {
        snprintf(note, n, "the film's own synopsis never arrived");
        return 1;
    }

    memset(&a, 0, sizeof(a));
    snprintf(a.id, sizeof(a.id), "a-different-film");
    snprintf(a.name, sizeof(a.name), "Another Film");
    a.kind      = ITEM_KIND_MOVIE;
    a.season_no = a.episode_no = -1;
    detail_page_show(&a);
    page_step(0);
    screen_describe(said, sizeof(said));
    if (!strstr(said, "synopsis: no")) {
        snprintf(note, n, "a second item was handed the first one's synopsis");
        return 1;
    }
    snprintf(note, n, "one item's overview, and the next item gets none of it");
    return 0;
}

/* Not whatever the frame happened to keep: a control id is a small integer
   each page picks, so the listing's row 2 and this page's Play can collide on
   the same number. */
static int t_the_page_opens_on_play(char *note, unsigned n) {
    int   has = 0;
    ui_id id;

    if (a_detail(note, n) != 0) return -1;

    id = ui_frame_focus(screen_ui(), &has);
    if (!has) {
        snprintf(note, n, "the page opened with nothing selected");
        return 1;
    }
    if (id != DETAIL_ID_PLAY) {
        snprintf(note, n, "the page opened on %u, wanted play (%u)", (unsigned)id, (unsigned)DETAIL_ID_PLAY);
        return 1;
    }
    snprintf(note, n, "play (%u) has the cursor on arrival", (unsigned)id);
    return 0;
}

/* The marks and the resume point are asked for again on the way back, and
   asking once blanked the synopsis for seconds on the console. */
static int t_returning_from_a_film_keeps_the_synopsis(char *note, unsigned n) {
    char said[REMOTE_STATE_MAX];
    int  rc = a_detail(note, n);

    if (rc != 0) return rc;
    if (!synopsis_landed()) {
        snprintf(note, n, "the page never had one to keep");
        return 1;
    }

    page_step(PAD_CROSS);
    page_step(0);
    if (strcmp(screen_top_name(), "player") != 0) {
        snprintf(note, n, "cross on Play left the stack on \"%s\"", screen_top_name());
        return 1;
    }
    page_step(PAD_CIRCLE);
    page_step(0);
    if (strcmp(screen_top_name(), "detail") != 0) {
        snprintf(note, n, "circle out of the player left the stack on \"%s\"", screen_top_name());
        return 1;
    }

    screen_describe(said, sizeof(said));
    if (!strstr(said, "synopsis: yes")) {
        snprintf(note, n, "the page came back without the synopsis it went in with");
        return 1;
    }
    snprintf(note, n, "in and out of a run with the paragraph still on it");
    return 0;
}

void test_detail_page_register(void) {
    selftest_add("detail", "the item arrives from the row", t_the_item_arrives_from_the_row);
    selftest_add("detail", "the detail page draws", t_the_detail_page_draws);
    selftest_add("detail", "the page fits the body", t_the_page_fits_the_body);
    selftest_add("detail", "the mark button says what the item is", t_the_mark_button_says_what_the_item_is);
    selftest_add("detail", "the page opens on play", t_the_page_opens_on_play);
    selftest_add("detail", "circle returns to the listing", t_circle_returns_to_the_listing);
    selftest_add("detail", "the synopsis belongs to the item", t_the_synopsis_belongs_to_the_item);
    selftest_add("detail", "returning from a film keeps the synopsis", t_returning_from_a_film_keeps_the_synopsis);
}
