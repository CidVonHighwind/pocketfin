/* Fragmented MP4 as the server sends it: an initialisation segment (ftyp +
 * moov, empty sample tables), then whole moof + mdat segments. Nothing is
 * allocated; samples point into the caller's buffer and are valid until it is
 * refilled. */
#ifndef MODEL_FMP4_H
#define MODEL_FMP4_H

/* An SPS is 30-50 bytes and a PPS under ten for every transcode this server
   produces; the boxes are refused rather than truncated if that changes. */
#define FMP4_PARAM_MAX 128

typedef enum { FMP4_VIDEO = 0, FMP4_AUDIO } fmp4_track;

typedef struct {
    unsigned id;
    unsigned timescale; /* never a constant: the server transcodes at the source rate */

    /* Video, present when sps_len is. */
    unsigned width, height;
    /* 1..4: samples are length-prefixed, not Annex-B, and a decoder told the
       wrong prefix reads the first length as picture data. */
    unsigned char nal_len_size;
    unsigned char sps[FMP4_PARAM_MAX];
    unsigned      sps_len;
    unsigned char pps[FMP4_PARAM_MAX];
    unsigned      pps_len;

    /* Audio, present when asc_len is. */
    unsigned      rate;
    unsigned char channels;
    unsigned char asc[FMP4_PARAM_MAX];
    unsigned      asc_len;
} fmp4_trak;

typedef struct {
    fmp4_trak video, audio;

    /* Written by every fragment: its last video sample's, 0 until one says. */
    unsigned sample_duration;

    char err[96];
} fmp4;

typedef struct {
    fmp4_track           track;
    const unsigned char *data;
    unsigned             len;
    unsigned long long   dts; /* in that track's own timescale */
    unsigned             duration;
} fmp4_sample;

/* Non-zero from the callback stops the walk. */
typedef int (*fmp4_sample_cb)(const fmp4_sample *s, void *user);

/* 0 when a video track came out of it. */
int fmp4_init(fmp4 *m, const unsigned char *data, unsigned len);

/* One moof + mdat. Returns how many samples of both tracks were handed over,
 * or -1 with m->err set. */
int fmp4_fragment(fmp4 *m, const unsigned char *data, unsigned len, fmp4_sample_cb cb, void *user);

#endif
