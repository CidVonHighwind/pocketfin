/* Posters from the server. model/posters.c owns the pixels and is handed
   jf_poster() as its source. */
#ifndef JELLY_IMAGE_H
#define JELLY_IMAGE_H

#define JF_POSTER_NONE (-2) /* the item has no picture: settled, never asked again */

/* Decodes into `dst`, which outlives the call. 0 with the size in
   got_w/got_h. */
int jf_poster(const char *id, int out_w, int out_h, unsigned short *dst, int dst_w, int dst_h, int *got_w, int *got_h);

#endif
