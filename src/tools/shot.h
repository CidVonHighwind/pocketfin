/* What the panel is scanning, as a PPM on the development machine. The
 * desktop build writes the same format from the same drawing code, so the
 * two can be compared. */
#ifndef TOOLS_SHOT_H
#define TOOLS_SHOT_H

/* Writes `leaf` on the link, a row at a time: 391 kB through an 8 kB queue.
 * -1 rather than waiting for ever if the cable stops taking bytes. */
int shot_write(const char *leaf);

/* Since the load. */
void shot_stats(unsigned *rows, unsigned *lost);

#endif
