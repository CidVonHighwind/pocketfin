/* See keys.h. */

#include "page/keys.h"

#include "base/log.h"
#include "port/platform.h"
#include "view/element.h"
#include "view/page_chrome.h"

#include <stdio.h>
#include <string.h>

SCREEN_ARG_FITS(keys_ask);

/* Read after the pop, so not in the arena. */
static int  g_answer_tag = -1;
static char g_answer[384];

typedef struct {
    int      up;
    unsigned frame;
    keys_ask ask;
} keys_state;

void keys_ask_for(int tag, const char *title, const char *initial, unsigned max_chars) {
    keys_ask a;

    memset(&a, 0, sizeof(a));
    a.tag       = tag;
    a.max_chars = max_chars;
    snprintf(a.title, sizeof(a.title), "%s", title ? title : "");
    snprintf(a.initial, sizeof(a.initial), "%s", initial ? initial : "");
    screen_push_with(&keys_page_screen, &a, sizeof(a));
}

int keys_take(int tag, char *out, unsigned n) {
    if (g_answer_tag != tag) return 0;
    snprintf(out, n, "%s", g_answer);
    memset(g_answer, 0, sizeof(g_answer)); /* it may have been a password */
    g_answer_tag = -1;
    return 1;
}

/* Between frames, as the chooser: the dialog draws nothing when started from
   inside a display list. */
static void enter(void *st, const void *arg, unsigned arg_len) {
    keys_state *s = (keys_state *)st;

    if (arg && arg_len == sizeof(keys_ask)) memcpy(&s->ask, arg, sizeof(keys_ask));
    g_answer_tag = -1;
    s->up        = platform_keyboard_begin(s->ask.title, s->ask.initial, s->ask.max_chars) == 0;
    if (!s->up) log_line("keys: this machine has no keyboard to offer");
}

static void frame(ui_frame *ui, void *st) {
    keys_state *s = (keys_state *)st;

    s->frame++;
    if (!platform_keyboard_showing()) {
        ui_page_begin(&ui->layout, UI_PAGE_TEXT, "Pocketfin", 0, -1);
        ui_waiting(&ui->layout, s->ask.title, s->frame);
        ui_page_end(&ui->layout, 0, 0, 0, 0);
    }
    if (!s->up) {
        screen_pop();
        return;
    }
    /* The firmware reads the pad while it is up: Circle must not pop a dialog
       still running (see page/network.c). */
    screen_frame_took_back();

    switch (platform_keyboard_step()) {
    case PLATFORM_KEYS_RUNNING: return;
    case PLATFORM_KEYS_DONE:
        snprintf(g_answer, sizeof(g_answer), "%s", platform_keyboard_text());
        g_answer_tag = s->ask.tag;
        break;
    default: break; /* cancelled: nothing to collect */
    }
    s->up = 0;
    screen_pop();
}

static void describe(void *st, char *out, unsigned n) {
    const keys_state *s = (const keys_state *)st;

    snprintf(out, n, "keys: %s\nopen: %d\n", s ? s->ask.title : "", s ? s->up : 0);
}

const screen_def keys_page_screen = {"keys", sizeof(keys_state), sizeof(keys_ask), enter, frame, 0, 0, describe};
