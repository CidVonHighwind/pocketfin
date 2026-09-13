/* Where things go, so a screen never computes an x or a y. A stack of open
 * boxes, each remembering where its next child goes; nothing is retained
 * between frames and nothing is allocated. */
#ifndef VIEW_LAYOUT_H
#define VIEW_LAYOUT_H

#include "base/rect.h"

/* A row of columns inside a scrolling page is three. */
#define UI_LAYOUT_DEPTH 6

#define UI_FILL (-1) /* whatever the parent has left, as is 0 */

typedef struct {
    ui_rect box;
    int     axis; /* 0 across, 1 down */
    int     gap;
    int     at;      /* where the next child starts               */
    int     end;     /* and where one taken from the far end ends */
    int     started; /* whether anything has been put in          */
    int     ended;   /* whether anything has come off the far end */
} ui_box;

typedef struct {
    ui_box stack[UI_LAYOUT_DEPTH];
    int    depth;
    int    overflowed;
} ui_layout;

void ui_layout_begin(ui_layout *layout, ui_rect area);

/* `size` of the current box's main axis and all of its cross axis. */
void ui_row(ui_layout *layout, int size, int gap);
void ui_col(ui_layout *layout, int size, int gap);

/* Ignoring the parent's cursor: a scrolling region, or a row a scroll placed. */
void ui_row_at(ui_layout *layout, ui_rect area, int gap);
void ui_col_at(ui_layout *layout, ui_rect area, int gap);

void ui_end(ui_layout *layout);

/* Hands out the size asked for whether or not the box has it; ui_end() logs the
 * overflow. A fixed cross size starts at a column's left edge and sits on a
 * row's centre line. */
ui_rect ui_take(ui_layout *layout, int w, int h);

ui_rect ui_rest(ui_layout *layout);

/* From the far end inward. Take those first and what is left is right without
 * anyone subtracting a width from a screen edge. */
ui_rect ui_take_end(ui_layout *layout, int w, int h);

void ui_gap(ui_layout *layout, int pixels);

/* Resets the cursor. */
void ui_pad_x(ui_layout *layout, int left, int right);

ui_rect ui_here(const ui_layout *layout);

/* More than the box holds draws a plausible screen with its last line missing,
 * found only by counting. */
int ui_used(const ui_layout *layout);

/* So an element that leads with space adds none above the first thing on a
 * page. */
int ui_started(const ui_layout *layout);

/* The box's gap wins over an element's own lead: a column that states its
 * spacing means it. */
int ui_gap_of(const ui_layout *layout);

/* A box refused to open, or was given more than it holds. */
int ui_layout_overflowed(const ui_layout *layout);
int ui_layout_depth(const ui_layout *layout);

#endif
