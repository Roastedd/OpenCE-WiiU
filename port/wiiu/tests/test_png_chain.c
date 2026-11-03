/*
TEST_PNG_CHAIN.C

port/wiiu/src/wiiu_png_chain.h makes the bytes the loops it replaced made: here are those loops (xbox_textures_gx2.c as it was until 2026-11-03, copied), and
the functions are checked against them on random texels of many sizes (odd ones, one texel wide or high, and the extremes of the bytes), level by level.
Built by tools/test_wiiu_png_chain.py, natively and (qemu-ppc) big-endian.
*/

#include "../src/wiiu_png_chain.h"

#include <stdio.h>
#include <stdlib.h>

static uint32_t state = 0x1234567u;

static unsigned char random_byte(unsigned kind)
{
	state = state * 1664525u + 1013904223u;
	switch (kind)
	{
	case 0: return (unsigned char)(state >> 24);
	case 1: return (state >> 24) & 1 ? 255 : 0;
	case 2: return (unsigned char)(250 + ((state >> 24) % 6));
	default: return (unsigned char)((state >> 24) % 4);
	}
}

/* the loop that swapped red and blue */
static void reference_swap(unsigned char *pixels, unsigned long width, unsigned long height)
{
	unsigned long index;

	for (index = 0; index < width * height; index++)
	{
		unsigned char red = pixels[index * 4];

		pixels[index * 4] = pixels[index * 4 + 2];
		pixels[index * 4 + 2] = red;
	}
}

/* the loop that made the level below */
static void reference_next(const unsigned char *pixels, unsigned long width, unsigned long height, unsigned char *next, unsigned long next_width,
	unsigned long next_height)
{
	unsigned long x, y, c;

	for (y = 0; y < next_height; y++)
	{
		unsigned long y0 = y * 2, y1 = y * 2 + 1 < height ? y * 2 + 1 : y * 2;

		for (x = 0; x < next_width; x++)
		{
			unsigned long x0 = x * 2, x1 = x * 2 + 1 < width ? x * 2 + 1 : x * 2;

			for (c = 0; c < 4; c++)
				next[(y * next_width + x) * 4 + c] = (unsigned char)((pixels[(y0 * width + x0) * 4 + c] +
					pixels[(y0 * width + x1) * 4 + c] + pixels[(y1 * width + x0) * 4 + c] +
					pixels[(y1 * width + x1) * 4 + c] + 2) / 4);
		}
	}
}

static unsigned long dimension(unsigned long size, unsigned long level)
{
	return (size >> level) ? size >> level : 1;
}

static int check_image(unsigned long width, unsigned long height, unsigned kind)
{
	unsigned long largest, levels = 1, level, index;
	uint32_t *a = malloc(width * height * 4), *b = malloc(width * height * 4);
	int failures = 0;

	for (index = 0; index < width * height * 4; index++)
		((unsigned char *)a)[index] = random_byte(kind);
	memcpy(b, a, width * height * 4);
	reference_swap((unsigned char *)a, width, height);
	wiiu_png_swap_red_blue(b, width * height);
	if (memcmp(a, b, width * height * 4))
	{
		printf("swap differs for %lux%lu kind %u\n", width, height, kind);
		failures++;
	}
	for (largest = width > height ? width : height; largest > 1; largest >>= 1)
		levels++;
	for (level = 1; level < levels; level++)
	{
		const unsigned long above_width = dimension(width, level - 1), above_height = dimension(height, level - 1);
		const unsigned long next_width = dimension(width, level), next_height = dimension(height, level);
		uint32_t *reference = malloc(next_width * next_height * 4), *mine = malloc(next_width * next_height * 4);

		reference_next((const unsigned char *)a, above_width, above_height, (unsigned char *)reference, next_width, next_height);
		wiiu_png_next_level(b, above_width, above_height, mine, next_width, next_height);
		if (memcmp(reference, mine, next_width * next_height * 4))
		{
			printf("level %lu differs for %lux%lu kind %u\n", level, width, height, kind);
			failures++;
		}
		free(a);
		free(b);
		a = reference;
		b = mine;
	}
	free(a);
	free(b);
	return failures;
}

static int check_coverage(unsigned long count)
{
	unsigned char *coverage = malloc(count), *reference = malloc(count * 4);
	uint32_t *mine = malloc(count * 4);
	unsigned long index;
	int failures = 0;

	for (index = 0; index < count; index++)
		coverage[index] = random_byte(0);
	for (index = 0; index < count; index++)
	{
		reference[index * 4 + 0] = reference[index * 4 + 1] = reference[index * 4 + 2] = 255;
		reference[index * 4 + 3] = coverage[index];
	}
	wiiu_png_coverage_to_white(mine, coverage, count);
	if (memcmp(reference, mine, count * 4))
	{
		printf("coverage differs for %lu\n", count);
		failures++;
	}
	free(coverage);
	free(reference);
	free(mine);
	return failures;
}

int main(void)
{
	static const unsigned long sizes[] = { 1, 2, 3, 4, 5, 7, 8, 16, 33, 64, 100, 128, 255, 256, 1024, 2048 };
	int failures = 0;
	unsigned long w, h, kind, count = 0;

	for (kind = 0; kind < 4; kind++)
	{
		for (w = 0; w < sizeof(sizes) / sizeof(sizes[0]); w++)
		{
			for (h = 0; h < sizeof(sizes) / sizeof(sizes[0]); h++)
			{
				if (sizes[w] * sizes[h] > 300000 && kind)
					continue;
				failures += check_image(sizes[w], sizes[h], (unsigned)kind);
				count++;
			}
		}
	}
	failures += check_coverage(1) + check_coverage(7) + check_coverage(4096) + check_coverage(2048u * 5u);
	/* the one-byte extremes of the mean, over all four texels the same, and 255 with 0 */
	{
		uint32_t a = 0xFFFFFFFFu, z = 0, mixed = 0x00FF00FFu;

		if (wiiu_png_mean4(a, a, a, a) != a || wiiu_png_mean4(z, z, z, z) != z || wiiu_png_mean4(a, a, a, z) != 0xBFBFBFBFu ||
			wiiu_png_mean4(mixed, z, z, z) != 0x00400040u || wiiu_png_mean4(z, z, a, a) != 0x80808080u)
		{
			printf("mean of the extremes\n");
			failures++;
		}
	}
	printf("png chain: %d images, %d failures (%s endian)\n", (int)count, failures, WIIU_PNG_BIG_ENDIAN ? "big" : "little");
	return failures ? 1 : 0;
}
