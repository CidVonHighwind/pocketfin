/* The front page: four rails of artwork -- libraries, Continue Watching,
 * Next Up, Recently Added -- read from the library over the network. */
#ifndef PAGE_HOME_H
#define PAGE_HOME_H

#include "view/stack.h"

extern const screen_def home_page_screen;

/* Every item the rail holds, not just those on screen. */
int home_page_library_count(void);
int home_page_watching_count(void);

/* "" out of range. */
const char *home_page_library_name(int index);

/* A card is as wide as its picture, so a portrait poster draws narrower. 0
 * before the rail has drawn once. */
int home_page_watching_card_w(int index);

/* Focus ids: the libraries rail starts at 0, each other rail at its base. */
#define HOME_RAIL_WATCHING_BASE 100
#define HOME_RAIL_NEXT_BASE     200
#define HOME_RAIL_RECENT_BASE   300

int home_page_next_count(void);
int home_page_next_scroll(void);

/* A rail card's box: picture plus its ring. */
#define WATCH_CARD_BOX 82

int home_page_scroll(void);

#endif
