/* See element.h. */

#include "view/element.h"

#include "port/gfx.h"
#include "port/input.h"
#include "view/theme.h"

#define WHITE 0xFFFFFFu

#define ROW_H    24
#define ROW_GAP  10
#define SLIDER_W 110
#define SLIDER_H 8
#define SWITCH_W 24
#define SWITCH_H 12

void ui_row_wash(ui_rect at) {
    const ui_theme *t = ui_theme_now();

    gfx_hgrad(at.x, at.y, at.w, at.h, t->sel_a, t->sel_b);
    gfx_vgrad(at.x, at.y, 3, at.h, t->brand_a, t->brand_b);
}

static void switch_at(int x, int y, int on) {
    const ui_theme *t = ui_theme_now();
    int             w = SWITCH_W, h = SWITCH_H;
    int             r    = h / 2; /* true half-circle ends */
    int             knob = h - 4;

    if (on) {
        gfx_round_hgrad(x, y, w, h, r, t->brand_a, t->brand_b);
    } else {
        gfx_round_fill(x, y, w, h, r, 0x161A24u);
        gfx_round_frame(x, y, w, h, r, 0x3B4457u);
    }

    /* A circle: at eight pixels a rounded rectangle is a different shape. */
    gfx_round_fill(on ? x + w - knob - 2 : x + 2, y + 2, knob, knob, knob / 2, on ? WHITE : 0x768095u);
}

static void slider_at(ui_rect bar, int value, int low, int high, int focused) {
    const ui_theme *t = ui_theme_now();
    int             r, filled;

    if (bar.w <= 0 || bar.h <= 0 || high <= low) return;
    if (value < low) value = low;
    if (value > high) value = high;

    r      = bar.h / 2;
    filled = bar.w * (value - low) / (high - low);

    gfx_round_fill(bar.x, bar.y, bar.w, bar.h, r, 0x222836u);
    if (filled > 0)
        gfx_round_hgrad_caps(bar.x, bar.y, filled, bar.h, r, t->brand_a, t->brand_b, filled >= bar.w - r ? GFX_CAP_BOTH : GFX_CAP_LEFT);

    /* Concentric with the bar: at the bar's own radius the curves pinch at the
       corners. */
    if (focused) gfx_round_frame(bar.x - 2, bar.y - 2, bar.w + 4, bar.h + 4, r + 2, t->brand_a);
}

static ui_rect bleed(ui_rect box) { return ui_rect_make(0, box.y, GFX_W, box.h); }

int ui_option(ui_frame *ui, ui_id id, const char *label, const char *value, ui_option_kind kind, int at, int lo, int hi) {
    const ui_role_style *lab = ui_style_of(UI_BODY);
    const ui_role_style *val = ui_style_of(UI_VALUE);
    ui_layout           *L   = &ui->layout;
    ui_rect              row = ui_take(L, UI_FILL, ROW_H);
    int                  hot = ui_touch(ui, id, bleed(row));

    if (hot) ui_row_wash(bleed(row));

    ui_row_at(L, row, ROW_GAP);
    if (kind == UI_OPTION_SLIDER) {
        slider_at(ui_take_end(L, SLIDER_W, SLIDER_H), at, lo, hi, hot);
    } else if (kind == UI_OPTION_SWITCH) {
        ui_rect s = ui_take_end(L, SWITCH_W, SWITCH_H);

        switch_at(s.x, s.y, at);
    }

    if (value && value[0]) {
        ui_rect v = ui_take_end(L, ui_text_fit_w(val->face, value, 0), UI_FILL);

        (void)ui_text_fit(v.x, ui_text_y(val->face, v.y, v.h), val->face, ui_role_ink(UI_VALUE), value, v.w);
    }
    {
        /* Whatever is left, so a long label is cut rather than running under
           the value beside it. */
        ui_rect l = ui_take(L, UI_FILL, UI_FILL);

        (void)ui_text_fit(l.x, ui_text_y(lab->face, l.y, l.h), lab->face, hot ? UI_FG : ui_role_ink(UI_BODY), label, l.w);
    }
    ui_end(L);
    return hot;
}

#define PICK_W      320
#define PICK_ROW_H  20
#define PICK_ROWS   8
#define PICK_HEAD_H 24
#define PICK_PAD    10

int ui_pick_list(ui_frame *ui, ui_scroll *s, const char *title, const char *const *rows, int n, int current, ui_id first_id) {
    const ui_theme *t     = ui_theme_now();
    text_face_id    f     = ui_style_of(UI_BODY)->face;
    int             shown = n < PICK_ROWS ? n : PICK_ROWS;
    int             h     = PICK_HEAD_H + shown * PICK_ROW_H + PICK_PAD / 2;
    ui_rect         box   = ui_rect_make((GFX_W - PICK_W) / 2, (GFX_H - h) / 2, PICK_W, h);
    /* Inside the frame, or a selected row's wash paints over it. */
    ui_rect view = ui_rect_make(box.x + 1, box.y + PICK_HEAD_H, box.w - 2, shown * PICK_ROW_H);
    int             i, chosen = -1, focus_at = -1;

    /* The page under it registered first; dropping those leaves the cursor
       nowhere to go but these rows. */
    ui->entries = 0;

    gfx_blend_rect(0, 0, GFX_W, GFX_H, UI_SCRIM_INK, UI_SCRIM_ALPHA * 255 / UI_SCRIM_STEPS);
    gfx_round_fill(box.x, box.y, box.w, box.h, 6, t->panel_top);
    gfx_round_frame(box.x, box.y, box.w, box.h, 6, UI_BUTTON_EDGE);
    (void)ui_text_fit(box.x + PICK_PAD, ui_text_y(TEXT_SMALL, box.y, PICK_HEAD_H), TEXT_SMALL, UI_FG_DIM, title, box.w - 2 * PICK_PAD);

    ui_scroll_span(s, view.h, 0);
    gfx_scissor(view.x, view.y, view.w, view.h);
    for (i = 0; i < n; i++) {
        ui_rect at  = ui_rect_make(view.x, view.y + ui_scroll_place(s, i * PICK_ROW_H), view.w, PICK_ROW_H);
        int     hot = ui_touch(ui, first_id + (ui_id)i, at);

        if (hot) {
            focus_at = i;
            if (ui->pressed & PAD_CROSS) chosen = i;
            ui_row_wash(at);
        }
        (void)ui_text_fit(at.x + PICK_PAD, ui_text_y(f, at.y, at.h), f, hot ? UI_FG : i == current ? t->accent : UI_FG_DIM, rows[i],
                          at.w - 2 * PICK_PAD);
    }
    gfx_scissor_none();
    ui_scrollbar(view, s->offset, ui_scroll_content(s, n * PICK_ROW_H), 1);

    if (focus_at >= 0) ui_scroll_into_view(s, focus_at * PICK_ROW_H, focus_at * PICK_ROW_H + PICK_ROW_H, n * PICK_ROW_H);
    ui_scroll_step(s);
    return chosen;
}
