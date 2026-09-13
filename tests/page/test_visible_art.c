/* Every page that shows artwork walks all of its items, so focus can move onto
 * something not on screen yet, and the scroll clips the drawing, not the loop.
 * A loop that asks the picture store for an item off the panel still looks
 * right: it just pulls the whole folder over 802.11b and holds every slot of
 * a store that has two dozen. */

#include "model/posters.h"

#include "model/catalog.h"
#include "page/home.h"
#include "page/listing.h"
#include "port/input.h"
#include "port/platform.h"
#include "tools/selftest.h"
#include "view/stack.h"

#include "test_page.h"
#include "../model/test_catalog.h"

#include <stdio.h>
#include <string.h>

#define SEEN_MAX 96

static char g_seen[SEEN_MAX][40];
static int  g_seen_n;

/* Runs on the store's worker, so g_seen is only read once the page has gone
   quiet. */
static int counting_source(const char *id, int out_w, int out_h, unsigned short *dst, int dst_w, int dst_h, int *got_w, int *got_h) {
    int i;

    for (i = 0; i < g_seen_n; i++)
        if (strcmp(g_seen[i], id) == 0) break;
    if (i == g_seen_n && g_seen_n < SEEN_MAX) snprintf(g_seen[g_seen_n++], sizeof(g_seen[0]), "%s", id);

    memset(dst, 0, (size_t)dst_w * (size_t)dst_h * sizeof(*dst));
    if (got_w) *got_w = out_w > dst_w ? dst_w : out_w;
    if (got_h) *got_h = out_h > dst_h ? dst_h : out_h;
    return 0;
}

static int store_up(char *note, unsigned n) {
    posters_stop();
    g_seen_n = 0;
    if (posters_start(counting_source, 0, 0) != 0) {
        snprintf(note, n, "the picture store would not start");
        return -1;
    }
    if (page_panel(note, n) != 0 || real_library(note, n) != 0) {
        posters_stop();
        return -1;
    }
    return 0;
}

/* Until quiet rather than a fixed count, which would have to fit the slowest
   machine and be dead time on every other. */
static int settled(void) {
    int frames, quiet = 0, last = -1;

    for (frames = 0; frames < 400 && quiet < 12; frames++) {
        page_step(0);
        poster_frame();
        if (g_seen_n == last) {
            quiet++;
        } else {
            quiet = 0;
            last  = g_seen_n;
        }
        platform_sleep_us(500);
    }
    return g_seen_n;
}

/* The biggest real library, over the front page as the application opens it. */
static int open_real(char *note, unsigned n) {
    char     parent[JF_ID_LEN], title[ITEM_NAME_LEN];
    int      count = 0;
    unsigned t0;

    if (real_listing(-1, parent, title, &count, 0, note, n) != 0) return -1;
    screen_reset(&home_page_screen);
    listing_page_show(parent, title);
    t0 = platform_clock_us();
    while (listing_page_count() <= 0 && platform_clock_us() - t0 < 30000000u) {
        page_step(0);
        platform_sleep_us(10000);
    }
    return 0;
}

/* Two rails and part of a third fit on a 480x272 panel. */
static int t_the_front_page_fetches_what_it_shows(char *note, unsigned n) {
    int fetched, cards;

    if (store_up(note, n) != 0) return -1;
    screen_reset(&home_page_screen);

    fetched = settled();
    cards   = home_page_library_count() + home_page_watching_count() + home_page_next_count();
    posters_stop();

    if (cards < 12) {
        snprintf(note, n, "only %d cards on the server -- too few for the panel to be the thing that limits the fetching", cards);
        return -1;
    }
    if (fetched >= cards) {
        snprintf(note, n, "opening the page fetched %d pictures for %d cards; it is loading the library, not the panel", fetched, cards);
        return 1;
    }
    snprintf(note, n, "%d pictures for %d cards", fetched, cards);
    return 0;
}

/* The harder case: the whole folder is one column that only the vertical clip
   keeps off the panel. */
static int t_a_listing_fetches_what_it_shows(char *note, unsigned n) {
    int fetched, rows;

    if (store_up(note, n) != 0) return -1;
    if (open_real(note, n) != 0) {
        posters_stop();
        return -1;
    }

    fetched = settled();
    rows    = listing_page_count();
    posters_stop();

    if (rows < 12) {
        snprintf(note, n, "only %d rows in the fixture -- fewer than fill the panel, so this proves nothing", rows);
        return -1;
    }
    if (fetched >= rows) {
        snprintf(note, n, "opening the folder fetched %d pictures for %d rows; it is loading the folder, not the panel", fetched, rows);
        return 1;
    }
    snprintf(note, n, "%d pictures for %d rows", fetched, rows);
    return 0;
}

/* The count above is a window, not a cap that happens to sit near it. */
static int t_scrolling_a_listing_fetches_the_rest(char *note, unsigned n) {
    int opened, walked, rows, i;

    if (store_up(note, n) != 0) return -1;
    if (open_real(note, n) != 0) {
        posters_stop();
        return -1;
    }

    opened = settled();
    rows   = listing_page_count();
    for (i = 0; i < rows; i++) {
        page_step(PAD_DOWN);
        poster_frame();
        platform_sleep_us(500);
    }
    walked = settled();
    posters_stop();

    if (walked <= opened) {
        snprintf(note, n, "opening fetched %d and walking all %d rows still %d -- the rows further down never ask", opened, rows, walked);
        return 1;
    }
    snprintf(note, n, "%d on opening, %d after walking %d rows", opened, walked, rows);
    return 0;
}

/* The console's link: 26 posters over 802.11b in 1,608,901 us, so 62 ms each.
   Guessed lower, the check called the pack worthless -- on the console a pack
   read is 4.9 ms, not the desktop's 0.05. */
#define LINK_US   62000
#define PACK_ONLY 1 /* g_refuse: nothing may reach the source */

static int g_delay_us;
static int g_refuse;

static int slow_source(const char *id, int out_w, int out_h, unsigned short *dst, int dst_w, int dst_h, int *got_w, int *got_h) {
    if (g_delay_us) platform_sleep_us((unsigned)g_delay_us);
    if (g_refuse) return -1;
    return counting_source(id, out_w, out_h, dst, dst_w, dst_h, got_w, got_h);
}

static const char *walk_pack(void) {
    static char path[160];

    if (!path[0]) snprintf(path, sizeof(path), "%spocketfin-art-walk.dat", platform_data_dir());
    return path;
}

/* A duration, not a frame count: with pacing off sixty frames can pass before
   the worker has fetched anything, and the walk then settled having timed
   nothing. Generous because a cold picture is a fetch and then a 19 ms write
   to the Memory Stick, and one still in flight must not read as quiet. */
#define QUIET_US (6 * LINK_US)

/* Does not wait for every row: on the console one of twenty never arrived, and
   waiting for it once failed the check after 90 s without saying anything
   about the cache. posters_times() reports either way. */
static unsigned walk_the_folder(int rows) {
    unsigned began = platform_clock_us();

    /* Cold on the console: 19 of 20 in 30 s. */
    unsigned bound = began + 20000000u;
    int      i, dir;

    for (dir = 0; dir < 2; dir++)
        for (i = 0; i < rows; i++) {
            page_step(dir ? PAD_UP : PAD_DOWN);
            poster_frame();
        }

    /* Until nothing more arrives, not until a count is reached: rows with no
       artwork ask for nothing, so 20 rows settle at 12, and waiting for 20
       spent the whole bound every time -- 20 s a walk, 40 s of a 70 s suite. */
    {
        int      last  = -1;
        unsigned since = platform_clock_us();

        while (platform_clock_us() - since < QUIET_US && platform_clock_us() < bound) {
            unsigned from_pack = 0;
            int      now;

            posters_times(&from_pack, 0, 0);
            now = g_seen_n + (int)from_pack;

            if (now != last) {
                since = platform_clock_us();
                last  = now;
            }

            page_step(0);
            poster_frame();
        }
    }
    return platform_clock_us() - began;
}

static int open_the_folder(char *note, unsigned n, int refuse) {
    posters_stop();
    g_seen_n   = 0;
    g_delay_us = LINK_US;
    g_refuse   = refuse;
    if (posters_start(slow_source, walk_pack(), 0) != 0) {
        snprintf(note, n, "the picture store would not start");
        return -1;
    }
    if (page_panel(note, n) != 0 || open_real(note, n) != 0) {
        posters_stop();
        return -1;
    }
    return 0;
}

/* The second walk reads the same pack with the source refusing, so only the
   file could have served it. */
static int t_a_cached_folder_fills_far_faster(char *note, unsigned n) {
    unsigned cold, warm, pack_us = 0, source_us = 0, cold_source_us;
    int      rows, fetched;

    remove(walk_pack());
    if (open_the_folder(note, n, 0) != 0) return -1;
    rows = listing_page_count();
    if (rows < 12) {
        posters_stop();
        snprintf(note, n, "only %d rows -- fewer than fill the panel", rows);
        return -1;
    }
    cold    = walk_the_folder(rows);
    fetched = g_seen_n;
    posters_times(0, 0, &cold_source_us);
    posters_stop();

    if (open_the_folder(note, n, PACK_ONLY) != 0) return -1;
    warm = walk_the_folder(rows);
    posters_stop();

    if (g_seen_n != 0) {
        snprintf(note, n, "%d of %d cached rows went to the source anyway", g_seen_n, rows);
        return 1;
    }

    /* The artwork's own cost, not the walk's: forty rendered frames cost 17 ms
       each on a software rasteriser and swamp everything the pictures do. */
    posters_times(0, &pack_us, &source_us);
    if (pack_us * 4 > cold_source_us) {
        snprintf(note, n, "%d cached rows cost %u us out of the pack against %u us to fetch them -- the pack is buying almost nothing",
                 rows, pack_us, cold_source_us);
        return 1;
    }
    snprintf(note, n, "%d of %d rows fetched cold: %u us out of the pack against %u us over the link; %u ms and %u ms of walking", fetched,
             rows, pack_us, cold_source_us, cold / 1000u, warm / 1000u);
    return 0;
}

void test_visible_art_register(void) {
    selftest_add("visible_art", "the front page fetches what it shows", t_the_front_page_fetches_what_it_shows);
    selftest_add("visible_art", "a listing fetches what it shows", t_a_listing_fetches_what_it_shows);
    selftest_add("visible_art", "scrolling a listing fetches the rest", t_scrolling_a_listing_fetches_the_rest);
    selftest_add("visible_art", "a cached folder fills far faster", t_a_cached_folder_fills_far_faster);
}
