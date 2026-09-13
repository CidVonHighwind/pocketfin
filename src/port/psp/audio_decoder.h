/* AAC through sceAudiocodec, and the output channel.
 *
 * The channel is 44,100 Hz only -- the rate-converting one crackles even on a
 * synthesized sine -- and PCM is double-buffered, since the hardware is still
 * reading the buffer just handed over. */
#ifndef PORT_PSP_AUDIO_DECODER_H
#define PORT_PSP_AUDIO_DECODER_H

#include "port/audio.h"

#include <stdint.h>

/* The firmware reads this many bytes of input whatever the frame's size. */
#define AUDIO_IN_BYTES 0x609

#define AUDIO_SAMPLES_PER_FRAME 1024
#define AUDIO_PCM_BYTES         (AUDIO_SAMPLES_PER_FRAME * AUDIO_CHANNELS * 2)

/* Headroom for SBR doubling the output to 2048 samples. */
#define AUDIO_PCM_CAP (AUDIO_PCM_BYTES * 4)

#define AUDIO_FRAME_CAP (8u * 1024u)

typedef struct {
    int     opened;
    int     edram;
    int     channel; /* -1 if none reserved */
    int     pcmidx;
    uint8_t channels;

    /* An underrun is the queue empty as the next buffer arrives: a gap. */
    uint32_t frames;
    int      underruns;
    char     err[96];
} audio_decoder;

/* asc is the esds box's AudioSpecificConfig; rate must be AUDIO_RATE. */
int audio_decoder_open(audio_decoder *a, uint32_t rate, const uint8_t *asc, uint16_t asc_len);

int audio_decoder_decode(audio_decoder *a, const void *frame, uint32_t size, int16_t **out);

/* Blocks until there is room, which is what paces playback. */
int audio_decoder_play(audio_decoder *a, const int16_t *pcm);

/* Not on a merely zeroed struct: channel 0 is real, and open() sets the -1. */
void audio_decoder_close(audio_decoder *a);

#endif
