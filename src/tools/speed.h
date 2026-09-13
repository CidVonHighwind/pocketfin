/* How fast this machine actually pulls bytes: `pocketfin.prx speed`.
 *
 * A mode, not a check: it needs the radio, a saved profile and a server, so as
 * a check it would fail on the world's state rather than the code.
 *
 * Wall clock: bytes over playback time with an empty ring reduces to the
 * requested bitrate, and hours of thread-priority guessing followed from
 * trusting that.
 *
 * Connect, first byte and body are reported separately because only the body
 * scales with size. */
#ifndef TOOLS_SPEED_H
#define TOOLS_SPEED_H

/* 0 when a number was obtained. Everything it does is a GET. */
int speed_run(void);

#endif
