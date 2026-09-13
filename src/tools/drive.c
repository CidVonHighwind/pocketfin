/* See drive.h. */

#include "tools/drive.h"

#include "app/app.h"
#include "base/log.h"
#include "base/prefs.h"
#include "model/catalog.h"
#include "port/platform.h"
#include "tools/pages.h"
#include "tools/remote.h"
#include "tools/shot.h"
#include "view/frame.h"
#include "view/stack.h"

#include <stdio.h>
#include <stdlib.h>

/* An open channel says a script MAY drive this run, not that nobody is at the
   console: app mode is a person with the cable in, who can work a dialog. The
   check run refuses the picker for itself. */
void drive_start(void) {
    remote_start();
    platform_picker_offer(1);
}

static void publish_status(void) {
    char     s[REMOTE_STATE_MAX];
    unsigned up = app_uptime_us();
    int      len;

    len = snprintf(s, sizeof(s),
                   "up %u.%03u s\nframes %u\npage %s\ndepth %d\n"
                   "stage %s\nlost %d\n",
                   up / 1000000u, (up / 1000u) % 1000u, app_frames(), screen_top_name(), screen_depth(),
                   session_stage_text(session_stage()), session_lost());

    /* A driver pressing blind can't tell "moved" from "wrapped around" on a
       rail with no ends. */
    if (len > 0 && (unsigned)len < sizeof(s)) screen_describe(s + len, (unsigned)sizeof(s) - (unsigned)len);
    remote_publish(s);
}

int drive_serve(void) {
    char arg[REMOTE_ARG_MAX];

    switch (remote_take(arg, sizeof(arg))) {
    case REMOTE_NONE: return 0;

    case REMOTE_PING: break;

    case REMOTE_STATUS: publish_status(); break;

    case REMOTE_PAGE:
        if (!pages_go(arg)) log_printf("page: \"%s\" is none of %s", arg, pages_names());
        break;

    case REMOTE_FOCUS: (void)ui_frame_focus_index(screen_ui(), atoi(arg)); break;

    case REMOTE_BACK: screen_back(); break;

    /* Drawn here, before the ack, so an ack means the press reached the
       screen rather than that the word was understood. */
    case REMOTE_PRESS: {
        unsigned mask = remote_pad_named(arg);

        if (!mask) log_printf("press: \"%s\" is not a button", arg);
        app_inject(mask);
        app_draw();
        break;
    }

    /* Twice: the swap latches at the next vertical blank, so a capture after
       one frame reads the buffer that is not on screen. */
    case REMOTE_SHOT:
        app_draw();
        app_draw();
        if (shot_write(arg[0] ? arg : "pocketfin-shot.ppm") != 0) log_line("shot: the picture did not land");
        break;

    case REMOTE_QUIT: platform_note_quit(); break;

    /* Acked anyway: an ack that never comes reads exactly like a hang. */
    default: log_printf("remote: nothing here does \"%s\"", arg); break;
    }
    remote_done();

    /* Serving one can block -- a shot streams 391 kB through the cable from
       this thread -- and that gap is the harness's, not the application's. */
    screen_timing_skip_gap();
    return 1;
}
