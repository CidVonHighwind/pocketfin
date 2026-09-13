#ifndef BASE_RECT_H
#define BASE_RECT_H

typedef struct {
    int x, y, w, h;
} ui_rect;

ui_rect ui_rect_make(int x, int y, int w, int h);

/* Smaller by `by` on every side, never past nothing. */
ui_rect ui_rect_inset(ui_rect r, int by);

#endif
