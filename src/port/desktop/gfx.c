/* gfx.h, for the PC. A software rasteriser into the same 5-6-5 buffers the
 * console scans, so a pixel check compares like with like. Headless: the
 * shell reads frames back through the capture seam. */

#include "port/gfx.h"

#include "port/decode.h"

#include "base/corners.h"

#include "port/platform.h"

#include <stdlib.h>
#include <string.h>

/* 59.94 Hz, the rate the console's panel actually refreshes at. */
#define REFRESH_US 16683u

static unsigned short *g_fb[2];

static int g_cx0, g_cy0, g_cx1 = GFX_W, g_cy1 = GFX_H;
static int g_page;
static int g_up;
static int g_in_frame;

static unsigned g_started;

/* There is no panel, so the clock stands in for one. */
unsigned gfx_vcount(void) {
    if (!g_up) return 0;
    return (platform_clock_us() - g_started) / REFRESH_US;
}

static int g_pace = 1;

int gfx_up(void) { return g_up; }

int gfx_in_frame(void) { return g_in_frame; }

int gfx_wait_frame(unsigned bound_us) {
    unsigned start = gfx_vcount(), t0 = platform_clock_us();

    while (gfx_vcount() == start) {
        if (platform_clock_us() - t0 > bound_us) return -1;
        platform_sleep_us(1000);
    }
    return 0;
}

int gfx_start(void) {
    unsigned pixels = GFX_STRIDE * GFX_H;

    if (g_up) return 0;
    g_fb[0] = (unsigned short *)calloc(pixels, sizeof(unsigned short));
    g_fb[1] = (unsigned short *)calloc(pixels, sizeof(unsigned short));
    if (!g_fb[0] || !g_fb[1]) return -1;

    corners_build();
    g_started = platform_clock_us();
    g_up      = 1;
    return 0;
}

void gfx_stop(void) {
    if (!g_up) return;
    g_up = 0;
    free(g_fb[0]);
    g_fb[0] = 0;
    free(g_fb[1]);
    g_fb[1] = 0;
}

/* While a film runs the whole frame is composed at 8888 on its surface; see
   port/decode.h. */
static unsigned *g_on;
static int       g_on_stride;
/* For the read-back: the shell presents the film's surface while a film
   runs. */
static unsigned *g_shown_on;
static int       g_shown_stride;

static void put(int x, int y, int r, int g, int b) {
    if (g_on)
        g_on[y * g_on_stride + x] = DECODE_PACK8888(r, g, b);
    else
        g_fb[g_page][y * GFX_STRIDE + x] = GFX_PACK565(r, g, b);
}

static void get(int x, int y, int *r, int *g, int *b) {
    if (g_on) {
        unsigned p = g_on[y * g_on_stride + x];

        *r = (int)(p & 0xFF);
        *g = (int)((p >> 8) & 0xFF);
        *b = (int)((p >> 16) & 0xFF);
    } else {
        unsigned short p = g_fb[g_page][y * GFX_STRIDE + x];

        /* Red in the LOW bits, as the panel stores it. */
        *r = (p & 0x1F) << 3;
        *g = ((p >> 5) & 0x3F) << 2;
        *b = ((p >> 11) & 0x1F) << 3;
    }
}

static void put565(int x, int y, unsigned short p) {
    if (g_on)
        put(x, y, (p & 0x1F) << 3, ((p >> 5) & 0x3F) << 2, ((p >> 11) & 0x1F) << 3);
    else
        g_fb[g_page][y * GFX_STRIDE + x] = p;
}

void gfx_frame_begin(void) {
    if (!g_up || g_in_frame) return;
    g_on       = 0;
    g_shown_on = 0;
    memset(g_fb[g_page], 0, GFX_STRIDE * GFX_H * sizeof(*g_fb[0]));
    g_in_frame = 1;
}

void gfx_fill(int x, int y, int w, int h, unsigned rgb) {
    int row;

    if (!g_up || !g_in_frame) return;
    if (x < g_cx0) {
        w += x - g_cx0;
        x = g_cx0;
    }
    if (y < g_cy0) {
        h += y - g_cy0;
        y = g_cy0;
    }
    if (x + w > g_cx1) w = g_cx1 - x;
    if (y + h > g_cy1) h = g_cy1 - y;
    if (w <= 0 || h <= 0) return;

    for (row = 0; row < h; row++) {
        int i;

        for (i = 0; i < w; i++) put(x + i, y + row, (int)((rgb >> 16) & 0xFF), (int)((rgb >> 8) & 0xFF), (int)(rgb & 0xFF));
    }
}

void gfx_blend(int x, int y, unsigned rgb, unsigned char coverage);

/* `fx`,`fy` mirror the mask, so the same bytes serve all four corners. The
 * colour follows the ramp across the box, so a rounded end is part of the
 * gradient rather than a flat cap glued onto it; `den` 0 is flat `left`. */
static void arc(const unsigned char *m, int r, int x, int y, int fx, int fy, unsigned left, unsigned right, int col0, int den) {
    int px, py;

    for (py = 0; py < r; py++)
        for (px = 0; px < r; px++) {
            int mx = fx ? r - 1 - px : px;
            int my = fy ? r - 1 - py : py;

            gfx_blend(x + px, y + py, corners_ramp(left, right, col0 + px, den), m[my * CORNER_PITCH + mx]);
        }
}

void gfx_round_fill(int x, int y, int w, int h, int r, unsigned rgb) {
    if (!g_up || !g_in_frame || w <= 0 || h <= 0) return;
    r = corners_clamp_r(w, h, r);
    if (r < 1) {
        gfx_fill(x, y, w, h, rgb);
        return;
    }

    gfx_fill(x + r, y, w - 2 * r, h, rgb);
    gfx_fill(x, y + r, r, h - 2 * r, rgb);
    gfx_fill(x + w - r, y + r, r, h - 2 * r, rgb);

    arc(corner_mask[r], r, x, y, 0, 0, rgb, rgb, 0, 0);
    arc(corner_mask[r], r, x + w - r, y, 1, 0, rgb, rgb, 0, 0);
    arc(corner_mask[r], r, x, y + h - r, 0, 1, rgb, rgb, 0, 0);
    arc(corner_mask[r], r, x + w - r, y + h - r, 1, 1, rgb, rgb, 0, 0);
}

void gfx_round_hgrad_caps(int x, int y, int w, int h, int r, unsigned left, unsigned right, int caps) {
    int lr, rr, col;

    if (!g_up || !g_in_frame || w <= 0 || h <= 0) return;
    r  = corners_clamp_r(w, h, r);
    lr = (caps & GFX_CAP_LEFT) ? r : 0;
    rr = (caps & GFX_CAP_RIGHT) ? r : 0;

    for (col = lr; col < w - rr; col++) gfx_fill(x + col, y, 1, h, corners_ramp(left, right, col, w - 1));

    /* The caps ramp too: flat endpoint colours leave a plateau r columns wide
     * at each end. */
    if (lr) {
        for (col = 0; col < lr; col++) gfx_fill(x + col, y + lr, 1, h - 2 * lr, corners_ramp(left, right, col, w - 1));
        arc(corner_mask[lr], lr, x, y, 0, 0, left, right, 0, w - 1);
        arc(corner_mask[lr], lr, x, y + h - lr, 0, 1, left, right, 0, w - 1);
    }
    if (rr) {
        for (col = 0; col < rr; col++) gfx_fill(x + w - rr + col, y + rr, 1, h - 2 * rr, corners_ramp(left, right, w - rr + col, w - 1));
        arc(corner_mask[rr], rr, x + w - rr, y, 1, 0, left, right, w - rr, w - 1);
        arc(corner_mask[rr], rr, x + w - rr, y + h - rr, 1, 1, left, right, w - rr, w - 1);
    }
}

void gfx_round_hgrad(int x, int y, int w, int h, int r, unsigned left, unsigned right) {
    gfx_round_hgrad_caps(x, y, w, h, r, left, right, GFX_CAP_BOTH);
}

void gfx_hgrad(int x, int y, int w, int h, unsigned left, unsigned right) { gfx_round_hgrad_caps(x, y, w, h, 0, left, right, 0); }

/* Sides and arcs, not a fill with a smaller one punched out: there is no
 * colour behind it to punch with. */
void gfx_round_hgrad_frame(int x, int y, int w, int h, int r, unsigned left, unsigned right) {
    int col;

    if (!g_up || !g_in_frame || w <= 0 || h <= 0) return;
    r = corners_clamp_r(w, h, r);

    for (col = r; col < w - r; col++) {
        unsigned c = corners_ramp(left, right, col, w - 1);

        gfx_fill(x + col, y, 1, 1, c);
        gfx_fill(x + col, y + h - 1, 1, 1, c);
    }
    gfx_fill(x, y + r, 1, h - 2 * r, left);
    gfx_fill(x + w - 1, y + r, 1, h - 2 * r, right);

    if (r < 1) return;
    arc(corner_stroke[r], r, x, y, 0, 0, left, right, 0, w - 1);
    arc(corner_stroke[r], r, x + w - r, y, 1, 0, left, right, w - r, w - 1);
    arc(corner_stroke[r], r, x, y + h - r, 0, 1, left, right, 0, w - 1);
    arc(corner_stroke[r], r, x + w - r, y + h - r, 1, 1, left, right, w - r, w - 1);
}

void gfx_round_frame(int x, int y, int w, int h, int r, unsigned rgb) {
    if (!g_up || !g_in_frame || w <= 0 || h <= 0) return;
    r = corners_clamp_r(w, h, r);
    if (r < 1) {
        gfx_fill(x, y, w, 1, rgb);
        gfx_fill(x, y + h - 1, w, 1, rgb);
        gfx_fill(x, y, 1, h, rgb);
        gfx_fill(x + w - 1, y, 1, h, rgb);
        return;
    }
    /* From the corner box's edge, not inside it: the arc already draws its own
     * top row, and a solid line over those pixels replaces the antialiasing
     * with a step. */
    gfx_fill(x + r, y, w - 2 * r, 1, rgb);
    gfx_fill(x + r, y + h - 1, w - 2 * r, 1, rgb);
    gfx_fill(x, y + r, 1, h - 2 * r, rgb);
    gfx_fill(x + w - 1, y + r, 1, h - 2 * r, rgb);

    arc(corner_stroke[r], r, x, y, 0, 0, rgb, rgb, 0, 0);
    arc(corner_stroke[r], r, x + w - r, y, 1, 0, rgb, rgb, 0, 0);
    arc(corner_stroke[r], r, x, y + h - r, 0, 1, rgb, rgb, 0, 0);
    arc(corner_stroke[r], r, x + w - r, y + h - r, 1, 1, rgb, rgb, 0, 0);
}

void gfx_blend_rect(int x, int y, int w, int h, unsigned rgb, int alpha) {
    int px, py;

    if (!g_up || !g_in_frame || alpha <= 0) return;
    if (alpha > 255) alpha = 255;

    for (py = 0; py < h; py++)
        for (px = 0; px < w; px++) gfx_blend(x + px, y + py, rgb, (unsigned char)alpha);
}

void gfx_vgrad(int x, int y, int w, int h, unsigned top, unsigned bottom) {
    int row;

    if (!g_up || !g_in_frame || h <= 0) return;

    for (row = 0; row < h; row++) {
        unsigned r = (((top >> 16) & 0xFF) * (h - 1 - row) + ((bottom >> 16) & 0xFF) * row) / (h > 1 ? h - 1 : 1);
        unsigned g = (((top >> 8) & 0xFF) * (h - 1 - row) + ((bottom >> 8) & 0xFF) * row) / (h > 1 ? h - 1 : 1);
        unsigned b = ((top & 0xFF) * (h - 1 - row) + (bottom & 0xFF) * row) / (h > 1 ? h - 1 : 1);

        gfx_fill(x, y + row, w, 1, (r << 16) | (g << 8) | b);
    }
}

void gfx_scissor(int x, int y, int w, int h) {
    if (!g_up || !g_in_frame) return;
    g_cx0 = x < 0 ? 0 : x;
    g_cy0 = y < 0 ? 0 : y;
    g_cx1 = (x + w > GFX_W) ? GFX_W : x + w;
    g_cy1 = (y + h > GFX_H) ? GFX_H : y + h;
    if (g_cx1 < g_cx0) g_cx1 = g_cx0;
    if (g_cy1 < g_cy0) g_cy1 = g_cy0;
}

void gfx_scissor_none(void) {
    if (!g_up || !g_in_frame) return;
    g_cx0 = g_cy0 = 0;
    g_cx1         = GFX_W;
    g_cy1         = GFX_H;
}

int gfx_blit(int x, int y, const unsigned short *src, int w, int h) { return gfx_blit_part(x, y, src, w, h, w, h, 0, 0); }

int gfx_blit_part(int x, int y, const unsigned short *src, int tex_w, int tex_h, int w, int h, int r, unsigned bg) {
    int px, py;

    if (!g_up || !g_in_frame) return 0;
    if (!gfx_blit_can(src, tex_w, tex_h)) return 0;
    if (w <= 0 || h <= 0 || w > tex_w || h > tex_h) return 0;
    r = corners_clamp_r(w, h, r);

    for (py = 0; py < h; py++)
        for (px = 0; px < w; px++) {
            /* mask[0][0] is the extreme corner, so the outermost pixel
             * indexes 0. Mirrored the other way it cut a hole one pixel
             * inside the image instead of the corner off it. */
            int cx = px < r ? px : (px >= w - r ? r - 1 - (px - (w - r)) : -1);
            int cy = py < r ? py : (py >= h - r ? r - 1 - (py - (h - r)) : -1);

            unsigned short p     = src[py * tex_w + px];
            int            cover = 255;

            if (r > 0 && cx >= 0 && cy >= 0) cover = corner_mask[r][cy * CORNER_PITCH + cx];
            if (!cover) continue;

            if (cover == 255) {
                if (x + px < g_cx0 || x + px >= g_cx1) continue;
                if (y + py < g_cy0 || y + py >= g_cy1) continue;
                put565(x + px, y + py, p);
            } else {
                /* `bg` is the console's: it cannot read the frame buffer
                 * mid-draw, and this machine can. */
                (void)bg;
                gfx_blend(x + px, y + py,
                          ((unsigned)((p & 0x1F) << 3) << 16) | ((unsigned)(((p >> 5) & 0x3F) << 2) << 8) |
                              (unsigned)(((p >> 11) & 0x1F) << 3),
                          (unsigned char)cover);
            }
        }
    return 1;
}

int gfx_blit_card(int x, int y, const unsigned short *src, int tex_w, int tex_h, int w, int h, int r, unsigned edge_from, unsigned edge_to,
                  unsigned bg) {
    int px, py;

    (void)bg; /* this machine reads what is under it */

    if (!g_up || !g_in_frame) return 0;
    if (!gfx_blit_can(src, tex_w, tex_h)) return 0;
    if (w <= 0 || h <= 0 || w > tex_w || h > tex_h) return 0;
    r = corners_clamp_r(w, h, r);

    for (py = 0; py < h; py++)
        for (px = 0; px < w; px++) {
            int            cx    = px < r ? px : (px >= w - r ? r - 1 - (px - (w - r)) : -1);
            int            cy    = py < r ? py : (py >= h - r ? r - 1 - (py - (h - r)) : -1);
            int            alpha = 255, edge = 0;
            unsigned short p;
            int            ir, ig, ib, er, eg, eb, t;

            if (cx >= 0 && cy >= 0) {
                alpha = corner_mask[r][cy * CORNER_PITCH + cx];
                edge  = corner_band[r][cy * CORNER_PITCH + cx];
            } else if (px == 0 || px == w - 1 || py == 0 || py == h - 1) {
                edge = 255; /* the straight run of the border */
            }
            if (!alpha) continue;
            if (x + px < g_cx0 || x + px >= g_cx1) continue;
            if (y + py < g_cy0 || y + py >= g_cy1) continue;

            p  = src[py * tex_w + px];
            ir = (p & 0x1F) << 3;
            ig = ((p >> 5) & 0x3F) << 2;
            ib = ((p >> 11) & 0x1F) << 3;

            t  = w > 1 ? px * 255 / (w - 1) : 0;
            er = (int)((edge_from >> 16) & 0xFF) + ((int)((edge_to >> 16) & 0xFF) - (int)((edge_from >> 16) & 0xFF)) * t / 255;
            eg = (int)((edge_from >> 8) & 0xFF) + ((int)((edge_to >> 8) & 0xFF) - (int)((edge_from >> 8) & 0xFF)) * t / 255;
            eb = (int)(edge_from & 0xFF) + ((int)(edge_to & 0xFF) - (int)(edge_from & 0xFF)) * t / 255;

            /* The frame tints the pixel, then one composite with the shape's
               alpha. */
            ir += (er - ir) * edge / 255;
            ig += (eg - ig) * edge / 255;
            ib += (eb - ib) * edge / 255;

            if (alpha == 255)
                put(x + px, y + py, ir, ig, ib);
            else
                gfx_blend(x + px, y + py, ((unsigned)ir << 16) | ((unsigned)ig << 8) | (unsigned)ib, (unsigned char)alpha);
        }
    return 1;
}

int gfx_capture_row(int y, unsigned short *dst) {
    /* The page not being drawn into is the one a swap put on screen. */
    const unsigned short *row;

    if (!g_up || !dst || y < 0 || y >= GFX_H) return -1;
    row = g_fb[g_page ^ 1] + y * GFX_STRIDE;
    memcpy(dst, row, GFX_W * sizeof(*dst));
    return 0;
}

int gfx_capture_row8888(int y, unsigned *dst) {
    int x;

    if (!g_up || !dst || y < 0 || y >= GFX_H) return -1;

    if (g_shown_on) {
        memcpy(dst, g_shown_on + y * g_shown_stride, GFX_W * sizeof(*dst));
        return 0;
    }
    {
        const unsigned short *row = g_fb[g_page ^ 1] + y * GFX_STRIDE;

        for (x = 0; x < GFX_W; x++) {
            unsigned short p = row[x];

            dst[x] = DECODE_PACK8888((p & 0x1F) << 3, ((p >> 5) & 0x3F) << 2, ((p >> 11) & 0x1F) << 3);
        }
    }
    return 0;
}

void gfx_frame_end(void) {
    if (!g_up) return;
    g_in_frame = 0;
    g_page ^= 1;
    /* No panel to wait for; the wait makes a run pace like the console's. */
    if (g_pace) (void)gfx_wait_frame(20000u);
}

void gfx_pace(int on) { g_pace = on ? 1 : 0; }

/* Nothing to write back: this machine's engine is the CPU. */
void gfx_wrote_pixels(void) {}

/* No panel was taken: the shell reads whichever surface the frame was
   composed on. */
void gfx_panel_restore(void) {}

void gfx_copy8888(const void *src, int src_stride, int sx, int sy, void *dst, int dst_stride, int dx, int dy, int w, int h) {
    const unsigned *s = (const unsigned *)src;
    unsigned       *d = (unsigned *)dst;
    int             row;

    if (!s || !d || w <= 0 || h <= 0) return;
    for (row = 0; row < h; row++) memcpy(d + (dy + row) * dst_stride + dx, s + (sy + row) * src_stride + sx, (size_t)w * sizeof(*d));
}

/* Nothing to switch: this machine blends a pixel at a time. */
void gfx_use_mask_texture(void) {}

/* Declared by text.c rather than in port/gfx.h: the console blends in the
 * engine and has no equivalent. */
void gfx_blend(int x, int y, unsigned rgb, unsigned char coverage) {
    /* Signed: the difference goes negative wherever the glyph is darker than
     * what is under it, and unsigned it wraps to a bright pixel. */
    int r, g, b, a = coverage;

    if (!g_up || !coverage) return;
    if (x < g_cx0 || y < g_cy0 || x >= g_cx1 || y >= g_cy1) return;

    get(x, y, &r, &g, &b);
    r += ((int)((rgb >> 16) & 0xFF) - r) * a / 255;
    g += ((int)((rgb >> 8) & 0xFF) - g) * a / 255;
    b += ((int)(rgb & 0xFF) - b) * a / 255;
    put(x, y, r, g, b);
}

/* No clear and no swap: the picture is already there. */
void gfx_frame_begin_on(void *px, int stride) {
    if (!g_up || g_in_frame || !px) return;
    g_on        = (unsigned *)px;
    g_on_stride = stride;
    g_in_frame  = 1;
}

void gfx_frame_end_on(void) {
    if (!g_up || !g_in_frame) return;
    g_shown_on     = g_on;
    g_shown_stride = g_on_stride;
    g_on           = 0;
    g_in_frame     = 0;
}
