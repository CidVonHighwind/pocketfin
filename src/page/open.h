/* What opening a row means, in one place. A series is somewhere to go, not a
 * thing to watch: reading it as one made every season unreachable, because
 * /Items/Latest answers with series for a television library. */
#ifndef PAGE_OPEN_H
#define PAGE_OPEN_H

#include "io/net.h"
#include "jelly/item.h"

void page_open_item(const item *it);

/* Takes both facts rather than reading them: the desktop has no dialog and its
 * radio is never anything but up. */
int page_offer_picker(net_state_t link, int has_picker);

/* The retry button on both the start-up and the offline screen, so the two
   cannot drift. 1 when the dialog was pushed and the caller should return. */
int page_retry_or_pick(void);

#endif
