/* Driven through the pad: what a button means depends on where the selection
 * is and whether a seek is aimed, and a check that called into the state
 * directly would agree with itself rather than with the screen. */

#include "page/player.h"

#include "page/offline.h"
#include "port/gfx.h"
#include "port/input.h"
#include "view/element.h"
#include "view/stack.h"
#include "tools/selftest.h"
#include "test_page.h"

#include <stdio.h>
#include <string.h>

/* Held, not tapped: the seek repeat is counted in frames, so a press and its
   release are two calls. */
static void tap(unsigned mask) {
    page_step(mask);
    page_step(0);
}

/* With no decoder the page runs its own clock off the machine's, so a
   position is only ever about where it was -- what these ask is that nothing
   jumped, not that nothing moved. A seek step is far more than a second. */
static int near_enough(unsigned long long a, unsigned long long b) { return (a > b ? a - b : b - a) < ITEM_TICKS_PER_S; }

static int open_player(char *note, unsigned n, int has_prev, int has_next) {
    item it;

    if (page_panel(note, n) != 0) return -1;

    memset(&it, 0, sizeof(it));
    snprintf(it.id, sizeof(it.id), "check");
    snprintf(it.name, sizeof(it.name), "A Film");
    it.run_ticks = 90ull * 60ull * ITEM_TICKS_PER_S;

    /* Something under it: a page at the bottom of the stack cannot pop, and
       leaving is half of what this screen does. */
    screen_reset(&offline_page_screen);
    page_step(0);
    player_page_show(&it, 10ull * 60ull * ITEM_TICKS_PER_S, has_prev, has_next, -1, -1);
    page_step(0);

    if (strcmp(screen_top_name(), "player") != 0) {
        snprintf(note, n, "the stack is on \"%s\"", screen_top_name());
        return -1;
    }
    return 0;
}

static int t_it_opens_where_it_was_asked_to(char *note, unsigned n) {
    unsigned long long want = 10ull * 60ull * ITEM_TICKS_PER_S;
    int                rc   = open_player(note, n, 0, 0);

    if (rc) return rc;
    if (!near_enough(player_page_position(), want)) {
        snprintf(note, n, "asked for %us, opened at %us", (unsigned)(want / ITEM_TICKS_PER_S),
                 (unsigned)(player_page_position() / ITEM_TICKS_PER_S));
        return 1;
    }
    if (!player_page_showing()) {
        snprintf(note, n, "the band did not come up with the film");
        return 1;
    }
    return 0;
}

/* Unconditional by design: there is no state in which cross on the picture
   does something else. */
static int t_cross_pauses_and_cross_again_resumes(char *note, unsigned n) {
    int rc = open_player(note, n, 0, 0);

    if (rc) return rc;
    if (player_page_paused()) {
        snprintf(note, n, "it opened paused");
        return 1;
    }
    tap(PAD_CROSS);
    if (!player_page_paused()) {
        snprintf(note, n, "cross did not pause it");
        return 1;
    }
    tap(PAD_CROSS);
    if (player_page_paused()) {
        snprintf(note, n, "cross did not resume it");
        return 1;
    }
    return 0;
}

/* On the bar, left and right aim rather than walk the transport row. */
static int t_a_seek_aims_before_it_moves_anything(char *note, unsigned n) {
    unsigned long long was;
    int                rc = open_player(note, n, 0, 0);

    if (rc) return rc;
    tap(PAD_DOWN);
    was = player_page_position();

    tap(PAD_LEFT);
    if (!player_page_seeking()) {
        snprintf(note, n, "left on the bar armed no seek");
        return 1;
    }
    if (!near_enough(player_page_position(), was)) {
        snprintf(note, n, "aiming moved playback: %us became %us", (unsigned)(was / ITEM_TICKS_PER_S),
                 (unsigned)(player_page_position() / ITEM_TICKS_PER_S));
        return 1;
    }
    return 0;
}

static int t_circle_cancels_an_aimed_seek(char *note, unsigned n) {
    unsigned long long was;
    int                rc = open_player(note, n, 0, 0);

    if (rc) return rc;
    tap(PAD_DOWN);
    was = player_page_position();
    tap(PAD_LEFT);
    if (!player_page_seeking()) {
        snprintf(note, n, "no seek to cancel");
        return 1;
    }

    tap(PAD_CIRCLE);
    if (player_page_seeking()) {
        snprintf(note, n, "circle left the cursor up");
        return 1;
    }
    if (!near_enough(player_page_position(), was)) {
        snprintf(note, n, "a cancelled seek still moved to %us", (unsigned)(player_page_position() / ITEM_TICKS_PER_S));
        return 1;
    }
    if (strcmp(screen_top_name(), "player") != 0) {
        snprintf(note, n, "circle left the page instead of cancelling");
        return 1;
    }
    return 0;
}

static int t_a_committed_seek_moves_playback(char *note, unsigned n) {
    unsigned long long was;
    int                rc = open_player(note, n, 0, 0);

    if (rc) return rc;
    tap(PAD_DOWN);
    was = player_page_position();
    tap(PAD_LEFT);
    tap(PAD_CROSS);

    if (player_page_seeking()) {
        snprintf(note, n, "cross left the cursor armed");
        return 1;
    }
    if (player_page_position() >= was) {
        snprintf(note, n, "a seek back from %us landed at %us", (unsigned)(was / ITEM_TICKS_PER_S),
                 (unsigned)(player_page_position() / ITEM_TICKS_PER_S));
        return 1;
    }
    /* Landing paused would make the viewer press cross again every jump. */
    if (player_page_paused()) {
        snprintf(note, n, "the seek landed paused");
        return 1;
    }
    return 0;
}

/* With no neighbours the row is one button. Both directions are asked: one
   alone passes over a phantom on the other side. */
static int t_a_film_with_no_neighbours_cannot_step_to_one(char *note, unsigned n) {
    static const unsigned WAY[] = {PAD_LEFT, PAD_RIGHT};
    unsigned              i;

    for (i = 0; i < sizeof(WAY) / sizeof(WAY[0]); i++) {
        int rc = open_player(note, n, 0, 0);

        if (rc) return rc;
        tap(PAD_UP);
        tap(WAY[i]);
        tap(PAD_CROSS);

        if (strcmp(screen_top_name(), "player") != 0) {
            snprintf(note, n, "%s then cross left the page as \"%s\"", WAY[i] == PAD_LEFT ? "left" : "right",
                     player_result_text((player_result)player_page_result()));
            return 1;
        }
    }
    return 0;
}

static int t_next_leaves_saying_which_way(char *note, unsigned n) {
    int rc = open_player(note, n, 1, 1);

    if (rc) return rc;
    tap(PAD_UP);
    tap(PAD_RIGHT);
    tap(PAD_CROSS);

    if (strcmp(screen_top_name(), "player") == 0) {
        snprintf(note, n, "cross on Next did not leave");
        return 1;
    }
    if (player_page_result() != (int)PLAYER_NEXT) {
        snprintf(note, n, "left as \"%s\", not next", player_result_text((player_result)player_page_result()));
        return 1;
    }
    return 0;
}

static int t_prev_leaves_saying_which_way(char *note, unsigned n) {
    int rc = open_player(note, n, 1, 1);

    if (rc) return rc;
    tap(PAD_UP);
    tap(PAD_LEFT);
    tap(PAD_CROSS);

    if (strcmp(screen_top_name(), "player") == 0) {
        snprintf(note, n, "cross on Prev did not leave");
        return 1;
    }
    if (player_page_result() != (int)PLAYER_PREV) {
        snprintf(note, n, "left as \"%s\", not prev", player_result_text((player_result)player_page_result()));
        return 1;
    }
    return 0;
}

static int t_stopping_is_not_finishing(char *note, unsigned n) {
    int rc = open_player(note, n, 0, 0);

    if (rc) return rc;
    tap(PAD_CIRCLE);

    if (strcmp(screen_top_name(), "player") == 0) {
        snprintf(note, n, "circle did not leave");
        return 1;
    }
    if (player_page_result() != (int)PLAYER_STOPPED) {
        snprintf(note, n, "circle left as \"%s\"", player_result_text((player_result)player_page_result()));
        return 1;
    }
    return 0;
}

/* Getting this wrong marks an episode watched and walks a season, which is
   why the two questions are separate ones. */
static int t_only_a_finished_film_counts_as_watched(char *note, unsigned n) {
    static const player_result ALL[] = {PLAYER_ENDED, PLAYER_STOPPED, PLAYER_PREV, PLAYER_NEXT, PLAYER_FAILED};
    unsigned                   i;

    for (i = 0; i < sizeof(ALL) / sizeof(ALL[0]); i++) {
        int want = ALL[i] == PLAYER_ENDED;

        if (player_result_is_watched(ALL[i]) != want) {
            snprintf(note, n, "\"%s\" reads as %swatched", player_result_text(ALL[i]), want ? "un" : "");
            return 1;
        }
    }
    if (player_result_may_advance(PLAYER_FAILED) || player_result_may_advance(PLAYER_STOPPED)) {
        snprintf(note, n, "a broken or stopped film would walk to the next one");
        return 1;
    }
    if (!player_result_may_advance(PLAYER_ENDED) || !player_result_may_advance(PLAYER_NEXT)) {
        snprintf(note, n, "a finished film would not advance");
        return 1;
    }
    return 0;
}

void test_player_page_register(void) {
    selftest_add("player_page", "it opens where it was asked to", t_it_opens_where_it_was_asked_to);
    selftest_add("player_page", "cross pauses and cross again resumes", t_cross_pauses_and_cross_again_resumes);
    selftest_add("player_page", "a seek aims before it moves anything", t_a_seek_aims_before_it_moves_anything);
    selftest_add("player_page", "circle cancels an aimed seek", t_circle_cancels_an_aimed_seek);
    selftest_add("player_page", "a committed seek moves playback", t_a_committed_seek_moves_playback);
    selftest_add("player_page", "a film with no neighbours cannot step to one", t_a_film_with_no_neighbours_cannot_step_to_one);
    selftest_add("player_page", "next leaves saying which way", t_next_leaves_saying_which_way);
    selftest_add("player_page", "prev leaves saying which way", t_prev_leaves_saying_which_way);
    selftest_add("player_page", "stopping is not finishing", t_stopping_is_not_finishing);
    selftest_add("player_page", "only a finished film counts as watched", t_only_a_finished_film_counts_as_watched);
}
