/* See port/platform.h. */

#include "port/platform.h"

#include "tools/selftest.h"

#include <stdio.h>

static int t_the_clock_moves_forward(char *note, unsigned n) {
    unsigned a = platform_clock_us(), b;

    platform_sleep_us(20000);
    b = platform_clock_us();

    /* A difference is correct across the ~71 minute wrap; b > a is not. */
    if (b - a < 15000u || b - a > 200000u) {
        snprintf(note, n, "a 20 ms sleep measured %u us", b - a);
        return 1;
    }
    snprintf(note, n, "20 ms measured %u us", b - a);
    return 0;
}

static int t_a_difference_survives_the_wrap(char *note, unsigned n) {
    unsigned before = 0xFFFFFF00u, after = 0x00000100u;

    if (after - before != 0x200u) {
        snprintf(note, n, "across the wrap the difference read %u", after - before);
        return 1;
    }
    if (after > before) {
        snprintf(note, n, "comparing absolutes says the later time is earlier, which is why nothing does");
        return 1;
    }
    snprintf(note, n, "0x%08x -> 0x%08x is %u us apart", before, after, after - before);
    return 0;
}

#define BUMPS 4000

static platform_lock *g_lock;
static volatile int   g_counter;
static volatile int   g_helper_done;

static int helper(void *arg) {
    int i;

    (void)arg;
    for (i = 0; i < BUMPS; i++) {
        platform_lock_take(g_lock);
        g_counter = g_counter + 1;
        platform_lock_give(g_lock);
    }
    g_helper_done = 1;
    return 0;
}

static int t_a_lock_excludes(char *note, unsigned n) {
    unsigned waited;
    int      i;

    g_lock = platform_lock_new("check");
    if (!g_lock) {
        snprintf(note, n, "no lock could be made");
        return 1;
    }
    g_counter     = 0;
    g_helper_done = 0;

    if (platform_thread_start("checkhelper", helper, NULL) != 0) {
        snprintf(note, n, "no thread could be started");
        return 1;
    }
    for (i = 0; i < BUMPS; i++) {
        platform_lock_take(g_lock);
        g_counter = g_counter + 1;
        platform_lock_give(g_lock);
    }

    waited = platform_clock_us();
    while (!g_helper_done) {
        if (platform_clock_us() - waited > 5000000u) {
            snprintf(note, n, "the helper never finished");
            return 1;
        }
        platform_sleep_us(1000);
    }

    if (g_counter != BUMPS * 2) {
        snprintf(note, n, "%d bumps of %d -- the lock did not exclude", g_counter, BUMPS * 2);
        return 1;
    }
    snprintf(note, n, "%d bumps from two threads, none lost", g_counter);
    return 0;
}

static int t_the_directories_exist(char *note, unsigned n) {
    const char *data = platform_data_dir();
    unsigned    len;

    if (!data || !data[0]) {
        snprintf(note, n, "no data directory");
        return 1;
    }
    len = 0;
    while (data[len]) len++;
    if (data[len - 1] != '/') {
        snprintf(note, n, "\"%s\" does not end in a separator", data);
        return 1;
    }
    snprintf(note, n, "data %s, link %s", data, platform_hostfs_dir()[0] ? platform_hostfs_dir() : "(none)");
    return 0;
}

void test_platform_register(void) {
    selftest_add("platform", "the clock moves forward", t_the_clock_moves_forward);
    selftest_add("platform", "a difference survives the wrap", t_a_difference_survives_the_wrap);
    selftest_add("platform", "a lock excludes", t_a_lock_excludes);
    selftest_add("platform", "the directories exist", t_the_directories_exist);
}
