/* See view/element.h. An element arrives with its check, or it does not
 * arrive. */

#include "view/element.h"

#include "base/align.h"
#include "port/gfx.h"
#include "port/input.h"
#include "view/layout.h"
#include "page/listing.h"
#include "view/page_chrome.h"
#include "view/style.h"
#include "port/platform.h"
#include "tools/selftest.h"
#include "tools/shot.h"
#include "port/text.h"
#include "view/theme.h"

#include "../page/test_page.h"

#include <stdio.h>
#include <string.h>

/* The console dithers: the title bar's gradient spreads each step across a
   row on the engine but not on the desktop's software fill, so a plain `!=`
   against the row's left edge once counted dither as ink and found 294
   pixels of spinner on a page that had none. */
static int is_ink(unsigned short px, unsigned short ground) {
    int dr = (int)((px >> 11) & 31u) - (int)((ground >> 11) & 31u);
    int dg = (int)((px >> 5) & 63u) - (int)((ground >> 5) & 63u);
    int db = (int)(px & 31u) - (int)(ground & 31u);

    if (dr < 0) dr = -dr;
    if (dg < 0) dg = -dg;
    if (db < 0) db = -db;
    return dr > 2 || dg > 4 || db > 2;
}

/* The CAP height centred, not the line box: a band centred on the line box
   leaves anything without an ascender sitting visibly low. */
static int t_a_line_is_centred_on_its_caps(char *note, unsigned n) {
    const text_face *f;
    int              y;

    if (page_panel(note, n) != 0) return -1;

    f = &text_faces[TEXT_BODY];
    y = ui_text_y(TEXT_BODY, 0, 40);

    /* The odd pixel a band and a cap of different parities cannot split goes
       below: a line a shade high is what the eye forgives. */
    {
        int above = y + f->cap_top;
        int below = 40 - (above + f->cap_h);

        if (below - above < 0 || below - above > 1) {
            snprintf(note, n, "a %d-tall cap in a 40 band left %d above and %d below", f->cap_h, above, below);
            return 1;
        }
        snprintf(note, n, "cap %d, %d above and %d below", f->cap_h, above, below);
    }
    return 0;
}

/* Cut with an ellipsis, and the ellipsis counted: a fit that spends the whole
   budget on characters draws the dots past the edge it was given. */
static int t_a_line_too_long_is_cut_inside_its_width(char *note, unsigned n) {
    const char *s = "A line far wider than the space it is being given to draw in";
    int         full, cut, drawn;

    if (page_panel(note, n) != 0) return -1;

    full = text_width(TEXT_SMALL, s);
    if (full <= 120) {
        snprintf(note, n, "the sample is %d wide -- it does not overflow 120", full);
        return -1;
    }

    cut = ui_text_fit_w(TEXT_SMALL, s, 120);
    if (cut != 120) {
        snprintf(note, n, "a %d-wide line capped at 120 measured %d", full, cut);
        return 1;
    }

    gfx_frame_begin();
    drawn = ui_text_fit(0, 20, TEXT_SMALL, UI_FG, s, 120) - 0;
    gfx_frame_end();

    if (drawn > 120) {
        snprintf(note, n, "the pen finished at %d, past the 120 it was given", drawn);
        return 1;
    }
    /* A fit that drew nothing would also pass above. */
    if (drawn < 60) {
        snprintf(note, n, "the pen finished at %d -- almost nothing was drawn", drawn);
        return 1;
    }
    snprintf(note, n, "%d wide cut to %d, pen at %d", full, cut, drawn);
    return 0;
}

/* Reads the panel, not the return value: a cut line once drew the glyph that
   didn't fit and started the ellipsis on top of it, so the pen came back
   inside the width while the panel still had ink past it. */
static int t_a_cut_line_leaves_no_ink_past_its_width(char *note, unsigned n) {
    const char    *s = "A line far wider than the space it is being given to draw in";
    unsigned short row[GFX_W];
    int            maxw = 120, y, x, right = -1, dots = 0, gap = 0;

    if (page_panel(note, n) != 0) return -1;

    gfx_frame_begin();
    gfx_fill(0, 0, GFX_W, 40, 0x000000u);
    (void)ui_text_fit(0, 8, TEXT_SMALL, UI_FG, s, maxw);
    gfx_frame_end();

    for (y = 0; y < 40; y++) {
        if (gfx_capture_row(y, row) != 0) {
            snprintf(note, n, "the panel could not be read");
            return -1;
        }
        for (x = GFX_W - 1; x > right; x--)
            if (row[x] != 0) {
                right = x;
                break;
            }
    }

    if (right < 0) {
        snprintf(note, n, "nothing was drawn at all");
        return 1;
    }
    if (right >= maxw) {
        snprintf(note, n, "ink reaches x=%d, past the %d it was given", right, maxw);
        return 1;
    }

    /* Three spaced dots: one full stop would pass every test above. */
    if (gfx_capture_row(8 + text_height(TEXT_SMALL) - 4, row) == 0) {
        for (x = 0; x <= right; x++) {
            if (row[x] != 0) {
                if (gap) dots++;
                gap = 0;
            } else {
                gap = 1;
            }
        }
    }
    snprintf(note, n, "ink stops at %d of %d, %d marks on the baseline", right, maxw, dots);
    return 0;
}

static int t_a_line_that_fits_is_left_alone(char *note, unsigned n) {
    const char *s = "Lab";
    int         want, got;

    if (page_panel(note, n) != 0) return -1;

    want = text_width(TEXT_TITLE, s);
    got  = ui_text_fit_w(TEXT_TITLE, s, 400);
    if (got != want) {
        snprintf(note, n, "\"%s\" is %d wide and measured %d inside 400", s, want, got);
        return 1;
    }
    return 0;
}

/* A style table whose roles all resolved the same would draw a plausible page
   and mean nothing. */
static int t_the_roles_differ(char *note, unsigned n) {
    const ui_role_style *h = ui_style_of(UI_HEADING);
    const ui_role_style *b = ui_style_of(UI_BODY);
    const ui_role_style *c = ui_style_of(UI_CAPTION);

    if (h->face == b->face || b->face == c->face) {
        snprintf(note, n, "faces are %d/%d/%d -- two roles share one", h->face, b->face, c->face);
        return 1;
    }
    if (ui_role_ink(UI_HEADING) == ui_role_ink(UI_BODY) || ui_role_ink(UI_BODY) == ui_role_ink(UI_CAPTION)) {
        snprintf(note, n, "inks are %06X/%06X/%06X -- two roles share one", ui_role_ink(UI_HEADING), ui_role_ink(UI_BODY),
                 ui_role_ink(UI_CAPTION));
        return 1;
    }
    /* A band has to hold its own face, or the text is clipped by its slot. */
    if (h->band < text_height(h->face) || b->band < text_height(b->face) || c->band < text_height(c->face)) {
        snprintf(note, n, "a band is shorter than the face it holds");
        return 1;
    }
    snprintf(note, n, "heading/body/caption differ in face and ink");
    return 0;
}

/* Rather than reading off the end of the table -- the same refusal
   ui_theme_set makes. */
static int t_an_unknown_role_falls_back(char *note, unsigned n) {
    if (ui_style_of((ui_role)99) != ui_style_of(UI_BODY)) {
        snprintf(note, n, "role 99 did not fall back to the body style");
        return 1;
    }
    if (ui_style_of((ui_role)-1) != ui_style_of(UI_BODY)) {
        snprintf(note, n, "role -1 did not fall back to the body style");
        return 1;
    }
    return 0;
}

/* The point of the role: a page passes no height, and the lines it stacks do
   not overlap or leave a gap the style did not ask for. */
static int t_stacked_roles_land_where_the_style_says(char *note, unsigned n) {
    ui_layout            L;
    const ui_role_style *body = ui_style_of(UI_BODY);
    int                  after_one, after_two;

    if (page_panel(note, n) != 0) return -1;

    gfx_frame_begin();
    ui_layout_begin(&L, ui_rect_make(0, 0, 480, 272));
    ui_body(&L, "one");
    after_one = ui_take(&L, UI_FILL, 1).y;

    ui_layout_begin(&L, ui_rect_make(0, 0, 480, 272));
    ui_body(&L, "one");
    ui_body(&L, "two");
    after_two = ui_take(&L, UI_FILL, 1).y;
    gfx_frame_end();

    /* The FIRST line does not lead: the space above it is the page's margin,
       and counting both leaves a heading sitting low on page one and right on
       every other. */
    if (after_one != body->band) {
        snprintf(note, n, "one line took %d, wanted its %d band with no lead", after_one, body->band);
        return 1;
    }
    if (after_two != 2 * body->band + body->lead) {
        snprintf(note, n, "two lines took %d, wanted %d (%d + %d lead + %d)", after_two, 2 * body->band + body->lead, body->band,
                 body->lead, body->band);
        return 1;
    }
    snprintf(note, n, "band %d, lead %d, two lines take %d", body->band, body->lead, after_two);
    return 0;
}

/* A sentence that didn't fit once lost everything past the first line, which
   reads as a shorter sentence rather than as a fault. */
static int t_a_long_line_wraps_rather_than_being_cut(char *note, unsigned n) {
    const char          *s  = "The server did not answer. It may be off, or this console may have joined a network that cannot reach it.";
    const ui_role_style *st = ui_style_of(UI_BODY);
    ui_layout            L;
    int                  lines, took;

    if (page_panel(note, n) != 0) return -1;

    lines = ui_lines_for(UI_BODY, s, 300);
    if (lines < 2) {
        snprintf(note, n, "the sample wrapped to %d lines at 300 px -- it does not overflow", lines);
        return -1;
    }

    gfx_frame_begin();
    ui_layout_begin(&L, ui_rect_make(0, 0, 300, 272));
    ui_body(&L, s);
    took = ui_used(&L);
    gfx_frame_end();

    if (took != lines * st->band) {
        snprintf(note, n, "%d wrapped lines took %d px, wanted %d (%d x %d)", lines, took, lines * st->band, lines, st->band);
        return 1;
    }
    snprintf(note, n, "wrapped to %d lines, %d px", lines, took);
    return 0;
}

/* Out of box rather than out of words is when the ellipsis appears, or a
   paragraph stops mid sentence with no sign anything is missing. */
static int t_a_line_with_no_room_left_is_cut(char *note, unsigned n) {
    const char *s = "A sentence with rather more words in it than will ever fit into one short line of a narrow box";
    ui_layout   L;
    int         took, box_h = ui_style_of(UI_BODY)->band;

    if (page_panel(note, n) != 0) return -1;

    gfx_frame_begin();
    ui_layout_begin(&L, ui_rect_make(0, 0, 200, box_h));
    ui_body(&L, s);
    took = ui_used(&L);
    gfx_frame_end();

    if (took > box_h) {
        snprintf(note, n, "a %d px box took %d px -- the wrap ran past its box", box_h, took);
        return 1;
    }
    snprintf(note, n, "a %d px box took %d and cut the rest", box_h, took);
    return 0;
}

/* Centring needs the height before drawing -- an immediate-mode page has no
   second pass to find it in. */
static int t_a_notice_centres_its_block(char *note, unsigned n) {
    static const char *HEAD = "Stopped";
    static const char *BODY = "The server did not answer, and this sentence is long enough to wrap onto a second line.";
    ui_frame           ui;
    int                block, drawn, room, above, below;

    if (page_panel(note, n) != 0) return -1;

    gfx_frame_begin();
    ui_frame_begin(&ui, ui_rect_make(0, 0, GFX_W, GFX_H), 0, 0, 0);
    ui_page_begin(&ui.layout, UI_PAGE_TEXT, "Lab", 0, -1);

    /* The whole box: measured against what is left, the page's top margin
       sits above the block with nothing under it to match, and the check
       agrees with the code while both disagree with the eye. */
    room  = ui_here(&ui.layout).h;
    block = ui_notice_h(&ui.layout, HEAD, BODY, "Try again");
    (void)ui_notice(&ui, 0, HEAD, BODY, "Try again");
    drawn = ui_used(&ui.layout);
    ui_frame_end(&ui);
    gfx_frame_end();

    if (block <= 0 || block >= room) {
        snprintf(note, n, "the block measured %d px against %d px of room -- nothing to centre", block, room);
        return -1;
    }

    above = drawn - block;
    below = room - drawn;

    if (above < 0 || below < 0) {
        snprintf(note, n, "block %d px in a %d px box left %d above and %d below", block, room, above, below);
        return 1;
    }
    if (above - below < -1 || above - below > 1) {
        snprintf(note, n, "a %d px block in a %d px box left %d above and %d below", block, room, above, below);
        return 1;
    }
    snprintf(note, n, "%d px block centred in %d, %d above and %d below", block, room, above, below);
    return 0;
}

/* A row measured short overlaps whatever sits beside it. */
static int t_a_hint_row_is_as_wide_as_its_hints(char *note, unsigned n) {
    static const ui_hint_item TWO[] = {{UI_BTN_CROSS, "Select"}, {UI_BTN_CIRCLE, "Back"}};
    int                       one, two, both;

    if (page_panel(note, n) != 0) return -1;

    one  = ui_hint_w(UI_BTN_CROSS, "Select");
    two  = ui_hint_w(UI_BTN_CIRCLE, "Back");
    both = ui_hint_row_w(TWO, 2);

    if (one <= UI_BTN_SIZE) {
        snprintf(note, n, "a hint measured %d -- no wider than its %d glyph", one, UI_BTN_SIZE);
        return 1;
    }
    if (both < one + two) {
        snprintf(note, n, "two hints of %d and %d measured %d together", one, two, both);
        return 1;
    }
    snprintf(note, n, "%d + %d fit in %d", one, two, both);
    return 0;
}

/* The footer's two groups are laid out from opposite ends, so neither has to
   know the other's width. */
static int t_the_footers_two_groups_do_not_meet(char *note, unsigned n) {
    static const ui_hint_item RIGHT[] = {{UI_BTN_CROSS, "Select"}, {UI_BTN_CIRCLE, "Back"}};
    int                       started;

    if (page_panel(note, n) != 0) return -1;

    gfx_frame_begin();
    started = ui_hint_row_right(GFX_W - 8, 250, 8, RIGHT, 2);
    gfx_frame_end();

    if (started >= GFX_W - 8) {
        snprintf(note, n, "the right group started at %d, at or past the edge it ends on", started);
        return 1;
    }
    if (started != GFX_W - 8 - ui_hint_row_w(RIGHT, 2)) {
        snprintf(note, n, "the right group started at %d, wanted %d -- its own width back from the edge", started,
                 GFX_W - 8 - ui_hint_row_w(RIGHT, 2));
        return 1;
    }
    snprintf(note, n, "right group runs %d..%d", started, GFX_W - 8);
    return 0;
}

/* A page that overlapped a bar would draw under it and look merely cramped. */
static int t_the_body_is_what_the_bars_leave(char *note, unsigned n) {
    const ui_page_style *ps   = ui_style_page(UI_PAGE_TEXT);
    ui_rect              body = ui_page_body();

    if (body.y != ps->title_h) {
        snprintf(note, n, "the body starts at %d, and the title bar is %d tall", body.y, ps->title_h);
        return 1;
    }
    if (body.y + body.h != GFX_H - ps->foot_h) {
        snprintf(note, n, "the body ends at %d, and the footer starts at %d", body.y + body.h, GFX_H - ps->foot_h);
        return 1;
    }
    snprintf(note, n, "body %d..%d between a %d bar and an %d footer", body.y, body.y + body.h, ps->title_h, ps->foot_h);
    return 0;
}

#define WELL     ui_rect_make(20, 20, 60, 40)
#define WELL_MID (20 + 40 / 2)

static int well_row(unsigned phase, unsigned short *row) {
    ui_art art;

    memset(&art, 0, sizeof(art));
    art.waiting = phase;

    /* Twice: the swap latches at the next blank, so one pass reads back the
       other buffer. The same holds for every capture below. */
    gfx_frame_begin();
    ui_artwork(WELL, &art, 0, 1, 0x000000u);
    gfx_frame_end();
    gfx_frame_begin();
    ui_artwork(WELL, &art, 0, 1, 0x000000u);
    gfx_frame_end();

    return gfx_capture_row(WELL_MID, row);
}

/* An empty well and a waiting one must differ, or a viewer cannot tell a slow
   library from a broken one. */
static int t_a_waiting_well_carries_dots(char *note, unsigned n) {
    unsigned short quiet[GFX_W], dotted[GFX_W], later[GFX_W];
    int            x, marks = 0, moved = 0, on = 0;

    if (page_panel(note, n) != 0) return -1;

    /* Real time moves the dots, so a frame count would prove nothing. Just
       over one step. */
    if (well_row(0, quiet) != 0 || well_row(1, dotted) != 0) {
        snprintf(note, n, "the panel could not be read");
        return -1;
    }
    platform_sleep_us(130000u);
    if (well_row(1, later) != 0) {
        snprintf(note, n, "the panel could not be read");
        return -1;
    }

    /* Against the empty well, not a colour: the console dithers and rounds an
       antialiased edge differently from the PC, so both machines agree only
       on what the dots changed. The three span x=42..56; the window keeps
       clear of the well's edge. */
    for (x = 34; x < 66; x++) {
        int ink = (dotted[x] != quiet[x]);

        if (ink && !on) marks++;
        on = ink;
        if (dotted[x] != later[x]) moved++;
    }

    if (marks != 3) {
        snprintf(note, n, "a waiting well drew %d marks on its middle row, wanted 3", marks);
        return 1;
    }
    /* Three marks that never change cannot be told from a finished picture. */
    if (moved == 0) {
        snprintf(note, n, "the dots are identical ten frames apart -- nothing is walking");
        return 1;
    }
    snprintf(note, n, "3 dots, %d pixels of them changed over ten frames", moved);
    return 0;
}

/* A rail's scissor is exactly the card band tall, so a well drawn one pixel
   proud of its slot loses its top and bottom edges to the clip. */
static int t_an_empty_card_stays_inside_its_rectangle(char *note, unsigned n) {
    ui_rect        at = ui_rect_make(40, 40, UI_CARD_BOX(80), UI_CARD_BOX(45));
    unsigned short row[GFX_W];
    ui_art         art;
    int            x, y, outside = 0, first_y = -1;

    if (page_panel(note, n) != 0) return -1;
    memset(&art, 0, sizeof(art));

    for (y = 0; y < 2; y++) {
        gfx_frame_begin();
        gfx_fill(0, 0, GFX_W, GFX_H, 0x000000u);
        ui_art_card(at, &art, 0, 0x000000u, 0, 0, 0);
        gfx_frame_end();
    }

    for (y = at.y - 3; y < at.y + at.h + 3; y++) {
        if (gfx_capture_row(y, row) != 0) {
            snprintf(note, n, "the panel could not be read");
            return -1;
        }
        for (x = at.x - 3; x < at.x + at.w + 3; x++) {
            int in = (y >= at.y && y < at.y + at.h && x >= at.x && x < at.x + at.w);

            if (!in && row[x] != 0) {
                if (first_y < 0) first_y = y;
                outside++;
            }
        }
    }

    if (outside) {
        snprintf(note, n,
                 "%d pixels landed outside the card, first at y=%d -- the well overhangs the rectangle and a rail's scissor cuts it",
                 outside, first_y);
        return 1;
    }

    /* A card that drew nothing would also have nothing outside. */
    if (gfx_capture_row(at.y, row) != 0 || row[at.x + at.w / 2] == 0) {
        snprintf(note, n, "the card's own top edge is blank -- nothing was drawn");
        return 1;
    }
    snprintf(note, n, "%dx%d empty card, nothing outside it", at.w, at.h);
    return 0;
}

/* A ramp that ends at 255 leaves the card's bottom row a black bar over the
   poster that nothing else on the panel would flag.

   Read back as alpha: over a white picture the red channel is the blend, and
   5-6-5 quantises it in steps of 8, so an exact colour would pin the panel's
   depth instead. */
static int scrim_alpha_of(unsigned short px) {
    int red = (int)((px & 0x1Fu) << 3);
    int a   = (255 * (255 - red)) / (255 - 6);

    /* The last 5-bit step lands below the ink's own red, so a solid row
       reconstructs past 255 rather than at it. */
    return a > 255 ? 255 : a;
}

static int t_a_captions_scrim_never_reaches_opaque(char *note, unsigned n) {
    static POCKETFIN_ALIGN16 unsigned short tex[64 * 64];
    unsigned short                          row[GFX_W];
    ui_rect               slot = ui_rect_make(40, 40, UI_CARD_BOX(64), UI_CARD_BOX(64));
    ui_art                art;
    ui_rect               inner;
    int                   i, x, top_a, bot_a;

    if (page_panel(note, n) != 0) return -1;
    if (!gfx_blit_can(tex, 64, 64)) {
        snprintf(note, n, "the sample texture is not one the engine can read");
        return -1;
    }

    for (i = 0; i < 64 * 64; i++) tex[i] = GFX_PACK565(255, 255, 255);
    /* The engine does not snoop the CPU's cache: without this it reads
       whichever rows of the texture happen to have been written back and
       black where they have not -- the picture once came out striped after
       an unrelated change moved the array. */
    gfx_wrote_pixels();

    memset(&art, 0, sizeof(art));
    art.px    = tex;
    art.tex_w = 64;
    art.tex_h = 64;
    art.w     = 64;
    art.h     = 64;

    for (i = 0; i < 2; i++) {
        gfx_frame_begin();
        gfx_fill(0, 0, GFX_W, GFX_H, 0x000000u);
        ui_art_card(slot, &art, 0, 0x000000u, 0, 0, "A");
        gfx_frame_end();
    }

    /* Clear of the caption's own text, which starts five in from the left,
       and clear of the corner the card's radius cuts. */
    inner = ui_rect_inset(slot, UI_CARD_EDGE);
    x     = inner.x + inner.w - 8;

    if (gfx_capture_row(inner.y + inner.h - 13, row) != 0) {
        snprintf(note, n, "the panel could not be read");
        return -1;
    }
    top_a = scrim_alpha_of(row[x]);
    if (gfx_capture_row(inner.y + inner.h - 1, row) != 0) {
        snprintf(note, n, "the panel could not be read");
        return -1;
    }
    bot_a = scrim_alpha_of(row[x]);

    if (bot_a > 240) {
        snprintf(note, n, "the band ran %d..%d of 255 -- at the bottom the poster is gone under it", top_a, bot_a);
        return 1;
    }
    if (top_a < 84 || top_a > 108 || bot_a < 211 || bot_a > 235) {
        snprintf(note, n, "the band ran %d..%d of 255, wanted 96..223 (3/8 to 7/8)", top_a, bot_a);
        return 1;
    }
    snprintf(note, n, "the band ran %d..%d of 255 over a white picture", top_a, bot_a);
    return 0;
}

/* The title gives up room only down to a floor: a status wide enough to sit
   over the title's start is pushed off centre instead. */
static int t_a_wide_status_stops_at_the_titles_floor(char *note, unsigned n) {
    const ui_page_style *ps = ui_style_page(UI_PAGE_TEXT);
    unsigned short       row[GFX_W];
    char                 status[160];
    ui_rect              bar;
    int                  i, y, x, sw, first = -1, title_last = -1, gap_ink = 0;

    if (page_panel(note, n) != 0) return -1;

    /* Wide enough that centring would put it left of the floor, not so wide
       that the right edge pushes it past: then only the floor decides. */
    for (i = 0; i < (int)sizeof(status) - 1; i++) {
        status[i]     = 'W';
        status[i + 1] = 0;
        if (text_width(TEXT_SMALL, status) > 390) break;
    }
    sw = text_width(TEXT_SMALL, status);
    if (sw < 385 || sw > 424) {
        snprintf(note, n, "no status between 385 and 424 wide could be built -- %d", sw);
        return -1;
    }

    bar = ui_rect_make(0, 0, GFX_W, ps->title_h);
    for (i = 0; i < 2; i++) {
        gfx_frame_begin();
        gfx_fill(0, 0, GFX_W, GFX_H, 0x000000u);
        ui_title_bar_at(bar, "A title long enough to be cut short", status, -1, 0, 0);
        gfx_frame_end();
    }

    /* The bar's ground is a vertical gradient, so within a row anything
       differing from the left edge is ink. The last two rows are the brand's
       line. */
    for (y = 2; y < bar.h - 2; y++) {
        if (gfx_capture_row(y, row) != 0) {
            snprintf(note, n, "the panel could not be read");
            return -1;
        }
        for (x = 1; x < GFX_W - 1; x++) {
            if (!is_ink(row[x], row[0])) continue;
            if (x < 42) {
                if (x > title_last) title_last = x;
            } else if (x < 46) {
                gap_ink++;
            } else if (first < 0 || x < first) {
                first = x;
            }
        }
    }

    if (title_last < 0) {
        snprintf(note, n, "the title drew nothing at all");
        return 1;
    }
    if (first < 0) {
        snprintf(note, n, "a %d-wide status drew nothing past x=46", sw);
        return 1;
    }
    /* 8 for the bar's inset plus the 40 the title keeps. */
    if (first < 48 || first > 54) {
        snprintf(note, n, "a %d-wide status started at x=%d, wanted the floor at 48", sw, first);
        return 1;
    }
    if (gap_ink) {
        snprintf(note, n, "%d pixels of ink between the title and a status at x=%d -- they are running into each other", gap_ink, first);
        return 1;
    }
    snprintf(note, n, "a %d-wide status held at x=%d with the title cut by x=%d", sw, first, title_last);
    return 0;
}

/* The body column opens with its margins already on it. */
static int t_a_page_opens_and_closes_one_box(char *note, unsigned n) {
    const ui_page_style *ps = ui_style_page(UI_PAGE_TEXT);
    ui_layout            page;
    ui_rect              first;

    if (page_panel(note, n) != 0) return -1;

    gfx_frame_begin();
    ui_page_begin(&page, UI_PAGE_TEXT, "Lab", 0, -1);
    if (ui_layout_depth(&page) != 1) {
        snprintf(note, n, "the page opened %d boxes, wanted 1", ui_layout_depth(&page));
        gfx_frame_end();
        return 1;
    }
    first = ui_take(&page, UI_FILL, 10);
    ui_page_end(&page, 0, 0, 0, 0);
    gfx_frame_end();

    if (first.x != ps->pad_l) {
        snprintf(note, n, "the first thing on the page sat at x=%d, and the margin is %d", first.x, ps->pad_l);
        return 1;
    }
    if (first.x + first.w != GFX_W - ps->pad_r) {
        snprintf(note, n, "it ended at %d, and the right margin is %d", first.x + first.w, ps->pad_r);
        return 1;
    }
    if (first.y != ui_page_body().y + ps->pad_top) {
        snprintf(note, n, "it sat at y=%d, wanted %d -- the body top plus the page's own space", first.y, ui_page_body().y + ps->pad_top);
        return 1;
    }
    snprintf(note, n, "the body column runs %d..%d from y=%d", first.x, first.x + first.w, first.y);
    return 0;
}

/* Ink where a centred status draws, and in the spinner's box at the right
   end. */
#define STATUS_X0 46
#define SPIN_X0   450

static int bar_ink(int busy, const char *status, int *status_ink, int *spin_ink) {
    const ui_page_style *ps = ui_style_page(UI_PAGE_LIST);
    ui_layout            page;
    unsigned short       row[GFX_W];
    int                  i, x, y;

    *status_ink = *spin_ink = 0;
    for (i = 0; i < 2; i++) {
        gfx_frame_begin();
        ui_page_busy(busy);
        ui_page_begin(&page, UI_PAGE_LIST, "Filme", status, -1);
        ui_page_end(&page, 0, 0, 0, 0);
        gfx_frame_end();
    }
    for (y = 2; y < ps->title_h - 2; y++) {
        if (gfx_capture_row(y, row) != 0) return -1;
        for (x = STATUS_X0; x < SPIN_X0; x++)
            if (is_ink(row[x], row[0])) (*status_ink)++;
        for (x = SPIN_X0; x < GFX_W - 2; x++)
            if (is_ink(row[x], row[0])) (*spin_ink)++;
    }
    return 0;
}

/* A page that overwrote the status to say it was fetching destroyed the one
   thing the viewer needed to read. */
static int t_a_busy_page_keeps_its_status(char *note, unsigned n) {
    int quiet_status, quiet_spin, busy_status, busy_spin;

    if (page_panel(note, n) != 0) return -1;

    if (bar_ink(0, "Name  A-Z", &quiet_status, &quiet_spin) != 0 || bar_ink(1, "Name  A-Z", &busy_status, &busy_spin) != 0) {
        snprintf(note, n, "the panel could not be read");
        return -1;
    }

    if (quiet_status <= 0) {
        snprintf(note, n, "a quiet page drew no status ink at all between x=%d and x=%d", STATUS_X0, SPIN_X0);
        return 1;
    }
    if (quiet_spin != 0) {
        snprintf(note, n, "a page that is not busy drew %d pixels in the spinner's box", quiet_spin);
        return 1;
    }
    if (busy_spin <= 0) {
        snprintf(note, n, "ui_page_busy(1) drew nothing past x=%d -- there is no spinner", SPIN_X0);
        return 1;
    }
    if (busy_status != quiet_status) {
        snprintf(note, n, "busy left %d pixels of status where a quiet page drew %d -- the spinner moved or ate it", busy_status,
                 quiet_status);
        return 1;
    }
    snprintf(note, n, "%d pixels of status either way, and %d more in the spinner's box when busy", busy_status, busy_spin);
    return 0;
}

/* Every row registers, on the panel or not. */
static void listing_rows(ui_frame *ui, int rows, unsigned held, unsigned pressed, unsigned now_us) {
    int i;

    ui_frame_begin(ui, ui_rect_make(0, 0, GFX_W, GFX_H), held, pressed, now_us);
    for (i = 0; i < rows; i++) (void)ui_touch(ui, (ui_id)i, ui_rect_make(0, i * 20, GFX_W, 20));
    ui_frame_end(ui);
}

/* The scroll clips the drawing and not the loop, so a table shorter than
   LISTING_MAX drops rows silently and a press at the end wraps to the top,
   which reads as a scrolling fault. */
static int t_a_frame_holds_every_row_a_listing_registers(char *note, unsigned n) {
    /* Static: UI_FRAME_MAX entries is a few kilobytes on the check thread's
       fixed stack. */
    static ui_frame ui;
    unsigned        now = 16667u;
    int             i, has = 0;

    memset(&ui, 0, sizeof(ui));
    listing_rows(&ui, LISTING_MAX, 0, 0, now);
    if (ui_frame_entries(&ui) != LISTING_MAX) {
        snprintf(note, n, "%d of %d rows registered -- the frame holds %d", ui_frame_entries(&ui), LISTING_MAX, UI_FRAME_MAX);
        return 1;
    }

    ui_frame_focus_set(&ui, (ui_id)(LISTING_MAX - 1));
    if (!ui_frame_focus(&ui, &has) || !has) {
        snprintf(note, n, "row %d was refused the focus", LISTING_MAX - 1);
        return 1;
    }
    if ((int)ui_frame_focus(&ui, &has) != LISTING_MAX - 1) {
        snprintf(note, n, "focus set to row %d landed on %u", LISTING_MAX - 1, ui_frame_focus(&ui, &has));
        return 1;
    }

    /* Reached, not only jumped to: one press a frame, none wrapping. */
    memset(&ui, 0, sizeof(ui));
    listing_rows(&ui, LISTING_MAX, 0, 0, now);
    for (i = 1; i < LISTING_MAX; i++) {
        now += 16667u;
        listing_rows(&ui, LISTING_MAX, PAD_DOWN, PAD_DOWN, now);
        if ((int)ui_frame_focus(&ui, &has) != i) {
            snprintf(note, n, "%d presses down reached row %u, not row %d", i, ui_frame_focus(&ui, &has), i);
            return 1;
        }
    }
    snprintf(note, n, "%d rows registered in a frame of %d, and %d presses reached the last", LISTING_MAX, UI_FRAME_MAX, LISTING_MAX - 1);
    return 0;
}

void test_element_register(void) {
    selftest_add("element", "a line is centred on its caps", t_a_line_is_centred_on_its_caps);
    selftest_add("element", "a line too long is cut inside its width", t_a_line_too_long_is_cut_inside_its_width);
    selftest_add("element", "a cut line leaves no ink past its width", t_a_cut_line_leaves_no_ink_past_its_width);
    selftest_add("element", "a line that fits is left alone", t_a_line_that_fits_is_left_alone);
    selftest_add("element", "the roles differ", t_the_roles_differ);
    selftest_add("element", "an unknown role falls back", t_an_unknown_role_falls_back);
    selftest_add("element", "stacked roles land where the style says", t_stacked_roles_land_where_the_style_says);
    selftest_add("element", "a long line wraps rather than being cut", t_a_long_line_wraps_rather_than_being_cut);
    selftest_add("element", "a line with no room left is cut", t_a_line_with_no_room_left_is_cut);
    selftest_add("element", "a notice centres its block", t_a_notice_centres_its_block);
    selftest_add("element", "a hint row is as wide as its hints", t_a_hint_row_is_as_wide_as_its_hints);
    selftest_add("element", "the footer's two groups do not meet", t_the_footers_two_groups_do_not_meet);
    selftest_add("element", "the body is what the bars leave", t_the_body_is_what_the_bars_leave);
    selftest_add("element", "an empty card stays inside its rectangle", t_an_empty_card_stays_inside_its_rectangle);
    selftest_add("element", "a waiting well carries walking dots", t_a_waiting_well_carries_dots);
    selftest_add("element", "a caption's scrim never reaches opaque", t_a_captions_scrim_never_reaches_opaque);
    selftest_add("element", "a wide status stops at the title's floor", t_a_wide_status_stops_at_the_titles_floor);
    selftest_add("element", "a page opens and closes one box", t_a_page_opens_and_closes_one_box);
    selftest_add("element", "a busy page keeps its status", t_a_busy_page_keeps_its_status);
    selftest_add("frame", "a frame holds every row a listing registers", t_a_frame_holds_every_row_a_listing_registers);
}
