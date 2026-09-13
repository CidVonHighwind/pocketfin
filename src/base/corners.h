/* The rounded-corner masks: one quarter-arc per radius, one set of bytes
 * indexed by both renderers, so the two machines cut the same curve. */
#ifndef BASE_CORNERS_H
#define BASE_CORNERS_H

#include "base/align.h"

#define CORNER_MAX_R 8

/* 16 both ways: the GE's texture pitch has a 16-byte floor, and an 8-byte
 * wide T8 mask shears every corner into diagonal garbage. */
#define CORNER_PITCH 16

/* Fills the tables below. Idempotent. On a machine whose engine reads them
 * the caller is also what makes them visible to it. */
void corners_build(void);

/* One step of the ramp between a and b: two ways of interpolating are two
 * different pictures. */
unsigned corners_ramp(unsigned a, unsigned b, int num, int den);

/* Clamped to CORNER_MAX_R and to half of the shorter side. */
int corners_clamp_r(int w, int h, int r);

/* corner_mask[r] is a CORNER_PITCH square whose top-left r x r holds the
 * top-left arc, filled. The other three corners are the same bytes with the
 * texture coordinates swapped. */
extern POCKETFIN_ALIGN16 unsigned char corner_mask[CORNER_MAX_R + 1][CORNER_PITCH * CORNER_PITCH];

/* The one-pixel border as a solid band: 255 inside the shape and outside the
 * disc one pixel in. Not corner_stroke, whose annulus spreads its ink over
 * two pixels on a diagonal -- a frame drawn with it reads brighter at the
 * corners than along the sides. */
extern POCKETFIN_ALIGN16 unsigned char corner_band[CORNER_MAX_R + 1][CORNER_PITCH * CORNER_PITCH];

/* What is outside the curve. The console cannot skip pixels mid-texture, so
 * it draws the picture square and paints these back in the background. */
extern POCKETFIN_ALIGN16 unsigned char corner_cut[CORNER_MAX_R + 1][CORNER_PITCH * CORNER_PITCH];

/* The arc as a one-pixel stroke, for an outline. */
extern POCKETFIN_ALIGN16 unsigned char corner_stroke[CORNER_MAX_R + 1][CORNER_PITCH * CORNER_PITCH];

#endif
