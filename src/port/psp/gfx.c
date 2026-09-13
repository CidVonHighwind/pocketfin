/* gfx.h, for the console. The frame loop is samples/gu/cube's, call for call:
 * its two waits block. */

#include "port/gfx.h"

#include "port/decode.h"

#include <string.h>

#include "base/corners.h"
#include "base/align.h"

#include "base/log.h"
#include "port/platform.h"

#include <pspdisplay.h>
#include <pspgu.h>
#include <pspkernel.h>

#include <stdint.h>

#define REFRESH_US   16683u /* one frame at 59.94 Hz */
#define PACE_SPIN_US 2000u
#define PACE_STEP_US 250u

/* Cache-line aligned with a dead line at each end, as pspgl does it. */
#define LIST_WORDS 262144
#define LIST_PAD   16 /* one 64-byte line */

static unsigned int __attribute__((aligned(64))) g_list_store[LIST_PAD + LIST_WORDS + LIST_PAD];

#define g_list (g_list_store + LIST_PAD)

/* Positions only, every field two bytes: mixing a 4-byte colour with 2-byte
 * positions gives a 10-byte struct the compiler pads to 12 and the engine
 * walks at 10. */
typedef struct {
    short x, y, z;
} vert;

typedef struct {
    float        s, t;
    unsigned int c;
    float        x, y, z;
} tvert;

/* Float positions for the same padding reason as `vert`. */
typedef struct {
    unsigned int c;
    float        x, y, z;
} cvert;

/* Coverage is the alpha; the colour comes from the vertex. */
#define A1(n)  ((((unsigned)(n)) << 24) | 0x00FFFFFFu)
#define A4(n)  A1(n), A1((n) + 1), A1((n) + 2), A1((n) + 3)
#define A16(n) A4(n), A4((n) + 4), A4((n) + 8), A4((n) + 12)
#define A64(n) A16(n), A16((n) + 16), A16((n) + 32), A16((n) + 48)
static POCKETFIN_ALIGN16 const unsigned int g_coverage_clut[256] = {A64(0), A64(64), A64(128), A64(192)};

/* The engine's texture setup, reset every frame: state carries across lists. */
#define TEX_NONE  0
#define TEX_MASK  1 /* T8 coverage through the CLUT, blended */
#define TEX_IMAGE 2 /* 5-6-5 pixels, replaced */
static int g_tex;

static void *g_fb[2];
static void *g_zb;
static int   g_up;
static int   g_in_frame;

static int g_page;

/* A swap named but not yet latched -- see gfx_capture_row(). */
static int g_swapped;

unsigned gfx_vcount(void) { return (unsigned)sceDisplayGetVcount(); }

int gfx_up(void) { return g_up; }

int gfx_in_frame(void) { return g_in_frame; }

void gfx_list_suspend(void) {
    if (!g_up || !g_in_frame) return;
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    g_in_frame = 0;
}

/* Once per swap: the controller latches a swap at the vertical blank, and
   until then sceDisplayGetFrameBuf answers with the buffer before. */
static const volatile void *shown(int *stride, int *fmt) {
    void *base = 0;

    if (g_swapped) {
        sceDisplayWaitVblankStart();
        g_swapped = 0;
    }
    if (sceDisplayGetFrameBuf(&base, stride, fmt, PSP_DISPLAY_SETBUF_IMMEDIATE) < 0 || !base || *stride <= 0) return 0;
    return (const volatile void *)(0x40000000u | (uintptr_t)base);
}

int gfx_capture_row8888(int y, unsigned *dst) {
    const volatile void           *px;
    const volatile unsigned short *row16;
    const volatile unsigned       *row32;
    int                            stride = 0, fmt = 0, x;

    if (!g_up || !dst || y < 0 || y >= GFX_H) return -1;
    px = shown(&stride, &fmt);
    if (!px) return -1;

    if (fmt == PSP_DISPLAY_PIXEL_FORMAT_8888) {
        row32 = (const volatile unsigned *)px + y * stride;
        for (x = 0; x < GFX_W; x++) dst[x] = row32[x];
        return 0;
    }
    if (fmt != PSP_DISPLAY_PIXEL_FORMAT_565) return -1;

    row16 = (const volatile unsigned short *)px + y * stride;
    for (x = 0; x < GFX_W; x++) {
        unsigned short p = row16[x];

        dst[x] = DECODE_PACK8888((p & 0x1F) << 3, ((p >> 5) & 0x3F) << 2, ((p >> 11) & 0x1F) << 3);
    }
    return 0;
}

int gfx_wait_frame(unsigned bound_us) {
    unsigned start = gfx_vcount(), t0 = platform_clock_us(), gone;

    for (;;) {
        if (gfx_vcount() != start) return 0;
        gone = platform_clock_us() - t0;
        if (gone >= bound_us) return -1;
        if (gone + PACE_SPIN_US < REFRESH_US) platform_sleep_us(PACE_STEP_US);
    }
}

static void gu_state(void) {
    /* A startup wedge has been seen between "gu_state" and "sync". */
    log_mark("gfx: gu_start");
    sceGuStart(GU_DIRECT, g_list);
    log_mark("gfx: gu_buffers");
    sceGuDrawBuffer(GU_PSM_5650, g_fb[g_page], GFX_STRIDE);
    sceGuDispBuffer(GFX_W, GFX_H, g_fb[g_page ^ 1], GFX_STRIDE);
    /* Unset, depth goes to VRAM offset 0: over the picture. */
    sceGuDepthBuffer(g_zb, GFX_STRIDE);
    sceGuOffset(2048 - GFX_W / 2, 2048 - GFX_H / 2);
    sceGuViewport(2048, 2048, GFX_W, GFX_H);
    sceGuDepthRange(0xC350, 0x2710);
    sceGuScissor(0, 0, GFX_W, GFX_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);
    sceGuShadeModel(GU_SMOOTH);
    /* The panel is 5-6-5 and a ramp bands without dithering. */
    sceGuEnable(GU_DITHER);
    log_mark("gfx: gu_finish");
    sceGuFinish();
    log_mark("gfx: sync");
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);

    log_mark("gfx: vblank");
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

int gfx_capture_row(int y, unsigned short *dst) {
    const volatile unsigned short *row;
    int                            stride = 0, fmt = 0, x;

    if (!g_up || !dst || y < 0 || y >= GFX_H) return -1;
    row = (const volatile unsigned short *)shown(&stride, &fmt);
    if (!row || fmt != PSP_DISPLAY_PIXEL_FORMAT_565) {
        log_printf("gfx: the panel is format %d, not 5-6-5 -- a film still has it", fmt);
        return -1;
    }

    row += y * stride;
    for (x = 0; x < GFX_W; x++) dst[x] = row[x];
    return 0;
}

int gfx_start(void) {
    if (g_up) return 0;

    /* A misaligned list corrupts memory somewhere nothing traces back here. */
    if (((uintptr_t)g_list & 63u) != 0) {
        log_line("gfx: THE DISPLAY LIST IS NOT CACHE-LINE ALIGNED");
        return -1;
    }

    g_fb[0] = guGetStaticVramBuffer(GFX_STRIDE, GFX_H, GU_PSM_5650);
    g_fb[1] = guGetStaticVramBuffer(GFX_STRIDE, GFX_H, GU_PSM_5650);
    g_zb    = guGetStaticVramBuffer(GFX_STRIDE, GFX_H, GU_PSM_4444);

    corners_build();
    gfx_wrote_pixels();

    log_mark("gfx: sceGuInit");
    sceGuInit();
    g_up = 1;
    log_mark("gfx: gu_state");
    gu_state();
    log_mark("gfx: gu_state done");

    platform_console_silence();

    log_printf("gfx: %dx%d on the engine, buffers %u and %u", GFX_W, GFX_H, (unsigned)(uintptr_t)g_fb[0], (unsigned)(uintptr_t)g_fb[1]);
    return 0;
}

/* Not the Range variant: its pointer and size must be cache-line aligned,
 * and getting that wrong writes back lines nobody meant to touch. */
void gfx_wrote_pixels(void) { sceKernelDcacheWritebackAll(); }

void gfx_copy8888(const void *src, int src_stride, int sx, int sy, void *dst, int dst_stride, int dx, int dy, int w, int h) {
    int mine = 0;

    if (!g_up || !src || !dst || w <= 0 || h <= 0) return;

    /* Without a list the command lands wherever the list pointer is: a bus
       error a long way from here. */
    if (!g_in_frame) {
        sceGuStart(GU_DIRECT, g_list);
        mine = 1;
    }

    /* The engine addresses memory physically, so the uncached alias is stripped. */
    sceGuCopyImage(GU_PSM_8888, sx, sy, w, h, src_stride, (void *)((uintptr_t)src & ~0x40000000u), dx, dy, dst_stride,
                   (void *)((uintptr_t)dst & ~0x40000000u));

    if (mine) {
        sceGuFinish();
        sceGuSync(0, 0);
    }
}

/* Format and address only, not contents: the caller draws a frame after. */
void gfx_panel_restore(void) {
    void *base   = 0;
    int   stride = 0, fmt = 0;

    if (!g_up) return;

    /* The engine first: a draw buffer left on the film's 8888 surface puts it
       up again on the next swap, however the panel is set. */
    if (!g_in_frame) {
        sceGuStart(GU_DIRECT, g_list);
        sceGuDrawBuffer(GU_PSM_5650, g_fb[g_page], GFX_STRIDE);
        sceGuDispBuffer(GFX_W, GFX_H, g_fb[g_page ^ 1], GFX_STRIDE);
        sceGuFinish();
        sceGuSync(0, 0);
    }

    /* On whatever it is showing: a panel left in 8888 under a 5-6-5 browser
       is worse than a black flash. */
    if (sceDisplayGetFrameBuf(&base, &stride, &fmt, PSP_DISPLAY_SETBUF_IMMEDIATE) >= 0 && base) {
        memset((void *)(0x40000000u | (uintptr_t)base), 0, (size_t)GFX_STRIDE * GFX_H * 2u);
        sceDisplaySetFrameBuf(base, GFX_STRIDE, PSP_DISPLAY_PIXEL_FORMAT_565, PSP_DISPLAY_SETBUF_NEXTFRAME);
    }
    g_swapped = 0;
}

void gfx_stop(void) {
    if (!g_up) return;
    g_up = 0;
    sceGuDisplay(GU_FALSE);
    sceGuTerm();
}

static void use_texture(int mode) {
    if (g_tex == mode) return;

    if (mode == TEX_NONE) {
        sceGuDisable(GU_BLEND);
        sceGuDisable(GU_TEXTURE_2D);
        sceGuDisable(GU_ALPHA_TEST);
        g_tex = mode;
        return;
    }
    sceGuEnable(GU_TEXTURE_2D);

    if (mode == TEX_MASK) {
        sceGuEnable(GU_BLEND);
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
        /* Discarded: blending by zero still writes the pixel. */
        sceGuEnable(GU_ALPHA_TEST);
        sceGuAlphaFunc(GU_GREATER, 0, 0xFF);
        sceGuTexMode(GU_PSM_T8, 0, 0, 0);
        sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
        sceGuEnable(GU_DITHER);
    } else {
        sceGuDisable(GU_BLEND);
        sceGuDisable(GU_ALPHA_TEST);
        /* These pixels are already 5-6-5; dithering would move them a step. */
        sceGuDisable(GU_DITHER);
        sceGuTexMode(GU_PSM_5650, 0, 0, 0);
        sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
    }
    sceGuTexEnvColor(0x0);
    sceGuTexOffset(0.0f, 0.0f);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    g_tex = mode;
}

void gfx_use_mask_texture(void) { use_texture(TEX_MASK); }

static void list_opened(void) {
    sceGuClutMode(GU_PSM_8888, 0, 0xFF, 0);
    sceGuClutLoad(32, g_coverage_clut);

    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_TEXTURE_2D);
    g_tex      = TEX_NONE;
    g_in_frame = 1;
}

void gfx_frame_begin_on(void *px, int stride) {
    if (!g_up || g_in_frame || !px) return;
    sceGuStart(GU_DIRECT, g_list);
    sceGuDrawBufferList(GU_PSM_8888, (void *)((uintptr_t)px & ~0x40000000u), stride);
    list_opened();
}

/* Never swapped: the decoder shows the buffer itself. The draw buffer goes
   back, or every browser frame after a film draws into the decoder's 8888
   buffer at the wrong stride and depth. */
void gfx_frame_end_on(void) {
    if (!g_up || !g_in_frame) return;
    sceGuDrawBuffer(GU_PSM_5650, g_fb[g_page], GFX_STRIDE);
    sceGuFinish();
    sceGuSync(0, 0);
    g_in_frame = 0;
}

void gfx_frame_begin(void) {
    if (!g_up || g_in_frame) return;
    sceGuStart(GU_DIRECT, g_list);
    sceGuClearColor(0xFF000000);
    sceGuClear(GU_COLOR_BUFFER_BIT);
    list_opened();
}

void gfx_fill(int x, int y, int w, int h, unsigned rgb) {
    vert    *v;
    unsigned c;

    if (!g_up || !g_in_frame) return;
    use_texture(TEX_NONE);
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > GFX_W) w = GFX_W - x;
    if (y + h > GFX_H) h = GFX_H - y;
    if (w <= 0 || h <= 0) return;

    c = 0xFF000000u | ((rgb & 0xFFu) << 16) | (rgb & 0xFF00u) | ((rgb >> 16) & 0xFFu);

    v = (vert *)sceGuGetMemory(2 * sizeof(vert));
    if (!v) return;
    v[0].x = (short)x;
    v[0].y = (short)y;
    v[0].z = 0;
    v[1].x = (short)(x + w);
    v[1].y = (short)(y + h);
    v[1].z = 0;

    sceGuColor(c);
    sceGuDrawArray(GU_SPRITES, GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, v);
}

static void fills(const short *xywh, int n, unsigned rgb) {
    vert    *v;
    unsigned c;
    int      i, out = 0;

    if (!g_up || !g_in_frame || n <= 0) return;
    use_texture(TEX_NONE);
    c = 0xFF000000u | ((rgb & 0xFFu) << 16) | (rgb & 0xFF00u) | ((rgb >> 16) & 0xFFu);

    v = (vert *)sceGuGetMemory((unsigned)(2 * n) * sizeof(vert));
    if (!v) return;
    for (i = 0; i < n; i++) {
        int x = xywh[i * 4], y = xywh[i * 4 + 1];
        int w = xywh[i * 4 + 2], h = xywh[i * 4 + 3];

        if (x < 0) {
            w += x;
            x = 0;
        }
        if (y < 0) {
            h += y;
            y = 0;
        }
        if (x + w > GFX_W) w = GFX_W - x;
        if (y + h > GFX_H) h = GFX_H - y;
        if (w <= 0 || h <= 0) continue;
        v[out].x = (short)x;
        v[out].y = (short)y;
        v[out].z = 0;
        out++;
        v[out].x = (short)(x + w);
        v[out].y = (short)(y + h);
        v[out].z = 0;
        out++;
    }
    if (!out) return;
    sceGuColor(c);
    sceGuDrawArray(GU_SPRITES, GU_VERTEX_16BIT | GU_TRANSFORM_2D, out, 0, v);
}

static unsigned abgr(unsigned rgb) { return 0xFF000000u | ((rgb & 0xFFu) << 16) | (rgb & 0xFF00u) | ((rgb >> 16) & 0xFFu); }

/* The mask stores the top-left corner; the others mirror s or t. Triangles,
 * not a sprite, which carries one colour and cannot hold the ramp `cl`..`cr`. */
static void corner_tris(tvert *v, int r, int x, int y, int s0, int t0, int s1, int t1, unsigned cl, unsigned cr) {
    float x0 = (float)x, x1 = (float)(x + r);
    float y0 = (float)y, y1 = (float)(y + r);
    float u0 = (float)s0, u1 = (float)s1;
    float w0 = (float)t0, w1 = (float)t1;
    int   i;

    /* TL TR BL, then TR BR BL. */
    const float    sx[6] = {x0, x1, x0, x1, x1, x0};
    const float    sy[6] = {y0, y0, y1, y0, y1, y1};
    const float    su[6] = {u0, u1, u0, u1, u1, u0};
    const float    sv[6] = {w0, w0, w1, w0, w1, w1};
    const unsigned sc[6] = {cl, cr, cl, cr, cr, cl};

    for (i = 0; i < 6; i++) {
        v[i].s = su[i];
        v[i].t = sv[i];
        v[i].c = sc[i];
        v[i].x = sx[i];
        v[i].y = sy[i];
        v[i].z = 0.0f;
    }
}

static void corners_of(int x, int y, int w, int h, int r, unsigned lc, unsigned rc) {
    tvert *v = (tvert *)sceGuGetMemory(24 * sizeof(tvert));

    if (!v) return;
    corner_tris(v + 0, r, x, y, 0, 0, r, r, lc, lc);
    corner_tris(v + 6, r, x + w - r, y, r, 0, 0, r, rc, rc);
    corner_tris(v + 12, r, x, y + h - r, 0, r, r, 0, lc, lc);
    corner_tris(v + 18, r, x + w - r, y + h - r, r, r, 0, 0, rc, rc);

    sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 24, 0, v);
}

/* One cap's two corners. The four corners of a 2r-wide box put arcs inside
 * the shape, and a rounded gradient's ends read as flat blocks. `x` is the
 * cap's left edge; `cl`/`cr` are the ramp there and r columns along. */
static void cap_corners(int x, int y, int h, int r, int right, unsigned cl, unsigned cr) {
    tvert *v = (tvert *)sceGuGetMemory(12 * sizeof(tvert));

    if (!v) return;
    if (right) {
        corner_tris(v + 0, r, x, y, r, 0, 0, r, cl, cr);
        corner_tris(v + 6, r, x, y + h - r, r, r, 0, 0, cl, cr);
    } else {
        corner_tris(v + 0, r, x, y, 0, 0, r, r, cl, cr);
        corner_tris(v + 6, r, x, y + h - r, 0, r, r, 0, cl, cr);
    }
    sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 12, 0, v);
}

static void mask_bind(const unsigned char *m) {
    sceGuTexImage(0, CORNER_PITCH, CORNER_PITCH, CORNER_PITCH, m);
    sceGuTexScale(1.0f / (float)CORNER_PITCH, 1.0f / (float)CORNER_PITCH);
}

void gfx_round_fill(int x, int y, int w, int h, int r, unsigned rgb) {
    unsigned c;

    if (!g_up || !g_in_frame || w <= 0 || h <= 0) return;
    r = corners_clamp_r(w, h, r);
    if (r < 1) {
        gfx_fill(x, y, w, h, rgb);
        return;
    }
    c = abgr(rgb);

    {
        const short cross[12] = {(short)(x + r), (short)y,           (short)(w - 2 * r), (short)h,       (short)x, (short)(y + r),
                                 (short)r,       (short)(h - 2 * r), (short)(x + w - r), (short)(y + r), (short)r, (short)(h - 2 * r)};

        fills(cross, 3, rgb);
    }

    use_texture(TEX_MASK);
    mask_bind(corner_mask[r]);
    corners_of(x, y, w, h, r, c, c);
}

/* Not a sprite: two vertices cannot carry two colours across the box. The
 * colours are top-left, bottom-left, top-right, bottom-right. */
static void quad(int x, int y, int w, int h, unsigned tl, unsigned bl, unsigned tr, unsigned br) {
    cvert         *v    = (cvert *)sceGuGetMemory(4 * sizeof(cvert));
    const unsigned c[4] = {tl, bl, tr, br};
    int            i;

    if (!v || w <= 0 || h <= 0) return;
    use_texture(TEX_NONE);
    for (i = 0; i < 4; i++) {
        v[i].c = c[i];
        v[i].x = (float)(i < 2 ? x : x + w);
        v[i].y = (float)(i % 2 ? y + h : y);
        v[i].z = 0.0f;
    }
    sceGuShadeModel(GU_SMOOTH);
    sceGuDrawArray(GU_TRIANGLE_STRIP, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 4, 0, v);
}

static void hgrad_quad(int x, int y, int w, int h, unsigned l, unsigned rt) { quad(x, y, w, h, l, l, rt, rt); }

void gfx_round_hgrad_caps(int x, int y, int w, int h, int r, unsigned left, unsigned right, int caps) {
    int      lr, rr;
    unsigned lc, rc;

    if (!g_up || !g_in_frame || w <= 0 || h <= 0) return;
    r  = corners_clamp_r(w, h, r);
    lr = (caps & GFX_CAP_LEFT) ? r : 0;
    rr = (caps & GFX_CAP_RIGHT) ? r : 0;

    /* Across the whole width: across the body alone, each cap shows a seam. */
    lc = corners_ramp(left, right, lr, w - 1);
    rc = corners_ramp(left, right, w - 1 - rr, w - 1);

    hgrad_quad(x + lr, y, w - lr - rr, h, abgr(lc), abgr(rc));

    if (lr) {
        unsigned l0 = abgr(corners_ramp(left, right, 0, w - 1));

        hgrad_quad(x, y + lr, lr, h - 2 * lr, l0, abgr(lc));
        use_texture(TEX_MASK);
        mask_bind(corner_mask[lr]);
        cap_corners(x, y, h, lr, 0, l0, abgr(lc));
    }
    if (rr) {
        unsigned r1 = abgr(corners_ramp(left, right, w - 1, w - 1));

        hgrad_quad(x + w - rr, y + rr, rr, h - 2 * rr, abgr(rc), r1);
        use_texture(TEX_MASK);
        mask_bind(corner_mask[rr]);
        cap_corners(x + w - rr, y, h, rr, 1, abgr(rc), r1);
    }
}

void gfx_round_hgrad(int x, int y, int w, int h, int r, unsigned left, unsigned right) {
    gfx_round_hgrad_caps(x, y, w, h, r, left, right, GFX_CAP_BOTH);
}

/* Gouraud samples at pixel centres, so the ramp never reaches column 0 or
 * w-1 on its own. */
void gfx_hgrad(int x, int y, int w, int h, unsigned left, unsigned right) {
    gfx_round_hgrad_caps(x, y, w, h, 0, left, right, 0);
    if (w > 1) {
        gfx_fill(x, y, 1, h, left);
        gfx_fill(x + w - 1, y, 1, h, right);
    }
}

void gfx_round_hgrad_frame(int x, int y, int w, int h, int r, unsigned left, unsigned right) {
    unsigned lc, rc;

    if (!g_up || !g_in_frame || w <= 0 || h <= 0) return;
    r  = corners_clamp_r(w, h, r);
    lc = corners_ramp(left, right, r, w - 1);
    rc = corners_ramp(left, right, w - 1 - r, w - 1);

    hgrad_quad(x + r, y, w - 2 * r, 1, abgr(lc), abgr(rc));
    hgrad_quad(x + r, y + h - 1, w - 2 * r, 1, abgr(lc), abgr(rc));
    gfx_fill(x, y + r, 1, h - 2 * r, left);
    gfx_fill(x + w - 1, y + r, 1, h - 2 * r, right);

    if (r < 1) return;
    use_texture(TEX_MASK);
    mask_bind(corner_stroke[r]);
    cap_corners(x, y, h, r, 0, abgr(corners_ramp(left, right, 0, w - 1)), abgr(lc));
    cap_corners(x + w - r, y, h, r, 1, abgr(rc), abgr(corners_ramp(left, right, w - 1, w - 1)));
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
    /* From the corner box's edge: a line over the arc's own row replaces its
     * antialiasing with a step. */
    {
        const short edges[16] = {(short)(x + r),
                                 (short)y,
                                 (short)(w - 2 * r),
                                 1,
                                 (short)(x + r),
                                 (short)(y + h - 1),
                                 (short)(w - 2 * r),
                                 1,
                                 (short)x,
                                 (short)(y + r),
                                 1,
                                 (short)(h - 2 * r),
                                 (short)(x + w - 1),
                                 (short)(y + r),
                                 1,
                                 (short)(h - 2 * r)};

        fills(edges, 4, rgb);
    }

    use_texture(TEX_MASK);
    mask_bind(corner_stroke[r]);
    {
        unsigned c = abgr(rgb);

        corners_of(x, y, w, h, r, c, c);
    }
}

void gfx_vgrad(int x, int y, int w, int h, unsigned top, unsigned bottom) {
    if (!g_up || !g_in_frame || w <= 0 || h <= 0) return;
    quad(x, y, w, h, abgr(top), abgr(bottom), abgr(top), abgr(bottom));
    if (h > 1) {
        gfx_fill(x, y, w, 1, top);
        gfx_fill(x, y + h - 1, w, 1, bottom);
    }
}

void gfx_blend_rect(int x, int y, int w, int h, unsigned rgb, int alpha) {
    if (!g_up || !g_in_frame || w <= 0 || h <= 0 || alpha <= 0) return;
    if (alpha > 255) alpha = 255;

    {
        cvert   *v = (cvert *)sceGuGetMemory(2 * sizeof(cvert));
        unsigned c = (((unsigned)alpha & 0xFFu) << 24) | ((rgb & 0xFFu) << 16) | (rgb & 0xFF00u) | ((rgb >> 16) & 0xFFu);

        if (!v) return;
        use_texture(TEX_NONE);
        sceGuEnable(GU_BLEND);
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
        v[0].c = c;
        v[0].x = (float)x;
        v[0].y = (float)y;
        v[0].z = 0.0f;
        v[1].c = c;
        v[1].x = (float)(x + w);
        v[1].y = (float)(y + h);
        v[1].z = 0.0f;
        sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, v);
        sceGuDisable(GU_BLEND);
    }
}

void gfx_scissor(int x, int y, int w, int h) {
    if (!g_up || !g_in_frame) return;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    /* Width and height, not corners. */
    sceGuScissor(x, y, w, h);
}

void gfx_scissor_none(void) {
    if (!g_up || !g_in_frame) return;
    sceGuScissor(0, 0, GFX_W, GFX_H);
}

int gfx_blit(int x, int y, const unsigned short *src, int w, int h) { return gfx_blit_part(x, y, src, w, h, w, h, 0, 0); }

int gfx_blit_part(int x, int y, const unsigned short *src, int tex_w, int tex_h, int w, int h, int r, unsigned bg) {
    tvert *v;

    if (!g_up || !g_in_frame) return 0;
    if (!gfx_blit_can(src, tex_w, tex_h)) return 0;
    if (w <= 0 || h <= 0 || w > tex_w || h > tex_h) return 0;

    use_texture(TEX_IMAGE);
    sceGuTexImage(0, tex_w, tex_h, tex_w, src);
    sceGuTexScale(1.0f / (float)tex_w, 1.0f / (float)tex_h);

    v = (tvert *)sceGuGetMemory(2 * sizeof(tvert));
    if (!v) return 0;
    v[0].s = 0.0f;
    v[0].t = 0.0f;
    v[0].c = 0xFFFFFFFFu;
    v[0].x = (float)x;
    v[0].y = (float)y;
    v[0].z = 0.0f;
    v[1].s = (float)w;
    v[1].t = (float)h;
    v[1].c = 0xFFFFFFFFu;
    v[1].x = (float)(x + w);
    v[1].y = (float)(y + h);
    v[1].z = 0.0f;

    sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, v);

    /* Drawn square; outside the curve is painted back in `bg`. */
    if (r > 0) {
        r = corners_clamp_r(w, h, r);
        use_texture(TEX_MASK);
        mask_bind(corner_cut[r]);
        corners_of(x, y, w, h, r, abgr(bg), abgr(bg));
    }
    return 1;
}

/* Frame before cut: cutting first leaves picture at the arc's outer fringe
 * where the PC has pure frame. */
int gfx_blit_card(int x, int y, const unsigned short *src, int tex_w, int tex_h, int w, int h, int r, unsigned edge_from, unsigned edge_to,
                  unsigned bg) {
    if (!g_up || !g_in_frame) return 0;
    if (!gfx_blit_can(src, tex_w, tex_h)) return 0;
    if (w <= 0 || h <= 0 || w > tex_w || h > tex_h) return 0;
    if (r > 0) r = corners_clamp_r(w, h, r);

    if (!gfx_blit_part(x, y, src, tex_w, tex_h, w, h, 0, bg)) return 0;

    if (edge_from == edge_to) {
        gfx_fill(x + r, y, w - 2 * r, 1, edge_from);
        gfx_fill(x + r, y + h - 1, w - 2 * r, 1, edge_from);
        gfx_fill(x, y + r, 1, h - 2 * r, edge_from);
        gfx_fill(x + w - 1, y + r, 1, h - 2 * r, edge_from);
    } else {
        gfx_hgrad(x + r, y, w - 2 * r, 1, edge_from, edge_to);
        gfx_hgrad(x + r, y + h - 1, w - 2 * r, 1, edge_from, edge_to);
        gfx_fill(x, y + r, 1, h - 2 * r, edge_from);
        gfx_fill(x + w - 1, y + r, 1, h - 2 * r, edge_to);
    }
    if (r > 0) {
        use_texture(TEX_MASK);
        mask_bind(corner_band[r]);
        corners_of(x, y, w, h, r, abgr(edge_from), abgr(edge_to));
        mask_bind(corner_cut[r]);
        corners_of(x, y, w, h, r, abgr(bg), abgr(bg));
    }
    return 1;
}

/* Ignored here: without a blank between two swaps the second replaces the
   first's queued buffer, and the next frame draws into the one being
   scanned. Unpaced, the checks went from 34 s to 23 s, but "a caption's scrim
   never reaches opaque" failed at random on a half-drawn frame, and two
   blanks in the capture did not close it. */
void gfx_pace(int on) { (void)on; }

void gfx_frame_end(void) {
    if (!g_up) return;

    /* The two unbounded hardware waits this file is allowed. */
    if (g_in_frame) {
        sceGuFinish();
        sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
        g_in_frame = 0;
    }

    /* Outside the guard: this is the loop's only pacing, so a frame that drew
     * nothing still costs a frame. */
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
    g_swapped = 1;

    g_page ^= 1;
}
