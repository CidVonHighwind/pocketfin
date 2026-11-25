/* Music: a queue of songs played on a worker of its own, so it carries on
 * while the viewer browses. The server transcodes each song to AAC in ADTS
 * frames (jelly/api.h jf_audio_path), which go straight to the AAC decoder the
 * film player uses -- one output, so a film stops the music. */
#ifndef MODEL_MUSIC_H
#define MODEL_MUSIC_H

#include "jelly/item.h"

#define MUSIC_QUEUE_MAX 128

typedef enum { MUSIC_STOPPED = 0, MUSIC_LOADING, MUSIC_PLAYING, MUSIC_PAUSED } music_state;

typedef struct {
    music_state state;
    int         index, count; /* in the queue */
    item        now;          /* the song at `index` */
    unsigned    pos_s, dur_s;
    char        error[80]; /* the last song that would not play, and why */
} music_status;

/* Plays `items[start]` and then the rest, in order. Anything that is not a
   song is skipped. */
void music_play(const item *items, int n, int start);
void music_toggle_pause(void);
void music_next(void);
void music_prev(void); /* restarts a song more than three seconds in */
/* Returns once the output is free: the film player calls it before opening. */
void music_stop(void);

void music_status_get(music_status *out);
int  music_active(void); /* playing, loading or paused */

/* One ADTS frame at `p`: 1 with its total length and header length (and the
   two-byte AudioSpecificConfig it describes), 0 when `avail` holds less than
   a whole frame, -1 when `p` is not a frame start. */
int music_adts_frame(const unsigned char *p, unsigned avail, unsigned *frame_len, unsigned *hdr_len, unsigned char asc[2]);

#endif
