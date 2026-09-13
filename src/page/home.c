#include "page/home.h"

#include "page/item_text.h"
#include "model/posters.h"
#include "page/open.h"
#include "model/catalog.h"
#include "port/gfx.h"
#include "port/input.h"
#include "view/element.h"
#include "view/page_chrome.h"
#include "page/listing.h"
#include "view/scroll.h"
#include "page/settings.h"
#include "port/text.h"
#include "view/theme.h"

#include <stdio.h>
#include <string.h>

#define LIB_CARD_W   120
#define LIB_CARD_H   68
#define WATCH_CARD_W 80
#define WATCH_CARD_H 45
#define CARD_GAP     8
#define RAIL_GAP     10
#define TITLE_GAP    2
#define HOME_TOP     8
#define VIEW_X       8
#define VIEW_W       (GFX_W - 16)
/* The least space between a rail's heading and the focused card's name. */
#define HEAD_GAP 16
/* The whole panel, not the inset view: scrolled with that, the rail stops
   eight pixels short and its first card never bleeds off the left edge. */
#define RAIL_VIEW_W GFX_W

/* In lib_rail's order. The libraries rail draws no heading of its own. */
static const struct {
    const char *title;
    int         base;
} kRails[LIB_RAIL_N] = {{"Libraries", 0},
                        {"Continue Watching", HOME_RAIL_WATCHING_BASE},
                        {"Next Up", HOME_RAIL_NEXT_BASE},
                        {"Recently Added", HOME_RAIL_RECENT_BASE}};

typedef struct {
    /* Pointers into the catalog, not copies: the worker replaces those arrays
       whole, so a reader between fetches sees one consistent rail. */
    lib_home lib;

    ui_scroll rail_scroll[LIB_RAIL_N], page_scroll;

    /* The width each Continue Watching card came out, for a check. */
    int      watch_cw[LIB_RAIL_MAX];
    int      waiting; /* cards with nothing yet, counted while drawing */
    unsigned frame;   /* the waiting ring's clock */
} home_state;

static home_state *now(void) {
    static home_state none;

    return screen_top_state() ? (home_state *)screen_top_state() : &none;
}

/* Clear of every rail's own range -- see HOME_RAIL_*_BASE. */
#define HOME_RETRY_ID 900

static void forget_art(home_state *h) {
    int r;

    posters_clear();
    for (r = 0; r < LIB_RAIL_N; r++) h->rail_scroll[r].offset = h->rail_scroll[r].target = 0;
    h->page_scroll.offset = h->page_scroll.target = 0;
}

static void enter(void *st, const void *arg, unsigned arg_len) {
    home_state *h = (home_state *)st;

    (void)arg;
    (void)arg_len;
    library_home(&h->lib);
    forget_art(h);
}

/* A mark set on a page above makes the rails stale; the catalog drops them when
   the write lands, so this either does nothing or fetches them again. */
static void resumed(void *st) {
    home_state *h = (home_state *)st;

    library_home(&h->lib);
}

int home_page_library_count(void) { return now()->lib.n[LIB_RAIL_VIEWS]; }
int home_page_watching_count(void) { return now()->lib.n[LIB_RAIL_RESUME]; }
int home_page_next_count(void) { return now()->lib.n[LIB_RAIL_NEXT]; }
int home_page_scroll(void) { return now()->page_scroll.offset; }
int home_page_next_scroll(void) { return now()->rail_scroll[LIB_RAIL_NEXT].offset; }

const char *home_page_library_name(int index) {
    const lib_home *l = &now()->lib;

    return (index >= 0 && index < l->n[LIB_RAIL_VIEWS]) ? l->rail[LIB_RAIL_VIEWS][index].name : "";
}

int home_page_watching_card_w(int index) {
    const home_state *h = now();

    return (index >= 0 && index < h->lib.n[LIB_RAIL_RESUME]) ? h->watch_cw[index] : 0;
}

static int card_w(int art_w, int nominal, int tile) {
    /* A floor at half the nominal size, or a tiny thumbnail is too small to
       aim at. A tile keeps the full width so the libraries rail stays a grid. */
    if (tile || art_w <= 0) return nominal;
    if (art_w > nominal) return nominal;
    return art_w < nominal / 2 ? nominal / 2 : art_w;
}

static void rail_fault(int top, const char *label) {
    (void)text_draw(VIEW_X, top, TEXT_SMALL, UI_FG, label);
    (void)ui_text_fit(VIEW_X, top + text_height(TEXT_SMALL) + TITLE_GAP, TEXT_SMALL, UI_FG_DIM, "Could not be loaded", VIEW_W);
}

static int rail_has_focus(const ui_frame *ui, int id0, int n) {
    int   has;
    ui_id id = ui_frame_focus(ui, &has);

    return has && (int)id >= id0 && (int)id < id0 + n;
}

#define RAIL_PLAIN   0
#define RAIL_CAPTION 1
#define RAIL_BAR     2

/* Returns the index opened by cross, or -1. */
static int rail(ui_frame *ui, home_state *h, ui_scroll *scroll, int y, const char *label, const item *items, int n, int cw, int ch, int id0,
                int decoration, int *cw_out) {
    const ui_theme *t = ui_theme_now();
    int             i, opened = -1, logical_x = 0;
    int             focus_at = -1, focus_x = 0, focus_w = 0;
    int             top = y + (label ? text_height(TEXT_SMALL) + TITLE_GAP : 0);
    int             visible;
    ui_rect         heading;

    ui_scroll_span(scroll, RAIL_VIEW_W, VIEW_X);

    heading = ui_rect_make(VIEW_X, y, VIEW_W, text_height(TEXT_SMALL));
    if (label) text_draw(heading.x, heading.y, TEXT_SMALL, UI_FG, label);

    /* Full panel width: clipped at the inset, a scrolled rail ends in a hard
       line eight pixels from the edge. */
    {
        int y0 = top, y1 = top + UI_CARD_BOX(ch);
        int b0 = ui_page_body().y, b1 = b0 + ui_page_body().h;

        if (y0 < b0) y0 = b0;
        if (y1 > b1) y1 = b1;
        /* Clips the drawing, not the loop: a rail out of sight still registers
           every card, or focus can never move into it -- "down stops working
           at the third rail". */
        visible = (y1 > y0);
        if (visible) gfx_scissor(0, y0, GFX_W, y1 - y0);
    }
    for (i = 0; i < n; i++) {
        ui_art  art;
        int     w, focused, on_panel;
        int     at_x = ui_scroll_place(scroll, logical_x);
        ui_rect box;

        memset(&art, 0, sizeof(art));

        /* Judged on the nominal box, the widest the card can be: the real width
           comes from the picture this decides whether to ask for. Asking
           renews against eviction: reading for all twenty cards once held
           twenty slots of a 24-slot store, so nothing past the twenty-fourth
           card ever got a picture at all. */
        on_panel = visible && at_x + UI_CARD_BOX(cw) > 0 && at_x < GFX_W;
        if (on_panel) {
            art.px = poster_get(items[i].id, cw, ch, &art.tex_w, &art.tex_h, &art.w, &art.h, &art.waiting);
            if (art.waiting) h->waiting++;
        }

        w       = UI_CARD_BOX(card_w(art.px ? art.w : 0, cw, decoration == RAIL_CAPTION));
        box     = ui_rect_make(at_x, top, w, UI_CARD_BOX(ch));
        focused = ui_touch(ui, (ui_id)(id0 + i), box);
        if (focused) {
            focus_at = i;
            focus_x  = logical_x;
            focus_w  = w;
        }

        if (visible)
            ui_art_card(box, &art, focused, t->bg_top, decoration == RAIL_BAR ? item_progress_pct(&items[i]) : 0,
                        decoration == RAIL_BAR ? 100 : 0, decoration == RAIL_CAPTION ? items[i].name : 0);

        if (focused && (ui->pressed & PAD_CROSS)) opened = i;
        if (cw_out) cw_out[i] = w;
        logical_x += w + CARD_GAP;
    }
    if (visible) gfx_scissor_none();

    /* The focused card's title, against the right of the heading. */
    if (label && focus_at >= 0) {
        char       name[ITEM_ROW_TEXT];
        ui_layout *L     = &ui->layout;
        int        avail = VIEW_W - text_width(TEXT_SMALL, label) - HEAD_GAP;
        ui_rect    slot;

        item_row_title(&items[focus_at], 1, name, sizeof(name));
        ui_row_at(L, heading, 0);
        slot = ui_take_end(L, ui_text_fit_w(TEXT_SMALL, name, avail), UI_FILL);
        ui_text_fit(slot.x, slot.y, TEXT_SMALL, UI_FG_DIM, name, avail);
        ui_end(L);
    }

    if (focus_at >= 0) ui_scroll_into_view(scroll, focus_x, focus_x + focus_w, logical_x - CARD_GAP);
    ui_scroll_step(scroll);
    return opened;
}

static void frame(ui_frame *ui, void *st) {
    home_state               *h      = (home_state *)st;
    static const ui_hint_item LEFT[] = {
        {UI_BTN_TRIANGLE, "Reload"},
        {UI_BTN_START, "Settings"},
    };
    static const ui_hint_item RIGHT[] = {
        {UI_BTN_CROSS, "Open"},
    };
    ui_rect body = ui_page_body();
    int     r, focus_top = -1, focus_bot = 0;
    /* From the top of the content, never the panel: derived from a drawn y
       that already held the offset, the page scrolled away from its cursor. */
    int at = 0;
    /* Hardcoded at 14 it put every rail below the libraries two pixels high. */
    int label_h = text_height(TEXT_SMALL) + TITLE_GAP;

    h->waiting = 0;
    h->frame++;
    /* Idempotent: a want matching what is loaded or in flight does nothing. */
    library_home(&h->lib);

    /* Pictures are kept: clearing them threw the viewer back to a blank page
       while every poster was re-fetched over the radio. */
    if ((ui->pressed & PAD_TRIANGLE) && h->lib.state != LIB_BUSY) {
        library_forget_home();
        library_home(&h->lib);
    }

    ui_page_busy(h->lib.state == LIB_BUSY);
    ui_page_begin(&ui->layout, UI_PAGE_HOME, jf_server_name()[0] ? jf_server_name() : "Pocketfin", 0, -1);

    if (h->lib.n[LIB_RAIL_VIEWS] + h->lib.n[LIB_RAIL_RESUME] + h->lib.n[LIB_RAIL_NEXT] + h->lib.n[LIB_RAIL_LATEST] == 0) {
        /* Failed is not loading: a server that answered an error once left this
           page spinning forever with no way out but the power switch. */
        if (h->lib.state == LIB_BUSY) {
            ui_waiting(&ui->layout, "Loading your library", h->frame);
        } else if (h->lib.state == LIB_FAILED) {
            /* The sentence alone: under a "Nothing here" heading, a network
               error reads as an empty library. */
            if (ui_notice(ui, HOME_RETRY_ID, 0, h->lib.error[0] ? h->lib.error : "The server did not answer.", "Try again"))
                library_forget_home();
        } else {
            if (ui_notice(ui, HOME_RETRY_ID, "Nothing here", "This server has nothing to watch yet.", "Try again")) library_forget_home();
        }
        ui_page_end(&ui->layout, LEFT, 2, 0, 0);
        return;
    }

    /* HOME_TOP below the last rail too, so at full scroll the selected card
       rests clear of the hint bar. */
    ui_scroll_span(&h->page_scroll, body.h, HOME_TOP);

    for (r = 0; r < LIB_RAIL_N; r++) {
        const item *rows  = h->lib.rail[r];
        int         count = h->lib.n[r], top, tall, opened;

        /* An empty rail is not drawn at all; a failed one says so under its own
           heading. */
        if (r == LIB_RAIL_VIEWS)
            tall = UI_CARD_BOX(LIB_CARD_H) + RAIL_GAP;
        else if (count > 0)
            tall = label_h + UI_CARD_BOX(WATCH_CARD_H) + (r == LIB_RAIL_LATEST ? 0 : RAIL_GAP);
        else if (h->lib.rail_state[r] == LIB_FAILED)
            tall = label_h + RAIL_GAP;
        else
            continue;

        top = body.y + ui_scroll_place(&h->page_scroll, at);
        if (r != LIB_RAIL_VIEWS && count == 0) {
            rail_fault(top, kRails[r].title);
        } else {
            if (r == LIB_RAIL_VIEWS)
                opened = rail(ui, h, &h->rail_scroll[r], top, 0, rows, count, LIB_CARD_W, LIB_CARD_H, 0, RAIL_CAPTION, 0);
            else
                opened = rail(ui, h, &h->rail_scroll[r], top, kRails[r].title, rows, count, WATCH_CARD_W, WATCH_CARD_H, kRails[r].base,
                              RAIL_BAR, r == LIB_RAIL_RESUME ? h->watch_cw : 0);
            if (rail_has_focus(ui, kRails[r].base, count)) {
                focus_top = at;
                focus_bot = at + tall;
            }
            if (opened >= 0) {
                if (r == LIB_RAIL_VIEWS)
                    listing_page_show(rows[opened].id, rows[opened].name);
                else
                    page_open_item(&rows[opened]);
                return;
            }
        }
        at += tall;
    }

    if (focus_top >= 0) ui_scroll_into_view(&h->page_scroll, focus_top, focus_bot, at);
    ui_scroll_step(&h->page_scroll);

    if (ui->pressed & PAD_START) screen_push(&settings_page_screen);

    ui_page_end(&ui->layout, LEFT, 2, RIGHT, 1);
}

static void describe(void *st, char *out, unsigned n) {
    home_state *h = (home_state *)st;
    ui_id       focus;
    int         has = 0, r, i, rows = 0, flat = 0, at = -1, at_rail = -1;
    unsigned    len;

    library_home(&h->lib);
    focus = ui_frame_focus(screen_ui(), &has);

    for (r = 0; r < LIB_RAIL_N; r++) rows += h->lib.n[r];

    /* Before the listing, which runs out of buffer on a full front page: a
       missing ">" cannot tell "nothing is focused" from "did not fit". */
    if (has)
        for (r = 0; r < LIB_RAIL_N; r++) {
            int col = (int)focus - kRails[r].base;

            if (col >= 0 && col < h->lib.n[r]) {
                at      = col;
                at_rail = r;
                break;
            }
            flat += h->lib.n[r];
        }

    len = (unsigned)snprintf(out, n,
                             "title: Pocketfin\nrails: %d\nrows: %d\n"
                             "busy: %d\nart: %d waiting\n",
                             LIB_RAIL_N, rows, h->lib.state == LIB_BUSY, h->waiting);
    if (len >= n) return;

    if (at_rail >= 0)
        len += (unsigned)snprintf(out + len, n - len, "cursor: #%d %s[%d] %s\n", flat + at, kRails[at_rail].title, at,
                                  h->lib.rail[at_rail][at].name);
    else
        len += (unsigned)snprintf(out + len, n - len, "cursor: none\n");

    flat = 0;
    for (r = 0; r < LIB_RAIL_N && len < n; r++) {
        len += (unsigned)snprintf(out + len, n - len, "rail %s: %d\n", kRails[r].title, h->lib.n[r]);
        for (i = 0; i < h->lib.n[r] && len < n; i++)
            len += (unsigned)snprintf(out + len, n - len, "%s #%d %s\n", (r == at_rail && i == at) ? ">" : " ", flat + i,
                                      h->lib.rail[r][i].name);
        flat += h->lib.n[r];
    }
}

const screen_def home_page_screen = {"home", sizeof(home_state), 0, enter, frame, 0, resumed, describe};
