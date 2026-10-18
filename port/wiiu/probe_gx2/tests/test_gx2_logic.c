/*
The GX2 probe's portable logic on known answers: the BC1/BC3 pattern, its decoder and the search that explains a drawing,
the vertex stream and index models, the geometry the tests look at, the font, the embedded shader files and the report
helpers. Built for the desktop, for 32-bit x86 and for 32-bit big-endian PowerPC (run under qemu-ppc), with the
sanitizers where there are any: tools/wiiu/probe_gx2_tests.sh. Exit status 0 when everything holds; a failing check says
which and exits 1.

The byte order of the machine must not matter to any of it: the data these modules make is a list of bytes.
*/

#include "../src/answers.h"
#include "../src/bc.h"
#include "../src/font_mini.h"
#include "../src/gsh_check.h"
#include "../src/iface_check.h"
#include "../src/index_test.h"
#include "../src/readback.h"
#include "../src/scene.h"
#include "../src/shader_data.h"
#include "../src/vertex_test.h"

#include <stdio.h>
#include <string.h>

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

/* ---------- BC */

static unsigned pixel_diff(const uint8_t *a, const uint8_t *b)
{
	return readback_distance(a, b);
}

static void test_bc_pattern(void)
{
	static uint8_t image[BC_TEXTURE_SIZE * BC_TEXTURE_SIZE * 4];
	uint8_t block[16], texels[16][4];
	unsigned bx, by, t, f;

	/* block (0, 0) by hand: c0 = 0x807F (132, 12, 255), c1 = 0x7FE0 (123, 255, 0), first index 0 */
	bc_pattern_block(BC_FORMAT_BC1, 0, 0, block);
	CHECK(block[0] == 0x7F && block[1] == 0x80 && block[2] == 0xE0 && block[3] == 0x7F);
	bc_decode_block(BC_FORMAT_BC1, block, texels);
	CHECK(texels[0][0] == 132 && texels[0][1] == 12 && texels[0][2] == 255 && texels[0][3] == 255);
	/* texel 1 has index 1: c1 */
	CHECK(texels[1][0] == 123 && texels[1][1] == 255 && texels[1][2] == 0);

	for (f = 0; f < BC_FORMAT_COUNT; f++)
	{
		bc_format format = (bc_format)f;
		size_t bytes = bc_block_bytes(format);

		CHECK(bytes == (f ? 16u : 8u));
		for (by = 0; by < BC_BLOCKS; by++)
		{
			for (bx = 0; bx < BC_BLOCKS; bx++)
			{
				unsigned seen = 0, alpha_seen = 0, c0, c1;
				const uint8_t *color;
				uint32_t indices;

				bc_pattern_block(format, bx, by, block);
				color = f ? block + 8 : block;
				c0 = color[0] | (color[1] << 8);
				c1 = color[2] | (color[3] << 8);
				CHECK(c0 > c1);
				indices = (uint32_t)color[4] | ((uint32_t)color[5] << 8) | ((uint32_t)color[6] << 16) | ((uint32_t)color[7] << 24);
				for (t = 0; t < 16; t++)
					seen |= 1u << ((indices >> (2 * t)) & 3);
				CHECK(seen == 15);
				if (f == BC_FORMAT_BC2)
				{
					unsigned seen_alpha = 0;

					for (t = 0; t < 16; t++)
						seen_alpha |= 1u << ((block[t / 2] >> (4 * (t & 1))) & 15);
					CHECK(seen_alpha == 0xFFFF); /* all sixteen levels in every block */
				}
				if (f == BC_FORMAT_BC3)
				{
					uint64_t bits = 0;
					unsigned k;

					CHECK(block[0] > block[1]);
					for (k = 0; k < 6; k++)
						bits |= (uint64_t)block[2 + k] << (8 * k);
					for (t = 0; t < 16; t++)
						alpha_seen |= 1u << ((bits >> (3 * t)) & 7);
					/* seven of the eight levels (which one is left out turns with the block) */
					CHECK(alpha_seen == 0xFE || alpha_seen == 0xFD || alpha_seen == 0xFB || alpha_seen == 0xF7 || alpha_seen == 0xEF ||
						alpha_seen == 0xDF || alpha_seen == 0xBF || alpha_seen == 0x7F);
				}
			}
		}
		bc_pattern_image(format, image);
		/* the image is made of the blocks: spot-check three */
		bc_pattern_block(format, 15, 15, block);
		bc_decode_block(format, block, texels);
		CHECK(!memcmp(image + ((63 * BC_TEXTURE_SIZE) + 63) * 4, texels[15], 4));
		bc_pattern_block(format, 3, 7, block);
		bc_decode_block(format, block, texels);
		CHECK(!memcmp(image + (((7 * 4 + 2) * BC_TEXTURE_SIZE) + 3 * 4 + 1) * 4, texels[9], 4));
		if (f == BC_FORMAT_BC2)
		{
			/* the alpha nibbles, widened: texel t of block (0, 0) has (5 t + 1) & 15 */
			bc_pattern_block(BC_FORMAT_BC2, 0, 0, block);
			bc_decode_block(BC_FORMAT_BC2, block, texels);
			CHECK(texels[0][3] == 0x11 && texels[1][3] == 0x66 && texels[3][3] == 0x00 && texels[15][3] == 0xCC);
		}
		if (f == BC_FORMAT_BC3)
		{
			/* alpha levels: a0 = 255 - 8 bx, a1 = 8 by; index 1 is a1 and index 0 is a0 */
			bc_pattern_block(BC_FORMAT_BC3, 2, 5, block);
			bc_decode_block(BC_FORMAT_BC3, block, texels);
			CHECK(block[0] == 239 && block[1] == 40);
			for (t = 0; t < 16; t++)
				CHECK(texels[t][3] >= 40 && texels[t][3] <= 239);
		}
	}
}

static void test_bc_decoder(void)
{
	uint8_t block[16], texels[16][4];

	/* BC1, c0 <= c1: three colors and a transparent black */
	memset(block, 0, sizeof(block));
	block[0] = 0x00; block[1] = 0x00; /* c0 = black */
	block[2] = 0xFF; block[3] = 0xFF; /* c1 = white */
	block[4] = 0xE4; /* indices 0, 1, 2, 3 for the first four texels */
	bc_decode_block(BC_FORMAT_BC1, block, texels);
	CHECK(texels[0][0] == 0 && texels[0][3] == 255);
	CHECK(texels[1][0] == 255 && texels[1][1] == 255 && texels[1][2] == 255);
	CHECK(texels[2][0] == 127 && texels[2][3] == 255);
	CHECK(texels[3][0] == 0 && texels[3][3] == 0);

	/* BC3 alpha, a0 <= a1: six levels, 0 and 255 */
	memset(block, 0, sizeof(block));
	block[0] = 10; block[1] = 200;
	block[2] = (uint8_t)(0 | (1 << 3) | (6 << 6)); /* texels 0, 1: indices 0, 1; texel 2 low bits of 6 */
	block[3] = (uint8_t)(6 >> 2); /* texel 2 = 6 (zero), texel 3 = 0 */
	bc_decode_block(BC_FORMAT_BC3, block, texels);
	CHECK(texels[0][3] == 10 && texels[1][3] == 200 && texels[2][3] == 0);
	block[2] = (uint8_t)((7 << 0)); /* texel 0 = 7: 255 */
	bc_decode_block(BC_FORMAT_BC3, block, texels);
	CHECK(texels[0][3] == 255);
	/* eight levels: 7 * 255 / 7 ... index 2 of a0 = 255, a1 = 0 is 6/7 */
	memset(block, 0, sizeof(block));
	block[0] = 255; block[1] = 0; block[2] = 2;
	bc_decode_block(BC_FORMAT_BC3, block, texels);
	CHECK(pixel_diff(texels[0], (const uint8_t[4]){ 0, 0, 0, 218 }) <= 1);
}

static void test_bc_orders(void)
{
	uint8_t in[16], out[16], back[16];
	unsigned o, i, f;

	for (i = 0; i < 16; i++)
		in[i] = (uint8_t)(0x10 + i);
	bc_apply_order(BC_ORDER_SWAP16, 8, in, out);
	CHECK(out[0] == 0x11 && out[1] == 0x10 && out[2] == 0x13 && out[7] == 0x16);
	bc_apply_order(BC_ORDER_SWAP32, 8, in, out);
	CHECK(out[0] == 0x13 && out[3] == 0x10 && out[4] == 0x17 && out[7] == 0x14);
	bc_apply_order(BC_ORDER_SWAP64, 16, in, out);
	CHECK(out[0] == 0x17 && out[7] == 0x10 && out[8] == 0x1F && out[15] == 0x18);
	bc_apply_order(BC_ORDER_REVERSE, 16, in, out);
	CHECK(out[0] == 0x1F && out[15] == 0x10);
	for (f = 0; f < BC_FORMAT_COUNT; f++)
	{
		bc_format format = (bc_format)f;
		size_t bytes = bc_block_bytes(format);

		for (o = 0; o < BC_ORDER_COUNT; o++)
		{
			/* every order undoes itself */
			bc_apply_order((bc_order)o, bytes, in, out);
			bc_apply_order((bc_order)o, bytes, out, back);
			CHECK(!memcmp(in, back, bytes));
		}
	}
	CHECK(!bc_order_distinct(BC_FORMAT_BC1, BC_ORDER_REVERSE) && bc_order_distinct(BC_FORMAT_BC3, BC_ORDER_REVERSE));
	CHECK(!strcmp(bc_order_name(BC_ORDER_SWAP32), "swap32"));
}

/* the GPU as a rearrangement of the block's bytes, and what it draws from the pattern uploaded as it is */
static void draw_samples(bc_format format, const bc_permutation *gpu, int jitter, uint8_t observed[BC_EXPLAIN_SAMPLES][16][4])
{
	unsigned s, t, k;
	size_t bytes = bc_block_bytes(format);

	for (s = 0; s < BC_EXPLAIN_SAMPLES; s++)
	{
		unsigned bx, by;
		uint8_t block[16], logical[16];

		bc_explain_block(s, &bx, &by);
		bc_pattern_block(format, bx, by, block);
		for (k = 0; k < bytes; k++)
			logical[k] = block[gpu->source[k]];
		bc_decode_block(format, logical, observed[s]);
		if (jitter)
			for (t = 0; t < 16; t++)
				for (k = 0; k < 4; k++)
				{
					int v = observed[s][t][k] + (((int)(s + t + k) % 3) - 1) * 2;

					observed[s][t][k] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
				}
	}
}

static void make_permutation(bc_format format, bc_order order, bc_permutation *p)
{
	uint8_t identity[16];
	unsigned i;

	memset(p, 0, sizeof(*p));
	for (i = 0; i < 16; i++)
		identity[i] = (uint8_t)i;
	bc_apply_order(order, bc_block_bytes(format), identity, p->source);
}

static void test_bc_explain(void)
{
	static uint8_t observed[BC_EXPLAIN_SAMPLES][16][4];
	unsigned f, o, s;
	bc_permutation gpu, found;
	char name[64];

	/* the sample blocks are all different */
	for (s = 0; s < BC_EXPLAIN_SAMPLES; s++)
	{
		unsigned bx, by, u, ux, uy;

		bc_explain_block(s, &bx, &by);
		for (u = s + 1; u < BC_EXPLAIN_SAMPLES; u++)
		{
			bc_explain_block(u, &ux, &uy);
			CHECK(bx != ux && by != uy);
		}
	}
	for (f = 0; f < BC_FORMAT_COUNT; f++)
	{
		bc_format format = (bc_format)f;

		for (o = 0; o < BC_ORDER_COUNT; o++)
		{
			unsigned jitter;

			if (!bc_order_distinct(format, (bc_order)o))
				continue;
			for (jitter = 0; jitter < 2; jitter++)
			{
				unsigned count;

				make_permutation(format, (bc_order)o, &gpu);
				draw_samples(format, &gpu, (int)jitter, observed);
				count = bc_explain(format, observed, 4, &found);
				CHECK(count == 1);
				CHECK(!memcmp(found.source, gpu.source, bc_block_bytes(format)));
				bc_permutation_name(format, &found, name, sizeof(name));
				CHECK(!strcmp(name, bc_order_name((bc_order)o)));
			}
		}
	}

	/* an order none of the named ones is: found among the 40320 */
	memset(&gpu, 0, sizeof(gpu));
	{
		static const uint8_t odd[8] = { 2, 3, 0, 1, 7, 6, 5, 4 };

		memcpy(gpu.source, odd, 8);
		draw_samples(BC_FORMAT_BC1, &gpu, 0, observed);
		CHECK(bc_explain(BC_FORMAT_BC1, observed, 2, &found) == 1);
		CHECK(!memcmp(found.source, odd, 8));
		bc_permutation_name(BC_FORMAT_BC1, &found, name, sizeof(name));
		CHECK(!strcmp(name, "bytes 2,3,0,1,7,6,5,4"));
	}
	/* BC3 with the halves exchanged and the color half swapped by 32 bits */
	{
		static const uint8_t odd[16] = { 11, 10, 9, 8, 15, 14, 13, 12, 0, 1, 2, 3, 4, 5, 6, 7 };

		memcpy(gpu.source, odd, 16);
		draw_samples(BC_FORMAT_BC3, &gpu, 0, observed);
		CHECK(bc_explain(BC_FORMAT_BC3, observed, 2, &found) == 1);
		CHECK(!memcmp(found.source, odd, 16));
	}
	/* a drawing that no order explains */
	draw_samples(BC_FORMAT_BC1, &gpu, 0, observed);
	memset(observed, 7, sizeof(observed));
	CHECK(bc_explain(BC_FORMAT_BC1, observed, 4, &found) == 0);
}

/* the answer the program gives: upload so that the GPU's rearrangement gives the Xbox's block back */
static void test_bc_invert(void)
{
	unsigned f, o;

	for (f = 0; f < BC_FORMAT_COUNT; f++)
	{
		bc_format format = (bc_format)f;
		size_t bytes = bc_block_bytes(format);

		for (o = 0; o < BC_ORDER_COUNT; o++)
		{
			bc_permutation gpu;
			uint8_t block[16], upload[16], logical[16];
			unsigned k;

			if (!bc_order_distinct(format, (bc_order)o))
				continue;
			make_permutation(format, (bc_order)o, &gpu);
			bc_pattern_block(format, 6, 9, block);
			bc_permutation_invert_block(format, &gpu, block, upload);
			for (k = 0; k < bytes; k++)
				logical[k] = upload[gpu.source[k]];
			CHECK(!memcmp(logical, block, bytes));
		}
	}
	/* a cyclic order (not an involution) too */
	{
		bc_permutation gpu;
		uint8_t block[16], upload[16], logical[16];
		unsigned k;

		for (k = 0; k < 8; k++)
			gpu.source[k] = (uint8_t)((k + 3) & 7);
		bc_pattern_block(BC_FORMAT_BC1, 1, 2, block);
		bc_permutation_invert_block(BC_FORMAT_BC1, &gpu, block, upload);
		for (k = 0; k < 8; k++)
			logical[k] = upload[gpu.source[k]];
		CHECK(!memcmp(logical, block, 8));
	}
}

/* the whole texture: with a GPU that rearranges, exactly the variant that undoes it draws the pattern */
static void test_bc_variants(void)
{
	static uint8_t upload[BC_BLOCKS * BC_BLOCKS * 16];
	static uint8_t expected[BC_TEXTURE_SIZE * BC_TEXTURE_SIZE * 4];
	unsigned f, g, v;

	for (f = 0; f < BC_FORMAT_COUNT; f++)
	{
		bc_format format = (bc_format)f;
		size_t bytes = bc_block_bytes(format);

		bc_pattern_image(format, expected);
		for (g = 0; g < BC_ORDER_COUNT; g++)
		{
			unsigned matching = 0;

			if (!bc_order_distinct(format, (bc_order)g))
				continue;
			for (v = 0; v < BC_ORDER_COUNT; v++)
			{
				unsigned blocks, bad = 0, i;
				bc_permutation gpu;

				if (!bc_order_distinct(format, (bc_order)v))
					continue;
				make_permutation(format, (bc_order)g, &gpu);
				bc_pattern_upload(format, (bc_order)v, upload);
				for (blocks = 0; blocks < BC_BLOCKS * BC_BLOCKS; blocks++)
				{
					uint8_t logical[16], texels[16][4];
					unsigned x = blocks % BC_BLOCKS, y = blocks / BC_BLOCKS, tx, ty;

					for (i = 0; i < bytes; i++)
						logical[i] = upload[blocks * bytes + gpu.source[i]];
					bc_decode_block(format, logical, texels);
					for (ty = 0; ty < 4; ty++)
						for (tx = 0; tx < 4; tx++)
							if (pixel_diff(texels[ty * 4 + tx], expected + (((y * 4 + ty) * BC_TEXTURE_SIZE) + x * 4 + tx) * 4) > 4)
								bad++;
				}
				if (!bad)
					matching++;
				/* an involution undoes itself: the variant with the GPU's own order draws right */
				CHECK((!bad) == (v == g));
			}
			CHECK(matching == 1);
		}
	}
}

/* ---------- readback */

static void test_readback(void)
{
	uint8_t found[4], order_found[4];
	readback_order order, order2;
	unsigned a, b, c, d;
	static const uint8_t expected[4] = { 51, 102, 153, 204 };
	uint8_t image[2 * 16], pixel[4];
	readback_compare result;
	uint8_t want[2 * 2 * 4];

	/* every order of four channels is found */
	for (a = 0; a < 4; a++)
		for (b = 0; b < 4; b++)
			for (c = 0; c < 4; c++)
				for (d = 0; d < 4; d++)
				{
					unsigned map[4];
					unsigned i;

					map[0] = a; map[1] = b; map[2] = c; map[3] = d;
					if (a == b || a == c || a == d || b == c || b == d || c == d)
						continue;
					for (i = 0; i < 4; i++)
						found[map[i]] = expected[i];
					CHECK(readback_detect_order(found, expected, &order));
					for (i = 0; i < 4; i++)
						CHECK(order.source[i] == map[i]);
				}
	/* two channels the same: no answer */
	order_found[0] = 51; order_found[1] = 51; order_found[2] = 153; order_found[3] = 204;
	CHECK(!readback_detect_order(order_found, expected, &order2));
	CHECK(order2.source[0] == 0 && order2.source[3] == 3);

	/* a 2 x 2 image with rows of 16 bytes, in the order b, g, r, a */
	memset(image, 0xAA, sizeof(image));
	{
		unsigned x, y;

		/* rgba[i] = memory[source[i]]: r from memory byte 2, g from 1, b from 0, a from 3 */
		order.source[0] = 2; order.source[1] = 1; order.source[2] = 0; order.source[3] = 3;
		for (y = 0; y < 2; y++)
			for (x = 0; x < 2; x++)
			{
				uint8_t *p = image + y * 16 + x * 4;

				p[0] = (uint8_t)(10 * (y * 2 + x) + 3); /* b */
				p[1] = (uint8_t)(10 * (y * 2 + x) + 2); /* g */
				p[2] = (uint8_t)(10 * (y * 2 + x) + 1); /* r */
				p[3] = 255;
				want[(y * 2 + x) * 4 + 0] = p[2];
				want[(y * 2 + x) * 4 + 1] = p[1];
				want[(y * 2 + x) * 4 + 2] = p[0];
				want[(y * 2 + x) * 4 + 3] = p[3];
			}
	}
	readback_pixel(image, 16, 1, 1, &order, pixel);
	CHECK(pixel[0] == 31 && pixel[1] == 32 && pixel[2] == 33 && pixel[3] == 255);
	readback_compare_image(image, 16, 2, 2, &order, want, 0, &result);
	CHECK(result.pixels == 4 && result.mismatches == 0 && result.max_error == 0);
	want[3 * 4 + 1] = 40; /* one pixel 8 off in green */
	readback_compare_image(image, 16, 2, 2, &order, want, 3, &result);
	CHECK(result.mismatches == 1 && result.max_error == 8 && result.first_x == 1 && result.first_y == 1);
	CHECK(result.first_found[1] == 32 && result.first_expected[1] == 40);
	CHECK(readback_hash(image, 16, 2, 2, &order) == readback_hash(image, 16, 2, 2, &order));
	image[0] ^= 1;
	CHECK(readback_hash(image, 16, 2, 2, &order) != readback_hash(image, 16, 1, 2, &order));
}

/* ---------- vertex streams */

static void test_vertex(void)
{
	unsigned f, which, h, k;

	for (f = 0; f < VTX_FORMAT_COUNT; f++)
	{
		vtx_format format = (vtx_format)f;

		CHECK(vtx_format_bytes(format) >= 4 && vtx_format_bytes(format) <= VTX_MAX_BYTES);
		for (which = 0; which < 2; which++)
		{
			uint8_t bytes[VTX_MAX_BYTES], sel[4], pixel[4], intended_pixel[4];
			float intended[4], fetched[4], selected[4];

			vtx_pack_le(format, which, bytes);
			vtx_intended(format, which, intended);
			vtx_to_pixel(intended, intended_pixel);

			/* little-endian data needs no swap: the fetch that does not swap delivers the intended value (before the mask for the byte color) */
			vtx_selection_for(format, VTX_SWAP_NONE, sel);
			vtx_decode(format, VTX_SWAP_NONE, bytes, fetched);
			vtx_select(fetched, sel, selected);
			for (k = 0; k < 4; k++)
				CHECK(selected[k] - intended[k] < 1e-6f && intended[k] - selected[k] < 1e-6f);

			/* every swap, with the selection made for it, gives the byte color; for the others a wrong swap shows in
			one of the two triangles at least (checked below, with both) */
			for (h = 0; h < VTX_SWAP_COUNT; h++)
			{
				vtx_selection_for(format, (vtx_swap)h, sel);
				vtx_expected_pixel(format, which, (vtx_swap)h, sel, pixel);
				if (format == VTX_UNORM8X4 || h == VTX_SWAP_NONE)
					CHECK(readback_distance(pixel, intended_pixel) <= 1);
			}
		}
		for (h = VTX_SWAP_8_IN_16; h < VTX_SWAP_COUNT && format != VTX_UNORM8X4; h++)
		{
			uint8_t sel[4], wrong[2][4], right[2][4];

			vtx_selection_for(format, VTX_SWAP_NONE, sel);
			for (which = 0; which < 2; which++)
			{
				vtx_expected_pixel(format, which, (vtx_swap)h, sel, wrong[which]);
				vtx_expected_pixel(format, which, VTX_SWAP_NONE, sel, right[which]);
			}
			CHECK(readback_distance(wrong[0], right[0]) >= 24 || readback_distance(wrong[1], right[1]) >= 24);
		}

		/* the three hypotheses are told apart by the pixels (under the selection for no swap) */
		{
			uint8_t sel[4], a[VTX_SWAP_COUNT][4], b[VTX_SWAP_COUNT][4];
			unsigned i, j;

			vtx_selection_for(format, VTX_SWAP_NONE, sel);
			for (h = 0; h < VTX_SWAP_COUNT; h++)
			{
				vtx_expected_pixel(format, 0, (vtx_swap)h, sel, a[h]);
				vtx_expected_pixel(format, 1, (vtx_swap)h, sel, b[h]);
			}
			for (i = 0; i < VTX_SWAP_COUNT; i++)
			{
				CHECK(vtx_classify(format, sel, a[i], b[i], 3) == (1u << i));
				for (j = i + 1; j < VTX_SWAP_COUNT; j++)
					CHECK(readback_distance(a[i], a[j]) >= 24 || readback_distance(b[i], b[j]) >= 24);
			}
		}
	}

	/* the intended values themselves */
	{
		float v[4];
		uint8_t px[4];

		vtx_intended(VTX_UNORM8X4, 0, v); /* bytes B, G, R, A = 30, 60, 90, C0 */
		vtx_to_pixel(v, px);
		CHECK(v[0] > 0.56f && v[0] < 0.57f && v[2] > 0.18f && v[2] < 0.19f);
		vtx_intended(VTX_PACKED3, 0, v);
		CHECK(v[0] > 0.684f && v[0] < 0.685f && v[1] < -0.29f && v[1] > -0.294f && v[2] > 0.39f && v[2] < 0.392f && v[3] == 1.0f);
		vtx_intended(VTX_SSCALED16X2, 0, v);
		CHECK(v[0] == 1.0f && v[1] == -2.0f && v[2] == 0.0f && v[3] == 1.0f);
		/* a pixel back to the value that draws it */
		px[0] = 255; px[1] = 0; px[2] = 128; px[3] = 51;
		vtx_value_for_pixel(px, v);
		vtx_to_pixel(v, px);
		CHECK(px[0] == 255 && px[1] == 0 && px[2] == 128 && px[3] == 51);
	}
	{
		/* the byte color as the swaps arrange it */
		uint8_t sel[4];

		vtx_selection_for(VTX_UNORM8X4, VTX_SWAP_NONE, sel);
		CHECK(sel[0] == 2 && sel[1] == 1 && sel[2] == 0 && sel[3] == 3);
		vtx_selection_for(VTX_UNORM8X4, VTX_SWAP_8_IN_32, sel);
		CHECK(sel[0] == 1 && sel[1] == 2 && sel[2] == 3 && sel[3] == 0);
		vtx_selection_for(VTX_UNORM8X4, VTX_SWAP_8_IN_16, sel);
		CHECK(sel[0] == 3 && sel[1] == 0 && sel[2] == 1 && sel[3] == 2);
	}
}

/* ---------- index buffers */

static void test_index(void)
{
	unsigned t, big;
	uint8_t data[IDX_MAX_BYTES];
	unsigned read[IDX_COUNT];

	for (t = 0; t < IDX_TYPE_COUNT; t++)
	{
		idx_type type = (idx_type)t;

		CHECK(idx_type_width(type) == (t == IDX_U32_LE || t == IDX_U32 ? 4u : 2u));
		CHECK(idx_type_swaps(type) == (t == IDX_U16 || t == IDX_U32));
		for (big = 0; big < 2; big++)
		{
			idx_pack(idx_type_width(type), (int)big, data);
			/* a type reads what it is given exactly when its byte order is the data's */
			CHECK(idx_reads_correctly(type, data) == (idx_type_swaps(type) == (int)big));
			idx_read(type, data, read);
			if (idx_type_swaps(type) == (int)big)
				CHECK(idx_corners(read) == 0xD);
			else
				CHECK(idx_corners(read) != 0xD);
		}
	}
	idx_pack(2, 0, data);
	CHECK(data[0] == 9 && data[1] == 0 && data[2] == 10 && data[3] == 0);
	idx_pack(2, 1, data);
	CHECK(data[0] == 0 && data[1] == 9 && data[2] == 0 && data[3] == 10);
	idx_pack(4, 0, data);
	CHECK(data[0] == 9 && data[1] == 0 && data[2] == 0 && data[3] == 0);
	idx_pack(4, 1, data);
	CHECK(data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 9);
	/* the mismatched 16-bit reads stay inside 65536 vertices: the zero-filled buffer of 1 MiB the test draws from is enough */
	idx_pack(2, 0, data);
	idx_read(IDX_U16, data, read);
	for (t = 0; t < IDX_COUNT; t++)
		CHECK(read[t] <= 0xFFFFu);
}

/* ---------- the scenes */

static void test_scene(void)
{
	scene_point halves[6], corners[12];
	unsigned i, c, x, y, k;

	CHECK(scene_clear_rgba[0] != scene_clear_rgba[1] && scene_clear_rgba[1] != scene_clear_rgba[2] &&
		scene_clear_rgba[2] != scene_clear_rgba[3] && scene_clear_rgba[0] != scene_clear_rgba[3]);

	scene_halves(halves);
	for (i = 0; i < 2; i++)
	{
		scene_halves_sample(i, &x, &y);
		CHECK(scene_inside(halves + 3 * i, x, y));
		CHECK(!scene_inside(halves + 3 * (1 - i), x, y));
	}

	scene_corners(corners);
	for (c = 0; c < 4; c++)
	{
		scene_corner_sample(c, &x, &y);
		CHECK(scene_inside(corners + 3 * c, x, y));
		for (k = 0; k < 4; k++)
			if (k != c)
				CHECK(!scene_inside(corners + 3 * k, x, y));
	}
	/* the corner numbers are where they are drawn: 0 top left, 1 top right, 2 bottom left, 3 bottom right */
	scene_corner_sample(0, &x, &y);
	CHECK(x < 32 && y < 32);
	scene_corner_sample(1, &x, &y);
	CHECK(x >= 32 && y < 32);
	scene_corner_sample(2, &x, &y);
	CHECK(x < 32 && y >= 32);
	scene_corner_sample(3, &x, &y);
	CHECK(x >= 32 && y >= 32);

	{
		float p[3][4], col[3][4];
		scene_point flipped[3];
		unsigned ix, iy, ox, oy;
		uint8_t rgba[4];

		scene_generated(p, col);
		for (i = 0; i < 3; i++)
		{
			flipped[i].x = p[i][0];
			flipped[i].y = -p[i][1]; /* the generated vertex shader's gl_Position.y = -gl_Position.y */
		}
		scene_generated_sample(&ix, &iy, &ox, &oy);
		CHECK(scene_inside(flipped, ix, iy));
		CHECK(!scene_inside(flipped, ox, oy));
		scene_generated_color(rgba);
		for (k = 0; k < 4; k++)
			CHECK((unsigned)(col[0][k] * 255.0f + 0.5f) == rgba[k] || k < 3);
		CHECK(rgba[3] == 255 && col[0][3] == 1.0f);
	}
}

/* ---------- the font */

static void test_font(void)
{
	uint8_t rows[FONT_GLYPH_H];
	static uint8_t pixels[40 * 24 * 4];
	text_layer layer;
	unsigned c, lit = 0, i;

	CHECK(font_glyph('A', rows) && rows[0] == 2 && rows[1] == 5 && rows[2] == 7 && rows[3] == 5 && rows[4] == 5);
	CHECK(font_glyph('a', rows) && rows[2] == 7);
	CHECK(font_glyph('1', rows) && rows[0] == 2 && rows[4] == 7);
	CHECK(!font_glyph('~', rows) && rows[0] == 7 && rows[4] == 7);
	/* the characters the pages use have glyphs */
	for (c = 0; c < strlen("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 .,:;-+=_/()[]<>!?%#*'\""); c++)
		CHECK(font_glyph("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 .,:;-+=_/()[]<>!?%#*'\""[c], rows));
	/* every glyph has 15 entries and lit pixels (but the space) */
	for (c = 0x21; c < 0x7F; c++)
	{
		unsigned any = 0;

		if (font_glyph((char)c, rows))
		{
			for (i = 0; i < FONT_GLYPH_H; i++)
			{
				CHECK(rows[i] < 8);
				any |= rows[i];
			}
			CHECK(any != 0);
		}
	}
	CHECK(font_glyph(' ', rows) && rows[0] == 0 && rows[4] == 0);

	text_layer_init(&layer, pixels, 40, 24, 2);
	CHECK(layer.cols == 5 && layer.rows == 2);
	text_layer_clear(&layer);
	CHECK(text_layer_puts(&layer, 0, 0, "A") == 1);
	/* 'A' at scale 2: top row 010 lights pixels 2 and 3 of row 0 and 1 */
	CHECK(pixels[(0 * 40 + 2) * 4] == 255 && pixels[(0 * 40 + 3) * 4 + 3] == 255 && pixels[(1 * 40 + 2) * 4] == 255);
	CHECK(pixels[(0 * 40 + 0) * 4 + 3] == 0 && pixels[(0 * 40 + 4) * 4 + 3] == 0);
	for (i = 0; i < 40 * 24; i++)
		lit += pixels[i * 4 + 3] == 255;
	CHECK(lit == 10 * 4); /* the ten lit pixels of an A, each four pixels at scale 2 */
	/* cut off at the edge; a row past the bottom writes nothing */
	CHECK(text_layer_puts(&layer, 3, 1, "ABCDE") == 2);
	CHECK(text_layer_puts(&layer, 0, 2, "A") == 0);
	text_layer_clear(&layer);
	for (i = 0; i < 40 * 24; i++)
		CHECK(pixels[i * 4 + 3] == 0);
}

/* ---------- embedded shaders */

static void test_shaders(void)
{
	unsigned i, j;
	gsh_summary summary;
	uint8_t copy[256];

	for (i = 0; i < GSH_COUNT; i++)
	{
		const char *problem = gsh_check(gsh_files[i].data, gsh_files[i].size, &summary);

		if (problem)
			printf("gsh %s: %s\n", gsh_files[i].name, problem);
		CHECK(problem == NULL);
		CHECK(summary.major == 7 && summary.vertex_headers == 1 && summary.pixel_programs == 1);
		CHECK(summary.blocks == 5);
		CHECK(((unsigned long)(size_t)gsh_files[i].data & 63u) == 0);
	}
	CHECK(!strcmp(gsh_files[GSH_UI].name, "ui") && !strcmp(gsh_files[GSH_GENERATED].name, "generated"));

	/* the checker finds what is wrong */
	CHECK(gsh_check(gsh_files[GSH_UI].data, 20, &summary) != NULL);
	CHECK(gsh_check(gsh_files[GSH_UI].data, gsh_files[GSH_UI].size - 8, &summary) != NULL);
	CHECK(gsh_check(gsh_files[GSH_UI].data, gsh_files[GSH_UI].size - 32, &summary) != NULL);
	memcpy(copy, gsh_files[GSH_UI].data, sizeof(copy));
	copy[0] = 'X';
	CHECK(gsh_check(copy, gsh_files[GSH_UI].size, &summary) != NULL);

	for (i = 0; i < GLSL_SOURCE_COUNT; i++)
	{
		const char *text = glsl_sources[i].text;

		CHECK(strlen(text) > 100);
		CHECK(!strncmp(text, "#version ", 9));
		CHECK(strstr(text, "void main") != NULL);
		CHECK(glsl_sources[i].kind == GLSL_VERTEX ? strstr(text, "gl_Position") != NULL : strstr(text, "gl_Position") == NULL);
		for (j = i + 1; j < GLSL_SOURCE_COUNT; j++)
			CHECK(strcmp(glsl_sources[i].name, glsl_sources[j].name) != 0);
	}
	/* what the generator wrote is the ES dialect, the probe's own is desktop GLSL 330 */
	CHECK(!strncmp(glsl_sources[GLSL_GEN_VS00].text, "#version 300 es", 15));
	CHECK(!strncmp(glsl_sources[GLSL_GEN_PS_DIFFUSE].text, "#version 300 es", 15));
	CHECK(!strncmp(glsl_sources[GLSL_ATTR_VS].text, "#version 330", 12));
	CHECK(strstr(glsl_sources[GLSL_GEN_VS00_PACKED].text, "uint v2_packed") != NULL);
	CHECK(strstr(glsl_sources[GLSL_GEN_VS09_LIT].text, "xWorldNormal") != NULL);
	CHECK(glsl_sources[GLSL_GEN_VS00].kind == GLSL_VERTEX && glsl_sources[GLSL_GEN_PS_TEXT].kind == GLSL_PIXEL);
}

/* ---------- interface check, answers */

static void test_iface_and_answers(void)
{
	/* the numbers CafeGLSL gave the game's vs00 and the text pixel shader (tools: host inspection, 2026-10-18) */
	static const uint32_t vs[3] = { 0x8d8c8b8au, 0x91908f8eu, 0xffffff92u };
	static const uint32_t ps[2] = { 0x8cu, 0x8fu };
	static const uint32_t bad[2] = { 0x8cu, 0x93u };
	static const uint32_t ui_vs[1] = { 0xffff8b8au };
	static const uint32_t ui_ps[2] = { 0x8au, 0x8bu };
	int first;
	answers a;
	char storage[4096];
	report r;

	CHECK(iface_missing(vs, 3, ps, 2, &first) == 0 && first == -1);
	CHECK(iface_missing(vs, 3, bad, 2, &first) == 1 && first == 0x93);
	CHECK(iface_missing(ui_vs, 1, ui_ps, 2, &first) == 0);
	CHECK(iface_output_index(vs, 3, 0x8c) == 2 && iface_output_index(vs, 3, 0x92) == 8 && iface_output_index(vs, 3, 0xFF) == -1);
	CHECK(iface_output_index(ui_vs, 1, 0xFF) == -1 && iface_output_index(ui_vs, 1, 0x8b) == 1);

	answers_init(&a);
	answers_set(&a, "one", "%d", 1);
	answers_set(&a, "two", "second %s", "line");
	answers_set(&a, "one", "%d again", 2);
	CHECK(a.count == 2 && !strcmp(answers_get(&a, "one"), "2 again") && !strcmp(answers_get(&a, "two"), "second line"));
	CHECK(answers_get(&a, "three") == NULL);
	{
		char key[16];
		unsigned i;

		for (i = 0; i < ANSWERS_MAX + 3; i++)
		{
			snprintf(key, sizeof(key), "k%u", i);
			answers_set(&a, key, "x");
		}
		CHECK(a.count == ANSWERS_MAX && a.lost == 5);
	}
	report_init(&r, storage, sizeof(storage), NULL, NULL);
	answers_report(&a, &r);
	CHECK(strstr(storage, "[answers]\none = 2 again\ntwo = second line\n") == storage);
	CHECK(strstr(storage, "# 5 answers did not fit") != NULL);
}

int main(void)
{
	test_bc_pattern();
	test_bc_decoder();
	test_bc_orders();
	test_bc_explain();
	test_bc_invert();
	test_bc_variants();
	test_readback();
	test_vertex();
	test_index();
	test_scene();
	test_font();
	test_shaders();
	test_iface_and_answers();
	if (failures)
	{
		printf("%d checks failed\n", failures);
		return 1;
	}
	printf("all checks passed\n");
	return 0;
}
