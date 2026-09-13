/* The Media Engine's JPEG unit; port/jpeg.h is the contract.
 *
 * sceJpegCreateMJpeg takes the frame size up front, padded to whole
 * macroblocks, and sceJpegDecodeMJpeg refuses anything else, so the header is
 * parsed for it first (base/jpeg_header.c). Neither entry point scales and
 * output is RGBA8888 only, so the pack to 5-6-5 rides the resize. Progressive
 * is refused from the header rather than left to fail inside the firmware.
 *
 * Nothing here allocates: memalign() per poster fails on a fragmented heap,
 * so everything comes out of the caller's scratch. */

#include "port/jpeg.h"

#include <pspkernel.h>
#include <psputility.h>

#include "avmod.h"
#include <psputility_avmodules.h>
#include <pspjpeg.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_ready;

/* No lock: callers hold the engine (port/media.h) across the whole
   init/decode/finish, and a lock here could only cover single calls. */

int jpeg_init(void) {
    int rc;

    if (g_ready) return 0;

    /* The AV module first, or sceJpegInitMJpeg answers 0x8002013A. */
    av_module_take(PSP_AV_MODULE_AVCODEC);

    rc = sceJpegInitMJpeg();
    if (rc < 0) {
        jpeg_failc("sceJpegInitMJpeg failed", rc);
        return JPEG_ERR_STATE;
    }

    g_ready = 1;
    jpeg_err_clear();
    return 0;
}

void jpeg_finish(void) {
    if (!g_ready) return;
    sceJpegFinishMJpeg();
    g_ready = 0;
}

static unsigned char *align64(unsigned char *p) { return (unsigned char *)(((uintptr_t)p + 63u) & ~(uintptr_t)63); }

int jpeg_decode_rgb565(const void *jpg, unsigned jpg_len, void *out565, int out_w, int out_h, int *got_w, int *got_h, void *scratch,
                       unsigned scratch_len) {
    unsigned char *rgba, *in;
    unsigned       want;
    int            source_width = 0, source_height = 0, rc;

    jpeg_err_clear();

    if (!jpg || !out565 || !scratch || jpg_len < 4 || out_w <= 0 || out_h <= 0) {
        jpeg_fail("bad arguments");
        return JPEG_ERR_ARGS;
    }
    if (!g_ready) {
        jpeg_fail("jpeg_init not called");
        return JPEG_ERR_STATE;
    }

    rc = jpeg_probe(jpg, jpg_len, &source_width, &source_height);
    if (rc != 0) return rc;

    /* Padded to whole 16x16 MCUs: the decoder writes the padding. */
    want = JPEG_SCRATCH_BYTES(source_width, source_height);
    if (scratch_len < want) {
        jpeg_failf("%dx%d needs %u bytes of scratch, given %u", source_width, source_height, want, scratch_len);
        return JPEG_ERR_SCRATCH;
    }

    rgba = align64((unsigned char *)scratch);

    /* Per image: the context is sized to one frame. */
    rc = sceJpegCreateMJpeg(JPEG_MB_UP(source_width), JPEG_MB_UP(source_height));
    if (rc < 0) {
        jpeg_failc("sceJpegCreateMJpeg failed", rc);
        return JPEG_ERR_DECODE;
    }

    in = (unsigned char *)(uintptr_t)jpg;
    sceKernelDcacheWritebackRange(in, jpg_len);

    rc = sceJpegDecodeMJpeg(in, jpg_len, rgba, 0);
    sceJpegDeleteMJpeg();
    if (rc < 0) {
        jpeg_failc("sceJpegDecodeMJpeg failed", rc);
        return JPEG_ERR_DECODE;
    }

    /* rc is (width << 16) | height as decoded, trusted over the header. */
    if ((rc >> 16) > 0 && (rc & 0xFFFF) > 0) {
        int rw = rc >> 16, rh = rc & 0xFFFF;
        if (rw <= source_width && rh <= source_height) {
            source_width  = rw;
            source_height = rh;
        }
    }

    sceKernelDcacheWritebackInvalidateAll();

    jpeg_shrink_565(rgba, JPEG_MB_UP(source_width), source_width, source_height, out565, out_w, out_h, got_w, got_h);
    return 0;
}
