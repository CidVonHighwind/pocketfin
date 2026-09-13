/* Every prefs check writes over the viewer's own settings.txt. A run once left
 * playback reporting off and the next film recorded nothing on the server, so
 * a check keeps the file first and puts it back after. Both cover the cable's
 * directory and the data directory. */
#ifndef TESTS_BASE_TEST_PREFS_H
#define TESTS_BASE_TEST_PREFS_H

/* Only the first call in a run does anything. */
void prefs_file_keep(void);

/* Removes the file where there was none. Safe to call more than once. */
void prefs_file_put_back(void);

#endif
