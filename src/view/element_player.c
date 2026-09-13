/* The pieces the player's band is made of. See element.h. */

#include "view/element.h"

#include "jelly/item.h"
#include "view/layout.h"
#include "port/text.h"

#include <stdio.h>

#include "port/gfx.h"
#include "view/theme.h"

#define SCRIM_INK 0x04060Au
#define WHITE     0xFFFFFFu

#define UI_SEEK_MARK 5

/* The transport icons, centred on cx,cy. */
#define UI_ICON 12
typedef enum { UI_ICON_PLAY = 0, UI_ICON_PAUSE, UI_ICON_PREV, UI_ICON_NEXT } ui_icon_id;

/* One blend per row: under the console's display list a full-panel scrim
   written per pixel is 130,560 quads against a 6,000-primitive budget and a
   256 kB list, which overflows and takes the machine with it. */
void ui_scrim(ui_rect at, int fade_at_top) {
    int row;

    if (at.h <= 0 || at.w <= 0) return;

    for (row = 0; row < at.h; row++) {
        int a = UI_SCRIM_ALPHA;

        if (fade_at_top) {
            if (row < UI_SCRIM_FADE) a = (UI_SCRIM_ALPHA * (row + 1)) / UI_SCRIM_FADE;
        } else {
            if (row >= at.h - UI_SCRIM_FADE) a = (UI_SCRIM_ALPHA * (at.h - row)) / UI_SCRIM_FADE;
        }
        /* UI_SCRIM_ALPHA is eighths and the API is 0..255: taken unscaled, the
           band darkened the film by 5/255, no scrim at all over video. */
        gfx_blend_rect(at.x, at.y + row, at.w, 1, SCRIM_INK, a * 255 / UI_SCRIM_STEPS);
    }
}

static int along(ui_rect at, int v, int low, int high) {
    if (high <= low) return at.x;
    if (v < low) v = low;
    if (v > high) v = high;
    return at.x + (int)(((long long)(v - low) * at.w) / (high - low));
}

/* `at` is the track; the play head and cursor stand UI_SEEK_MARK proud of it
 * above and below. The fill runs to position, never to the cursor.
 * `buffered` below `position` draws nothing; `force_cursor` draws the cursor
 * even on the position, to say the bar is what left and right will move. */
static void ui_seek_bar(ui_rect at, int cursor, int position, int buffered, int low, int high, int force_cursor) {
    const ui_theme *t = ui_theme_now();
    int             r, filled;

    if (at.w <= 0 || at.h <= 0) return;

    r = at.h / 2;
    if (r > 3) r = 3;

    gfx_round_fill(at.x, at.y, at.w, at.h, r, UI_TRACK);

    /* Without it a stall and a healthy stream look identical. At least two
       pixels: a few seconds of buffer on a two-hour bar is a fraction of one. */
    if (buffered > position) {
        int from = along(at, position, low, high);
        int to   = along(at, buffered, low, high);

        if (to < from + 2) to = from + 2;
        if (to > at.x + at.w) to = at.x + at.w;
        if (to > from) gfx_fill(from, at.y + 1, to - from, at.h - 2, UI_BUFFERED);
    }

    filled = along(at, position, low, high) - at.x;
    if (filled > 0)
        gfx_round_hgrad_caps(at.x, at.y, filled, at.h, r, t->brand_a, t->brand_b, filled >= at.w - r ? GFX_CAP_BOTH : GFX_CAP_LEFT);

    /* Always: a mark reads even on a bar that is nearly empty or nearly full. */
    gfx_round_fill(along(at, position, low, high), at.y - 3, 2, at.h + 6, 1, WHITE);

    if (cursor != position || force_cursor)
        gfx_round_fill(along(at, cursor, low, high), at.y - UI_SEEK_MARK, 2, at.h + 2 * UI_SEEK_MARK, 1, t->brand_a);
}

static void icon_skip(int cx, int cy, unsigned rgb, int dir) {
    int tw = 8, th = UI_ICON - 2, barw = 2, gap = 2;
    int tx = cx - (tw + gap + barw) / 2;

    if (dir == UI_ARROW_RIGHT) {
        ui_arrow(tx, cy - th / 2, tw, th, dir, rgb);
        gfx_round_fill(tx + tw + gap, cy - th / 2, barw, th, 1, rgb);
    } else {
        gfx_round_fill(tx, cy - th / 2, barw, th, 1, rgb);
        ui_arrow(tx + barw + gap, cy - th / 2, tw, th, dir, rgb);
    }
}

static void ui_icon_draw(int cx, int cy, ui_icon_id which, unsigned rgb) {
    switch (which) {
    case UI_ICON_PLAY: ui_arrow(cx - UI_ICON / 2, cy - UI_ICON / 2, UI_ICON, UI_ICON, UI_ARROW_RIGHT, rgb); break;
    case UI_ICON_PAUSE: {
        int h = UI_ICON, w = 3, gap = 3;

        gfx_round_fill(cx - gap / 2 - w, cy - h / 2, w, h, 1, rgb);
        gfx_round_fill(cx + gap / 2, cy - h / 2, w, h, 1, rgb);
        break;
    }
    case UI_ICON_PREV: icon_skip(cx, cy, rgb, UI_ARROW_LEFT); break;
    default: icon_skip(cx, cy, rgb, UI_ARROW_RIGHT); break;
    }
}

/* `enabled` 0 draws it faint and registers nothing, so the cursor steps over it
 * by geometry. The page resolves the presses. */
static void ui_icon_button_at(ui_frame *ui, ui_id id, ui_rect at, ui_icon_id which, int enabled) {
    int hot = enabled && ui_touch(ui, id, at);

    ui_icon_draw(at.x + at.w / 2, at.y + at.h / 2, which, !enabled ? UI_FG_FAINT : hot ? ui_theme_now()->accent : UI_FG);
}

/* The page owns the transport and the timeline; this owns how the band looks.
 * BAND_H counts both UI_SEEK_MARKs: spaced to the track alone, the play head
 * lands among the buttons. */
#define BAND_PAD     6 /* inside the band, left and right   */
#define BAND_PAD_BOT 2
/* The one knob that moves the contents down the panel: the band's y is
   GFX_H - height, so growing it moves only its top edge. */
#define BAND_DROP 14
#define ROW_H     14 /* the clock / transport / run time line */
#define CTL_GAP   3  /* that line to the top of the play head */
#define BAR_H     6  /* the seek TRACK, marks not included    */
#define BAR_GAP   2  /* the bar to the hints                  */
#define HINT_H    (UI_BTN_SIZE + 2)

#define BAND_H (BAND_DROP + BAND_PAD + ROW_H + CTL_GAP + UI_SEEK_MARK + BAR_H + UI_SEEK_MARK + BAR_GAP + HINT_H + BAND_PAD_BOT)

/* The strip carrying the film's name, outside the band's rectangle. */
#define TOP_H 27

/* A button's box is what the cursor aims at: the icon plus air. */
#define CTL_STEP 26
#define CTL_W    22

/* Not raw ticks, which overflow 32 bits past seven minutes. */
#define BAR_SPAN 1000

static int on_bar(const ui_band *b, unsigned long long ticks) {
    if (!b->dur) return 0;
    if (ticks > b->dur) ticks = b->dur;
    return (int)(ticks * (unsigned long long)BAR_SPAN / b->dur);
}

/* "1:02:03" past an hour, "2:03" under one. `with_hours` is the duration's
   shape, not this value's, so a clock does not change width as it runs. */
static void fmt_time(char *out, unsigned n, unsigned long long ticks, int with_hours) {
    unsigned s = (unsigned)(ticks / ITEM_TICKS_PER_S);

    if (with_hours)
        snprintf(out, n, "%u:%02u:%02u", s / 3600u, (s / 60u) % 60u, s % 60u);
    else
        snprintf(out, n, "%u:%02u", s / 60u, s % 60u);
}

/* Signed, and seconds under a minute: "+20s" is a quantity where "+0:20"
   invites being read as a time. */
static void fmt_delta(char *out, unsigned n, long long ticks) {
    char     sign = ticks < 0 ? '-' : '+';
    unsigned s;

    if (ticks < 0) ticks = -ticks;
    s = (unsigned)((unsigned long long)ticks / ITEM_TICKS_PER_S);
    if (s < 60u)
        snprintf(out, n, "%c%us", sign, s);
    else if (s < 3600u)
        snprintf(out, n, "%c%u:%02u", sign, s / 60u, s % 60u);
    else
        snprintf(out, n, "%c%u:%02u:%02u", sign, s / 3600u, (s / 60u) % 60u, s % 60u);
}

/* The readout's own scrim is sized to the numbers, so it does not blot out the
   film's name. */
#define STAT_PAD  6
#define STAT_VPAD 3
/* Deeper under the last line than over the first: the small face's descenders
   otherwise sit on the bottom edge of their own background. */
#define STAT_VPAD_BOT 8

static void draw_stats(const ui_band *b) {
    int n = b->stat_count, i, w = 0, h, x, y;

    if (!b->stats || !b->stat_lines || n <= 0) return;

    for (i = 0; i < n; i++) {
        int tw = text_width(TEXT_SMALL, b->stat_lines[i]);

        if (tw > w) w = tw;
    }
    w += STAT_PAD * 2;
    if (w > GFX_W) w = GFX_W;
    x = GFX_W - w;

    h = n * text_height(TEXT_SMALL) + STAT_VPAD + STAT_VPAD_BOT;
    ui_scrim(ui_rect_make(x, 0, w, h), 0);

    y = STAT_VPAD;
    for (i = 0; i < n; i++) {
        (void)ui_text_fit(x + STAT_PAD, ui_text_y(TEXT_SMALL, y, text_height(TEXT_SMALL)), TEXT_SMALL, UI_FG, b->stat_lines[i],
                          w - 2 * STAT_PAD);
        y += text_height(TEXT_SMALL);
    }
}

static void draw_hints(const ui_band *b, ui_rect at, int on_bar_now) {
    static const ui_hint_item CTL[] = {
        {UI_BTN_CROSS, "select"},
        {UI_BTN_UPDOWN, "seek bar"},
        {UI_BTN_SQUARE, "hide"},
    };
    static const ui_hint_item SEEK[] = {
        {UI_BTN_CROSS, "jump"},
        {UI_BTN_CIRCLE, "cancel"},
    };
    static const ui_hint_item BAR[] = {
        {UI_BTN_LEFTRIGHT, "seek"},
        {UI_BTN_CROSS, "pause"},
        {UI_BTN_SQUARE, "hide"},
        {UI_BTN_CIRCLE, "stop"},
    };
    const ui_hint_item *hints = b->seek_armed ? SEEK : on_bar_now ? BAR : CTL;
    int                 n     = b->seek_armed ? 2 : on_bar_now ? 4 : 3;

    (void)ui_hint_row_right(at.x + at.w, at.y, at.x, hints, n);
}

void ui_player_band(ui_frame *ui, const ui_band *b) {
    ui_rect band = ui_rect_make(0, GFX_H - BAND_H, GFX_W, BAND_H);
    ui_rect row, bar, hints;
    char    now[16], total[16];
    int     hours = (b->dur >= 3600ull * ITEM_TICKS_PER_S);
    int     bar_has_focus;

    if (!b->up) {
        /* The readout is triangle's, not the band's: hiding the band must not
           take it with it. */
        draw_stats(b);
        return;
    }

    {
        ui_rect top = ui_rect_make(0, 0, GFX_W, TOP_H);

        ui_scrim(top, 0);
        (void)ui_text_fit(BAND_PAD, ui_text_y(TEXT_SMALL, 0, TOP_H), TEXT_SMALL, UI_FG, b->title, GFX_W - 2 * BAND_PAD);
    }
    ui_scrim(band, 1);

    /* Not through the layout: the top line is anchored to the left margin,
       the panel's centre and the right margin, which a column of rows cannot
       say. */
    row   = ui_rect_make(BAND_PAD, band.y + BAND_DROP, GFX_W - 2 * BAND_PAD, ROW_H);
    bar   = ui_rect_make(BAND_PAD, row.y + row.h + CTL_GAP + UI_SEEK_MARK, GFX_W - 2 * BAND_PAD, BAR_H);
    hints = ui_rect_make(BAND_PAD, bar.y + bar.h + UI_SEEK_MARK + BAR_GAP, GFX_W - 2 * BAND_PAD, HINT_H);

    /* The bar's box is the track plus the marks. */
    (void)ui_touch(ui, b->id_bar, ui_rect_make(bar.x, bar.y - UI_SEEK_MARK, bar.w, bar.h + 2 * UI_SEEK_MARK));

    /* Left and right on the bar seek instead of walking, so the frame is told
       where the cursor is. Without this nothing on the band ever lit up. */
    {
        const ui_id of_ctl[] = {b->id_bar, b->id_prev, b->id_play, b->id_next};

        ui_frame_focus_set(ui, of_ctl[b->ctl]);
        bar_has_focus = (b->ctl == UI_CTL_NONE);
    }

    {
        const ui_theme *t  = ui_theme_now();
        int             ty = ui_text_y(TEXT_SMALL, row.y, row.h);

        /* The caption takes the clock's place: centred, it lands on the
           buttons. */
        if (b->cap == UI_CAP_BUFFERING) {
            static const char *DOTS[4] = {"Buffering", "Buffering.", "Buffering..", "Buffering..."};

            (void)ui_text_fit(row.x, ty, TEXT_SMALL, t->accent, DOTS[(b->tick / 6) & 3], row.w);
        } else {
            int at;

            fmt_time(now, sizeof(now), b->seek_armed ? b->seek_to : b->pos, hours);
            at = ui_text_fit(row.x, ty, TEXT_SMALL, b->seek_armed ? t->accent : UI_FG, now, row.w);
            if (b->seek_armed) {
                char delta[16];

                fmt_delta(delta, sizeof(delta), (long long)b->seek_to - (long long)b->pos);
                (void)ui_text_fit(at + 8, ty, TEXT_SMALL, t->accent, delta, row.x + row.w - (at + 8));
            }
        }

        fmt_time(total, sizeof(total), b->dur, hours);
        (void)ui_text_fit(row.x + row.w - text_width(TEXT_SMALL, total), ty, TEXT_SMALL, UI_FG_DIM, total, text_width(TEXT_SMALL, total));

        /* Centred on the panel rather than between the clocks, so they do not
           shift when a delta appears. */
        {
            int     cy = row.y + row.h / 2;
            ui_rect b0 = ui_rect_make(GFX_W / 2 - CTL_STEP - CTL_W / 2, cy - ROW_H / 2, CTL_W, ROW_H);
            ui_rect b1 = ui_rect_make(GFX_W / 2 - CTL_W / 2, cy - ROW_H / 2, CTL_W, ROW_H);
            ui_rect b2 = ui_rect_make(GFX_W / 2 + CTL_STEP - CTL_W / 2, cy - ROW_H / 2, CTL_W, ROW_H);

            ui_icon_button_at(ui, b->id_prev, b0, UI_ICON_PREV, b->has_prev);
            ui_icon_button_at(ui, b->id_play, b1, b->paused ? UI_ICON_PLAY : UI_ICON_PAUSE, 1);
            ui_icon_button_at(ui, b->id_next, b2, UI_ICON_NEXT, b->has_next);
        }
    }

    ui_seek_bar(bar, b->seek_armed ? on_bar(b, b->seek_to) : on_bar(b, b->pos), on_bar(b, b->pos),
                b->buffered ? on_bar(b, b->pos + b->buffered) : -1, 0, BAR_SPAN, bar_has_focus);

    draw_hints(b, hints, bar_has_focus);

    /* Last, so it draws over the title strip rather than instead of it. */
    draw_stats(b);
}
