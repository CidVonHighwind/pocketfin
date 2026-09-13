/* The page's two bars. See element.h. */

#include "view/element.h"

#include "port/gfx.h"
#include "view/theme.h"

#define BAR_X       8
#define MIN_TITLE_W 40

/* Not a theme colour: a rule that changed with the gradient reads as a
   mistake. */
#define HAIRLINE 0x2A3142u

#define TITLE_FG 0xFFFFFFu

/* Beside the status, not over it: a busy page still has a sort to name. */
#define SPIN_BOX 20

void ui_title_bar_at(ui_rect at, const char *title, const char *status, int arrow, int busy, unsigned frame) {
    const ui_theme *t       = ui_theme_now();
    int             right   = at.x + at.w - BAR_X;
    int             title_w = right - (at.x + BAR_X);
    int             ty      = ui_text_y(TEXT_TITLE, at.y, at.h);
    /* The status shares the title's baseline rather than its cap centre:
       centring each on the bar independently leaves the smaller floating. */
    int sy = ty + text_faces[TEXT_TITLE].ascent - text_faces[TEXT_SMALL].ascent;

    gfx_vgrad(at.x, at.y, at.w, at.h - 2, t->panel_top, t->panel_bot);
    gfx_hgrad(at.x, at.y + at.h - 2, at.w, 2, t->brand_a, t->brand_b);

    if (busy) {
        ui_spin_at(right - SPIN_BOX / 2, at.y + (at.h - 2) / 2, frame);
        right -= SPIN_BOX;
        title_w -= SPIN_BOX;
    }

    if (status && status[0]) {
        int tw = text_width(TEXT_SMALL, status);
        int sw = tw + (arrow >= 0 ? UI_ARROW_GAP + UI_ARROW_W : 0);
        int sx = at.x + (at.w - sw) / 2;

        /* Centred if it fits, and pushed off centre rather than over the
           title if it does not. */
        if (sx < at.x + BAR_X + MIN_TITLE_W) sx = at.x + BAR_X + MIN_TITLE_W;
        if (sx + sw > right) sx = right - sw;

        text_draw(sx, sy, TEXT_SMALL, t->accent, status);
        if (arrow >= 0)
            ui_arrow(sx + tw + UI_ARROW_GAP, sy + text_faces[TEXT_SMALL].ascent - UI_ARROW_H, UI_ARROW_W, UI_ARROW_H, arrow, t->accent);
        title_w = sx - BAR_X - (at.x + BAR_X);
    }
    (void)ui_text_fit(at.x + BAR_X, ty, TEXT_TITLE, TITLE_FG, title ? title : "", title_w);
}

void ui_footer_at(ui_rect at, const ui_hint_item *left, int left_n, const ui_hint_item *right, int right_n) {
    const ui_theme *t          = ui_theme_now();
    int             right_edge = at.x + at.w - BAR_X;
    int             y          = at.y;

    /* Inverted, so the two bars bracket the page rather than repeating it. */
    gfx_vgrad(at.x, y + 1, at.w, at.h - 1, t->panel_bot, t->panel_top);
    gfx_fill(at.x, y, at.w, 1, HAIRLINE);

    y += 5;
    if (right && right_n > 0) right_edge = ui_hint_row_right(right_edge, y, BAR_X, right, right_n);
    if (left && left_n > 0) (void)ui_hint_row(at.x + BAR_X, y, right_edge - 10, left, left_n);
}
