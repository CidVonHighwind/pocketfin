/* Between ui_page_begin and ui_page_end the body column is open, with the
 * page's margins already on it.
 *
 * The ground is drawn here, not by the title bar: a bar that paints the whole
 * panel makes the order of a page's calls load-bearing. */
#ifndef VIEW_PAGE_CHROME_H
#define VIEW_PAGE_CHROME_H

#include "view/element.h"
#include "view/layout.h"
#include "view/style.h"

/* A spinner in the title bar for the next ui_page_begin, which clears it. The
 * status is untouched: a page that overwrote it to say "Loading" destroyed the
 * sort the viewer needed to read. */
void ui_page_busy(int busy);

/* `status` and `arrow` are the title bar's; see ui_title_bar_at. */
void ui_page_begin(ui_layout *layout, ui_page_kind kind, const char *title, const char *status, int arrow);

/* The body column stays open, so what a page took and what it had are both
 * readable after this returns. Either hint group may be NULL/0. Logs a page
 * that left a box open or overflowed its body. */
void ui_page_end(ui_layout *layout, const ui_hint_item *left, int left_n, const ui_hint_item *right, int right_n);

/* Between the bars, for a page that places its own contents. */
ui_rect ui_page_body(void);

#endif
