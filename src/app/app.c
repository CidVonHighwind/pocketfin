/* See app.h. */

#include "app/app.h"

#include "base/log.h"
#include "base/prefs.h"
#include "base/rect.h"
#include "base/standby.h"
#include "io/link.h"
#include "jelly/image.h"
#include "model/catalog.h"
#include "model/reports.h"
#include "io/http.h"
#include "model/stream.h"
#include "model/posters.h"
#include "page/boot.h"
#include "page/offline.h"
#include "port/decode.h"
#include "port/gfx.h"
#include "port/input.h"
#include "port/media.h"
#include "port/mem.h"
#include "port/platform.h"
#include "port/text.h"
#include "view/stack.h"
#include "view/theme.h"

#include <stdio.h>

static unsigned g_frames;
static unsigned g_started_us;

/* One frame's worth of pad from the command channel, OR-ed into the real one. */
static unsigned g_injected;

unsigned app_frames(void) { return g_frames; }
unsigned app_uptime_us(void) { return platform_clock_us() - g_started_us; }
void     app_inject(unsigned mask) { g_injected = mask; }

void app_start(void) {
    g_started_us = platform_clock_us();

    prefs_load();
    log_to_card(pref(PREF_LOG_TO_CARD));
    ui_theme_set(pref(PREF_THEME));
    log_printf("prefs: %s, theme %d, %d kbit/s, card log %s", prefs_file(), pref(PREF_THEME), pref(PREF_BITRATE_KBPS),
               pref(PREF_LOG_TO_CARD) ? "on" : "off");
    log_printf("cpu: asked %d MHz, running at %d", pref(PREF_CPU_MHZ), platform_set_cpu_mhz(pref(PREF_CPU_MHZ)));

    mem_report();

    /* The link is the only thing a standby takes away. */
    standby_watch(hostfs_release, hostfs_acquire, hostfs_verify);

    media_start();
    if (gfx_start() != 0) log_line("gfx: the panel did not come up");
    if (input_start() != 0) log_line("input: the pad did not start");
    if (text_start() != 0) log_line("text: the glyphs did not load");

    /* Not fatal: a screen that cannot reach a server says so. */
    if (library_start() != 0) log_printf("library: not started -- %s", session_error());
    if (reports_start() != 0) log_line("reports: no worker -- the server will not hear where the viewer is");
    /* Before any film: the console's network stack will not serve a thread
       made later. */
    if (stream_start() != 0) log_line("stream: no worker -- films will not play");

    {
        char pack[160];

        snprintf(pack, sizeof(pack), "%spocketfin-art.dat", platform_data_dir());
        if (posters_start(jf_poster, pack, 0) != 0) log_line("posters: no worker -- pages will stay on their placeholders");
    }

    screen_reset(&boot_page_screen);
    log_line("mode: app");
}

void app_draw(void) {
    unsigned held    = input_held() | g_injected;
    unsigned pressed = input_pressed() | g_injected;

    g_injected = 0;
    if (!gfx_up()) return;

    /* Over a film the whole frame is composed at 8888 on the decoder's own
       surface (port/decode.h); the page above draws its band exactly as it
       draws it over black. */
    {
        int   stride = 0;
        void *on     = decode_surface(&stride);

        if (on)
            gfx_frame_begin_on(on, stride);
        else
            gfx_frame_begin();

        screen_run_frame(ui_rect_make(0, 0, GFX_W, GFX_H), held, pressed);

        /* The first picture arrives mid-frame: asking again is a frame of
           film instead of a frame of black, every time a film starts. */
        if (!on) on = decode_surface(&stride);

        if (on) {
            gfx_frame_end_on();
            decode_show();
        } else {
            gfx_frame_end();
        }
    }
    g_frames++;
}

void app_frame(void) {
    input_sample();

    /* Only the radio can see a link that went away without a request having
       failed. The offline screen pops itself when the link is back. */
    library_watch_link();
    if (session_link_went()) screen_push(&offline_page_screen);

    /* Before the drawing: eviction is decided by which frame a picture was
       last asked in, and this pass is about to ask. */
    poster_frame();
    app_draw();
}

void app_stop(void) {
    /* Wake every blocked read first: a worker inside a socket read does not
       return until that read's own timeout, and several in a row is a console
       that looks hung. The sockets are woken, never closed -- nothing here
       owns them. */
    http_cancel();

    stream_stop();
    posters_stop();
    library_stop();
    reports_stop();
    gfx_stop();
    mem_release_all();
}
