#include "jelly/image.h"

#include "jelly/api.h"
#include "io/http.h"
#include "port/media.h"
#include "port/jpeg.h"
#include "port/gfx.h"
#include "base/log.h"

#include <stdio.h>
#include <string.h>

/* Refused past the cap, not truncated: a cut JPEG still decodes. */
typedef struct {
    unsigned char *p;
    unsigned       cap, len;
    int            overflowed;
} jpg_sink;

static int collect_jpg(const unsigned char *data, unsigned len, void *user) {
    jpg_sink *j = (jpg_sink *)user;

    if (j->len + len > j->cap) {
        j->overflowed = 1;
        return 1;
    }
    memcpy(j->p + j->len, data, len);
    j->len += len;
    return 0;
}

int jf_poster(const char *id, int out_w, int out_h, unsigned short *dst, int dst_w, int dst_h, int *got_w, int *got_h) {
    /* One poster worker, and the scratch alone is many times its stack. */
    static unsigned char  jpg[64 * 1024];
    static unsigned char  scratch[JPEG_SCRATCH_BYTES(256, 256)];
    static unsigned short out[256 * 256];

    char        path[192], err[96], host[JF_HOST_LEN];
    jpg_sink    sink;
    int         y, gw = 0, gh = 0, status = 0, port, decoded;
    http_result rc;

    if (got_w) *got_w = 0;
    if (got_h) *got_h = 0;
    if (!jf_signed_in() || !id || !id[0] || !dst || dst_w <= 0 || dst_h <= 0) return -1;

    if (out_w > dst_w) out_w = dst_w;
    if (out_h > dst_h) out_h = dst_h;
    if (out_w > 256) out_w = 256;
    if (out_h > 256) out_h = 256;

    /* Rounded to 16 for the Media Engine's direct decode path. quality=95
       measures 40.2 dB against 39.4 dB for 5-6-5 quantisation alone. */
    snprintf(path, sizeof(path), "/Items/%s/Images/Primary?maxWidth=%d&maxHeight=%d&format=Jpg&quality=95", id, (out_w + 15) & ~15,
             (out_h + 15) & ~15);
    jf_address(host, sizeof(host), &port);

    sink.p          = jpg;
    sink.cap        = (unsigned)sizeof(jpg);
    sink.len        = 0;
    sink.overflowed = 0;
    rc              = http_stream(HTTP_SOCK_NONE, host, port, path, collect_jpg, &sink, 0, 0, &status, 0, err, sizeof(err));

    if (rc != HTTP_OK || status != 200) {
        if (status == 404) return JF_POSTER_NONE;
        log_printf("poster: %s -- %s, status %d", id, http_result_text(rc), status);
        return -1;
    }
    if (sink.overflowed) {
        log_printf("poster: %s is larger than %u bytes", id, (unsigned)sizeof(jpg));
        return -1;
    }

    /* Refused while a film holds the decode engine. */
    if (media_still_begin() != 0) return -1;
    decoded = jpeg_init() == 0 && jpeg_decode_rgb565(jpg, sink.len, out, out_w, out_h, &gw, &gh, scratch, sizeof(scratch)) == 0;
    jpeg_finish();
    media_still_end();
    if (!decoded) return -1;

    for (y = 0; y < gh; y++) memcpy(dst + y * dst_w, out + y * gw, (unsigned)gw * sizeof(*out));
    /* Without the flush a card drew in bands. */
    gfx_wrote_pixels();

    if (got_w) *got_w = gw;
    if (got_h) *got_h = gh;
    return 0;
}
