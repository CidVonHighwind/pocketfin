/* The screen a connection loss lands on. Pushed over whatever was being
 * browsed and popped when the link is back: a front page kept through an
 * outage is lying, since opening any of its rows fails. */
#ifndef PAGE_OFFLINE_H
#define PAGE_OFFLINE_H

#include "view/stack.h"

extern const screen_def offline_page_screen;

int offline_page_trying(void);

#endif
