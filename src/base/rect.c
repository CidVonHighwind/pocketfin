/* See rect.h. */

#include "base/rect.h"

ui_rect ui_rect_make(int x, int y, int w, int h) {
    ui_rect r;

    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

ui_rect ui_rect_inset(ui_rect r, int by) {
    r.x += by;
    r.y += by;
    r.w -= 2 * by;
    r.h -= 2 * by;
    if (r.w < 0) r.w = 0;
    if (r.h < 0) r.h = 0;
    return r;
}
