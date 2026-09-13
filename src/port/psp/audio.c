/* See port/audio.h. The decoder and channel are audio_decoder.c, whose play
 * call blocks on the channel: that is what paces the sound.
 *
 * Audio opens before video, and only then may either decode: both share the
 * Media Engine's EDRAM, and the other order gives video with no chroma. */

#include "port/audio.h"

#include "base/log.h"

#include "audio_decoder.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static audio_decoder g_a;
static int           g_open;
static char          g_err[128];

int         audio_available(void) { return 1; }
const char *audio_error(void) { return g_err; }

void audio_stats(unsigned *frames, unsigned *underruns) {
    if (frames) *frames = g_open ? g_a.frames : 0u;
    if (underruns) *underruns = g_open ? (unsigned)g_a.underruns : 0u;
}

int audio_open(const unsigned char *asc, unsigned asc_len, unsigned rate, unsigned channels) {
    audio_close();
    g_err[0] = 0;

    if (!asc || !asc_len) {
        snprintf(g_err, sizeof(g_err), "no decoder configuration");
        return -1;
    }
    if (audio_decoder_open(&g_a, rate, asc, (unsigned short)asc_len) != 0) {
        snprintf(g_err, sizeof(g_err), "%s", g_a.err);
        return -1;
    }
    (void)channels;
    g_open = 1;
    log_printf("audio: sceAudiocodec, %u Hz", rate);
    return 0;
}

int audio_play(const unsigned char *frame, unsigned len) {
    int16_t *pcm = 0;

    if (!g_open || !frame || !len) return -1;

    /* A bad frame is dropped, a click: stopping left the rest silent. */
    if (audio_decoder_decode(&g_a, frame, len, &pcm) != 0) return -1;
    if (audio_decoder_play(&g_a, pcm) != 0) {
        snprintf(g_err, sizeof(g_err), "%s", g_a.err);
        return -1;
    }
    return 0;
}

void audio_close(void) {
    if (g_open) audio_decoder_close(&g_a);
    g_open = 0;
}
