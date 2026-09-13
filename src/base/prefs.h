/* What the application remembers between runs, in the data directory's
 * settings.txt. pref() loads it on first use. The connection -- server, port,
 * credentials -- is not here. */
#ifndef BASE_PREFS_H
#define BASE_PREFS_H

/* Written in this order. Unknown keys are skipped, so an older file reads. */
typedef enum {
    /* Off by default and it must stay that way: a test run must not write
       watch state onto a real library. */
    PREF_REPORT_PLAYBACK = 0,

    /* Off by default: a card line costs 18,924 us, over half a frame. The
       cable sink is not gated and costs 19 us. */
    PREF_LOG_TO_CARD,

    PREF_THEME, /* 0..UI_THEME_N-1 */
    PREF_BITRATE_KBPS,
    PREF_CPU_MHZ, /* two steps: the two clocks this machine has */

    PREF_N
} pref_id;

/* In the setting's own units, never a step number. */
int pref(pref_id id);

/* Clamped to the range; a dial refuses and logs a value that is not a step.
 * In memory only until prefs_save(). */
void pref_set(pref_id id, int value);

/* Steps are 1-based and out of range clamps. A setting that is not a dial
 * answers 0 steps and step 1. */
int  pref_steps(pref_id id);
int  pref_at_step(pref_id id, int step1);
int  pref_step_now(pref_id id);
void pref_set_step(pref_id id, int step1);

void        prefs_load(void);
int         prefs_save(void); /* 0 on success */
const char *prefs_file(void);

/* The next accessor reads the file again. */
void prefs_forget(void);

#endif
