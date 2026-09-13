/* See play_clock.h. */

#include "model/play_clock.h"

#include <string.h>

void play_clock_start(play_clock *c, unsigned long long from, unsigned now_us) {
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->pos     = from;
    c->last_us = now_us;
}

void play_clock_step(play_clock *c, unsigned now_us) {
    unsigned d;

    if (!c) return;

    /* Taken even when stopped, so time paused or rebuffering is not paid back
       on the next step. */
    d          = now_us - c->last_us;
    c->last_us = now_us;

    if (!play_clock_running(c)) return;
    if (d > PLAY_CLOCK_STEP_CAP_US) d = PLAY_CLOCK_STEP_CAP_US;
    c->pos += (unsigned long long)d * (ITEM_TICKS_PER_S / 1000000ull);
}

void play_clock_playing(play_clock *c, int on) {
    if (c) c->playing = on ? 1 : 0;
}

void play_clock_pause(play_clock *c, int on) {
    if (c) c->paused = on ? 1 : 0;
}

unsigned long long play_clock_pos(const play_clock *c) { return c ? c->pos : 0ull; }

int play_clock_running(const play_clock *c) { return c && c->playing && !c->paused; }

void play_clock_follow(play_clock *c, unsigned long long at, unsigned since_us) {
    if (!play_clock_running(c) || since_us > PLAY_CLOCK_SOUND_STALE_US) return;
    if (since_us > PLAY_CLOCK_SOUND_FRAME_US) since_us = PLAY_CLOCK_SOUND_FRAME_US;
    c->pos = at + (unsigned long long)since_us * (ITEM_TICKS_PER_S / 1000000ull);
}
