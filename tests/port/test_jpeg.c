/* See port/jpeg.h. The header checks build their own bytes; the rest read
 * files scripts/fixtures.py stages on the link, and skip without them. */

#include "port/jpeg.h"

#include "base/align.h"
#include "port/gfx.h"
#include "io/link.h"
#include "tools/shot.h"
#include "port/text.h"
#include "tools/selftest.h"

#include <stdio.h>
#include <string.h>

#define JPEG_STAGED "poster.jpg"
/* Drawn, not a real poster: the assertions need markers at known places.
 * 100x150 because neither side is a multiple of 16, so the decoder is created
 * at 112x160 and writes its rows at that stride -- a shrink reading at the
 * image's own width shears the picture, and an aligned fixture cannot tell.
 * That fault shipped once and every check passed. */
#define JPEG_MARKERS "markers.jpg"

/* The ring one pixel outside the picture, so the two curves are concentric. */
#define UI_ART_RADIUS 3
#define UI_ART_EDGE_R (UI_ART_RADIUS + 1)
#define PAGE_BG       0xC02020u

static unsigned char  g_jpg[64 * 1024];
static unsigned char  g_scratch[JPEG_SCRATCH_BYTES(256, 384)];
static unsigned short g_out[256 * 384];

static int t_a_real_poster_decodes(char *note, unsigned n) {
    unsigned char  *jpg     = g_jpg;
    unsigned char  *scratch = g_scratch;
    unsigned short *out     = g_out;
    unsigned        got;
    int             w = 0, h = 0, gw = 0, gh = 0, rc;

    got = hostfs_up() ? hostfs_slurp(JPEG_MARKERS, (char *)jpg, sizeof(g_jpg)) : 0;
    if (!got) {
        snprintf(note, n, "no %s staged on the link", JPEG_MARKERS);
        return -1;
    }
    /* A JPEG cut short keeps a valid header and decodes to something, so EOI
     * is the only thing that says it all arrived. */
    if (got < 128 || jpg[got - 2] != 0xFF || jpg[got - 1] != 0xD9) {
        snprintf(note, n, "%u bytes and no end-of-image marker", got);
        return 1;
    }
    if (jpeg_probe(jpg, got, &w, &h) != 0) {
        snprintf(note, n, "probe refused it: %s", jpeg_error());
        return 1;
    }
    if (jpeg_init() != 0) {
        snprintf(note, n, "the decoder would not load: %s", jpeg_error());
        return 1;
    }
    rc = jpeg_decode_rgb565(jpg, got, out, 128, 192, &gw, &gh, scratch, sizeof(g_scratch));
    jpeg_finish();

    if (rc != 0) {
        snprintf(note, n, "%dx%d decode failed: %s", w, h, jpeg_error());
        return 1;
    }
    if (gw <= 0 || gh <= 0 || gw > 128 || gh > 192) {
        snprintf(note, n, "decoded to %dx%d", gw, gh);
        return 1;
    }
    /* A decoder returning garbage of the right size passes everything above.
     * markers.jpg carries a red block in the top-left and a green one near the
     * far corner. */
    {
        unsigned short tl   = out[16 * gw + 16];
        unsigned short br   = out[(gh - 16) * gw + (gw - 16)];
        int            tl_r = (tl & 0x1F) << 3, tl_g = ((tl >> 5) & 0x3F) << 2;
        int            br_r = (br & 0x1F) << 3, br_g = ((br >> 5) & 0x3F) << 2;

        if (tl_r < 160 || tl_g > 110) {
            snprintf(note, n, "the red marker read %02x,%02x", tl_r, tl_g);
            return 1;
        }
        if (br_g < 140 || br_r > 110) {
            snprintf(note, n, "the green marker read %02x,%02x", br_r, br_g);
            return 1;
        }
    }
    snprintf(note, n, "%dx%d to %dx%d, markers in place", w, h, gw, gh);
    return 0;
}

/* A poster fitted to 85x128 has no power-of-two side, and the engine encodes
 * texture size as a log2, so it is staged into the top-left of a 128x128
 * buffer. */
#define STAGE 128

/* 16-byte aligned: the engine's texture base wants it, and an unaligned
 * blit draws black on some builds and not others. */
static POCKETFIN_ALIGN16 unsigned short g_stage[STAGE * STAGE];

static int t_a_poster_reaches_the_panel(char *note, unsigned n) {
    unsigned char  *jpg     = g_jpg;
    unsigned char  *scratch = g_scratch;
    unsigned short *out     = g_out;
    unsigned        got;
    int             gw = 0, gh = 0, y, i;

    if (gfx_start() != 0) {
        snprintf(note, n, "no panel");
        return -1;
    }
    got = hostfs_up() ? hostfs_slurp(JPEG_STAGED, (char *)jpg, sizeof(g_jpg)) : 0;
    if (!got) {
        snprintf(note, n, "no %s staged on the link", JPEG_STAGED);
        return -1;
    }
    if (jpeg_init() != 0 || jpeg_decode_rgb565(jpg, got, out, STAGE, STAGE, &gw, &gh, scratch, sizeof(g_scratch)) != 0) {
        snprintf(note, n, "decode failed: %s", jpeg_error());
        jpeg_finish();
        return 1;
    }
    jpeg_finish();

    for (i = 0; i < STAGE * STAGE; i++) g_stage[i] = 0;
    for (y = 0; y < gh; y++) memcpy(g_stage + y * STAGE, out + y * gw, (unsigned)gw * sizeof(*out));
    gfx_wrote_pixels();

    for (i = 0; i < 2; i++) {
        gfx_frame_begin();
        gfx_fill(0, 0, GFX_W, GFX_H, PAGE_BG);
        if (!gfx_blit_part(24, 40, g_stage, STAGE, STAGE, gw, gh, UI_ART_RADIUS, PAGE_BG)) {
            snprintf(note, n, "the staged %dx%d was refused too", STAGE, STAGE);
            gfx_frame_end();
            return 1;
        }
        gfx_frame_end();
    }
    if (shot_write("poster-drawn.ppm") != 0) {
        snprintf(note, n, "the picture did not land");
        return 1;
    }
    snprintf(note, n, "decoded %dx%d, staged into %dx%d, drawn", gw, gh, STAGE, STAGE);
    return 0;
}

/* Real files at ten sizes, since the decoder's rules are about what a server
 * sends. Two are taller than the panel and must be refused. */
#define FIXTURES 10
#define CELL_W   88
#define CELL_H   96

static int draw_fixture_page(char *why, unsigned why_n, int *drawn, int *nodecode, int *noblit, int *missing, int *refused_right) {
    int  i;
    char names[FIXTURES][32];
    static const struct {
        int w, h;
    } want[FIXTURES] = {{128, 192}, {64, 96}, {100, 150}, {256, 384}, {91, 137}, {200, 300}, {480, 272}, {128, 128}, {32, 48}, {300, 200}};

    *drawn = *nodecode = *noblit = *missing = *refused_right = 0;
    for (i = 0; i < FIXTURES; i++) snprintf(names[i], sizeof(names[i]), "fixtures/%02d-%dx%d.jpg", i, want[i].w, want[i].h);

    gfx_frame_begin();
    gfx_fill(0, 0, GFX_W, GFX_H, PAGE_BG);
    text_draw(8, 4, TEXT_SMALL, 0x90A0B0u, "ten posters, ten sizes");

    for (i = 0; i < FIXTURES; i++) {
        int      col = i % 5, row = i / 5;
        int      x = 8 + col * (CELL_W + 6), y = 22 + row * (CELL_H + 30);
        int      gw = 0, gh = 0, sw, sh, yy;
        unsigned got;
        char     label[24];

        got = hostfs_up() ? hostfs_slurp(names[i], (char *)g_jpg, sizeof(g_jpg)) : 0;
        if (!got) {
            (*missing)++;
            continue;
        }

        {
            int fits = want[i].w <= JPEG_MAX_W && want[i].h <= JPEG_MAX_H;
            int ok = jpeg_init() == 0 && jpeg_decode_rgb565(g_jpg, got, g_out, CELL_W, CELL_H, &gw, &gh, g_scratch, sizeof(g_scratch)) == 0;
            jpeg_finish();

            if (ok != fits) {
                if (!why[0]) snprintf(why, why_n, "%02d %s: %.60s", i, fits ? "would not decode" : "was not refused", jpeg_error());
                (*nodecode)++;
                continue;
            }
            if (!fits) {
                (*refused_right)++;
                continue;
            }
        }
        for (yy = 0; yy < STAGE * STAGE; yy++) g_stage[yy] = 0;
        for (yy = 0; yy < gh && yy < STAGE; yy++)
            memcpy(g_stage + yy * STAGE, g_out + yy * gw, (unsigned)(gw < STAGE ? gw : STAGE) * sizeof(*g_out));
        gfx_wrote_pixels();

        sw = gw < STAGE ? gw : STAGE;
        sh = gh < STAGE ? gh : STAGE;
        if (gfx_blit_part(x, y, g_stage, STAGE, STAGE, sw, sh, UI_ART_RADIUS, PAGE_BG))
            (*drawn)++;
        else {
            if (!why[0]) snprintf(why, why_n, "%.31s: blit refused %dx%d", names[i], sw, sh);
            (*noblit)++;
        }

        snprintf(label, sizeof(label), "%dx%d", gw, gh);
        text_draw(x, y + CELL_H + 2, TEXT_SMALL, 0x90A0B0u, label);
    }
    gfx_frame_end();
    return 0;
}

static int t_the_fixtures_draw(char *note, unsigned n) {
    int  drawn = 0, nodecode = 0, noblit = 0, missing = 0, refused_right = 0;
    char why[160] = "";
    int  pass;

    if (gfx_start() != 0 || text_start() != 0) {
        snprintf(note, n, "no panel or no glyphs");
        return -1;
    }
    /* The page twice, not the frame: a second empty frame clears what the
     * first drew. */
    for (pass = 0; pass < 2; pass++) draw_fixture_page(why, sizeof(why), &drawn, &nodecode, &noblit, &missing, &refused_right);

    if (missing) {
        snprintf(note, n, "%d of %d fixtures are not on the link -- run scripts/fixtures.py", missing, FIXTURES);
        return -1;
    }
    if (nodecode || noblit) {
        /* The reason first: the note is 96 bytes and a preamble of counts
         * pushed it out entirely. */
        snprintf(note, n, "%.70s (%d drawn)", why, drawn);
        return 1;
    }
    (void)shot_write("fixtures.ppm");
    snprintf(note, n, "%d drawn, %d refused for being taller than the panel", drawn, refused_right);
    return 0;
}

/* A spread of radii because the right one is a judgement, and 0 to show what
 * the ring alone buys. */
static const int RADII[5] = {0, 2, 3, 5, 8};

static int t_cards_draw_at_each_radius(char *note, unsigned n) {
    static POCKETFIN_ALIGN16 unsigned short art[STAGE * STAGE];
    unsigned                                got;
    int                                     gw = 0, gh = 0, i, y, pass, drawn = 0;

    if (gfx_start() != 0 || text_start() != 0) {
        snprintf(note, n, "no panel or no glyphs");
        return -1;
    }
    got = hostfs_up() ? hostfs_slurp("cards/card.jpg", (char *)g_jpg, sizeof(g_jpg)) : 0;
    if (!got) {
        snprintf(note, n, "cards/card.jpg is not on the link -- run scripts/fixtures.py");
        return -1;
    }
    if (jpeg_init() != 0 || jpeg_decode_rgb565(g_jpg, got, g_out, 72, 108, &gw, &gh, g_scratch, sizeof(g_scratch)) != 0) {
        snprintf(note, n, "cards/card.jpg: %.70s", jpeg_error());
        jpeg_finish();
        return 1;
    }
    jpeg_finish();
    if (gw > STAGE) gw = STAGE;
    if (gh > STAGE) gh = STAGE;

    for (y = 0; y < STAGE * STAGE; y++) art[y] = 0;
    for (y = 0; y < gh; y++) memcpy(art + y * STAGE, g_out + y * gw, (unsigned)gw * sizeof(*g_out));
    gfx_wrote_pixels();

    for (pass = 0; pass < 2; pass++) {
        drawn = 0;
        gfx_frame_begin();
        gfx_fill(0, 0, GFX_W, GFX_H, PAGE_BG);
        text_draw(8, 4, TEXT_SMALL, 0x90A0B0u, "one poster, five radii: picture at r, ring at r+1.  top plain, bottom focused");

        for (i = 0; i < 5; i++) {
            int  r = RADII[i], x = 16 + i * 92;
            int  top = 26, bot = 150;
            char label[16];

            if (gfx_blit_card(x, top, art, STAGE, STAGE, gw, gh, r, 0x2C3444u, 0x2C3444u, PAGE_BG)) drawn++;
            if (gfx_blit_card(x, bot, art, STAGE, STAGE, gw, gh, r, 0x2A5A8Cu, 0x7AC7FFu, PAGE_BG)) drawn++;

            snprintf(label, sizeof(label), "r=%d", r);
            text_draw(x, top + gh + 3, TEXT_SMALL, 0x90A0B0u, label);
        }
        gfx_frame_end();
    }
    if (!drawn) {
        snprintf(note, n, "nothing drew");
        return 1;
    }
    (void)shot_write("cards.ppm");
    snprintf(note, n, "%dx%d at radii 0,2,3,5,8, plain and focused", gw, gh);
    return 0;
}

/* 02 in the fixture set is refused by both decode paths with 0x80650041 and
 * the others are not; this is the same poster at eight sizes around it. */
static int t_which_sizes_the_engine_takes(char *note, unsigned n) {
    static const struct {
        int w, h;
    } sz[8] = {{100, 150}, {96, 144}, {100, 150}, {96, 144}, {100, 150}, {107, 160}, {100, 150}, {98, 147}};
    char line[SELFTEST_NOTE_MAX];
    int  i, at = 0, taken = 0;

    if (jpeg_init() != 0) {
        snprintf(note, n, "no decoder");
        return -1;
    }
    line[0] = 0;
    for (i = 0; i < 8; i++) {
        char     name[24];
        unsigned got;
        int      gw = 0, gh = 0, ok;

        snprintf(name, sizeof(name), "probe/p%d.jpg", i);
        got = hostfs_up() ? hostfs_slurp(name, (char *)g_jpg, sizeof(g_jpg)) : 0;
        if (!got) {
            jpeg_finish();
            snprintf(note, n, "%s is not on the link", name);
            return -1;
        }
        ok = jpeg_decode_rgb565(g_jpg, got, g_out, 128, 192, &gw, &gh, g_scratch, sizeof(g_scratch)) == 0;
        if (ok) taken++;
        at += snprintf(line + at, sizeof(line) - (unsigned)at, "%dx%d%s ", sz[i].w, sz[i].h, ok ? "+" : "-");
        if (at >= (int)sizeof(line) - 12) break;
    }
    jpeg_finish();
    /* Which sizes it takes is a diagnosis; none is a broken decoder, and every
       poster in the app comes through this call. */
    if (taken == 0) {
        snprintf(note, n, "the engine took none of the 8 probe sizes");
        return 1;
    }
    snprintf(note, n, "%s(%d/8)", line, taken);
    return taken == 8 ? 0 : 1;
}

void test_jpeg_register(void) {
    selftest_add("jpeg", "a real poster decodes", t_a_real_poster_decodes);
    selftest_add("jpeg", "a poster reaches the panel", t_a_poster_reaches_the_panel);
    selftest_add("jpeg", "the fixtures draw", t_the_fixtures_draw);
    selftest_add("jpeg", "which sizes the engine takes", t_which_sizes_the_engine_takes);
    selftest_add("jpeg", "cards draw at each radius", t_cards_draw_at_each_radius);
}
