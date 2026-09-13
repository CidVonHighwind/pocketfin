/* Poster decoding on a PC, through stb_image. See port/jpeg.h.
 *
 * The console's Media Engine takes baseline 4:2:0 only and will not scale, so
 * its rules are enforced in front of stb_image, or this build would decode
 * artwork the console refuses: the size bound, progressive rejected from the
 * frame header, and the scratch size checked. stbi_info() says nothing about
 * the coding mode, which is why the header walk is jpeg_probe()'s.
 *
 * stb_image allocates, which only a PC may: port/mem.h's budget is about a
 * console heap whose largest free run measured 1.16 MB. */

#include "port/jpeg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG /* nothing else is ever asked of it */
#define STBI_NO_STDIO  /* the bytes are always already in memory */
#include "stb_image.h"

int jpeg_init(void) {
    jpeg_err_clear();
    return 0;
}
void jpeg_finish(void) {}

int jpeg_decode_rgb565(const void *jpg, unsigned jpg_len, void *out565, int out_w, int out_h, int *got_w, int *got_h, void *scratch,
                       unsigned scratch_len) {
    unsigned char *pixels;
    int            source_w = 0, source_h = 0, channels = 0, rc;
    unsigned       want;

    if (!jpg || !jpg_len || !out565 || !scratch || out_w <= 0 || out_h <= 0) {
        jpeg_failf("bad arguments");
        return JPEG_ERR_ARGS;
    }

    jpeg_err_clear();

    rc = jpeg_probe(jpg, jpg_len, &source_w, &source_h);
    if (rc != 0) return rc;

    /* Not used by stb_image, but checked so a caller sizing its buffer wrong
     * finds out on the machine where finding out is free. */
    want = JPEG_SCRATCH_BYTES(source_w, source_h);
    if (scratch_len < want) {
        jpeg_failf("%dx%d needs %u bytes of scratch, given %u", source_w, source_h, want, scratch_len);
        return JPEG_ERR_SCRATCH;
    }

    pixels = stbi_load_from_memory((const stbi_uc *)jpg, (int)jpg_len, &source_w, &source_h, &channels, 4);
    if (!pixels) {
        jpeg_failf("%s", stbi_failure_reason());
        return JPEG_ERR_DECODE;
    }

    jpeg_shrink_565(pixels, source_w, source_w, source_h, out565, out_w, out_h, got_w, got_h);
    stbi_image_free(pixels);
    return 0;
}
