/* Artwork, the marks that go beside it, and the row that carries both. See
 * element.h. */

#include "view/element.h"

#include "port/platform.h"

#include "port/gfx.h"
#include "view/theme.h"

#define ROW_GAP 10
#define MARK_IN 2
#define MARK_W  22
#define MARK_H  3
#define TICK_W  14

void ui_artwork(ui_rect slot, const ui_art *art, int focused, int fixed, unsigned bg) {
    const ui_theme *t    = ui_theme_now();
    int             have = (art && art->px && art->w > 0 && art->h > 0);
    int             pw = have ? art->w : 0, ph = have ? art->h : 0;
    int             w = (have && !fixed) ? pw : slot.w;
    int             h = (have && !fixed) ? ph : slot.h;
    int             x, y;

    if (w > slot.w) w = slot.w;
    if (h > slot.h) h = slot.h;
    x = slot.x + (slot.w - w) / 2;
    y = slot.y + (slot.h - h) / 2;

    if (have) {
        /* Picture and frame as one composite: the shape decides the pixel's
           alpha, the frame only its colour. Drawn separately, the ring samples
           an arc the picture has already covered and the corners come out
           brighter than the sides. */
        int px_w = pw > slot.w ? slot.w : pw;
        int px_h = ph > slot.h ? slot.h : ph;

        /* No well behind a picture: a poster fitted to a 28x40 slot comes back
           about 26 wide, and the well showed as a band down each side. */
        gfx_blit_card(x + (w - px_w) / 2, y + (h - px_h) / 2, art->px, art->tex_w, art->tex_h, px_w, px_h, UI_ART_RADIUS,
                      focused ? t->brand_a : UI_WELL_EDGE, focused ? t->brand_b : UI_WELL_EDGE, bg);
        return;
    }

    /* Rounded: a rectangular gradient would square off the corners of every
       selected card. */
    if (focused)
        gfx_round_hgrad(x - 1, y - 1, w + 2, h + 2, UI_ART_EDGE_R, t->brand_a, t->brand_b);
    else
        gfx_round_fill(x - 1, y - 1, w + 2, h + 2, UI_ART_EDGE_R, UI_WELL_EDGE);
    gfx_round_fill(x, y, w, h, UI_ART_RADIUS, UI_WELL_BG);

    if (art) ui_art_dots(ui_rect_make(x, y, w, h), art->waiting);
}

/* Real time, not a frame count, so both machines walk the dots at the same
   rate. Stepped every ten frames they cycled in half a second, and a viewer
   saw one phase: a static mark. */
#define DOT_STEP_US 110000u

void ui_art_well(ui_rect at) {
    gfx_round_fill(at.x, at.y, at.w, at.h, UI_ART_RADIUS, UI_WELL_BG);
    gfx_round_frame(at.x, at.y, at.w, at.h, UI_ART_RADIUS, UI_WELL_EDGE);
}

void ui_art_dots(ui_rect at, unsigned waiting) {
    const ui_theme *t = ui_theme_now();
    int             lit, i;

    if (!waiting) return;
    lit = (int)((platform_clock_us() / DOT_STEP_US) % 3u);

    for (i = 0; i < 3; i++) gfx_fill(at.x + at.w / 2 - 8 + i * 6, at.y + at.h / 2 - 1, 3, 3, i == lit ? t->accent : UI_WELL_EDGE);
}

static void ui_progress(ui_rect bar, int num, int den) {
    const ui_theme *t = ui_theme_now();
    int             done, r;

    if (num <= 0 || den <= 0 || bar.w <= 0 || bar.h <= 0) return;

    done = (int)(((long long)num * bar.w) / den);
    if (done < 2) done = 2;
    if (done > bar.w) done = bar.w;

    r = bar.h / 2;
    if (r > 3) r = 3;

    gfx_round_fill(bar.x, bar.y, bar.w, bar.h, r, UI_TRACK);
    /* Square where it stops, unless it reaches the track's own cap. */
    gfx_round_hgrad_caps(bar.x, bar.y, done, bar.h, r, t->brand_a, t->brand_b, done >= bar.w - r ? GFX_CAP_BOTH : GFX_CAP_LEFT);
}

static void ui_tick(int cx, int cy, unsigned rgb) {
    int i;

    /* The second stroke starts where the first ENDED: begun a pixel along, the
       two never meet and the mark reads as a notched V. */
    for (i = 0; i < 3; i++) gfx_fill(cx - 4 + i, cy - 1 + i, 2, 2, rgb);
    for (i = 1; i < 5; i++) gfx_fill(cx - 2 + i, cy + 1 - i, 2, 2, rgb);
}

static int ui_watch_mark(int right, int cy, int num, int den, unsigned rgb) {
    if (den > 0 && num >= den) {
        ui_tick(right - 5, cy, rgb);
        return TICK_W;
    }
    if (num <= 0) return 0;

    ui_progress(ui_rect_make(right - MARK_W, cy - MARK_H / 2, MARK_W, MARK_H), num, den > 0 ? den : num);
    return MARK_W + 4;
}

void ui_scrollbar(ui_rect view, int offset, int content, int vertical) {
    int span  = (vertical ? view.h : view.w) - 2 * UI_SCROLLBAR_GAP;
    int reach = vertical ? view.h : view.w;
    int thumb, at, x, y;

    /* Also the guard on the divisor below. */
    if (content <= reach + 4 || span <= 8) return;

    thumb = span * reach / content;
    if (thumb < 8) thumb = 8;
    at = (span - thumb) * offset / (content - reach);
    if (at < 0) at = 0;
    if (at > span - thumb) at = span - thumb;

    if (vertical) {
        x = view.x + view.w - UI_SCROLLBAR_GAP - UI_SCROLLBAR_W;
        y = view.y + UI_SCROLLBAR_GAP;
        gfx_round_fill(x, y, UI_SCROLLBAR_W, span, UI_SCROLLBAR_W / 2, 0x1E2430u);
        gfx_round_fill(x, y + at, UI_SCROLLBAR_W, thumb, UI_SCROLLBAR_W / 2, UI_FG_FAINT);
    } else {
        x = view.x + UI_SCROLLBAR_GAP;
        y = view.y + view.h - UI_SCROLLBAR_GAP - UI_SCROLLBAR_W;
        gfx_round_fill(x, y, span, UI_SCROLLBAR_W, UI_SCROLLBAR_W / 2, 0x1E2430u);
        gfx_round_fill(x + at, y, thumb, UI_SCROLLBAR_W, UI_SCROLLBAR_W / 2, UI_FG_FAINT);
    }
}

/* A row's wash and the box the cursor aims at run to the panel's edges; its
   contents keep the page's margin. */
static ui_rect bleed(ui_rect box) { return ui_rect_make(0, box.y, GFX_W, box.h); }

int ui_media_row_at(ui_frame *ui, ui_id id, ui_rect box, int draw, const ui_art *art, int art_w, int art_h, const char *title,
                    const char *subtitle, int pct) {
    const ui_theme      *t   = ui_theme_now();
    const ui_role_style *ts  = ui_style_of(UI_BODY);
    const ui_role_style *ss  = ui_style_of(UI_CAPTION);
    ui_layout           *L   = &ui->layout;
    int                  hot = ui_touch(ui, id, bleed(box));

    if (!draw) return hot;
    if (hot) ui_row_wash(bleed(box));

    ui_row_at(L, box, ROW_GAP);
    ui_artwork(ui_take(L, art_w, art_h), art, 0, 1, t->bg_top);

    /* The mark's width comes off the end, so the text gets what is left. */
    {
        ui_rect r = ui_here(L);

        (void)ui_take_end(L, ui_watch_mark(r.x + r.w - MARK_IN, box.y + box.h / 2, pct, 100, hot ? t->accent : UI_FG_FAINT), UI_FILL);
    }

    /* Centred as a pair: centring each on the row leaves the smaller one
       floating. */
    {
        ui_rect r = ui_rest(L);
        int     y = r.y + (r.h - text_height(ts->face) - text_height(ss->face)) / 2;

        (void)ui_text_fit(r.x, y, ts->face, hot ? UI_FG : ui_role_ink(UI_BODY), title, r.w);
        if (subtitle) (void)ui_text_fit(r.x, y + text_height(ts->face), ss->face, hot ? t->accent : ui_role_ink(UI_CAPTION), subtitle, r.w);
    }
    ui_end(L);
    return hot;
}

#define CHIP_PAD 6

void ui_chip(ui_layout *L, const char *text, unsigned ink, unsigned fill) {
    ui_rect at = ui_take(L, text_width(TEXT_SMALL, text) + 2 * CHIP_PAD, UI_CHIP_H);

    gfx_round_fill(at.x, at.y, at.w, UI_CHIP_H, 3, fill);
    (void)text_draw(at.x + CHIP_PAD, ui_text_y(TEXT_SMALL, at.y, UI_CHIP_H), TEXT_SMALL, ink, text);
}

#define CARD_SCRIM_H 13
#define CARD_BAR_H   2
#define WHITE        0xFFFFFFu

/* On the box's own edge: see UI_CARD_EDGE. */
static void artwork_edge(ui_rect box, int focused) {
    const ui_theme *t = ui_theme_now();

    if (focused)
        gfx_round_hgrad_frame(box.x, box.y, box.w, box.h, UI_ART_EDGE_R, t->brand_a, t->brand_b);
    else
        gfx_round_frame(box.x, box.y, box.w, box.h, UI_ART_EDGE_R, UI_WELL_EDGE);
}

/* A ramp rather than a filled band hides the band's own edge, and stopping
   short of opaque leaves the artwork under the caption. 3/8 to 7/8. */
#define CARD_SCRIM_A0 96
#define CARD_SCRIM_A1 223

static void card_caption(ui_rect over, const char *text, int bright) {
    int band = over.y + over.h - CARD_SCRIM_H;
    int i;

    for (i = 0; i < CARD_SCRIM_H; i++)
        gfx_blend_rect(over.x, band + i, over.w, 1, UI_SCRIM_INK,
                       CARD_SCRIM_A0 + (i * (CARD_SCRIM_A1 - CARD_SCRIM_A0)) / (CARD_SCRIM_H - 1));

    (void)ui_text_fit(over.x + 5, ui_text_y(TEXT_SMALL, band, CARD_SCRIM_H), TEXT_SMALL, bright ? WHITE : UI_FG, text, over.w - 10);
}

void ui_art_card(ui_rect slot, const ui_art *art, int focused, unsigned bg, int num, int den, const char *caption) {
    int     have = (art && art->px && art->w > 0 && art->h > 0);
    int     px_w, px_h, x, y, full;
    ui_rect inner;

    if (!have) {
        /* Inset: ui_artwork draws its edge one pixel outside what it is given,
           and a rail's scissor is exactly the card band tall, so handed the
           whole slot an empty card lost its top and bottom edges. */
        ui_artwork(ui_rect_inset(slot, UI_CARD_EDGE), art, focused, 1, bg);
        return;
    }

    /* Always the slot's own size, so a rail of cards lines up; a narrower
       picture is centred on the well. */
    inner = ui_rect_inset(slot, UI_CARD_EDGE);

    px_w = art->w > inner.w ? inner.w : art->w;
    px_h = art->h > inner.h ? inner.h : art->h;
    x    = inner.x + (inner.w - px_w) / 2;
    y    = inner.y + (inner.h - px_h) / 2;
    full = (px_w == inner.w && px_h == inner.h);

    /* The largest single fill on a page of artwork, and fully overdrawn when
       the picture covers the slot. */
    if (!full) gfx_round_fill(inner.x, inner.y, inner.w, inner.h, UI_ART_RADIUS, UI_WELL_BG);

    /* Rounded only when the picture is the card's own edge; smaller, it is a
       square photo on a rounded mat. No ring baked in: the bar would
       overwrite half of it. */
    gfx_blit_part(x, y, art->px, art->tex_w, art->tex_h, px_w, px_h, full ? UI_ART_RADIUS : 0, bg);

    /* One decoration, never both. Square ends: the ring drawn after rounds
       them. */
    if (num > 0 && den > 0)
        ui_progress(ui_rect_make(inner.x, inner.y + inner.h - CARD_BAR_H, inner.w, CARD_BAR_H), num, den);
    else if (caption && caption[0])
        card_caption(inner, caption, focused);

    /* Last: the decoration squared off the bottom corners. Sized to the slot,
       not the picture. */
    artwork_edge(slot, focused);
}
