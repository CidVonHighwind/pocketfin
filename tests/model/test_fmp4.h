/* test_fmp4.c's fixture, lent to the checks that play it: frag.mp4, made by
 * scripts/fixtures.py and read off the link. */
#ifndef TESTS_MODEL_TEST_FMP4_H
#define TESTS_MODEL_TEST_FMP4_H

#include "model/fmp4.h"

extern unsigned char frag[64 * 1024];
extern unsigned      frag_len;

/* -1 when it is not on the link. */
int frag_load(char *note, unsigned n);

/* Loaded with its init read: -1 without the file, 1 when the init is refused. */
int frag_open(fmp4 *m, char *note, unsigned n);

/* Where the `which`th moof starts, or -1. */
int frag_moof(int which);

#endif
