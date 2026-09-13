#include "page/settings.h"

#include "view/element.h"
#include "port/input.h"
#include "base/version.h"
#include "base/log.h"
#include "port/platform.h"
#include "base/prefs.h"
#include "model/posters.h"
#include "model/catalog.h"
#include "view/page_chrome.h"
#include "view/theme.h"

#include <stdio.h>

static const struct {
    const char *label, *help;
} kRow[SETTINGS_ROW_N] = {
    [SETTINGS_ROW_THEME]   = {"Colour", "The gradient the whole application is drawn in"},
    [SETTINGS_ROW_BITRATE] = {"Stream bitrate", "Lower survives a weak signal; takes effect on the next playback"},
    [SETTINGS_ROW_CPU]     = {"Processor speed", "222 MHz plays everything and lasts longer; 333 for a stubborn film"},
    [SETTINGS_ROW_REPORT]  = {"Report playback to server", "Marks films watched and resumes them on other clients"},
    [SETTINGS_ROW_LOG]     = {"Write a log", "Costs half a frame per line on the Memory Stick; off unless wanted"},
    [SETTINGS_ROW_CACHE]   = {"Clear image cache", "Deletes every cached poster and backdrop"},
    [SETTINGS_ROW_SIGNIN]  = {"Sign in again", "Asks the server for a new session over this connection"},
};

#define OPTION(row, kind, at, lo, hi) (void)ui_option(ui, (row), kRow[row].label, s->value[row], (kind), (at), (lo), (hi))
/* A switch carries no value: it already shows its state, and the word beside
   it says the same thing twice. */
#define TOGGLE(row, on) (void)ui_option(ui, (row), kRow[row].label, 0, UI_OPTION_SWITCH, (on), 0, 1)

/* The settings live in base/prefs; this only reads and writes them. */
typedef struct {
    char status[32];
    int  dirty;   /* a value changed and the file has not caught up */
    int  signing; /* a sign-in the viewer asked for, watched to its end */
    char value[SETTINGS_ROW_N][24];
} settings_state;

static settings_state *now(void) {
    static settings_state none;

    return screen_top_state() ? (settings_state *)screen_top_state() : &none;
}

const char *settings_page_value(int row) { return (row >= 0 && row < SETTINGS_ROW_N) ? now()->value[row] : ""; }

static void adjust(settings_state *s, int row, int by) {
    switch (row) {
    case SETTINGS_ROW_THEME:
        pref_set(PREF_THEME, pref(PREF_THEME) + by);
        ui_theme_set(pref(PREF_THEME));
        break;
    case SETTINGS_ROW_BITRATE: pref_set_step(PREF_BITRATE_KBPS, pref_step_now(PREF_BITRATE_KBPS) + by); break;
    case SETTINGS_ROW_CPU:
        pref_set_step(PREF_CPU_MHZ, pref_step_now(PREF_CPU_MHZ) + by);
        (void)platform_set_cpu_mhz(pref(PREF_CPU_MHZ));
        break;
    case SETTINGS_ROW_REPORT: pref_set(PREF_REPORT_PLAYBACK, !pref(PREF_REPORT_PLAYBACK)); break;
    case SETTINGS_ROW_LOG:
        pref_set(PREF_LOG_TO_CARD, !pref(PREF_LOG_TO_CARD));
        log_to_card(pref(PREF_LOG_TO_CARD));
        break;
    default: break;
    }
    /* A status left up while other dials turn describes something finished. */
    s->status[0] = 0;
    s->dirty     = 1;
}

/* Once, on the way out: saving on every adjust made a held dial one synchronous
   file write per repeat, and over usbhostfs a write can block for as long as
   it likes. */
static void leave(settings_state *s) {
    if (!s->dirty) return;
    s->dirty = 0;
    if (prefs_save() != 0) snprintf(s->status, sizeof(s->status), "could not save");
}

static void frame(ui_frame *ui, void *st) {
    settings_state           *s       = (settings_state *)st;
    static const ui_hint_item LEFT[]  = {{UI_BTN_LEFTRIGHT, "Change"}};
    static const ui_hint_item RIGHT[] = {{UI_BTN_CROSS, "Select"}, {UI_BTN_CIRCLE, "Back"}};
    const ui_theme           *t;
    int                       focus_row = -1, has;
    ui_id                     id        = ui_frame_focus(ui, &has);

    /* Before the values below are formatted, or a press shows a frame late. */
    if (has && (int)id < SETTINGS_ROW_N) focus_row = (int)id;
    if (focus_row >= 0) {
        /* Fired, not the edge, so a held direction steps at the repeat rate. */
        unsigned fired = ui_frame_fired(ui);

        if (fired == PAD_RIGHT)
            adjust(s, focus_row, 1);
        else if (fired == PAD_LEFT)
            adjust(s, focus_row, -1);
        else if (ui->pressed & PAD_CROSS) {
            if (focus_row == SETTINGS_ROW_REPORT || focus_row == SETTINGS_ROW_LOG)
                adjust(s, focus_row, 1);
            else if (focus_row == SETTINGS_ROW_CACHE) {
                /* Pictures, not listings: clearing the listing cache once
                   left nothing visibly changed. */
                unsigned bytes   = 0;
                int      records = 0;

                posters_cache_stats(&bytes, &records);
                posters_cache_clear();
                posters_clear();
                snprintf(s->status, sizeof(s->status), "Freed %u KB", bytes / 1024u);
            } else if (focus_row == SETTINGS_ROW_SIGNIN) {
                session_retry();
                s->signing = 1;
                snprintf(s->status, sizeof(s->status), "Signing in");
            }
        }

        /* Left and right belong to the row while it has focus, so the frame
           must not also resolve them as a move. */
        if (fired == PAD_LEFT || fired == PAD_RIGHT) ui_frame_take_fired(ui);
    }
    t = ui_theme_now(); /* adjust() may have just changed it */

    snprintf(s->value[SETTINGS_ROW_THEME], sizeof(s->value[0]), "%s", t->name);
    snprintf(s->value[SETTINGS_ROW_BITRATE], sizeof(s->value[0]), "%d.%d Mbit/s", pref(PREF_BITRATE_KBPS) / 1000,
             (pref(PREF_BITRATE_KBPS) % 1000) / 100);
    snprintf(s->value[SETTINGS_ROW_CPU], sizeof(s->value[0]), "%d MHz", pref(PREF_CPU_MHZ));
    snprintf(s->value[SETTINGS_ROW_REPORT], sizeof(s->value[0]), "%s", pref(PREF_REPORT_PLAYBACK) ? "On" : "Off");
    snprintf(s->value[SETTINGS_ROW_LOG], sizeof(s->value[0]), "%s", pref(PREF_LOG_TO_CARD) ? "On" : "Off");
    {
        /* What is on the card, which is what the row offers to free. */
        unsigned bytes   = 0;
        int      records = 0;

        posters_cache_stats(&bytes, &records);
        if (records)
            snprintf(s->value[SETTINGS_ROW_CACHE], sizeof(s->value[0]), "%u KB in %d", bytes / 1024u, records);
        else
            snprintf(s->value[SETTINGS_ROW_CACHE], sizeof(s->value[0]), "empty");
    }
    snprintf(s->value[SETTINGS_ROW_SIGNIN], sizeof(s->value[0]), "%s", session_connected() ? "Signed in" : "Not signed in");

    if (s->signing && session_stage() != LIB_STAGE_STARTING && session_stage() != LIB_STAGE_LINKING &&
        session_stage() != LIB_STAGE_SIGNIN) {
        s->signing = 0;
        snprintf(s->status, sizeof(s->status), "%s", session_connected() ? "Signed in" : "Could not sign in");
    }
    ui_page_busy(s->signing);

    ui_page_begin(&ui->layout, UI_PAGE_ROWS, "Settings", s->status[0] ? s->status : 0, -1);

    /* One-based: at zero the track is empty and the first theme reads as "no
       colour chosen". */
    OPTION(SETTINGS_ROW_THEME, UI_OPTION_SLIDER, pref(PREF_THEME) + 1, 1, UI_THEME_N);
    OPTION(SETTINGS_ROW_BITRATE, UI_OPTION_SLIDER, pref_step_now(PREF_BITRATE_KBPS), 1, pref_steps(PREF_BITRATE_KBPS));
    OPTION(SETTINGS_ROW_CPU, UI_OPTION_SLIDER, pref_step_now(PREF_CPU_MHZ), 1, pref_steps(PREF_CPU_MHZ));
    TOGGLE(SETTINGS_ROW_REPORT, pref(PREF_REPORT_PLAYBACK));
    TOGGLE(SETTINGS_ROW_LOG, pref(PREF_LOG_TO_CARD));
    OPTION(SETTINGS_ROW_CACHE, UI_OPTION_NONE, 0, 0, 0);
    OPTION(SETTINGS_ROW_SIGNIN, UI_OPTION_NONE, 0, 0, 0);

    ui_caption(&ui->layout, focus_row >= 0 ? kRow[focus_row].help : "");
    ui_gap(&ui->layout, 2);
    ui_divider(&ui->layout);
    ui_gap(&ui->layout, 2);

    ui_pair(&ui->layout, "Server", session_address()[0] ? session_address() : "not connected");
    ui_pair(&ui->layout, "Signed in as", jf_user()[0] ? jf_user() : "-");
    ui_pair(&ui->layout, "Version", POCKETFIN_VERSION);

    ui_page_end(&ui->layout, LEFT, 1, RIGHT, 2);
}

static int back(void *st) {
    leave((settings_state *)st);
    return 0; /* and then pop */
}

static void describe(void *st, char *out, unsigned n) {
    const settings_state *s = (const settings_state *)st;
    ui_id                 focus;
    int                   has = 0, i;
    unsigned              len;

    focus = ui_frame_focus(screen_ui(), &has);
    len   = (unsigned)snprintf(out, n, "status: %s\nserver: %s\nuser: %s\n", s->status[0] ? s->status : "none",
                             session_address()[0] ? session_address() : "none", jf_user()[0] ? jf_user() : "none");
    for (i = 0; i < SETTINGS_ROW_N && len < n; i++)
        len +=
            (unsigned)snprintf(out + len, n - len, "%s #%d %s: %s\n", (has && (int)focus == i) ? ">" : " ", i, kRow[i].label, s->value[i]);
}

const screen_def settings_page_screen = {"settings", sizeof(settings_state), 0, 0, frame, back, 0, describe};
