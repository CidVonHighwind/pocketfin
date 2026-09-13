/* The screen stack: which page is on top, and the one frame it draws through.
 * State is per push, from an arena, because a listing opens itself and a
 * screen whose state was its own statics cannot be on the stack twice. */
#ifndef VIEW_STACK_H
#define VIEW_STACK_H

#include "view/frame.h"

typedef struct {
    const char *name;
    unsigned    state_bytes; /* 0 for a screen with no state */
    /* A push must hand exactly this or nothing; any other size is refused. */
    unsigned arg_bytes;
    /* `arg` is already a copy. */
    void (*enter)(void *st, const void *arg, unsigned arg_len);
    void (*frame)(ui_frame *ui, void *st);
    int (*back)(void *st); /* 1 = handled here, 0 = pop */
    /* Where a page re-asks for the rows the screen above took the buffer for. */
    void (*resumed)(void *st);
    /* Optional. Every rail wraps at both ends, so a script cannot count presses
       and has to read where the cursor is. */
    void (*describe)(void *st, char *out, unsigned n);
} screen_def;

/* Home, a library, a series, a season, a detail page, a film -- and the
 * offline screen over any of them. */
#define SCREEN_STACK_MAX 8

/* Sized from the listing, the largest state and the one that stacks three
 * deep. */
#define SCREEN_ARENA_BYTES 8192

/* An item is 248 bytes, and the player's argument is that plus a start
 * position and two flags. */
#define SCREEN_ARG_MAX 320

/* No static_assert in C99: a negative array size is the compile error. */
#define SCREEN_ARG_FITS(type) typedef char screen_arg_fits_##type[sizeof(type) <= SCREEN_ARG_MAX ? 1 : -1]

/* Asked inside a frame, a navigation is applied after the frame returns, so a
 * stack local may be handed over; a second in the same frame is refused.
 * Outside a frame it happens at once. */
void screen_push_with(const screen_def *def, const void *arg, unsigned arg_len);
void screen_push(const screen_def *def);

/* Never pops the last screen. */
void screen_pop(void);

void screen_reset(const screen_def *def);

/* Asks the top screen first and pops only if it says no. */
void screen_back(void);

/* A page holding a modifier says so, and circle does not also pop it. Cleared
 * every frame. */
void screen_frame_took_back(void);

/* The gap before the next frame is not charged to the application. A
 * screenshot streams 391 kB through hostfs from the drawing thread and once
 * reported a 1.4 s stall that was the camera. */
void screen_timing_skip_gap(void);

void screen_run_frame(ui_rect area, unsigned held, unsigned pressed);

const char *screen_top_name(void);
int         screen_depth(void);

/* Always ends in a line reading "end": these files are rewritten in place, and
 * a reader that landed mid-write can tell. */
void screen_describe(char *out, unsigned n);

void *screen_top_state(void);

ui_frame *screen_ui(void);

#endif
