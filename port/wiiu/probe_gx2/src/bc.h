/*
BC.H

BC1 and BC3 (the Xbox's DXT1 and DXT5) blocks: the test pattern the texture test draws, a decoder to say what the
GPU should have drawn from it, the byte orders an upload can take, and the search that explains what the GPU did draw.

The question is what byte order the Latte GPU reads these blocks in. A block is a few little-endian fields (two 16-bit
colors and 32 bits of 2-bit indices; BC2 and BC3 put an 8-byte alpha block in front), and the console's CPU is big-endian,
so the data that comes out of the Xbox's files arrives in one order and the GPU may want another. The test uploads the
pattern in every order that is plausible, reads back what the GPU drew from each, and compares with the decoded
pattern; independently of the guesses, bc_explain finds which rearrangement of the block's bytes turns what was
uploaded as it is into what was drawn.

Nothing here depends on the byte order of the machine that runs it: bytes are read and written one at a time.
*/

#ifndef HALO_WIIU_GX2_BC_H
#define HALO_WIIU_GX2_BC_H

#include <stddef.h>
#include <stdint.h>

#define BC_TEXTURE_SIZE 64u /* texels on a side: 16 x 16 blocks */
#define BC_BLOCKS 16u

typedef enum bc_format
{
	BC_FORMAT_BC1, /* D3DFMT_DXT1: 8 bytes: two colors and 2-bit indices */
	BC_FORMAT_BC2, /* D3DFMT_DXT3: 16 bytes: 4-bit alpha for each texel, then a BC1 color block */
	BC_FORMAT_BC3, /* D3DFMT_DXT5: 16 bytes: an alpha block (two endpoints, 3-bit indices), then a BC1 color block */
	BC_FORMAT_COUNT
} bc_format;

size_t bc_block_bytes(bc_format format);
const char *bc_format_name(bc_format format);

/* The block at (bx, by) of the test pattern, as the Xbox holds it (little-endian). Every block has two different
colors and uses all four color indices; BC3's alpha block has a0 > a1 and uses at least six of its eight. */
void bc_pattern_block(bc_format format, unsigned bx, unsigned by, uint8_t *out);

/* Decodes a block (little-endian, as above) into 16 texels (row-major, RGBA bytes). A BC1 block with c0 <= c1 is the
three-color form with a transparent black fourth color. */
void bc_decode_block(bc_format format, const uint8_t *block, uint8_t rgba[16][4]);

/* the whole pattern decoded: BC_TEXTURE_SIZE squared texels, RGBA bytes, rows from the top */
void bc_pattern_image(bc_format format, uint8_t *rgba);

/* The orders an upload can take: every block of the pattern with its bytes rearranged. */
typedef enum bc_order
{
	BC_ORDER_AS_IS,   /* as the Xbox holds it */
	BC_ORDER_SWAP16,  /* the bytes of each 16-bit unit reversed (what a big-endian CPU reading u16 fields has) */
	BC_ORDER_SWAP32,  /* ... of each 32-bit unit */
	BC_ORDER_SWAP64,  /* ... of each 64-bit unit (the two halves of a BC3 block separately) */
	BC_ORDER_REVERSE, /* the whole block */
	BC_ORDER_COUNT
} bc_order;

const char *bc_order_name(bc_order order);
/* 0 where an order is the same as another for the format (BC1's whole block is one 64-bit unit) */
int bc_order_distinct(bc_format format, bc_order order);
/* the bytes of one block rearranged (in and out may not overlap) */
void bc_apply_order(bc_order order, size_t block_bytes, const uint8_t *in, uint8_t *out);
/* the whole pattern, BC_BLOCKS squared blocks, rows of blocks from the top, as uploaded in the order */
void bc_pattern_upload(bc_format format, bc_order order, uint8_t *out);

/* ---------- what the GPU did */

#define BC_EXPLAIN_SAMPLES 16u

/* A rearrangement of a block's bytes: what the GPU decoded was logical[i] = memory[source[i]]. */
typedef struct bc_permutation
{
	uint8_t source[16];
} bc_permutation;

/* Which rearrangements explain the drawing. observed holds, for BC_EXPLAIN_SAMPLES blocks of the pattern (block
numbers chosen by bc_explain_block), the 16 texels the GPU drew from the upload "as it is". BC1: all 40320 orders of
the eight bytes are tried; BC2 and BC3: the 32 that move the 8-byte halves and reverse or swap inside them. Returns how many
agree with every texel (within tolerance per channel) and the first of them in *first (the identity first, then the
structured orders, then the rest). 0 when none does. */
unsigned bc_explain(bc_format format, uint8_t observed[BC_EXPLAIN_SAMPLES][16][4], unsigned tolerance, bc_permutation *first);

/* the pattern's block numbers (bx, by) the explanation samples: spread over the image, all different */
void bc_explain_block(unsigned sample, unsigned *bx, unsigned *by);

/* "as it is", "swap16", "swap32", "swap64", "reverse", or the sources listed */
void bc_permutation_name(bc_format format, const bc_permutation *permutation, char *out, size_t size);

/* The upload that the GPU's rearrangement turns back into the Xbox's block: the permutation applied to an upload
as upload[source[i]] = block[i]. In and out may not overlap. */
void bc_permutation_invert_block(bc_format format, const bc_permutation *permutation, const uint8_t *block, uint8_t *upload);

#endif
