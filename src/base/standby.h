/* Standby and wake. The power callback only records an edge; the machine
 * advances on the loop's thread, the only one allowed to touch hardware.
 *
 * The epoch increments once, on the way back up, and only after something
 * has been observed working -- not when the firmware says RESUME_COMPLETE. */
#ifndef BASE_STANDBY_H
#define BASE_STANDBY_H

typedef enum {
    STANDBY_UP = 0,
    STANDBY_LOSING, /* an event says it is going; it still answers */
    STANDBY_DOWN,
    STANDBY_RESTORING /* back, and nothing has been proven yet */
} standby_phase;

unsigned      standby_epoch(void);
standby_phase standby_state(void);
const char   *standby_text(standby_phase p);

/* The one thing that does not survive a standby: the display keeps scanning
 * and nothing else held state through four measured standbys, so only the
 * link needs releasing.
 *
 * `acquire` runs once per restore; `verify` repeats until it returns 0, and
 * the epoch does not advance before then. Re-acquiring would reset whatever
 * baseline verify measures against. */
void standby_watch(void (*release)(void), int (*acquire)(void), int (*verify)(void));

/* The callback's entry points. SUSPENDING and STANDBY are the same edge. */
void standby_note_going(void);
void standby_note_back(void); /* RESUME_COMPLETE */

/* Once a frame, from the loop. */
void standby_step(void);

#endif
