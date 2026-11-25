/* See model/music.h. The server's AAC arrives as ADTS frames, and each one is
 * cut out and handed to the decoder; what the cut gets wrong is a song that
 * plays as noise or not at all. */

#include "model/music.h"

#include "jelly/query.h"
#include "tools/selftest.h"

#include <stdio.h>
#include <string.h>

/* An ADTS header for AAC-LC, 44.1 kHz, stereo, no CRC, `len` bytes in all. */
static void header(unsigned char *p, unsigned len) {
    p[0] = 0xFF;
    p[1] = 0xF1;                                   /* MPEG-4, layer 0, no CRC   */
    p[2] = (unsigned char)((1 << 6) | (4 << 2));   /* LC, 44100 (index 4)       */
    p[3] = (unsigned char)((2 << 6) | ((len >> 11) & 3)); /* 2 channels         */
    p[4] = (unsigned char)((len >> 3) & 0xFF);
    p[5] = (unsigned char)(((len & 7) << 5) | 0x1F);
    p[6] = 0xFC;
}

static int t_a_frame_is_cut_with_its_config(char *note, unsigned n) {
    unsigned char buf[400], asc[2] = {0, 0};
    unsigned      flen = 0, hlen = 0;

    memset(buf, 0x55, sizeof(buf));
    header(buf, 371);
    if (music_adts_frame(buf, sizeof(buf), &flen, &hlen, asc) != 1 || flen != 371 || hlen != 7) {
        snprintf(note, n, "read a frame of %u with a %u-byte header", flen, hlen);
        return 1;
    }
    /* AAC-LC (2), 44.1 kHz (4), two channels: 00010 0100 0010 000 */
    if (asc[0] != 0x12 || asc[1] != 0x10) {
        snprintf(note, n, "AudioSpecificConfig %02X %02X, not 12 10", asc[0], asc[1]);
        return 1;
    }
    return 0;
}

static int t_half_a_frame_waits_for_the_rest(char *note, unsigned n) {
    unsigned char buf[400];
    unsigned      flen, hlen;

    header(buf, 371);
    if (music_adts_frame(buf, 200, &flen, &hlen, 0) != 0 || music_adts_frame(buf, 5, &flen, &hlen, 0) != 0) {
        snprintf(note, n, "a cut frame was not asked to wait");
        return 1;
    }
    return 0;
}

static int t_noise_is_not_a_frame(char *note, unsigned n) {
    unsigned char buf[16] = {0x12, 0x34, 0xFF, 0x00, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    unsigned      flen, hlen;

    if (music_adts_frame(buf, sizeof(buf), &flen, &hlen, 0) != -1 || music_adts_frame(buf + 2, 14, &flen, &hlen, 0) != -1) {
        snprintf(note, n, "noise was read as a frame start");
        return 1;
    }
    /* A CRC'd header is nine bytes. */
    header(buf, 16);
    buf[1] = 0xF0;
    if (music_adts_frame(buf, 16, &flen, &hlen, 0) != 1 || hlen != 9) {
        snprintf(note, n, "a header with a CRC read as %u bytes", hlen);
        return 1;
    }
    return 0;
}

/* An album opens in this order; a sort with the wrong key plays it shuffled. */
static int t_track_order_is_disc_then_track(char *note, unsigned n) {
    if (strcmp(item_sort_key(ITEM_SORT_TRACK), "ParentIndexNumber,IndexNumber,SortName") != 0) {
        snprintf(note, n, "track order asks for %s", item_sort_key(ITEM_SORT_TRACK));
        return 1;
    }
    return 0;
}

void test_music_register(void) {
    selftest_add("music", "a frame is cut with its config", t_a_frame_is_cut_with_its_config);
    selftest_add("music", "half a frame waits for the rest", t_half_a_frame_waits_for_the_rest);
    selftest_add("music", "noise is not a frame", t_noise_is_not_a_frame);
    selftest_add("music", "track order is disc then track", t_track_order_is_disc_then_track);
}
