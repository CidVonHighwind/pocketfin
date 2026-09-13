/* One scrolling run: a rail of cards, a column of rows, a page of rails.
 *
 * The caller places its items in item coordinates, the first at 0. The margin
 * counts at both ends, or the run scrolls that much too far and the selected
 * item at the end finishes flush against the panel edge. */
#ifndef VIEW_SCROLL_H
#define VIEW_SCROLL_H

/* Frames for a scroll of any length to settle, near enough. */
#define UI_SCROLL_SETTLE 24

typedef struct {
    int offset; /* where it is now, eased towards target */
    int target;

    /* Declared by ui_scroll_span() every frame, before any item is placed. */
    int view;
    int pad;
} ui_scroll;

/* Once a frame, before placing anything. */
void ui_scroll_span(ui_scroll *s, int view_len, int pad);

/* Relative to the view's own origin, margin and offset included. */
int ui_scroll_place(const ui_scroll *s, int at);

/* The whole run, margins included -- what a scrollbar needs. */
int ui_scroll_content(const ui_scroll *s, int items_len);

/* Brings [from, to) into view, in item coordinates; `items_len` is where the
 * last item ends. Leaves a peek of half the item's span, capped at a third of
 * the view, so the next one is partly visible. */
void ui_scroll_into_view(ui_scroll *s, int from, int to, int items_len);

/* One frame of easing: a THIRD of what is left. */
void ui_scroll_step(ui_scroll *s);

#endif
