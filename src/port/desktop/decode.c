/* See port/decode.h -- this machine, through libavcodec.
 *
 * The decoder only, never libavformat: fetching and demuxing are
 * model/stream.c's, the code the console runs too.
 *
 * Everything here is 8888: sixteen bits bands every gradient in a film. */

#include "port/decode.h"

#include "base/align.h"
#include "base/log.h"
#include "port/gfx.h"

#include <stdio.h>
#include <string.h>

#ifndef POCKETFIN_FFMPEG

int decode_available(void) { return 0; }
int decode_open(const unsigned char *sps, unsigned sps_len, const unsigned char *pps, unsigned pps_len, unsigned nal_len_size, int src_w,
                int src_h) {
    (void)sps;
    (void)sps_len;
    (void)pps;
    (void)pps_len;
    (void)nal_len_size;
    (void)src_w;
    (void)src_h;
    return -1;
}
int decode_sample(const unsigned char *data, unsigned len, decode_picture *out) {
    (void)data;
    (void)len;
    (void)out;
    return -1;
}
/* Nothing to take: this port's surface is its own allocation. */
int decode_take_panel(void) { return 0; }

void *decode_surface(int *stride) {
    (void)stride;
    return 0;
}
void decode_blit(int x, int y) {
    (void)x;
    (void)y;
}
void        decode_show(void) {}
void        decode_close(void) {}
const char *decode_error(void) { return "this build has no decoder"; }

#else

#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

/* The console's stride, so both machines compose a film's frame in the same
   shape. */
#define SURF_STRIDE 512

static AVCodecContext    *g_dec;
static struct SwsContext *g_sws;
static AVFrame           *g_frame;
static AVPacket          *g_pkt;

/* The decoded picture, and the frame composed over it. */
static POCKETFIN_ALIGN16 unsigned g_pic[SURF_STRIDE * GFX_H];
static POCKETFIN_ALIGN16 unsigned g_surf[SURF_STRIDE * GFX_H];

static int  g_w, g_h; /* the picture, after the fit */
static int  g_fit_w, g_fit_h;
static int  g_open;
static char g_err[160];

int         decode_available(void) { return 1; }
const char *decode_error(void) { return g_err; }

static void say(const char *what, int rc) {
    char buf[64];

    av_strerror(rc, buf, sizeof(buf));
    snprintf(g_err, sizeof(g_err), "%s: %s", what, buf);
    log_printf("decode: %s", g_err);
}

void decode_close(void) {
    if (g_sws) {
        sws_freeContext(g_sws);
        g_sws = 0;
    }
    if (g_dec) avcodec_free_context(&g_dec);
    if (g_frame) av_frame_free(&g_frame);
    if (g_pkt) av_packet_free(&g_pkt);
    g_w = g_h = 0;
    g_open    = 0;
}

/* The avcC box, rebuilt: libavcodec configures an h264 decoder from that box
   rather than from loose parameter sets, and it is also what says how long
   each NAL's length prefix is -- the one number that cannot be inferred from
   the bytes. Eleven bytes of header around the two sets. */
static int extradata(const unsigned char *sps, unsigned sps_len, const unsigned char *pps, unsigned pps_len, unsigned nal_len_size) {
    unsigned       n = 11 + sps_len + pps_len;
    unsigned char *p;

    if (sps_len < 4) return -1;
    p = (unsigned char *)av_mallocz(n + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!p) return -1;

    p[0] = 1;      /* configurationVersion  */
    p[1] = sps[1]; /* AVCProfileIndication  */
    p[2] = sps[2]; /* profile_compatibility */
    p[3] = sps[3]; /* AVCLevelIndication    */
    p[4] = (unsigned char)(0xFC | ((nal_len_size - 1) & 3));
    p[5] = (unsigned char)(0xE0 | 1); /* one SPS */
    p[6] = (unsigned char)(sps_len >> 8);
    p[7] = (unsigned char)sps_len;
    memcpy(p + 8, sps, sps_len);
    p[8 + sps_len]  = 1; /* one PPS */
    p[9 + sps_len]  = (unsigned char)(pps_len >> 8);
    p[10 + sps_len] = (unsigned char)pps_len;
    memcpy(p + 11 + sps_len, pps, pps_len);

    g_dec->extradata      = p;
    g_dec->extradata_size = (int)n;
    return 0;
}

int decode_open(const unsigned char *sps, unsigned sps_len, const unsigned char *pps, unsigned pps_len, unsigned nal_len_size, int src_w,
                int src_h) {
    const AVCodec *codec;
    int            rc;

    decode_close();
    g_err[0] = 0;

    if (!sps || !sps_len || !pps || !pps_len || nal_len_size < 1 || nal_len_size > 4) {
        snprintf(g_err, sizeof(g_err), "no parameter sets");
        return -1;
    }

    codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    g_dec = codec ? avcodec_alloc_context3(codec) : 0;
    if (!g_dec) {
        snprintf(g_err, sizeof(g_err), "no h264 decoder in this build");
        return -1;
    }
    if (extradata(sps, sps_len, pps, pps_len, nal_len_size) != 0) {
        snprintf(g_err, sizeof(g_err), "the parameter sets would not fit an avcC");
        decode_close();
        return -1;
    }
    if ((rc = avcodec_open2(g_dec, codec, 0)) < 0) {
        say("the decoder would not open", rc);
        decode_close();
        return -1;
    }

    g_frame = av_frame_alloc();
    g_pkt   = av_packet_alloc();
    if (!g_frame || !g_pkt) {
        snprintf(g_err, sizeof(g_err), "no frame or packet");
        decode_close();
        return -1;
    }

    /* The panel is the fit, never larger: a stream smaller than it is left
       alone rather than blown up, which is what the console does by having
       no scaler at all. */
    g_fit_w = (src_w > 0 && src_w < GFX_W) ? src_w : GFX_W;
    g_fit_h = (src_h > 0 && src_h < GFX_H) ? src_h : GFX_H;
    memset(g_surf, 0, sizeof(g_surf));
    g_open = 1;
    return 0;
}

/* Built on the first picture, not at open: the coded size is only known once
   the decoder has read the SPS. */
static int scaler_ready(void) {
    int sw = g_frame->width, sh = g_frame->height;

    if (g_sws) return 0;
    if (sw <= 0 || sh <= 0) return -1;

    g_w = g_fit_w;
    g_h = (int)((long long)sh * g_fit_w / sw);
    if (g_h > g_fit_h) {
        g_h = g_fit_h;
        g_w = (int)((long long)sw * g_fit_h / sh);
    }

    /* RGBA is 0xAABBGGRR as a word: the console's GU_PSM_8888 exactly. */
    g_sws = sws_getContext(sw, sh, (enum AVPixelFormat)g_frame->format, g_w, g_h, AV_PIX_FMT_RGBA, SWS_BILINEAR, 0, 0, 0);
    if (!g_sws) {
        snprintf(g_err, sizeof(g_err), "no scaler for %dx%d", sw, sh);
        return -1;
    }
    log_printf("decode: h264 %dx%d -> %dx%d, 8888", sw, sh, g_w, g_h);
    return 0;
}

int decode_sample(const unsigned char *data, unsigned len, decode_picture *out) {
    unsigned char *dst[4]   = {0};
    int            pitch[4] = {0};
    int            rc;

    if (!g_dec || !out) return -1;

    if (data && len) {
        /* libavcodec copies a packet with no buffer of its own, so the ring's
           bytes are free again at once. */
        g_pkt->data = (unsigned char *)data;
        g_pkt->size = (int)len;
        rc          = avcodec_send_packet(g_dec, g_pkt);
        if (rc < 0 && rc != AVERROR(EAGAIN)) {
            say("the decoder refused a sample", rc);
            return -1;
        }
    }

    rc = avcodec_receive_frame(g_dec, g_frame);
    if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) return 0;
    if (rc < 0) {
        say("the decoder refused to hand a picture over", rc);
        return -1;
    }
    if (scaler_ready() != 0) return -1;

    dst[0]   = (unsigned char *)g_pic;
    pitch[0] = SURF_STRIDE * (int)sizeof(*g_pic);
    sws_scale(g_sws, (const unsigned char *const *)g_frame->data, g_frame->linesize, 0, g_frame->height, dst, pitch);

    out->w = g_w;
    out->h = g_h;
    return 1;
}

int decode_take_panel(void) { return 0; }

void *decode_surface(int *stride) {
    if (!g_open) return 0;
    if (stride) *stride = SURF_STRIDE;
    return g_surf;
}

void decode_blit(int x, int y) {
    if (!g_open || g_w <= 0 || g_h <= 0 || x < 0 || y < 0) return;
    if (y + g_h > GFX_H) return;
    gfx_copy8888(g_pic, SURF_STRIDE, 0, 0, g_surf, SURF_STRIDE, x, y, g_w, g_h);
}

/* The shell reads the composed frame back through the capture seam every
   frame, film or no film. */
void decode_show(void) {}

#endif /* POCKETFIN_FFMPEG */
