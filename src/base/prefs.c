/* See prefs.h. One table walked by the reader, the writer and both dials, so
   a key cannot be read and not written back. */

#include "base/prefs.h"

#include "base/log.h"
#include "port/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PREF_FILE "settings.txt"

/* Stored in their own units, so the file stays meaningful if the steps change.
 *
 * 2800 was measured, not chosen: 2.8 Mbit/s as one-second HLS fMP4 segments
 * ran at 30 fps with zero starvation across sixteen titles and reached first
 * picture in 1.0-1.5 s.
 *
 * It stops at 3200 because the radio does: this console's link carries
 * 3691-3985 kbit/s off a plain HTTP server, unchanged over two sockets at
 * once, every receive chunk and window size, and every thread priority. A
 * 4.0 Mbit/s stream buffered five times in 25 seconds. 3200 leaves about 13%
 * of the link to be late in. */
static const short kBitrateKbps[] = {1000, 1500, 2000, 2400, 2800, 3200};

/* Homebrew boots at 222/111 and 333 is worth about 16% on a decode. */
static const short kCpuMhz[] = {222, 333};

#define COUNT(a) ((short)(sizeof(a) / sizeof((a)[0])))

/* A NULL `steps` means a plain value clamped to low..high. A flag is that with
 * 0..1, and is the only kind a file may spell as a word ("on"). The theme's 5
 * is UI_THEME_N - 1, without base including view; ui_theme_set refuses an
 * index past its table anyway. */
static struct {
    const char  *key;
    int          value;
    int          def; /* so forgetting can undo */
    const short *steps;
    short        nsteps;
    short        low, high;
} g_pref[PREF_N] = {{"report_playback", 0, 0, 0, 0, 0, 1},
                    {"logging", 0, 0, 0, 0, 0, 1},
                    {"theme", 0, 0, 0, 0, 0, 5},
                    {"bitrate_kbps", 2800, 2800, kBitrateKbps, COUNT(kBitrateKbps), 0, 0},
                    {"cpu_mhz", 222, 222, kCpuMhz, COUNT(kCpuMhz), 0, 0}};

static char g_path[80];
static int  g_loaded;

static int in_range(pref_id id) { return id >= 0 && id < PREF_N; }

static int is_flag(pref_id id) { return !g_pref[id].steps && g_pref[id].low == 0 && g_pref[id].high == 1; }

/* `from_file` decides whether an off-dial value is worth a line: the file
   saying one thing and the machine doing another must not be silent. */
static void set_value(pref_id id, int v, int from_file) {
    int i;

    if (!in_range(id)) return;

    if (!g_pref[id].steps) {
        if (v < g_pref[id].low) v = g_pref[id].low;
        if (v > g_pref[id].high) v = g_pref[id].high;
        g_pref[id].value = v;
        return;
    }
    for (i = 0; i < g_pref[id].nsteps; i++)
        if (g_pref[id].steps[i] == v) {
            g_pref[id].value = v;
            return;
        }

    if (from_file) log_printf("prefs: %s %d is not one of the steps -- ignored, running at %d", g_pref[id].key, v, g_pref[id].value);
}

const char *prefs_file(void) {
    prefs_load();
    return g_path;
}

/* Back to defaults, not merely unloaded: a file only carries the keys it
   carries, so a reload alone leaves whatever the last caller set. */
void prefs_forget(void) {
    int i;

    for (i = 0; i < PREF_N; i++) g_pref[i].value = g_pref[i].def;
    g_loaded = 0;
}

void prefs_load(void) {
    char  key[32], val[32];
    FILE *f;

    /* Set before the read: an unparseable file must not be read again by every
       accessor call after it. */
    if (g_loaded) return;
    g_loaded = 1;

    snprintf(g_path, sizeof(g_path), "%s%s", platform_data_dir(), PREF_FILE);
    if ((f = fopen(g_path, "r")) == NULL) {
        log_printf("prefs: no %s -- defaults", g_path);
        return;
    }
    log_printf("prefs: read %s", g_path);

    while (fscanf(f, "%31s %31s", key, val) == 2) {
        int i;

        for (i = 0; i < PREF_N; i++) {
            if (strcmp(key, g_pref[i].key) != 0) continue;
            set_value((pref_id)i, is_flag((pref_id)i) ? (strcmp(val, "1") == 0 || strcmp(val, "on") == 0) : atoi(val), 1);
            break;
        }
        /* An unknown key is skipped, or downgrading a build throws away
           everything a newer one wrote. */
    }
    fclose(f);
}

int prefs_save(void) {
    FILE *f;
    int   i;

    prefs_load();
    if ((f = fopen(g_path, "w")) == NULL) {
        log_printf("prefs: could not write %s", g_path);
        return -1;
    }
    for (i = 0; i < PREF_N; i++) fprintf(f, "%s %d\n", g_pref[i].key, g_pref[i].value);
    fclose(f);
    log_printf("prefs: wrote %s", g_path);
    return 0;
}

int pref(pref_id id) {
    prefs_load();
    return in_range(id) ? g_pref[id].value : 0;
}

void pref_set(pref_id id, int value) {
    prefs_load();
    set_value(id, value, 0);
}

int pref_steps(pref_id id) { return in_range(id) ? g_pref[id].nsteps : 0; }

int pref_at_step(pref_id id, int step1) {
    if (!in_range(id)) return 0;
    if (!g_pref[id].steps || g_pref[id].nsteps <= 0) return pref(id);
    if (step1 < 1) step1 = 1;
    if (step1 > g_pref[id].nsteps) step1 = g_pref[id].nsteps;
    return g_pref[id].steps[step1 - 1];
}

int pref_step_now(pref_id id) {
    int i;

    prefs_load();
    if (!in_range(id) || !g_pref[id].steps) return 1;
    for (i = 0; i < g_pref[id].nsteps; i++)
        if (g_pref[id].steps[i] == g_pref[id].value) return i + 1;
    return 1;
}

void pref_set_step(pref_id id, int step1) { pref_set(id, pref_at_step(id, step1)); }
