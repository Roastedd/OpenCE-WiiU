/*
WIIU_TEXTURE_LAYOUT.H

The arithmetic of a texture's levels that the GX2 backend (wiiu_gx2.c, image_surface) does before it asks the heap for memory, kept apart from GX2 so that the
host tests it with the numbers a console's log gave.

A level's slices. A 2D texture's level has one slice, a cube map's has six (the faces), and a 3D texture's has its depth halved a level at a time (32, 16, 8, 4, 2,
1): the slices of level 0 are not the slices of level 1. image_surface checked every level against the whole surface with the depth of level 0, so the 32 x 32 x 32
volume of the game's distance attenuation (the dynamic lights' falloff, rasterizer_xbox_environment.c) "did not fit its 349440 bytes" in every console log from the
first (262144 + 4096 x 32 = 393216 > 349440 where 262144 + 4096 x 16 = 327680 fits: GX2's own total is 262144 for level 0 and 65536 + 16384 + 4096 + 1024 + 256 for
the five levels after it), the texture was dropped, and a light's pass sampled a white 2D texel for the whole of its volume.

A texture that does not fit the memory left. The 2048 x 2048 high-res scope mask of the pistol is 22,380,288 bytes with its levels; when the heap has no block that large,
the texture is made without its top levels (1024 x 1024 is 5.6 MB, 512 x 512 1.4 MB), as small as 256 on its larger side and no smaller: a mask that is a quarter as sharp
is the scope, one that is not there is a black screen.
*/
#ifndef WIIU_TEXTURE_LAYOUT_H
#define WIIU_TEXTURE_LAYOUT_H

#include <stdint.h>

/* the kinds of dk_commands.h (DK_TEXTURE_2D, _3D, _CUBE) */
enum { WIIU_TEXTURE_KIND_2D = 0, WIIU_TEXTURE_KIND_3D = 1, WIIU_TEXTURE_KIND_CUBE = 2 };

/* the smallest the larger side of a texture is made when the memory is short */
#define WIIU_TEXTURE_SMALLEST_SIDE 256u

/* the size of a side at a level (at least 1) */
static inline uint32_t wiiu_texture_level_size(uint32_t size, unsigned level)
{
	size = level >= 32 ? 0 : size >> level;
	return size ? size : 1;
}

/* the slices of a level: a cube map's six faces, a 3D texture's depth halved a level at a time, one for a 2D texture */
static inline uint32_t wiiu_texture_slices_at(unsigned kind, uint32_t depth, unsigned level)
{
	if (kind == WIIU_TEXTURE_KIND_CUBE)
		return 6;
	if (kind == WIIU_TEXTURE_KIND_3D)
		return wiiu_texture_level_size(depth ? depth : 1, level);
	return 1;
}

/* whether a level, from offset, slices of slice_bytes each, ends inside the surface's total bytes */
static inline int wiiu_texture_level_fits(uint32_t offset, uint32_t slice_bytes, uint32_t slices, uint32_t total)
{
	return (uint64_t)offset + (uint64_t)slice_bytes * slices <= total;
}

/* the first level to make when the memory is short: skip levels are left off the top. Allowed for a 2D texture that has levels beyond the skipped ones, while the larger
side is still WIIU_TEXTURE_SMALLEST_SIDE or more (the larger side at skip 0 is what the texture is: a small texture is never made smaller) */
static inline int wiiu_texture_can_skip(unsigned kind, uint32_t width, uint32_t height, uint32_t levels, unsigned skip)
{
	const uint32_t larger = width > height ? width : height;

	if (kind != WIIU_TEXTURE_KIND_2D || skip == 0 || skip >= levels)
		return skip == 0;
	return wiiu_texture_level_size(larger, skip) >= WIIU_TEXTURE_SMALLEST_SIDE;
}

#endif
