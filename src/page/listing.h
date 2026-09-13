/* The listing screen: what is inside one thing. On the stack once per level,
 * each with its own parent, title, sort and scroll; the rows are not copied
 * per level, so a level coming back asks again. */
#ifndef PAGE_LISTING_H
#define PAGE_LISTING_H

#include "jelly/item.h"
#include "model/catalog.h"
#include "view/stack.h"

extern const screen_def listing_page_screen;

#define LISTING_MAX 128

typedef struct {
    char parent[ITEM_ID_LEN];
    char title[64];
} listing_arg;

/* `parent_id` NULL is the top of the library. */
void listing_page_show(const char *parent_id, const char *title);

int listing_page_count(void);

/* What the top level has asked the catalog for. */
void listing_page_items(lib_items *out);

/* The string the viewer reads, not a re-derivation of it. */
const char *listing_page_status(void);

int         listing_page_scroll(void);
const char *listing_page_row_name(int index);

int listing_page_opened(void);

int listing_page_sort(void);

#endif
