/* The console's on-screen keyboard, as a screen: pushed with what to ask,
 * stepped once a frame like the connection chooser (page/network.c), and
 * popped with the answer kept for the page underneath to collect. */
#ifndef PAGE_KEYS_H
#define PAGE_KEYS_H

#include "view/stack.h"

typedef struct {
    int      tag; /* the asking page's own number for the field */
    unsigned max_chars;
    char     title[64];
    char     initial[128];
} keys_ask;

extern const screen_def keys_page_screen;

/* Opens the keyboard over the current page. */
void keys_ask_for(int tag, const char *title, const char *initial, unsigned max_chars);

/* Once per answer: 1 with the text typed for `tag`, 0 when there is nothing
   new for it (still typing, cancelled, or asked for another field). */
int keys_take(int tag, char *out, unsigned n);

#endif
