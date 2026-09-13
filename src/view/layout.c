/* See layout.h. */

#include "view/layout.h"

#include "base/log.h"

#define ACROSS 0
#define DOWN   1

static ui_box *top(ui_layout *layout) { return &layout->stack[layout->depth - 1]; }

static void reset_cursor(ui_box *b) {
    b->at      = (b->axis == DOWN) ? b->box.y : b->box.x;
    b->end     = (b->axis == DOWN) ? b->box.y + b->box.h : b->box.x + b->box.w;
    b->started = 0;
    b->ended   = 0;
}

static void push_box(ui_layout *layout, ui_rect area, int axis, int gap) {
    ui_box *b = &layout->stack[layout->depth++];

    b->box  = area;
    b->axis = axis;
    b->gap  = gap;
    reset_cursor(b);
}

void ui_layout_begin(ui_layout *layout, ui_rect area) {
    layout->depth      = 0;
    layout->overflowed = 0;
    push_box(layout, area, DOWN, 0);
}

static ui_rect remaining(const ui_box *b) {
    if (b->axis == DOWN) return ui_rect_make(b->box.x, b->at, b->box.w, b->end - b->at);
    return ui_rect_make(b->at, b->box.y, b->end - b->at, b->box.h);
}

/* Before the next child, so a box never ends with a trailing gap. */
static int lead(const ui_box *b) { return b->started ? b->gap : 0; }

static int or_rest(int want, int rest) { return (want == UI_FILL || want == 0) ? rest : want; }

ui_rect ui_take(ui_layout *layout, int w, int h) {
    ui_box *b;
    ui_rect free_space, slot;

    if (layout->depth <= 0) return ui_rect_make(0, 0, 0, 0);
    b = top(layout);

    b->at += lead(b);
    free_space = remaining(b);

    slot.x = free_space.x;
    slot.y = free_space.y;

    slot.w = or_rest(w, free_space.w);
    slot.h = or_rest(h, free_space.h);
    /* Down a column, not centred: text centred on its own width wanders as it
       changes. */
    if (b->axis == DOWN) {
        b->at += slot.h;
    } else {
        b->at += slot.w;
        slot.y += (free_space.h - slot.h) / 2;
    }
    b->started = 1;

    if (slot.w < 0) slot.w = 0;
    if (slot.h < 0) slot.h = 0;
    return slot;
}

ui_rect ui_rest(ui_layout *layout) { return ui_take(layout, UI_FILL, UI_FILL); }

ui_rect ui_take_end(ui_layout *layout, int w, int h) {
    ui_box *b;
    ui_rect free_space, slot;

    if (layout->depth <= 0) return ui_rect_make(0, 0, 0, 0);
    b = top(layout);

    b->end -= b->ended ? b->gap : 0;
    free_space = remaining(b);

    slot.w = or_rest(w, free_space.w);
    slot.h = or_rest(h, free_space.h);
    if (b->axis == DOWN) {
        b->end -= slot.h;
        slot.x = free_space.x;
        slot.y = b->end;
    } else {
        b->end -= slot.w;
        slot.x = b->end;
        slot.y = free_space.y + (free_space.h - slot.h) / 2;
    }
    b->ended = 1;

    if (slot.w < 0) slot.w = 0;
    if (slot.h < 0) slot.h = 0;
    return slot;
}

/* A box that does not open lays its contents out against the parent, which
 * looks like a nesting mistake and is not. */
static int depth_ok(ui_layout *layout) {
    if (layout->depth < UI_LAYOUT_DEPTH) return 1;
    if (!layout->overflowed)
        log_printf("ui: layout deeper than %d -- a box did not open, so its contents are laid out against the parent", UI_LAYOUT_DEPTH);
    layout->overflowed = 1;
    return 0;
}

static void open_box(ui_layout *layout, int axis, int size, int gap) {
    ui_rect slot;

    if (!depth_ok(layout)) return;

    if (top(layout)->axis == DOWN)
        slot = ui_take(layout, UI_FILL, size);
    else
        slot = ui_take(layout, size, UI_FILL);

    push_box(layout, slot, axis, gap);
}

void ui_row(ui_layout *layout, int size, int gap) { open_box(layout, ACROSS, size, gap); }
void ui_col(ui_layout *layout, int size, int gap) { open_box(layout, DOWN, size, gap); }

void ui_row_at(ui_layout *layout, ui_rect area, int gap) {
    if (depth_ok(layout)) push_box(layout, area, ACROSS, gap);
}

void ui_col_at(ui_layout *layout, ui_rect area, int gap) {
    if (depth_ok(layout)) push_box(layout, area, DOWN, gap);
}

void ui_end(ui_layout *layout) {
    /* ui_take never refuses, so an element need not check -- which is why
       somebody has to. An overfull box draws a plausible screen with its last
       line missing or running under the next thing. */
    if (layout->depth > 0) {
        const ui_box *b    = &layout->stack[layout->depth - 1];
        int           used = ui_used(layout);
        int           room = (b->axis == DOWN) ? b->box.h : b->box.w;

        if (used > room && !layout->overflowed) {
            layout->overflowed = 1;
            log_printf("ui: a box was given %d px of a %d px %s -- %d over", used, room, b->axis == DOWN ? "column" : "row", used - room);
        }
    }
    if (layout->depth > 1) layout->depth--;
}

void ui_gap(ui_layout *layout, int pixels) {
    ui_box *b;

    if (layout->depth <= 0) return;
    b = top(layout);
    b->at += pixels;
    b->started = 1;
}

void ui_pad_x(ui_layout *layout, int left, int right) {
    ui_box *b;

    if (layout->depth <= 0) return;
    b = top(layout);
    b->box.x += left;
    b->box.w -= left + right;
    if (b->box.w < 0) b->box.w = 0;
    reset_cursor(b);
}

ui_rect ui_here(const ui_layout *layout) {
    if (layout->depth <= 0) return ui_rect_make(0, 0, 0, 0);
    return layout->stack[layout->depth - 1].box;
}

int ui_used(const ui_layout *layout) {
    const ui_box *b;

    if (layout->depth <= 0) return 0;
    b = &layout->stack[layout->depth - 1];
    return b->at - (b->axis == DOWN ? b->box.y : b->box.x);
}

int ui_gap_of(const ui_layout *layout) {
    if (layout->depth <= 0) return 0;
    return layout->stack[layout->depth - 1].gap;
}

int ui_started(const ui_layout *layout) {
    if (layout->depth <= 0) return 0;
    return layout->stack[layout->depth - 1].started;
}

int ui_layout_depth(const ui_layout *layout) { return layout->depth; }
int ui_layout_overflowed(const ui_layout *layout) { return layout->overflowed; }
