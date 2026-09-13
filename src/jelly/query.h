/* Ordering and filtering a listing: the choice, its label, and the name the
 * server knows it by.
 *
 * The server does both: a listing is at most 128 rows of a library that may
 * hold thousands, so ordering the rows in hand orders only the
 * alphabetically-first 128, and filtering them hides items further down
 * instead of finding them. */
#ifndef JELLY_QUERY_H
#define JELLY_QUERY_H

#include "jelly/item.h"

typedef enum {
    ITEM_SORT_NAME = 0,
    ITEM_SORT_ADDED,
    ITEM_SORT_RELEASE,
    ITEM_SORT_RATING,
    ITEM_SORT_RUNTIME,
    ITEM_SORT_PLAYED,
    ITEM_SORT_RANDOM,
    ITEM_SORT_COUNT
} item_sort;

typedef enum {
    ITEM_FILTER_ALL = 0,
    ITEM_FILTER_UNPLAYED,
    ITEM_FILTER_PLAYED,
    ITEM_FILTER_FAVORITE,
    ITEM_FILTER_RESUMABLE,
    ITEM_FILTER_COUNT
} item_filter;

const char *item_sort_label(item_sort s);
const char *item_filter_label(item_filter f);

/* Which way a sort runs when it is first chosen. */
int item_sort_default_descending(item_sort s);

/* `SortBy=` and `Filters=`. The filter is NULL for "everything": an empty
 * `Filters=` is a 400, not a no-op. */
const char *item_sort_key(item_sort s);
const char *item_filter_key(item_filter f);

#endif
