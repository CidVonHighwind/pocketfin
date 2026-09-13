/* The film position, advanced by wall time only while playing.
 *
 * Accumulated (add the capped gap since the last step), not anchored
 * (`start + now - anchor`), because the console sleeps: an anchored clock
 * would dump every buffered picture and the whole sound ring after a standby.
 * Accumulating turns a sleep into one capped step. */
#ifndef MODEL_PLAY_CLOCK_H
#define MODEL_PLAY_CLOCK_H

#include "jelly/item.h"

/* Larger than any frame the drawing loop takes (16.7 ms at 60, and a slow
 * frame is not a fault), far smaller than a standby. */
#define PLAY_CLOCK_STEP_CAP_US 100000u

typedef struct {
    unsigned long long pos; /* ticks                           */
    unsigned           last_us;
    int                playing; /* enough is buffered to run       */
    int                paused;  /* the viewer stopped it           */
} play_clock;

/* Starts stopped. */
void play_clock_start(play_clock *c, unsigned long long from, unsigned now_us);

/* Once a frame. */
void play_clock_step(play_clock *c, unsigned now_us);

void play_clock_playing(play_clock *c, int on);

/* Separate from playing: a film can be paused and unbuffered, and must not
 * run when only one of them clears. */
void play_clock_pause(play_clock *c, int on);

unsigned long long play_clock_pos(const play_clock *c);

int play_clock_running(const play_clock *c);

/* One AAC frame at 44,100 Hz, 1024 samples. */
#define PLAY_CLOCK_SOUND_FRAME_US 23220u

/* Longer than this since the hardware took a frame and the wall clock carries
 * the film instead. */
#define PLAY_CLOCK_SOUND_STALE_US 100000u

/* THE SOUND IS THE CLOCK. `at` is the film position of the frame the hardware
 * took `since_us` ago. Its crystal and the wall clock run 0.09% apart
 * (measured), and steering a wall-time position toward the sound cracked.
 * Wall time only fills the gap between two frames, never past the end of the
 * one playing. Ignored while stopped, or once the sound is stale. */
void play_clock_follow(play_clock *c, unsigned long long at, unsigned since_us);

#endif
