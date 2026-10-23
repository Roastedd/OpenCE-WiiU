/* The 1D tiled layout: a bijection onto the surface, and the places the console showed (the GX2 probe's reports). */

#include "../src/wiiu_tile.h"

#include <stdio.h>
#include <string.h>

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

static void bijection(unsigned block_bytes, unsigned across, unsigned down, unsigned pitch)
{
	static unsigned char seen[64 * 64];
	unsigned x, y;
	const size_t blocks = (size_t)pitch * ((down + 7) / 8 * 8);

	memset(seen, 0, sizeof(seen));
	for (y = 0; y < down; y++)
		for (x = 0; x < across; x++)
		{
			size_t offset = wiiu_tile_1d_offset(x, y, pitch, block_bytes);

			CHECK(offset % block_bytes == 0 && offset / block_bytes < blocks);
			CHECK(!seen[offset / block_bytes]);
			seen[offset / block_bytes] = 1;
		}
}

int main(void)
{
	uint8_t rows[16 * 16 * 16], tiled[16 * 16 * 16];
	unsigned i;

	bijection(8, 16, 16, 16);
	bijection(16, 16, 16, 16);
	bijection(8, 13, 11, 16);
	/* the first tile: blocks (0,0) (1,0) (0,1) are the first three of 8 byte blocks; (0,0) (0,1) (1,0) of 16 byte ones */
	CHECK(wiiu_tile_1d_offset(0, 0, 16, 8) == 0 && wiiu_tile_1d_offset(1, 0, 16, 8) == 8 && wiiu_tile_1d_offset(0, 1, 16, 8) == 16);
	CHECK(wiiu_tile_1d_offset(0, 0, 16, 16) == 0 && wiiu_tile_1d_offset(0, 1, 16, 16) == 16 && wiiu_tile_1d_offset(1, 0, 16, 16) == 32);
	/* the second tile across starts a tile (64 blocks) on; the second row of tiles, a row of tiles (pitch / 8 tiles) on */
	CHECK(wiiu_tile_1d_offset(8, 0, 16, 8) == 64 * 8 && wiiu_tile_1d_offset(0, 8, 16, 8) == 2 * 64 * 8);
	for (i = 0; i < sizeof(rows); i++)
		rows[i] = (uint8_t)(i * 7 + i / 256);
	wiiu_tile_1d(rows, 16, 16, 16, 16, tiled);
	CHECK(!memcmp(tiled + wiiu_tile_1d_offset(5, 9, 16, 16), rows + (9 * 16 + 5) * 16, 16));
	/* the fast path against the one-block-at-a-time offset, for sizes that are not whole tiles */
	{
		static uint8_t source[40 * 27 * 16], fast[48 * 32 * 16], slow[48 * 32 * 16];
		unsigned bytes, x, y;

		for (i = 0; i < sizeof(source); i++)
			source[i] = (uint8_t)(i * 13 + i / 7);
		for (bytes = 8; bytes <= 16; bytes += 8)
		{
			memset(fast, 0, sizeof(fast));
			memset(slow, 0, sizeof(slow));
			wiiu_tile_1d(source, 37, 27, bytes, 40, fast);
			for (y = 0; y < 27; y++)
				for (x = 0; x < 37; x++)
					memcpy(slow + wiiu_tile_1d_offset(x, y, 40, bytes), source + ((size_t)y * 37 + x) * bytes, bytes);
			CHECK(!memcmp(fast, slow, sizeof(fast)));
		}
	}
	if (failures)
		return 1;
	printf("all checks passed\n");
	return 0;
}
