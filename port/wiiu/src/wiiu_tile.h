/*
WIIU_TILE.H

The Latte reads textures in tiled layouts, not in rows (a linear BC texture drew one block right and the rest black on the console:
reports/console/2026-10-21-probe-gx2-*). This makes the 1D tiled thin layout (GX2_TILE_MODE_TILED_1D_THIN1) from rows of blocks as the Xbox
(and a DDS file) has them, so that the bytes of each block go to the GPU as they are. Found on a console by the GX2 probe, which drew BC1,
BC2 and BC3 right this way and a search over all 720 orders of the pixel bits found no other.
*/

#ifndef HALO_WIIU_TILE_H
#define HALO_WIIU_TILE_H

#include <stddef.h>
#include <stdint.h>

/* Where in a 1D tiled surface the block at column x, row y of a surface pitch_blocks blocks wide (GX2CalcSurfaceSizeAndAlignment's pitch for
a compressed format, in blocks, a multiple of 8) is, in bytes. block_bytes is 8 (BC1) or 16 (BC2, BC3, and the other 128 bit formats). */
size_t wiiu_tile_1d_offset(unsigned x, unsigned y, unsigned pitch_blocks, unsigned block_bytes);

/* Rearranges blocks_across x blocks_down blocks (rows of blocks, no padding) into tiled, which GX2 sized (GX2Surface imageSize). */
void wiiu_tile_1d(const uint8_t *rows, unsigned blocks_across, unsigned blocks_down, unsigned block_bytes, unsigned pitch_blocks, uint8_t *tiled);

#endif
