/* See selftest.h. */

#include "tools/selftest.h"

#include "base/log.h"
#include "port/platform.h"

#include <stdio.h>
#include <string.h>

/* The console's debug screen is 34 rows: results wrap over all but the last,
 * which keeps the summary. */
#define SCREEN_ROWS 33
#define SCREEN_LAST 33

typedef struct {
    char        group[SELFTEST_NAME_MAX];
    char        name[SELFTEST_NAME_MAX];
    selftest_fn fn;
} entry;

static entry    g_test[SELFTEST_MAX];
static int      g_count;
static int      g_refused;
static int      g_skipped;
static unsigned g_ms;

void selftest_add(const char *group, const char *name, selftest_fn fn) {
    if (!fn || !name) return;
    if (g_count >= SELFTEST_MAX) {
        g_refused++;
        log_printf("selftest: no room for \"%s\" -- %d turned away", name, g_refused);
        return;
    }
    snprintf(g_test[g_count].group, sizeof(g_test[0].group), "%s", group ? group : "");
    snprintf(g_test[g_count].name, sizeof(g_test[0].name), "%s", name);
    g_test[g_count].fn = fn;
    g_count++;
}

int selftest_skipped(void) { return g_skipped; }

/* A comma-separated list because reaching the console costs a build, a load
 * and a stop: four groups in one load is 6 s, one at a time 25 s. */
static int wanted(const char *group, const char *prefix) {
    const char *p = prefix;

    if (!prefix || !prefix[0]) return 1;
    while (*p) {
        const char *g     = group;
        const char *start = p;

        while (*p && *p != ',' && *g == *p) {
            g++;
            p++;
        }
        if (p != start && (!*p || *p == ',')) return 1;

        while (*p && *p != ',') p++;
        if (*p == ',') p++;
    }
    return 0;
}

/* "of" is the registry and "ran" the run: a prefix matching nothing would
 * otherwise read exactly like a pass. */
static void say_summary(const char *prefix, int failed, int ran) {
    char line[SELFTEST_NAME_MAX + 64];

    if (prefix && prefix[0])
        snprintf(line, sizeof(line), "  %d failed, %d skipped, %d ran matching \"%s\", in %u ms", failed, g_skipped, ran, prefix, g_ms);
    else
        snprintf(line, sizeof(line), "  %d failed, %d skipped, of %d, in %u ms", failed, g_skipped, g_count, g_ms);

    platform_console_at(0, SCREEN_LAST, line);
}

static int run(const char *prefix) {
    int      failed = 0, i, ran = 0, total = 0;
    unsigned t0 = platform_clock_us();

    for (i = 0; i < g_count; i++)
        if (wanted(g_test[i].group, prefix)) total++;

    for (i = 0; i < g_count; i++) {
        char     note[SELFTEST_NOTE_MAX];
        char     screen[SELFTEST_NOTE_MAX + SELFTEST_NAME_MAX + 64];
        unsigned took;
        int      rc;

        if (!wanted(g_test[i].group, prefix)) continue;
        ran++;
        note[0] = 0;
        took    = platform_clock_us();
        rc      = g_test[i].fn(note, sizeof(note));
        took    = platform_clock_us() - took;

        if (rc > 0) failed++;
        if (rc < 0) g_skipped++;
        log_printf("%-4s %s/%s  %u us  %s", rc > 0 ? "FAIL" : rc < 0 ? "SKIP" : "ok", g_test[i].group, g_test[i].name, took, note);

        /* In us below a millisecond: most checks are microseconds, and a
           column of "0 ms" says nothing about which is slow. */
        snprintf(screen, sizeof(screen), "  %2d/%2d %-4s %5u %s  %-24s%s%s", ran, total,
                 rc > 0   ? "FAIL"
                 : rc < 0 ? "SKIP"
                          : "ok",
                 took < 1000u ? took : took / 1000u, took < 1000u ? "us" : "ms", g_test[i].name, rc != 0 && note[0] ? "  -- " : "",
                 rc != 0 ? note : "");
        platform_console_at(0, (ran - 1) % SCREEN_ROWS, screen);
    }

    g_ms = (platform_clock_us() - t0) / 1000u;

    /* A filtered run proves only what it ran, so it must not claim the
     * registry is sound. */
    if (prefix && prefix[0]) {
        log_printf("POCKETFIN-PART %s: failed=%d of=%d in %u ms", prefix, failed, ran, g_ms);
        log_flush();
        if (!ran) log_printf("selftest: no group begins with \"%s\"", prefix);
        say_summary(prefix, failed, ran);
        return failed;
    }

    /* A check the registry dropped reported nothing, so it counts as failed. */
    failed += g_refused;
    if (g_refused) log_printf("selftest: %d check(s) never registered", g_refused);

    {
        log_stats st;

        log_get_stats(&st);
        if (st.cable_dropped || st.card_dropped)
            log_printf("selftest: %u line(s) never reached the cable, %u never reached the card -- the transcript is incomplete",
                       st.cable_dropped, st.card_dropped);
    }
    log_printf("POCKETFIN-DONE failed=%d skipped=%d of=%d in %u ms", failed, g_skipped, g_count, g_ms);
    log_flush();
    say_summary(0, failed, ran);
    return failed;
}

int selftest_run_all(void) { return run(0); }
int selftest_run_group(const char *prefix) { return run(prefix); }
