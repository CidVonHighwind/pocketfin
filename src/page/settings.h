/* Rows of label, value and control; left and right change the focused row. */
#ifndef PAGE_SETTINGS_H
#define PAGE_SETTINGS_H

#include "view/stack.h"

extern const screen_def settings_page_screen;

/* Also the rows' focus ids. */
#define SETTINGS_ROW_THEME   0
#define SETTINGS_ROW_BITRATE 1
#define SETTINGS_ROW_CPU     2
#define SETTINGS_ROW_REPORT  3
#define SETTINGS_ROW_LOG     4
#define SETTINGS_ROW_CACHE   5
#define SETTINGS_ROW_SIGNIN  6
#define SETTINGS_ROW_N       7

const char *settings_page_value(int row);

#endif
