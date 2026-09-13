/* See port/media.h.
 *
 * Nothing is shared on this machine, but the checks in tests/port are written
 * against the console's rules, so this keeps the state machine and hands out
 * distinct buffers rather than answering yes to everything. */

#include "port/media.h"

#include <string.h>

#define PANEL_N     2
#define FAKE_STRIDE 512
#define FAKE_ROWS   272
#define FAKE_BYTES  ((size_t)FAKE_STRIDE * FAKE_ROWS * 4)

static unsigned char g_picture[FAKE_BYTES];
static unsigned char g_panel[PANEL_N][FAKE_BYTES];

static int g_mode;

void media_start(void) {}

int media_film_take(media_film *out) {
    int i;

    if (!out) return -1;

    /* A second take is the same claim: it must not clear what the film has
       already drawn. */
    if (!g_mode) {
        memset(g_picture, 0, sizeof(g_picture));
        memset(g_panel, 0, sizeof(g_panel));
    }
    g_mode = 1;

    out->picture = g_picture;
    out->count   = PANEL_N;
    out->stride  = FAKE_STRIDE;
    for (i = 0; i < PANEL_N; i++) {
        out->panel[i]   = g_panel[i];
        out->panel_u[i] = g_panel[i];
    }
    return 0;
}

void media_film_give(void) { g_mode = 0; }
int  media_film_has_it(void) { return g_mode; }

int media_still_begin(void) { return g_mode ? -1 : 0; }

void media_still_end(void) {}
