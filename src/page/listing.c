/* See listing.h. */

#include "page/listing.h"

#include "jelly/query.h"

#include "page/item_text.h"
#include "model/catalog.h"
#include "model/posters.h"
#include "port/gfx.h"
#include "port/input.h"
#include "view/layout.h"
#include "view/element.h"
#include "page/open.h"
#include "page/settings.h"
#include "view/page_chrome.h"
#include "view/scroll.h"
#include "port/text.h"
#include "view/theme.h"

#include <stdio.h>
#include <string.h>

#define ROW_H 46
/* Shared by every level: the library holds one listing, so a covered level
   asks again. Per level it would be 31 kB a push for rows already stale. */
static item g_items[LISTING_MAX];
static int  g_n;

static int g_opts; /* square is held */

static int g_opened = -1;

static unsigned g_taken; /* the listing generation these rows came from */

static unsigned g_frame;

static char g_status[40];

/* Clear of every row's id, which is its index. */
#define LISTING_RETRY_ID 900

#define SKELETON_ROWS 5

typedef struct {
    listing_arg at;
    item_sort   sort;
    item_filter filter;
    int         desc;
    /* Per push, so a covered level comes back exactly where it was. A shared
       scroll is what made going back scroll. */
    ui_scroll scroll;
} listing_state;

SCREEN_ARG_FITS(listing_arg);

/* Every frame, and idempotent: asked only on enter and uncover, a want the
   worker was too busy to take was never made again. */
static void ask(const listing_state *v, lib_items *li) {
    library_items(v->at.parent[0] ? v->at.parent : 0, v->at.title, v->sort, v->desc, v->filter, li);
}

/* Copied: the worker replaces its buffer whole, and drawing out of it is how a
   row and its thumbnail come apart. The generation, not the rows: a refetch
   after a mark has the same count and first row, and the old marks stayed
   drawn. */
static void take(const listing_state *v, lib_items *li) {
    int i;

    ask(v, li);
    if (!li->rows || li->gen == g_taken) return;
    g_taken = li->gen;
    g_n     = li->n > LISTING_MAX ? LISTING_MAX : li->n;
    for (i = 0; i < g_n; i++) g_items[i] = li->rows[i];
}

/* Cleared: old order under a new sort label is worse than an empty page. */
static void reask(listing_state *v) {
    lib_items li;

    g_n              = 0;
    g_taken          = 0;
    v->scroll.offset = v->scroll.target = 0;
    ask(v, &li);
}

/* The rows stay up: the answer is the same list, and clearing only flashes. */
static void refetch(const listing_state *v) {
    lib_items li;

    g_taken = 0;
    library_forget_items();
    ask(v, &li);
}

static void enter(void *st, const void *arg, unsigned arg_len) {
    listing_state *v = (listing_state *)st;

    (void)arg_len;
    if (arg) v->at = *(const listing_arg *)arg;
    v->sort   = ITEM_SORT_NAME;
    v->filter = ITEM_FILTER_ALL;
    v->desc   = item_sort_default_descending(ITEM_SORT_NAME);
    memset(&v->scroll, 0, sizeof(v->scroll));
    g_opts = 0;
    reask(v);
}

/* Rows are dropped only if the buffer holds someone else's answer: clearing
   unconditionally redrew an empty page and then scrolled into place. */
static void resumed(void *st) {
    lib_items li;

    ask((const listing_state *)st, &li);
    if (!li.rows) g_n = 0;
    g_taken = 0;
}

void listing_page_show(const char *parent_id, const char *title) {
    listing_arg into;

    memset(&into, 0, sizeof(into));
    snprintf(into.parent, sizeof(into.parent), "%s", parent_id ? parent_id : "");
    snprintf(into.title, sizeof(into.title), "%s", title ? title : "");
    screen_push_with(&listing_page_screen, &into, (unsigned)sizeof(into));
}

int                         listing_page_count(void) { return g_n; }
const char                 *listing_page_status(void) { return g_status; }
static const listing_state *now(void) {
    static listing_state none;

    return screen_top_state() ? (const listing_state *)screen_top_state() : &none;
}

void listing_page_items(lib_items *out) { ask(now(), out); }
int  listing_page_scroll(void) { return now()->scroll.offset; }
int  listing_page_sort(void) { return (int)now()->sort; }
int  listing_page_opened(void) { return g_opened; }

const char *listing_page_row_name(int index) { return (index >= 0 && index < g_n) ? g_items[index].name : ""; }

static void frame(ui_frame *ui, void *st) {
    listing_state            *v      = (listing_state *)st;
    static const ui_hint_item LEFT[] = {
        {UI_BTN_SQUARE, "Sort/Filter"},
        {UI_BTN_TRIANGLE, "Reload"},
        {UI_BTN_START, "Settings"},
    };
    static const ui_hint_item RIGHT[] = {
        {UI_BTN_CROSS, "Open"},
        {UI_BTN_CIRCLE, "Back"},
    };
    /* Square held is a modifier: the same rows stay up and the directions mean
       something else. */
    static const ui_hint_item OPTS[] = {
        {UI_BTN_LEFTRIGHT, "Sort"},
        {UI_BTN_UPDOWN, "Filter"},
        {UI_BTN_CROSS, "Direction"},
    };
    int        i, opened = -1, focus_at = -1;
    ui_layout *L = &ui->layout;
    ui_rect    view;
    lib_items  li;
    /* Last frame's: this frame's is only known once its input is applied. */
    static lib_state was = LIB_IDLE;

    g_frame++;
    g_opts = (ui->held & PAD_SQUARE) != 0;
    if (g_opts) {
        /* Their own types, not int: psp-gcc warns on an enum compared against a
           signed int, and the build refuses warnings. ui->pressed, not
           ui_frame_fired(): the repeat spun through all seven sorts and
           re-asked the server for each. */
        unsigned    fired      = ui->pressed & (PAD_LEFT | PAD_RIGHT | PAD_UP | PAD_DOWN);
        item_sort   was_sort   = v->sort;
        item_filter was_filter = v->filter;
        int         was_desc   = v->desc;

        if (fired == PAD_LEFT)
            v->sort = (item_sort)((v->sort + ITEM_SORT_COUNT - 1) % ITEM_SORT_COUNT);
        else if (fired == PAD_RIGHT)
            v->sort = (item_sort)((v->sort + 1) % ITEM_SORT_COUNT);
        else if (fired == PAD_UP)
            v->filter = (item_filter)((v->filter + ITEM_FILTER_COUNT - 1) % ITEM_FILTER_COUNT);
        else if (fired == PAD_DOWN)
            v->filter = (item_filter)((v->filter + 1) % ITEM_FILTER_COUNT);

        if (v->sort != was_sort) v->desc = item_sort_default_descending(v->sort);
        if (ui->pressed & PAD_CROSS) v->desc = !v->desc;

        if (v->sort != was_sort || v->filter != was_filter || v->desc != was_desc) reask(v);

        /* The presses belong to the modifier, circle included: the stack reads
           circle itself, so clearing ui->pressed alone let square-and-circle
           pop the listing. */
        ui_frame_take_fired(ui);
        ui->pressed = 0;
        screen_frame_took_back();
    }

    /* A second Reload while one is on the wire only throws away the first. */
    if ((ui->pressed & PAD_TRIANGLE) && was != LIB_BUSY) refetch(v);
    if (ui->pressed & PAD_START) {
        screen_push(&settings_page_screen);
        return;
    }
    take(v, &li);
    was = li.state;

    /* Never "Loading" or an error: that hid the sort and read "sorted by
       Loading". Busy has its own signal and a failure goes in the body. */
    snprintf(g_status, sizeof(g_status), "%s%s%s", item_sort_label(v->sort), v->filter == ITEM_FILTER_ALL ? "" : "  ",
             v->filter == ITEM_FILTER_ALL ? "" : item_filter_label(v->filter));

    /* Otherwise a listing cut at LISTING_MAX is indistinguishable from a short
       one. */
    if (g_n > 0 && li.total > g_n) {
        unsigned at = (unsigned)strlen(g_status);

        snprintf(g_status + at, sizeof(g_status) - at, "  %d of %d", g_n, li.total);
    }
    ui_page_busy(li.state == LIB_BUSY);
    ui_page_begin(&ui->layout, UI_PAGE_LIST, v->at.title[0] ? v->at.title : "Listing", g_status, v->desc ? UI_ARROW_DOWN : UI_ARROW_UP);

    if (g_n <= 0) {
        if (li.state != LIB_FAILED && li.state != LIB_READY) {
            /* Rows land where the placeholders were, so nothing moves. */
            ui_skeleton(&ui->layout, ROW_H, SKELETON_ROWS, g_frame);
        } else if (li.state == LIB_FAILED) {
            if (ui_notice(ui, LISTING_RETRY_ID, "Not loaded", li.error[0] ? li.error : "The server did not answer.", "Try again"))
                refetch(v);
        } else if (v->filter != ITEM_FILTER_ALL) {
            (void)ui_notice(ui, LISTING_RETRY_ID, "Nothing matches", "Hold square and press up or down to change the filter.", 0);
        } else {
            (void)ui_notice(ui, LISTING_RETRY_ID, "Nothing here", "This folder has nothing in it.", 0);
        }
        ui_page_end(&ui->layout, g_opts ? OPTS : LEFT, 3, 0, 0);
        return;
    }

    /* The padded column; the element bleeds the wash and focus box to the
       panel's edges itself. */
    view = ui_here(L);

    ui_scroll_span(&v->scroll, view.h, 0);

    gfx_scissor(0, view.y, GFX_W, view.h);
    for (i = 0; i < g_n; i++) {
        ui_rect at = ui_rect_make(view.x, view.y + ui_scroll_place(&v->scroll, i * ROW_H), view.w, ROW_H);
        ui_art  art;
        char    title[ITEM_ROW_TEXT], sub[ITEM_ROW_TEXT];
        /* Every row registers, on the panel or not, or the cursor could never
           wrap from the last row to the first. */
        int         on_panel = !(at.y + at.h < view.y || at.y > view.y + view.h);
        const char *id       = g_items[i].id;
        int         aw, ah;

        /* Reading a picture renews it against eviction: reading all 128 rows
           held the whole store and the rows under the cursor never got a slot. */
        memset(&art, 0, sizeof(art));
        item_art_box(&g_items[i], &aw, &ah);
        if (on_panel) art.px = poster_get(id, aw, ah, &art.tex_w, &art.tex_h, &art.w, &art.h, &art.waiting);

        item_row_title(&g_items[i], 0, title, sizeof(title));
        item_row_subtitle(&g_items[i], 0, sub, sizeof(sub));
        if (ui_media_row_at(ui, (ui_id)i, at, on_panel, &art, aw, ah, title, sub,
                            g_items[i].played ? 100 : item_progress_pct(&g_items[i]))) {
            focus_at = i;
            if (ui->pressed & PAD_CROSS) opened = i;
        }
    }
    gfx_scissor_none();
    ui_scrollbar(ui_rect_make(0, view.y, GFX_W, view.h), v->scroll.offset, ui_scroll_content(&v->scroll, g_n * ROW_H), 1);

    if (focus_at >= 0) ui_scroll_into_view(&v->scroll, focus_at * ROW_H, focus_at * ROW_H + ROW_H, g_n * ROW_H);
    ui_scroll_step(&v->scroll);

    if (g_opts)
        ui_page_end(&ui->layout, OPTS, 3, 0, 0);
    else
        ui_page_end(&ui->layout, LEFT, 3, RIGHT, 2);

    if (opened >= 0) {
        g_opened = opened;
        page_open_item(&g_items[opened]);
    }
}

/* The composed strings, because a title built from the wrong branch is right
   in the source and wrong on the panel. */
static void describe(void *st, char *out, unsigned n) {
    const listing_state *v = (const listing_state *)st;
    ui_id                focus;
    int                  has = 0, i;
    unsigned             len;
    lib_items            li;

    ask(v, &li);
    focus = ui_frame_focus(screen_ui(), &has);
    len   = (unsigned)snprintf(out, n, "title: %s\nrows: %d of %d\nsort: %s%s\nfilter: %s\nstate: %s\nopts: %d\n",
                             v->at.title[0] ? v->at.title : "Listing", g_n, li.total, item_sort_label(v->sort), v->desc ? " desc" : "",
                               item_filter_label(v->filter), library_state_text(li.state), g_opts);
    if (len >= n) return;

    if (has && (int)focus < g_n) {
        char title[ITEM_ROW_TEXT], sub[ITEM_ROW_TEXT];

        item_row_title(&g_items[focus], 0, title, sizeof(title));
        item_row_subtitle(&g_items[focus], 0, sub, sizeof(sub));
        len += (unsigned)snprintf(out + len, n - len, "cursor: #%u %s | %s\n", (unsigned)focus, title, sub);
    } else {
        len += (unsigned)snprintf(out + len, n - len, "cursor: none\n");
    }

    for (i = 0; i < g_n && len < n; i++) {
        char title[ITEM_ROW_TEXT], sub[ITEM_ROW_TEXT];

        item_row_title(&g_items[i], 0, title, sizeof(title));
        item_row_subtitle(&g_items[i], 0, sub, sizeof(sub));
        /* The id, so two builds that ordered a listing differently match rows
           by item rather than by place. */
        len += (unsigned)snprintf(out + len, n - len, "%s #%d %s | %s [%.8s]\n", (has && (int)focus == i) ? ">" : " ", i, title, sub,
                                  g_items[i].id);
    }
}

const screen_def listing_page_screen = {"listing", sizeof(listing_state), sizeof(listing_arg), enter, frame, 0, resumed, describe};
