/* What a thing looks like, as a table: a value more than one element consults.
 * An element's own anatomy -- a thumbnail's size, a chip's padding -- stays in
 * the element, where a page cannot reshape one by accident. */
#ifndef VIEW_STYLE_H
#define VIEW_STYLE_H

#include "port/text.h"

typedef enum {
    UI_HEADING = 0, /* a page's own name, and a section's           */
    UI_BODY,        /* what the page is mostly made of              */
    UI_CAPTION,     /* under a thing, or explaining one             */
    UI_VALUE,       /* what a setting is currently set to           */
    UI_SUBHEADING,  /* the name of the ONE thing a page is about    */
    UI_ROLE_N
} ui_role;

typedef struct {
    text_face_id face;
    int          lead; /* space before it, when something precedes it */
    int          band; /* the height it takes -- room for the face, not just its caps */
} ui_role_style;

const ui_role_style *ui_style_of(ui_role role);

/* Not in the struct: a value draws in the theme's accent, which changes. */
unsigned ui_role_ink(ui_role role);

/* A page's margins, so a page states none of its own. */
typedef enum {
    UI_PAGE_TEXT = 0, /* prose: space under the bar   */
    UI_PAGE_ROWS,     /* a list: the rows butt the bar */
    UI_PAGE_LIST,     /* a library */
    UI_PAGE_DETAIL,   /* one item */
    UI_PAGE_HOME,     /* rails: they bleed off both edges, so no side margin */
    UI_PAGE_KIND_N
} ui_page_kind;

typedef struct {
    int pad_l, pad_r, pad_top;
    int title_h, foot_h; /* the rows the two bars take out of the panel */
} ui_page_style;

const ui_page_style *ui_style_page(ui_page_kind kind);

#endif
