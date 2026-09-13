/* The check runner. Checks register into a fixed table: a registry that
 * filled quietly would drop whole groups while the suite reported everything
 * passing, so run_all FAILS when anything was turned away. */
#ifndef TOOLS_SELFTEST_H
#define TOOLS_SELFTEST_H

/* About 80 bytes an entry, so the whole table is 26 kB in the console's
 * binary. */
#define SELFTEST_MAX      320
#define SELFTEST_NAME_MAX 48
#define SELFTEST_NOTE_MAX 96

/* 0 passes, above 0 fails, BELOW 0 skipped -- the check could not run at all.
 * Say why in `note`; a passing check may leave a measurement there too. */
typedef int (*selftest_fn)(char *note, unsigned note_len);

void selftest_add(const char *group, const char *name, selftest_fn fn);

/* Returns the number that failed, counting anything the registry turned away.
 * Skips are never counted as passes. */
int selftest_run_all(void);

/* Only the groups whose name begins with `prefix`, which may be a
 * comma-separated list. Proves ONLY what it ran, so it is never the gate.
 * Several groups in ONE load matters on the console: the checks are
 * milliseconds and reaching the machine is seconds. */
int selftest_run_group(const char *prefix);
/* A skip is not a pass, and the shell's exit code turns on this. */
int selftest_skipped(void);

#endif
