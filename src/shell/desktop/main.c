/* Pocketfin, a Jellyfin client for the PlayStation Portable.
 * Copyright (C) 2026 CidVonHighwind
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. It is distributed WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License in LICENSE for details.
 *
 * The application, in a window on this machine. */

#include "port/desktop/window.h"

#include "app/app.h"
#include "tools/drive.h"
#include "base/log.h"
#include "io/link.h"
#include "io/net.h"
#include "port/gfx.h"
#include "port/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CABLE_LOG "pocketfin-pc.log"

static void present(void) {
    static unsigned frame[GFX_H][GFX_W];
    int             y;

    /* At full depth: while a film runs the frame was composed at 8888, and
       the point of that is not to quantise it. */
    for (y = 0; y < GFX_H; y++)
        if (gfx_capture_row8888(y, frame[y]) != 0) return;

    window_present8888(&frame[0][0]);
}

static unsigned cable(const void *bytes, unsigned n) { return hostfs_append(CABLE_LOG, bytes, n); }
static int      cable_idle(void) { return hostfs_idle(CABLE_LOG); }

int main(int argc, char **argv) {
    int slow = 0, i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--slow") == 0 && i + 1 < argc)
            slow = atoi(argv[++i]);
        else if (strncmp(argv[i], "--slow=", 7) == 0)
            slow = atoi(argv[i] + 7);
        else {
            printf("\n  usage: pocketfin-pc [--slow N]\n"
                   "    --slow N   pretend the link is N times slower than the\n"
                   "               console's, so what is fetched when is visible\n\n");
            return 2;
        }
    }

    setvbuf(stdout, NULL, _IONBF, 0);

    /* Before anything posts to it. */
    if (hostfs_start() != 0) printf("no log in %s\n", platform_hostfs_dir());
    log_cable(cable, cable_idle);

    /* Before app_start(), which first reaches the server. */
    if (slow) net_pretend_slow(slow);

    if (window_open() != 0) {
        printf("no window\n");
        return 2;
    }

    log_line("POCKETFIN-READY");
    log_line("mode: windowed");
    printf("\n  Pocketfin -- arrows move, S opens, D goes back, Enter is START, Esc quits\n\n");

    app_start();
    drive_start();

    while (!window_closed() && !platform_should_quit()) {
        window_pump();
        drive_serve();
        app_frame();
        present();
    }

    app_stop();
    log_close();
    return 0;
}
