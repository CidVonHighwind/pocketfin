/* See shot.h. No file operations: the link thread owns the transport. */

#include "tools/shot.h"

#include "port/gfx.h"
#include "io/link.h"
#include "base/log.h"
#include "port/platform.h"

#include <stdio.h>
#include <string.h>

/* A row is 1,440 bytes into an 8 kB queue, so a healthy cable never spends
 * anything like this. Reaching it means the transport has stopped. */
#define ROOM_BOUND_US 2000000u

static unsigned g_rows, g_lost;

void shot_stats(unsigned *rows, unsigned *lost) {
    if (rows) *rows = g_rows;
    if (lost) *lost = g_lost;
}

/* 1 once the link has written everything handed to it. */
static int drained(const char *leaf) {
    unsigned t0 = platform_clock_us();

    while (!hostfs_idle(leaf)) {
        if (platform_clock_us() - t0 > ROOM_BOUND_US) return 0;
        platform_sleep_us(1000);
    }
    return 1;
}

/* The bound restarts whenever the link takes bytes. */
static int push(const char *leaf, const unsigned char *b, unsigned n) {
    unsigned t0 = platform_clock_us();

    while (n) {
        unsigned took = hostfs_append(leaf, b, n);

        if (took) {
            b += took;
            n -= took;
            t0 = platform_clock_us();
            continue;
        }
        if (platform_clock_us() - t0 > ROOM_BOUND_US) return -1;
        platform_sleep_us(1000);
    }
    return 0;
}

int shot_write(const char *leaf) {
    /* Static, not on the stack: 2.4 kB of it, and the caller is a frame loop
     * whose thread was not sized for pictures. */
    static unsigned char  rgb[GFX_W * 3];
    static unsigned       row[GFX_W];
    char                  head[64];
    int                   y, x;

    if (!leaf || !hostfs_up() || !gfx_up()) return -1;

    /* Truncated first: an append onto yesterday's picture is a file that
     * decodes and is wrong, which is worse than one that does not. */
    hostfs_put(leaf, "");
    if (!drained(leaf)) {
        log_line("shot: the cable would not empty the old picture");
        return -1;
    }

    snprintf(head, sizeof(head), "P6\n%d %d\n255\n", GFX_W, GFX_H);
    if (push(leaf, (const unsigned char *)head, (unsigned)strlen(head)) != 0) {
        log_line("shot: the cable would not take the header");
        return -1;
    }

    for (y = 0; y < GFX_H; y++) {
        /* Full depth: the 5-6-5 seam refuses a film's 8888 frame. */
        if (gfx_capture_row8888(y, row) != 0) {
            log_printf("shot: row %d could not be read", y);
            g_lost++;
            return -1;
        }
        for (x = 0; x < GFX_W; x++) {
            unsigned p = row[x];

            rgb[x * 3 + 0] = (unsigned char)(p & 0xFF);
            rgb[x * 3 + 1] = (unsigned char)((p >> 8) & 0xFF);
            rgb[x * 3 + 2] = (unsigned char)((p >> 16) & 0xFF);
        }
        if (push(leaf, rgb, sizeof(rgb)) != 0) {
            log_printf("shot: the cable stopped taking bytes at row %d", y);
            g_lost++;
            return -1;
        }
        g_rows++;
    }

    /* Still queued means a script reading the file gets a picture that stops
     * half way down. */
    if (!drained(leaf)) {
        log_line("shot: the last rows never landed");
        return -1;
    }
    return 0;
}
