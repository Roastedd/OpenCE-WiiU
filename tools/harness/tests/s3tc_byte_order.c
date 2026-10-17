/*
S3TC_BYTE_ORDER.C (test)

The real readers of a map's texture pixels on the CPU (the lightmap and diffuse
sampling that sets a unit's ambient illumination, which the AI sees by): the
single-pixel S3TC decoders (bitmaps/s3tc/s3tc.c) and bitmap_format_to_a8r8g8b8
(bitmaps/bitmaps.c). A map keeps its pixels as the Xbox's little-endian values:
on a big-endian host they must be read as those, as the bytes below are. In
the first big-endian build the colours came out wrong and the units' ambient
illumination with them. test_s3tc_byte_order.py takes the code (under_test.inc).
*/

#include "harness.h"

/* (an unsupported format ends the game in the real function: match_vassert, here nothing) */
#pragma clang diagnostic ignored "-Wsometimes-uninitialized"

typedef unsigned short word;
typedef unsigned long pixel32;

#define match_vassert(file, line, condition, message) ((void)0)
#define global_vector_palette harness_vector_palette
static pixel32 harness_vector_palette[256];

#include "under_test.inc"

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	/* DXT1: red and green end points, and the colours between; the indices of a row, one, two, three, zero */
	CASE("dxt1-colors")
	{
		/* (color0 0xF800, color1 0x07E0, indices 0xE4: pixels 0, 1, 2, 3 of the first row take colours 0, 1, 2, 3) */
		static const byte block[8] = { 0x00, 0xF8, 0xE0, 0x07, 0xE4, 0x00, 0x00, 0x00 };
		struct s3tc_color color;
		static const byte expected[4][4] = { { 0, 0, 255, 255 }, { 0, 255, 0, 255 }, { 0, 85, 170, 255 }, { 0, 170, 85, 255 } };
		short u;

		for (u = 0; u < 4; u++)
		{
			DecodeBlockRGB__single_pixel((struct s3tc_block_rgb const *)block, &color, u, 0);
			CHECK(!memcmp(color.rgba, expected[u], 4), "pixel %d is %d %d %d %d, not %d %d %d %d", u, color.rgba[0],
				color.rgba[1], color.rgba[2], color.rgba[3], expected[u][0], expected[u][1], expected[u][2], expected[u][3]);
		}
		return 0;
	}
	/* DXT3: a row's alpha is four 4-bit values of a 16-bit value */
	CASE("dxt3-alpha")
	{
		static const byte block[16] = { 0x10, 0x32, 0, 0, 0, 0, 0, 0, 0x00, 0xF8, 0xE0, 0x07, 0, 0, 0, 0 };
		struct s3tc_color color;
		short u;

		for (u = 0; u < 4; u++)
		{
			DecodeBlockAlpha4__single_pixel((struct s3tc_block_alpha4 const *)block, &color, u, 0);
			CHECK(color.rgba[3] == u * 0x11, "pixel %d has alpha %d, not %d", u, color.rgba[3], u * 0x11);
		}
		return 0;
	}
	/* uncompressed pixels: a 16-bit value, a 32-bit value */
	CASE("pixels")
	{
		static const byte red[2] = { 0x00, 0xF8 };
		static const byte argb[4] = { 0x11, 0x22, 0x33, 0x44 };

		CHECK(bitmap_format_to_a8r8g8b8(_bitmap_format_r5g6b5, red, 0) == 0xFFFF0000UL, "r5g6b5 0xF800 is 0x%lX",
			(unsigned long)bitmap_format_to_a8r8g8b8(_bitmap_format_r5g6b5, red, 0));
		CHECK(bitmap_format_to_a8r8g8b8(_bitmap_format_a8r8g8b8, argb, 0) == 0x44332211UL, "a8r8g8b8 is 0x%lX",
			(unsigned long)bitmap_format_to_a8r8g8b8(_bitmap_format_a8r8g8b8, argb, 0));
		return 0;
	}
	return 2;
}
