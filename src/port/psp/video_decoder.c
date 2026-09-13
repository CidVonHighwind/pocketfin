/* See video_decoder.h. */

#include "port/psp/video_decoder.h"

#include <pspkernel.h>
#include <psputils.h>
#include <psputility.h>

#include "avmod.h"
#include <psputility_avmodules.h>

#include <string.h>
#include <stdio.h>

#include "base/log.h"

typedef char video_au_inside_block[(VIDEO_AU_OFFSET < VIDEO_DDRTOP_SIZE) ? 1 : -1];

/* Statics: nothing in a run may allocate. VIDEO_WORK_MAX is a ceiling;
 * sceMpegQueryMemSize is the size, and open() refuses one above it. */
#define VIDEO_PARAM_MAX 64
#define VIDEO_WORK_MAX  0x20000u

/* SPS immediately followed by PPS. */
static uint8_t g_param_sets[VIDEO_PARAM_MAX * 2] __attribute__((aligned(64)));
/* Larger than SceMpegAu: the firmware reads fields it does not name. */
static uint8_t g_au[64] __attribute__((aligned(64)));
static uint8_t g_work[VIDEO_WORK_MAX] __attribute__((aligned(64)));

static void fail(video_decoder *decoder, const char *msg) {
    snprintf(decoder->err, sizeof(decoder->err), "%s", msg);
    log_printf("video decoder: %s", msg);
}
static void failc(video_decoder *decoder, const char *msg, int code) {
    snprintf(decoder->err, sizeof(decoder->err), "%s (0x%08X)", msg, (unsigned)code);
    log_printf("video decoder: %s (0x%08X)", msg, (unsigned)code);
}

void video_decoder_close(video_decoder *decoder) {
    if (decoder->did_create) {
        sceMpegDelete(&decoder->mpeg);
        decoder->did_create = 0;
    }
    if (decoder->did_init) {
        sceMpegFinish();
        decoder->did_init = 0;
    }
}

int video_decoder_open(video_decoder *decoder, const uint8_t *sps, uint16_t sps_len, const uint8_t *pps, uint16_t pps_len,
                       uint8_t nal_prefix_size, void *block, uint32_t block_len) {
    SceMpegAu     *au = (SceMpegAu *)(void *)g_au;
    SceMpegAvcMode pm;
    int            rc, size;

    memset(decoder, 0, sizeof(*decoder));
    decoder->sps_size        = sps_len;
    decoder->pps_size        = pps_len;
    decoder->nal_prefix_size = nal_prefix_size;

    if (!sps || !pps || sps_len == 0 || pps_len == 0) {
        fail(decoder, "missing SPS/PPS");
        return -1;
    }
    if ((unsigned)sps_len + pps_len > sizeof(g_param_sets)) {
        failc(decoder, "parameter sets larger than the buffer; bytes", (int)((unsigned)sps_len + pps_len));
        return -1;
    }

    /* Here: a misaligned block fails sceMpegCreate as a codec problem. */
    if (!block) {
        fail(decoder, "no decoder block");
        return -1;
    }
    if ((uintptr_t)block & (VIDEO_DDRTOP_ALIGN - 1u)) {
        failc(decoder, "decoder block is not 4 MB aligned; address", (int)(uintptr_t)block);
        return -1;
    }
    if (block_len < VIDEO_DDRTOP_SIZE) {
        failc(decoder, "decoder block too small for the requested layout", (int)block_len);
        return -1;
    }

    memcpy(g_param_sets, sps, sps_len);
    memcpy(g_param_sets + sps_len, pps, pps_len);

    /* The Media Engine reads these directly and does not snoop the cache. */
    sceKernelDcacheWritebackRange(g_param_sets, ((unsigned)sps_len + pps_len + 63u) & ~63u);

    av_module_take(PSP_AV_MODULE_AVCODEC);
    av_module_take(PSP_AV_MODULE_MPEGBASE);

    /* A run that ended without video_decoder_close leaves sceMpegInit
     * failing with 0x80618005. */
    rc = sceMpegInit();
    if (rc != 0) {
        sceMpegFinish();
        rc = sceMpegInit();
    }
    if (rc != 0) {
        failc(decoder, "sceMpegInit failed", rc);
        goto fail;
    }
    decoder->did_init = 1;

    size = sceMpegQueryMemSize(VIDEO_MODE_SD);
    if (size <= 0) {
        failc(decoder, "sceMpegQueryMemSize failed", size);
        goto fail;
    }
    if ((unsigned)size > sizeof(g_work)) {
        failc(decoder, "mpeg work memory larger than the buffer; bytes", size);
        goto fail;
    }

    rc = sceMpegCreate(&decoder->mpeg, g_work, size, &decoder->ring, VIDEO_STRIDE, VIDEO_MODE_SD, (SceInt32)(uintptr_t)block);
    if (rc != 0) {
        failc(decoder, "sceMpegCreate failed", rc);
        goto fail;
    }
    decoder->did_create = 1;

    /* 0xFF, not 0, as the reference players do. */
    memset(g_au, 0xFF, sizeof(g_au));

    rc = sceMpegInitAu(&decoder->mpeg, (uint8_t *)block + VIDEO_AU_OFFSET, au);
    if (rc != 0) {
        failc(decoder, "sceMpegInitAu failed", rc);
        goto fail;
    }

    pm.iUnk0        = -1;
    pm.iPixelFormat = SCE_MPEG_AVC_FORMAT_8888;
    rc              = sceMpegAvcDecodeMode(&decoder->mpeg, &pm);
    if (rc != 0) {
        failc(decoder, "sceMpegAvcDecodeMode failed", rc);
        goto fail;
    }
    return 0;

fail:
    video_decoder_close(decoder);
    return -1;
}

int video_decoder_decode(video_decoder *decoder, const void *sample, uint32_t size, void **out_frames, int frame_mode) {
    SceMpegAu        *au = (SceMpegAu *)(void *)g_au;
    video_decoder_nal nal;
    SceInt32          pic_num = 0;
    int               rc;

    if (!decoder->did_create) {
        fail(decoder, "decoder not open");
        return -1;
    }
    if (!sample || size == 0) {
        fail(decoder, "empty sample");
        return -1;
    }

    nal.sps_buffer      = g_param_sets;
    nal.sps_size        = decoder->sps_size;
    nal.pps_buffer      = g_param_sets + decoder->sps_size;
    nal.pps_size        = decoder->pps_size;
    nal.nal_prefix_size = decoder->nal_prefix_size;
    nal.nal_buffer      = (void *)(uintptr_t)sample;
    nal.nal_size        = (int32_t)size;
    nal.mode            = frame_mode;

    /* sceMpegInitAu fills only the ES buffer fields. */
    au->iPtsMSB = 0xFFFFFFFFu;
    au->iPts    = 0xFFFFFFFFu;
    au->iDtsMSB = 0xFFFFFFFFu;
    au->iDts    = 0xFFFFFFFFu;

    /* Rounded outwards: `sample` is only 4-byte aligned, cache lines are 64. */
    {
        uintptr_t from = (uintptr_t)sample & ~(uintptr_t)63;
        uintptr_t to   = ((uintptr_t)sample + size + 63u) & ~(uintptr_t)63;

        sceKernelDcacheWritebackRange((void *)from, (unsigned)(to - from));
    }

    rc = sceMpegGetAvcNalAu(&decoder->mpeg, &nal, au);
    if (rc != 0) {
        failc(decoder, "sceMpegGetAvcNalAu failed", rc);
        return -1;
    }

    rc = sceMpegAvcDecode(&decoder->mpeg, au, VIDEO_STRIDE, out_frames, &pic_num);
    if (rc != 0) {
        failc(decoder, "sceMpegAvcDecode failed", rc);
        return -1;
    }
    return (int)pic_num;
}
