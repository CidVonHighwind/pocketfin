/* The console's own connection chooser. The firmware draws it, stepped once a
 * frame through this screen, so the application keeps running underneath and
 * can still be stopped. No credential passes through here: the profiles
 * belong to the firmware. */
#ifndef PAGE_NETWORK_H
#define PAGE_NETWORK_H

#include "view/stack.h"

extern const screen_def network_page_screen;

#endif
