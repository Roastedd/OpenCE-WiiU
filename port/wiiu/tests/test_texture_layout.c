/* wiiu_texture_layout.h: a level holds its own slices, and a texture that does not fit the heap is made without its top levels. The numbers are the console's:
the 32 x 32 x 32 volume of the game's distance attenuation is "262144 + 4096 x 32 does not fit its 349440 bytes" in every log from the first. */

#include "../src/wiiu_texture_layout.h"

#include <stdio.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

int main(void)
{
	/* GX2's layout of the volume (linear aligned, rows 64 texels apart): level 0 is 32 slices of 8192; level n after it is 32 >> n slices of 64 x (32 >> n) x 4 */
	const uint32_t total = 349440;
	const uint32_t offset[6] = { 0, 262144, 262144 + 65536, 262144 + 65536 + 16384, 262144 + 65536 + 16384 + 4096, 262144 + 65536 + 16384 + 4096 + 1024 };
	const uint32_t slice_bytes[6] = { 8192, 4096, 2048, 1024, 512, 256 };
	unsigned level;
	uint32_t sum = 262144;

	/* the slices of each kind of texture at each level */
	for (level = 0; level < 6; level++)
		CHECK(wiiu_texture_slices_at(WIIU_TEXTURE_KIND_3D, 32, level) == (32u >> level));
	CHECK(wiiu_texture_slices_at(WIIU_TEXTURE_KIND_3D, 32, 9) == 1);
	CHECK(wiiu_texture_slices_at(WIIU_TEXTURE_KIND_3D, 4, 0) == 4 && wiiu_texture_slices_at(WIIU_TEXTURE_KIND_3D, 4, 3) == 1);
	CHECK(wiiu_texture_slices_at(WIIU_TEXTURE_KIND_3D, 0, 0) == 1);
	for (level = 0; level < 6; level++)
		CHECK(wiiu_texture_slices_at(WIIU_TEXTURE_KIND_CUBE, 1, level) == 6 && wiiu_texture_slices_at(WIIU_TEXTURE_KIND_2D, 1, level) == 1);

	/* the volume's six levels, each with its own slices, add up to GX2's total, and every one of them fits (the old check: the depth of level 0 at every level) */
	for (level = 1; level < 6; level++)
		sum += slice_bytes[level] * wiiu_texture_slices_at(WIIU_TEXTURE_KIND_3D, 32, level);
	CHECK(sum == total);
	for (level = 0; level < 6; level++)
		CHECK(wiiu_texture_level_fits(offset[level], slice_bytes[level], wiiu_texture_slices_at(WIIU_TEXTURE_KIND_3D, 32, level), total));
	CHECK(!wiiu_texture_level_fits(offset[1], slice_bytes[1], 32, total)); /* what the console's log said: 262144 + 4096 x 32 */
	CHECK(wiiu_texture_level_fits(offset[5], slice_bytes[5], 1, total));
	CHECK(!wiiu_texture_level_fits(offset[5], slice_bytes[5], 2, total));
	CHECK(wiiu_texture_level_fits(0, 0xffffffffu, 1, 0xffffffffu) && !wiiu_texture_level_fits(0xffffffffu, 0xffffffffu, 2, 0xffffffffu)); /* no overflow */

	/* the sizes of a side */
	CHECK(wiiu_texture_level_size(2048, 0) == 2048 && wiiu_texture_level_size(2048, 1) == 1024 && wiiu_texture_level_size(2048, 11) == 1 && wiiu_texture_level_size(2048, 40) == 1);

	/* a 2048 x 2048 mask of 12 levels: skipping one level (1024), two (512) or three (256) is allowed, four (128) is not */
	CHECK(wiiu_texture_can_skip(WIIU_TEXTURE_KIND_2D, 2048, 2048, 12, 0));
	CHECK(wiiu_texture_can_skip(WIIU_TEXTURE_KIND_2D, 2048, 2048, 12, 1));
	CHECK(wiiu_texture_can_skip(WIIU_TEXTURE_KIND_2D, 2048, 2048, 12, 2));
	CHECK(wiiu_texture_can_skip(WIIU_TEXTURE_KIND_2D, 2048, 2048, 12, 3));
	CHECK(!wiiu_texture_can_skip(WIIU_TEXTURE_KIND_2D, 2048, 2048, 12, 4));
	/* the larger side counts (the sniper rifle's 2048 x 1024 mask) */
	CHECK(wiiu_texture_can_skip(WIIU_TEXTURE_KIND_2D, 2048, 1024, 12, 3) && !wiiu_texture_can_skip(WIIU_TEXTURE_KIND_2D, 2048, 1024, 12, 4));
	CHECK(wiiu_texture_can_skip(WIIU_TEXTURE_KIND_2D, 128, 2048, 12, 3));
	/* a small texture is never made smaller, nor one with no level to start from, nor a volume or a cube map */
	CHECK(!wiiu_texture_can_skip(WIIU_TEXTURE_KIND_2D, 256, 256, 9, 1));
	CHECK(!wiiu_texture_can_skip(WIIU_TEXTURE_KIND_2D, 64, 64, 7, 1));
	CHECK(!wiiu_texture_can_skip(WIIU_TEXTURE_KIND_2D, 2048, 2048, 3, 3));
	CHECK(!wiiu_texture_can_skip(WIIU_TEXTURE_KIND_2D, 2048, 2048, 1, 1));
	CHECK(!wiiu_texture_can_skip(WIIU_TEXTURE_KIND_3D, 2048, 2048, 12, 1) && !wiiu_texture_can_skip(WIIU_TEXTURE_KIND_CUBE, 2048, 2048, 12, 1));
	CHECK(wiiu_texture_can_skip(WIIU_TEXTURE_KIND_3D, 32, 32, 6, 0) && wiiu_texture_can_skip(WIIU_TEXTURE_KIND_CUBE, 32, 32, 6, 0));

	printf("%d failures\n", failures);
	return failures ? 1 : 0;
}
