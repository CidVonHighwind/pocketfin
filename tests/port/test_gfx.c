/* See port/gfx.h. */

#include "base/align.h"
#include "port/gfx.h"

#include "port/platform.h"
#include "port/input.h"
#include "tools/selftest.h"
#include "tools/shot.h"
#include "tools/lab.h"
#include "view/stack.h"
#include "page/settings.h"
#include "port/text.h"

#include <stdio.h>

/* One step of tolerance: the console dithers, so a flat fill is two tones a
 * step apart. */
static int close_enough(unsigned short got, unsigned short want) {
    int dr = (int)(got & 0x1F) - (int)(want & 0x1F);
    int dg = (int)((got >> 5) & 0x3F) - (int)((want >> 5) & 0x3F);
    int db = (int)((got >> 11) & 0x1F) - (int)((want >> 11) & 0x1F);

    if (dr < 0) dr = -dr;
    if (dg < 0) dg = -dg;
    if (db < 0) db = -db;
    return dr <= 1 && dg <= 1 && db <= 1;
}

/* Twice: the swap latches at the next vertical blank, so a capture after one
 * frame reads the buffer that is not on screen. A check that read once went
 * green on the previous check's drawing. */
static void two_frames(int x, int y, int w, int h, unsigned rgb) {
    int i;

    for (i = 0; i < 2; i++) {
        gfx_frame_begin();
        gfx_fill(x, y, w, h, rgb);
        gfx_frame_end();
    }
}

static int t_a_fill_lands_where_it_was_asked(char *note, unsigned n) {
    unsigned short row[GFX_W];
    unsigned short want = GFX_PACK565(0xFF, 0x30, 0x20);

    if (gfx_start() != 0) {
        snprintf(note, n, "the panel did not come up");
        return -1;
    }
    two_frames(40, 40, 64, 64, 0xFF3020u);

    if (gfx_capture_row(60, row) != 0) {
        snprintf(note, n, "row 60 could not be read back");
        return 1;
    }
    if (!close_enough(row[60], want)) {
        snprintf(note, n, "inside the fill read %04x, wanted %04x", row[60], want);
        return 1;
    }
    /* A fill that ignored w would pass above and cover the screen. */
    if (close_enough(row[200], want)) {
        snprintf(note, n, "the fill reached x=200, 96 past its right edge");
        return 1;
    }
    snprintf(note, n, "64x64 at 40,40 read %04x inside and %04x outside", row[60], row[200]);
    return 0;
}

/* A fill outside a frame cannot be read back -- the next frame's clear erases
 * it -- so this checks the flag every drawing call asks. */
static int t_a_frame_opens_and_closes(char *note, unsigned n) {
    int inside, outside_before, outside_after;

    if (gfx_start() != 0) {
        snprintf(note, n, "the panel did not come up");
        return -1;
    }
    outside_before = gfx_in_frame();

    gfx_frame_begin();
    inside = gfx_in_frame();
    gfx_fill(0, 0, 8, 8, 0x102030u);
    gfx_frame_end();
    outside_after = gfx_in_frame();

    if (outside_before || !inside || outside_after) {
        snprintf(note, n, "in_frame read %d before, %d inside, %d after", outside_before, inside, outside_after);
        return 1;
    }
    snprintf(note, n, "0 outside, 1 inside, 0 after");
    return 0;
}

/* A call before gfx_frame_begin appends to a display list the console never
 * submits, so a primitive that skips the check draws on the desktop and
 * nothing on the console. gfx_frame_end flips even with no frame open, which
 * shows what these would have drawn without a clear erasing it first. */
static int t_a_primitive_refuses_outside_a_frame(char *note, unsigned n) {
    static POCKETFIN_ALIGN16 unsigned short art[64 * 64];
    unsigned short row[GFX_W];
    unsigned short bg = GFX_PACK565(0x40, 0x40, 0x40);
    int            i;

    if (gfx_start() != 0) {
        snprintf(note, n, "the panel did not come up");
        return -1;
    }
    for (i = 0; i < 64 * 64; i++) art[i] = GFX_PACK565(0xFF, 0x00, 0xFF);
    gfx_wrote_pixels();

    /* Both buffers one flat colour, whatever page parity a previous check
       left behind. */
    gfx_frame_begin();
    gfx_scissor_none();
    gfx_frame_end();
    for (i = 0; i < 2; i++) {
        gfx_frame_begin();
        gfx_fill(0, 0, GFX_W, GFX_H, 0x404040u);
        gfx_frame_end();
    }

    gfx_round_fill(40, 40, 64, 64, 8, 0xFF00FFu);
    gfx_blit(120, 40, art, 64, 64);
    gfx_scissor(0, 0, 32, 32);
    gfx_frame_end();

    /* Row 47, not 40: the corner pixel is cut away by the curve and reads
       background whether or not the primitive drew. */
    if (gfx_capture_row(47, row) != 0) {
        snprintf(note, n, "row 47 could not be read back");
        return 1;
    }
    if (!close_enough(row[47], bg)) {
        snprintf(note, n, "round_fill drew at 47,47 outside a frame: read %04x", row[47]);
        return 1;
    }
    if (!close_enough(row[140], bg)) {
        snprintf(note, n, "blit drew at 140,47 outside a frame: read %04x", row[140]);
        return 1;
    }

    /* Had the scissor set outside a frame stuck, x=400 would keep the old
       colour. */
    for (i = 0; i < 2; i++) {
        gfx_frame_begin();
        gfx_fill(0, 0, GFX_W, GFX_H, 0x800080u);
        gfx_frame_end();
    }
    if (gfx_capture_row(200, row) != 0) {
        snprintf(note, n, "row 200 could not be read back");
        return 1;
    }
    if (!close_enough(row[400], GFX_PACK565(0x80, 0x00, 0x80))) {
        snprintf(note, n, "the scissor set outside a frame clipped a later fill: %04x at x=400", row[400]);
        return 1;
    }

    snprintf(note, n, "round_fill, blit and scissor outside a frame all did nothing");
    return 0;
}

/* The only thing that can say the picture is coming back after a standby. */
static int t_the_panel_is_scanning(char *note, unsigned n) {
    unsigned a, b;

    if (gfx_start() != 0) {
        snprintf(note, n, "the panel did not come up");
        return -1;
    }
    a = gfx_vcount();
    if (gfx_wait_frame(200000u) != 0) {
        snprintf(note, n, "no frame started in 200 ms; the counter sat at %u", a);
        return 1;
    }
    b = gfx_vcount();
    snprintf(note, n, "the counter moved %u to %u", a, b);
    return 0;
}

/* gfx_frame_end is the loop's ONLY throttle. A guard that stopped it touching
 * a gone panel once removed the pacing with it, and the application busy-spun
 * through the exact window the resume needed. */
#define PACED_FRAMES 10

static int t_a_frame_costs_a_frame(char *note, unsigned n) {
    unsigned t0, took, each;
    int      i;

    if (gfx_start() != 0) {
        snprintf(note, n, "the panel did not come up");
        return -1;
    }
    /* The runs turn pacing off; this is the check that would notice it going
       missing from the application. */
    gfx_pace(1);
    t0 = platform_clock_us();
    for (i = 0; i < PACED_FRAMES; i++) {
        gfx_frame_begin();
        gfx_fill(0, 0, 16, 16, 0x000080u);
        gfx_frame_end();
    }
    took = platform_clock_us() - t0;
    gfx_pace(0);
    each = took / PACED_FRAMES;

    if (each < 8000u || each > 40000u) {
        snprintf(note, n, "%u frames took %u us, %u each -- not a 16.7 ms frame", (unsigned)PACED_FRAMES, took, each);
        return 1;
    }
    snprintf(note, n, "%u us a frame over %u", each, (unsigned)PACED_FRAMES);
    return 0;
}

static int t_the_panel_can_be_photographed(char *note, unsigned n) {
    unsigned rows = 0, lost = 0;
    int      i;

    if (gfx_start() != 0 || text_start() != 0) {
        snprintf(note, n, "the panel or the glyphs did not come up");
        return -1;
    }

    /* Both machines capture this scene to be diffed pixel for pixel, so
     * nothing in it may depend on a frame counter or a clock. */
    for (i = 0; i < 2; i++) {
        gfx_frame_begin();
        gfx_fill(0, 0, GFX_W, GFX_H, 0x101418u);

        gfx_fill(16, 24, 56, 52, 0xFF3020u);
        text_draw(16, 80, TEXT_SMALL, 0x90A0B0u, "fill");

        gfx_round_fill(80, 24, 56, 52, 8, 0xE0A020u);
        text_draw(80, 80, TEXT_SMALL, 0x90A0B0u, "round_fill");

        gfx_round_frame(144, 24, 56, 52, 8, 0x60E060u);
        text_draw(144, 80, TEXT_SMALL, 0x90A0B0u, "round_frame");

        gfx_round_hgrad(208, 24, 56, 52, 8, 0x203060u, 0xC0D0FFu);
        text_draw(208, 80, TEXT_SMALL, 0x90A0B0u, "round_hgrad");

        gfx_round_hgrad_frame(272, 24, 56, 52, 8, 0x802000u, 0xFFD060u);
        text_draw(272, 80, TEXT_SMALL, 0x90A0B0u, "hgrad_frame");

        gfx_round_hgrad_caps(336, 24, 56, 52, 8, 0x602030u, 0xFFC0D0u, GFX_CAP_LEFT);
        text_draw(336, 80, TEXT_SMALL, 0x90A0B0u, "caps: left");

        gfx_fill(400, 24, 56, 52, 0x304050u);
        gfx_blend_rect(400, 24, 56, 52, 0xFFFFFFu, 128);
        text_draw(400, 80, TEXT_SMALL, 0x90A0B0u, "blend 50%");

        gfx_hgrad(16, 104, 208, 28, 0x000000u, 0xFFFFFFu);
        text_draw(16, 136, TEXT_SMALL, 0x90A0B0u, "hgrad  black to white");

        gfx_vgrad(256, 104, 208, 28, 0xFF0000u, 0x0000FFu);
        text_draw(256, 136, TEXT_SMALL, 0x90A0B0u, "vgrad  red to blue");

        text_draw(16, 160, TEXT_TITLE, 0xFFFFFFu, "Pocketfin 0123");
        text_draw(16, 186, TEXT_BODY, 0xC0D0E0u, "The quick brown fox");
        text_draw(16, 210, TEXT_SMALL, 0x80C080u, "jumps over the lazy dog");

        /* Full width, so the right edge is the scissor's and nothing else. */
        gfx_scissor(256, 232, 160, 24);
        gfx_fill(0, 232, GFX_W, 24, 0x804000u);
        gfx_scissor_none();
        text_draw(16, 236, TEXT_SMALL, 0x90A0B0u, "scissor 256..416");
        gfx_frame_end();
    }

    if (shot_write("reference.ppm") != 0) {
        shot_stats(&rows, &lost);
        snprintf(note, n, "the picture did not land: %u rows sent, %u lost", rows, lost);
        return 1;
    }
    shot_stats(&rows, &lost);
    snprintf(note, n, "%dx%d to reference.ppm, %u rows sent", GFX_W, GFX_H, rows);
    return 0;
}

static int t_the_page_draws(char *note, unsigned n) {
    int i;

    if (gfx_start() != 0 || text_start() != 0) {
        snprintf(note, n, "no panel or no glyphs");
        return -1;
    }

    for (i = 0; i < 2; i++) {
        gfx_frame_begin();
        lab_page_draw();
        gfx_frame_end();
    }
    if (shot_write("lab.ppm") != 0) {
        snprintf(note, n, "the picture did not land");
        return 1;
    }
    snprintf(note, n, "the page captured");
    return 0;
}

/* Pure arithmetic, so both machines must answer identically. */
static int t_a_blit_refuses_what_the_engine_cannot_draw(char *note, unsigned n) {
    static POCKETFIN_ALIGN16 unsigned short ok16[64];
    const unsigned short                   *odd = ok16 + 1; /* 2-byte aligned, not 16 */

    if (!gfx_blit_can(ok16, 128, 128)) {
        snprintf(note, n, "128x128 aligned was refused");
        return 1;
    }
    if (gfx_blit_can(ok16, 100, 150)) {
        snprintf(note, n, "100x150 was accepted -- neither is a power of two");
        return 1;
    }
    if (gfx_blit_can(ok16, 128, 192)) {
        snprintf(note, n, "192 was accepted and is not a power of two");
        return 1;
    }
    if (gfx_blit_can(ok16, 1024, 128)) {
        snprintf(note, n, "1024 was accepted, past GFX_BLIT_MAX");
        return 1;
    }
    if (gfx_blit_can(odd, 128, 128)) {
        snprintf(note, n, "an unaligned pointer was accepted");
        return 1;
    }
    if (gfx_blit_can(0, 128, 128) || gfx_blit_can(ok16, 0, 128)) {
        snprintf(note, n, "a null source or a zero side was accepted");
        return 1;
    }
    snprintf(note, n, "powers of two up to %d, 16-byte aligned, and nothing else", GFX_BLIT_MAX);
    return 0;
}

/* Four corner faults were each found by looking at a render, none by a check.
 * The frame is not the ground's colour: a broken rounding falls into the
 * straight-border branch and would be painted the ground, invisibly. */
#define CARD_BG   0x101820u
#define CARD_EDGE 0x20C040u

static int t_a_card_corner_is_cut(char *note, unsigned n) {
    static POCKETFIN_ALIGN16 unsigned short art[64 * 64];
    unsigned short                          row[GFX_W], bg, mid, corner;
    int                                     i;

    if (gfx_start() != 0) {
        snprintf(note, n, "the panel did not come up");
        return -1;
    }
    for (i = 0; i < 64 * 64; i++) art[i] = GFX_PACK565(0xFF, 0xFF, 0xFF);
    gfx_wrote_pixels();
    bg = GFX_PACK565(0x10, 0x18, 0x20);

    for (i = 0; i < 2; i++) {
        gfx_frame_begin();
        gfx_fill(0, 0, GFX_W, GFX_H, CARD_BG);
        gfx_blit_card(40, 40, art, 64, 64, 64, 64, 8, CARD_EDGE, CARD_EDGE, CARD_BG);
        gfx_frame_end();
    }
    (void)shot_write("cardcorner.ppm");
    if (gfx_capture_row(40, row) != 0) {
        snprintf(note, n, "the top row could not be read back");
        return 1;
    }
    /* The extreme corner is outside the curve: the ground, neither picture
     * nor frame. */
    corner = row[40];
    if (!close_enough(corner, bg)) {
        snprintf(note, n, "the corner read %04x, wanted the ground %04x (frame %04x)", corner, bg, GFX_PACK565(0x20, 0xC0, 0x40));
        return 1;
    }
    if (gfx_capture_row(72, row) != 0) {
        snprintf(note, n, "the middle row could not be read back");
        return 1;
    }
    mid = row[72];
    if (close_enough(mid, bg)) {
        snprintf(note, n, "the card's middle is the background -- nothing drew");
        return 1;
    }
    snprintf(note, n, "corner %04x is the ground, middle %04x is the picture", corner, mid);
    return 0;
}

#define COST_FRAMES 20

static int t_what_a_full_page_costs(char *note, unsigned n) {
    unsigned t0, frame_us, draw_us = 0;
    int      i;

    if (gfx_start() != 0 || text_start() != 0) {
        snprintf(note, n, "no panel or no glyphs");
        return -1;
    }

    t0 = platform_clock_us();
    for (i = 0; i < COST_FRAMES; i++) {
        unsigned d0;

        gfx_frame_begin();
        d0 = platform_clock_us();
        screen_run_frame(ui_rect_make(0, 0, GFX_W, GFX_H), 0, 0);
        draw_us += platform_clock_us() - d0;
        gfx_frame_end();
    }
    frame_us = (platform_clock_us() - t0) / COST_FRAMES;
    draw_us /= COST_FRAMES;

    if (frame_us > 40000u) {
        snprintf(note, n, "%u us a frame -- past two refreshes", frame_us);
        return 1;
    }
    snprintf(note, n, "drawing %u us of a %u us frame (one refresh 16683)", draw_us, frame_us);
    return 0;
}

void test_gfx_register(void) {
    selftest_add("gfx", "a fill lands where it was asked", t_a_fill_lands_where_it_was_asked);
    selftest_add("gfx", "a frame opens and closes", t_a_frame_opens_and_closes);
    selftest_add("gfx", "a primitive refuses outside a frame", t_a_primitive_refuses_outside_a_frame);
    selftest_add("gfx", "the panel is scanning", t_the_panel_is_scanning);
    selftest_add("gfx", "a frame costs a frame", t_a_frame_costs_a_frame);
    selftest_add("gfx", "the panel can be photographed", t_the_panel_can_be_photographed);
    selftest_add("gfx", "the page draws", t_the_page_draws);
    selftest_add("gfx", "a blit refuses what the engine cannot draw", t_a_blit_refuses_what_the_engine_cannot_draw);
    selftest_add("gfx", "a card corner is cut", t_a_card_corner_is_cut);
    selftest_add("gfx", "what a full page costs", t_what_a_full_page_costs);
}
