/* Text as an element. See element.h. */

#include "view/element.h"

#include "port/gfx.h"
#include "view/theme.h"

#include <string.h>

#define ELLIPSIS "..."

int ui_text_y(text_face_id f, int band_y, int band_h) {
    const text_face *face;

    if (f < 0 || f >= TEXT_FACE_N) f = TEXT_BODY;
    face = &text_faces[f];
    return band_y + (band_h - face->cap_h) / 2 - face->cap_top;
}

int ui_text_fit_w(text_face_id f, const char *s, int maxw) {
    int w;

    if (!s) return 0;
    w = text_width(f, s);
    if (maxw <= 0 || w <= maxw) return w;
    return maxw;
}

int ui_text_fit(int x, int y, text_face_id f, unsigned rgb, const char *s, int maxw) {
    int                  dots, budget;
    const unsigned char *p;

    if (!s) return x;
    if (maxw <= 0 || text_width(f, s) <= maxw) return text_draw(x, y, f, rgb, s);

    dots   = text_width(f, ELLIPSIS);
    budget = maxw - dots;

    /* Measured before it is drawn: drawing first put the glyph that did not
       fit on the panel anyway, and the ellipsis on top of it. */
    for (p = (const unsigned char *)s; *p; p++) {
        char one[2];
        int  adv;

        one[0] = (char)*p;
        one[1] = 0;
        adv    = text_width(f, one);
        if (adv > budget) break;

        (void)text_draw_glyph(x, y, f, rgb, *p);
        budget -= adv;
        x += adv;
    }
    return text_draw(x, y, f, rgb, ELLIPSIS);
}

/* The longest run copied out to be measured. Wider than the panel at any
   face, so it never truncates a word the wrap did not. */
#define WORD_MAX 128

/* Breaks after the last word that fits. Returns how many bytes belong on this
   line, at least one. */
static int wrap_at(text_face_id face, const char *s, int w) {
    int n = 0, fit = 0;

    while (s[n]) {
        char buf[WORD_MAX];
        int  end = n, take;

        while (s[end] && s[end] != ' ') end++;
        take = end > WORD_MAX - 1 ? WORD_MAX - 1 : end;
        memcpy(buf, s, (size_t)take);
        buf[take] = 0;
        if (text_width(face, buf) > w) break;

        fit = end;
        n   = end;
        while (s[n] == ' ') n++;
        if (!s[n]) break;
    }
    return fit > 0 ? fit : 1;
}

int ui_lines_for(ui_role role, const char *s, int w) {
    const ui_role_style *st    = ui_style_of(role);
    int                  lines = 0;

    if (!s || !s[0] || w <= 0) return 0;
    while (*s) {
        int fit = wrap_at(st->face, s, w);

        lines++;
        if (!s[fit]) break;
        s += fit;
        while (*s == ' ') s++;
    }
    return lines;
}

/* Wraps, and cuts with an ellipsis only when the box or `max_lines` runs out.
 * The lead is skipped at the top of a box: that space is the page's margin. */
void ui_text(ui_layout *L, ui_role role, const char *s, int max_lines) {
    const ui_role_style *st  = ui_style_of(role);
    unsigned             ink = ui_role_ink(role);
    int                  w, room, lines, i, gap;
    ui_rect              at;

    if (!s || !s[0]) return;
    /* The box's gap wins: a column that states its own spacing means it, and a
       role adding its lead on top spaces one page unlike every other. */
    if (ui_started(L) && !ui_gap_of(L)) ui_gap(L, st->lead);

    /* A box with a gap spaces the lines of a paragraph by it too. */
    gap  = ui_gap_of(L);
    w    = ui_here(L).w;
    room = ui_here(L).h - ui_used(L);
    if (ui_started(L)) room -= gap;

    /* Counted up front so the last line can be the one that gets the
       ellipsis, whether it ran out of lines or out of box. */
    lines = ui_lines_for(role, s, w);
    if (max_lines > 0 && lines > max_lines) lines = max_lines;
    if (lines > (room + gap) / (st->band + gap)) lines = (room + gap) / (st->band + gap);
    if (lines < 1) lines = 1;

    for (i = 0; i < lines; i++) {
        int  fit = wrap_at(st->face, s, w);
        char buf[WORD_MAX];
        int  take;

        at = ui_take(L, UI_FILL, st->band);

        if (i == lines - 1 || !s[fit]) {
            (void)ui_text_fit(at.x, ui_text_y(st->face, at.y, at.h), st->face, ink, s, at.w);
            return;
        }

        take = fit > WORD_MAX - 1 ? WORD_MAX - 1 : fit;
        memcpy(buf, s, (size_t)take);
        buf[take] = 0;
        (void)ui_text_fit(at.x, ui_text_y(st->face, at.y, at.h), st->face, ink, buf, at.w);

        s += fit;
        while (*s == ' ') s++;
    }
}

void ui_heading(ui_layout *L, const char *s) { ui_text(L, UI_HEADING, s, 0); }
void ui_body(ui_layout *L, const char *s) { ui_text(L, UI_BODY, s, 0); }
void ui_caption(ui_layout *L, const char *s) { ui_text(L, UI_CAPTION, s, 0); }

/* Not a theme colour: a rule that changed with the gradient reads as a
   mistake. */
#define RULE 0x242A38u

void ui_divider(ui_layout *L) {
    ui_rect at = ui_take(L, UI_FILL, 1);

    gfx_fill(at.x, at.y, at.w, 1, RULE);
}

/* No lead: with one the settings page came to 235 px in a 232 px body and
   "Signed in as" was drawn over the hint bar. */
void ui_pair(ui_layout *L, const char *label, const char *value) {
    const ui_role_style *st = ui_style_of(UI_CAPTION);
    ui_rect              at, v;

    at = ui_take(L, UI_FILL, st->band);

    ui_row_at(L, at, 0);
    v = ui_take_end(L, ui_text_fit_w(st->face, value, 0), UI_FILL);
    (void)ui_text_fit(v.x, ui_text_y(st->face, v.y, v.h), st->face, UI_FG_DIM, value, v.w);
    {
        ui_rect l = ui_take(L, UI_FILL, UI_FILL);

        (void)ui_text_fit(l.x, ui_text_y(st->face, l.y, l.h), st->face, ui_role_ink(UI_CAPTION), label, l.w);
    }
    ui_end(L);
}
