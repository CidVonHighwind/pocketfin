#include "page/settings.h"

#include "view/element.h"
#include "port/gfx.h"
#include "port/input.h"
#include "view/layout.h"
#include "view/page_chrome.h"
#include "view/stack.h"
#include "tools/selftest.h"
#include "test_page.h"

#include "../base/test_prefs.h"
#include "tools/shot.h"
#include "port/platform.h"
#include "model/catalog.h"
#include "base/prefs.h"
#include "port/text.h"
#include "view/theme.h"

#include <stdio.h>
#include <string.h>

int page_panel(char *note, unsigned n) {
    if (gfx_start() != 0 || text_start() != 0) {
        snprintf(note, n, "the panel or the font did not come up");
        return -1;
    }
    ui_theme_set(0);
    return 0;
}

void page_step(unsigned mask) {
    gfx_frame_begin();
    screen_run_frame(ui_rect_make(0, 0, GFX_W, GFX_H), mask, mask);
    gfx_frame_end();
}

/* -1 leaves the cursor wherever the page put it. */
static int at_row(char *note, unsigned n, int row) {
    if (page_panel(note, n) != 0) return -1;
    screen_reset(&settings_page_screen);
    page_step(0);
    if (row >= 0) {
        ui_frame_focus_set(screen_ui(), (ui_id)row);
        page_step(0);
    }
    return 0;
}

static int t_the_settings_page_draws(char *note, unsigned n) {
    if (at_row(note, n, -1) != 0) return -1;

    /* A second frame: the swap latches at the end of a frame, so one frame
       then a capture reads the buffer that is not on screen. */
    page_step(0);
    if (shot_write("settings.ppm") != 0) {
        snprintf(note, n, "the picture did not land");
        return 1;
    }
    if (ui_frame_entries(screen_ui()) != SETTINGS_ROW_N) {
        snprintf(note, n, "%d rows registered, wanted %d", ui_frame_entries(screen_ui()), SETTINGS_ROW_N);
        return 1;
    }
    return 0;
}

/* Every row is focused in turn, because the help line is drawn only for the
   focused one; measured with nothing focused, the page once passed while
   "Signed in as" was drawn over the hint bar. */
static int t_the_page_fits_the_body(char *note, unsigned n) {
    int row, worst = -1, worst_used = 0, body = 0, spare = 9999;

    for (row = 0; row < SETTINGS_ROW_N; row++) {
        int used;

        if (at_row(note, n, row) != 0) return -1;

        /* The body column is still the open box when the page returns. */
        used = ui_used(&screen_ui()->layout);
        body = ui_here(&screen_ui()->layout).h;
        if (body - used < spare) {
            spare      = body - used;
            worst      = row;
            worst_used = used;
        }
    }

    if (spare < 0) {
        snprintf(note, n, "on row %d the page puts %d px into a %d px body -- %d over", worst, worst_used, body, -spare);
        return 1;
    }
    snprintf(note, n, "%d rows all fit; row %d is the tightest at %d px in %d, %d to spare", SETTINGS_ROW_N, worst, worst_used, body,
             spare);
    return 0;
}

static int t_down_walks_the_rows(char *note, unsigned n) {
    int i, has;

    if (at_row(note, n, SETTINGS_ROW_THEME) != 0) return -1;

    for (i = 1; i < SETTINGS_ROW_N; i++) {
        page_step(PAD_DOWN);
        page_step(0);
        if ((int)ui_frame_focus(screen_ui(), &has) != i || !has) {
            snprintf(note, n, "down %d times reached row %d, wanted %d", i, (int)ui_frame_focus(screen_ui(), &has), i);
            return 1;
        }
    }
    snprintf(note, n, "down reached all %d rows in order", SETTINGS_ROW_N);
    return 0;
}

static int t_right_changes_the_value_and_stays(char *note, unsigned n) {
    char before[24];
    int  has;

    if (at_row(note, n, SETTINGS_ROW_BITRATE) != 0) return -1;
    /* Off the top step, where the viewer's saved bitrate may sit. */
    page_step(PAD_LEFT);

    snprintf(before, sizeof(before), "%s", settings_page_value(SETTINGS_ROW_BITRATE));
    page_step(PAD_RIGHT);

    if (strcmp(before, settings_page_value(SETTINGS_ROW_BITRATE)) == 0) {
        snprintf(note, n, "right left the bitrate at %s", before);
        return 1;
    }
    if ((int)ui_frame_focus(screen_ui(), &has) != SETTINGS_ROW_BITRATE) {
        snprintf(note, n, "right moved the cursor to row %d", (int)ui_frame_focus(screen_ui(), &has));
        return 1;
    }
    snprintf(note, n, "%s became %s, cursor stayed", before, settings_page_value(SETTINGS_ROW_BITRATE));
    return 0;
}

/* The theme row redraws the whole application, so the check is the theme
   changing rather than the row's own text. */
static int t_the_colour_row_changes_the_theme(char *note, unsigned n) {
    int before;

    if (at_row(note, n, SETTINGS_ROW_THEME) != 0) return -1;

    before = ui_theme_get();
    page_step(PAD_RIGHT);
    if (ui_theme_get() == before) {
        snprintf(note, n, "right on Colour left the theme at %d", before);
        return 1;
    }
    snprintf(note, n, "theme %d became %d", before, ui_theme_get());
    return 0;
}

static int t_cross_toggles_a_switch(char *note, unsigned n) {
    char before[24];

    if (at_row(note, n, SETTINGS_ROW_LOG) != 0) return -1;

    snprintf(before, sizeof(before), "%s", settings_page_value(SETTINGS_ROW_LOG));
    page_step(PAD_CROSS);

    if (strcmp(before, settings_page_value(SETTINGS_ROW_LOG)) == 0) {
        snprintf(note, n, "cross left the log switch at %s", before);
        return 1;
    }
    snprintf(note, n, "%s became %s", before, settings_page_value(SETTINGS_ROW_LOG));
    return 0;
}

/* Read back from the file: a page that kept the value only in memory once
   persisted nothing. */
static int t_a_change_reaches_the_file(char *note, unsigned n) {
    char     path[128];
    char     body[512];
    FILE    *f;
    int      was, now;
    unsigned got = 0;

    if (at_row(note, n, SETTINGS_ROW_CPU) != 0) return -1;

    was = pref(PREF_CPU_MHZ);
    /* Whichever way there is room to move on a two-step dial. */
    page_step(was == 222 ? PAD_RIGHT : PAD_LEFT);
    now = pref(PREF_CPU_MHZ);

    if (now == was) {
        snprintf(note, n, "the row did not change the clock at all (%d)", was);
        return 1;
    }

    /* The cable's directory and the data directory are both candidates;
       guessing once failed this check against a file written correctly. */
    snprintf(path, sizeof(path), "%s", prefs_file());

    /* Not yet: a held dial repeats, and a write per repeat is eight
       synchronous writes from the drawing thread, across usbhostfs on the
       console, where one can block for as long as it likes. */
    prefs_file_keep();
    remove(path);
    page_step(0);
    if ((f = fopen(path, "rb")) != 0) {
        fclose(f);
        snprintf(note, n, "%s was written while the page was still open", path);
        return 1;
    }

    /* One write, on the one way out. */
    page_step(PAD_CIRCLE);
    f = fopen(path, "rb");
    if (!f) {
        snprintf(note, n, "no %s after leaving -- the change never left the screen", path);
        return 1;
    }
    got       = (unsigned)fread(body, 1, sizeof(body) - 1, f);
    body[got] = 0;
    fclose(f);

    {
        char want[48];

        snprintf(want, sizeof(want), "cpu_mhz %d", now);
        if (!strstr(body, want)) {
            snprintf(note, n, "%s does not say \"%s\" -- the page kept it in memory", path, want);
            return 1;
        }
    }

    /* This check writes over the settings the application runs from; leaving
       no file behind once reset them all to default, and playback reporting
       was found switched off with nothing to say so. */
    if (at_row(note, n, SETTINGS_ROW_CPU) != 0) return -1;
    page_step(was == 222 ? PAD_LEFT : PAD_RIGHT);
    page_step(PAD_CIRCLE);
    prefs_file_put_back();
    snprintf(note, n, "%d MHz became %d; nothing written until circle, then %s says so", was, now, path);
    return 0;
}

/* Both lines were once constants, so the page showed somebody else's server
   with nothing on screen to say the real one was different. */
static int t_the_connection_is_reported_not_invented(char *note, unsigned n) {
    const char *at = session_address();

    if (at_row(note, n, -1) != 0) return -1;

    /* With no worker running the honest answer is an empty address. */
    if (at[0] && strchr(at, ':') == 0) {
        snprintf(note, n, "the address \"%s\" has no port", at);
        return 1;
    }
    if (strcmp(at, "192.168.0.10:8096") == 0 || strcmp(jf_user(), "demo") == 0) {
        snprintf(note, n, "the page is still reporting the made-up connection (\"%s\" as \"%s\")", at, jf_user());
        return 1;
    }
    snprintf(note, n, "address \"%s\", user \"%s\" -- both from the library", at[0] ? at : "(none)", jf_user()[0] ? jf_user() : "(none)");
    return 0;
}

void test_settings_page_register(void) {
    selftest_add("settings", "the connection is reported not invented", t_the_connection_is_reported_not_invented);
    selftest_add("settings", "the settings page draws", t_the_settings_page_draws);
    selftest_add("settings", "the page fits the body", t_the_page_fits_the_body);
    selftest_add("settings", "down walks the rows", t_down_walks_the_rows);
    selftest_add("settings", "right changes the value and stays", t_right_changes_the_value_and_stays);
    selftest_add("settings", "the colour row changes the theme", t_the_colour_row_changes_the_theme);
    selftest_add("settings", "cross toggles a switch", t_cross_toggles_a_switch);
    selftest_add("settings", "a change reaches the file", t_a_change_reaches_the_file);
}
