/* See network.h. */

#include "page/network.h"

#include "model/catalog.h"
#include "base/log.h"
#include "port/platform.h"
#include "view/element.h"
#include "view/page_chrome.h"

#include <stdio.h>

/* -1 until opened once. Read after the pop, so not in the arena. */
static int g_result = -1;

typedef struct {
    int      up;
    unsigned frame;
} network_state;

/* In enter(), between frames: sceUtilityNetconfInitStart draws nothing when
   called from inside our display list, and frame() runs inside one. */
static void enter(void *st, const void *arg, unsigned arg_len) {
    network_state *s = (network_state *)st;

    (void)arg;
    (void)arg_len;
    s->up = platform_picker_begin() == 0;
    if (!s->up) {
        g_result = -1;
        log_line("net: this machine has no connection dialog to offer");
    }
}

/* The dialog draws nothing while it comes up, and a black panel is what a
   frozen application looks like. */
static void frame(ui_frame *ui, void *st) {
    network_state *s = (network_state *)st;

    s->frame++;

    /* Only until the dialog shows: it composites over the buffer, and our ring
       showed through the middle of the network names. */
    if (!platform_picker_showing()) {
        ui_page_begin(&ui->layout, UI_PAGE_TEXT, "Pocketfin", 0, -1);
        ui_waiting(&ui->layout, "Choose a network", s->frame);
        ui_page_end(&ui->layout, 0, 0, 0, 0);
    }

    if (!s->up) {
        screen_pop();
        return;
    }

    /* The firmware reads the pad while the dialog is up. Circle popping this
       page abandoned a running dialog, which holds its threads, refuses every
       later open and needs a console restart. */
    screen_frame_took_back();

    switch (platform_picker_step()) {
    case PLATFORM_PICKER_RUNNING: return;

    case PLATFORM_PICKER_JOINED:
        g_result = 1;
        session_retry();
        break;

    case PLATFORM_PICKER_CANCELLED:
        g_result = 0;
        /* Backing out is a choice, and the screen underneath has to say so
           rather than repeat the old failure. */
        session_note_no_choice();
        break;

    default: g_result = -1; break;
    }

    s->up = 0;
    screen_pop();
}

static void describe(void *st, char *out, unsigned n) {
    const network_state *s = (const network_state *)st;

    snprintf(out, n, "picker: %s\nopen: %d\nresult: %d\n", platform_has_picker() ? "available" : "gone", s ? s->up : 0, g_result);
}

const screen_def network_page_screen = {"network", sizeof(network_state), 0, enter, frame, 0, 0, describe};
