/* The player: this page decides everything, view/element_player.c draws the
 * band it describes. With no decoder the ground is black and the position runs
 * off the machine's clock; every other part of the screen is the same. */
#ifndef PAGE_PLAYER_H
#define PAGE_PLAYER_H

#include "jelly/item.h"
#include "view/stack.h"

/* Out here so a check compares against the page's own numbers and fails when
   they are renumbered. */
#define PLAYER_ID_PREV 1
#define PLAYER_ID_PLAY 2
#define PLAYER_ID_NEXT 3
#define PLAYER_ID_BAR  4

/* A broken stream and a finished film must never be the same value: getting it
 * wrong marks episodes watched and walks a season. */
typedef enum {
    PLAYER_ENDED = 0, /* the only one that marks it seen */
    PLAYER_STOPPED,   /* keep the resume point */
    PLAYER_PREV,
    PLAYER_NEXT,
    PLAYER_FAILED /* not watched, not advanced */
} player_result;

const char *player_result_text(player_result r);
int         player_result_is_watched(player_result r);
int         player_result_may_advance(player_result r);

extern const screen_def player_page_screen;

/* The item is copied: its listing may be replaced while a film runs. With
 * `has_prev`/`has_next` zero those controls register no box, so the cursor
 * steps over them. */
void player_page_show(const item *it, unsigned long long start_ticks, int has_prev, int has_next);

/* These outlive the page, for the resumed() hook of the screen underneath. */
int                player_page_result(void);
unsigned long long player_page_end_ticks(void);

int                player_page_showing(void);
int                player_page_paused(void);
int                player_page_seeking(void);
unsigned long long player_page_position(void);

#endif
