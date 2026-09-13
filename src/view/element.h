/* A page says what is on it and never where or what colour: an element takes
 * its own space from the open box, and asks style.h what its role looks like.
 *
 * An `_at` form is given a rectangle instead of taking a slot, for an element
 * placed at a coordinate of its own. */
#ifndef VIEW_ELEMENT_H
#define VIEW_ELEMENT_H

#include "view/frame.h"
#include "view/layout.h"
#include "base/rect.h"
#include "view/style.h"
#include "port/text.h"

/* A line in its role, taking a band from the open box and the space that role
 * leads with. Cut with an ellipsis if the box is narrower than the words. */
void ui_heading(ui_layout *layout, const char *s);
void ui_body(ui_layout *layout, const char *s);
void ui_caption(ui_layout *layout, const char *s);

/* Any role, capped at `max_lines` (0 for as many as fit). */
void ui_text(ui_layout *layout, ui_role role, const char *s, int max_lines);

/* How many lines that role's text wraps to at that width, without drawing. */
int ui_lines_for(ui_role role, const char *s, int w);

/* A one-pixel rule across the open box; the air around it is the caller's. */
void ui_divider(ui_layout *layout);

/* A faint label and its value at the box's two edges -- what a page says
 * about itself rather than something to change. */
void ui_pair(ui_layout *layout, const char *label, const char *value);

/* START and SELECT are words in a pill: nothing drawn in nine pixels can say
 * which of the two it is. */
typedef enum {
    UI_BTN_CROSS = 0,
    UI_BTN_CIRCLE,
    UI_BTN_TRIANGLE,
    UI_BTN_SQUARE,
    UI_BTN_LEFTRIGHT,
    UI_BTN_UPDOWN,
    UI_BTN_START,
    UI_BTN_SELECT,
    UI_BTN_N
} ui_btn;

/* The cell the four face buttons and the two d-pad glyphs draw in. */
#define UI_BTN_SIZE 9

enum { UI_ARROW_UP = 0, UI_ARROW_DOWN, UI_ARROW_LEFT, UI_ARROW_RIGHT };
#define UI_ARROW_W   7
#define UI_ARROW_H   5
#define UI_ARROW_GAP 4

void ui_arrow(int x, int y, int w, int h, int dir, unsigned rgb);

/* A button's glyph and its label. */
int ui_hint_w(ui_btn b, const char *label);

/* The array is not copied, so it must outlive the call. */
typedef struct {
    ui_btn      btn;
    const char *label;
} ui_hint_item;

/* A row of hints from the left, stopping at `right` rather than running off
 * the panel. Returns how many it drew. */
int ui_hint_row(int x, int y, int right, const ui_hint_item *hints, int n);

/* The same row laid out backwards from `right`. Returns the x it started at. */
int ui_hint_row_right(int right, int y, int left_limit, const ui_hint_item *hints, int n);

/* What that would need, without drawing. */
int ui_hint_row_w(const ui_hint_item *hints, int n);

/* The bar only; the ground belongs to ui_page_begin. `status` is a sort order,
 * a count, or NULL; `arrow` is a UI_ARROW_* beside it, or -1. The title gives
 * up room before the status does. `busy` runs a spinner at the right end, off
 * the bar's width, and `frame` turns it. */
void ui_title_bar_at(ui_rect at, const char *title, const char *status, int arrow, int busy, unsigned frame);

/* The footer rule and both hint groups. Either group may be NULL/0. */
void ui_footer_at(ui_rect at, const ui_hint_item *left, int left_n, const ui_hint_item *right, int right_n);

#define UI_BUTTON_H 22

/* Takes its own slot, as wide as its label needs. Returns 1 on the frame it
 * has the cursor and cross is pressed. */
int ui_button(ui_frame *ui, ui_id id, const char *label);

/* The same button, smaller. */
#define UI_BUTTON_QUIET_H 17
int ui_button_quiet(ui_frame *ui, ui_id id, const char *label);

/* A turning ring and a line under it, centred in the open box. `frame` is a
 * counter the caller advances once a frame. */
void ui_waiting(ui_layout *layout, const char *text, unsigned frame);
void ui_spin_at(int cx, int cy, unsigned frame);

/* `rows` row-shaped placeholders at `row_h`, for a list that is loading, so the
 * layout does not jump when the rows land. Takes the boxes it draws. */
void ui_skeleton(ui_layout *layout, int row_h, int rows, unsigned frame);

/* A heading, an explanation and at most one button, as a block centred in the
 * open box. `head`, `body` and `button` may each be null. Returns 1 when the
 * button is pressed. */
int ui_notice(ui_frame *ui, ui_id id, const char *head, const char *body, const char *button);

/* What that block will take, without drawing. It must agree with the drawing:
 * the centring is only as good as this height. */
int ui_notice_h(ui_layout *layout, const char *head, const char *body, const char *button);

#define UI_ART_RADIUS 3
#define UI_ART_EDGE_R (UI_ART_RADIUS + 1)

/* A picture staged in a texture: the engine reads powers of two, and a poster
 * is 72x108. `w` by `h` of a `tex_w` by `tex_h` texture is the picture; `px`
 * NULL is no picture at all, and draws the empty well. */
typedef struct {
    const unsigned short *px;
    int                   tex_w, tex_h;
    int                   w, h;
    /* Nonzero while the picture is still coming. The dots walk off the clock:
       a page counting its own frames for it got a rate that depended on how
       long the page happened to be up. */
    unsigned waiting;
} ui_art;

/* The well and, if there is one, the picture in it. `fixed` keeps the well the
 * size of the slot, for a row that must be uniform; otherwise the well takes
 * the picture's own size, which is what a rail of posters wants. `bg` is the
 * ground the well sits on: its corners fade into it, and an element cannot see
 * what was drawn behind it. */
void ui_artwork(ui_rect slot, const ui_art *art, int focused, int fixed, unsigned bg);

/* The plate artwork sits in: a 200x212 slot with nothing in it reads as a page
 * that failed to draw. The walking dots go without the well, centred in `at`;
 * 0 draws nothing. */
void ui_art_well(ui_rect at);
void ui_art_dots(ui_rect at, unsigned frame);

/* Down the right of `view`, or along its bottom; drawn only when enough is
 * off-screen to be worth saying. Three wide: at two the rounding cuts less than
 * a fifth of a pixel and the bar stays visibly square. */
#define UI_SCROLLBAR_W   3
#define UI_SCROLLBAR_GAP 3
void ui_scrollbar(ui_rect view, int offset, int content, int vertical);

/* A card's rectangle contains its ring: ask for UI_CARD_BOX of the picture. */
#define UI_CARD_EDGE     1
#define UI_CARD_BOX(art) ((art) + 2 * UI_CARD_EDGE)

/* The picture, and exactly one decoration: a bar when `den` is set, else a
 * caption. */
void ui_art_card(ui_rect slot, const ui_art *art, int focused, unsigned bg, int num, int den, const char *caption);

/* A word in a pill: how long something runs, whether it has been watched.
 * UI_CHIP_GAP is the space to the next one, so a row of them can be opened as
 * a box with that gap and each chip just takes its slot. */
#define UI_CHIP_H   14
#define UI_CHIP_GAP 5

void ui_chip(ui_layout *layout, const char *text, unsigned ink, unsigned fill);

/* `art` may be null for no picture. Returns 1 while the row has the cursor.
 * Registers the row whether or not `draw`: a cursor that cannot find an
 * off-screen row can never scroll to it. */
int ui_media_row_at(ui_frame *ui, ui_id id, ui_rect box, int draw, const ui_art *art, int art_w, int art_h, const char *title,
                    const char *subtitle, int pct);

/* One blend per row, never per pixel: under the console's display list a blend
 * is a one-pixel quad, so a full-panel scrim written per pixel is 130,560 of
 * them against a 6,000-primitive budget and a 256 kB list -- which overflows
 * and takes the machine with it.
 *
 * Ramps to nothing over UI_SCRIM_FADE rows. Alpha is in eighths: 5 of 8 reads
 * white text over a snowfield with the film still visible through it. Scaled
 * to the 0..255 gfx_blend_rect takes at the call site; 5 straight is a two per
 * cent wash. */
#define UI_SCRIM_ALPHA 5
#define UI_SCRIM_STEPS 8
#define UI_SCRIM_FADE  9
void ui_scrim(ui_rect at, int fade_at_top);

/* Which transport control has the selection. NONE is the seek bar. */
#define UI_CTL_NONE 0
#define UI_CTL_PREV 1
#define UI_CTL_PLAY 2
#define UI_CTL_NEXT 3

/* What the band says it is doing. SEEKING and BUFFERING carry a target. */
typedef enum {
    UI_CAP_PLAYING = 0,
    UI_CAP_PAUSED,
    UI_CAP_SEEKING,  /* a jump is being aimed and playback has not moved */
    UI_CAP_BUFFERING /* the stream is being restarted at the target      */
} ui_band_cap;

/* Values: the page decides all of this and the element draws what it is told. */
typedef struct {
    const char        *title;
    unsigned long long pos, dur, buffered;
    int                has_prev, has_next;
    unsigned           tick; /* a 60 Hz counter, for the buffering dots */

    int up, paused, seek_armed, stats;

    /* The readout, already worded; the scrim is sized to it. */
    const char *const *stat_lines;
    int                stat_count;

    unsigned long long seek_to;
    int                ctl; /* UI_CTL_* */
    ui_band_cap        cap;

    ui_id id_prev, id_play, id_next, id_bar;
} ui_band;

void ui_player_band(ui_frame *ui, const ui_band *b);

typedef enum {
    UI_OPTION_NONE = 0, /* an action: its value says what it acts on */
    UI_OPTION_SLIDER,
    UI_OPTION_SWITCH
} ui_option_kind;

/* Label, value, and the control that changes it. `at`, `lo` and `hi` are the
 * control's position and its ends -- ignored by UI_OPTION_NONE, and a switch
 * reads `at` as on or off. Returns 1 while the row has the cursor.
 *
 * The row's wash and the box the cursor aims at run to the panel's edges
 * while its contents keep the page's margin. */
int ui_option(ui_frame *ui, ui_id id, const char *label, const char *value, ui_option_kind kind, int at, int lo, int hi);

/* A selected row's wash, for an element that arranges its own row. */
void ui_row_wash(ui_rect at);

/* The y that puts a face's cap height in the middle of a band -- not its line
 * box, which leaves anything short of an ascender sitting low. */
int ui_text_y(text_face_id f, int band_y, int band_h);

/* Drawn to fit, cut with an ellipsis if it does not. `maxw` <= 0 is no limit.
 * Returns the pen position; `_w` answers what it would take without drawing. */
int ui_text_fit(int x, int y, text_face_id f, unsigned rgb, const char *s, int maxw);
int ui_text_fit_w(text_face_id f, const char *s, int maxw);

#endif
