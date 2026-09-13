/* See style.h. */

#include "view/style.h"

#include "view/theme.h"

/* The bands are the face's line height plus a little air, not its cap height:
   a band cut to the caps clips a descender, and two lines of different faces
   set solid read as one block. */
static const ui_role_style g_role[UI_ROLE_N] = {
    /* face        lead  band */
    {TEXT_TITLE, 10, 20}, /* UI_HEADING */
    {TEXT_BODY, 4, 18},   /* UI_BODY    */
    {TEXT_SMALL, 2, 14},  /* UI_CAPTION */
    {TEXT_SMALL, 0, 14},  /* UI_VALUE -- never stacked, so it leads with none */
    /* Not the title face: the bar above already carries the page's name in
       it. */
    {TEXT_BODY, 6, 17}, /* UI_SUBHEADING */
};

unsigned ui_role_ink(ui_role role) {
    switch (role) {
    case UI_HEADING: return UI_FG;
    case UI_CAPTION: return UI_FG_FAINT;
    /* Focused or not: dimmed on every row but one, a settings page reads as
       mostly disabled. */
    case UI_VALUE: return ui_theme_now()->accent;
    case UI_SUBHEADING: return UI_FG;
    default: return UI_FG_DIM;
    }
}

/* pad_l, pad_r, pad_top, title_h, foot_h */
static const ui_page_style g_page[UI_PAGE_KIND_N] = {
    {12, 14, 12, 22, 18}, /* UI_PAGE_TEXT */
    {12, 14, 0, 22, 18},  /* UI_PAGE_ROWS */
    {14, 10, 0, 22, 18},  /* UI_PAGE_LIST */
    {10, 10, 10, 22, 18}, /* UI_PAGE_DETAIL */
    /* The rail insets its own heading and first card. */
    {0, 0, 8, 22, 18}, /* UI_PAGE_HOME */
};

const ui_role_style *ui_style_of(ui_role role) {
    if (role < 0 || role >= UI_ROLE_N) role = UI_BODY;
    return &g_role[role];
}

const ui_page_style *ui_style_page(ui_page_kind kind) {
    if (kind < 0 || kind >= UI_PAGE_KIND_N) kind = UI_PAGE_TEXT;
    return &g_page[kind];
}
