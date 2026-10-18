/*
READBACK.H

Reading what the GPU drew: the test draws into a small linear color buffer, and the CPU compares the bytes it finds
there with the pixels it expects. The only thing assumed about those bytes is that they are four channels of eight bits
per pixel; their order is found out (a control draw of a known color), so that a surprise there does not turn every
other result into a failure.
*/

#ifndef HALO_WIIU_GX2_READBACK_H
#define HALO_WIIU_GX2_READBACK_H

#include <stddef.h>
#include <stdint.h>

/* The order of the channels in memory: rgba[i] = memory[order[i]]. The identity is R, G, B, A. */
typedef struct readback_order
{
	uint8_t source[4];
} readback_order;

/* The order that turns the 4 bytes found for a pixel of known, pairwise different channel values into those values.
Returns 0 (and the identity) if no order does. */
int readback_detect_order(const uint8_t found[4], const uint8_t expected[4], readback_order *order);

/* a pixel of a buffer with rows of pitch_bytes, in the detected order */
void readback_pixel(const uint8_t *image, unsigned pitch_bytes, unsigned x, unsigned y, const readback_order *order, uint8_t rgba[4]);

/* the largest difference of any channel */
unsigned readback_distance(const uint8_t a[4], const uint8_t b[4]);

typedef struct readback_compare
{
	unsigned pixels;
	unsigned mismatches; /* pixels further than the tolerance from expected */
	unsigned max_error;  /* the largest channel difference found */
	unsigned first_x, first_y; /* the first mismatch */
	uint8_t first_found[4], first_expected[4];
} readback_compare;

/* an image of width x height pixels in the buffer (rows of pitch_bytes) against expected (tightly packed RGBA) */
void readback_compare_image(const uint8_t *image, unsigned pitch_bytes, unsigned width, unsigned height, const readback_order *order,
	const uint8_t *expected, unsigned tolerance, readback_compare *result);

/* a stable summary of an image for the report: the sum of 32-bit words of its pixels in order, mixed by position */
uint32_t readback_hash(const uint8_t *image, unsigned pitch_bytes, unsigned width, unsigned height, const readback_order *order);

#endif
