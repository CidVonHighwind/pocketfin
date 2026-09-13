/* See view/scroll.h. */

#include "view/scroll.h"

#include "tools/selftest.h"

#include <stdio.h>
#include <string.h>

static void run(ui_scroll *s, int pad) {
    memset(s, 0, sizeof(*s));
    ui_scroll_span(s, 200, pad);
}

static int t_a_visible_item_does_not_move_the_target(char *note, unsigned n) {
    ui_scroll s;

    run(&s, 0);
    ui_scroll_into_view(&s, 10, 50, 400);
    if (s.target != 0) {
        snprintf(note, n, "an item already inside [0,200) moved the target to %d", s.target);
        return 1;
    }
    return 0;
}

/* The peek, half the item, keeps the next one partly on screen: flush against
 * the edge, a selection gives no sign of what is beyond it. */
static int t_an_item_past_the_far_edge_moves_the_target_the_minimum(char *note, unsigned n) {
    ui_scroll s;

    /* 80 long, ending 60 past a 200 view: 60 of overhang and 40 of peek. */
    run(&s, 0);
    ui_scroll_into_view(&s, 180, 260, 400);
    if (s.target != 100) {
        snprintf(note, n, "target is %d, wanted 100 (60 overhang + 40 peek)", s.target);
        return 1;
    }
    ui_scroll_into_view(&s, 180, 260, 400);
    if (s.target != 100) {
        snprintf(note, n, "a settled item moved the target to %d", s.target);
        return 1;
    }
    return 0;
}

static int t_an_item_past_the_near_edge_moves_the_target_back(char *note, unsigned n) {
    ui_scroll s;

    run(&s, 0);
    s.offset = s.target = 300;
    ui_scroll_into_view(&s, 250, 330, 600);
    if (s.target != 210) {
        snprintf(note, n, "target is %d, wanted 210 (250 less the 40 peek)", s.target);
        return 1;
    }
    return 0;
}

/* Without the clamp the peek holds the start at the first item's own margin
 * and the end short of the last one's. */
static int t_the_ends_are_reachable(char *note, unsigned n) {
    ui_scroll s;

    run(&s, 0);
    s.target = 200;
    ui_scroll_into_view(&s, 0, 40, 400);
    if (s.target != 0) {
        snprintf(note, n, "the first item left the target at %d, not 0", s.target);
        return 1;
    }

    s.target = 0;
    ui_scroll_into_view(&s, 360, 400, 400);
    if (s.target != 200) {
        snprintf(note, n, "the last item left the target at %d, wanted 200 (400 of items less the 200 view)", s.target);
        return 1;
    }
    return 0;
}

/* Counted once, the selected item at the end finishes flush against the edge,
 * which is what happened to the rails. */
static int t_the_margin_is_kept_at_both_ends(char *note, unsigned n) {
    ui_scroll s;
    int       at;

    run(&s, 8);

    if (ui_scroll_place(&s, 0) != 8) {
        snprintf(note, n, "the first item draws at %d, wanted the margin (8)", ui_scroll_place(&s, 0));
        return 1;
    }

    if (ui_scroll_content(&s, 400) != 416) {
        snprintf(note, n, "400 of items with an 8 margin measured %d, wanted 416", ui_scroll_content(&s, 400));
        return 1;
    }

    /* The item ends at 400 in item coordinates and draws at 192 of a 200
       view, 8 short of the edge. */
    s.target = 0;
    ui_scroll_into_view(&s, 360, 400, 400);
    s.offset = s.target;
    at       = ui_scroll_place(&s, 400);
    if (at != 200 - 8) {
        snprintf(note, n, "at the end the last item finishes at %d of a 200 view -- wanted %d, the margin clear of the edge", at, 200 - 8);
        return 1;
    }
    snprintf(note, n, "content 416 for 400 of items; the end leaves 8 clear");
    return 0;
}

/* Decelerating, because a constant speed passes "not a jump" and still arrives
 * at full tilt. */
static int t_offset_eases_towards_target_rather_than_jumping(char *note, unsigned n) {
    ui_scroll s;
    int       steps = 0, first, second;

    run(&s, 0);
    s.target = 100;
    ui_scroll_step(&s);
    if (s.offset == 0 || s.offset == 100) {
        snprintf(note, n, "one step went from 0 to %d -- not eased", s.offset);
        return 1;
    }
    first = s.offset;

    ui_scroll_step(&s);
    second = s.offset - first;
    if (second >= first) {
        snprintf(note, n, "step one moved %d and step two moved %d -- it is not decelerating", first, second);
        return 1;
    }

    while (s.offset != s.target && steps < 100) {
        ui_scroll_step(&s);
        steps++;
    }
    if (s.offset != 100) {
        snprintf(note, n, "did not converge: offset %d after %d more steps", s.offset, steps);
        return 1;
    }
    if (steps + 2 > UI_SCROLL_SETTLE) {
        snprintf(note, n, "took %d steps to settle, and UI_SCROLL_SETTLE promises %d", steps + 2, UI_SCROLL_SETTLE);
        return 1;
    }
    snprintf(note, n, "settled in %d steps", steps + 2);
    return 0;
}

void test_scroll_register(void) {
    selftest_add("scroll", "a visible item does not move the target", t_a_visible_item_does_not_move_the_target);
    selftest_add("scroll", "an item past the far edge moves the target the minimum",
                 t_an_item_past_the_far_edge_moves_the_target_the_minimum);
    selftest_add("scroll", "an item past the near edge moves the target back", t_an_item_past_the_near_edge_moves_the_target_back);
    selftest_add("scroll", "the ends are reachable", t_the_ends_are_reachable);
    selftest_add("scroll", "the margin is kept at both ends", t_the_margin_is_kept_at_both_ends);
    selftest_add("scroll", "offset eases towards target rather than jumping", t_offset_eases_towards_target_rather_than_jumping);
}
