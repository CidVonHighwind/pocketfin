/* One row of a listing: a library, a series, a season, or something
 * playable. */
#ifndef JELLY_ITEM_H
#define JELLY_ITEM_H

#include <stdint.h>

#define ITEM_ID_LEN   40
#define ITEM_NAME_LEN 64

/* The server counts time in ticks of 100 ns. Kept 64-bit: cast to int a
   position overflows at about three and a half minutes, which is how an
   unwatched film once drew a watched tick. */
#define ITEM_TICKS_PER_S 10000000ull

typedef enum {
    ITEM_KIND_FOLDER = 0,
    ITEM_KIND_MOVIE,
    ITEM_KIND_SERIES,
    ITEM_KIND_SEASON,
    ITEM_KIND_EPISODE,
    /* A box set, or a folder inside a library: opened like a folder, but its
     * artwork is 2:3 like a film's, where a library's own image is 16:9. */
    ITEM_KIND_COLLECTION
} item_kind;

typedef struct {
    char      id[ITEM_ID_LEN]; /* undashed, as the API returns it */
    char      name[ITEM_NAME_LEN];
    item_kind kind;
    int       has_image;
    uint64_t  resume_ticks; /* where playback stopped, 0 if unwatched */
    uint64_t  run_ticks;    /* total run time, 0 when the source omits it */
    int       played;       /* watched to the end */
    int       favorite;

    /* No sort fields: the server sorts and filters (see query.h). */

    /* Resume and Next Up arrive flat, with no series above an episode, so a
     * row that names its show reads it from here. */
    char series[ITEM_NAME_LEN]; /* "" when not an episode */
    char series_id[ITEM_ID_LEN];
    int  season_no;  /* -1 when absent */
    int  episode_no; /* -1 when absent */
} item;

#endif
