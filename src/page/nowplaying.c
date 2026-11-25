/* See nowplaying.h. */

#include "page/nowplaying.h"

#include "model/music.h"
#include "model/posters.h"
#include "page/item_text.h"
#include "port/gfx.h"
#include "port/input.h"
#include "view/element.h"
#include "view/page_chrome.h"
#include "view/theme.h"

#include <stdio.h>

#define ART_W   200
#define ART_H   200
#define ART_GAP 8
#define COL_GAP 6
#define BAR_H   6

#define ID_PREV 0
#define ID_PLAY 1
#define ID_NEXT 2
#define ID_STOP 3

typedef struct {
    unsigned frame;
} np_state;

static void clock_text(unsigned s, char *out, unsigned n) { snprintf(out, n, "%u:%02u", s / 60u, s % 60u); }

static void frame(ui_frame *ui, void *st) {
    np_state                 *p       = (np_state *)st;
    static const ui_hint_item RIGHT[] = {{UI_BTN_CROSS, "Select"}, {UI_BTN_CIRCLE, "Back"}};
    const ui_theme           *t       = ui_theme_now();
    ui_layout                *L       = &ui->layout;
    music_status              m;
    ui_rect                   art_slot;
    ui_art                    art;
    char                      text[64], at[16], of[16];

    p->frame++;
    music_status_get(&m);

    /* L and R skip from anywhere on the page. */
    if (ui->pressed & PAD_L) music_prev();
    if (ui->pressed & PAD_R) music_next();

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
    snprintf(text, sizeof(text), "%s%d of %d", m.state == MUSIC_PAUSED ? "Paused   " : "", m.index + 1, m.count);
    ui_caption(L, text);

    /* Where the song is: a track and its fill, in the theme's gradient. */
    ui_gap(L, 6);
    {
        ui_rect  bar  = ui_take(L, UI_FILL, BAR_H);
        unsigned dur  = m.dur_s ? m.dur_s : 1;
        int      fill = (int)((unsigned long long)bar.w * (m.pos_s > dur ? dur : m.pos_s) / dur);

        gfx_round_fill(bar.x, bar.y, bar.w, bar.h, BAR_H / 2, t->panel_bot);
        if (fill > BAR_H) gfx_round_hgrad(bar.x, bar.y, fill, bar.h, BAR_H / 2, t->brand_a, t->brand_b);
    }
    clock_text(m.pos_s, at, sizeof(at));
    clock_text(m.dur_s, of, sizeof(of));
    snprintf(text, sizeof(text), "%s / %s", at, m.dur_s ? of : "--:--");
    ui_caption(L, text);

    ui_gap(L, 8);
    {
        ui_rect band = ui_take(L, UI_FILL, UI_BUTTON_H);

        ui_row_at(L, band, 6);
        if (ui_button(ui, ID_PREV, "Prev")) music_prev();
        if (ui_button(ui, ID_PLAY, m.state == MUSIC_PAUSED ? "Play" : "Pause")) music_toggle_pause();
        if (ui_button(ui, ID_NEXT, "Next")) music_next();
        if (ui_button(ui, ID_STOP, "Stop")) music_stop();
        ui_end(L);
    }

    ui_end(L);
    ui_end(L);
    ui_page_end(L, 0, 0, RIGHT, 2);
}

static void describe(void *st, char *out, unsigned n) {
    music_status m;

    (void)st;
    music_status_get(&m);
    snprintf(out, n, "state: %d\nsong: %s\nindex: %d of %d\nat: %u of %u\nerror: %s\n", (int)m.state, m.now.name, m.index + 1, m.count,
             m.pos_s, m.dur_s, m.error[0] ? m.error : "none");
}

const screen_def nowplaying_page_screen = {"nowplaying", sizeof(np_state), 0, 0, frame, 0, 0, describe};
