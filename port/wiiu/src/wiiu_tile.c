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

/* Words, not memcpy of 8 or 16 bytes: the compiler may do that with floating-point loads, which trap when misaligned (the rows of a texture are not aligned). */
static void copy_block(uint8_t *to, const uint8_t *from, unsigned block_bytes)
{
	uint32_t word[4];

	memcpy(&word[0], from, 4);
	memcpy(&word[1], from + 4, 4);
	if (block_bytes == 16)
	{
		memcpy(&word[2], from + 8, 4);
		memcpy(&word[3], from + 12, 4);
	}
	memcpy(to, &word[0], 4);
	memcpy(to + 4, &word[1], 4);
	if (block_bytes == 16)
	{
		memcpy(to + 8, &word[2], 4);
		memcpy(to + 12, &word[3], 4);
	}
}

void wiiu_tile_1d(const uint8_t *rows, unsigned blocks_across, unsigned blocks_down, unsigned block_bytes, unsigned pitch_blocks, uint8_t *tiled)
{
	uint8_t index[64];
	unsigned x, y, tile_x, tile_y;
	const unsigned tiles_across = pitch_blocks / 8;

	if (block_bytes != 8 && block_bytes != 16)
	{
		for (y = 0; y < blocks_down; y++)
			for (x = 0; x < blocks_across; x++)
				memcpy(tiled + wiiu_tile_1d_offset(x, y, pitch_blocks, block_bytes), rows + ((size_t)y * blocks_across + x) * block_bytes, block_bytes);
		return;
	}
	for (y = 0; y < 8; y++)
		for (x = 0; x < 8; x++)
			index[y * 8 + x] = (uint8_t)index_in_tile(x, y, block_bytes);
	for (tile_y = 0; tile_y * 8 < blocks_down; tile_y++)
		for (tile_x = 0; tile_x * 8 < blocks_across; tile_x++)
		{
			uint8_t *tile = tiled + ((size_t)tile_y * tiles_across + tile_x) * 64 * block_bytes;
			const unsigned y_end = blocks_down - tile_y * 8 < 8 ? blocks_down - tile_y * 8 : 8;
			const unsigned x_end = blocks_across - tile_x * 8 < 8 ? blocks_across - tile_x * 8 : 8;

			for (y = 0; y < y_end; y++)
			{
				const uint8_t *row = rows + ((size_t)(tile_y * 8 + y) * blocks_across + tile_x * 8) * block_bytes;

				for (x = 0; x < x_end; x++)
					copy_block(tile + (size_t)index[y * 8 + x] * block_bytes, row + (size_t)x * block_bytes, block_bytes);
			}
		}
}
