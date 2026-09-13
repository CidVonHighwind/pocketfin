/* See frame.h. */

#include "view/frame.h"

#include "port/input.h"
#include "base/log.h"

#define DIR_MASK (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT)

/* A delay, then two accelerations, measured against a finger rather than a
   spec. */
#define REPEAT_DELAY_US 267000u
#define REPEAT_RATE_US  100000u
#define REPEAT_FAST1_US 600000u
#define REPEAT_FAST2_US 1600000u
#define REPEAT_MID_US   66700u
#define REPEAT_MIN_US   33300u

static int repeat_rate_us(unsigned hold_us) {
    unsigned repeating = hold_us > REPEAT_DELAY_US ? hold_us - REPEAT_DELAY_US : 0;

    if (repeating > REPEAT_FAST2_US) return (int)REPEAT_MIN_US;
    if (repeating > REPEAT_FAST1_US) return (int)REPEAT_MID_US;
    return (int)REPEAT_RATE_US;
}

void ui_frame_begin(ui_frame *ui, ui_rect area, unsigned held, unsigned pressed, unsigned now_us) {
    unsigned dir  = held & DIR_MASK;
    unsigned dt   = ui->last_us ? now_us - ui->last_us : 0;
    int      fire = 0;

    ui->held          = held;
    ui->pressed       = pressed;
    ui->entries       = 0;
    ui->want_focus_on = 0;
    /* Zero means "no reading yet". */
    ui->last_us = now_us ? now_us : 1u;

    if (pressed & DIR_MASK) {
        dir              = pressed & DIR_MASK;
        fire             = 1;
        ui->hold_us      = 0;
        ui->repeat_in_us = (int)REPEAT_DELAY_US;
    } else if (dir != ui->prev_dir) {
        /* Without the reset, rolling from up to down repeats down at full
           speed. */
        ui->hold_us      = 0;
        ui->repeat_in_us = (int)REPEAT_DELAY_US;
        fire             = (dir != 0);
    } else if (dir) {
        ui->hold_us += dt;
        ui->repeat_in_us -= (int)dt;
        if (ui->repeat_in_us <= 0) {
            fire             = 1;
            ui->repeat_in_us = repeat_rate_us(ui->hold_us);
        }
    }
    ui->prev_dir = held & DIR_MASK;

    /* One direction a frame, or a diagonal press skips the highlight twice. */
    ui->fired = 0;
    if (fire) {
        if (dir & PAD_DOWN)
            ui->fired = PAD_DOWN;
        else if (dir & PAD_UP)
            ui->fired = PAD_UP;
        else if (dir & PAD_RIGHT)
            ui->fired = PAD_RIGHT;
        else if (dir & PAD_LEFT)
            ui->fired = PAD_LEFT;
    }

    ui_layout_begin(&ui->layout, area);
}

unsigned ui_frame_fired(const ui_frame *ui) { return ui->fired; }
void     ui_frame_take_fired(ui_frame *ui) { ui->fired = 0; }

int ui_touch(ui_frame *ui, ui_id id, ui_rect box) {
    if (ui->entries < UI_FRAME_MAX) {
        ui->entry[ui->entries].id  = id;
        ui->entry[ui->entries].box = box;
        ui->entries++;
        if (ui->want_focus_on && ui->want_focus == id) {
            ui->focus         = id;
            ui->has_focus     = 1;
            ui->want_focus_on = 0;
        }
    } else if (!ui->overflowed) {
        /* A dropped entry is a widget the cursor never reaches, which reads as
           a scrolling fault. */
        ui->overflowed = 1;
        log_printf("ui: more than %d widgets in one frame; the cursor will wrap before the real end", UI_FRAME_MAX);
    }
    return ui->has_focus && ui->focus == id;
}

static int find(const ui_frame *ui, ui_id id) {
    int i;

    for (i = 0; i < ui->entries; i++)
        if (ui->entry[i].id == id) return i;
    return -1;
}

static int mid_y(ui_rect r) { return r.y + r.h / 2; }

static int abs_i(int v) { return v < 0 ? -v : v; }

static int rows_overlap(ui_rect a, ui_rect b) { return a.y < b.y + b.h && b.y < a.y + a.h; }

/* Sideways stays on the line, or "right" off the end of a rail walks into the
 * rail below.
 *
 * Vertically the leading edges are compared, not centres: one row mixes a
 * 30-pixel poster with an 80-pixel still, and by centres the first tile lines
 * up with the second thing below it.
 *
 * Distance travelled decides and the cross axis breaks ties; the other way
 * round, a settings row with an inset control gets stepped over. */
static int pick(const ui_frame *ui, int from, unsigned dir) {
    ui_rect a        = ui->entry[from].box;
    int     sideways = (dir == PAD_LEFT || dir == PAD_RIGHT);
    int     best = -1, best_score = 0, i;

    for (i = 0; i < ui->entries; i++) {
        ui_rect b;
        int     along, across, score;

        if (i == from) continue;
        b = ui->entry[i].box;

        if (sideways) {
            if (!rows_overlap(a, b)) continue;
            along  = (dir == PAD_RIGHT) ? b.x - a.x : a.x - b.x;
            across = mid_y(b) - mid_y(a);
        } else {
            if (rows_overlap(a, b)) continue;
            along  = (dir == PAD_DOWN) ? b.y - a.y : a.y - b.y;
            across = b.x - a.x;
        }

        if (along < 4) continue;
        if (across < 0) across = -across;

        score = along * 4 + across;
        if (best < 0 || score < best_score) {
            best       = i;
            best_score = score;
        }
    }
    return best;
}

/* Sideways stays on the line, so a rail wraps to its own start; vertically it
 * must leave the line, so a row of buttons does not wrap into itself. */
static int wrap(const ui_frame *ui, int from, unsigned dir) {
    ui_rect a        = ui->entry[from].box;
    int     sideways = (dir == PAD_LEFT || dir == PAD_RIGHT);
    int     best = -1, best_score = 0, i;

    for (i = 0; i < ui->entries; i++) {
        ui_rect b;
        int     along, across, score;

        if (i == from) continue;
        b = ui->entry[i].box;

        if (sideways) {
            if (!rows_overlap(a, b)) continue;
            along  = (dir == PAD_RIGHT) ? a.x - b.x : b.x - a.x;
            across = 0;
        } else {
            if (rows_overlap(a, b)) continue;
            along = (dir == PAD_DOWN) ? a.y - b.y : b.y - a.y;
            /* Leading edges: by centres a 122-wide tile lines up with the
               second card of a rail of 32-wide posters. */
            across = abs_i(b.x - a.x);
        }
        if (along < 4) continue;

        score = along * 2 - across;
        if (best < 0 || score > best_score) {
            best       = i;
            best_score = score;
        }
    }
    return best;
}

void ui_frame_end(ui_frame *ui) {
    int      from, to;
    unsigned dir;

    if (ui->entries == 0) return;

    /* A page a script opens must have a focus without a press. */
    if (!ui->has_focus || find(ui, ui->focus) < 0) {
        ui->focus     = ui->entry[0].id;
        ui->has_focus = 1;
        return;
    }

    dir = ui->fired;
    if (!dir) return;

    from = find(ui, ui->focus);
    to   = pick(ui, from, dir);

    /* Fresh press only: at the repeat's top speed a wrap arrives before the eye
       reaches the end. */
    if (to < 0 && (ui->pressed & dir)) to = wrap(ui, from, dir);

    if (to >= 0) ui->focus = ui->entry[to].id;
}

/* The player names its selected button before drawing the row; refused, the
   band came up with nothing lit. */
void ui_frame_focus_set(ui_frame *ui, ui_id id) {
    if (find(ui, id) >= 0) {
        ui->focus         = id;
        ui->has_focus     = 1;
        ui->want_focus_on = 0;
        return;
    }
    ui->want_focus    = id;
    ui->want_focus_on = 1;
}

int ui_frame_focus_index(ui_frame *ui, int index) {
    if (index < 0 || index >= ui->entries) return -1;
    ui->focus     = ui->entry[index].id;
    ui->has_focus = 1;
    return 0;
}

int ui_frame_entries(const ui_frame *ui) { return ui->entries; }

int ui_frame_focus_box(const ui_frame *ui, ui_rect *out) {
    int at;

    if (!ui->has_focus) return 0;
    at = find(ui, ui->focus);
    if (at < 0) return 0;
    if (out) *out = ui->entry[at].box;
    return 1;
}

ui_id ui_frame_focus(const ui_frame *ui, int *has_focus) {
    if (has_focus) *has_focus = ui->has_focus;
    return ui->focus;
}
