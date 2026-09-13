/* See port/decode.h. The codec is video_decoder.c; this is its block, its
 * buffers in video memory, and the panel while a film runs.
 *
 * Video memory is 2 MB and the film needs 1.7 of it -- one decode buffer and
 * two panel surfaces, all 8888 -- which does not fit beside the browser's
 * buffers, so the film takes them: the decode buffer sits at offset 0, over
 * the two 5-6-5 pages and the depth buffer, which nothing draws into while a
 * film is on.
 *
 * The panel stays 8888 for the length of the film and goes back in 5-6-5 on
 * the way out, or every page after a film draws at the wrong depth into
 * somebody else's buffer. */

#include "port/decode.h"

#include "base/log.h"
#include "port/gfx.h"
#include "port/jpeg.h"
#include "port/media.h"
#include "port/mem.h"

#include "video_decoder.h"

#include <pspdisplay.h>
#include <pspge.h>
#include <pspkernel.h>

#include <stdio.h>
#include <string.h>

#define SURF_STRIDE 512

/* Two, so the panel is never scanning the buffer being composed. */
#define DISPLAY_N 2

#define DEC_BYTES  ((unsigned)(VIDEO_STRIDE * VIDEO_HEIGHT * 4))
#define DISP_BYTES ((unsigned)(SURF_STRIDE * GFX_H * 4))

static video_decoder g_v;
static int           g_open;

static void *g_dec;                /* the engine's target, uncached  */
static void *g_disp[DISPLAY_N];    /* what the panel is shown        */
static void *g_disp_u[DISPLAY_N];  /* the same, uncached             */
static int   g_next;               /* the one being composed         */
static int   g_shown = -1;

static int  g_w, g_h;
static int  g_first; /* the opening frame is decoded in its own mode */
static int  g_panel; /* the surfaces are ours, with or without a codec */
static char g_err[128];

int         decode_available(void) { return 1; }
const char *decode_error(void) { return g_err; }

/* Taken when the film opens, not when the codec does a second or so later
   with the init segment: the surfaces are where the browser's 5-6-5 pages
   are, and taking them at both ends of that second made the band disappear
   and come back. */
int decode_take_panel(void) {
    media_film held;
    int        i;

    if (g_panel) return 0;

    /* The engine, the memory and the panel in one claim (port/media.h):
       taken separately from two threads, they raced posters and wrecked
       browser pages. */
    if (media_film_take(&held) != 0) {
        snprintf(g_err, sizeof(g_err), "the film could not take the media hardware");
        return -1;
    }

    g_dec = held.picture;
    for (i = 0; i < DISPLAY_N && i < held.count; i++) {
        g_disp[i]   = held.panel[i];
        g_disp_u[i] = held.panel_u[i];
    }

    g_next  = 0;
    g_shown = -1;
    g_panel = 1;
    return 0;
}

int decode_open(const unsigned char *sps, unsigned sps_len, const unsigned char *pps, unsigned pps_len, unsigned nal_len_size, int src_w,
                int src_h) {
    void *block;

    /* Only the codec: handing the panel back and taking it again put a black
       frame where the init segment arrives. */
    if (g_open) {
        video_decoder_close(&g_v);
        g_open = 0;
        g_w = g_h = 0;
    }
    g_err[0] = 0;

    if (!sps || !sps_len || !pps || !pps_len || nal_len_size < 1 || nal_len_size > 4) {
        snprintf(g_err, sizeof(g_err), "no parameter sets");
        return -1;
    }
    if (src_w <= 0 || src_h <= 0 || src_w > GFX_W || src_h > GFX_H) {
        snprintf(g_err, sizeof(g_err), "a %dx%d picture does not fit the panel", src_w, src_h);
        return -1;
    }

    /* Before the codec: sceMpegCreate fails while the JPEG unit holds the
       engine, and taking the hardware puts that unit down. */
    if (decode_take_panel() != 0) return -1;

    block = mem_reserve(MEM_DECODER);
    if (!block) {
        snprintf(g_err, sizeof(g_err), "the decoder's 4 MB block was not placed");
        return -1;
    }

    if (video_decoder_open(&g_v, sps, (unsigned short)sps_len, pps, (unsigned short)pps_len, (unsigned char)nal_len_size, block,
                           mem_size(MEM_DECODER)) != 0) {
        snprintf(g_err, sizeof(g_err), "%s", g_v.err);
        return -1;
    }

    g_w      = src_w;
    g_h      = src_h;
    g_first  = 1;
    g_open   = 1;
    log_printf("decode: media engine, %dx%d 8888, video memory %u of %u", g_w, g_h, DEC_BYTES + DISPLAY_N * DISP_BYTES,
               (unsigned)sceGeEdramGetSize());
    return 0;
}

int decode_sample(const unsigned char *data, unsigned len, decode_picture *out) {
    void *dest[VIDEO_MAX_IMAGES];
    int   pics, k;

    if (!g_open || !out) return -1;

    /* One destination four times: an IDR reliably gives three pictures, and
       only the last is shown. */
    for (k = 0; k < VIDEO_MAX_IMAGES; k++) dest[k] = g_dec;

    /* The engine does not snoop the CPU's cache. */
    gfx_wrote_pixels();

    /* Held until a picture comes out: a stream need not begin at a keyframe,
       and spending it before the first IDR decodes the IDR as a later frame. */
    pics = video_decoder_decode(&g_v, data, len, dest, g_first ? VIDEO_FRAME_FIRST : VIDEO_FRAME_NEXT);
    if (pics > 0) g_first = 0;
    if (pics < 0) {
        snprintf(g_err, sizeof(g_err), "%s", g_v.err);
        return -1;
    }
    if (pics == 0) return 0;

    out->w = g_w;
    out->h = g_h;
    return 1;
}

void *decode_surface(int *stride) {
    if (!g_panel) return 0;
    if (stride) *stride = SURF_STRIDE;
    return g_disp_u[g_next];
}

void decode_blit(int x, int y) {
    if (!g_open || g_w <= 0 || g_h <= 0) return;

    /* The surface is never cleared, so a letterboxed film (480x250 in 480x272)
       clears its margins or keeps strips of the band in the bars. */
    if (y > 0) gfx_fill(0, 0, GFX_W, y, 0);
    if (y + g_h < GFX_H) gfx_fill(0, y + g_h, GFX_W, GFX_H - (y + g_h), 0);
    if (x > 0) gfx_fill(0, y, x, g_h, 0);
    if (x + g_w < GFX_W) gfx_fill(x + g_w, y, GFX_W - (x + g_w), g_h, 0);

    gfx_copy8888(g_dec, VIDEO_STRIDE, 0, 0, g_disp_u[g_next], SURF_STRIDE, x, y, g_w, g_h);
}

void decode_show(void) {
    if (!g_panel) return;
    /* NEXTFRAME: IMMEDIATE after a WaitVblankStart wrecked the picture, the
       panel drawing 8888 at the wrong depth. */
    sceDisplaySetFrameBuf(g_disp[g_next], SURF_STRIDE, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);

    g_shown = g_next;
    g_next  = (g_next + 1) % DISPLAY_N;
}

void decode_close(void) {
    if (!g_open && !g_panel) return;
    if (g_open) video_decoder_close(&g_v);
    g_open  = 0;
    g_panel = 0;
    g_w = g_h = 0;

    media_film_give();
}
