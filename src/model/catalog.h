/* What the server has, fetched on a worker: every jelly/api.h call blocks for
 * 55-107 ms of server think time against a 16.7 ms frame. Screens ask every
 * frame for what they show and draw what has arrived; asking is idempotent. */
#ifndef MODEL_CATALOG_H
#define MODEL_CATALOG_H

#include "jelly/item.h"
#include "jelly/query.h"
#include "jelly/api.h"
#include "model/session.h"

typedef enum { LIB_IDLE = 0, LIB_BUSY, LIB_READY, LIB_FAILED } lib_state;

typedef enum { LIB_RAIL_VIEWS = 0, LIB_RAIL_RESUME, LIB_RAIL_NEXT, LIB_RAIL_LATEST, LIB_RAIL_N } lib_rail;

/* /Items/Latest de-duplicates against the limit: asking for twenty returned a
 * different set than asking for ten. */
#define LIB_RAIL_ASK 10
#define LIB_RAIL_MAX 20
#define LIB_ITEM_MAX 128

const char *library_state_text(lib_state s);

int  library_start(void);
void library_stop(void);
void library_watch_link(void); /* once a frame */

/* Each getter asks for what it returns. */
typedef struct {
    lib_state   state;
    const char *error;
    const item *rail[LIB_RAIL_N];
    int         n[LIB_RAIL_N];
    lib_state   rail_state[LIB_RAIL_N];
} lib_home;

void library_home(lib_home *out);
void library_forget_home(void);

typedef struct {
    lib_state   state;
    const char *error;
    const item *rows; /* NULL unless they answer this exact ask */
    int         n, total;
    unsigned    gen; /* moves whenever the rows are replaced */
} lib_items;

void library_items(const char *parent_id, const char *title, item_sort sort, int descending, item_filter filter, lib_items *out);
void library_forget_items(void);

/* Episodes also ask for their neighbours. */
typedef struct {
    lib_state   state;
    const item *item; /* NULL until the answer for this id is in */
    const char      *overview;
    const jf_tracks *tracks; /* keyed like the overview */
} lib_item;

void library_item(const item *it, lib_item *out);
void library_forget_item(void);

/* NULL when the answer does not contain `id`: stepping from the first row
 * instead is how Next once played a series' first episode. */
const item *library_adjacent(const char *id, int after);

/* Marking played clears the resume point, with no undo. 0 while another mark
 * is in flight. */
typedef enum { LIB_MARK_PLAYED = 0, LIB_MARK_FAVOURITE } lib_mark;

int       library_mark(const char *id, lib_mark which, int on);
lib_state library_mark_state(void);

/* From model/reports.c when a stop lands: the rails are built from resume
 * points. */
void library_note_stopped(const char *id, unsigned long long ticks);

#endif
