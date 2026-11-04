/*
The plan of a texture's streaming (port/linux/game/texture_stream.h), apart from the game: where the read of a bitmap's pixels is cut, which levels the tail has, and that
the two reads (the head [0, tail_offset) and the tail [tail_offset, pixels_size)) cover the bytes the one read covered and write none twice. The sizes of the levels are made
as the game makes them (bitmap_mipmap_get_pixel_data_size: a compressed level has whole 4 by 4 blocks, any other at least one texel).

    test_texture_stream plan | random | broken
Exit status 0 when everything holds. "broken" must fail: the plan is made with the cut not at a sector.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef BROKEN
#define TEXTURE_STREAM_SECTOR 1UL
#endif
#include "../../linux/game/texture_stream.h"

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("check failed, line %d: %s\n", __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

/* the bytes of a level: texels a bits_per_texel / 8; a compressed level in blocks of 4 by 4 (at least one) */
static long level_size(int width, int height, int level, int compressed, int bits_per_texel)
{
	long w = width >> level ? width >> level : 1, h = height >> level ? height >> level : 1;

	if (compressed)
	{
		w += (-w) & 3;
		h += (-h) & 3;
	}
	return w * h * bits_per_texel / 8;
}

static int max_level_of(int width, int height, int compressed)
{
	int side = width > height ? width : height, level = 0;

	if (compressed)
		side /= 4;
	while (side > 1)
	{
		side >>= 1;
		level++;
	}
	return level;
}

static void make(struct texture_stream_plan *plan, int width, int height, int compressed, int bits_per_texel, long extra)
{
	long sizes[TEXTURE_STREAM_MAXIMUM_LEVELS], total = 0;
	int max_level = max_level_of(width, height, compressed), level;

	for (level = 0; level <= max_level; level++)
	{
		sizes[level] = level_size(width, height, level, compressed, bits_per_texel);
		total += sizes[level];
	}
	texture_stream_plan_make(plan, width, height, max_level, sizes, total + extra);
}

static void plan_cases(void)
{
	struct texture_stream_plan plan;

	/* 512 by 512 DXT5 (8 bits a texel): levels 512 ... 4, 8 of them; the tail starts at 32 by 32 (level 4) */
	make(&plan, 512, 512, 1, 8, 0);
	CHECK(plan.eligible);
	CHECK(plan.level == 4);
	CHECK(plan.level_offset == 262144 + 65536 + 16384 + 4096);
	CHECK(plan.tail_offset == (plan.level_offset & ~511UL));
	CHECK(plan.width_log2 == 5 && plan.height_log2 == 5);
	CHECK(plan.levels == 4); /* 32, 16, 8, 4 */
	CHECK(plan.tail_size == 262144 + 65536 + 16384 + 4096 + 1024 + 256 + 64 + 16 - plan.tail_offset);
	/* 64 by 64 DXT1: all of it under 3 KB: read whole */
	make(&plan, 64, 64, 1, 4, 0);
	CHECK(!plan.eligible);
	/* 256 by 256 32-bit, mipmaps down to 1 by 1 (8 levels): the tail from 32 by 32 (level 3) */
	make(&plan, 256, 256, 0, 32, 0);
	CHECK(plan.eligible);
	CHECK(plan.level == 3);
	CHECK(plan.level_offset == 262144 + 65536 + 16384);
	CHECK(plan.levels == 6 && plan.width_log2 == 5);
	/* 2048 by 128 DXT1: the tail from level 6 (32 by 2) */
	make(&plan, 2048, 128, 1, 4, 0);
	CHECK(plan.eligible && plan.level == 6);
	CHECK(plan.width_log2 == 5 && plan.height_log2 == 1);
	/* a texture with no mipmaps, one with one, a non power of two */
	{
		long sizes[3] = { 1048576, 262144, 65536 };

		texture_stream_plan_make(&plan, 1024, 1024, 0, sizes, 1048576);
		CHECK(!plan.eligible);
		texture_stream_plan_make(&plan, 1024, 1024, 1, sizes, 1048576 + 262144);
		CHECK(!plan.eligible);
		texture_stream_plan_make(&plan, 1000, 1024, 2, sizes, 1048576 + 262144 + 65536);
		CHECK(!plan.eligible);
		/* fewer bytes in the map than the levels add up to */
		texture_stream_plan_make(&plan, 1024, 1024, 2, sizes, 1048576);
		CHECK(!plan.eligible);
	}
	/* the map has a few bytes more than the levels (padding to 128): the tail takes them */
	make(&plan, 512, 512, 1, 8, 96);
	CHECK(plan.eligible);
	CHECK(plan.tail_size == (unsigned long)(262144 + 65536 + 16384 + 4096 + 1024 + 256 + 64 + 16 + 96) - plan.tail_offset);
}

static void random_cases(void)
{
	unsigned long state = 12345;
	int trial;

	for (trial = 0; trial < 20000; trial++)
	{
		const int compressed = (int)(state >> 3 & 1);
		const int width = 4 << (state >> 9) % 10, height = 4 << (state >> 17) % 10;
		static const int bits[] = { 4, 8, 8, 16, 32, 8 };
		const int bits_per_texel = compressed ? ((state >> 5 & 1) ? 4 : 8) : bits[(state >> 21) % 6];
		const long padding = (long)((state >> 26) & 127);
		struct texture_stream_plan plan;
		long total = 0, sizes[TEXTURE_STREAM_MAXIMUM_LEVELS];
		int level, max_level = max_level_of(width, height, compressed);

		state = state * 1664525UL + 1013904223UL;
		state ^= state >> 13;
		for (level = 0; level <= max_level; level++)
		{
			sizes[level] = level_size(width, height, level, compressed, bits_per_texel);
			total += sizes[level];
		}
		texture_stream_plan_make(&plan, width, height, max_level, sizes, total + padding);
		if (!plan.eligible)
			continue;
		/* the head and the tail are the map's bytes, once */
		CHECK(plan.tail_offset % 512 == 0);
		CHECK(plan.tail_offset <= plan.level_offset && plan.level_offset - plan.tail_offset < 512);
		CHECK(plan.tail_offset + plan.tail_size == (unsigned long)(total + padding));
		CHECK(plan.tail_offset >= TEXTURE_STREAM_MINIMUM_HEAD && plan.tail_size <= TEXTURE_STREAM_MAXIMUM_TAIL);
		/* the tail's levels are the last ones, and the header says as many of them */
		CHECK(plan.level >= 1 && plan.level <= max_level);
		CHECK(plan.levels == max_level - plan.level + 1);
		CHECK(plan.width_log2 == texture_stream_log2((unsigned long)((width >> plan.level) ? (width >> plan.level) : 1)));
		CHECK(plan.height_log2 == texture_stream_log2((unsigned long)((height >> plan.level) ? (height >> plan.level) : 1)));
		/* the level offset is the sum of the levels before it */
		{
			unsigned long sum = 0;

			for (level = 0; level < plan.level; level++)
				sum += (unsigned long)sizes[level];
			CHECK(sum == plan.level_offset);
		}
		/* the tail's first level is 32 texels or under on its longer side (or is the last level) */
		{
			const int a = width >> plan.level, b = height >> plan.level;

			CHECK((a > b ? a : b) <= TEXTURE_STREAM_TAIL_SIDE || plan.level == max_level);
		}
		if (failures > 20)
			return;
	}
}

int main(int argc, char **argv)
{
	const char *which = argc > 1 ? argv[1] : "plan";

	if (!strcmp(which, "plan") || !strcmp(which, "broken"))
		plan_cases();
	if (!strcmp(which, "random") || !strcmp(which, "broken"))
		random_cases();
	if (failures)
	{
		printf("%d checks failed\n", failures);
		return 1;
	}
	printf("ok\n");
	return 0;
}
