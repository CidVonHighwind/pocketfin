/* The checks, on a PC: the core is only portable if something other than the
 * console compiles and runs it. */

#include "port/gfx.h"
#include "port/platform.h"
#include "base/log.h"
#include "base/prefs.h"
#include "tools/speed.h"
#include "view/theme.h"
#include "io/link.h"
#include "tools/remote.h"
#include "tools/selftest.h"

#include <stdio.h>
#include <string.h>

void tests_register_all(void);

#define CABLE_LOG "pocketfin-run.log"

static unsigned cable(const void *bytes, unsigned n) { return hostfs_append(CABLE_LOG, bytes, n); }
static int      cable_idle(void) { return hostfs_idle(CABLE_LOG); }

int main(int argc, char **argv) {
    char card[96];
    int  failed;
    /* A group prefix, or a comma-separated list of them. */
    const char *only = argc > 1 ? argv[1] : 0;

    /* Unbuffered: stdout is usually a pipe, and MSVC treats line buffering as
       full buffering. */
    setvbuf(stdout, NULL, _IONBF, 0);

    /* Before anything posts to it. */
    hostfs_start();
    log_cable(cable, cable_idle);

    snprintf(card, sizeof(card), "%spocketfin.log", platform_data_dir());
    if (log_open(card) != 0) printf("no card log at %s\n", card);

    log_line("POCKETFIN-READY");
    log_printf("log    %s" CABLE_LOG " + %s", platform_hostfs_dir(), card);
    if (only)
        log_printf("mode: checks, only \"%s\"", only);
    else
        log_line("mode: checks");

    /* This runner is the driver, so the marker goes down before the channel
     * looks for it. */
    hostfs_put("remote.on", "");

    prefs_load();
    log_to_card(pref(PREF_LOG_TO_CARD));
    ui_theme_set(pref(PREF_THEME));

    /* Its own channel: an application running beside it would eat the
       commands the checks wait on. */
    remote_use_channel("cmd-check.txt", "ack-check.txt", "state-check.txt");
    remote_start();

    /* A mode, not a check: it would fail on the world's state, not the code. */
    if (only && !strcmp(only, "speed")) {
        int rc = speed_run();

        log_close();
        return rc;
    }

    /* Nothing watches this panel, and pacing each frame to the refresh was
       over half the suite. */
    gfx_pace(0);
    tests_register_all();
    failed = only ? selftest_run_group(only) : selftest_run_all();
    log_close();

    /* A skip is not a pass. */
    return (failed || selftest_skipped()) ? 1 : 0;
}
