/* Plain 0xRRGGBB, what gfx.h takes. */
#ifndef VIEW_THEME_H
#define VIEW_THEME_H

typedef struct {
    const char *name;
    unsigned    brand_a, brand_b;     /* the gradient, and its direction */
    unsigned    bg_top, bg_bot;       /* the ground behind everything */
    unsigned    panel_top, panel_bot; /* rows, cards, the title bar */
    unsigned    accent;               /* the gradient, as readable text */
    unsigned    sel_a, sel_b;         /* a focused row */
} ui_theme;

#define UI_THEME_N 6

extern const ui_theme ui_themes[UI_THEME_N];

/* Out of range selects 0 rather than reading off the end. */
void            ui_theme_set(int which);
int             ui_theme_get(void);
const ui_theme *ui_theme_now(void);

/* Fixed across every theme. */
#define UI_FG          0xEAEEF6u /* primary text */
#define UI_FG_DIM      0x8A94A8u /* secondary */
#define UI_FG_FAINT    0x5C6476u /* disabled, rules */
#define UI_WELL_BG     0x1A1F2Au
#define UI_WELL_EDGE   0x2C3444u
#define UI_CHIP_BG     0x202531u
#define UI_CHIP_HOT    0x142C3Au
#define UI_CHIP_MARK   0x2A1E33u
#define UI_MARK_INK    0xC9A4DCu
#define UI_BUTTON_BG   0x1C212Cu
#define UI_BUTTON_EDGE 0x333B4Cu
#define UI_TRACK       0x2A3140u
#define UI_SCRIM_INK   0x06080Cu
/* Fetched but not yet shown, on a seek bar. Between the track and the fill:
   read as neither, a stall and a healthy stream look the same. */
#define UI_BUFFERED 0x5E667Au

#endif
