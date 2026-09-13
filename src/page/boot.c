/* See boot.h. */

#include "page/boot.h"

#include "view/element.h"
#include "page/home.h"
#include "port/input.h"
#include "model/catalog.h"
#include "io/net.h"
#include "page/network.h"
#include "page/open.h"
#include "port/platform.h"
#include "view/page_chrome.h"

#include <stdio.h>

#define ID_RETRY 0

typedef struct {
    unsigned frame;
    int      attempts; /* Try again, for a check */
    int      offered;  /* the console's dialog, put up at most once */
} boot_state;

static boot_state *now(void) {
    static boot_state none;

    return screen_top_state() ? (boot_state *)screen_top_state() : &none;
}

int boot_page_attempts(void) { return now()->attempts; }

static void frame(ui_frame *ui, void *st) {
    boot_state               *b       = (boot_state *)st;
    static const ui_hint_item RETRY[] = {{UI_BTN_CROSS, "Try again"}};
    lib_stage                 stage   = session_stage();

    b->frame++;
    ui_page_begin(&ui->layout, UI_PAGE_TEXT, "Pocketfin", 0, -1);

    if (stage != LIB_STAGE_STOPPED) {
        ui_waiting(&ui->layout, session_stage_text(stage), b->frame);
        ui_page_end(&ui->layout, 0, 0, 0, 0);
        /* After the drawing, so the frame that finishes the sign-in is not
           half of each page. */
        if (stage == LIB_STAGE_READY) screen_reset(&home_page_screen);
        return;
    }

    /* Once: backing out of the dialog must not put it straight back up. */
    if (!b->offered && page_offer_picker(net_state(), platform_has_picker())) {
        b->offered = 1;
        screen_push(&network_page_screen);
    }

    if (ui_notice(ui, ID_RETRY, "Stopped", session_error()[0] ? session_error() : "Something stopped the start-up.", "Try again")) {
        if (page_retry_or_pick()) return;
        b->attempts++;
    }

    ui_page_end(&ui->layout, RETRY, 1, 0, 0);
}

/* Backing out of the first screen would be quitting, which is HOME's. */
static int back(void *st) {
    (void)st;
    return 1;
}

static void describe(void *st, char *out, unsigned n) {
    const boot_state *b = (const boot_state *)st;

    snprintf(out, n, "stage: %s\nattempts: %d\naddress: %s\nfault: %s\n", session_stage_text(session_stage()), b->attempts,
             session_address()[0] ? session_address() : "none", session_error()[0] ? session_error() : "none");
}

const screen_def boot_page_screen = {"boot", sizeof(boot_state), 0, 0, frame, back, 0, describe};
