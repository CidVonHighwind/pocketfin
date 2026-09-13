/* The radio. After a standby the firmware still reports the association it had,
 * so an association only counts in the standby generation it was made in
 * (base/standby.h), and net_state() reports an older one DOWN. Nothing here
 * reconnects on its own. */
#ifndef IO_NET_H
#define IO_NET_H

typedef enum {
    NET_OFF = 0, /* the WLAN switch is off */
    NET_DOWN,    /* on, but not associated */
    NET_UP
} net_state_t;

const char *net_state_text(net_state_t s);

/* Loads the modules and claims the pool; joins nothing. Idempotent.
 * NET_START_NO_SWITCH is the one failure a viewer can cure. */
#define NET_START_NO_SWITCH (-7)
int  net_start(void);
void net_stop(void);

/* Joins a connection profile saved on the console, counting from 1, so no
 * network credentials live here. Bounded; 0 with the address in `ip`.
 *
 * Profile 0 adopts an association already up but joins nothing blind: guessing
 * 1 joined the wrong one of two saved networks. */
int  net_connect(int profile, char *ip, unsigned iplen);
void net_disconnect(void);

/* Drawing thread only: from a worker the firmware refuses the query
 * (0x80410005), and the refusal reads as a radio that is off. */
net_state_t net_state(void);

/* What the last net_state() found, for workers. */
net_state_t net_state_last(void);

int         net_joined(void);
const char *net_address(void); /* "" when not joined */

/* The saved connection joined over, or 0. The console's dialog never hands one
 * back, so this is what a wake rejoins. */
int net_profile(void);

/* Accepts the association already there as current; -1 when there is none. */
int net_revalidate(void);

/* Pretends to be `times` slower than the console's link, so when things are
 * fetched is visible on a desktop. 0 is off. Nothing on the console. */
void net_pretend_slow(int times);

#endif
