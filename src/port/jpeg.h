#ifndef PORT_JPEG_H
#define PORT_JPEG_H

#define JPEG_ERR_ARGS        (-1)
#define JPEG_ERR_STATE       (-2)
#define JPEG_ERR_HEADER      (-3)
#define JPEG_ERR_PROGRESSIVE (-4)
#define JPEG_ERR_TOO_BIG     (-5)
#define JPEG_ERR_SCRATCH     (-6)
#define JPEG_ERR_DECODE      (-7)

#define JPEG_MAX_W 480
#define JPEG_MAX_H 272

/* 16x16 macroblocks; scratch is 8888 (4 B/px) plus YCbCr (3/2 more), +64 B slack. */
#define JPEG_MB_UP(v) (((v) + 15) & ~15)
#define JPEG_SCRATCH_BYTES(w, h) \
    ((unsigned)(JPEG_MB_UP(w) * JPEG_MB_UP(h)) * 4u + (unsigned)(JPEG_MB_UP(w) * JPEG_MB_UP(h)) * 3u / 2u + 64u)

int jpeg_init(void);

/* MUST run before the video decoder opens: sceJpeg and sceMpeg share a unit. */
void jpeg_finish(void);

/* scratch is the caller's, sized for the largest artwork ever asked for.
 * One thread: sceJpeg keeps a single global decoder context. */
int jpeg_decode_rgb565(const void *jpg, unsigned jpg_len, void *out565, int out_w, int out_h, int *got_w, int *got_h, void *scratch,
                       unsigned scratch_len);

int jpeg_probe(const void *jpg, unsigned jpg_len, int *w, int *h);

/* Four bytes a pixel, R first. The fit keeps the aspect. */
void jpeg_shrink_565(const unsigned char *rgba, int pitch, int sw, int sh, void *out565, int out_w, int out_h, int *got_w, int *got_h);

const char *jpeg_error(void);

void jpeg_fail(const char *msg);
void jpeg_failf(const char *fmt, ...);
void jpeg_failc(const char *msg, int code);
void jpeg_err_clear(void);

#endif
