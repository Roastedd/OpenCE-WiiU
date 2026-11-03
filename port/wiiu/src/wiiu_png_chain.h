/*
WIIU_PNG_CHAIN.H

The texel work of the Wii U's texture cache on a decoded PNG or the text's atlas (xbox_textures_gx2.c), done a word at a time: the bytes it makes are
the bytes the loops it replaces made (port/wiiu/tests/test_png_chain.c has those loops and checks every function here against them, on this machine's
byte order and, under qemu-ppc, the console's). On the console they were the start-up's largest piece of the game thread's CPU after the menus' build: a
byte at a time, four channels in an inner loop, for every texel of every level of every menu picture.

A texel is 4 bytes. A decoded PNG's are red, green, blue, alpha; the images the GX2 thread is given are blue, green, red, alpha, so the first and third
bytes of each are swapped; each level's texels are the mean of the (up to) four above them, rounded to nearest (sum plus two, over four), per byte.
*/

#ifndef WIIU_PNG_CHAIN_H
#define WIIU_PNG_CHAIN_H

#include <stdint.h>
#include <string.h>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define WIIU_PNG_BIG_ENDIAN 1
#else
#define WIIU_PNG_BIG_ENDIAN 0
#endif

/* the first and third bytes of each of count texels swapped (red first to blue first); texels are 4-byte aligned */
static inline void wiiu_png_swap_red_blue(uint32_t *texels, unsigned long count)
{
	unsigned long index;

	for (index = 0; index < count; index++)
	{
		const uint32_t v = texels[index];

#if WIIU_PNG_BIG_ENDIAN
		/* bytes in memory order r g b a: the word is r g b a from its top */
		texels[index] = (v & 0x00FF00FFu) | ((v >> 16) & 0x0000FF00u) | ((v << 16) & 0xFF000000u);
#else
		/* bytes in memory order r g b a: the word is a b g r from its top */
		texels[index] = (v & 0xFF00FF00u) | ((v >> 16) & 0x000000FFu) | ((v << 16) & 0x00FF0000u);
#endif
	}
}

/* the mean of four texels, byte by byte, rounded to nearest: (a + b + c + d + 2) / 4 in each byte, two bytes to a 16-bit lane (a lane holds the sum of
four bytes and the 2: at most 1022) */
static inline uint32_t wiiu_png_mean4(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
	const uint32_t even = (a & 0x00FF00FFu) + (b & 0x00FF00FFu) + (c & 0x00FF00FFu) + (d & 0x00FF00FFu) + 0x00020002u;
	const uint32_t odd = ((a >> 8) & 0x00FF00FFu) + ((b >> 8) & 0x00FF00FFu) + ((c >> 8) & 0x00FF00FFu) + ((d >> 8) & 0x00FF00FFu) + 0x00020002u;

	return ((even >> 2) & 0x00FF00FFu) | (((odd >> 2) & 0x00FF00FFu) << 8);
}

/* the level below a level of width by height texels (rows of width texels): next_width by next_height, which are half of them, down to 1; each texel
the mean of the 2 by 2 above it, an edge of an odd level using the one texel twice, as the loops this replaces did */
static inline void wiiu_png_next_level(const uint32_t *above, unsigned long width, unsigned long height, uint32_t *next,
	unsigned long next_width, unsigned long next_height)
{
	unsigned long x, y;

	for (y = 0; y < next_height; y++)
	{
		const unsigned long y0 = y * 2, y1 = y * 2 + 1 < height ? y * 2 + 1 : y * 2;
		const uint32_t *row0 = above + y0 * width, *row1 = above + y1 * width;
		uint32_t *out = next + y * next_width;

		for (x = 0; x < next_width; x++)
		{
			const unsigned long x0 = x * 2, x1 = x * 2 + 1 < width ? x * 2 + 1 : x * 2;

			out[x] = wiiu_png_mean4(row0[x0], row0[x1], row1[x0], row1[x1]);
		}
	}
}

/* count bytes of coverage as white texels of that alpha (255, 255, 255, coverage): the text's atlas, as the maps' fonts are */
static inline void wiiu_png_coverage_to_white(uint32_t *texels, const unsigned char *coverage, unsigned long count)
{
	unsigned long index;

	for (index = 0; index < count; index++)
	{
#if WIIU_PNG_BIG_ENDIAN
		texels[index] = 0xFFFFFF00u | coverage[index];
#else
		texels[index] = 0x00FFFFFFu | ((uint32_t)coverage[index] << 24);
#endif
	}
}

#endif
