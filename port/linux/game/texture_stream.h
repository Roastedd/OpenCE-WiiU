/*
TEXTURE_STREAM.H

The plan of a texture's streaming (halo/stream.txt on the Wii U; xbox_texture_cache.c). A texture's pixels are one run of bytes in the map: the base level, then
each smaller level after it, the smallest last. When the game asks for a texture that is not in the cache yet, the Xbox's code waits for all of it (a stall of the
game thread for as long as the card takes: 50 ms for a 350 KB bitmap, a second and more for a room's worth). With streaming the map's read is cut in two at a
level boundary: the tail (the levels of about 32 by 32 texels and under, a few KB) is read first, before the other reads that are waiting, and the head (the big
levels) as the read always was. While the head is not in, the texture is drawn from its tail: the same memory, described as a texture of the tail's levels alone (a
second header whose data pointer is at the tail's first level and whose size and level count are the tail's). The two reads cover exactly the bytes the one read
covered and write no byte twice, so no byte a draw reads is written again.

No game data here, so that the rules can be tested apart from the game (port/wiiu/tests/test_texture_stream.c).
*/

#ifndef __TEXTURE_STREAM_H
#define __TEXTURE_STREAM_H

#ifndef TEXTURE_STREAM_SECTOR /* (a test sets another, to see that the checks notice) */
#define TEXTURE_STREAM_SECTOR 512UL /* the cache file's sector: reads start and end on one */
#endif
#define TEXTURE_STREAM_TAIL_SIDE 32 /* the first level at or under this many texels on its longer side starts the tail */
#define TEXTURE_STREAM_MINIMUM_HEAD 49152UL /* a texture with less before its tail is read whole: it takes a few ms, about what the extra read of the tail costs */
#define TEXTURE_STREAM_MAXIMUM_TAIL 32768UL /* ... and one whose tail is bigger than this is too (the sizes of levels are not what was assumed) */
#define TEXTURE_STREAM_MAXIMUM_LEVELS 16

struct texture_stream_plan
{
	int eligible;
	int level; /* the tail's first level */
	unsigned long level_offset; /* the bytes before that level */
	unsigned long tail_offset; /* where the tail's read starts: that, down to a sector (the head is [0, tail_offset)) */
	unsigned long tail_size; /* the tail's read: to the end of the pixels */
	int width_log2, height_log2; /* of that level */
	int levels; /* the levels from that one to the smallest (the header's mipmap count) */
};

static __inline int texture_stream_log2(unsigned long value)
{
	int log = 0;

	while (value > 1)
	{
		value >>= 1;
		log++;
	}
	return log;
}

/* width, height: the base level's texels (powers of two); max_level: the smallest level's index (the header has max_level + 1 levels); sizes[i]: the bytes of
level i; pixels_size: the bytes the map has for the texture (the read's). Not eligible: a texture with no small levels, a small one, or sizes that do not add up. */
static __inline void texture_stream_plan_make(struct texture_stream_plan *plan, int width, int height, int max_level, const long *sizes, long pixels_size)
{
	unsigned long total = 0, offset = 0;
	int level;

	plan->eligible = 0;
	plan->level = plan->width_log2 = plan->height_log2 = plan->levels = 0;
	plan->level_offset = plan->tail_offset = plan->tail_size = 0;
	if (width <= 0 || height <= 0 || (width & (width - 1)) || (height & (height - 1)) || max_level < 2 || max_level >= TEXTURE_STREAM_MAXIMUM_LEVELS ||
		pixels_size <= 0)
		return;
	for (level = 0; level <= max_level; level++)
	{
		if (sizes[level] <= 0)
			return;
		total += (unsigned long)sizes[level];
	}
	if ((unsigned long)pixels_size < total)
		return;
	for (level = 1; level < max_level; level++)
	{
		const int side = (width >> level) > (height >> level) ? (width >> level) : (height >> level);

		if (side <= TEXTURE_STREAM_TAIL_SIDE)
			break;
	}
	for (offset = 0, plan->level = 0; plan->level < level; plan->level++)
		offset += (unsigned long)sizes[plan->level];
	plan->level = level;
	plan->level_offset = offset;
	plan->tail_offset = offset & ~(TEXTURE_STREAM_SECTOR - 1);
	plan->tail_size = (unsigned long)pixels_size - plan->tail_offset;
	plan->width_log2 = texture_stream_log2((unsigned long)((width >> level) ? (width >> level) : 1));
	plan->height_log2 = texture_stream_log2((unsigned long)((height >> level) ? (height >> level) : 1));
	plan->levels = max_level - level + 1;
	if (plan->tail_offset < TEXTURE_STREAM_MINIMUM_HEAD || plan->tail_size > TEXTURE_STREAM_MAXIMUM_TAIL)
		return;
	plan->eligible = 1;
}

#endif /* __TEXTURE_STREAM_H */
