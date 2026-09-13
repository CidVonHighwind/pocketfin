/* Pictures, in one store the screens share, decoded away from the drawing
 * thread. A screen asks every frame for each picture it draws, and asking is
 * what keeps it: what nothing asked for in two frames may be evicted. */
#ifndef MODEL_POSTERS_H
#define MODEL_POSTERS_H

#include "jelly/image.h"

/* 0 with the size decoded into dst; JF_POSTER_NONE when there is no picture;
   anything else is worth another go. */
typedef int (*poster_source)(const char *id, int out_w, int out_h, unsigned short *dst, int dst_w, int dst_h, int *got_w, int *got_h);

/* `pack_path` NULL runs without the card cache; `pack_cap` 0 is the default. */
int  posters_start(poster_source source, const char *pack_path, unsigned pack_cap);
void posters_stop(void);

/* The rest are the drawing thread's. */

/* The picture, or NULL and asked for. `waiting` is 1 while it is still coming
   and 0 once there is none. Any out pointer may be NULL. */
const unsigned short *poster_get(const char *id, int w, int h, int *tex_w, int *tex_h, int *got_w, int *got_h, unsigned *waiting);
void                  poster_frame(void);

void posters_clear(void);
void posters_cache_clear(void); /* done by the worker, between pictures */
void posters_cache_stats(unsigned *bytes, int *records);
void posters_times(unsigned *from_pack, unsigned *pack_us, unsigned *source_us);

#endif
