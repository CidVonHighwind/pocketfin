/* HLS segments off the server, one at a time. */
#ifndef JELLY_SEGMENTS_H
#define JELLY_SEGMENTS_H

#include "io/http.h"
#include "jelly/api.h"

typedef enum {
    SEG_ARRIVED = 0,
    SEG_NOT_READY, /* ask again in a moment */
    SEG_END,       /* the only result that ends a film */
    SEG_FAILED     /* retryable; never "watched to the end" */
} seg_result;

const char *seg_result_text(seg_result r);

/* Metadata runtime and the encoder's length disagree by a second or two:
   live data was served two segments past ceil(runtime). */
#define SEG_END_SLACK 8

/* Largest segment measured: 824 kB. */
#define SEG_STAGE_MIN (1024u * 1024u)

/* Taken off the end of the stage. An init is 1422 bytes, measured. */
#define SEG_INIT_MAX (16u * 1024u)

typedef struct {
    jf_hls hls;

    unsigned char *stage; /* lent by the caller */
    unsigned       stage_cap;
    unsigned       len;
    unsigned char *init; /* the stage's last SEG_INIT_MAX bytes */
    unsigned       init_len;

    /* -1 when unknown, and then every 404 is the end. */
    int last_seg;

    unsigned           fetch_ms, fetch_n;
    unsigned           ttfb_ms, body_ms; /* server think time vs our read */
    unsigned           pipelined;
    unsigned long long bytes;

    /* The next segment, asked for while this one's body arrives: the server
       thinks 80 ms before a first byte. */
    http_sock pre;
    int       pre_seg;
    unsigned  pre_epoch; /* the standby generation it was sent in */

    char err[96];
} segments;

/* cap must be at least SEG_STAGE_MIN. run_ticks 0: runtime unknown. */
int seg_open(segments *g, const jf_hls *h, unsigned long long run_ticks, unsigned char *stage, unsigned cap);

/* Mid-film a 404, a 416 and a silent connection all mean the encoder has not
   got there yet; near the end they mean the end. */
seg_result seg_verdict(const segments *g, int index, int status, http_result rc, unsigned len);

/* On SEG_ARRIVED the bytes are g->stage[0..len), valid until the next segment,
   or for index < 0 the init at g->init[0..init_len). A segment larger than the
   stage is dropped whole: a truncated fragment stalls the demuxer.

   The init only once a segment has arrived: Jellyfin serves an init at once
   only when a later segment is already written, and otherwise waits for
   segment 0, which a transcode started further in never writes -- 15 s, and
   once 175 s on the console. */
seg_result seg_fetch(segments *g, int index);

/* An open connection keeps the server's transcode alive. */
void seg_close(segments *g);

#endif
