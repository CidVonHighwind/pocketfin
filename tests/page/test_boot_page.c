/* A working server cannot be made to fail, so both screens are driven through
   the session's fake seam. */

#include "page/boot.h"
#include "page/offline.h"
#include "page/open.h"

#include "port/gfx.h"
#include "port/input.h"
#include "model/catalog.h"
#include "view/stack.h"
#include "tools/selftest.h"
#include "test_page.h"
#include "tools/shot.h"

#include <stdio.h>
#include <string.h>

static int t_the_boot_page_waits_then_explains(char *note, unsigned n) {
    if (page_panel(note, n) != 0) return -1;

    session_fake_stage(LIB_STAGE_SIGNIN, "");
    screen_reset(&boot_page_screen);
    page_step(0);
    page_step(0);
    if (shot_write("boot-signin.ppm") != 0) {
        snprintf(note, n, "the waiting picture did not land");
        return 1;
    }

    /* The longest sentence the start-up can stop on, so the capture shows it fits. */
    {
        char why[160];

        session_sign_in_text(JF_ERR_OFFLINE, "192.168.100.200:8096", why, sizeof(why));
        session_fake_stage(LIB_STAGE_STOPPED, why);
    }
    page_step(0);
    page_step(0);
    if (shot_write("boot-stopped.ppm") != 0) {
        snprintf(note, n, "the stopped picture did not land");
        return 1;
    }
    return 0;
}

/* A screen that sits still while a network call blocks cannot be told from one
   that has hung, so two frames must differ, not merely draw. */
static int t_the_waiting_ring_turns(char *note, unsigned n) {
    /* The whole body: the ring is about ten pixels across and a guessed row
       once missed it entirely. */
    static unsigned short before[GFX_H][GFX_W];
    unsigned short        now[GFX_W];
    int                   y, x, moved = 0;

    if (page_panel(note, n) != 0) return -1;

    session_fake_stage(LIB_STAGE_SIGNIN, "");
    screen_reset(&boot_page_screen);

    page_step(0);
    page_step(0);
    for (y = 0; y < GFX_H; y++)
        if (gfx_capture_row(y, before[y]) != 0) {
            snprintf(note, n, "the panel could not be read");
            return -1;
        }

    /* The ring steps every four frames. */
    for (y = 0; y < 10; y++) page_step(0);

    for (y = 0; y < GFX_H && !moved; y++) {
        if (gfx_capture_row(y, now) != 0) {
            snprintf(note, n, "the panel could not be read");
            return -1;
        }
        for (x = 0; x < GFX_W; x++)
            if (now[x] != before[y][x]) {
                moved = y;
                break;
            }
    }

    if (!moved) {
        snprintf(note, n, "ten frames apart the panel is identical -- the ring is not turning");
        return 1;
    }
    snprintf(note, n, "the panel changed by row %d between frames", moved);
    return 0;
}

/* A boot screen that stayed up after the sign-in landed is an application that
   never starts. */
static int t_a_connected_library_takes_the_boot_page_away(char *note, unsigned n) {
    if (page_panel(note, n) != 0) return -1;

    session_fake_stage(LIB_STAGE_LINKING, "");
    screen_reset(&boot_page_screen);
    page_step(0);
    if (strcmp(screen_top_name(), "boot") != 0) {
        snprintf(note, n, "linking, and the top screen is already \"%s\"", screen_top_name());
        return 1;
    }

    session_fake_stage(LIB_STAGE_READY, "");
    page_step(0);
    if (strcmp(screen_top_name(), "home") != 0) {
        snprintf(note, n, "connected, and the top screen is \"%s\", not home", screen_top_name());
        return 1;
    }
    /* Replaced, not pushed: circle out of the front page must not land back
       on the boot screen. */
    if (screen_depth() != 1) {
        snprintf(note, n, "the front page came up %d deep -- the boot page is still under it", screen_depth());
        return 1;
    }
    return 0;
}

static int t_the_stopped_page_says_why_and_retries(char *note, unsigned n) {
    int before;

    if (page_panel(note, n) != 0) return -1;
    session_fake_stage(LIB_STAGE_STOPPED, "The wireless switch is off.");
    screen_reset(&boot_page_screen);
    page_step(0);

    before = boot_page_attempts();
    page_step(PAD_CROSS);
    if (boot_page_attempts() != before + 1) {
        snprintf(note, n, "cross on Try again went from %d attempts to %d", before, boot_page_attempts());
        return 1;
    }
    if (strcmp(screen_top_name(), "boot") != 0) {
        snprintf(note, n, "Try again left the boot page for \"%s\"", screen_top_name());
        return 1;
    }
    return 0;
}

/* Driven through the same two calls the application's loop makes. */
static int t_the_offline_page_covers_what_was_showing(char *note, unsigned n) {
    if (page_panel(note, n) != 0) return -1;

    session_fake_stage(LIB_STAGE_READY, "");
    screen_reset(&boot_page_screen);
    page_step(0);

    session_fake_lost(1);
    session_watch();
    if (session_link_went()) screen_push(&offline_page_screen);
    page_step(0);
    if (strcmp(screen_top_name(), "offline") != 0) {
        snprintf(note, n, "the link is gone and \"%s\" is still on the panel", screen_top_name());
        return 1;
    }
    page_step(0);
    if (shot_write("offline.ppm") != 0) {
        snprintf(note, n, "the picture did not land");
        return 1;
    }

    session_watch();
    if (session_link_went()) screen_push(&offline_page_screen);
    page_step(0);
    if (screen_depth() != 2) {
        snprintf(note, n, "%d deep: the link going away put up more than one", screen_depth());
        return 1;
    }

    session_fake_lost(0);
    page_step(0);
    page_step(0);
    if (strcmp(screen_top_name(), "offline") == 0 || screen_depth() != 1) {
        snprintf(note, n, "the link came back and \"%s\" is showing, %d deep", screen_top_name(), screen_depth());
        return 1;
    }
    return 0;
}

static int t_offline_retry_goes_to_the_ring(char *note, unsigned n) {
    if (page_panel(note, n) != 0) return -1;

    session_fake_stage(LIB_STAGE_STOPPED, "Nothing answered.");
    session_fake_lost(1);
    screen_reset(&boot_page_screen);
    screen_push(&offline_page_screen);
    page_step(0);
    if (offline_page_trying()) {
        snprintf(note, n, "the page came up already retrying");
        return 1;
    }

    page_step(PAD_CROSS);
    /* No worker runs in a check to act on session_retry(), so only the page's
       half is asserted. */
    if (!offline_page_trying()) {
        snprintf(note, n, "cross on Retry did not start a retry");
        return 1;
    }
    session_fake_stage(LIB_STAGE_LINKING, "");
    page_step(0);
    page_step(0);
    if (shot_write("offline-trying.ppm") != 0) {
        snprintf(note, n, "the retrying picture did not land");
        return 1;
    }
    return 0;
}

/* The desktop has no dialog and its radio is always up, so neither screen can
   be driven into this; the rule is checked directly. */
static int t_the_dialog_is_offered_for_the_radio_only(char *note, unsigned n) {
    struct {
        net_state_t link;
        int         has;
        int         want;
        const char *why;
    } CASE[] = {
        {NET_OFF, 1, 0, "the switch is off -- a dialog built on the radio cannot help, and came up black"},
        {NET_DOWN, 1, 1, "the saved connection would not join"},
        {NET_UP, 1, 0, "the link is up -- the server is what refused"},
        {NET_OFF, 0, 0, "no dialog to offer"},
        {NET_DOWN, 0, 0, "no dialog to offer"},
        {NET_UP, 0, 0, "neither"},
    };
    int i;

    for (i = 0; i < (int)(sizeof(CASE) / sizeof(CASE[0])); i++)
        if (page_offer_picker(CASE[i].link, CASE[i].has) != CASE[i].want) {
            snprintf(note, n, "link %s with%s a dialog: offered %d, wanted %d -- %s", net_state_text(CASE[i].link),
                     CASE[i].has ? "" : "out", page_offer_picker(CASE[i].link, CASE[i].has), CASE[i].want, CASE[i].why);
            return 1;
        }
    snprintf(note, n, "%d combinations; only a radio that is not up is worth a dialog", i);
    return 0;
}

/* Retry must reconnect rather than push a dialog screen that would pop straight
   back. */
static int t_retry_reconnects_where_there_is_no_dialog(char *note, unsigned n) {
    if (page_panel(note, n) != 0) return -1;

    session_fake_stage(LIB_STAGE_STOPPED, "Nothing answered.");
    session_fake_lost(1);
    screen_reset(&boot_page_screen);
    screen_push(&offline_page_screen);
    page_step(0);
    page_step(PAD_CROSS);
    page_step(0);

    if (strcmp(screen_top_name(), "network") == 0) {
        snprintf(note, n, "Retry opened the connection dialog on a machine that has none");
        return 1;
    }
    if (!offline_page_trying()) {
        snprintf(note, n, "Retry neither reconnected nor opened anything");
        return 1;
    }
    return 0;
}

void test_boot_page_register(void) {
    selftest_add("boot", "the boot page waits then explains", t_the_boot_page_waits_then_explains);
    selftest_add("boot", "the waiting ring turns", t_the_waiting_ring_turns);
    selftest_add("boot", "a connected library takes the boot page away", t_a_connected_library_takes_the_boot_page_away);
    selftest_add("boot", "the stopped page says why and retries", t_the_stopped_page_says_why_and_retries);
    selftest_add("offline", "the offline page covers what was showing", t_the_offline_page_covers_what_was_showing);
    selftest_add("offline", "retry goes to the ring", t_offline_retry_goes_to_the_ring);
    selftest_add("offline", "the dialog is offered for the radio only", t_the_dialog_is_offered_for_the_radio_only);
    selftest_add("offline", "retry reconnects where there is no dialog", t_retry_reconnects_where_there_is_no_dialog);
}
