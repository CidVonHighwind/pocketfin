/* See page_chrome.h. */

#include "view/page_chrome.h"

#include "port/gfx.h"
#include "base/log.h"

#include <stdio.h>
#include <string.h>
#include "view/style.h"
#include "view/theme.h"
#include "view/stack.h"

static int      g_busy;
static unsigned g_busy_frame;

void ui_page_busy(int busy) { g_busy = busy ? 1 : 0; }

/* The bars are the same height whatever the page kind. */
ui_rect ui_page_body(void) {
    const ui_page_style *ps = ui_style_page(UI_PAGE_TEXT);

    return ui_rect_make(0, ps->title_h, GFX_W, GFX_H - ps->title_h - ps->foot_h);
}

void ui_page_begin(ui_layout *layout, ui_page_kind kind, const char *title, const char *status, int arrow) {
    const ui_theme      *t  = ui_theme_now();
    const ui_page_style *ps = ui_style_page(kind);

    gfx_vgrad(0, 0, GFX_W, GFX_H, t->bg_top, t->bg_bot);
    ui_title_bar_at(ui_rect_make(0, 0, GFX_W, ps->title_h), title, status, arrow, g_busy, g_busy_frame++);
    g_busy = 0;

    ui_layout_begin(layout, ui_page_body());
    ui_pad_x(layout, ps->pad_l, ps->pad_r);
    ui_gap(layout, ps->pad_top);
}

static char g_said[96];

/* Repeated verbatim, it is the same fault still on screen and worth nothing;
   changed, it is a different page or a different overflow and worth saying. */
static void say_once(const char *fmt, int a, int b, int c) {
    char now[sizeof(g_said)];

    snprintf(now, sizeof(now), fmt, a, b, c);
    if (strcmp(now, g_said) == 0) return;
    snprintf(g_said, sizeof(g_said), "%s", now);
    log_printf("ui: %s (%s)", now, screen_top_name());
}

void ui_page_end(ui_layout *layout, const ui_hint_item *left, int left_n, const ui_hint_item *right, int right_n) {
    const ui_page_style *ps = ui_style_page(UI_PAGE_TEXT);

    /* Once, not once a frame: 60 lines a second is 19 us each on the cable and
       18,924 us to the card, which is the machine gone. */
    if (ui_layout_depth(layout) != 1)
        say_once("the page left %d boxes open -- a ui_end is missing", ui_layout_depth(layout) - 1, 0, 0);
    else if (ui_used(layout) > ui_here(layout).h)
        say_once("the page put %d px into a %d px body -- %d over", ui_used(layout), ui_here(layout).h,
                 ui_used(layout) - ui_here(layout).h);
    else
        g_said[0] = 0;

    ui_footer_at(ui_rect_make(0, GFX_H - ps->foot_h, GFX_W, ps->foot_h), left, left_n, right, right_n);
}
