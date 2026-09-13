/* See base/standby.h. There is no reset: every check reads the epoch before and
 * after, and leaves the machine back in UP. */

#include "base/standby.h"

#include "tools/selftest.h"

#include <stdio.h>

static int n_release, n_acquire, n_verify;
static int rc_acquire, rc_verify;

static void fake_release(void) { n_release++; }
static int  fake_acquire(void) {
    n_acquire++;
    return rc_acquire;
}
static int fake_verify(void) {
    n_verify++;
    return rc_verify;
}

/* A check that fails partway leaves the machine mid-cycle, and without this
 * the next one reddens too. */
static void arm(void) {
    int i;

    rc_acquire = rc_verify = 0;
    standby_watch(fake_release, fake_acquire, fake_verify);

    standby_note_back();
    /* One step unconditionally: standby_step() CONSUMES a note, and one left
       pending lands on whichever check runs next. */
    standby_step();
    for (i = 0; i < 4 && standby_state() != STANDBY_UP; i++) standby_step();

    n_release = n_acquire = n_verify = 0;
}

/* The middle assertion catches an epoch bumped on the way down. */
static int t_a_cycle_advances_the_epoch_once(char *note, unsigned n) {
    unsigned e0;

    arm();
    e0 = standby_epoch();

    standby_note_going();
    standby_step();
    if (standby_state() != STANDBY_DOWN) {
        snprintf(note, n, "one step after going left it in %s, not down", standby_text(standby_state()));
        return 1;
    }
    if (n_release != 1) {
        snprintf(note, n, "release ran %d times going down, not once", n_release);
        return 1;
    }

    standby_note_back();
    standby_step();
    if (standby_epoch() != e0) {
        snprintf(note, n, "the epoch reached %u before anything was observed", standby_epoch());
        return 1;
    }

    standby_step();
    if (standby_state() != STANDBY_UP || standby_epoch() != e0 + 1) {
        snprintf(note, n, "came back %s in epoch %u, wanted up in %u", standby_text(standby_state()), standby_epoch(), e0 + 1);
        return 1;
    }

    standby_step();
    standby_step();
    if (standby_epoch() != e0 + 1) {
        snprintf(note, n, "the epoch kept climbing to %u while up", standby_epoch());
        return 1;
    }

    snprintf(note, n, "epoch %u -> %u, 1 release 1 acquire", e0, standby_epoch());
    return 0;
}

/* RESUME_COMPLETE is not evidence: the epoch waits for the verify. */
static int t_the_epoch_waits_for_the_observation(char *note, unsigned n) {
    unsigned e0;
    int      i;

    arm();
    rc_verify = -1;
    e0        = standby_epoch();

    standby_note_going();
    standby_step();
    standby_note_back();
    standby_step();

    for (i = 0; i < 10; i++) standby_step();

    if (standby_epoch() != e0) {
        snprintf(note, n, "the epoch reached %u with every verify refusing", standby_epoch());
        return 1;
    }
    if (standby_state() != STANDBY_RESTORING) {
        snprintf(note, n, "it called itself %s while nothing had come back", standby_text(standby_state()));
        return 1;
    }
    if (n_acquire != 1) {
        snprintf(note, n, "acquire ran %d times over %d verifies, not once", n_acquire, n_verify);
        return 1;
    }

    rc_verify = 0;
    standby_step();
    if (standby_state() != STANDBY_UP || standby_epoch() != e0 + 1) {
        snprintf(note, n, "the passing verify left it %s in epoch %u", standby_text(standby_state()), standby_epoch());
        return 1;
    }

    snprintf(note, n, "%d verifies refused, 1 acquire, then epoch %u", n_verify - 1, standby_epoch());
    return 0;
}

/* Seen on the console: callbacks arrive while up and nothing was lost. */
static int t_a_resume_with_nothing_lost_moves_nothing(char *note, unsigned n) {
    unsigned e0;

    arm();
    e0 = standby_epoch();

    standby_note_back();
    standby_step();
    standby_step();

    if (standby_epoch() != e0 || standby_state() != STANDBY_UP) {
        snprintf(note, n, "a bare resume left it %s in epoch %u, was up in %u", standby_text(standby_state()), standby_epoch(), e0);
        return 1;
    }
    if (n_release != 0 || n_acquire != 0) {
        snprintf(note, n, "a bare resume ran %d releases and %d acquires", n_release, n_acquire);
        return 1;
    }

    snprintf(note, n, "epoch stayed %u, the resource was not touched", e0);
    return 0;
}

static int t_a_refused_acquire_is_tried_again(char *note, unsigned n) {
    unsigned e0;
    int      i;

    arm();
    rc_acquire = -1;
    e0         = standby_epoch();

    standby_note_going();
    standby_step();
    standby_note_back();
    standby_step();

    for (i = 0; i < 5; i++) standby_step();

    if (n_acquire < 5) {
        snprintf(note, n, "acquire was tried %d times in 5 passes", n_acquire);
        return 1;
    }
    if (n_verify != 0) {
        snprintf(note, n, "verify ran %d times though nothing was acquired", n_verify);
        return 1;
    }

    rc_acquire = 0;
    standby_step();
    if (standby_state() != STANDBY_UP || standby_epoch() != e0 + 1) {
        snprintf(note, n, "after the acquire took it was %s in epoch %u", standby_text(standby_state()), standby_epoch());
        return 1;
    }

    snprintf(note, n, "refused %d times, then up in epoch %u", n_acquire - 1, standby_epoch());
    return 0;
}

/* One standby raises SUSPENDING and STANDBY both (shell/psp/main.c). */
static int t_the_firmware_edges_are_one_standby(char *note, unsigned n) {
    unsigned e0;
    int      i;

    arm();
    e0 = standby_epoch();

    /* Stepped between: two notes before one step are one flag. */
    standby_note_going(); /* SUSPENDING */
    standby_step();
    standby_note_going(); /* STANDBY, the same standby, a frame later */
    standby_step();

    standby_step();
    if (standby_state() != STANDBY_DOWN) {
        snprintf(note, n, "with nothing back it moved to %s", standby_text(standby_state()));
        return 1;
    }
    if (n_release != 1) {
        snprintf(note, n, "two going edges released %d times, not once", n_release);
        return 1;
    }
    if (standby_epoch() != e0) {
        snprintf(note, n, "the epoch reached %u before anything came back", standby_epoch());
        return 1;
    }

    standby_note_back();
    for (i = 0; i < 3 && standby_state() != STANDBY_UP; i++) standby_step();

    if (standby_state() != STANDBY_UP || standby_epoch() != e0 + 1 || n_acquire != 1) {
        snprintf(note, n, "came back %s in epoch %u after %d acquires", standby_text(standby_state()), standby_epoch(), n_acquire);
        return 1;
    }

    snprintf(note, n, "2 going = 1 release, 1 acquire, epoch %u", standby_epoch());
    return 0;
}

void test_standby_register(void) {
    selftest_add("standby", "a cycle advances the epoch once", t_a_cycle_advances_the_epoch_once);
    selftest_add("standby", "the epoch waits for the observation", t_the_epoch_waits_for_the_observation);
    selftest_add("standby", "a resume with nothing lost moves nothing", t_a_resume_with_nothing_lost_moves_nothing);
    selftest_add("standby", "a refused acquire is tried again", t_a_refused_acquire_is_tried_again);
    selftest_add("standby", "the firmware edges are one standby", t_the_firmware_edges_are_one_standby);
}
