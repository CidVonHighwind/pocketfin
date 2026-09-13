/* See corners.h. */

#include "base/corners.h"

POCKETFIN_ALIGN16 unsigned char corner_mask[CORNER_MAX_R + 1][CORNER_PITCH * CORNER_PITCH];
POCKETFIN_ALIGN16 unsigned char corner_stroke[CORNER_MAX_R + 1][CORNER_PITCH * CORNER_PITCH];
POCKETFIN_ALIGN16 unsigned char corner_cut[CORNER_MAX_R + 1][CORNER_PITCH * CORNER_PITCH];
POCKETFIN_ALIGN16 unsigned char corner_band[CORNER_MAX_R + 1][CORNER_PITCH * CORNER_PITCH];

static int g_built;

/* Sixteenths of a pixel: the grid the old build cut on, so the curve is the
 * one that was tuned. */
#define SUB 16

/* True area sampled on an 8x8 grid rather than a distance ramp: a ramp is
 * ragged on the diagonal, exactly where a corner is looked at. */
#define SS 8

static int disc_cov(int radius_sub, int x, int y, int r) {
    int inside = 0, sy, sx;

    for (sy = 0; sy < SS; sy++)
        for (sx = 0; sx < SS; sx++) {
            int px = x * SUB + (sx * 2 + 1) * SUB / (2 * SS);
            int py = y * SUB + (sy * 2 + 1) * SUB / (2 * SS);
            int dx = r * SUB - px;
            int dy = r * SUB - py;

            if (dx < 0) dx = 0;
            if (dy < 0) dy = 0;
            if (dx * dx + dy * dy <= radius_sub * radius_sub) inside++;
        }
    return inside;
}

void corners_build(void) {
    int r, x, y;

    if (g_built) return;
    g_built = 1;

    for (r = 1; r <= CORNER_MAX_R; r++) {
        for (y = 0; y < r; y++)
            for (x = 0; x < r; x++) {
                int outer = disc_cov(r * SUB, x, y, r);
                int inner = disc_cov((r - 1) * SUB, x, y, r);

                corner_mask[r][y * CORNER_PITCH + x]   = (unsigned char)(outer * 255 / (SS * SS));
                corner_stroke[r][y * CORNER_PITCH + x] = (unsigned char)((outer - inner) * 255 / (SS * SS));
                corner_cut[r][y * CORNER_PITCH + x]    = (unsigned char)(255 - outer * 255 / (SS * SS));
                /* Antialiased against the inner disc; a binary band makes
                 * the arc a staircase. */
                corner_band[r][y * CORNER_PITCH + x] = (unsigned char)(255 - inner * 255 / (SS * SS));
            }
    }
}

unsigned corners_ramp(unsigned a, unsigned b, int num, int den) {
    unsigned out = 0;
    int      i;

    if (den <= 0) return a;
    for (i = 0; i < 3; i++) {
        int sh = i * 8;
        int ca = (int)((a >> sh) & 0xFF), cb = (int)((b >> sh) & 0xFF);

        out |= (unsigned)(ca + (cb - ca) * num / den) << sh;
    }
    return out;
}

int corners_clamp_r(int w, int h, int r) {
    int half = (w < h ? w : h) / 2;

    if (r > CORNER_MAX_R) r = CORNER_MAX_R;
    if (r > half) r = half;
    return r;
}
