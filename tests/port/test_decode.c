/* See port/decode.h. A clip of one known colour through the demuxer and the
 * call the player makes, and the pixel read back: a film with red and blue
 * swapped still looks like a film -- the first render of Aguirre had green
 * mountains coming out pink and it read as a grade, not a bug.
 *
 * The clip is made by scripts/fixtures.py, so these skip without it. */

#include "port/decode.h"

#include "port/gfx.h"
#include "port/platform.h"
#include "../model/test_fmp4.h"
#include "tools/selftest.h"

#include <stdio.h>
#include <string.h>

/* Fed one at a time: the decoder wants more than one before it hands a
   picture over. */
#define KEEP 64
static struct {
    const unsigned char *data;
    unsigned             len;
} g_samp[KEEP];
static int g_samp_n;

static int keep(const fmp4_sample *s, void *user) {
    (void)user;
    if (g_samp_n >= KEEP) return 1;
    g_samp[g_samp_n].data = s->data;
    g_samp[g_samp_n].len  = s->len;
    g_samp_n++;
    return 0;
}

/* The first fragment's samples in g_samp and a decoder open on them: -1 when
   this cannot run here, 1 when a step failed. */
static int opened(fmp4 *m, char *note, unsigned n) {
    int at, rc;

    if (!decode_available()) {
        snprintf(note, n, "this build has no decoder");
        return -1;
    }
    /* On the console the copy to the panel is a display-list command and
       refuses outright without the engine up. */
    if (gfx_start() != 0) {
        snprintf(note, n, "the panel did not come up");
        return -1;
    }
    if ((rc = frag_open(m, note, n)) != 0) return rc;
    at       = frag_moof(0);
    g_samp_n = 0;
    if (at < 0 || fmp4_fragment(m, frag + at, frag_len - (unsigned)at, keep, 0) <= 0) {
        snprintf(note, n, "%s", m->err);
        return 1;
    }
    if (decode_open(m->video.sps, m->video.sps_len, m->video.pps, m->video.pps_len, m->video.nal_len_size, (int)m->video.width,
                    (int)m->video.height) != 0) {
        snprintf(note, n, "%s", decode_error());
        return 1;
    }
    return 0;
}

static int t_a_green_frame_is_green(char *note, unsigned n) {
    fmp4            m;
    decode_picture  pic;
    const unsigned *surf;
    int             i, got = 0, stride = 0, r, g, b, rc;
    unsigned        px;

    if ((rc = opened(&m, note, n)) != 0) return rc;

    memset(&pic, 0, sizeof(pic));
    for (i = 0; i < g_samp_n && got <= 0; i++) got = decode_sample(g_samp[i].data, g_samp[i].len, &pic);

    if (got <= 0) {
        snprintf(note, n, "%d samples gave no picture: %s", g_samp_n, decode_error());
        decode_close();
        return 1;
    }

    /* Read off the frame's surface, not a private buffer: that copy is where
       a format could still be lost. In a frame, because on the console reading
       the surface before the list has run gives black. */
    surf = (const unsigned *)decode_surface(&stride);
    if (!surf || stride <= 0) {
        snprintf(note, n, "the decoder has no surface to compose on");
        decode_close();
        return 1;
    }
    gfx_frame_begin_on((void *)surf, stride);
    decode_blit(0, 0);
    gfx_frame_end_on();

    /* Well inside it, clear of any edge the scaler softened. */
    px = surf[(pic.h / 2) * stride + (pic.w / 2)];
    r  = (int)(px & 0xFFu);
    g  = (int)((px >> 8) & 0xFFu);
    b  = (int)((px >> 16) & 0xFFu);
    decode_close();

    if (g < 96 || r > 96 || b > 96) {
        snprintf(note, n, "a green frame read r=%d g=%d b=%d -- the channels are the wrong way round", r, g, b);
        return 1;
    }
    snprintf(note, n, "green read r=%d g=%d b=%d at %dx%d, 8888, after %d samples", r, g, b, pic.w, pic.h, i);
    return 0;
}

/* A decoder that cannot keep up looks like starving, which sends everyone
   chasing the network. Half of a 25 fps frame: the drawing thread and fetch
   worker need the CPU in the same frame. */
#define FRAME_BUDGET_US 20000u

static int t_a_picture_decodes_inside_its_frame(char *note, unsigned n) {
    fmp4     m;
    int      i, pics = 0, rc;
    unsigned spent = 0, each;

    if ((rc = opened(&m, note, n)) != 0) return rc;

    for (i = 0; i < g_samp_n; i++) {
        decode_picture pic;
        unsigned       t0 = platform_clock_us();
        int            got;

        memset(&pic, 0, sizeof(pic));
        got = decode_sample(g_samp[i].data, g_samp[i].len, &pic);
        spent += platform_clock_us() - t0;
        if (got > 0) pics++;
    }
    decode_close();

    if (!pics) {
        snprintf(note, n, "%d samples gave no picture: %s", g_samp_n, decode_error());
        return 1;
    }
    each = spent / (unsigned)pics;
    if (each > FRAME_BUDGET_US) {
        snprintf(note, n, "%u us a picture over %d -- past the %u us a frame allows", each, pics, FRAME_BUDGET_US);
        return 1;
    }
    snprintf(note, n, "%u us a picture over %d, %u us of budget", each, pics, FRAME_BUDGET_US);
    return 0;
}

/* Refused at open, or it fails on the first sample with an error about the
   picture instead of the stream. */
static int t_no_parameter_sets_is_refused(char *note, unsigned n) {
    static const unsigned char SPS[] = {0x67, 0x42, 0x00, 0x1E};

    if (!decode_available()) {
        snprintf(note, n, "this build has no decoder");
        return -1;
    }
    if (decode_open(0, 0, 0, 0, 4, 64, 48) == 0) {
        snprintf(note, n, "it opened with no parameter sets at all");
        decode_close();
        return 1;
    }
    /* The avcC's length prefix is 1 to 4 bytes. */
    if (decode_open(SPS, sizeof(SPS), SPS, sizeof(SPS), 7, 64, 48) == 0) {
        snprintf(note, n, "it opened with a 7-byte nal length prefix");
        decode_close();
        return 1;
    }
    snprintf(note, n, "refused: %s", decode_error());
    return 0;
}

void test_decode_register(void) {
    selftest_add("decode", "a green frame is green", t_a_green_frame_is_green);
    selftest_add("decode", "a picture decodes inside its frame", t_a_picture_decodes_inside_its_frame);
    selftest_add("decode", "no parameter sets is refused", t_no_parameter_sets_is_refused);
}
