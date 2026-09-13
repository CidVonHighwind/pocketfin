/* See view/layout.h. */

#include "view/layout.h"

#include "tools/selftest.h"

#include <stdio.h>

/* A fixed size is exactly what was asked for and UI_FILL is the rest, not
   half each. */
static int t_a_fill_row_takes_what_a_fit_row_leaves(char *note, unsigned n) {
    ui_layout layout;
    ui_rect   fit, fill;

    ui_layout_begin(&layout, ui_rect_make(0, 0, 100, 200));
    fit  = ui_take(&layout, UI_FILL, 30);
    fill = ui_take(&layout, UI_FILL, UI_FILL);

    if (fit.y != 0 || fit.h != 30) {
        snprintf(note, n, "the fit row sat at y=%d h=%d, wanted 0/30", fit.y, fit.h);
        return 1;
    }
    if (fill.y != 30 || fill.h != 170) {
        snprintf(note, n, "the fill row sat at y=%d h=%d, wanted 30/170", fill.y, fill.h);
        return 1;
    }
    snprintf(note, n, "fit at %d/%d, fill at %d/%d", fit.y, fit.h, fill.y, fill.h);
    return 0;
}

/* The gap goes BEFORE the next child, so a box never ends with a trailing
   space somebody has to subtract back off. */
static int t_the_gap_sits_before_not_after(char *note, unsigned n) {
    ui_layout layout;
    ui_rect   a, b;

    ui_layout_begin(&layout, ui_rect_make(0, 0, 100, 100));
    ui_row(&layout, UI_FILL, 10);
    a = ui_take(&layout, 20, UI_FILL);
    b = ui_take(&layout, 20, UI_FILL);
    ui_end(&layout);

    if (a.x != 0) {
        snprintf(note, n, "the first child sat at x=%d, wanted 0 -- a gap in front of it", a.x);
        return 1;
    }
    if (b.x != a.x + a.w + 10) {
        snprintf(note, n, "the second child sat at x=%d, wanted %d", b.x, a.x + a.w + 10);
        return 1;
    }
    snprintf(note, n, "a.x=%d, b.x=%d", a.x, b.x);
    return 0;
}

/* A row centres a shorter child; a column never does -- text centred on its
   own width wanders as its length changes. */
static int t_a_row_centres_and_a_column_does_not(char *note, unsigned n) {
    ui_layout layout;
    ui_rect   in_row, in_col;

    ui_layout_begin(&layout, ui_rect_make(0, 0, 100, 40));
    ui_row(&layout, UI_FILL, 0);
    in_row = ui_take(&layout, 20, 10);
    ui_end(&layout);

    if (in_row.y != 15) {
        snprintf(note, n, "a 10-tall child in a 40-tall row sat at y=%d, wanted 15", in_row.y);
        return 1;
    }

    ui_layout_begin(&layout, ui_rect_make(0, 0, 100, 40));
    in_col = ui_take(&layout, 20, 10);
    if (in_col.x != 0) {
        snprintf(note, n, "a 20-wide child in a 100-wide column sat at x=%d, wanted 0", in_col.x);
        return 1;
    }
    snprintf(note, n, "row centres at %d, column starts at %d", in_row.y, in_col.x);
    return 0;
}

/* The margins are two numbers, and an inset resets the cursor: whatever the
   box had taken, it has taken nothing since being re-sized. */
static int t_the_two_side_margins_are_separate(char *note, unsigned n) {
    ui_layout layout;
    ui_rect   at;

    ui_layout_begin(&layout, ui_rect_make(0, 0, 480, 60));
    (void)ui_take(&layout, UI_FILL, 10); /* something taken BEFORE the pad */
    ui_pad_x(&layout, 12, 14);
    at = ui_take(&layout, UI_FILL, 10);

    if (at.x != 12 || at.w != 480 - 12 - 14) {
        snprintf(note, n, "margins of 12 and 14 left %d..%d, wanted 12..%d", at.x, at.x + at.w, 480 - 14);
        return 1;
    }
    if (at.y != 0) {
        snprintf(note, n, "the pad left the cursor at y=%d -- it did not reset", at.y);
        return 1;
    }
    return 0;
}

static int t_a_layout_too_deep_refuses_and_says_so(char *note, unsigned n) {
    ui_layout layout;
    int       i;

    ui_layout_begin(&layout, ui_rect_make(0, 0, 100, 100));
    for (i = 0; i < UI_LAYOUT_DEPTH + 3; i++) ui_col(&layout, UI_FILL, 0);

    if (!ui_layout_overflowed(&layout)) {
        snprintf(note, n, "%d nested boxes did not overflow a %d-deep stack", UI_LAYOUT_DEPTH + 3, UI_LAYOUT_DEPTH);
        return 1;
    }
    if (ui_layout_depth(&layout) > UI_LAYOUT_DEPTH) {
        snprintf(note, n, "depth reached %d, past the %d-deep stack", ui_layout_depth(&layout), UI_LAYOUT_DEPTH);
        return 1;
    }
    snprintf(note, n, "stopped at depth %d, overflow flagged", ui_layout_depth(&layout));
    return 0;
}

static int t_a_box_on_a_rectangle_pops_back(char *note, unsigned n) {
    ui_layout layout;
    ui_rect   child, after;

    ui_layout_begin(&layout, ui_rect_make(0, 0, 100, 100));
    (void)ui_take(&layout, UI_FILL, 10);

    ui_col_at(&layout, ui_rect_make(200, 200, 50, 900), 0);
    child = ui_take(&layout, UI_FILL, 40);
    ui_end(&layout);

    after = ui_take(&layout, UI_FILL, 10);

    if (child.x != 200 || child.y != 200) {
        snprintf(note, n, "the child of the placed box sat at %d,%d, wanted 200,200", child.x, child.y);
        return 1;
    }
    if (after.x != 0 || after.y != 10) {
        snprintf(note, n, "after closing, the next slot was at %d,%d, wanted 0,10 -- the parent was not restored", after.x, after.y);
        return 1;
    }
    return 0;
}

void test_layout_register(void) {
    selftest_add("layout", "a fill row takes what a fit row leaves", t_a_fill_row_takes_what_a_fit_row_leaves);
    selftest_add("layout", "the gap sits before, not after", t_the_gap_sits_before_not_after);
    selftest_add("layout", "a row centres and a column does not", t_a_row_centres_and_a_column_does_not);
    selftest_add("layout", "the two side margins are separate", t_the_two_side_margins_are_separate);
    selftest_add("layout", "a layout too deep refuses and says so", t_a_layout_too_deep_refuses_and_says_so);
    selftest_add("layout", "a box on a rectangle pops back", t_a_box_on_a_rectangle_pops_back);
}
