/* What a machine half tells the portable half: private, so only the code that
 * joined can say a link exists. */
#ifndef IO_NET_PRIVATE_H
#define IO_NET_PRIVATE_H

void net_note_joined(const char *ip, int profile); /* profile 0: the machine cannot say */
void net_note_left(void);
int  net_link_is_current(void);

#endif
