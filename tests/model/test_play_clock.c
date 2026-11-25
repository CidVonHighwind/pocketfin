/* See model/play_clock.h. Every case here went wrong on the console. The wall
 * clock is the check's own, so none needs a PSP, a film or a server. */

#include "model/play_clock.h"

#include "tools/selftest.h"

#include <stdio.h>
#include <string.h>

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

/* 59.94 refreshes against a clock pinned to a jittery sound stamp: the view
   must hold steady, and 23.976 pictures must fall 2-3-2-3, never on one
   refresh alone -- which is the hitch. */
#define REFRESH_US 16683u
static unsigned jitter_us(unsigned *seed) {
    *seed = *seed * 1103515245u + 12345u;
    return (*seed >> 8) % 8001u; /* 0..8 ms, so +-4 around the truth */
}

static int t_the_view_ignores_the_sounds_jitter(char *note, unsigned n) {
    play_clock c;
    play_view  v;
    unsigned   seed = 7, now = 1000000u, i, worst = 0;

    play_clock_start(&c, 0, now);
    play_clock_playing(&c, 1);
    memset(&v, 0, sizeof(v));
    for (i = 0; i < 600; i++) {
        unsigned long long truth = (unsigned long long)i * REFRESH_US;

        now += REFRESH_US;
        c.pos = (unsigned long long)((long long)truth + 1000000 + (long long)jitter_us(&seed) - 4000) * 10u; /* ticks */
        play_view_step(&v, &c, now);
        if (i > 120) { /* settled */
            long long err = (long long)play_view_pos(&v) / 10 - (long long)truth - 1000000;
            unsigned  e   = (unsigned)(err < 0 ? -err : err);

            if (e > worst) worst = e;
        }
    }
    if (worst > 1500u) {
        snprintf(note, n, "the view strayed %u us from true time under 4 ms of jitter", worst);
        return 1;
    }
    snprintf(note, n, "within %u us of true time", worst);
    return 0;
}

/* 23.976 on 59.94 is 2.5 refreshes a picture: held 2-3-2-3. A picture tipped
   onto the wrong refresh breaks that into 3-3 then 2-2, which is the hitch.
   Every alignment of pictures to refreshes is tried, as the drift between the
   crystals sweeps through them all; the bare clock broke it on 16% of
   pictures under this jitter. */
static int t_pictures_keep_the_2_3_rhythm(char *note, unsigned n) {
    unsigned breaks = 0, shown = 0, phase;

    for (phase = 0; phase < REFRESH_US; phase += 1000u) {
        play_clock         c;
        play_view          v;
        unsigned           seed = 11 + phase, now = 1000000u, i, held = 0, prev = 0;
        unsigned long long next = 10000000ull + phase * 10ull;
        const unsigned long long pic = 10000000ull * 1001u / 24000u; /* 23.976 */

        play_clock_start(&c, 0, now);
        play_clock_playing(&c, 1);
        memset(&v, 0, sizeof(v));
        for (i = 0; i < 1500; i++) {
            long long j = (long long)jitter_us(&seed) - 4000;

            now += REFRESH_US;
            c.pos = (unsigned long long)((long long)i * REFRESH_US + 1000000 + j) * 10u;
            play_view_step(&v, &c, now);
            held++;
            /* As stream.c: a picture at its nearest refresh, one a refresh. */
            if (next <= play_view_pos(&v) + 8333u * 10u) {
                if (i > 300) {
                    shown++;
                    if (held == prev || held < 2 || held > 3) breaks++;
                }
                prev = held;
                held = 0;
                next += pic;
            }
        }
    }
    if (breaks * 100u > shown) {
        snprintf(note, n, "%u of %u pictures broke the 2-3 rhythm", breaks, shown);
        return 1;
    }
    snprintf(note, n, "%u of %u pictures off the rhythm", breaks, shown);
    return 0;
}

/* The sound's crystal runs 0.09% off the wall clock: the view must follow it,
   or pictures slide out of step with the sound over a film. */
static int t_the_view_follows_the_sounds_drift(char *note, unsigned n) {
    play_clock c;
    play_view  v;
    unsigned   now = 1000000u, i;
    long long  err = 0;

    play_clock_start(&c, 0, now);
    play_clock_playing(&c, 1);
    memset(&v, 0, sizeof(v));
    for (i = 0; i < 60u * 60u * 10u; i++) { /* ten minutes */
        now += REFRESH_US;
        c.pos = (unsigned long long)i * REFRESH_US * 10009ull / 1000ull; /* 0.09% fast */
        play_view_step(&v, &c, now);
    }
    err = (long long)(c.pos - play_view_pos(&v)) / 10;
    if (err > 2000 || err < -2000) {
        snprintf(note, n, "after ten minutes the view was %lld us off the sound", err);
        return 1;
    }
    snprintf(note, n, "%lld us behind the sound after ten minutes", err);
    return 0;
}

static int t_the_view_jumps_with_a_seek(char *note, unsigned n) {
    play_clock c;
    play_view  v;
    unsigned   now = 1000000u;

    play_clock_start(&c, 0, now);
    play_clock_playing(&c, 1);
    memset(&v, 0, sizeof(v));
    play_view_step(&v, &c, now);
    now += REFRESH_US;
    c.pos = 600ull * 10000000ull; /* ten minutes on */
    play_view_step(&v, &c, now);
    if (play_view_pos(&v) != c.pos) {
        snprintf(note, n, "after a seek the view was %llu ticks behind", (unsigned long long)(c.pos - play_view_pos(&v)));
        return 1;
    }
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
    selftest_add("clock_film", "the view ignores the sound's jitter", t_the_view_ignores_the_sounds_jitter);
    selftest_add("clock_film", "pictures keep the 2-3 rhythm", t_pictures_keep_the_2_3_rhythm);
    selftest_add("clock_film", "the view follows the sound's drift", t_the_view_follows_the_sounds_drift);
    selftest_add("clock_film", "the view jumps with a seek", t_the_view_jumps_with_a_seek);
}
