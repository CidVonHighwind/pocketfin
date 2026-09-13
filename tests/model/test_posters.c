/* See model/posters.h. The rule is "ask every frame for what you draw, and
 * asking is what keeps it", and breaking it is silent: a page fetches the whole
 * library over 802.11b, or drops a picture out from under the frame drawing
 * it. */

#include "model/posters.h"

#include "port/platform.h"
#include "tools/selftest.h"

#include <stdio.h>
#include <string.h>

static int fake_source(const char *id, int out_w, int out_h, unsigned short *dst, int dst_w, int dst_h, int *got_w, int *got_h) {
    unsigned tint = 0;
    int      i;

    (void)out_w;
    (void)out_h;
    while (id && *id) tint = tint * 31u + (unsigned char)*id++;
    for (i = 0; i < dst_w * dst_h; i++) dst[i] = (unsigned short)(0x1000u + (tint & 0xFFu));
    if (got_w) *got_w = out_w > dst_w ? dst_w : out_w;
    if (got_h) *got_h = out_h > dst_h ? dst_h : out_h;
    return 0;
}

static int store_up(char *note, unsigned n) {
    if (posters_start(fake_source, 0, 0) != 0) {
        snprintf(note, n, "the store would not start");
        return -1;
    }
    return 0;
}

static const unsigned short *wait_for(const char *id, int w, int h, unsigned bound_us) {
    unsigned waited = 0;

    for (;;) {
        const unsigned short *px = poster_get(id, w, h, 0, 0, 0, 0, 0);

        if (px) return px;
        if (waited >= bound_us) return 0;
        platform_sleep_us(2000);
        waited += 2000;
        poster_frame();
    }
}

static int t_a_picture_asked_for_arrives(char *note, unsigned n) {
    const unsigned short *px;
    int                   tw = 0, th = 0, gw = 0, gh = 0;

    if (store_up(note, n) != 0) return -1;

    if (poster_get("one", 28, 40, &tw, &th, &gw, &gh, 0)) {
        snprintf(note, n, "a picture nobody asked for was already there");
        return 1;
    }
    if (!wait_for("one", 28, 40, 2000000u)) {
        snprintf(note, n, "two seconds on, the picture has not arrived");
        return 1;
    }

    px = poster_get("one", 28, 40, &tw, &th, &gw, &gh, 0);
    /* The slot is a power of two whatever was decoded into it: the engine
       blits from nothing else. */
    if (tw != 32 || th != 64) {
        snprintf(note, n, "the slot is %dx%d, not a power of two pair", tw, th);
        return 1;
    }
    if (gw != 28 || gh != 40) {
        snprintf(note, n, "the picture came back %dx%d, asked for 28x40", gw, gh);
        return 1;
    }
    if (px[0] == 0) {
        snprintf(note, n, "the slot is still blank where the source wrote");
        return 1;
    }
    snprintf(note, n, "28x40 in a %dx%d slot", tw, th);
    return 0;
}

/* A rail card's 80x45 handed to a page that asked for 200x212 draws at a
   quarter size. */
static int t_a_size_is_part_of_which_picture_it_is(char *note, unsigned n) {
    int w1 = 0, h1 = 0, w2 = 0, h2 = 0;

    if (store_up(note, n) != 0) return -1;
    if (!wait_for("two", 28, 40, 2000000u)) {
        snprintf(note, n, "the small one never arrived");
        return 1;
    }
    if (poster_get("two", 200, 212, 0, 0, 0, 0, 0)) {
        snprintf(note, n, "the 28x40 picture was handed back for a 200x212 ask");
        return 1;
    }
    if (!wait_for("two", 200, 212, 2000000u)) {
        snprintf(note, n, "the large one never arrived");
        return 1;
    }
    (void)poster_get("two", 28, 40, 0, 0, &w1, &h1, 0);
    (void)poster_get("two", 200, 212, 0, 0, &w2, &h2, 0);
    if (w1 != 28 || w2 != 200) {
        snprintf(note, n, "the two sizes came back %dx%d and %dx%d", w1, h1, w2, h2);
        return 1;
    }
    snprintf(note, n, "28x40 and 200x212 kept apart");
    return 0;
}

/* Otherwise a listing longer than the slots drops the row it is drawing to
   serve the row after it, a page that never finishes loading. */
static int t_asking_keeps_a_picture_a_full_page_cannot_drop(char *note, unsigned n) {
    char id[16];
    int  i;

    if (store_up(note, n) != 0) return -1;
    if (!wait_for("keep", 28, 40, 2000000u)) {
        snprintf(note, n, "the picture never arrived");
        return 1;
    }

    /* The frame has to advance, or the "asked in the last two frames" floor
       alone would hold "keep" and renewal would not be what passed this. */
    for (i = 0; i < 60; i++) {
        poster_frame();
        (void)poster_get("keep", 28, 40, 0, 0, 0, 0, 0);
        snprintf(id, sizeof(id), "flood-%d", i);
        (void)poster_get(id, 28, 40, 0, 0, 0, 0, 0);
        platform_sleep_us(2000);
    }

    if (!poster_get("keep", 28, 40, 0, 0, 0, 0, 0)) {
        snprintf(note, n, "asked for every frame and still dropped, to make room for a row further down the page");
        return 1;
    }
    snprintf(note, n, "held through 60 frames of churn in 16 slots");
    return 0;
}

/* A store that evicted nothing would pass the check above and never fetch
   again once full. */
static int t_a_picture_nobody_asks_for_is_dropped(char *note, unsigned n) {
    char id[16];
    int  i;

    if (store_up(note, n) != 0) return -1;
    if (!wait_for("stale", 28, 40, 2000000u)) {
        snprintf(note, n, "the picture never arrived");
        return 1;
    }

    for (i = 0; i < 60; i++) {
        poster_frame();
        snprintf(id, sizeof(id), "later-%d", i);
        (void)poster_get(id, 28, 40, 0, 0, 0, 0, 0);
        platform_sleep_us(2000);
    }

    if (poster_get("stale", 28, 40, 0, 0, 0, 0, 0)) {
        snprintf(note, n, "nothing asked for it in 60 frames and it is still there -- the store never evicts");
        return 1;
    }
    snprintf(note, n, "dropped after 60 frames unasked");
    return 0;
}

/* The front page holds 32 rail cards against 24 slots, and a loop that read
 * every card, on the panel or not, renewed all 24 every frame so claim()
 * never returned one. Measured on the real server: 24 image requests for 32
 * cards, the last rail empty for the life of the process. */
#define WINDOW_CARDS 6

static int t_more_cards_than_slots_all_get_a_picture(char *note, unsigned n) {
    const int CARDS = 40; /* comfortably past any one class's count */
    int       first, i, landed = 0;

    if (store_up(note, n) != 0) return -1;

    for (first = 0; first + WINDOW_CARDS <= CARDS; first++) {
        int settled = 0, tries;

        for (tries = 0; tries < 500 && !settled; tries++) {
            settled = 1;
            poster_frame();
            for (i = first; i < first + WINDOW_CARDS; i++) {
                char id[16];

                snprintf(id, sizeof(id), "card-%d", i);
                /* Only the window: a card off the panel is not asked for. */
                if (!poster_get(id, 80, 45, 0, 0, 0, 0, 0)) settled = 0;
            }
            if (!settled) platform_sleep_us(2000);
        }
        if (!settled) {
            snprintf(note, n, "cards %d..%d never all landed -- the store has stopped giving any out", first, first + WINDOW_CARDS - 1);
            return 1;
        }
        landed++;
    }
    snprintf(note, n, "%d windows of %d over %d cards, every card served", landed, WINDOW_CARDS, CARDS);
    return 0;
}

/* A server still starting answers 503 for a few seconds and then works; held
   as "no picture", a page loaded in those seconds stayed blank for the run. */
static int g_fail_next;

static int flaky_source(const char *id, int out_w, int out_h, unsigned short *dst, int dst_w, int dst_h, int *got_w, int *got_h) {
    if (g_fail_next) {
        g_fail_next = 0;
        return -1; /* not JF_POSTER_NONE */
    }
    return fake_source(id, out_w, out_h, dst, dst_w, dst_h, got_w, got_h);
}

static int t_a_failure_worth_repeating_is_asked_again(char *note, unsigned n) {
    const unsigned short *px = 0;
    unsigned              waited;

    if (posters_start(flaky_source, 0, 0) != 0) {
        snprintf(note, n, "the store would not start");
        return -1;
    }
    g_fail_next = 1;

    for (waited = 0; waited < 400000u && !px; waited += 2000) {
        unsigned waiting = 0;

        px = poster_get("flaky", 80, 45, 0, 0, 0, 0, &waiting);
        if (!px && !waiting) {
            snprintf(note, n, "a transient failure was remembered as \"no picture\"");
            return 1;
        }
        platform_sleep_us(2000);
        poster_frame();
    }
    if (!px) {
        snprintf(note, n, "one failed fetch and the card never filled again");
        return 1;
    }
    snprintf(note, n, "failed once, asked again, arrived");
    return 0;
}

void test_posters_register(void) {
    selftest_add("posters", "a picture asked for arrives", t_a_picture_asked_for_arrives);
    selftest_add("posters", "a size is part of which picture it is", t_a_size_is_part_of_which_picture_it_is);
    selftest_add("posters", "asking keeps a picture a full page cannot drop", t_asking_keeps_a_picture_a_full_page_cannot_drop);
    selftest_add("posters", "a picture nobody asks for is dropped", t_a_picture_nobody_asks_for_is_dropped);
    selftest_add("posters", "more cards than slots all get a picture", t_more_cards_than_slots_all_get_a_picture);
    selftest_add("posters", "a failure worth repeating is asked again", t_a_failure_worth_repeating_is_asked_again);
}
