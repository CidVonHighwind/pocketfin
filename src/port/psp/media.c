/* See port/media.h. */

#include "port/media.h"

#include "base/log.h"
#include "port/gfx.h"
#include "port/jpeg.h"
#include "port/platform.h"

#include "video_decoder.h"

#include <pspge.h>

#include <string.h>

#define SURF_STRIDE 512
#define PANEL_N     2

#define PICTURE_BYTES ((unsigned)(VIDEO_STRIDE * VIDEO_HEIGHT * 4))
#define PANEL_BYTES   ((unsigned)(SURF_STRIDE * GFX_H * 4))

#define MODE_IDLE 0
#define MODE_FILM 1

static int g_mode;

/* Held by a still for its whole decode, taken once by a film on the way in. */
static platform_lock *g_engine;
static volatile int   g_still;

static platform_lock *engine(void) {
    if (!g_engine) g_engine = platform_lock_new("media");
    return g_engine;
}

void media_start(void) { engine(); }

static void *uncached(void *p) { return (void *)(0x40000000u | (uintptr_t)p); }

static unsigned char *panel_at(unsigned char *base, int i) { return base + PICTURE_BYTES + (size_t)i * PANEL_BYTES; }

int media_film_take(media_film *out) {
    unsigned char *v = (unsigned char *)sceGeEdramGetAddr();
    int            i, again = g_mode == MODE_FILM;

    if (!out) return -1;
    if (PICTURE_BYTES + PANEL_N * PANEL_BYTES > (unsigned)sceGeEdramGetSize()) return -1;

    if (!again) {
        /* Reasoned about, not measured: if this line never appears, the
           exclusion guards a race that does not occur. */
        if (g_still) log_line("media: a film waited for a still to leave the engine");

        platform_lock_take(engine());
        g_mode = MODE_FILM;
        platform_lock_give(engine());

        /* sceMpegCreate fails while the JPEG unit holds the engine, with a
           status nobody can trace back to a poster. */
        jpeg_finish();
    }

    out->picture = uncached(v);
    out->count   = PANEL_N;
    out->stride  = SURF_STRIDE;
    for (i = 0; i < PANEL_N; i++) {
        out->panel[i]   = panel_at(v, i);
        out->panel_u[i] = uncached(out->panel[i]);
    }

    /* A second take is the same claim: clearing again would wipe the film
       already drawn there. Otherwise these are the browser's pages. */
    if (again) return 0;
    memset(out->picture, 0, PICTURE_BYTES);
    for (i = 0; i < PANEL_N; i++) memset(out->panel_u[i], 0, PANEL_BYTES);
    return 0;
}

void media_film_give(void) {
    unsigned char *v;
    int            i;

    if (g_mode != MODE_FILM) return;

    /* Here, at their size: gfx blacks only a 5-6-5 page, half of one of
       these, and the rest came back as noise at the wrong depth. */
    v = (unsigned char *)sceGeEdramGetAddr();
    for (i = 0; i < PANEL_N; i++) memset(uncached(panel_at(v, i)), 0, PANEL_BYTES);

    gfx_panel_restore();
    g_mode = MODE_IDLE;
}

int media_film_has_it(void) { return g_mode == MODE_FILM; }

int media_still_begin(void) {
    if (g_mode == MODE_FILM) return -1;
    if (!engine()) return -1;

    platform_lock_take(engine());
    /* Asked again holding the lock: a film may have taken the mode since. */
    if (g_mode == MODE_FILM) {
        platform_lock_give(engine());
        return -1;
    }
    g_still = 1;
    return 0;
}

void media_still_end(void) {
    g_still = 0;
    platform_lock_give(g_engine);
}
