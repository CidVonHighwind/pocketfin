#include "port/jpeg.h"
#include "port/gfx.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char g_err[128];

const char *jpeg_error(void) { return g_err; }

void jpeg_fail(const char *msg) { snprintf(g_err, sizeof(g_err), "%s", msg); }

void jpeg_failc(const char *msg, int code) { snprintf(g_err, sizeof(g_err), "%s (0x%08X)", msg, (unsigned)code); }

void jpeg_failf(const char *f, ...) {
    va_list ap;

    va_start(ap, f);
    vsnprintf(g_err, sizeof(g_err), f, ap);
    va_end(ap);
}

void jpeg_err_clear(void) { g_err[0] = 0; }

static int sof_size(const unsigned char *p, unsigned len, int *w, int *h, int *progressive) {
    unsigned i = 2;

    if (len < 4 || p[0] != 0xFF || p[1] != 0xD8) return -1;

    while (i + 3 < len) {
        unsigned char m;
        unsigned      seg;

        if (p[i] != 0xFF) {
            i++;
            continue;
        }
        m = p[i + 1];
        i += 2;
        if (m == 0xFF) {
            i--;
            continue;
        }
        if (m == 0x01 || (m >= 0xD0 && m <= 0xD9)) continue; /* RSTn/TEM: no length byte */

        if (i + 1 >= len) return -1;
        seg = ((unsigned)p[i] << 8) | p[i + 1];
        if (seg < 2 || i + seg > len) return -1;

        /* C0..CF less C4/C8/CC (not frame headers); SOF2/6/10/14 are progressive. */
        if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
            if (seg < 8) return -1;
            *h           = ((int)p[i + 3] << 8) | p[i + 4];
            *w           = ((int)p[i + 5] << 8) | p[i + 6];
            *progressive = (m == 0xC2 || m == 0xC6 || m == 0xCA || m == 0xCE);
            return 0;
        }
        if (m == 0xDA) return -1;

        i += seg;
    }
    return -1;
}

int jpeg_probe(const void *jpg, unsigned jpg_len, int *w, int *h) {
    int sw = 0, sh = 0, prog = 0;

    if (!jpg || jpg_len < 4) {
        jpeg_fail("bad arguments");
        return JPEG_ERR_ARGS;
    }
    if (sof_size((const unsigned char *)jpg, jpg_len, &sw, &sh, &prog) != 0) {
        jpeg_fail("no JPEG frame header");
        return JPEG_ERR_HEADER;
    }
    if (prog) {
        jpeg_fail("progressive JPEG: the hardware decoder cannot");
        return JPEG_ERR_PROGRESSIVE;
    }
    if (sw <= 0 || sh <= 0 || sw > JPEG_MAX_W || sh > JPEG_MAX_H) {
        jpeg_fail("image larger than the panel");
        return JPEG_ERR_TOO_BIG;
    }
    if (w) *w = sw;
    if (h) *h = sh;
    return 0;
}

/* Box-averaged: nearest-neighbour turns cover-art text into noise. `pitch` is
 * the stride the decoder wrote at; reading at the image's width shears the
 * picture into diagonal streaks. */
void jpeg_shrink_565(const unsigned char *rgba, int pitch, int sw, int sh, void *out565, int out_w, int out_h, int *got_w, int *got_h) {
    unsigned short *dst = (unsigned short *)out565;
    int             dw = sw, dh = sh, dx, dy;

    /* Never enlarged: the console's decoder cannot scale up. */
    if (sw > out_w || sh > out_h) {
        if (sw * out_h > out_w * sh) {
            dw = out_w;
            dh = sh * out_w / sw;
        } else {
            dh = out_h;
            dw = sw * out_h / sh;
        }
    }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;

    for (dy = 0; dy < dh; dy++) {
        int y0 = dy * sh / dh, y1 = (dy + 1) * sh / dh;

        if (y1 <= y0) y1 = y0 + 1;
        for (dx = 0; dx < dw; dx++) {
            int      x0 = dx * sw / dw, x1 = (dx + 1) * sw / dw;
            unsigned r = 0, g = 0, b = 0, n = 0;
            int      x, y;

            if (x1 <= x0) x1 = x0 + 1;
            for (y = y0; y < y1; y++) {
                const unsigned char *p = rgba + ((size_t)y * pitch + x0) * 4;

                for (x = x0; x < x1; x++, p += 4, n++) {
                    r += p[0];
                    g += p[1];
                    b += p[2];
                }
            }
            dst[(size_t)dy * dw + dx] = GFX_PACK565(r / n, g / n, b / n);
        }
    }
    if (got_w) *got_w = dw;
    if (got_h) *got_h = dh;
}
