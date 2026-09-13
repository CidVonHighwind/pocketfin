/* The glyph data is generated from assets/font.bin at build time. */
#ifndef PORT_TEXT_H
#define PORT_TEXT_H

/* Latin-1. Both renderers must cover all of it or neither may claim to. */
#define TEXT_FIRST  32
#define TEXT_LAST   255
#define TEXT_GLYPHS (TEXT_LAST - TEXT_FIRST + 1)

typedef enum { TEXT_SMALL = 0, TEXT_BODY, TEXT_TITLE, TEXT_FACE_N } text_face_id;

/* ONE atlas, all three faces, packed tallest-first at bake time: 512 is the
 * widest texture the engine takes, and that order fits them in 128 rows
 * rather than 134. */
#define TEXT_ATLAS_W 512
#define TEXT_ATLAS_H 128

extern const unsigned char text_atlas_start[TEXT_ATLAS_W * TEXT_ATLAS_H];

/* `by` is from the cell top, not the baseline: read as a baseline offset it
 * puts every ascender and digit a few pixels low and leaves the short letters
 * looking right. `adv4` is the advance in quarter pixels. */
typedef struct {
    unsigned short ax, ay;
    unsigned char  w, h;
    signed char    bx, by;
    unsigned char  adv4;
} text_glyph;

typedef struct {
    const text_glyph *rec;
    unsigned char     ascent, height, cap_top, cap_h;
} text_face;

extern const text_face text_faces[TEXT_FACE_N];

/* 0 when there is text. A bake that produced nothing must NOT report ready. */
int text_start(void);

/* Inside a frame, like every fill. `x`,`y` is the top-left of the line box;
 * returns the pen position after the last glyph. */
int text_draw(int x, int y, text_face_id face, unsigned rgb, const char *s);

/* Returns what it advanced. The UI measures, truncates and appends an
 * ellipsis itself, so it draws a character at a time. */
int text_draw_glyph(int x, int y, text_face_id face, unsigned rgb, int ch);

int text_width(text_face_id face, const char *s);
int text_height(text_face_id face);

/* A count below TEXT_GLYPHS is a renderer that will drop characters. */
void text_stats(unsigned *glyphs, unsigned *bytes);

#endif
