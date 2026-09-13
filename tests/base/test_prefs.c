/* See base/prefs.h. These write a real file and read it back: checked only in
 * memory, a table passes while nothing reaches the disk. */

#include "base/prefs.h"

#include "test_prefs.h"

#include "port/platform.h"
#include "tools/selftest.h"

#include <stdio.h>
#include <string.h>

/* The viewer's own file, put back when these are done. */
static char g_kept[1024];
static int  g_kept_len;
static int  g_kept_done;

static void settings_path(char *out, unsigned n) { snprintf(out, n, "%ssettings.txt", platform_data_dir()); }

void prefs_file_keep(void) {
    char  path[128];
    FILE *f;

    if (g_kept_done) return;
    g_kept_done = 1;
    settings_path(path, sizeof(path));
    if ((f = fopen(path, "rb")) == NULL) return;
    g_kept_len = (int)fread(g_kept, 1, sizeof(g_kept), f);
    fclose(f);
}

static int write_settings(const char *body) {
    char  path[128];
    FILE *f;

    prefs_file_keep();
    settings_path(path, sizeof(path));
    if ((f = fopen(path, "wb")) == NULL) return -1;
    fputs(body, f);
    fclose(f);
    prefs_forget();
    return 0;
}

static void forget_file(void) {
    char path[128];

    prefs_file_keep();
    settings_path(path, sizeof(path));
    remove(path);
    prefs_forget();
}

/* settings.c moves a dial only through pref_set_step(pref_step_now() +- 1), so
   a pref_steps() one short leaves the last step unreachable -- invisible to a
   check of pref_set() alone. */
static int t_a_dial_walks_every_step_and_stops(char *note, unsigned n) {
    const pref_id id = PREF_BITRATE_KBPS;
    int           steps, i, lo, hi;

    forget_file();
    steps = pref_steps(id);
    if (steps < 2) {
        snprintf(note, n, "the bitrate dial reports %d steps", steps);
        return 1;
    }
    lo = pref_at_step(id, 1);
    hi = pref_at_step(id, steps);

    for (i = 1; i <= steps; i++) {
        pref_set_step(id, i);
        if (pref_step_now(id) != i || pref(id) != pref_at_step(id, i)) {
            snprintf(note, n, "step %d of %d set %d and read back as step %d", i, steps, pref(id), pref_step_now(id));
            return 1;
        }
    }

    /* One past either end is what a held d-pad does. */
    pref_set_step(id, 1);
    pref_set_step(id, pref_step_now(id) - 1);
    if (pref(id) != lo) {
        snprintf(note, n, "stepping below the first gave %d, wanted %d", pref(id), lo);
        return 1;
    }
    pref_set_step(id, steps);
    pref_set_step(id, pref_step_now(id) + 1);
    if (pref(id) != hi) {
        snprintf(note, n, "stepping past the last gave %d, wanted %d", pref(id), hi);
        return 1;
    }

    forget_file();
    snprintf(note, n, "%d steps walked, %d to %d, both ends hold", steps, lo, hi);
    return 0;
}

/* Every setting has a default, and it is the SAFE one where safety is a
   question. Two of these are not preferences at all, they are promises. */
static int t_the_defaults_are_the_safe_ones(char *note, unsigned n) {
    forget_file();

    if (pref(PREF_REPORT_PLAYBACK) != 0) {
        snprintf(note, n, "playback reporting defaults ON -- a test run would write watch state onto a real library");
        return 1;
    }
    if (pref(PREF_LOG_TO_CARD) != 0) {
        snprintf(note, n, "card logging defaults ON -- 18,924 us a line, on every frame that writes one");
        return 1;
    }
    if (pref(PREF_CPU_MHZ) != 222) {
        snprintf(note, n, "the clock defaults to %d, and homebrew boots at 222", pref(PREF_CPU_MHZ));
        return 1;
    }
    if (pref(PREF_BITRATE_KBPS) != 2800) {
        snprintf(note, n, "the bitrate defaults to %d -- 2800 is the measured one", pref(PREF_BITRATE_KBPS));
        return 1;
    }
    snprintf(note, n, "report off, card log off, 222 MHz, 2800 kbit/s");
    return 0;
}

/* A file nobody validated is how a theme index reads past its table. */
static int t_a_value_out_of_range_is_clamped(char *note, unsigned n) {
    forget_file();

    pref_set(PREF_THEME, 99);
    if (pref(PREF_THEME) > 5) {
        snprintf(note, n, "theme 99 was stored as %d", pref(PREF_THEME));
        return 1;
    }
    pref_set(PREF_THEME, -4);
    if (pref(PREF_THEME) < 0) {
        snprintf(note, n, "theme -4 was stored as %d", pref(PREF_THEME));
        return 1;
    }
    pref_set(PREF_REPORT_PLAYBACK, 7);
    if (pref(PREF_REPORT_PLAYBACK) != 1) {
        snprintf(note, n, "a flag set to 7 reads %d", pref(PREF_REPORT_PLAYBACK));
        return 1;
    }
    return 0;
}

/* Refused rather than clamped: the steps are the machine's two clocks and the
   measured bitrates. */
static int t_a_dial_refuses_a_value_off_its_steps(char *note, unsigned n) {
    int was;

    forget_file();

    was = pref(PREF_CPU_MHZ);
    pref_set(PREF_CPU_MHZ, 280);
    if (pref(PREF_CPU_MHZ) != was) {
        snprintf(note, n, "280 MHz was accepted as %d -- the machine has two clocks", pref(PREF_CPU_MHZ));
        return 1;
    }
    pref_set(PREF_CPU_MHZ, 333);
    if (pref(PREF_CPU_MHZ) != 333) {
        snprintf(note, n, "333 MHz, which IS a step, was refused");
        return 1;
    }

    if (pref_at_step(PREF_CPU_MHZ, 1) != 222 || pref_at_step(PREF_CPU_MHZ, 2) != 333) {
        snprintf(note, n, "the clock steps are %d and %d", pref_at_step(PREF_CPU_MHZ, 1), pref_at_step(PREF_CPU_MHZ, 2));
        return 1;
    }
    if (pref_at_step(PREF_CPU_MHZ, 0) != 222 || pref_at_step(PREF_CPU_MHZ, 99) != 333) {
        snprintf(note, n, "a step out of range read past the table");
        return 1;
    }
    if (pref_step_now(PREF_CPU_MHZ) != 2) {
        snprintf(note, n, "at 333 MHz the dial says step %d", pref_step_now(PREF_CPU_MHZ));
        return 1;
    }
    snprintf(note, n, "280 refused, 333 taken, steps 222/333");
    return 0;
}

static int t_what_is_saved_is_what_is_read_back(char *note, unsigned n) {
    forget_file();

    pref_set(PREF_THEME, 3);
    pref_set(PREF_BITRATE_KBPS, 3200);
    pref_set(PREF_REPORT_PLAYBACK, 1);
    pref_set(PREF_LOG_TO_CARD, 1);
    pref_set(PREF_CPU_MHZ, 333);

    if (prefs_save() != 0) {
        snprintf(note, n, "could not write %s", prefs_file());
        return 1;
    }

    prefs_forget();
    pref_set(PREF_THEME, 0); /* loads the file first, then sets in memory */
    prefs_forget();

    if (pref(PREF_THEME) != 3 || pref(PREF_BITRATE_KBPS) != 3200 || pref(PREF_REPORT_PLAYBACK) != 1 || pref(PREF_LOG_TO_CARD) != 1 ||
        pref(PREF_CPU_MHZ) != 333) {
        snprintf(note, n, "read back theme %d, %d kbit/s, report %d, card %d, %d MHz", pref(PREF_THEME), pref(PREF_BITRATE_KBPS),
                 pref(PREF_REPORT_PLAYBACK), pref(PREF_LOG_TO_CARD), pref(PREF_CPU_MHZ));
        return 1;
    }
    snprintf(note, n, "five values through %s", prefs_file());
    forget_file();
    return 0;
}

/* Or downgrading a build throws away everything a newer one wrote. */
static int t_an_unknown_key_does_not_lose_the_rest(char *note, unsigned n) {
    if (write_settings("theme 2\nsomething_from_next_year 41\ncpu_mhz 333\n") != 0) {
        snprintf(note, n, "could not stage a settings file");
        return 1;
    }

    if (pref(PREF_THEME) != 2) {
        snprintf(note, n, "the key BEFORE the unknown one read %d", pref(PREF_THEME));
        return 1;
    }
    if (pref(PREF_CPU_MHZ) != 333) {
        snprintf(note, n, "the key AFTER the unknown one read %d -- the read stopped at what it did not recognise", pref(PREF_CPU_MHZ));
        return 1;
    }
    snprintf(note, n, "both real keys survived an unknown one between them");
    forget_file();
    return 0;
}

/* settings.txt is edited by hand. */
static int t_a_flag_reads_on_as_well_as_1(char *note, unsigned n) {
    if (write_settings("logging on\nreport_playback off\n") != 0) {
        snprintf(note, n, "could not stage a settings file");
        return 1;
    }
    if (pref(PREF_LOG_TO_CARD) != 1) {
        snprintf(note, n, "\"on\" read as %d", pref(PREF_LOG_TO_CARD));
        return 1;
    }
    if (pref(PREF_REPORT_PLAYBACK) != 0) {
        snprintf(note, n, "\"off\" read as %d", pref(PREF_REPORT_PLAYBACK));
        return 1;
    }
    forget_file();
    return 0;
}

/* An override that does nothing is worse than none: the file says one thing and
   the machine does another. */
static int t_an_off_dial_value_in_the_file_is_refused(char *note, unsigned n) {
    if (write_settings("cpu_mhz 280\nbitrate_kbps 12345\n") != 0) {
        snprintf(note, n, "could not stage a settings file");
        return 1;
    }
    if (pref(PREF_CPU_MHZ) != 222) {
        snprintf(note, n, "cpu_mhz 280 from a file gave %d", pref(PREF_CPU_MHZ));
        return 1;
    }
    if (pref(PREF_BITRATE_KBPS) != 2800) {
        snprintf(note, n, "bitrate 12345 from a file gave %d", pref(PREF_BITRATE_KBPS));
        return 1;
    }
    snprintf(note, n, "both off-dial values left the defaults in force");
    forget_file();
    return 0;
}

/* Nor does it read again on every accessor after, which would put a file read
   on the drawing thread. */
static int t_a_broken_file_leaves_the_defaults(char *note, unsigned n) {
    if (write_settings("]]] this is not a settings file [[[\n") != 0) {
        snprintf(note, n, "could not stage a settings file");
        return 1;
    }
    if (pref(PREF_CPU_MHZ) != 222 || pref(PREF_THEME) != 0) {
        snprintf(note, n, "garbage gave %d MHz and theme %d", pref(PREF_CPU_MHZ), pref(PREF_THEME));
        return 1;
    }
    forget_file();
    return 0;
}

static int t_it_says_which_file_it_read(char *note, unsigned n) {
    if (write_settings("theme 1\n") != 0) {
        snprintf(note, n, "could not stage a settings file");
        return 1;
    }
    prefs_load();
    if (!strstr(prefs_file(), "settings.txt")) {
        snprintf(note, n, "it says it read \"%s\"", prefs_file());
        return 1;
    }
    snprintf(note, n, "read %s", prefs_file());
    forget_file();
    return 0;
}

void prefs_file_put_back(void) {
    char  path[128];
    FILE *f;

    settings_path(path, sizeof(path));
    if (g_kept_len <= 0) {
        remove(path); /* there was none before these ran */
    } else if ((f = fopen(path, "wb")) != NULL) {
        fwrite(g_kept, 1, (unsigned)g_kept_len, f);
        fclose(f);
    }
    prefs_forget();
}

static int t_the_viewers_own_settings_are_put_back(char *note, unsigned n) {
    char  path[128];
    FILE *f;

    prefs_file_put_back();
    if (g_kept_len <= 0) {
        snprintf(note, n, "there was no settings file to put back");
        return 0;
    }
    settings_path(path, sizeof(path));
    if ((f = fopen(path, "rb")) == NULL) {
        snprintf(note, n, "%s was not put back", path);
        return 1;
    }
    fclose(f);
    snprintf(note, n, "%d bytes back in %s", g_kept_len, path);
    return 0;
}

void test_prefs_register(void) {
    selftest_add("prefs", "the defaults are the safe ones", t_the_defaults_are_the_safe_ones);
    selftest_add("prefs", "a value out of range is clamped", t_a_value_out_of_range_is_clamped);
    selftest_add("prefs", "a dial refuses a value off its steps", t_a_dial_refuses_a_value_off_its_steps);
    selftest_add("prefs", "a dial walks every step and stops", t_a_dial_walks_every_step_and_stops);
    selftest_add("prefs", "what is saved is what is read back", t_what_is_saved_is_what_is_read_back);
    selftest_add("prefs", "an unknown key does not lose the rest", t_an_unknown_key_does_not_lose_the_rest);
    selftest_add("prefs", "a flag reads on as well as 1", t_a_flag_reads_on_as_well_as_1);
    selftest_add("prefs", "an off-dial value in the file is refused", t_an_off_dial_value_in_the_file_is_refused);
    selftest_add("prefs", "a broken file leaves the defaults", t_a_broken_file_leaves_the_defaults);
    selftest_add("prefs", "it says which file it read", t_it_says_which_file_it_read);
    /* Last: it is the cleanup. */
    selftest_add("prefs", "the viewer's own settings are put back", t_the_viewers_own_settings_are_put_back);
}
