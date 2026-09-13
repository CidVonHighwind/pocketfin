/* While a film runs the frame is composed at 8888 on the decoder's surface
 * (see port/decode.h), and these compose it the way app.c does. */

#include "port/decode.h"
#include "port/gfx.h"

#include "tools/selftest.h"
#include "view/element.h"

#include <stdio.h>
#include <string.h>

/* No part of the interface draws this colour, and it does not survive 5-6-5
   unchanged. */
#define PIC_R 37
#define PIC_G 149
#define PIC_B 211

static unsigned *surface(int *stride, char *note, unsigned n) {
    void *s;

    if (!decode_available()) {
        snprintf(note, n, "this build has no decoder");
        return 0;
    }
    if (gfx_start() != 0) {
        snprintf(note, n, "the panel did not come up");
        return 0;
    }
    s = decode_surface(stride);
    if (!s) {
        snprintf(note, n, "no film is open, so there is no surface");
        return 0;
    }
    return (unsigned *)s;
}

static int open_any(char *note, unsigned n) {
    /* From the fixture rather than invented: an invented pair is refused. */
    static const unsigned char SPS[] = {0x67, 0x42, 0xC0, 0x0D, 0xA6, 0x11, 0x11, 0xE8, 0x40, 0x00, 0x00, 0x03,
                                        0x00, 0x40, 0x00, 0x00, 0x0C, 0x03, 0xC5, 0x8B, 0x65, 0x80};
    static const unsigned char PPS[] = {0x68, 0xCB, 0x83, 0xCB, 0x20};

    if (decode_open(SPS, sizeof(SPS), PPS, sizeof(PPS), 4, 320, 240) != 0) {
        snprintf(note, n, "%s", decode_error());
        return -1;
    }
    return 0;
}

/* Composed at 5-6-5 the colour comes back rounded to the nearest of 32 reds,
   the banding this path exists to avoid. */
static int t_a_picture_keeps_every_bit(char *note, unsigned n) {
    unsigned *surf;
    unsigned  row[GFX_W];
    int       stride = 0, x, y, r, g, b;

    if (open_any(note, n) != 0) return -1;
    surf = surface(&stride, note, n);
    if (!surf) {
        decode_close();
        return -1;
    }

    for (y = 0; y < GFX_H; y++)
        for (x = 0; x < GFX_W; x++) surf[y * stride + x] = DECODE_PACK8888(PIC_R, PIC_G, PIC_B);

    gfx_frame_begin_on(surf, stride);
    gfx_fill(0, GFX_H - 40, GFX_W, 40, 0x101010u);
    gfx_frame_end_on();
    /* Presented, as app_draw does: while a film runs the panel is the film's
       buffer, and reading before that reads the browser's. */
    decode_show();
    (void)gfx_wait_frame(50000u);

    if (gfx_capture_row8888(GFX_H / 2, row) != 0) {
        snprintf(note, n, "the composed frame could not be read");
        decode_close();
        return 1;
    }
    r = (int)(row[GFX_W / 2] & 0xFFu);
    g = (int)((row[GFX_W / 2] >> 8) & 0xFFu);
    b = (int)((row[GFX_W / 2] >> 16) & 0xFFu);
    decode_close();

    if (r != PIC_R || g != PIC_G || b != PIC_B) {
        snprintf(note, n, "%d,%d,%d came back as %d,%d,%d -- the film was quantised on the way to the panel", PIC_R, PIC_G, PIC_B, r, g, b);
        return 1;
    }
    snprintf(note, n, "%d,%d,%d exactly, through the composed frame", r, g, b);
    return 0;
}

/* A scrim that reaches opaque is a black bar across the picture; one that
   does nothing is text nobody can read over a bright scene. */
static int t_the_band_darkens_the_picture(char *note, unsigned n) {
    unsigned *surf;
    unsigned  row[GFX_W];
    int       stride = 0, x, y, r, g, b;

    if (open_any(note, n) != 0) return -1;
    surf = surface(&stride, note, n);
    if (!surf) {
        decode_close();
        return -1;
    }

    for (y = 0; y < GFX_H; y++)
        for (x = 0; x < GFX_W; x++) surf[y * stride + x] = DECODE_PACK8888(PIC_R, PIC_G, PIC_B);

    gfx_frame_begin_on(surf, stride);
    ui_scrim(ui_rect_make(0, GFX_H - 60, GFX_W, 60), 1);
    gfx_frame_end_on();
    decode_show();
    (void)gfx_wait_frame(50000u);

    if (gfx_capture_row8888(GFX_H - 10, row) != 0) {
        snprintf(note, n, "the composed frame could not be read");
        decode_close();
        return 1;
    }
    r = (int)(row[GFX_W / 2] & 0xFFu);
    g = (int)((row[GFX_W / 2] >> 8) & 0xFFu);
    b = (int)((row[GFX_W / 2] >> 16) & 0xFFu);
    decode_close();

    if (r >= PIC_R && g >= PIC_G && b >= PIC_B) {
        snprintf(note, n, "the scrim left %d,%d,%d untouched", r, g, b);
        return 1;
    }
    if (r == 0 && g == 0 && b == 0) {
        snprintf(note, n, "the scrim reached opaque -- the picture is gone under the band");
        return 1;
    }
    snprintf(note, n, "%d,%d,%d became %d,%d,%d under the band", PIC_R, PIC_G, PIC_B, r, g, b);
    return 0;
}

/* Left in the film's format, every page after it draws at the wrong depth
   into a buffer that is not being shown -- on the console, a machine that
   looks broken until it is reset. */
static int t_the_browser_gets_its_panel_back(char *note, unsigned n) {
    unsigned short row[GFX_W];

    if (open_any(note, n) != 0) return -1;
    if (!decode_surface(0)) {
        snprintf(note, n, "no surface to give back");
        decode_close();
        return -1;
    }
    decode_close();

    gfx_frame_begin();
    gfx_fill(0, 0, GFX_W, GFX_H, 0x203040u);
    gfx_frame_end();
    gfx_frame_begin();
    gfx_fill(0, 0, GFX_W, GFX_H, 0x203040u);
    gfx_frame_end();

    if (gfx_capture_row(GFX_H / 2, row) != 0) {
        snprintf(note, n, "the panel could not be read at 5-6-5 after a film");
        return 1;
    }
    snprintf(note, n, "the browser's own frame reads back after a film");
    return 0;
}

void test_compose_register(void) {
    selftest_add("compose", "a picture keeps every bit", t_a_picture_keeps_every_bit);
    selftest_add("compose", "the band darkens the picture", t_the_band_darkens_the_picture);
    selftest_add("compose", "the browser gets its panel back", t_the_browser_gets_its_panel_back);
}
