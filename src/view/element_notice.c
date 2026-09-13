/* A page with nothing on it but something to say: a button, a waiting ring,
 * and the block those go in. See element.h. */

#include "view/element.h"

#include "base/corners.h"
#include "port/gfx.h"
#include "port/input.h"
#include "view/theme.h"

#define WHITE 0xFFFFFFu

/* BUTTON_MIN, so two side by side do not read as two sizes. */
#define BUTTON_PAD 14
#define BUTTON_MIN 64
#define QUIET_PAD  7

#define SPIN_R   8
#define SPIN_GAP 8

#define NOTICE_GAP 12

static int button_w(const char *label) {
    int w = text_width(TEXT_BODY, label) + 2 * BUTTON_PAD;

    return w < BUTTON_MIN ? BUTTON_MIN : w;
}

/* The quiet button differs from the plain one in size only. */
static void button(ui_rect at, const char *label, int hot, text_face_id f) {
    const ui_theme *t  = ui_theme_now();
    int             ty = ui_text_y(f, at.y, at.h);
    int             tx = at.x + (at.w - text_width(f, label)) / 2;

    if (hot) {
        /* Corners included: a rectangle inset in a rounded fill leaves the
           flat colour showing where the corner curves away. */
        gfx_round_hgrad(at.x, at.y, at.w, at.h, 4, t->brand_a, t->brand_b);
        text_draw(tx, ty, f, WHITE, label);
    } else {
        gfx_round_fill(at.x, at.y, at.w, at.h, 4, UI_BUTTON_BG);
        gfx_round_frame(at.x, at.y, at.w, at.h, 4, UI_BUTTON_EDGE);
        text_draw(tx, ty, f, UI_FG_DIM, label);
    }
}

int ui_button(ui_frame *ui, ui_id id, const char *label) {
    ui_rect at  = ui_take(&ui->layout, button_w(label), UI_BUTTON_H);
    int     hot = ui_touch(ui, id, at);

    button(at, label, hot, TEXT_BODY);
    return hot && (ui->pressed & PAD_CROSS);
}

int ui_button_quiet(ui_frame *ui, ui_id id, const char *label) {
    ui_rect at  = ui_take(&ui->layout, text_width(TEXT_SMALL, label) + 2 * QUIET_PAD, UI_BUTTON_QUIET_H);
    int     hot = ui_touch(ui, id, at);

    button(at, label, hot, TEXT_SMALL);
    return hot && (ui->pressed & PAD_CROSS);
}

/* Eight positions on a circle, by hand: a trig ring at five pixels lands two
   dots on the same square and reads as seven. */
static const signed char kSpin[8][2] = {{0, -5}, {4, -4}, {5, 0}, {4, 4}, {0, 5}, {-4, 4}, {-5, 0}, {-4, -4}};

void ui_spin_at(int cx, int cy, unsigned frame) {
    unsigned head = (frame / 4) & 7; /* one turn every half second */
    int      i;

    for (i = 0; i < 8; i++) {
        unsigned age = (head - (unsigned)i) & 7;

        gfx_fill(cx + kSpin[i][0] - 1, cy + kSpin[i][1] - 1, 2, 2, age == 0 ? 0x7AD4F4u : age <= 2 ? 0x5486B4u : 0x384058u);
    }
}

void ui_waiting(ui_layout *L, const char *text, unsigned frame) {
    ui_rect box = ui_here(L);
    int     cx, cy;

    if (box.w <= 0 || box.h <= 0) return;

    /* The pair is centred, not the ring: the ring alone puts the group
       visibly low. */
    cx = box.x + box.w / 2;
    cy = box.y + (box.h - (SPIN_R * 2 + SPIN_GAP + text_height(TEXT_BODY))) / 2 + SPIN_R;

    ui_spin_at(cx, cy, frame);

    if (text && text[0]) {
        int w = text_width(TEXT_BODY, text);

        (void)ui_text_fit(cx - w / 2, cy + SPIN_R + SPIN_GAP, TEXT_BODY, UI_FG_DIM, text, box.w);
    }
}

/* Enough for the longest sentence these screens produce, and short enough that
   the button under it stays in the block. */
#define NOTICE_BODY_LINES 4

/* Must count what ui_notice() draws, the heading's lead included: an
   immediate-mode page has no second pass to discover it. */
int ui_notice_h(ui_layout *L, const char *head, const char *body, const char *button) {
    const ui_role_style *hs   = ui_style_of(UI_HEADING);
    const ui_role_style *bs   = ui_style_of(UI_BODY);
    int                  tall = 0;

    if (head) tall += ui_started(L) ? hs->lead + hs->band : hs->band;
    if (body) {
        if (head) tall += bs->lead;
        {
            /* Capped: a fault sentence is whatever the server produced, and on
               the boot and offline screens the button it would push out is the
               only way out. */
            int lines = ui_lines_for(UI_BODY, body, ui_here(L).w);

            if (lines > NOTICE_BODY_LINES) lines = NOTICE_BODY_LINES;
            tall += lines * bs->band;
        }
    }
    if (button) tall += NOTICE_GAP + UI_BUTTON_H;
    return tall;
}

int ui_notice(ui_frame *ui, ui_id id, const char *head, const char *body, const char *button) {
    ui_layout *L    = &ui->layout;
    int        room = ui_here(L).h;
    int        tall = ui_notice_h(L, head, body, button);
    int        lead;

    /* Centred in the whole box: centred in what was left, the page's top
       margin went unbalanced -- 70 above and 58 under. */
    lead = (room - tall) / 2 - ui_used(L);
    if (lead < 0) lead = 0;
    ui_gap(L, lead);

    if (head) ui_heading(L, head);
    if (body) ui_text(L, UI_BODY, body, NOTICE_BODY_LINES);
    if (button) {
        ui_gap(L, NOTICE_GAP);
        return ui_button(ui, id, button);
    }
    return 0;
}

/* Row-shaped, not a ring: a ring's landing jumps the layout, a skeleton's only
 * changes the ink. Only the title pulses; one moving thing reads as alive,
 * three as noise. */
#define SKEL_THUMB_W 28
#define SKEL_THUMB_H 40
#define SKEL_PAD     8
#define SKEL_TITLE_W 150
#define SKEL_SUB_W   60
#define SKEL_BAR_H   6
#define SKEL_PERIOD  96u

void ui_skeleton(ui_layout *L, int row_h, int rows, unsigned frame) {
    const ui_theme *t = ui_theme_now();
    int             i;
    unsigned        phase = frame % SKEL_PERIOD;
    int             lit   = (int)(phase < SKEL_PERIOD / 2 ? phase : SKEL_PERIOD - phase);
    unsigned        ink   = corners_ramp(UI_FG_FAINT, t->bg_top, lit, (int)SKEL_PERIOD / 2);

    for (i = 0; i < rows; i++) {
        ui_rect at = ui_take(L, UI_FILL, row_h);
        int     tx = at.x + SKEL_THUMB_W + SKEL_PAD;
        int     ty = at.y + (row_h - SKEL_THUMB_H) / 2;

        if (at.h < row_h) break;

        gfx_round_fill(at.x, ty, SKEL_THUMB_W, SKEL_THUMB_H, UI_ART_RADIUS, UI_WELL_BG);
        /* Each title shorter than the last: identical bars read as a table. */
        gfx_round_fill(tx, ty + 6, SKEL_TITLE_W - i * 9, SKEL_BAR_H, 2, ink);
        gfx_round_fill(tx, ty + 6 + SKEL_BAR_H + 6, SKEL_SUB_W, SKEL_BAR_H, 2, UI_WELL_BG);
    }
}
