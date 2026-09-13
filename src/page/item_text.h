/* Formatted at the point of display and never written back into the item: a
 * write-back puts the same episode under two different titles depending on
 * which screen opened it, and rewriting an item twice doubles the prefix. */
#ifndef PAGE_ITEM_TEXT_H
#define PAGE_ITEM_TEXT_H

#include <stddef.h>

#include "jelly/item.h"

/* The longest row string: an episode's is "Series  S1E01  Name" over two
 * 64-character fields. */
#define ITEM_ROW_TEXT (ITEM_NAME_LEN * 2 + 16)

/* Films, series and seasons are 2:3; an episode still and a library's own image
 * are 16:9, and a still asked for at 28x40 comes back 28x16. What the row draws
 * and what is asked of the server both come from here. */
#define ITEM_THUMB_W      28
#define ITEM_THUMB_H      40
#define ITEM_THUMB_WIDE_W 56
#define ITEM_THUMB_WIDE_H 32

void item_art_box(const item *it, int *w, int *h);

/* An item as one line. `with_series` puts the series and the S1E01 in front
 * of an episode's name; without it the number alone does. */
void item_label(const item *it, int with_series, char *out, size_t outlen);

/* `flat` is a home rail, with no series name anywhere else on screen: the title
 * carries the whole thing and the subtitle names the kind. Otherwise the series
 * is the page's title and the subtitle carries the episode number. */
void item_row_title(const item *it, int flat, char *out, size_t outlen);
void item_row_subtitle(const item *it, int flat, char *out, size_t outlen);

/* How far through the item is, 0..100. A tick is 100 ns, so a real runtime is
 * eleven digits and casting one to int overflows -- which drew a watched tick
 * on an unwatched film and nothing on a watched one. */
int item_progress_pct(const item *it);

#endif
