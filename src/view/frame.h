/* One pass of the interface: a layout, what got drawn where, and which of it
 * has focus. The registered boxes are the navigation graph -- down is the
 * nearest thing below -- so nothing describes a grid by hand.
 *
 * The repeat is in microseconds, not frames: the GE draws a frame in 5-7 ms,
 * the CPU in 32-46, and a finger feels real time. */
#ifndef VIEW_FRAME_H
#define VIEW_FRAME_H

#include "view/layout.h"

/* Picked by the caller -- a row index, say. Unique within a frame; a script can
 * use the same integer to land on a widget. */
typedef unsigned ui_id;

/* The listing registers LISTING_MAX (128) rows, on the panel or not; at 64 the
   cursor silently stopped at row 64 and wrapped to the top. 160 is 128 rows
   plus the chrome and a notice, at 160 * 20 bytes. */
#define UI_FRAME_MAX 160

typedef struct {
    ui_id   id;
    ui_rect box;
} ui_frame_entry;

typedef struct {
    ui_layout layout;

    unsigned held, pressed;

    /* The one direction acting this frame. A screen that consumes it zeroes
       it, so the frame does not also move the focus. */
    unsigned fired;
    unsigned prev_dir, hold_us, last_us;
    int      repeat_in_us;

    ui_id          focus;
    int            has_focus;
    ui_id          want_focus;
    int            want_focus_on;
    ui_frame_entry entry[UI_FRAME_MAX];
    int            entries;
    int            overflowed; /* said once, not once a frame */
} ui_frame;

/* `now_us` is a parameter so the repeat can be checked with synthetic time. */
void ui_frame_begin(ui_frame *ui, ui_rect area, unsigned held, unsigned pressed, unsigned now_us);

/* After drawing, with the box drawn in. A widget draws its focused state from
 * the return, never by comparing ids itself. */
int ui_touch(ui_frame *ui, ui_id id, ui_rect box);

/* Call once, after every widget has drawn. */
void ui_frame_end(ui_frame *ui);

/* An id not registered yet is remembered and applied when it registers this
 * frame; one that never does changes nothing. */
void ui_frame_focus_set(ui_frame *ui, ui_id id);

/* In registration order. -1 for out of range. */
int ui_frame_focus_index(ui_frame *ui, int index);

ui_id ui_frame_focus(const ui_frame *ui, int *has_focus);

int ui_frame_entries(const ui_frame *ui);

/* 0 if nothing has focus or it registered nothing this frame. */
int ui_frame_focus_box(const ui_frame *ui, ui_rect *out);

unsigned ui_frame_fired(const ui_frame *ui);
void     ui_frame_take_fired(ui_frame *ui);

#endif
