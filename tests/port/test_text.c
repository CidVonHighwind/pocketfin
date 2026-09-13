/* See port/text.h. */

#include "port/text.h"

#include "port/gfx.h"
#include "port/platform.h"
#include "tools/selftest.h"

#include <stdio.h>
#include <string.h>

/* Through gfx_capture_row, which both machines have: gfx_peek is the
 * desktop's alone, and the console's glyph atlas is what can really be wrong. */
static unsigned short g_ink[GFX_W];
static int            g_ink_row = -1;

/* After a frame drawn twice: the swap latches at the next vertical blank, so
 * a capture after one frame returns the previous one. */
static void ink_begin(void) { g_ink_row = -1; }

static unsigned short ink_at(int x, int y) {
    if (y != g_ink_row) {
        if (gfx_capture_row(y, g_ink) != 0) {
            g_ink_row = -1;
            return 0;
        }
        g_ink_row = y;
    }
    return (x >= 0 && x < GFX_W) ? g_ink[x] : 0;
}

/* The bake is data generated at build time, so it can be empty or short and
 * nothing at run time would say so. */
static int t_the_bake_covers_the_range(char *note, unsigned n) {
    int      face, code;
    unsigned drawable = 0;

    for (face = 0; face < TEXT_FACE_N; face++) {
        const text_face *f = &text_faces[face];

        if (!f->rec) {
            snprintf(note, n, "face %d has no glyph data at all", face);
            return 1;
        }
        if (!f->height || !f->ascent) {
            snprintf(note, n, "face %d has no line box", face);
            return 1;
        }
        for (code = 0; code < TEXT_GLYPHS; code++) {
            const text_glyph *g = &f->rec[code];

            /* A record past the atlas reads whatever follows it in memory. */
            if (g->ax + g->w > TEXT_ATLAS_W || g->ay + g->h > TEXT_ATLAS_H) {
                snprintf(note, n, "face %d glyph %d runs off the atlas", face, code + TEXT_FIRST);
                return 1;
            }
            if (g->w && g->h) drawable++;
        }
    }
    /* A face missing any printable ASCII silently loses letters. */
    for (face = 0; face < TEXT_FACE_N; face++)
        for (code = '!'; code <= '~'; code++)
            if (!text_faces[face].rec[code - TEXT_FIRST].adv4) {
                snprintf(note, n, "face %d cannot advance past '%c'", face, (char)code);
                return 1;
            }

    snprintf(note, n, "%d faces, %u-%u, %u drawable glyphs", TEXT_FACE_N, TEXT_FIRST, TEXT_LAST, drawable);
    return 0;
}

/* The atlas was once built for 95 glyphs while the font had 224, and accented
 * titles came out as question marks on the console and right on the PC. */
static int t_the_machine_holds_every_glyph(char *note, unsigned n) {
    unsigned glyphs = 0, bytes = 0, want = 0;
    int      face, code;

    if (text_start() != 0) {
        snprintf(note, n, "the glyphs did not load");
        return 1;
    }
    for (face = 0; face < TEXT_FACE_N; face++)
        for (code = 0; code < TEXT_GLYPHS; code++)
            if (text_faces[face].rec[code].w && text_faces[face].rec[code].h) want++;

    text_stats(&glyphs, &bytes);
    if (glyphs != want) {
        snprintf(note, n, "%u of %u glyphs made it to the machine", glyphs, want);
        return 1;
    }
    snprintf(note, n, "%u glyphs, %u bytes", glyphs, bytes);
    return 0;
}

static int t_accents_are_not_gaps(char *note, unsigned n) {
    static const char *accented = "\xC4\xD6\xDC\xE9\xF1"; /* A" O" U" e' n~ */
    int                plain, marked;

    plain  = text_width(TEXT_BODY, "AOUen");
    marked = text_width(TEXT_BODY, accented);

    if (marked <= 0) {
        snprintf(note, n, "five accented characters measured %d", marked);
        return 1;
    }
    /* Same letters underneath, so far apart means the accents fell through to
     * nothing. */
    if (marked < plain / 2) {
        snprintf(note, n, "accented %d px against plain %d px -- they are being dropped", marked, plain);
        return 1;
    }
    snprintf(note, n, "accented %d px, plain %d px", marked, plain);
    return 0;
}

static int t_width_is_what_drawing_advances(char *note, unsigned n) {
    static const char *s = "Pocketfin 123";
    int                said, went;

    if (text_start() != 0 || gfx_start() != 0) {
        snprintf(note, n, "no text or no panel on this build");
        return -1;
    }
    said = text_width(TEXT_BODY, s);

    gfx_frame_begin();
    went = text_draw(20, 40, TEXT_BODY, 0xFFFFFF, s) - 20;
    gfx_frame_end();

    if (said != went) {
        snprintf(note, n, "measured %d px, advanced %d px", said, went);
        return 1;
    }
    if (said < 20) {
        snprintf(note, n, "\"%s\" measured %d px -- too narrow to be real", s, said);
        return 1;
    }
    snprintf(note, n, "%d px, measured and drawn", said);
    return 0;
}

static int t_a_string_puts_ink_down(char *note, unsigned n) {
    int x, y, lit = 0, above = 0, w, h;

    if (text_start() != 0 || gfx_start() != 0) {
        snprintf(note, n, "no text or no panel on this build");
        return -1;
    }
    w = text_width(TEXT_BODY, "Pocketfin");
    h = text_height(TEXT_BODY);

    for (x = 0; x < 2; x++) { /* both buffers; see ink_begin */
        gfx_frame_begin();
        gfx_fill(0, 0, GFX_W, GFX_H, 0x000000);
        text_draw(20, 40, TEXT_BODY, 0xFFFFFF, "Pocketfin");
        gfx_frame_end();
    }

    ink_begin();
    for (y = 40; y < 40 + h; y++)
        for (x = 20; x < 20 + w; x++)
            if (ink_at(x, y)) lit++;
    for (y = 30; y < 40; y++)
        for (x = 20; x < 20 + w; x++)
            if (ink_at(x, y)) above++;

    if (lit < w) {
        snprintf(note, n, "%d lit pixels across %dx%d -- that is not text", lit, w, h);
        return 1;
    }
    if (above) {
        snprintf(note, n, "%d pixels above the line box", above);
        return 1;
    }
    snprintf(note, n, "%d lit pixels in %dx%d, none above", lit, w, h);
    return 0;
}

/* cap_top and cap_h are measured from the 'H' at bake time, so the renderer
 * is checked against the font rather than a typed number. Reading the offset
 * as a baseline distance rather than cell-top puts every ascender low while
 * short letters still look right. */
static int t_a_capital_sits_where_the_face_says(char *note, unsigned n) {
    const text_face *f = &text_faces[TEXT_BODY];
    int              x, y, top = -1, bottom = -1, at = 60;
    int              xmin = GFX_W, xmax = -1, rows = 0;

    if (text_start() != 0 || gfx_start() != 0) {
        snprintf(note, n, "no text or no panel on this build");
        return -1;
    }
    for (x = 0; x < 2; x++) { /* both buffers; see ink_begin */
        gfx_frame_begin();
        gfx_fill(0, 0, GFX_W, GFX_H, 0x000000);
        text_draw(20, at, TEXT_BODY, 0xFFFFFF, "H");
        gfx_frame_end();
    }

    ink_begin();
    for (y = at - 8; y < at + f->height + 8; y++) {
        int any = 0;

        for (x = 18; x < 20 + 16; x++)
            if (ink_at(x, y)) {
                if (top < 0) top = y;
                bottom = y;
                if (x < xmin) xmin = x;
                if (x > xmax) xmax = x;
                any = 1;
            }
        rows += any;
    }

    if (top < 0) {
        snprintf(note, n, "'H' drew nothing");
        return 1;
    }
    if (top != at + f->cap_top) {
        snprintf(note, n, "'H' starts at +%d, the face says +%d", top - at, f->cap_top);
        return 1;
    }
    if (bottom - top + 1 != f->cap_h) {
        snprintf(note, n, "'H' spans +%d..+%d (%d rows lit) x %d..%d, the face says %d tall", top - at, bottom - at, rows, xmin, xmax,
                 f->cap_h);
        return 1;
    }
    snprintf(note, n, "'H' at +%d, %d tall, as the face measured it", f->cap_top, f->cap_h);
    return 0;
}

/* The loop draws the readout every pass, including while the machine is away
 * and no list is open. text_draw was once unguarded, and engine commands with
 * no list open are undefined -- it survived a real standby exactly once. */
static int t_text_outside_a_frame_goes_nowhere(char *note, unsigned n) {
    int x, y, lit = 0;

    if (text_start() != 0 || gfx_start() != 0) {
        snprintf(note, n, "no text or no panel on this build");
        return -1;
    }
    for (x = 0; x < 2; x++) {
        gfx_frame_begin();
        gfx_fill(0, 0, GFX_W, GFX_H, 0x000000);
        gfx_frame_end();
    }

    if (gfx_in_frame()) {
        snprintf(note, n, "a frame is still open after gfx_frame_end");
        return 1;
    }
    /* Reaching the engine here is what took the console. */
    if (text_draw(20, 100, TEXT_BODY, 0xFFFFFF, "should not appear") != 20) {
        snprintf(note, n, "the pen advanced, so the glyphs were walked with no list open");
        return 1;
    }

    ink_begin();
    for (y = 95; y < 125; y++)
        for (x = 15; x < 200; x++)
            if (ink_at(x, y)) lit++;

    if (lit) {
        snprintf(note, n, "%d pixels drawn outside a frame", lit);
        return 1;
    }
    snprintf(note, n, "nothing drawn, and the engine was not asked");
    return 0;
}

static int t_what_a_line_costs(char *note, unsigned n) {
    static const char *s = "Pocketfin -- the readout is on the panel";
    unsigned           t0, took;
    int                i;

    if (text_start() != 0 || gfx_start() != 0) {
        snprintf(note, n, "no text or no panel on this build");
        return -1;
    }
    gfx_frame_begin();
    t0 = platform_clock_us();
    for (i = 0; i < 20; i++) text_draw(8, 8 + i, TEXT_BODY, 0xFFFFFF, s);
    took = platform_clock_us() - t0;
    gfx_frame_end();

    /* Text is drawn on every frame of every screen. 20 lines are 200 us here;
       the ceiling is forty times that, to catch a change of kind rather than
       of degree. */
    if (took > 8000u) {
        snprintf(note, n, "20 lines cost %u us -- most of a frame, for text alone", took);
        return 1;
    }
    snprintf(note, n, "%u us for 20 lines of %d characters", took, (int)strlen(s));
    return 0;
}

void test_text_register(void) {
    selftest_add("text", "the bake covers the range", t_the_bake_covers_the_range);
    selftest_add("text", "the machine holds every glyph", t_the_machine_holds_every_glyph);
    selftest_add("text", "accents are not gaps", t_accents_are_not_gaps);
    selftest_add("text", "width is what drawing advances", t_width_is_what_drawing_advances);
    selftest_add("text", "a string puts ink down", t_a_string_puts_ink_down);
    selftest_add("text", "a capital sits where the face says", t_a_capital_sits_where_the_face_says);
    selftest_add("text", "text outside a frame goes nowhere", t_text_outside_a_frame_goes_nowhere);
    selftest_add("text", "what a line costs", t_what_a_line_costs);
}
