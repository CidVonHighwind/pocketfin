/* The console's Media Engine, video memory and panel move together, in one
 * of two modes that only this seam changes:
 *
 *   IDLE  the browser draws in 5-6-5, stills may be decoded
 *   FILM  a film owns all three
 *
 * The film's buffers are handed out BY the claim, so a caller cannot draw
 * into memory it was never given nor keep drawing after giving it back.
 * sceJpeg and sceMpeg are the same silicon. */
#ifndef PORT_MEDIA_H
#define PORT_MEDIA_H

/* All 8888, in video memory, and none of them valid after media_film_give(). */
typedef struct {
    void *picture;    /* what the engine decodes into, uncached */
    void *panel[2];   /* what the panel is shown, by address    */
    void *panel_u[2]; /* the same, uncached, to draw into       */
    int   count;
    int   stride; /* pixels a row */
} media_film;

/* Before any worker exists: the lock the two below share is made here, not
 * by whichever thread asks first. */
void media_start(void);

/* Waits for a still in flight -- milliseconds, against a film about to run for
 * an hour. Zero on success and `out` is filled in. */
int media_film_take(media_film *out);

/* The panel back to the browser in 5-6-5 and the memory to whoever draws next.
 * Safe when nothing was taken. */
void media_film_give(void);

int media_film_has_it(void);

/* Non-zero means a film has the hardware: come back later rather than wait,
 * since the page a still is wanted for is behind that film. */
int  media_still_begin(void);
void media_still_end(void);

#endif
