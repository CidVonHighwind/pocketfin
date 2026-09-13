/* Bytes off a socket to a picture: every piece of playback has checks of its
 * own, and each can pass while the joins between them are wrong.
 *
 * Against the loopback server, see tests/io/test_http.c, so desktop only; the
 * console's socket is proven by an app run. */

#ifndef __PSP__

#include "jelly/segments.h"
#include "model/fmp4.h"
#include "port/decode.h"
#include "port/gfx.h"
#include "port/platform.h"

#include "../io/test_http.h"
#include "test_fmp4.h"
#include "tools/selftest.h"

#include <stdio.h>
#include <string.h>

static unsigned char g_stage[SEG_STAGE_MIN];
static char          g_reply[48 * 1024];

static int fetch_bytes(segments *g, unsigned from, unsigned len, char *note, unsigned n) {
    jf_conn  c;
    unsigned head;

    memset(&c, 0, sizeof(c));
    head = (unsigned)snprintf(g_reply, sizeof(g_reply), "HTTP/1.1 200 OK\r\nContent-Length: %u\r\n\r\n", len);
    if (head + len > sizeof(g_reply)) {
        snprintf(note, n, "%u bytes do not fit a canned reply", len);
        return -1;
    }
    memcpy(g_reply + head, frag + from, len);

    /* In pieces: a segment arrives over as many packets as the link gives. */
    if (!fake_http_start(g_reply, head + len, 512)) {
        snprintf(note, n, "could not start a server");
        return -1;
    }
    snprintf(c.host, sizeof(c.host), "127.0.0.1");
    c.port = fake_http_port();
    jf_use(&c);

    if (seg_fetch(g, 0) != SEG_ARRIVED) {
        snprintf(note, n, "the segment did not arrive: %s", g->err);
        fake_http_wait();
        return -1;
    }
    fake_http_wait();
    return 0;
}

static int g_pictures;

static int feed(const fmp4_sample *s, void *user) {
    decode_picture *pic = (decode_picture *)user;

    /* An AAC frame handed to h264 leaves a decoder that refuses everything
       after it. */
    if (s->track != FMP4_VIDEO) return 0;
    if (decode_sample(s->data, s->len, pic) > 0) g_pictures++;
    return 0;
}

static int t_a_segment_becomes_a_picture(char *note, unsigned n) {
    segments        g;
    fmp4            m;
    jf_hls          h;
    decode_picture  pic;
    const unsigned *surf;
    int             moof, next, stride = 0, r, gr, b;

    if (!decode_available()) {
        snprintf(note, n, "this build has no decoder");
        return -1;
    }
    if (gfx_start() != 0) {
        snprintf(note, n, "the panel did not come up");
        return -1;
    }
    if (frag_load(note, n) != 0) return -1;

    memset(&h, 0, sizeof(h));
    memset(&pic, 0, sizeof(pic));
    g_pictures = 0;
    snprintf(h.dir, sizeof(h.dir), "/videos/abc");
    snprintf(h.query, sizeof(h.query), "api_key=x");
    if (seg_open(&g, &h, 0, g_stage, sizeof(g_stage)) != 0) {
        snprintf(note, n, "the fetcher would not open");
        return 1;
    }

    moof = frag_moof(0);
    if (moof <= 0) {
        snprintf(note, n, "the fixture has no fragment");
        return 1;
    }
    if (fetch_bytes(&g, 0, (unsigned)moof, note, n) != 0) return 1;

    if (fmp4_init(&m, g.stage, g.len) != 0) {
        snprintf(note, n, "%s", m.err);
        return 1;
    }
    if (decode_open(m.video.sps, m.video.sps_len, m.video.pps, m.video.pps_len, m.video.nal_len_size, (int)m.video.width,
                    (int)m.video.height) != 0) {
        snprintf(note, n, "%s", decode_error());
        return 1;
    }

    next = frag_moof(1);
    if (next < 0) next = (int)frag_len;
    if (fetch_bytes(&g, (unsigned)moof, (unsigned)(next - moof), note, n) != 0) {
        decode_close();
        return 1;
    }

    if (fmp4_fragment(&m, g.stage, g.len, feed, &pic) <= 0) {
        snprintf(note, n, "%s", m.err);
        decode_close();
        return 1;
    }
    if (!g_pictures) {
        snprintf(note, n, "a whole fragment gave no picture: %s", decode_error());
        decode_close();
        return 1;
    }

    decode_blit(0, 0);
    surf = (const unsigned *)decode_surface(&stride);
    if (!surf) {
        snprintf(note, n, "there is no surface to compose on");
        decode_close();
        return 1;
    }
    {
        unsigned px = surf[(pic.h / 2) * stride + (pic.w / 2)];

        r  = (int)(px & 0xFFu);
        gr = (int)((px >> 8) & 0xFFu);
        b  = (int)((px >> 16) & 0xFFu);
    }
    decode_close();

    if (gr < 96 || r > 96 || b > 96) {
        snprintf(note, n, "the picture reached the panel as r=%d g=%d b=%d", r, gr, b);
        return 1;
    }
    snprintf(note, n, "%llu bytes fetched, %d pictures, %dx%d green on the panel", g.bytes, g_pictures, pic.w, pic.h);
    return 0;
}

void test_chain_register(void) { selftest_add("chain", "a segment becomes a picture", t_a_segment_becomes_a_picture); }

#else
void test_chain_register(void) {}
#endif
