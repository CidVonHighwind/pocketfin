/* text.h, for a PC: the same atlas, blended by the CPU instead of the engine. */

#include "port/text.h"

#include "port/gfx.h"

static int      g_up;
static unsigned g_packed;

/* gfx.c owns the buffer; a glyph is coverage rather than a rectangle. */
void gfx_blend(int x, int y, unsigned rgb, unsigned char coverage);

void text_stats(unsigned *glyphs, unsigned *bytes) {
    if (glyphs) *glyphs = g_packed;
    if (bytes) *bytes = TEXT_ATLAS_W * TEXT_ATLAS_H;
}

int text_height(text_face_id face) {
    if (face < 0 || face >= TEXT_FACE_N) return 0;
    return text_faces[face].height;
}

int text_start(void) {
    int i, j;

    if (g_up) return 0;
    g_packed = 0;
    for (i = 0; i < TEXT_FACE_N; i++)
        for (j = 0; j < TEXT_GLYPHS; j++)
            if (text_faces[i].rec[j].w && text_faces[i].rec[j].h) g_packed++;

    if (g_packed == 0) return -1;
    g_up = 1;
    return 0;
}

int text_width(text_face_id face, const char *s) {
    const text_face *f;
    unsigned         adv4 = 0;

    if (face < 0 || face >= TEXT_FACE_N || !s) return 0;
    f = &text_faces[face];
    for (; *s; s++) {
        unsigned c = (unsigned char)*s;

        if (c < TEXT_FIRST || c > TEXT_LAST) continue;
        adv4 += f->rec[c - TEXT_FIRST].adv4;
    }
    return (int)((adv4 + 2) / 4);
}

int text_draw(int x, int y, text_face_id face, unsigned rgb, const char *s) {
    const text_face *f;
    unsigned         adv4 = 0;

    /* Inside a frame, like every fill: the loop draws the readout the whole
     * time the machine is down. */
    if (!g_up || !gfx_in_frame()) return x;
    if (face < 0 || face >= TEXT_FACE_N || !s) return x;
    f = &text_faces[face];

    for (; *s; s++) {
        unsigned          c = (unsigned char)*s;
        const text_glyph *g;
        int               gx, gy, px, py;

        if (c < TEXT_FIRST || c > TEXT_LAST) continue;
        g  = &f->rec[c - TEXT_FIRST];
        gx = x + (int)((adv4 + 2) / 4) + g->bx;
        gy = y + g->by;

        for (py = 0; py < g->h; py++)
            for (px = 0; px < g->w; px++) gfx_blend(gx + px, gy + py, rgb, text_atlas_start[(g->ay + py) * TEXT_ATLAS_W + g->ax + px]);
        adv4 += g->adv4;
    }
    return x + (int)((adv4 + 2) / 4);
}

int text_draw_glyph(int x, int y, text_face_id face, unsigned rgb, int ch) {
    const text_face  *f;
    const text_glyph *g;
    unsigned          c = (unsigned)ch & 0xFFu;
    int               gx, gy, px, py;

    if (!g_up || !gfx_in_frame()) return 0;
    if (face < 0 || face >= TEXT_FACE_N) return 0;
    if (c < TEXT_FIRST || c > TEXT_LAST) return 0;

    f  = &text_faces[face];
    g  = &f->rec[c - TEXT_FIRST];
    gx = x + g->bx;
    gy = y + g->by;

    for (py = 0; py < g->h; py++)
        for (px = 0; px < g->w; px++) gfx_blend(gx + px, gy + py, rgb, text_atlas_start[(g->ay + py) * TEXT_ATLAS_W + g->ax + px]);
    return (int)((g->adv4 + 2) / 4);
}
