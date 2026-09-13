/* See model/fmp4.h. Against a real file: a fixture assembled by hand only
 * proves the parser agrees with whoever wrote the fixture. scripts/fixtures.py
 * makes it with ffmpeg, so these skip without it. */

#include "model/fmp4.h"
#include "test_fmp4.h"

#include "port/platform.h"
#include "io/link.h"
#include "tools/selftest.h"

#include <stdio.h>
#include <string.h>

unsigned char frag[64 * 1024];
unsigned      frag_len;

int frag_load(char *note, unsigned n) {
    frag_len = hostfs_slurp("frag.mp4", (char *)frag, sizeof(frag));
    if (!frag_len) {
        snprintf(note, n, "no frag.mp4 on the link -- run scripts/fixtures.py");
        return -1;
    }
    return frag_len > 64 ? 0 : -1;
}

int frag_open(fmp4 *m, char *note, unsigned n) {
    if (frag_load(note, n) != 0) return -1;
    if (fmp4_init(m, frag, frag_len) != 0) {
        snprintf(note, n, "%s", m->err);
        return 1;
    }
    return 0;
}

/* The parser is under test, so the check walks the boxes itself. */
static unsigned box_size(int at) {
    const unsigned char *p = frag + at;

    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3];
}

int frag_moof(int which) {
    unsigned pos  = 0;
    int      seen = 0;

    while (pos + 8 <= frag_len) {
        unsigned sz = box_size((int)pos);

        if (sz < 8 || pos + sz > frag_len) return -1;
        if (memcmp(frag + pos + 4, "moof", 4) == 0 && seen++ == which) return (int)pos;
        pos += sz;
    }
    return -1;
}

static int g_count_v, g_count_a;

static int count_tracks(const fmp4_sample *s, void *user) {
    (void)user;
    if (s->track == FMP4_VIDEO)
        g_count_v++;
    else
        g_count_a++;
    return 0;
}

typedef struct {
    int                  n;
    unsigned             total;
    unsigned long long   first_dts, last_dts;
    int                  inside; /* every sample's bytes lay in the segment */
    const unsigned char *base;
    unsigned             base_len;
} seen;

static void seen_over(seen *k, int at, unsigned len) {
    memset(k, 0, sizeof(*k));
    k->inside   = 1;
    k->base     = frag + at;
    k->base_len = len;
}

static int note_sample(const fmp4_sample *s, void *user) {
    seen *k = (seen *)user;

    /* Both tracks are interleaved on different timescales: counted together
       they make a frame rate out of two clocks. */
    if (s->track != FMP4_VIDEO) return 0;
    if (s->data < k->base || s->data + s->len > k->base + k->base_len) k->inside = 0;
    if (!k->n) k->first_dts = s->dts;
    k->last_dts = s->dts;
    k->total += s->len;
    k->n++;
    return 0;
}

/* Without the length prefix size the first four bytes of every picture read
   as data. */
static int t_the_init_segment_configures_a_decoder(char *note, unsigned n) {
    fmp4 m;
    int  rc;

    if ((rc = frag_open(&m, note, n)) != 0) return rc;
    if (m.video.width != 320 || m.video.height != 240) {
        snprintf(note, n, "%ux%u, not 320x240", m.video.width, m.video.height);
        return 1;
    }
    if (!m.video.sps_len || !m.video.pps_len) {
        snprintf(note, n, "sps %u bytes, pps %u", m.video.sps_len, m.video.pps_len);
        return 1;
    }
    if (m.video.nal_len_size != 4) {
        snprintf(note, n, "the nal length prefix is %u bytes", m.video.nal_len_size);
        return 1;
    }
    if (!m.video.timescale) {
        snprintf(note, n, "no timescale");
        return 1;
    }
    snprintf(note, n, "%ux%u, sps %u pps %u, timescale %u", m.video.width, m.video.height, m.video.sps_len, m.video.pps_len,
             m.video.timescale);
    return 0;
}

/* An offset read against the wrong base hands the decoder the middle of
   another picture, which decodes to something instead of failing. */
static int t_a_fragment_hands_over_its_samples(char *note, unsigned n) {
    fmp4 m;
    seen k;
    int  at, got, rc;

    if ((rc = frag_open(&m, note, n)) != 0) return rc;
    at = frag_moof(0);
    if (at < 0) {
        snprintf(note, n, "no fragment in the fixture");
        return 1;
    }
    seen_over(&k, at, frag_len - (unsigned)at);

    got = fmp4_fragment(&m, k.base, k.base_len, note_sample, &k);
    if (got <= 0) {
        snprintf(note, n, "%s", m.err);
        return 1;
    }
    if (got < k.n) {
        snprintf(note, n, "it returned %d and handed over %d pictures", got, k.n);
        return 1;
    }
    if (!k.inside) {
        snprintf(note, n, "a sample pointed outside the segment");
        return 1;
    }
    if (!m.sample_duration) {
        snprintf(note, n, "no sample duration came out of it");
        return 1;
    }
    /* The fixture is made at 25 fps. */
    if (m.video.timescale / m.sample_duration != 25) {
        snprintf(note, n, "timescale %u over duration %u is not 25 fps", m.video.timescale, m.sample_duration);
        return 1;
    }
    snprintf(note, n, "%d samples, %u bytes, %u/%u = 25 fps", got, k.total, m.video.timescale, m.sample_duration);
    return 0;
}

/* A segment is 1.001 s of film, ~68 samples (25 pictures, 43 sound frames),
   walked on the worker that fetches, so its cost comes off the fetching.
   Budget: 1.4 ms a sample. */
#define SEGMENT_SAMPLES 68u
#define DEMUX_BUDGET_US 100000u

static int t_cutting_a_segment_up_is_cheap(char *note, unsigned n) {
    fmp4     m;
    seen     k;
    int      at, got, i, rc;
    unsigned t0, spent, each, segment;

    if ((rc = frag_open(&m, note, n)) != 0) return rc;
    at = frag_moof(0);
    if (at < 0) {
        snprintf(note, n, "no fragment in the fixture");
        return 1;
    }
    seen_over(&k, at, frag_len - (unsigned)at);

    /* One walk is a handful of microseconds, the clock's own resolution. */
    t0  = platform_clock_us();
    got = 0;
    for (i = 0; i < 10; i++) got = fmp4_fragment(&m, k.base, k.base_len, note_sample, &k);
    spent = platform_clock_us() - t0;

    if (got <= 0) {
        snprintf(note, n, "%s", m.err);
        return 1;
    }
    each    = spent / (10u * (unsigned)got);
    segment = each * SEGMENT_SAMPLES;
    if (segment > DEMUX_BUDGET_US) {
        snprintf(note, n, "%u us a sample is %u ms a segment -- past the %u ms allowed", each, segment / 1000u, DEMUX_BUDGET_US / 1000u);
        return 1;
    }
    snprintf(note, n, "%u us a sample, %u ms for a segment of %u", each, segment / 1000u, SEGMENT_SAMPLES);
    return 0;
}

/* Positions come from each fragment's tfdt: a clock that starts again plays
   the film from zero every second. */
static int t_the_second_fragment_continues_the_first(char *note, unsigned n) {
    fmp4 m;
    seen one, two;
    int  a, b, rc;

    if ((rc = frag_open(&m, note, n)) != 0) return rc;
    a = frag_moof(0);
    b = frag_moof(1);
    if (a < 0 || b < 0) {
        snprintf(note, n, "the fixture has one fragment");
        return -1;
    }
    seen_over(&one, a, (unsigned)(b - a));
    seen_over(&two, b, frag_len - (unsigned)b);

    if (fmp4_fragment(&m, one.base, one.base_len, note_sample, &one) <= 0 ||
        fmp4_fragment(&m, two.base, two.base_len, note_sample, &two) <= 0) {
        snprintf(note, n, "%s", m.err);
        return 1;
    }
    if (two.first_dts <= one.last_dts) {
        snprintf(note, n, "the second starts at %llu, the first ended at %llu", two.first_dts, one.last_dts);
        return 1;
    }
    if (two.first_dts != one.last_dts + m.sample_duration) {
        snprintf(note, n, "a gap of %llu between the fragments", two.first_dts - one.last_dts);
        return 1;
    }
    snprintf(note, n, "%llu then %llu, one sample apart", one.first_dts, two.first_dts);
    return 0;
}

/* Never partly emitted: the samples that happen to fit feed a decoder the
   front of a picture whose rest never comes, and it hangs rather than says
   no. */
static int t_a_cut_fragment_is_refused(char *note, unsigned n) {
    fmp4 m;
    seen k;
    int  at, got, rc;

    if ((rc = frag_open(&m, note, n)) != 0) return rc;
    at = frag_moof(0);
    seen_over(&k, at, (unsigned)(frag_moof(1) - at) / 2u);

    got = fmp4_fragment(&m, k.base, k.base_len, note_sample, &k);
    if (got >= 0) {
        snprintf(note, n, "half a fragment gave %d samples", got);
        return 1;
    }
    snprintf(note, n, "refused: %s", m.err);
    return 0;
}

/* With no init there is no track to attribute samples to: parsing anyway reads
   a sample table against nothing. */
static int t_a_fragment_without_an_init_is_refused(char *note, unsigned n) {
    fmp4 m;
    int  at;

    if (frag_load(note, n) != 0) return -1;
    memset(&m, 0, sizeof(m));
    at = frag_moof(0);

    if (fmp4_fragment(&m, frag + at, frag_len - (unsigned)at, 0, 0) >= 0) {
        snprintf(note, n, "it parsed a fragment with no init segment");
        return 1;
    }
    snprintf(note, n, "refused: %s", m.err);
    return 0;
}

/* Without the AudioSpecificConfig a film plays silent for no reason anybody
   can see. */
static int t_the_init_segment_carries_the_sound(char *note, unsigned n) {
    fmp4 m;
    int  rc;

    if ((rc = frag_open(&m, note, n)) != 0) return rc;
    if (!m.audio.asc_len) {
        snprintf(note, n, "no audio track came out of a fixture that has one");
        return 1;
    }
    if (m.audio.asc_len > 8) {
        snprintf(note, n, "the decoder configuration is %u bytes", m.audio.asc_len);
        return 1;
    }
    if (m.audio.rate != 44100 || m.audio.channels != 2) {
        snprintf(note, n, "%u Hz, %u channels -- the hardware takes 44100 stereo", m.audio.rate, m.audio.channels);
        return 1;
    }
    if (m.audio.id == m.video.id) {
        snprintf(note, n, "both tracks claim id %u", m.video.id);
        return 1;
    }
    snprintf(note, n, "%u Hz stereo, %u bytes of config, track %u", m.audio.rate, m.audio.asc_len, m.audio.id);
    return 0;
}

/* Told apart by track id, not size or order: a stream with the sound first is
   just as ordinary. */
static int t_a_fragment_carries_both_tracks(char *note, unsigned n) {
    fmp4 m;
    int  at, rc;

    if ((rc = frag_open(&m, note, n)) != 0) return rc;
    at        = frag_moof(0);
    g_count_v = 0;
    g_count_a = 0;
    if (fmp4_fragment(&m, frag + at, frag_len - (unsigned)at, count_tracks, 0) <= 0) {
        snprintf(note, n, "%s", m.err);
        return 1;
    }
    if (!g_count_v || !g_count_a) {
        snprintf(note, n, "%d pictures and %d sound frames", g_count_v, g_count_a);
        return 1;
    }
    snprintf(note, n, "%d pictures and %d sound frames out of one fragment", g_count_v, g_count_a);
    return 0;
}

void test_fmp4_register(void) {
    selftest_add("fmp4", "the init segment configures a decoder", t_the_init_segment_configures_a_decoder);
    selftest_add("fmp4", "a fragment hands over its samples", t_a_fragment_hands_over_its_samples);
    selftest_add("fmp4", "the second fragment continues the first", t_the_second_fragment_continues_the_first);
    selftest_add("fmp4", "a cut fragment is refused", t_a_cut_fragment_is_refused);
    selftest_add("fmp4", "a fragment without an init is refused", t_a_fragment_without_an_init_is_refused);
    selftest_add("fmp4", "the init segment carries the sound", t_the_init_segment_carries_the_sound);
    selftest_add("fmp4", "a fragment carries both tracks", t_a_fragment_carries_both_tracks);
    selftest_add("fmp4", "cutting a segment up is cheap", t_cutting_a_segment_up_is_cheap);
}
