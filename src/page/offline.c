/* See offline.h. */

#include "page/offline.h"

#include "view/element.h"
#include "model/catalog.h"
#include "io/net.h"
#include "page/open.h"
#include "base/log.h"
#include "view/page_chrome.h"

#include <stdio.h>

#define ID_RETRY 0

/* Asked of the session: a flag went stale on every reconnect that skipped this
   page's button, and read "Not connected" over a sign-in under way. */
int offline_page_trying(void) {
    lib_stage s = session_stage();

    return s == LIB_STAGE_STARTING || s == LIB_STAGE_LINKING || s == LIB_STAGE_SIGNIN;
}

static void enter(void *st, const void *arg, unsigned arg_len) {
    (void)st;
    (void)arg;
    (void)arg_len;
    log_line("offline: the library cannot reach the server");
}

static void frame(ui_frame *ui, void *st) {
    unsigned                 *frames  = (unsigned *)st;
    static const ui_hint_item RETRY[] = {{UI_BTN_CROSS, "Retry"}};

    if (!session_lost()) {
        screen_pop();
        return;
    }
    (*frames)++;

    ui_page_begin(&ui->layout, UI_PAGE_TEXT, "Pocketfin", 0, -1);

    if (offline_page_trying()) {
        ui_waiting(&ui->layout, "Reconnecting", *frames);
        ui_page_end(&ui->layout, 0, 0, 0, 0);
        return;
    }

    if (ui_notice(ui, ID_RETRY, "Not connected", session_error()[0] ? session_error() : "The link to the server went away.", "Retry")) {
        if (page_retry_or_pick()) return;
    }

    ui_page_end(&ui->layout, RETRY, 1, 0, 0);
}

/* There is nothing behind this that works. */
static int back(void *st) {
    (void)st;
    return 1;
}

/* A radio that went away and a server that stopped answering look identical on
   the panel and are recovered differently. */
static void describe(void *st, char *out, unsigned n) {
    (void)st;
    snprintf(out, n, "trying: %d\nlink: %s\nwhy: %s\n", offline_page_trying(), net_state_text(net_state()),
             session_error()[0] ? session_error() : "none");
}

const screen_def offline_page_screen = {"offline", sizeof(unsigned), 0, enter, frame, back, 0, describe};
