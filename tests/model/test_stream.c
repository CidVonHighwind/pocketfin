/* See model/stream.h. The only check that runs a film through the rings, the
 * clock and both workers together, so it needs the real server. */

#include "model/stream.h"

#include "jelly/api.h"
#include "port/decode.h"
#include "port/gfx.h"
#include "port/platform.h"
#include "tools/selftest.h"

#include "test_catalog.h"

#include <stdio.h>
#include <string.h>

/* Three seconds are buffered before the clock runs; this is two more. */
#define PICTURES  50
#define BOUND_US  40000000u
#define PAUSE_US  1000000u
#define RESUME_US 3000000u
#define FROM      (10ull * 60ull * ITEM_TICKS_PER_S)

static char   g_reply[4096];
static jf_buf g_rb = JF_BUF(g_reply);

/* Stops at `enough` when it is not 0, or when the film ends. */
static int pictures_for(unsigned us, int enough, int paused) {
    unsigned t0  = platform_clock_us();
    int      got = 0;

    while (platform_clock_us() - t0 < us && (!enough || got < enough)) {
        stream_picture pic;
        int            r = stream_frame(&pic, paused);

        if (r < 0) break;
        got += r;
        platform_sleep_us(16000);
    }
    return got;
}

static int t_a_film_plays_with_its_sound(char *note, unsigned n) {
    char               parent[JF_ID_LEN], title[ITEM_NAME_LEN], id[JF_ID_LEN], err[128];
    int                count = 0, at = 0, got, held = 0, resumed = 0;
    unsigned long long run_ticks;
    unsigned           t0, took_ms;
    lib_items          li;
    stream_stats       s;

    if (!decode_available()) {
        snprintf(note, n, "this build has no decoder");
        return -1;
    }
    if (gfx_start() != 0) {
        snprintf(note, n, "the panel did not come up");
        return -1;
    }
    if (real_listing(ITEM_KIND_MOVIE, parent, title, &count, &at, note, n) != 0) return -1;
    library_items(parent, title, ITEM_SORT_NAME, item_sort_default_descending(ITEM_SORT_NAME), ITEM_FILTER_ALL, &li);
    if (!li.rows || at >= li.n) {
        snprintf(note, n, "\"%s\" was not there to play from", title);
        return -1;
    }
    snprintf(id, sizeof(id), "%s", li.rows[at].id);
    run_ticks = li.rows[at].run_ticks;

    if (stream_start() != 0 || stream_open(id, run_ticks > 2 * FROM ? FROM : 0, run_ticks) != 0) {
        snprintf(note, n, "it would not open: %s", stream_error());
        stream_close();
        return 1;
    }

    t0      = platform_clock_us();
    got     = pictures_for(BOUND_US, PICTURES, 0);
    took_ms = (platform_clock_us() - t0) / 1000u;
    if (got >= PICTURES) {
        held    = pictures_for(PAUSE_US, 0, 1);
        resumed = pictures_for(RESUME_US, 1, 0);
    }

    stream_get_stats(&s);
    snprintf(err, sizeof(err), "%s", stream_error());
    /* Or the server keeps the transcode running under this device's id. */
    if (stream_session()[0]) (void)jf_stop_encoding(&g_rb, stream_session());
    stream_close();

    if (got < PICTURES) {
        snprintf(note, n, "%d of %d pictures in %u ms: %s", got, PICTURES, took_ms, err[0] ? err : "nothing failed");
        return 1;
    }
    if (s.aring_cap && !s.audio_frames) {
        snprintf(note, n, "%d pictures and the sound opened, but no frame of it played", got);
        return 1;
    }
    if (held) {
        snprintf(note, n, "%d pictures were handed over while paused", held);
        return 1;
    }
    if (!resumed) {
        snprintf(note, n, "no picture within %u ms of resuming", RESUME_US / 1000u);
        return 1;
    }
    snprintf(note, n, "%d pictures in %u ms, %u sound frames%s, %u stalls, held while paused", got, took_ms, s.audio_frames,
             s.aring_cap ? "" : " (no sound here)", s.stalls);
    return 0;
}

void test_stream_register(void) { selftest_add("stream", "a film plays with its sound", t_a_film_plays_with_its_sound); }
