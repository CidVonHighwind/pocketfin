/* 44,100 Hz stereo only: the console's rate-converting output channel
 * crackles on a synthesised sine. */
#ifndef PORT_AUDIO_H
#define PORT_AUDIO_H

#define AUDIO_RATE     44100u
#define AUDIO_CHANNELS 2

int audio_available(void);

/* `asc` is the stream's AudioSpecificConfig, two to five bytes out of the
 * esds box. 0 on success; audio_error() says why not. */
int audio_open(const unsigned char *asc, unsigned asc_len, unsigned rate, unsigned channels);

/* Blocks until the hardware has room, which is what paces playback. -1 when
 * the frame was refused, which is one frame of silence, not the end of the
 * film. */
int audio_play(const unsigned char *frame, unsigned len);

/* An underrun is an audible gap. */
void audio_stats(unsigned *frames, unsigned *underruns);

void        audio_close(void);
const char *audio_error(void);

#endif
