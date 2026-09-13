/* What every check that draws a screen does first and then over and over,
 * lent by test_settings_page.c. */
#ifndef TESTS_PAGE_TEST_PAGE_H
#define TESTS_PAGE_TEST_PAGE_H

/* The panel and the font up, on the first theme. -1 with `note` saying so. */
int page_panel(char *note, unsigned n);

/* One frame of the top screen with `mask` held and pressed. */
void page_step(unsigned mask);

#endif
