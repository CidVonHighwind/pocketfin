/* The pad's buttons drawn as themselves. See element.h. */

#include "view/element.h"

#include "port/gfx.h"
#include "port/text.h"
#include "view/theme.h"

#include <string.h>

/* Sixteenths. These shapes are diagonals and arcs at nine pixels, and a bare
 * one-pixel diagonal on this panel is a visible staircase. */
#define SUB 16

static void plot(int x, int y, unsigned rgb, int a8) {
    if (a8 >= 8)
        gfx_fill(x, y, 1, 1, rgb);
    else if (a8 > 0)
        gfx_blend_rect(x, y, 1, 1, rgb, a8 * 255 / 8);
}

/* A run at a time, not a pixel: on the console every fill is a display list
 * entry, and a span set pixelwise is sixty of them a frame. */
static void span_h(int y, int l, int r, unsigned rgb) {
    int lp = l / SUB, rp = (r - 1) / SUB;

    if (r <= l) return;
    if (lp == rp) {
        gfx_blend_rect(lp, y, 1, 1, rgb, (r - l) * 255 / SUB);
        return;
    }
    gfx_blend_rect(lp, y, 1, 1, rgb, ((lp + 1) * SUB - l) * 255 / SUB);
    if (rp > lp + 1) gfx_fill(lp + 1, y, rp - lp - 1, 1, rgb);
    gfx_blend_rect(rp, y, 1, 1, rgb, (r - rp * SUB) * 255 / SUB);
}

static unsigned isqrt_u32(unsigned v) {
    unsigned rem = v, root = 0, bit = 1u << 30;

    while (bit > rem) bit >>= 2;
    while (bit) {
        if (rem >= root + bit) {
            rem -= root + bit;
            root = (root >> 1) + bit;
        } else
            root >>= 1;
        bit >>= 2;
    }
    return root;
}

/* The colour the console prints each button in. */
static unsigned btn_colour(ui_btn b) {
    switch (b) {
    case UI_BTN_CROSS: return 0x7CB8F0u;
    case UI_BTN_CIRCLE: return 0xF07884u;
    case UI_BTN_TRIANGLE: return 0x76DCA6u;
    case UI_BTN_SQUARE: return 0xEE92C8u;
    default: return UI_FG_DIM;
    }
}

static void shape_cross(int x, int y, unsigned rgb) {
    int i;

    for (i = 0; i < 7; i++) {
        plot(x + 1 + i, y + 1 + i, rgb, 8);
        plot(x + 7 - i, y + 1 + i, rgb, 8);
        plot(x + i, y + 1 + i, rgb, 3);
        plot(x + 2 + i, y + 1 + i, rgb, 3);
        plot(x + 6 - i, y + 1 + i, rgb, 3);
        plot(x + 8 - i, y + 1 + i, rgb, 3);
    }
}

static void shape_circle(int x, int y, unsigned rgb) {
    int row, col;

    /* Measured from the true centre of the 9x9 cell -- the corner between
     * four pixels, not the middle of one, which leaves it a pixel heavier on
     * two sides. Radius 3.7 in sixteenths, stroke a little over a pixel. */
    for (row = 0; row < 9; row++) {
        for (col = 0; col < 9; col++) {
            int dx = (col - 4) * SUB;
            int dy = (row - 4) * SUB;
            int d  = (int)isqrt_u32((unsigned)(dx * dx + dy * dy));
            int e  = d - 59;

            if (e < 0) e = -e;
            e -= 5;
            if (e >= SUB) continue;
            plot(x + col, y + row, rgb, e <= 0 ? 8 : 8 - e * 8 / SUB);
        }
    }
}

static void shape_triangle(int x, int y, unsigned rgb) {
    int row;

    /* An outline, like the button. The sloped sides are stroked where the
     * edge really falls -- 22 sixteenths, because a 45 degree edge crosses
     * about 1.4 pixels of a scanline -- and the two overlapping is what
     * closes the apex. Eight wide at the base, not seven: the cross and the
     * circle reach their cell edges and seven looks smaller beside them. */
    for (row = 1; row <= 7; row++) {
        int half = row * 8 * SUB / (2 * 7);
        int cx   = (x + 4) * SUB + SUB / 2;

        if (row == 7) {
            span_h(y + row, cx - half, cx + half, rgb);
        } else {
            span_h(y + row, cx - half, cx - half + 22, rgb);
            span_h(y + row, cx + half - 22, cx + half, rgb);
        }
    }
}

void ui_arrow(int x, int y, int w, int h, int dir, unsigned rgb) {
    /* Which axis is which comes from dir alone: comparing against h cannot
     * tell them apart on a square box. */
    int vertical = (dir == UI_ARROW_UP || dir == UI_ARROW_DOWN);
    int i, n = vertical ? h : w;
    int across = vertical ? w : h;
    int centre = vertical ? x * SUB + w * SUB / 2 : y * SUB + h * SUB / 2;

    if (n < 2 || across < 2) return;

    for (i = 0; i < n; i++) {
        int from_tip = (dir == UI_ARROW_DOWN || dir == UI_ARROW_RIGHT) ? n - 1 - i : i;
        /* Not zero wide at the tip, or the point has a gap in it. */
        int half = (from_tip + 1) * across * SUB / (2 * n);
        int lo = centre - half, hi = centre + half, p;

        for (p = lo / SUB; p <= (hi - 1) / SUB; p++) {
            int l = p * SUB, r = l + SUB;
            int cov = (hi < r ? hi : r) - (lo > l ? lo : l);

            if (cov <= 0) continue;
            if (cov > SUB) cov = SUB;
            if (vertical)
                plot(p, y + i, rgb, cov * 8 / SUB);
            else
                plot(x + i, p, rgb, cov * 8 / SUB);
        }
    }
}

/* A 3x5 alphabet, for those two words and nothing else: the smallest baked
 * face is a reading size, and set in it the word fills the pill edge to edge
 * and the two read as one smudge. Five rows of three bits, top row first,
 * leftmost pixel high -- row r column c is bit (4 - r) * 3 + (2 - c). */
#define MICRO_W 3
#define MICRO_H 5

static unsigned micro_letter(char c) {
    switch (c) {
    case 'S': return 0x79CFu;
    case 'T': return 0x7492u;
    case 'A': return 0x2BEDu;
    case 'R': return 0x6BADu;
    case 'E': return 0x79A7u;
    case 'L': return 0x4927u;
    case 'C': return 0x7927u;
    default: return 0u;
    }
}

static int micro_text_w(const char *s) {
    int n = (int)strlen(s);

    return n ? n * (MICRO_W + 1) - 1 : 0;
}

static void micro_text(int x, int y, const char *s, unsigned rgb) {
    int row, col;

    for (; *s; s++, x += MICRO_W + 1) {
        unsigned bits = micro_letter(*s);

        for (row = 0; row < MICRO_H; row++) {
            int run = 0;

            for (col = 0; col <= MICRO_W; col++) {
                int on = col < MICRO_W && (bits & (1u << ((4 - row) * 3 + (2 - col)))) != 0;

                if (on) {
                    run++;
                } else if (run) {
                    gfx_fill(x + col - run, y + row, run, 1, rgb);
                    run = 0;
                }
            }
        }
    }
}

/* A stadium, as the button is on the console. Taller than the 9-pixel cell
 * the other glyphs draw in, and there is room: the footer band is 18 rows. */
#define WORD_H   (MICRO_H + 6)
#define WORD_PAD 5

static const char *btn_word(ui_btn b) {
    if (b == UI_BTN_START) return "START";
    if (b == UI_BTN_SELECT) return "SELECT";
    return 0;
}

static int word_w(const char *word) { return micro_text_w(word) + 2 * WORD_PAD; }

static void glyph(int x, int y, ui_btn b) {
    unsigned rgb = btn_colour(b);

    switch (b) {
    case UI_BTN_CROSS: shape_cross(x, y, rgb); break;
    case UI_BTN_CIRCLE: shape_circle(x, y, rgb); break;
    case UI_BTN_TRIANGLE: shape_triangle(x, y, rgb); break;
    case UI_BTN_SQUARE: gfx_round_frame(x + 1, y + 1, 7, 7, 0, rgb); break;

    case UI_BTN_LEFTRIGHT:
        ui_arrow(x, y + 2, 4, 5, UI_ARROW_LEFT, rgb);
        ui_arrow(x + 5, y + 2, 4, 5, UI_ARROW_RIGHT, rgb);
        break;

    /* Shorter than the cell: filling it makes two triangles that meet in the
     * middle and read as one solid lozenge. */
    case UI_BTN_UPDOWN:
        ui_arrow(x + 1, y, 7, 3, UI_ARROW_UP, rgb);
        ui_arrow(x + 1, y + 6, 7, 3, UI_ARROW_DOWN, rgb);
        break;

    case UI_BTN_START:
    case UI_BTN_SELECT: {
        const char *word = btn_word(b);
        int         top  = y + (UI_BTN_SIZE - WORD_H) / 2;

        gfx_round_frame(x, top, word_w(word), WORD_H, WORD_H / 2, rgb);
        micro_text(x + WORD_PAD, top + 3, word, rgb);
        break;
    }
    default: break;
    }
}

static int glyph_w(ui_btn b) {
    const char *word = btn_word(b);

    return word ? word_w(word) : UI_BTN_SIZE;
}

int ui_hint_w(ui_btn b, const char *label) { return glyph_w(b) + 3 + text_width(TEXT_SMALL, label); }

#define HINT_GAP 10

static void hint(int x, int y, ui_btn b, const char *label) {
    glyph(x, y, b);
    /* Centred on the glyph's own box by capital height: what the eye reads as
     * the middle of each is not the top of the line box. */
    (void)text_draw(x + glyph_w(b) + 3, ui_text_y(TEXT_SMALL, y, UI_BTN_SIZE), TEXT_SMALL, UI_FG_DIM, label);
}

int ui_hint_row(int x, int y, int right, const ui_hint_item *hints, int n) {
    int i;

    for (i = 0; i < n; i++) {
        int w = ui_hint_w(hints[i].btn, hints[i].label);

        if (x + w > right) break;
        hint(x, y, hints[i].btn, hints[i].label);
        x += w + HINT_GAP;
    }
    return i;
}

int ui_hint_row_w(const ui_hint_item *hints, int n) {
    int i, w = 0;

    for (i = 0; i < n; i++) w += (i ? HINT_GAP : 0) + ui_hint_w(hints[i].btn, hints[i].label);
    return w;
}

int ui_hint_row_right(int right, int y, int left_limit, const ui_hint_item *hints, int n) {
    int start = right - ui_hint_row_w(hints, n), x = start, i;

    /* Refused rather than clipped or overlapped: two groups running into each
     * other say less than one dropped. */
    if (start < left_limit) return right;
    for (i = 0; i < n; i++) {
        hint(x, y, hints[i].btn, hints[i].label);
        x += ui_hint_w(hints[i].btn, hints[i].label) + HINT_GAP;
    }
    return start;
}
