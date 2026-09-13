/* text.h, for the console: one paletted atlas linked into main memory, which
 * survives a suspend where EDRAM's contents cannot be assumed to. */

#include "port/text.h"

#include "port/gfx.h"
#include "base/log.h"
#include "port/platform.h"

#include <pspgu.h>

/* Float texture coordinates in TEXELS: the 16-bit integer form only maps to
 * texels when sceGuTexScale is 1.0, which this code never establishes. */
typedef struct {
    float        s, t;
    unsigned int c;
    float        x, y, z;
} tvert;

static int      g_up;
static unsigned g_packed;

void text_stats(unsigned *glyphs, unsigned *bytes) {
    if (glyphs) *glyphs = g_packed;
    if (bytes) *bytes = TEXT_ATLAS_W * TEXT_ATLAS_H;
}

int text_height(text_face_id face) {
    if (face < 0 || face >= TEXT_FACE_N) return 0;
    return text_faces[face].height;
}

/* Counts what the bake produced: an empty one reported ready is a run with
 * no text and no reason. */
int text_start(void) {
    int i, j;

    if (g_up) return 0;

    g_packed = 0;
    for (i = 0; i < TEXT_FACE_N; i++)
        for (j = 0; j < TEXT_GLYPHS; j++)
            if (text_faces[i].rec[j].w && text_faces[i].rec[j].h) g_packed++;

    if (g_packed == 0) {
        log_line("text: the bake produced no glyphs");
        return -1;
    }

    g_up = 1;
    log_printf("text: %u glyphs of %d, %dx%d atlas", g_packed, TEXT_FACE_N * TEXT_GLYPHS, TEXT_ATLAS_W, TEXT_ATLAS_H);
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

int text_draw(int x, int y, text_face_id face, unsigned rgb, const char *str) {
    const text_face *f;
    unsigned         adv4 = 0, colour;
    int              len = 0, drawn = 0, i;
    tvert           *v;

    /* A command issued with no list open is undefined. */
    if (!g_up || !gfx_in_frame()) return x;
    if (face < 0 || face >= TEXT_FACE_N || !str) return x;
    f = &text_faces[face];

    while (str[len]) len++;
    if (!len) return x;

    colour = 0xFF000000u | ((rgb & 0xFFu) << 16) | (rgb & 0xFF00u) | ((rgb >> 16) & 0xFFu);

    /* Through gfx.c, which caches the engine's state: set directly, the next
     * masked shape skips a setup it needed. */
    gfx_use_mask_texture();
    sceGuTexImage(0, TEXT_ATLAS_W, TEXT_ATLAS_H, TEXT_ATLAS_W, text_atlas_start);
    sceGuTexScale(1.0f / (float)TEXT_ATLAS_W, 1.0f / (float)TEXT_ATLAS_H);

    /* One draw call for the string: per glyph is a command pair for every
     * character of every frame. */
    v = (tvert *)sceGuGetMemory((unsigned)(sizeof(tvert) * 2 * len));
    if (!v) return x;

    for (i = 0; i < len; i++) {
        unsigned          c = (unsigned char)str[i];
        const text_glyph *g;
        tvert            *a2, *b2;
        int               gx, gy;

        if (c < TEXT_FIRST || c > TEXT_LAST) continue;
        g = &f->rec[c - TEXT_FIRST];

        if (g->w && g->h) {
            gx = x + (int)((adv4 + 2) / 4) + g->bx;
            gy = y + g->by;

            a2 = &v[drawn * 2 + 0];
            b2 = &v[drawn * 2 + 1];

            a2->s = (float)g->ax;
            a2->t = (float)g->ay;
            a2->c = colour;
            a2->x = (float)gx;
            a2->y = (float)gy;
            a2->z = 0.0f;

            b2->s = (float)(g->ax + g->w);
            b2->t = (float)(g->ay + g->h);
            b2->c = colour;
            b2->x = (float)(gx + g->w);
            b2->y = (float)(gy + g->h);
            b2->z = 0.0f;
            drawn++;
        }
        adv4 += g->adv4;
    }

    if (drawn) sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, drawn * 2, 0, v);

    return x + (int)((adv4 + 2) / 4);
}

int text_draw_glyph(int x, int y, text_face_id face, unsigned rgb, int ch) {
    const text_face  *f;
    const text_glyph *g;
    unsigned          c = (unsigned)ch & 0xFFu, colour;
    tvert            *v;

    if (!g_up || !gfx_in_frame()) return 0;
    if (face < 0 || face >= TEXT_FACE_N) return 0;
    if (c < TEXT_FIRST || c > TEXT_LAST) return 0;

    f = &text_faces[face];
    g = &f->rec[c - TEXT_FIRST];
    if (!g->w || !g->h) return (int)((g->adv4 + 2) / 4);

    colour = 0xFF000000u | ((rgb & 0xFFu) << 16) | (rgb & 0xFF00u) | ((rgb >> 16) & 0xFFu);

    gfx_use_mask_texture();
    sceGuTexImage(0, TEXT_ATLAS_W, TEXT_ATLAS_H, TEXT_ATLAS_W, text_atlas_start);
    sceGuTexScale(1.0f / (float)TEXT_ATLAS_W, 1.0f / (float)TEXT_ATLAS_H);

    v = (tvert *)sceGuGetMemory(2 * sizeof(tvert));
    if (!v) return 0;

    v[0].s = (float)g->ax;
    v[0].t = (float)g->ay;
    v[0].c = colour;
    v[0].x = (float)(x + g->bx);
    v[0].y = (float)(y + g->by);
    v[0].z = 0.0f;
    v[1].s = (float)(g->ax + g->w);
    v[1].t = (float)(g->ay + g->h);
    v[1].c = colour;
    v[1].x = (float)(x + g->bx + g->w);
    v[1].y = (float)(y + g->by + g->h);
    v[1].z = 0.0f;

    sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, v);
    return (int)((g->adv4 + 2) / 4);
}
