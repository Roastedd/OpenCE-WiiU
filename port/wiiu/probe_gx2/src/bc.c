#include "bc.h"

#include <stdio.h>
#include <string.h>

size_t bc_block_bytes(bc_format format)
{
	return format == BC_FORMAT_BC1 ? 8 : 16;
}

const char *bc_format_name(bc_format format)
{
	return format == BC_FORMAT_BC3 ? "BC3" : format == BC_FORMAT_BC2 ? "BC2" : "BC1";
}

/* ---------- the pattern */

static void put_le16(uint8_t *out, unsigned value)
{
	out[0] = (uint8_t)(value & 0xFF);
	out[1] = (uint8_t)((value >> 8) & 0xFF);
}

/* Two colors that grow in opposite directions across the image and a Latin square of indices rotated block by block:
each block differs from the next in its colors and in the bytes of its indices. c0 has the larger red and so is the
larger number: the four-color form. */
static void color_block(unsigned bx, unsigned by, uint8_t *out)
{
	unsigned r0 = 16 + bx, g0 = by * 4 + 3, b0 = (31 - bx * 2) & 31;
	unsigned r1 = 15 - bx, g1 = 63 - by * 4, b1 = by * 2;
	uint32_t indices = 0;
	unsigned t;

	put_le16(out, (r0 << 11) | (g0 << 5) | b0);
	put_le16(out + 2, (r1 << 11) | (g1 << 5) | b1);
	for (t = 0; t < 16; t++)
		indices |= (uint32_t)(((t & 3) + (t >> 2) + bx + 2 * by) & 3) << (2 * t);
	out[4] = (uint8_t)(indices & 0xFF);
	out[5] = (uint8_t)((indices >> 8) & 0xFF);
	out[6] = (uint8_t)((indices >> 16) & 0xFF);
	out[7] = (uint8_t)((indices >> 24) & 0xFF);
}

void bc_pattern_block(bc_format format, unsigned bx, unsigned by, uint8_t *out)
{
	if (format == BC_FORMAT_BC1)
	{
		color_block(bx, by, out);
	}
	else if (format == BC_FORMAT_BC2)
	{
		unsigned t;

		/* sixteen different 4-bit alphas: texel t is the low nibble of byte t / 2 if t is even, else the high */
		memset(out, 0, 8);
		for (t = 0; t < 16; t++)
			out[t / 2] = (uint8_t)(out[t / 2] | (((t * 5 + bx + by * 3 + 1) & 15) << (4 * (t & 1))));
		color_block((bx + 7) & 15, (by + 9) & 15, out + 8);
	}
	else
	{
		uint64_t indices = 0;
		unsigned t;

		out[0] = (uint8_t)(255 - bx * 8);
		out[1] = (uint8_t)(by * 8);
		for (t = 0; t < 16; t++)
			indices |= (uint64_t)((3 * t + (t >> 2) + bx + by * 5) & 7) << (3 * t);
		for (t = 0; t < 6; t++)
			out[2 + t] = (uint8_t)((indices >> (8 * t)) & 0xFF);
		/* the color half of another block, so that it is not tied to the alpha half */
		color_block((bx + 5) & 15, (by + 3) & 15, out + 8);
	}
}

/* ---------- decoding */

static void expand565(unsigned c, uint8_t rgb[3])
{
	unsigned r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;

	rgb[0] = (uint8_t)((r << 3) | (r >> 2));
	rgb[1] = (uint8_t)((g << 2) | (g >> 4));
	rgb[2] = (uint8_t)((b << 3) | (b >> 2));
}

void bc_decode_block(bc_format format, const uint8_t *block, uint8_t rgba[16][4])
{
	const uint8_t *color = format == BC_FORMAT_BC1 ? block : block + 8;
	unsigned c0 = (unsigned)color[0] | ((unsigned)color[1] << 8);
	unsigned c1 = (unsigned)color[2] | ((unsigned)color[3] << 8);
	uint32_t indices = (uint32_t)color[4] | ((uint32_t)color[5] << 8) | ((uint32_t)color[6] << 16) | ((uint32_t)color[7] << 24);
	uint8_t palette[4][4];
	unsigned t, k;

	expand565(c0, palette[0]);
	expand565(c1, palette[1]);
	palette[0][3] = palette[1][3] = 255;
	if (c0 > c1 || format != BC_FORMAT_BC1)
	{
		for (k = 0; k < 3; k++)
		{
			palette[2][k] = (uint8_t)((2u * palette[0][k] + palette[1][k] + 1) / 3);
			palette[3][k] = (uint8_t)((palette[0][k] + 2u * palette[1][k] + 1) / 3);
		}
		palette[2][3] = palette[3][3] = 255;
	}
	else
	{
		for (k = 0; k < 3; k++)
			palette[2][k] = (uint8_t)((palette[0][k] + palette[1][k]) / 2);
		palette[2][3] = 255;
		palette[3][0] = palette[3][1] = palette[3][2] = palette[3][3] = 0;
	}
	for (t = 0; t < 16; t++)
		memcpy(rgba[t], palette[(indices >> (2 * t)) & 3], 4);

	if (format == BC_FORMAT_BC2)
	{
		for (t = 0; t < 16; t++)
		{
			unsigned nibble = (block[t / 2] >> (4 * (t & 1))) & 15;

			rgba[t][3] = (uint8_t)((nibble << 4) | nibble);
		}
	}
	else if (format == BC_FORMAT_BC3)
	{
		unsigned a0 = block[0], a1 = block[1];
		unsigned levels[8];
		uint64_t bits = 0;

		levels[0] = a0;
		levels[1] = a1;
		if (a0 > a1)
		{
			for (k = 1; k <= 6; k++)
				levels[k + 1] = ((7 - k) * a0 + k * a1 + 3) / 7;
		}
		else
		{
			for (k = 1; k <= 4; k++)
				levels[k + 1] = ((5 - k) * a0 + k * a1 + 2) / 5;
			levels[6] = 0;
			levels[7] = 255;
		}
		for (k = 0; k < 6; k++)
			bits |= (uint64_t)block[2 + k] << (8 * k);
		for (t = 0; t < 16; t++)
			rgba[t][3] = (uint8_t)levels[(bits >> (3 * t)) & 7];
	}
}

void bc_pattern_image(bc_format format, uint8_t *rgba)
{
	unsigned bx, by, x, y;
	uint8_t block[16];
	uint8_t texels[16][4];

	for (by = 0; by < BC_BLOCKS; by++)
	{
		for (bx = 0; bx < BC_BLOCKS; bx++)
		{
			bc_pattern_block(format, bx, by, block);
			bc_decode_block(format, block, texels);
			for (y = 0; y < 4; y++)
				for (x = 0; x < 4; x++)
					memcpy(rgba + (((by * 4 + y) * BC_TEXTURE_SIZE) + bx * 4 + x) * 4, texels[y * 4 + x], 4);
		}
	}
}

/* ---------- orders */

const char *bc_order_name(bc_order order)
{
	static const char *const names[BC_ORDER_COUNT] = { "as it is", "swap16", "swap32", "swap64", "reverse" };

	return (unsigned)order < BC_ORDER_COUNT ? names[order] : "?";
}

int bc_order_distinct(bc_format format, bc_order order)
{
	return !(format == BC_FORMAT_BC1 && order == BC_ORDER_REVERSE);
}

void bc_apply_order(bc_order order, size_t block_bytes, const uint8_t *in, uint8_t *out)
{
	size_t unit = 0;
	size_t i, j;

	switch (order)
	{
	case BC_ORDER_SWAP16: unit = 2; break;
	case BC_ORDER_SWAP32: unit = 4; break;
	case BC_ORDER_SWAP64: unit = 8; break;
	case BC_ORDER_REVERSE: unit = block_bytes; break;
	default: break;
	}
	if (!unit)
	{
		memcpy(out, in, block_bytes);
		return;
	}
	for (i = 0; i + unit <= block_bytes; i += unit)
		for (j = 0; j < unit; j++)
			out[i + j] = in[i + unit - 1 - j];
}

void bc_pattern_upload(bc_format format, bc_order order, uint8_t *out)
{
	size_t bytes = bc_block_bytes(format);
	unsigned bx, by;
	uint8_t block[16];

	for (by = 0; by < BC_BLOCKS; by++)
	{
		for (bx = 0; bx < BC_BLOCKS; bx++)
		{
			bc_pattern_block(format, bx, by, block);
			bc_apply_order(order, bytes, block, out + (by * BC_BLOCKS + bx) * bytes);
		}
	}
}

/* ---------- explaining what the GPU drew */

void bc_explain_block(unsigned sample, unsigned *bx, unsigned *by)
{
	*bx = (sample * 5 + 1) % BC_BLOCKS;
	*by = (sample * 7 + 3) % BC_BLOCKS;
}

static void permute(const bc_permutation *permutation, size_t bytes, const uint8_t *memory, uint8_t *logical)
{
	size_t i;

	for (i = 0; i < bytes; i++)
		logical[i] = memory[permutation->source[i]];
}

typedef struct explain_state
{
	bc_format format;
	size_t bytes;
	unsigned tolerance;
	uint8_t memory[BC_EXPLAIN_SAMPLES][16];
	uint8_t (*observed)[16][4];
} explain_state;

static int consistent(const explain_state *state, const bc_permutation *permutation)
{
	unsigned s, t, k;

	for (s = 0; s < BC_EXPLAIN_SAMPLES; s++)
	{
		uint8_t logical[16] = { 0 };
		uint8_t texels[16][4];

		permute(permutation, state->bytes, state->memory[s], logical);
		bc_decode_block(state->format, logical, texels);
		for (t = 0; t < 16; t++)
		{
			for (k = 0; k < 4; k++)
			{
				int d = (int)texels[t][k] - (int)state->observed[s][t][k];

				if (d < -(int)state->tolerance || d > (int)state->tolerance)
					return 0;
			}
		}
	}
	return 1;
}

/* where byte i of a unit of the order comes from */
static unsigned unit_source(unsigned kind, unsigned i)
{
	switch (kind)
	{
	case 1: return i ^ 1;
	case 2: return i ^ 3;
	case 3: return 7 - i;
	default: return i;
	}
}

/* BC3: half 0 of the decoded block comes from memory half `from`, rearranged by lo; half 1 from the other, by hi */
static void structured(unsigned halves_swapped, unsigned lo, unsigned hi, bc_permutation *out)
{
	unsigned i;

	for (i = 0; i < 8; i++)
	{
		out->source[i] = (uint8_t)((halves_swapped ? 8 : 0) + unit_source(lo, i));
		out->source[8 + i] = (uint8_t)((halves_swapped ? 0 : 8) + unit_source(hi, i));
	}
}

/* BC1's orders of the eight bytes, the structured ones first */
static void structured_bc1(unsigned kind, bc_permutation *out)
{
	unsigned i;

	memset(out, 0, sizeof(*out));
	for (i = 0; i < 8; i++)
		out->source[i] = (uint8_t)unit_source(kind, i);
}

/* the next order of n bytes in lexicographic order; 0 after the last */
static int next_permutation(uint8_t *a, unsigned n)
{
	int i = (int)n - 2, j;

	while (i >= 0 && a[i] >= a[i + 1])
		i--;
	if (i < 0)
		return 0;
	j = (int)n - 1;
	while (a[j] <= a[i])
		j--;
	{
		uint8_t t = a[i];

		a[i] = a[j];
		a[j] = t;
	}
	for (j = (int)n - 1, i++; i < j; i++, j--)
	{
		uint8_t t = a[i];

		a[i] = a[j];
		a[j] = t;
	}
	return 1;
}

unsigned bc_explain(bc_format format, uint8_t observed[BC_EXPLAIN_SAMPLES][16][4], unsigned tolerance, bc_permutation *first)
{
	explain_state state;
	unsigned count = 0, s, i;
	bc_permutation candidate;
	int have_first = 0;

	memset(&state, 0, sizeof(state));
	state.format = format;
	state.bytes = bc_block_bytes(format);
	state.tolerance = tolerance;
	state.observed = observed;
	for (s = 0; s < BC_EXPLAIN_SAMPLES; s++)
	{
		unsigned bx, by;

		bc_explain_block(s, &bx, &by);
		bc_pattern_block(format, bx, by, state.memory[s]);
	}
	memset(first, 0, sizeof(*first));

	if (format != BC_FORMAT_BC1)
	{
		unsigned halves, lo, hi;

		for (halves = 0; halves < 2; halves++)
		{
			for (lo = 0; lo < 4; lo++)
			{
				for (hi = 0; hi < 4; hi++)
				{
					structured(halves, lo, hi, &candidate);
					if (consistent(&state, &candidate))
					{
						if (!have_first)
							*first = candidate;
						have_first = 1;
						count++;
					}
				}
			}
		}
		return count;
	}

	/* BC1: the identity, then the structured orders, take precedence as the one named ... */
	for (i = 0; i < 4; i++)
	{
		structured_bc1(i, &candidate);
		if (consistent(&state, &candidate))
		{
			*first = candidate;
			have_first = 1;
			break;
		}
	}
	/* ... and every order of the eight bytes is counted */
	for (i = 0; i < 8; i++)
		candidate.source[i] = (uint8_t)i;
	do
	{
		if (consistent(&state, &candidate))
		{
			if (!have_first)
				*first = candidate;
			have_first = 1;
			count++;
		}
	}
	while (next_permutation(candidate.source, 8));
	return count;
}

void bc_permutation_name(bc_format format, const bc_permutation *permutation, char *out, size_t size)
{
	static const bc_order orders[] = { BC_ORDER_AS_IS, BC_ORDER_SWAP16, BC_ORDER_SWAP32, BC_ORDER_SWAP64, BC_ORDER_REVERSE };
	size_t bytes = bc_block_bytes(format);
	uint8_t identity[16], expected[16];
	size_t i;
	unsigned o;
	int used;

	for (i = 0; i < bytes; i++)
		identity[i] = (uint8_t)i;
	for (o = 0; o < sizeof(orders) / sizeof(orders[0]); o++)
	{
		if (!bc_order_distinct(format, orders[o]))
			continue;
		bc_apply_order(orders[o], bytes, identity, expected);
		if (!memcmp(expected, permutation->source, bytes))
		{
			snprintf(out, size, "%s", bc_order_name(orders[o]));
			return;
		}
	}
	used = snprintf(out, size, "bytes");
	for (i = 0; i < bytes && used > 0 && (size_t)used < size; i++)
		used += snprintf(out + used, size - (size_t)used, "%c%u", i ? ',' : ' ', (unsigned)permutation->source[i]);
}

void bc_permutation_invert_block(bc_format format, const bc_permutation *permutation, const uint8_t *block, uint8_t *upload)
{
	size_t bytes = bc_block_bytes(format);
	size_t i;

	for (i = 0; i < bytes; i++)
		upload[permutation->source[i]] = block[i];
}
