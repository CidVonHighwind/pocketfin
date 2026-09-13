/* See port/audio.h. A real AAC frame through the call the player makes, with
 * the AudioSpecificConfig the demuxer lifts out of the esds box: a wrong one
 * opens happily and refuses every frame, a film that plays silent with nothing
 * in the log.
 *
 * It makes a sound on purpose: a check that stopped at the decoder would pass
 * on a machine with no working output. */

#include "port/audio.h"

#include "../model/test_fmp4.h"
#include "tools/selftest.h"

#include <stdio.h>

/* Handed over one at a time, as the stream's sound worker does. */
#define KEEP 4
static struct {
    const unsigned char *data;
    unsigned             len;
} g_frame[KEEP];
static int g_frames;

static int keep_audio(const fmp4_sample *s, void *user) {
    (void)user;
    if (s->track != FMP4_AUDIO || g_frames >= KEEP) return 0;
    g_frame[g_frames].data = s->data;
    g_frame[g_frames].len  = s->len;
    g_frames++;
    return 0;
}

static int t_a_real_frame_plays(char *note, unsigned n) {
    fmp4 m;
    int  at, i, rc;

    if (!audio_available()) {
        snprintf(note, n, "this build has no sound");
        return -1;
    }
    if ((rc = frag_open(&m, note, n)) != 0) return rc;
    if (!m.audio.asc_len) {
        snprintf(note, n, "the fixture has no sound track");
        return -1;
    }

    at       = frag_moof(0);
    g_frames = 0;
    if (at < 0 || fmp4_fragment(&m, frag + at, frag_len - (unsigned)at, keep_audio, 0) <= 0) {
        snprintf(note, n, "%s", m.err);
        return 1;
    }
    if (!g_frames) {
        snprintf(note, n, "the fragment carried no sound");
        return 1;
    }

    if (audio_open(m.audio.asc, m.audio.asc_len, m.audio.rate, m.audio.channels) != 0) {
        snprintf(note, n, "%s", audio_error());
        return 1;
    }
    /* A few, because a decoder hands nothing back off the first one. */
    for (i = 0; i < g_frames; i++) (void)audio_play(g_frame[i].data, g_frame[i].len);

    {
        unsigned frames = 0, dry = 0;

        audio_stats(&frames, &dry);
        audio_close();

        if (!frames) {
            snprintf(note, n, "%d frames went in and none came out: %s", g_frames, audio_error());
            return 1;
        }
        snprintf(note, n, "%u of %d frames played at %u Hz, %u dry", frames, g_frames, m.audio.rate, dry);
    }
    return 0;
}

/* The console's output channel is 44,100 only -- the rate-converting one
   crackles even on a synthesised sine -- so another rate is refused where a
   viewer can be told. */
static int t_another_rate_is_refused(char *note, unsigned n) {
    static const unsigned char ASC[] = {0x12, 0x10};

    if (!audio_available()) {
        snprintf(note, n, "this build has no sound");
        return -1;
    }
    if (audio_open(ASC, sizeof(ASC), 48000, 2) == 0) {
        audio_close();
        snprintf(note, n, "48 kHz was accepted");
        return 1;
    }
    if (audio_open(0, 0, AUDIO_RATE, 2) == 0) {
        audio_close();
        snprintf(note, n, "it opened with no decoder configuration at all");
        return 1;
    }
    snprintf(note, n, "refused: %s", audio_error());
    return 0;
}

void test_audio_register(void) {
    selftest_add("audio", "a real frame plays", t_a_real_frame_plays);
    selftest_add("audio", "another rate is refused", t_another_rate_is_refused);
}
