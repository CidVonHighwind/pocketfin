/* See nowplaying.h. */

#include "page/nowplaying.h"

#include "model/music.h"
#include "model/posters.h"
#include "page/item_text.h"
#include "port/gfx.h"
#include "port/input.h"
#include "port/platform.h"
#include "view/element.h"
#include "view/page_chrome.h"
#include "view/theme.h"

#include <stdio.h>

#define ART_W   200
#define ART_H   200
#define ART_GAP 8
#define COL_GAP 6
#define BAR_H   4

/* The transport: one round play button in the theme's gradient between two
   lighter skip glyphs, the way music players draw it. */
#define PLAY_D  40 /* the play button's diameter */
#define SKIP_D  32 /* the halo a focused skip button gets */
#define CTL_GAP 18
#define CTL_H   (PLAY_D + 4)

#define ID_PREV 0
#define ID_PLAY 1
#define ID_NEXT 2

typedef struct {
    unsigned frame;
    int      landed; /* focus put on Play once */
    int      dim;    /* the screen dimmed for listening */
} np_state;

static void clock_text(unsigned s, char *out, unsigned n) { snprintf(out, n, "%u:%02u", s / 60u, s % 60u); }

/* A triangle pointing right with its tip at x + w; drawn a row at a time. */
static void tri_right(int x, int y, int w, int h, unsigned rgb) { ui_arrow(x, y, w, h, UI_ARROW_RIGHT, rgb); }
static void tri_left(int x, int y, int w, int h, unsigned rgb) { ui_arrow(x, y, w, h, UI_ARROW_LEFT, rgb); }

/* |◀◀ and ▶▶|: two triangles and a bar, as on any player. */
static void glyph_skip(int cx, int cy, int dir, unsigned rgb) {
    int tw = 9, th = 14, bar = 3;
    int left = cx - (2 * tw + bar + 1) / 2, top = cy - th / 2;

    if (dir > 0) {
        tri_right(left, top, tw, th, rgb);
        tri_right(left + tw - 1, top, tw, th, rgb);
        gfx_round_fill(left + 2 * tw, top, bar, th, 1, rgb);
    } else {
        gfx_round_fill(left, top, bar, th, 1, rgb);
        tri_left(left + bar + 1, top, tw, th, rgb);
        tri_left(left + bar + tw, top, tw, th, rgb);
    }
}

static void glyph_play(int cx, int cy, unsigned rgb) {
    /* Nudged right: a triangle centred by its box looks off-centre in a circle. */
    tri_right(cx - 5, cy - 8, 13, 16, rgb);
}

static void glyph_pause(int cx, int cy, unsigned rgb) {
    gfx_round_fill(cx - 6, cy - 8, 4, 16, 1, rgb);
    gfx_round_fill(cx + 2, cy - 8, 4, 16, 1, rgb);
}

/* 1 when cross was pressed on it. */
static int skip_button(ui_frame *ui, ui_id id, int cx, int cy, int dir) {
    ui_rect box = ui_rect_make(cx - SKIP_D / 2, cy - SKIP_D / 2, SKIP_D, SKIP_D);
    int     hot = ui_touch(ui, id, box);

    if (hot) gfx_round_fill(box.x, box.y, box.w, box.h, SKIP_D / 2, ui_theme_now()->panel_top);
    glyph_skip(cx, cy, dir, hot ? ui_theme_now()->accent : UI_FG);
    return hot && (ui->pressed & PAD_CROSS);
}

static int play_button(ui_frame *ui, int cx, int cy, int paused) {
    const ui_theme *t   = ui_theme_now();
    ui_rect         box = ui_rect_make(cx - PLAY_D / 2, cy - PLAY_D / 2, PLAY_D, PLAY_D);
    int             hot = ui_touch(ui, ID_PLAY, box);

    if (hot) gfx_round_frame(box.x - 3, box.y - 3, box.w + 6, box.h + 6, PLAY_D / 2 + 3, UI_FG);
    gfx_round_hgrad(box.x, box.y, box.w, box.h, PLAY_D / 2, t->brand_a, t->brand_b);
    if (paused)
        glyph_play(cx, cy, 0xFFFFFFu);
    else
        glyph_pause(cx, cy, 0xFFFFFFu);
    return hot && (ui->pressed & PAD_CROSS);
}

/* Dimmed: black, with one faint line so it is plainly still playing. The
   console's own Backlight Auto-Off then turns the light off -- music ticks
   only the sleep timer (model/music.c) -- and any button brings it back. */
static void draw_dim(const music_status *m) {
    char line[96], at[16];

    gfx_fill(0, 0, GFX_W, GFX_H, 0x000000u);
    clock_text(m->pos_s, at, sizeof(at));
    snprintf(line, sizeof(line), "%s %s   %s", m->state == MUSIC_PAUSED ? "Paused " : "", m->now.name, at);
    ui_text_fit(12, GFX_H - 16, TEXT_SMALL, 0x30343Cu, line, GFX_W - 24);
}

static void frame(ui_frame *ui, void *st) {
    np_state                 *p       = (np_state *)st;
    static const ui_hint_item LEFT[]  = {{UI_BTN_TRIANGLE, "Dim"}, {UI_BTN_SQUARE, "Stop"}};
    static const ui_hint_item RIGHT[] = {{UI_BTN_CROSS, "Select"}, {UI_BTN_CIRCLE, "Back"}};
    const ui_theme           *t       = ui_theme_now();
    ui_layout                *L       = &ui->layout;
    music_status              m;
    ui_rect                   art_slot;
    ui_art                    art;
    char                      text[64], at[16], of[16];

    p->frame++;
    music_status_get(&m);

    if (p->dim) {
        /* Any button wakes it, and does nothing else. */
        if (ui->pressed || m.state == MUSIC_STOPPED) {
            p->dim = 0;
            screen_frame_took_back();
        } else {
            screen_frame_took_back();
            draw_dim(&m);
            return;
        }
    }

    if (ui->pressed & PAD_L) music_prev();
    if (ui->pressed & PAD_R) music_next();
    if (ui->pressed & PAD_SQUARE) music_stop();
    if ((ui->pressed & PAD_TRIANGLE) && m.state != MUSIC_STOPPED) {
        p->dim = 1;
        return;
    }

    ui_page_busy(m.state == MUSIC_LOADING);
    ui_page_begin(L, UI_PAGE_DETAIL, "Now playing", m.error[0] ? m.error : 0, -1);

    ui_row_at(L, ui_rest(L), ART_GAP);
    ui_col(L, ART_W, 0);
    art_slot = ui_take(L, UI_FILL, ART_H);
    ui_end(L);
    ui_col(L, UI_FILL, COL_GAP);

    art.px      = 0;
    art.waiting = 0;
    if (m.state != MUSIC_STOPPED && m.now.has_image)
        art.px = poster_get(item_art_id(&m.now), ART_W, ART_H, &art.tex_w, &art.tex_h, &art.w, &art.h, &art.waiting);
    if (art.px)
        ui_artwork(art_slot, &art, 0, 0, t->bg_top);
    else if (!art.waiting)
        ui_art_well(art_slot);
    ui_art_dots(art_slot, art.waiting);

    if (m.state == MUSIC_STOPPED) {
        ui_text(L, UI_SUBHEADING, "Nothing playing", 1);
        ui_caption(L, "Open an album and choose a song.");
        ui_end(L);
        ui_end(L);
        ui_page_end(L, 0, 0, RIGHT + 1, 1);
        return;
    }

    ui_text(L, UI_SUBHEADING, m.now.name, 2);
    if (m.now.series[0]) ui_text(L, UI_BODY, m.now.series, 1);
    snprintf(text, sizeof(text), "%d of %d%s", m.index + 1, m.count, m.state == MUSIC_PAUSED ? "   Paused" : "");
    ui_caption(L, text);

    /* Where the song is: a thin track, its fill in the theme's gradient, and
       the times under either end. */
    ui_gap(L, 10);
    {
        ui_rect  bar  = ui_take(L, UI_FILL, BAR_H);
        unsigned dur  = m.dur_s ? m.dur_s : 1;
        int      fill = (int)((unsigned long long)bar.w * (m.pos_s > dur ? dur : m.pos_s) / dur);

        gfx_round_fill(bar.x, bar.y, bar.w, bar.h, BAR_H / 2, UI_TRACK);
        if (fill > BAR_H) gfx_round_hgrad(bar.x, bar.y, fill, bar.h, BAR_H / 2, t->brand_a, t->brand_b);
        gfx_round_fill(bar.x + fill - 1, bar.y - 2, 3, bar.h + 4, 1, UI_FG);

        clock_text(m.pos_s, at, sizeof(at));
        clock_text(m.dur_s, of, sizeof(of));
        ui_text_fit(bar.x, bar.y + 16, TEXT_SMALL, UI_FG_DIM, at, bar.w / 2);
        if (m.dur_s) {
            int w = ui_text_fit_w(TEXT_SMALL, of, bar.w / 2);

            ui_text_fit(bar.x + bar.w - w, bar.y + 16, TEXT_SMALL, UI_FG_DIM, of, bar.w / 2);
        }
    }

    ui_gap(L, 22);
    {
        ui_rect band = ui_take(L, UI_FILL, CTL_H);
        int     cx = band.x + band.w / 2, cy = band.y + band.h / 2;
        int     step = PLAY_D / 2 + CTL_GAP + SKIP_D / 2;

        if (!p->landed) {
            ui_frame_focus_set(ui, ID_PLAY);
            p->landed = 1;
        }
        if (skip_button(ui, ID_PREV, cx - step, cy, -1)) music_prev();
        if (play_button(ui, cx, cy, m.state == MUSIC_PAUSED)) music_toggle_pause();
        if (skip_button(ui, ID_NEXT, cx + step, cy, 1)) music_next();
    }

    ui_end(L);
    ui_end(L);
    ui_page_end(L, LEFT, 2, RIGHT, 2);
}

/* Circle leaves the music playing; a dimmed screen only wakes. */
static int back(void *st) {
    np_state *p = (np_state *)st;

    if (p->dim) {
        p->dim = 0;
        return 1;
    }
    return 0;
}

static void describe(void *st, char *out, unsigned n) {
    const np_state *p = (const np_state *)st;
    music_status    m;

    music_status_get(&m);
    snprintf(out, n, "state: %d\nsong: %s\nindex: %d of %d\nat: %u of %u\ndim: %d\nerror: %s\n", (int)m.state, m.now.name, m.index + 1,
             m.count, m.pos_s, m.dur_s, p ? p->dim : 0, m.error[0] ? m.error : "none");
}

const screen_def nowplaying_page_screen = {"nowplaying", sizeof(np_state), 0, 0, frame, back, 0, describe};
