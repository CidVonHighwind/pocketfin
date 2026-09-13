/* See model/play_clock.h. Every case here went wrong on the console. The wall
 * clock is the check's own, so none needs a PSP, a film or a server. */

#include "model/play_clock.h"

#include "tools/selftest.h"

#include <stdio.h>

#define MS(n)       ((unsigned)(n) * 1000u)
#define TICKS_MS(n) ((unsigned long long)(n) * (ITEM_TICKS_PER_S / 1000ull))

static void run(play_clock *c, unsigned *now, unsigned ms, unsigned step) {
    unsigned end = *now + MS(ms);

    while (*now < end) {
        *now += MS(step);
        play_clock_step(c, *now);
    }
}

/* A film opens with an empty ring: a clock running from the open is seconds
   ahead of the first picture before it arrives. */
static int t_it_starts_stopped(char *note, unsigned n) {
    play_clock c;
    unsigned   now = 1000000u;

    play_clock_start(&c, 0, now);
    run(&c, &now, 500, 16);

    if (play_clock_pos(&c) != 0) {
        snprintf(note, n, "half a second of wall time moved it to %llu", play_clock_pos(&c));
        return 1;
    }
    snprintf(note, n, "still at 0 after 500 ms with nothing buffered");
    return 0;
}

static int t_playing_it_keeps_real_time(char *note, unsigned n) {
    play_clock         c;
    unsigned           now = 0;
    unsigned long long want = TICKS_MS(1000);

    play_clock_start(&c, 0, now);
    play_clock_playing(&c, 1);
    run(&c, &now, 1000, 16);

    if (play_clock_pos(&c) + TICKS_MS(20) < want || play_clock_pos(&c) > want + TICKS_MS(20)) {
        snprintf(note, n, "a second of wall time moved it %llu ms", play_clock_pos(&c) / (ITEM_TICKS_PER_S / 1000ull));
        return 1;
    }
    snprintf(note, n, "1000 ms of wall time is %llu ms of film", play_clock_pos(&c) / (ITEM_TICKS_PER_S / 1000ull));
    return 0;
}

/* Wall time runs through a standby: an anchored clock leaps two minutes, every
   buffered picture overdue at once and the sound bursting its ring, neither
   ever catching up. */
static int t_a_sleep_moves_it_one_step(char *note, unsigned n) {
    play_clock         c;
    unsigned           now = 0;
    unsigned long long after;

    play_clock_start(&c, 0, now);
    play_clock_playing(&c, 1);
    run(&c, &now, 200, 16);

    now += MS(120000);
    play_clock_step(&c, now);
    after = play_clock_pos(&c);

    if (after > TICKS_MS(200 + 120)) {
        snprintf(note, n, "two minutes asleep moved the film %llu ms", after / (ITEM_TICKS_PER_S / 1000ull));
        return 1;
    }
    snprintf(note, n, "two minutes asleep moved it %llu ms, the cap", after / (ITEM_TICKS_PER_S / 1000ull) - 200);
    return 0;
}

static int t_a_stall_does_not_advance_it(char *note, unsigned n) {
    play_clock         c;
    unsigned           now = 0;
    unsigned long long at_stall;

    play_clock_start(&c, 0, now);
    play_clock_playing(&c, 1);
    run(&c, &now, 1000, 16);
    at_stall = play_clock_pos(&c);

    play_clock_playing(&c, 0); /* the ring ran dry */
    run(&c, &now, 3000, 16);

    if (play_clock_pos(&c) != at_stall) {
        snprintf(note, n, "three seconds of buffering moved it %llu ms", (play_clock_pos(&c) - at_stall) / (ITEM_TICKS_PER_S / 1000ull));
        return 1;
    }

    play_clock_playing(&c, 1);
    run(&c, &now, 500, 16);
    if (play_clock_pos(&c) <= at_stall) {
        snprintf(note, n, "it never started again");
        return 1;
    }
    snprintf(note, n, "unmoved for three seconds, and running again after");
    return 0;
}

/* Press play while it is still buffering and an anchored clock runs away. */
static int t_pause_and_buffering_are_independent(char *note, unsigned n) {
    play_clock         c;
    unsigned           now = 0;
    unsigned long long held;

    play_clock_start(&c, 0, now);
    play_clock_playing(&c, 1);
    run(&c, &now, 500, 16);

    play_clock_pause(&c, 1);
    play_clock_playing(&c, 0);
    held = play_clock_pos(&c);
    run(&c, &now, 1000, 16);

    play_clock_pause(&c, 0);
    run(&c, &now, 1000, 16);
    if (play_clock_pos(&c) != held) {
        snprintf(note, n, "un-pausing an unbuffered film moved it %llu ms", (play_clock_pos(&c) - held) / (ITEM_TICKS_PER_S / 1000ull));
        return 1;
    }

    play_clock_playing(&c, 1);
    run(&c, &now, 200, 16);
    if (play_clock_pos(&c) <= held) {
        snprintf(note, n, "it did not run once both cleared");
        return 1;
    }
    snprintf(note, n, "both must clear before it runs");
    return 0;
}

static int t_it_starts_where_the_film_was_resumed(char *note, unsigned n) {
    play_clock c;
    unsigned   now = 0;

    play_clock_start(&c, TICKS_MS(600000), now);
    play_clock_playing(&c, 1);
    run(&c, &now, 1000, 16);

    if (play_clock_pos(&c) < TICKS_MS(600000)) {
        snprintf(note, n, "it started at %llu ms, before the resume point", play_clock_pos(&c) / (ITEM_TICKS_PER_S / 1000ull));
        return 1;
    }
    snprintf(note, n, "resumed at ten minutes and ran on from there");
    return 0;
}

/* This console's sound crystal measured 0.09% slow against the wall: kept from
   wall time, ten minutes of 60 Hz frames is 540 ms out. */
static int t_it_does_not_drift_from_the_sound(char *note, unsigned n) {
    const unsigned long long frame = (unsigned long long)PLAY_CLOCK_SOUND_FRAME_US * (ITEM_TICKS_PER_S / 1000000ull);
    play_clock               c;
    unsigned                 now = 0, i;
    unsigned long long       heard = 0;
    long long                gap;

    play_clock_start(&c, 0, now);
    play_clock_playing(&c, 1);
    for (i = 0; i < 36000u; i++) {
        unsigned long long at;

        now += 16667u;
        heard = (unsigned long long)now * 9991ull / 10000ull * (ITEM_TICKS_PER_S / 1000000ull);
        at    = heard / frame * frame;
        play_clock_step(&c, now);
        play_clock_follow(&c, at, (unsigned)((heard - at) / (ITEM_TICKS_PER_S / 1000000ull)));
    }

    gap = (long long)play_clock_pos(&c) - (long long)heard;
    if (gap > (long long)frame || gap < -(long long)frame) {
        snprintf(note, n, "ten minutes in it is %lld ms from the sound", gap / (long long)(ITEM_TICKS_PER_S / 1000ull));
        return 1;
    }
    snprintf(note, n, "ten minutes in, %lld us from the sound", gap / 10);
    return 0;
}

/* Never past the end of the frame playing, so a picture cannot run ahead of
   what is heard; the wall clock again once the sound stops feeding, so a dry
   ring does not freeze the picture; nothing at all while stopped. */
static int t_it_follows_the_sound_only_while_it_plays(char *note, unsigned n) {
    play_clock         c;
    unsigned           now = 0;
    unsigned long long held;

    play_clock_start(&c, 0, now);
    play_clock_playing(&c, 1);
    run(&c, &now, 1000, 16);

    play_clock_follow(&c, TICKS_MS(900), MS(60));
    held = TICKS_MS(900) + (unsigned long long)PLAY_CLOCK_SOUND_FRAME_US * (ITEM_TICKS_PER_S / 1000000ull);
    if (play_clock_pos(&c) != held) {
        snprintf(note, n, "60 ms after a frame it is at %llu ms, past the frame", play_clock_pos(&c) / (ITEM_TICKS_PER_S / 1000ull));
        return 1;
    }

    play_clock_follow(&c, TICKS_MS(100), PLAY_CLOCK_SOUND_STALE_US + 1u);
    if (play_clock_pos(&c) != held) {
        snprintf(note, n, "a sound silent past the bound still moved it");
        return 1;
    }

    play_clock_playing(&c, 0);
    play_clock_follow(&c, TICKS_MS(100), 0);
    if (play_clock_pos(&c) != held) {
        snprintf(note, n, "a stopped film followed the sound");
        return 1;
    }
    snprintf(note, n, "held at the frame's end, let go when silent, ignored when stopped");
    return 0;
}

void test_play_clock_register(void) {
    selftest_add("clock_film", "it starts stopped", t_it_starts_stopped);
    selftest_add("clock_film", "playing it keeps real time", t_playing_it_keeps_real_time);
    selftest_add("clock_film", "a sleep moves it one step", t_a_sleep_moves_it_one_step);
    selftest_add("clock_film", "a stall does not advance it", t_a_stall_does_not_advance_it);
    selftest_add("clock_film", "pause and buffering are independent", t_pause_and_buffering_are_independent);
    selftest_add("clock_film", "it starts where the film was resumed", t_it_starts_where_the_film_was_resumed);
    selftest_add("clock_film", "it does not drift from the sound", t_it_does_not_drift_from_the_sound);
    selftest_add("clock_film", "it follows the sound only while it plays", t_it_follows_the_sound_only_while_it_plays);
}
