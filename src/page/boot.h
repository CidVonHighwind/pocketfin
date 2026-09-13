/* Start-up: draws session_stage(), and replaces itself with the front page
 * once the session is ready. */
#ifndef PAGE_BOOT_H
#define PAGE_BOOT_H

#include "view/stack.h"

extern const screen_def boot_page_screen;

/* How many times Try again has been pressed, for a check. */
int boot_page_attempts(void);

#endif
