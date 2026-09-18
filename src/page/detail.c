/* See detail.h. */

#include "page/detail.h"

#include "page/item_text.h"
#include "model/posters.h"
#include "view/element.h"
#include "port/gfx.h"
#include "port/input.h"
#include "model/catalog.h"
#include "base/log.h"
#include "page/player.h"
#include "view/page_chrome.h"
#include "view/theme.h"

#include <stdio.h>
#include <string.h>

#define ID_MARK  0
#define ID_FAV   1
#define ID_PLAY  DETAIL_ID_PLAY
#define ID_START 3
#define ID_AUDIO 4
#define ID_SUB   5
#define ID_PICK  16 /* the list's rows, onward */

enum { PICK_NONE = 0, PICK_AUDIO, PICK_SUB };

/* The poster is fitted into this, not filled: the largest 2:3 and the largest
   16:9 the space holds. */
#define ART_W     200
#define ART_H     212
#define ART_GAP   8
#define COL_GAP   6
#define BLOCK_GAP 4
/* A fixed height, so nothing under the synopsis moves when it lands. */
#define SYNOPSIS_LINES 4

/* A stream that ends the moment it starts would otherwise walk a whole season
   in seconds, marking every episode watched. */
#define MAX_IN_A_ROW 32

typedef struct {
    item it;

    /* The server owns the marks, so the screen changes when the write lands,
       not when the button is pressed. */
    int writing, write_which, write_on;

    int land; /* 1 + the id the cursor lands on next frame; 0 for none */
    int played_from; /* -1 when no run has been started from this page */

    /* -1, or where a run starts once this item's tracks are in: an index
       resolved without them plays the wrong track, or none. */
    long long start;
    int       picking;
    ui_scroll pick_scroll;
    int       in_a_row;

    /* Taken when the run starts. Asked again when the player returns, the one
       shared request is often in flight and the step is dropped ("next is NOT
       FOUND"). */
    item neighbour[2]; /* [0] before, [1] after */
    int  has[2];
} detail_state;

SCREEN_ARG_FITS(item);

static detail_state *now(void) {
    static detail_state none;

    return screen_top_state() ? (detail_state *)screen_top_state() : &none;
}

static void enter(void *st, const void *arg, unsigned arg_len) {
    detail_state *d = (detail_state *)st;

    (void)arg_len;
    if (arg) d->it = *(const item *)arg;
    d->played_from = -1;
    d->start       = -1;
    d->land        = 1 + ID_PLAY;
}

/* A choice is kept by name and language, not index: an index means nothing in
   the next episode's file. An empty name is the server's audio, or no
   subtitles. Per series, a film being its own; for this run only. */
typedef struct {
    char     key[ITEM_ID_LEN];
    jf_track audio, sub;
} track_want;

#define WANT_MAX 8

static track_want g_want[WANT_MAX];
static unsigned   g_want_next;

static track_want *wants(const item *it, int make) {
    const char *key = it->series_id[0] ? it->series_id : it->id;
    track_want *w;
    int         i;

    for (i = 0; i < WANT_MAX; i++)
        if (!strcmp(g_want[i].key, key)) return &g_want[i];
    if (!make) return 0;
    w = &g_want[g_want_next++ % WANT_MAX];
    memset(w, 0, sizeof(*w));
    snprintf(w->key, sizeof(w->key), "%s", key);
    return w;
}

static int audio_index(const item *it, const jf_tracks *t) {
    const track_want *w = wants(it, 0);

    return (w && t) ? jf_track_find(t->audio, t->audio_n, &w->audio) : -1;
}

static int sub_index(const item *it, const jf_tracks *t) {
    const track_want *w = wants(it, 0);

    return (w && t) ? jf_track_find(t->sub, t->sub_n, &w->sub) : -1;
}

static void play_this(detail_state *d, unsigned long long from, const jf_tracks *t) {
    const item *prev = library_adjacent(d->it.id, 0);
    const item *next = library_adjacent(d->it.id, 1);

    d->has[0] = prev != 0;
    d->has[1] = next != 0;
    if (prev) d->neighbour[0] = *prev;
    if (next) d->neighbour[1] = *next;

    d->played_from = 1;
    player_page_show(&d->it, from, prev != 0, next != 0, audio_index(&d->it, t), sub_index(&d->it, t));
}

static void resumed(void *st) {
    detail_state *d = (detail_state *)st;
    player_result r = (player_result)player_page_result();

    /* Getting "was it watched" wrong damages the viewer's library. A failed run
       keeps its old resume point: a broken stream has no trustworthy position. */
    if (d->played_from >= 0) {
        if (player_result_is_watched(r)) {
            d->it.played       = 1;
            d->it.resume_ticks = 0;
        } else if (r != PLAYER_FAILED) {
            d->it.resume_ticks = player_page_end_ticks();
        }
        d->played_from = -1;

        /* Not after a failure, or one bad segment restarts every listing. */
        if (r != PLAYER_FAILED) library_forget_home();

        /* Prev and Next are the viewer asking, and are not capped. */
        {
            const item *step = 0;
            int         dir  = -1;

            if (r == PLAYER_NEXT)
                dir = 1;
            else if (r == PLAYER_PREV)
                dir = 0;
            else if (player_result_may_advance(r) && d->in_a_row < MAX_IN_A_ROW)
                dir = 1;

            if (dir >= 0) {
                step = library_adjacent(d->it.id, dir);
                if (!step && d->has[dir]) step = &d->neighbour[dir];
                if (step && r != PLAYER_NEXT && r != PLAYER_PREV) d->in_a_row++;
            }
            log_printf("detail: back from the player -- %s, next is %s", player_result_text(r), step ? step->name : "NOT FOUND");
            if (step) {
                lib_item asked;

                d->it = *step;
                library_forget_item();
                library_item(&d->it, &asked);
                d->start = 0;
                return;
            }
            if (d->in_a_row >= MAX_IN_A_ROW) log_printf("detail: stopped after %d in a row", d->in_a_row);
            d->in_a_row = 0;
        }
    }
}

static void ask_mark(detail_state *d, lib_mark which, int on) {
    if (d->writing) return;
    d->write_which = (int)which;
    d->write_on    = on;
    d->writing     = library_mark(d->it.id, which, on);
}

static void take_mark(detail_state *d) {
    lib_state st = library_mark_state();

    if (!d->writing || st == LIB_BUSY) return;
    d->writing = 0;
    if (st != LIB_READY) return;

    if (d->write_which == LIB_MARK_PLAYED) {
        d->it.played = d->write_on;
        /* Marked or unmarked, the server drops the resume point. */
        d->it.resume_ticks = 0;
    } else {
        d->it.favorite = d->write_on;
    }
}

const char *detail_page_mark_label(void) { return now()->it.played ? "Unwatch" : "Watched"; }

static void mins_text(unsigned mins, char *out, unsigned n) {
    if (mins >= 60)
        snprintf(out, n, "%uh %02um", mins / 60, mins % 60);
    else
        snprintf(out, n, "%u min", mins);
}

/* The row of `index` in the list, or -1. */
static int row_of(const jf_track *t, int n, int index) {
    int i;

    for (i = 0; i < n; i++)
        if (t[i].index == index) return i;
    return -1;
}

/* -1 plays what the server marks default, which the list shows as chosen. */
static int audio_row(const detail_state *d, const jf_tracks *t) {
    int index = audio_index(&d->it, t);
    int at    = row_of(t->audio, t->audio_n, index >= 0 ? index : t->audio_default);

    return at >= 0 ? at : 0;
}

/* Row 0 is Off. */
static int sub_row(const detail_state *d, const jf_tracks *t) {
    int index = sub_index(&d->it, t);

    return index < 0 ? 0 : 1 + row_of(t->sub, t->sub_n, index);
}

static void open_pick(ui_frame *ui, detail_state *d, int which, int row) {
    d->picking = which;
    memset(&d->pick_scroll, 0, sizeof(d->pick_scroll));
    ui_frame_focus_set(ui, ID_PICK + (ui_id)row);
    /* The press that opened the list would otherwise choose its first row. */
    ui->pressed &= ~PAD_CROSS;
}

static void close_pick(detail_state *d) {
    d->land    = 1 + (d->picking == PICK_AUDIO ? ID_AUDIO : ID_SUB);
    d->picking = PICK_NONE;
}

static void pick(ui_frame *ui, detail_state *d, const jf_tracks *t) {
    static const char *rows[JF_TRACK_MAX + 1];
    int                n = 0, i, got;

    if (!t) {
        close_pick(d);
        return;
    }
    if (d->picking == PICK_AUDIO) {
        for (i = 0; i < t->audio_n; i++) rows[n++] = t->audio[i].name;
        got = ui_pick_list(ui, &d->pick_scroll, "Audio", rows, n, audio_row(d, t), ID_PICK);
        if (got >= 0) wants(&d->it, 1)->audio = t->audio[got];
    } else {
        rows[n++] = "Off";
        for (i = 0; i < t->sub_n; i++) rows[n++] = t->sub[i].name;
        got = ui_pick_list(ui, &d->pick_scroll, "Subtitles, burned into the picture", rows, n, sub_row(d, t), ID_PICK);
        if (got == 0) memset(&wants(&d->it, 1)->sub, 0, sizeof(jf_track));
        if (got > 0) wants(&d->it, 1)->sub = t->sub[got - 1];
    }
    if (got >= 0) close_pick(d);
}

static void frame(ui_frame *ui, void *st) {
    detail_state             *d       = (detail_state *)st;
    static const ui_hint_item RIGHT[] = {{UI_BTN_CROSS, "Select"}, {UI_BTN_CIRCLE, "Back"}};
    const ui_theme           *t       = ui_theme_now();
    ui_layout                *L       = &ui->layout;
    ui_rect                   art_slot;
    ui_art                    art;
    lib_item                  li;
    char                      text[32];

    /* take_mark first: it clears d->writing, and a mark of ours in flight
       wins over the server's copy. Otherwise a mark another client cleared
       stays here until a restart. */
    take_mark(d);
    library_item(&d->it, &li);
    if (li.item && !d->writing) d->it = *li.item;

    if (!d->picking) {
        const item *step = (ui->pressed & PAD_L)   ? library_adjacent(d->it.id, 0)
                           : (ui->pressed & PAD_R) ? library_adjacent(d->it.id, 1)
                                                   : 0;

        if (step) {
            d->it = *step;
            library_forget_item();
            library_item(&d->it, &li);
        }
    }

    /* A failed fetch plays the defaults rather than never starting. */
    if (d->start >= 0 && (li.tracks || li.state == LIB_FAILED)) {
        long long from = d->start;

        d->start = -1;
        play_this(d, (unsigned long long)from, li.tracks);
        return;
    }

    ui_page_busy(d->writing || li.state == LIB_BUSY);
    ui_page_begin(L, UI_PAGE_DETAIL, d->it.series[0] ? d->it.series : "Details", 0, -1);

    /* The column states its own spacing, so nothing in it adds a lead. ui_rest,
       not ui_here: the box ignores the cursor, and it put the whole page ten
       pixels high. */
    ui_row_at(L, ui_rest(L), ART_GAP);
    ui_col(L, ART_W, 0);
    art_slot = ui_take(L, UI_FILL, ART_H);
    ui_end(L);
    ui_col(L, UI_FILL, COL_GAP);

    /* Every frame: asking is what renews it against eviction. */
    art.px      = 0;
    art.waiting = 0;
    if (d->it.has_image) art.px = poster_get(d->it.id, ART_W, ART_H, &art.tex_w, &art.tex_h, &art.w, &art.h, &art.waiting);
    /* The well only for artwork that does not exist: at 200x212 around a
       poster still coming it reads as a frame round nothing. */
    if (art.px)
        ui_artwork(art_slot, &art, 0, 0, t->bg_top);
    else if (!art.waiting)
        ui_art_well(art_slot);
    ui_art_dots(art_slot, art.waiting);

    /* The whole label: an episode's own name alone says which episode of
       nothing. */
    {
        char headline[ITEM_ROW_TEXT];

        item_label(&d->it, 1, headline, sizeof(headline));
        ui_text(L, UI_SUBHEADING, headline, 1);
    }

    {
        ui_rect band = ui_take(L, UI_FILL, UI_CHIP_H);

        ui_row_at(L, band, UI_CHIP_GAP);
        if (d->it.resume_ticks && !d->it.played) {
            char since[24];

            mins_text((unsigned)(d->it.resume_ticks / (60ull * ITEM_TICKS_PER_S)), since, sizeof(since));
            snprintf(text, sizeof(text), "Resume %s", since);
            ui_chip(L, text, t->accent, UI_CHIP_HOT);
        }
        if (d->it.run_ticks) {
            mins_text((unsigned)(d->it.run_ticks / (60ull * ITEM_TICKS_PER_S)), text, sizeof(text));
            ui_chip(L, text, UI_FG_DIM, UI_CHIP_BG);
        }
        if (d->it.played) ui_chip(L, "Watched", UI_FG_DIM, UI_CHIP_BG);
        if (d->it.favorite) ui_chip(L, "Favourite", UI_MARK_INK, UI_CHIP_MARK);
        ui_end(L);
    }

    /* Set before anything it names registers; the frame lands it on the way
       past. */
    if (d->land) {
        ui_frame_focus_set(ui, (ui_id)(d->land - 1));
        d->land = 0;
    }

    ui_gap(L, BLOCK_GAP);
    {
        ui_rect   band = ui_take(L, UI_FILL, UI_BUTTON_H);
        long long from = -1; /* -1 when nothing was pressed */

        ui_row_at(L, band, UI_CHIP_GAP);
        if (d->it.resume_ticks > 0 && !d->it.played) {
            if (ui_button(ui, ID_PLAY, "Resume")) from = (long long)d->it.resume_ticks;
            if (ui_button(ui, ID_START, "From start")) from = 0;
        } else if (ui_button(ui, ID_PLAY, "Play")) {
            from = 0;
        }
        ui_end(L);

        if (from >= 0) {
            /* A viewer's own press starts the auto-advance count again. */
            d->in_a_row = 0;
            d->start    = from;
        }
    }

    /* The band is taken before the tracks are known, so nothing below moves
       when they land. */
    {
        const jf_tracks *tr   = li.tracks;
        ui_rect          band = ui_take(L, UI_FILL, UI_BUTTON_QUIET_H);
        int              half = (band.w - ART_GAP) / 2;
        char             label[JF_TRACK_NAME + 16];

        ui_row_at(L, band, ART_GAP);
        if (tr && tr->audio_n > 1) {
            int row = audio_row(d, tr);

            ui_col(L, half, 0);
            snprintf(label, sizeof(label), "Audio: %s", tr->audio[row].name);
            if (ui_button_quiet(ui, ID_AUDIO, label)) open_pick(ui, d, PICK_AUDIO, row);
            ui_end(L);
        }
        if (tr && tr->sub_n > 0) {
            int row = sub_row(d, tr);

            ui_col(L, half, 0);
            snprintf(label, sizeof(label), "Subtitles: %s", row ? tr->sub[row - 1].name : "Off");
            if (ui_button_quiet(ui, ID_SUB, label)) open_pick(ui, d, PICK_SUB, row);
            ui_end(L);
        }
        ui_end(L);
    }

    /* Buttons, not face buttons: unwatching drops the play count and the
       last-played date with no way back. */
    {
        ui_rect band = ui_take(L, UI_FILL, UI_BUTTON_QUIET_H);

        ui_row_at(L, band, ART_GAP);
        if (ui_button_quiet(ui, ID_MARK, detail_page_mark_label())) ask_mark(d, LIB_MARK_PLAYED, !d->it.played);
        if (ui_button_quiet(ui, ID_FAV, d->it.favorite ? "Unfavourite" : "Favourite")) ask_mark(d, LIB_MARK_FAVOURITE, !d->it.favorite);
        ui_end(L);
    }

    {
        /* A listing does not carry Overview. */
        const char *overview = li.overview;

        ui_gap(L, BLOCK_GAP);
        /* The small face: synopsis ink is ten rows tall where a body line is
           twelve. Null is unknown and draws blank, so a fetch neither shows the
           previous film's paragraph nor reads as confirmed empty. */
        ui_text(L, UI_CAPTION, !overview ? "" : overview[0] ? overview : "No synopsis.", SYNOPSIS_LINES);
    }

    ui_end(L); /* the details column */
    ui_end(L); /* the row the poster and it share */
    ui_page_end(L, 0, 0, RIGHT, 2);

    if (d->picking) pick(ui, d, li.tracks);
}

static int back(void *st) {
    detail_state *d = (detail_state *)st;

    if (!d->picking) return 0;
    close_pick(d);
    return 1;
}

/* The marks and the resume point are the fields most likely to be drawn from a
   stale copy. */
static void describe(void *st, char *out, unsigned n) {
    const detail_state *d = (const detail_state *)st;
    char                title[ITEM_ROW_TEXT];
    lib_item            li;

    library_item(&d->it, &li);
    item_label(&d->it, 1, title, sizeof(title));
    snprintf(out, n,
             "title: %s\nid: %.8s\nkind: %d\nplayed: %d\nfavourite: %d\n"
             "resume: %llu of %llu\nsynopsis: %s\nwriting: %d\nitem: %s\n"
             "tracks: %d audio, %d subtitle\naudio: %d\nsubtitle: %d\npicking: %d\n",
             title, d->it.id, (int)d->it.kind, d->it.played, d->it.favorite, (unsigned long long)d->it.resume_ticks,
             (unsigned long long)d->it.run_ticks, li.overview ? "yes" : "no", d->writing, library_state_text(li.state),
             li.tracks ? li.tracks->audio_n : -1, li.tracks ? li.tracks->sub_n : -1, audio_index(&d->it, li.tracks),
             sub_index(&d->it, li.tracks), d->picking);
}

const screen_def detail_page_screen = {"detail", sizeof(detail_state), sizeof(item), enter, frame, back, resumed, describe};

void detail_page_show(const item *it) { screen_push_with(&detail_page_screen, it, (unsigned)sizeof(*it)); }
