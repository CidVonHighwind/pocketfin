/* See base/jpeg_header.c. jpeg_probe() reads bytes off the network before
 * anything is allocated for them, so every case here is hostile or truncated;
 * test_jpeg.c covers real posters. */

#include "port/jpeg.h"

#include "tools/selftest.h"

#include <stdio.h>
#include <string.h>

/* SOI and a three-component SOF: 21 bytes, the shortest sof_size() accepts. */
static unsigned build_frame(unsigned char *b, unsigned char sof, int w, int h) {
    unsigned n = 0;
    int      i;

    b[n++] = 0xFF;
    b[n++] = 0xD8;
    b[n++] = 0xFF;
    b[n++] = sof;
    b[n++] = 0x00;
    b[n++] = 0x11; /* segment length, 17, counts itself */
    b[n++] = 0x08;
    b[n++] = (unsigned char)(h >> 8);
    b[n++] = (unsigned char)(h & 0xFF);
    b[n++] = (unsigned char)(w >> 8);
    b[n++] = (unsigned char)(w & 0xFF);
    b[n++] = 0x03;
    for (i = 0; i < 3; i++) {
        b[n++] = (unsigned char)(i + 1);
        b[n++] = 0x11;
        b[n++] = 0x00;
    }
    return n;
}

static int t_a_well_formed_header_gives_the_right_size(char *note, unsigned n) {
    unsigned char b[32];
    unsigned      len = build_frame(b, 0xC0, 320, 240);
    int           w = 0, h = 0;

    if (jpeg_probe(b, len, &w, &h) != 0) {
        snprintf(note, n, "a well-formed baseline header was refused: %s", jpeg_error());
        return 1;
    }
    if (w != 320 || h != 240) {
        snprintf(note, n, "read %dx%d, wanted 320x240", w, h);
        return 1;
    }
    snprintf(note, n, "21 bytes read as 320x240");
    return 0;
}

/* A wrong URL, or a redirect to an HTML error page. */
static int t_bad_magic_is_refused(char *note, unsigned n) {
    static const unsigned char png[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    int                        w = 0, h = 0;

    if (jpeg_probe(png, sizeof(png), &w, &h) != JPEG_ERR_HEADER) {
        snprintf(note, n, "a PNG signature was read as a %dx%d JPEG", w, h);
        return 1;
    }
    snprintf(note, n, "PNG magic refused with JPEG_ERR_HEADER");
    return 0;
}

/* A connection cut before four bytes must not reach sof_size() at all. */
static int t_zero_length_and_very_short_inputs_are_refused(char *note, unsigned n) {
    static const unsigned char soi[] = {0xFF, 0xD8};
    int                        w = 0, h = 0;

    if (jpeg_probe(0, 0, &w, &h) != JPEG_ERR_ARGS) {
        snprintf(note, n, "a null, zero-length buffer was not refused as bad arguments");
        return 1;
    }
    if (jpeg_probe(soi, sizeof(soi), &w, &h) != JPEG_ERR_ARGS) {
        snprintf(note, n, "a 2-byte SOI-only buffer was not refused as bad arguments");
        return 1;
    }
    snprintf(note, n, "0 and 2 bytes both refused before any scan");
    return 0;
}

static int t_a_header_truncated_before_the_length_field(char *note, unsigned n) {
    unsigned char b[32];
    int           w = 0, h = 0;

    build_frame(b, 0xC0, 320, 240);
    if (jpeg_probe(b, 4, &w, &h) != JPEG_ERR_HEADER) {
        snprintf(note, n, "4 bytes (SOI + marker, no length) read as %dx%d", w, h);
        return 1;
    }
    snprintf(note, n, "4 bytes refused: no length field yet");
    return 0;
}

static int t_a_header_truncated_before_the_payload(char *note, unsigned n) {
    unsigned char b[32];
    int           w = 0, h = 0;

    build_frame(b, 0xC0, 320, 240);
    if (jpeg_probe(b, 6, &w, &h) != JPEG_ERR_HEADER) {
        snprintf(note, n, "6 bytes (length field, no payload) read as %dx%d", w, h);
        return 1;
    }
    snprintf(note, n, "6 bytes refused: length field present, payload is not");
    return 0;
}

/* Height and width are present at 15 bytes, but the segment claims 17. */
static int t_a_header_truncated_inside_the_payload(char *note, unsigned n) {
    unsigned char b[32];
    int           w = 0, h = 0;

    build_frame(b, 0xC0, 320, 240);
    if (jpeg_probe(b, 15, &w, &h) != JPEG_ERR_HEADER) {
        snprintf(note, n, "15 of 21 bytes, width/height present, still read as %dx%d", w, h);
        return 1;
    }
    snprintf(note, n, "15 bytes refused even though width/height bytes are present");
    return 0;
}

static int t_a_segment_length_past_the_buffer_is_refused(char *note, unsigned n) {
    static const unsigned char b[10] = {0xFF, 0xD8, 0xFF, 0xE0, 0xFF, 0xFF, 0, 0, 0, 0};
    int                        w = 0, h = 0;

    if (jpeg_probe(b, sizeof(b), &w, &h) != JPEG_ERR_HEADER) {
        snprintf(note, n, "a segment claiming 65535 bytes in a 10-byte buffer read as %dx%d", w, h);
        return 1;
    }
    snprintf(note, n, "a 65535-byte segment in 10 bytes refused");
    return 0;
}

/* The length field counts itself, so below 2 is impossible. */
static int t_a_segment_length_below_the_minimum_is_refused(char *note, unsigned n) {
    static const unsigned char b[8] = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x01, 0, 0};
    int                        w = 0, h = 0;

    if (jpeg_probe(b, sizeof(b), &w, &h) != JPEG_ERR_HEADER) {
        snprintf(note, n, "a segment length of 1 read as %dx%d", w, h);
        return 1;
    }
    snprintf(note, n, "a segment length of 1 refused");
    return 0;
}

/* The hardware decoder cannot open a progressive JPEG at all. */
static int t_a_progressive_frame_is_refused(char *note, unsigned n) {
    unsigned char b[32];
    unsigned      len = build_frame(b, 0xC2, 320, 240);
    int           w = 0, h = 0;

    if (jpeg_probe(b, len, &w, &h) != JPEG_ERR_PROGRESSIVE) {
        snprintf(note, n, "an SOF2 header was accepted as %dx%d", w, h);
        return 1;
    }
    snprintf(note, n, "SOF2 refused as progressive");
    return 0;
}

/* Restart markers carry no length, and every scanner branch must still advance
 * i. */
static int t_restart_markers_alone_do_not_loop_forever(char *note, unsigned n) {
    unsigned char b[64];
    unsigned      i, len = 0;
    int           w = 0, h = 0;

    b[len++] = 0xFF;
    b[len++] = 0xD8;
    for (i = 0; i < 20; i++) {
        b[len++] = 0xFF;
        b[len++] = (unsigned char)(0xD0 + (i % 8));
    }
    if (jpeg_probe(b, len, &w, &h) != JPEG_ERR_HEADER) {
        snprintf(note, n, "%u bytes of restart markers read as %dx%d", len, w, h);
        return 1;
    }
    snprintf(note, n, "%u bytes of restart markers scanned and refused, no hang", len);
    return 0;
}

/* 0xFF fill advances one byte at a time rather than two. */
static int t_fill_bytes_alone_do_not_loop_forever(char *note, unsigned n) {
    unsigned char b[64];
    unsigned      i, len = 0;
    int           w = 0, h = 0;

    b[len++] = 0xFF;
    b[len++] = 0xD8;
    for (i = 0; i < 40; i++) b[len++] = 0xFF;

    if (jpeg_probe(b, len, &w, &h) != JPEG_ERR_HEADER) {
        snprintf(note, n, "%u bytes of 0xFF fill read as %dx%d", len, w, h);
        return 1;
    }
    snprintf(note, n, "%u bytes of 0xFF fill scanned and refused, no hang", len);
    return 0;
}

/* Artwork is asked for at the size it will be drawn, so bigger is a wrong URL
 * -- and 1024x1024 needs 4 MB of intermediates on a console whose largest free
 * run is about 1 MB. */
static int t_an_image_larger_than_the_panel_is_refused(char *note, unsigned n) {
    unsigned char b[32];
    int           w = 0, h = 0;

    if (jpeg_probe(b, build_frame(b, 0xC0, 1024, 1024), &w, &h) != JPEG_ERR_TOO_BIG) {
        snprintf(note, n, "1024x1024 was accepted");
        return 1;
    }
    return 0;
}

/* "could not decode" for every failure is how an unsupported file gets
 * retried forever. */
static int t_every_failure_says_why(char *note, unsigned n) {
    unsigned char b[32];
    int           w, h;

    jpeg_err_clear();
    jpeg_probe(b, build_frame(b, 0xC2, 320, 240), &w, &h);
    if (!strstr(jpeg_error(), "progressive")) {
        snprintf(note, n, "progressive said \"%s\"", jpeg_error());
        return 1;
    }
    jpeg_err_clear();
    jpeg_probe(b, build_frame(b, 0xC0, 1024, 1024), &w, &h);
    if (!jpeg_error()[0]) {
        snprintf(note, n, "too-big said nothing");
        return 1;
    }
    return 0;
}

void test_jpeg_header_register(void) {
    selftest_add("jpeg_header", "a well-formed header gives the right size", t_a_well_formed_header_gives_the_right_size);
    selftest_add("jpeg_header", "bad magic is refused", t_bad_magic_is_refused);
    selftest_add("jpeg_header", "zero-length and very short inputs are refused", t_zero_length_and_very_short_inputs_are_refused);
    selftest_add("jpeg_header", "a header truncated before the length field", t_a_header_truncated_before_the_length_field);
    selftest_add("jpeg_header", "a header truncated before the payload", t_a_header_truncated_before_the_payload);
    selftest_add("jpeg_header", "a header truncated inside the payload", t_a_header_truncated_inside_the_payload);
    selftest_add("jpeg_header", "a segment length past the buffer is refused", t_a_segment_length_past_the_buffer_is_refused);
    selftest_add("jpeg_header", "a segment length below the minimum is refused", t_a_segment_length_below_the_minimum_is_refused);
    selftest_add("jpeg_header", "a progressive frame is refused", t_a_progressive_frame_is_refused);
    selftest_add("jpeg_header", "restart markers alone do not loop forever", t_restart_markers_alone_do_not_loop_forever);
    selftest_add("jpeg_header", "fill bytes alone do not loop forever", t_fill_bytes_alone_do_not_loop_forever);
    selftest_add("jpeg_header", "an image larger than the panel is refused", t_an_image_larger_than_the_panel_is_refused);
    selftest_add("jpeg_header", "every failure says why", t_every_failure_says_why);
}
