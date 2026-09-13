/* The detail screen: the poster beside what an item is, how far through it the
 * viewer is, and the marks. */
#ifndef PAGE_DETAIL_H
#define PAGE_DETAIL_H

#include "jelly/item.h"
#include "view/stack.h"

extern const screen_def detail_page_screen;

void detail_page_show(const item *it);

const char *detail_page_mark_label(void);

/* Placed on the way in rather than inherited: ids are small integers each page
 * picks, and the opening row's id would land wherever it collides. */
#define DETAIL_ID_PLAY 2

#endif
