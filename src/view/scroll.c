/* See scroll.h. */

#include "view/scroll.h"

void ui_scroll_span(ui_scroll *s, int view_len, int pad) {
    if (!s) return;
    s->view = view_len > 0 ? view_len : 0;
    s->pad  = pad > 0 ? pad : 0;
}

int ui_scroll_place(const ui_scroll *s, int at) { return s ? s->pad + at - s->offset : at; }

int ui_scroll_content(const ui_scroll *s, int items_len) {
    if (!s) return items_len;
    return s->pad + (items_len > 0 ? items_len : 0) + s->pad;
}

void ui_scroll_into_view(ui_scroll *s, int from, int to, int items_len) {
    int peek, most, near, far;

    if (!s || s->view <= 0) return;

    near = s->pad + from;
    far  = s->pad + to;

    peek = (to - from) / 2;
    if (peek > s->view / 3) peek = s->view / 3;
    if (peek < 0) peek = 0;

    /* An item longer than the view aligns to the near edge. */
    if (near - peek < s->target)
        s->target = near - peek;
    else if (far + peek > s->target + s->view)
        s->target = far + peek - s->view;

    /* Without the clamp the peek holds the start at the first item's own margin
       and the end short of the last one's. */
    most = ui_scroll_content(s, items_len) - s->view;
    if (most < 0) most = 0;
    if (s->target > most) s->target = most;
    if (s->target < 0) s->target = 0;
}

void ui_scroll_step(ui_scroll *s) {
    int d;

    if (!s) return;
    d = s->target - s->offset;

    /* A constant speed took 43 frames to come back from the end of a twenty-row
       list and arrived at full tilt. The +/-2 stops the last pixel or two
       rounding to no movement. */
    if (d > 0)
        s->offset += (d + 2) / 3;
    else if (d < 0)
        s->offset += (d - 2) / 3;
}
