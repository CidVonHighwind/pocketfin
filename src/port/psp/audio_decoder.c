/* See audio_decoder.h. */

#include "port/psp/audio_decoder.h"

#include <pspkernel.h>
#include <psputils.h>
#include <psputility.h>

#include "avmod.h"
#include <psputility_avmodules.h>
#include <pspaudio.h>
#include <pspaudiocodec.h>

#include <string.h>
#include <stdio.h>

#include "base/log.h"

#define CTX_ERR       (0x08 / 4)
#define CTX_IN_BUF    (0x18 / 4)
#define CTX_IN_SIZE   (0x1C / 4)
#define CTX_OUT_BUF   (0x20 / 4)
#define CTX_OUT_BYTES (0x24 / 4)
#define CTX_RATE      (0x28 / 4)

/* The context is addressed well past its declared fields. */
#define CTX_WORDS 128

/* Statics: nothing in a run may allocate. The Media Engine reads and writes
 * all three directly. */
static unsigned long g_ctx[CTX_WORDS] __attribute__((aligned(64)));
static uint8_t       g_frame[AUDIO_FRAME_CAP] __attribute__((aligned(64)));
static int16_t       g_pcm[2][AUDIO_PCM_CAP / 2] __attribute__((aligned(64)));

static void fail(audio_decoder *decoder, const char *msg) {
    snprintf(decoder->err, sizeof(decoder->err), "%s", msg);
    log_printf("audio decoder: %s", msg);
}

static void failc(audio_decoder *decoder, const char *msg, int code) {
    snprintf(decoder->err, sizeof(decoder->err), "%s (0x%08X)", msg, (unsigned)code);
    log_printf("audio decoder: %s (0x%08X)", msg, (unsigned)code);
}

/* Validated, not stored: the codec reads the configuration itself. */
static int parse_asc(audio_decoder *decoder, const uint8_t *asc, uint16_t asc_len) {
    uint8_t obj, freq_idx, chans;

    if (!asc || asc_len < 2) {
        fail(decoder, "no AudioSpecificConfig");
        return -1;
    }

    obj      = (uint8_t)(asc[0] >> 3);
    freq_idx = (uint8_t)(((asc[0] & 0x07) << 1) | (asc[1] >> 7));
    chans    = (uint8_t)((asc[1] >> 3) & 0x0F);

    if (obj < 1 || obj > 4) {
        fail(decoder, "unsupported AAC object type");
        return -1;
    }
    if (freq_idx > 12) {
        fail(decoder, "bad sample rate index");
        return -1;
    }
    if (chans < 1 || chans > 7) {
        fail(decoder, "bad channel config");
        return -1;
    }

    decoder->channels = chans;
    return 0;
}

int audio_decoder_open(audio_decoder *decoder, uint32_t rate, const uint8_t *asc, uint16_t asc_len) {
    int rc;

    memset(decoder, 0, sizeof(*decoder));
    decoder->channel = -1;

    if (rate != AUDIO_RATE) {
        snprintf(decoder->err, sizeof(decoder->err), "%u Hz refused: the output channel is %u Hz only", (unsigned)rate,
                 (unsigned)AUDIO_RATE);
        log_printf("audio decoder: %s", decoder->err);
        return -1;
    }

    if (parse_asc(decoder, asc, asc_len) != 0) return -1;

    memset(g_ctx, 0, sizeof(g_ctx));
    memset(g_pcm, 0, sizeof(g_pcm));

    av_module_take(PSP_AV_MODULE_AVCODEC);
    av_module_take(PSP_AV_MODULE_AAC);

    g_ctx[CTX_RATE] = rate;

    rc = sceAudiocodecCheckNeedMem(g_ctx, PSP_CODEC_AAC);
    if (rc < 0) {
        failc(decoder, "CheckNeedMem failed", rc);
        goto fail;
    }

    rc = sceAudiocodecGetEDRAM(g_ctx, PSP_CODEC_AAC);
    if (rc < 0) {
        failc(decoder, "GetEDRAM failed", rc);
        goto fail;
    }
    decoder->edram = 1;

    rc = sceAudiocodecInit(g_ctx, PSP_CODEC_AAC);
    if (rc < 0) {
        failc(decoder, "AudiocodecInit failed", rc);
        goto fail;
    }
    decoder->opened = 1;

    decoder->channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, AUDIO_SAMPLES_PER_FRAME, PSP_AUDIO_FORMAT_STEREO);
    if (decoder->channel < 0) {
        failc(decoder, "ChReserve failed", decoder->channel);
        goto fail;
    }

    /* Mono is logged, not refused: only stereo has been measured here. */
    log_printf("audio decoder: open, %u ch, %u Hz", (unsigned)decoder->channels, (unsigned)rate);
    return 0;

fail:
    audio_decoder_close(decoder);
    return -1;
}

int audio_decoder_decode(audio_decoder *decoder, const void *frame, uint32_t size, int16_t **out) {
    int16_t *pcm;
    int      rc;

    if (!decoder->opened) {
        fail(decoder, "decoder not open");
        return -1;
    }
    if (!frame || size == 0) {
        fail(decoder, "empty frame");
        return -1;
    }
    if (size > AUDIO_FRAME_CAP) {
        fail(decoder, "frame larger than buffer");
        return -1;
    }

    /* Swap before decoding: the buffer from last time may still be playing. */
    decoder->pcmidx = !decoder->pcmidx;
    pcm             = g_pcm[decoder->pcmidx];

    memset(g_frame, 0, AUDIO_IN_BYTES);
    memcpy(g_frame, frame, size);
    if (size < AUDIO_IN_BYTES) size = AUDIO_IN_BYTES;

    /* The Media Engine reads this directly and does not snoop the cache. */
    sceKernelDcacheWritebackRange(g_frame, size);

    g_ctx[CTX_IN_BUF]    = (unsigned long)(uintptr_t)g_frame;
    g_ctx[CTX_IN_SIZE]   = size;
    g_ctx[CTX_OUT_BUF]   = (unsigned long)(uintptr_t)pcm;
    g_ctx[CTX_ERR]       = 0;
    g_ctx[CTX_OUT_BYTES] = AUDIO_PCM_BYTES;

    memset(pcm, 0, AUDIO_PCM_CAP);

    rc = sceAudiocodecDecode(g_ctx, PSP_CODEC_AAC);
    if (rc < 0 || g_ctx[CTX_ERR] != 0) {
        if (rc < 0)
            failc(decoder, "AudiocodecDecode failed", rc);
        else
            failc(decoder, "decoder reported error", (int)g_ctx[CTX_ERR]);
        return -1;
    }

    sceKernelDcacheInvalidateRange(pcm, AUDIO_PCM_CAP);

    decoder->frames++;

    if (out) *out = pcm;
    return 0;
}

int audio_decoder_play(audio_decoder *decoder, const int16_t *pcm) {
    int rc;

    if (decoder->channel < 0) {
        fail(decoder, "no audio channel");
        return -1;
    }

    if (sceAudioGetChannelRestLen(decoder->channel) == 0) decoder->underruns++;

    rc = sceAudioOutputBlocking(decoder->channel, PSP_AUDIO_VOLUME_MAX, (void *)(uintptr_t)pcm);
    if (rc < 0) {
        failc(decoder, "AudioOutputBlocking failed", rc);
        return -1;
    }
    return 0;
}

void audio_decoder_close(audio_decoder *decoder) {
    if (decoder->channel >= 0) {
        sceAudioChRelease(decoder->channel);
        decoder->channel = -1;
    }
    if (decoder->edram) {
        sceAudiocodecReleaseEDRAM(g_ctx);
        decoder->edram = 0;
    }
    decoder->opened = 0;
}
