/* Commands from the development machine. This owns the protocol; io/link.h
 * owns the cable. Three files in platform_hostfs_dir():
 *
 *   cmd.txt      the PC writes "<seq> <word> [arg]", one line
 *   ack.txt      the app writes "<seq>" once the command is complete
 *   state.txt    what the app has to say
 *
 * Deleting cmd.txt from the console is not reliable over usbhostfs, so the
 * sequence number is the only thing that makes a command happen once. It
 * starts at zero every load, so a script starts at 1. */
#ifndef TOOLS_REMOTE_H
#define TOOLS_REMOTE_H

#define REMOTE_STATE_MAX 2048
#define REMOTE_LINE_MAX  256
#define REMOTE_WORD_MAX  32
#define REMOTE_ARG_MAX   64

typedef enum {
    REMOTE_NONE = 0,
    REMOTE_PING,   /* acked, nothing else */
    REMOTE_STATUS, /* the caller fills state.txt, then it is acked */
    REMOTE_SHOT,   /* the panel, as a PPM named by the argument */
    REMOTE_PAGE,   /* which screen draws, by name              */
    REMOTE_FOCUS,  /* the screen's row, by number -- no pad    */
    REMOTE_BACK,   /* circle, without pressing it               */
    REMOTE_PRESS,  /* one real pad press, by name              */
    REMOTE_QUIT
} remote_cmd;

/* 0 for a name that is not a button. Here rather than in each platform's loop
 * because two copies drift. */
unsigned remote_pad_named(const char *name);

/* Before remote_start(). Only the check suite calls it: pointed at one
 * cmd.txt, the running application eats a command the checks are waiting on.
 * NULL for a default. */
void remote_use_channel(const char *cmd, const char *ack, const char *state);

/* Silent when there is no link. */
void remote_start(void);

/* Does NOT ack: the caller acks when the work is done, which is what makes an
 * ack mean complete. */
remote_cmd remote_take(char *arg, unsigned arg_len);

void remote_done(void);

/* Before remote_done, so a script that has seen the ack can read the answer. */
void remote_publish(const char *text);

int remote_on(void);

#endif
