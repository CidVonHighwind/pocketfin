/* The large-buffer budget: every big block a film needs, in one table.
 *
 * Claimed by the film, not at start-up: stream.c takes the rings when a film
 * opens and decode.c takes the decoder block at the first picture, and all
 * of it is held until app_stop(). Placement, not total, is what can fail:
 * the decoder wants 4 MB aligned to 4 MB out of a 14 MB heap, and a heap
 * fragmented by the browser may have no such run. mem_budget_ok() is the
 * check for that, and mem_reserve() logs the heap's state when it fails so
 * the failure is not a status nobody can trace.
 *
 * Only the console has a decoder; the desktop implements this as nothing. */
#ifndef PORT_MEM_H
#define PORT_MEM_H

typedef enum {
    MEM_DECODER = 0, /* 4 MB aligned to 4 MB */
    MEM_VIDEO_RING,
    MEM_AUDIO_RING,
    MEM_SEG_STAGE,
    MEM_N
} mem_id;

/* NULL when it could not be placed, and logged. Claiming twice returns the
 * same block. */
void    *mem_reserve(mem_id id);
unsigned mem_size(mem_id id);

/* The whole table, in order. 0 when all of it is held. After the network is
 * up: sceNetInit takes its own 512 kB, and a large claim ahead of it can
 * leave the radio nothing. */
int  mem_reserve_all(void);
void mem_release_all(void);

/* Whether the table could be placed, without keeping it. */
int mem_budget_ok(void);

/* The table and the partition either side of it, to the log. */
void mem_report(void);

#endif
