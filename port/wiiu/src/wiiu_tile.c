#include "wiiu_tile.h"

#include <string.h>

/* The index of a block inside its 8 x 8 tile, from the bits of its position. 8 byte blocks: x0 y0 x1 x2 y1 y2 (bit 0 first); 16 byte blocks:
y0 x0 x1 x2 y1 y2. Both from the console, not from a table. */
static unsigned index_in_tile(unsigned x, unsigned y, unsigned block_bytes)
{
	const unsigned x0 = x & 1u, x1 = (x >> 1) & 1u, x2 = (x >> 2) & 1u, y0 = y & 1u, y1 = (y >> 1) & 1u, y2 = (y >> 2) & 1u;

	if (block_bytes == 8)
		return x0 | (y0 << 1) | (x1 << 2) | (x2 << 3) | (y1 << 4) | (y2 << 5);
	return y0 | (x0 << 1) | (x1 << 2) | (x2 << 3) | (y1 << 4) | (y2 << 5);
}

size_t wiiu_tile_1d_offset(unsigned x, unsigned y, unsigned pitch_blocks, unsigned block_bytes)
{
	const size_t tile = (size_t)(y / 8) * (pitch_blocks / 8) + (x / 8);

	return (tile * 64 + index_in_tile(x % 8, y % 8, block_bytes)) * block_bytes;
}

void wiiu_tile_1d(const uint8_t *rows, unsigned blocks_across, unsigned blocks_down, unsigned block_bytes, unsigned pitch_blocks, uint8_t *tiled)
{
	unsigned x, y;

	for (y = 0; y < blocks_down; y++)
		for (x = 0; x < blocks_across; x++)
			memcpy(tiled + wiiu_tile_1d_offset(x, y, pitch_blocks, block_bytes), rows + ((size_t)y * blocks_across + x) * block_bytes, block_bytes);
}
