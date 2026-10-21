/*
The texture stage: BC1 and BC3 (the Xbox's DXT1 and DXT5) uploaded in every plausible byte order of the block, drawn 1:1
into the 64 x 64 target, read back and compared with the pattern decoded on the CPU. The order that draws right is the
byte order the GPU reads these blocks in. Independently of the variants tried, bc_explain finds which rearrangement of the
bytes of a block explains what the GPU drew from the upload "as it is" (all 40320 for BC1), so that an order none of the
variants has is still found, and then drawn as one more variant to confirm it.

Tiling is linear (aligned) here. The byte order inside a block does not depend on how blocks are laid out.
*/

#include "stages.h"

#include <stdio.h>
#include <string.h>

#define REFERENCE_TILE 100u
#define VARIANTS (BC_ORDER_COUNT + 1) /* the orders, then the one bc_explain derived */

typedef struct variant
{
	gx_texture texture;
	char name[40];
	int built;
	int right;
	readback_compare compare;
} variant;

static variant variants[BC_FORMAT_COUNT][VARIANTS];

static void draw_tile(unsigned index, float width, float height)
{
	gx_session *s = session_current();

	if (index >= REFERENCE_TILE)
	{
		const GX2Texture *reference = control_reference((bc_format)(index - REFERENCE_TILE));

		if (reference)
			draw_texture_fill(s, reference, width, height);
	}
	else if (variants[index / VARIANTS][index % VARIANTS].built)
	{
		draw_texture_fill(s, &variants[index / VARIANTS][index % VARIANTS].texture.texture, width, height);
	}
	ui_flush();
}

/* draws the variant over the whole target and compares what the CPU finds with the pattern */
static void draw_and_compare(gx_session *s, variant *v, bc_format format)
{
	scene_begin(s);
	draw_texture_fill(s, &v->texture.texture, (float)SCENE_SIZE, (float)SCENE_SIZE);
	scene_end(s);
	readback_compare_image(gx_target_pixels(&s->scene), s->scene.pitch_bytes, SCENE_SIZE, SCENE_SIZE, &s->order, control_expected(format), 6, &v->compare);
	/* a decoder rounds its interpolated colors as it likes; a wrong byte order is wrong in nearly every pixel */
	v->right = v->compare.mismatches * 20 <= v->compare.pixels;
}

static int make_variant(variant *v, bc_format format, const uint8_t *upload, char *error, size_t error_size)
{
	if (!gx_texture_create(&v->texture, BC_TEXTURE_SIZE, BC_TEXTURE_SIZE,
		format == BC_FORMAT_BC3 ? GX2_SURFACE_FORMAT_UNORM_BC3 : format == BC_FORMAT_BC2 ? GX2_SURFACE_FORMAT_UNORM_BC2 : GX2_SURFACE_FORMAT_UNORM_BC1,
		(unsigned)bc_block_bytes(format), 1, error, error_size))
		return 0;
	gx_texture_upload(&v->texture, upload, BC_BLOCKS * bc_block_bytes(format));
	v->built = 1;
	return 1;
}

/* the 16 blocks bc_explain looks at, as the target shows them now */
static void capture_samples(const gx_session *s, uint8_t observed[BC_EXPLAIN_SAMPLES][16][4])
{
	unsigned sample, t;

	for (sample = 0; sample < BC_EXPLAIN_SAMPLES; sample++)
	{
		unsigned bx, by;

		bc_explain_block(sample, &bx, &by);
		for (t = 0; t < 16; t++)
			session_pixel(s, bx * 4 + (t & 3), by * 4 + (t >> 2), observed[sample][t]);
	}
}

/* The Latte's 1D tiled thin layout, made on the CPU: the blocks in tiles of 8 x 8, each tile in the order of its pixel bits (x0 y0 x1 x2 y1 y2 for
8 byte blocks, x0 y0 x1 y1 x2 y2 for 16 byte ones, the AddrLib's non displayable order), the tiles in rows across the pitch. */
/* order[i] says which pixel bit goes to bit i of the index: 0, 1, 2 are x0, x1, x2 and 3, 4, 5 are y0, y1, y2 */
static const uint8_t order_64[6] = { 0, 3, 1, 2, 4, 5 };   /* x0 y0 x1 x2 y1 y2 */
static const uint8_t order_128[6] = { 3, 0, 1, 2, 4, 5 };  /* y0 x0 x1 x2 y1 y2: found on the console by the search (the only one of 720) */

static unsigned micro_index(unsigned x, unsigned y, const uint8_t *order)
{
	unsigned bits[6] = { x & 1u, (x >> 1) & 1u, (x >> 2) & 1u, y & 1u, (y >> 1) & 1u, (y >> 2) & 1u }, index = 0, i;

	for (i = 0; i < 6; i++)
		index |= bits[order[i]] << i;
	return index;
}

static void tile_1d(const uint8_t *tight, unsigned blocks_across, unsigned blocks_down, unsigned block_bytes, unsigned pitch_blocks, const uint8_t *order,
	uint8_t *out)
{
	unsigned x, y;

	for (y = 0; y < blocks_down; y++)
		for (x = 0; x < blocks_across; x++)
		{
			size_t tile = (size_t)(y / 8) * (pitch_blocks / 8) + (x / 8);
			size_t at = (tile * 64 + micro_index(x % 8, y % 8, order)) * block_bytes;

			memcpy(out + at, tight + ((size_t)y * blocks_across + x) * block_bytes, block_bytes);
		}
}

/* a texture of the tiled mode, with the blocks of tight (rows of blocks, no padding) put in it by tile_1d */
static int make_cpu_tiled(variant *v, bc_format format, const uint8_t *tight, const uint8_t *order, char *error, size_t error_size)
{
	GX2Surface *d = &v->texture.texture.surface;
	unsigned bytes = (unsigned)bc_block_bytes(format);

	memset(v, 0, sizeof(*v));
	d->dim = GX2_SURFACE_DIM_TEXTURE_2D;
	d->width = BC_TEXTURE_SIZE;
	d->height = BC_TEXTURE_SIZE;
	d->depth = 1;
	d->mipLevels = 1;
	d->format = format == BC_FORMAT_BC3 ? GX2_SURFACE_FORMAT_UNORM_BC3 : format == BC_FORMAT_BC2 ? GX2_SURFACE_FORMAT_UNORM_BC2 : GX2_SURFACE_FORMAT_UNORM_BC1;
	d->aa = GX2_AA_MODE1X;
	d->use = GX2_SURFACE_USE_TEXTURE;
	d->tileMode = GX2_TILE_MODE_TILED_1D_THIN1;
	v->texture.texture.viewNumMips = 1;
	v->texture.texture.viewNumSlices = 1;
	v->texture.texture.compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
	GX2CalcSurfaceSizeAndAlignment(d);
	if (d->imageSize < (size_t)BC_BLOCKS * BC_BLOCKS * bytes || d->pitch < BC_BLOCKS)
	{
		snprintf(error, error_size, "tiled surface: pitch %u, image %u bytes", (unsigned)d->pitch, (unsigned)d->imageSize);
		return 0;
	}
	d->image = gx_alloc(d->imageSize, d->alignment ? d->alignment : 256);
	if (!d->image)
	{
		snprintf(error, error_size, "no memory for %u bytes", (unsigned)d->imageSize);
		return 0;
	}
	tile_1d(tight, BC_BLOCKS, BC_BLOCKS, bytes, d->pitch, order, (uint8_t *)d->image);
	GX2InitTextureRegs(&v->texture.texture);
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, d->image, d->imageSize);
	return 1;
}

/* All 720 orders of the pixel bits, for a format whose order is not known: the ones that draw the pattern right (there should be one). */
static void search_order(gx_session *s, bc_format format, const uint8_t *tight)
{
	uint8_t order[6];
	unsigned n, i, j, found = 0;
	char error[160], text[160] = "";
	variant v;

	for (n = 0; n < 720 && s->keep_going(); n++)
	{
		unsigned code = n, left[6] = { 0, 1, 2, 3, 4, 5 }, count = 6;

		for (i = 0; i < 6; i++)
		{
			unsigned pick = code % count;

			code /= count;
			order[i] = (uint8_t)left[pick];
			for (j = pick; j + 1 < count; j++)
				left[j] = left[j + 1];
			count--;
		}
		if (!make_cpu_tiled(&v, format, tight, order, error, sizeof(error)))
			return;
		draw_and_compare(s, &v, format);
		if (v.right && found < 4)
		{
			size_t used = strlen(text);

			snprintf(text + used, sizeof(text) - used, " [%u %u %u %u %u %u]", order[0], order[1], order[2], order[3], order[4], order[5]);
			found++;
		}
		gx_free(v.texture.texture.surface.image);
	}
	report_value(s->report, "texture order search", "%s: %u orders draw right (bit i of the index comes from 0 1 2 = x0 x1 x2, 3 4 5 = y0 y1 y2):%s", bc_format_name(format), found, text);
}

/* every format drawn from CPU-tiled data, compared with the pattern; BC1 also with a color for each block */
static void test_cpu_tiled(gx_session *s)
{
	static uint8_t upload[BC_BLOCKS * BC_BLOCKS * 16];
	char error[160], text[200];
	unsigned f, bx, by, right;
	variant v;

	for (f = 0; f < BC_FORMAT_COUNT && s->keep_going(); f++)
	{
		bc_pattern_upload((bc_format)f, BC_ORDER_AS_IS, upload);
		if (!make_cpu_tiled(&v, (bc_format)f, upload, f == BC_FORMAT_BC1 ? order_64 : order_128, error, sizeof(error)))
		{
			report_value(s->report, "texture cpu-tiled", "%s: %s", bc_format_name((bc_format)f), error);
			continue;
		}
		draw_and_compare(s, &v, (bc_format)f);
		report_value(s->report, "texture cpu-tiled", "%s 1D tiled by the CPU (pitch %u, image %u bytes): %u of %u pixels differ by more than 6 (largest %u)%s",
			bc_format_name((bc_format)f), (unsigned)v.texture.texture.surface.pitch, (unsigned)v.texture.texture.surface.imageSize, v.compare.mismatches,
			v.compare.pixels, v.compare.max_error, v.right ? " -> DRAWS RIGHT" : "");
		if (v.compare.mismatches)
			report_value(s->report, "texture cpu-tiled first wrong", "%s: pixel (%u,%u) is %u,%u,%u,%u; expected %u,%u,%u,%u", bc_format_name((bc_format)f),
				v.compare.first_x, v.compare.first_y, v.compare.first_found[0], v.compare.first_found[1], v.compare.first_found[2], v.compare.first_found[3],
				v.compare.first_expected[0], v.compare.first_expected[1], v.compare.first_expected[2], v.compare.first_expected[3]);
		gx_free(v.texture.texture.surface.image);
		if (f != BC_FORMAT_BC1 && !v.right)
			search_order(s, (bc_format)f, upload);
	}
	/* a color for each BC1 block */
	for (by = 0; by < BC_BLOCKS; by++)
		for (bx = 0; bx < BC_BLOCKS; bx++)
		{
			unsigned color = (((bx * 2u + 1u) & 31u) << 11) | (((by * 4u + 1u) & 63u) << 5);
			uint8_t *block = upload + (by * BC_BLOCKS + bx) * 8;

			block[0] = (uint8_t)color;
			block[1] = (uint8_t)(color >> 8);
			block[2] = block[0];
			block[3] = block[1];
			memset(block + 4, 0, 4);
		}
	if (make_cpu_tiled(&v, BC_FORMAT_BC1, upload, order_64, error, sizeof(error)))
	{
		scene_begin(s);
		draw_texture_fill(s, &v.texture.texture, (float)SCENE_SIZE, (float)SCENE_SIZE);
		scene_end(s);
		right = 0;
		for (by = 0; by < BC_BLOCKS; by++)
			for (bx = 0; bx < BC_BLOCKS; bx++)
			{
				uint8_t px[4];

				session_pixel(s, bx * 4 + 1, by * 4 + 1, px);
				if ((px[0] * 31u + 127u) / 255u / 2u == bx && (px[1] * 63u + 127u) / 255u / 4u == by)
					right++;
			}
		snprintf(text, sizeof(text), "%u of %u blocks read as written", right, BC_BLOCKS * BC_BLOCKS);
		report_value(s->report, "texture cpu-tiled blocks", "BC1, a color for each block: %s", text);
		gx_free(v.texture.texture.surface.image);
	}
}

/* The same upload as it is, copied by the GPU (GX2CopySurface) into a surface of a tiled mode, drawn and compared: if the linear
surface does not draw right, a compressed texture may need to be tiled. */
static void try_tiled(gx_session *s, bc_format format, const variant *linear, GX2TileMode mode, const char *mode_name)
{
	report *r = s->report;
	variant tiled;
	GX2Surface *d = &tiled.texture.texture.surface;

	memset(&tiled, 0, sizeof(tiled));
	tiled.texture.texture = linear->texture.texture;
	d->tileMode = mode;
	d->image = NULL;
	d->mipmaps = NULL;
	d->imageSize = 0;
	d->pitch = 0;
	d->alignment = 0;
	GX2CalcSurfaceSizeAndAlignment(d);
	d->image = gx_alloc(d->imageSize, d->alignment ? d->alignment : 256);
	if (!d->image)
	{
		report_value(r, "texture tiled", "%s %s: no memory for %u bytes", bc_format_name(format), mode_name, (unsigned)d->imageSize);
		return;
	}
	GX2InitTextureRegs(&tiled.texture.texture);
	GX2CopySurface((GX2Surface *)&linear->texture.texture.surface, 0, 0, d, 0, 0);
	GX2DrawDone();
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, d->image, d->imageSize);
	draw_and_compare(s, &tiled, format);
	report_value(r, "texture tiled", "%s as it is, copied to %s (image %u bytes, pitch %u): %u of %u pixels differ by more than 6 (largest difference %u)%s",
		bc_format_name(format), mode_name, (unsigned)d->imageSize, (unsigned)d->pitch, tiled.compare.mismatches, tiled.compare.pixels,
		tiled.compare.max_error, tiled.right ? " -> DRAWS RIGHT" : "");
	if (tiled.compare.mismatches)
		report_value(r, "texture tiled first wrong", "%s %s: pixel (%u,%u) is %u,%u,%u,%u; expected %u,%u,%u,%u", bc_format_name(format), mode_name,
			tiled.compare.first_x, tiled.compare.first_y, tiled.compare.first_found[0], tiled.compare.first_found[1], tiled.compare.first_found[2],
			tiled.compare.first_found[3], tiled.compare.first_expected[0], tiled.compare.first_expected[1], tiled.compare.first_expected[2],
			tiled.compare.first_expected[3]);
	gx_free(d->image);
}

/* What does the GPU make of one block that is a single color? BC1 blocks of red (color0 0xF800) with color1 blue (0x001F) and every
index 0, uploaded as a DDS file has them and with the 16 and 32 bit halves swapped, drawn linear and copied to tiled surfaces. Four pixels
of what is drawn say which byte order the GPU reads a color in, and whether it reads the surface at all. */
static void solid_samples(gx_session *s, const char *what)
{
	static const unsigned at[4][2] = { { 0, 0 }, { 13, 2 }, { 5, 9 }, { 63, 63 } };
	char text[160] = "";
	unsigned i;

	for (i = 0; i < 4; i++)
	{
		uint8_t px[4];
		size_t used = strlen(text);

		session_pixel(s, at[i][0], at[i][1], px);
		snprintf(text + used, sizeof(text) - used, "%s(%u,%u)=%u,%u,%u,%u", i ? " " : "", at[i][0], at[i][1], px[0], px[1], px[2], px[3]);
	}
	report_value(s->report, "texture solid", "%s: %s", what, text);
}

static void test_solid(gx_session *s)
{
	static const uint8_t block_as_dds[8] = { 0x00, 0xF8, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00 };
	static const char *const names[3] = { "as it is (a DDS file's bytes)", "16-bit halves swapped", "32-bit words swapped" };
	static uint8_t upload[BC_BLOCKS * BC_BLOCKS * 8];
	char error[160], what[96];
	unsigned order, b, k;
	variant v;

	for (order = 0; order < 3; order++)
	{
		uint8_t block[8];

		for (k = 0; k < 8; k++)
			block[k] = order == 0 ? block_as_dds[k] : order == 1 ? block_as_dds[k ^ 1] : block_as_dds[k ^ 3];
		for (b = 0; b < BC_BLOCKS * BC_BLOCKS; b++)
			memcpy(upload + b * 8, block, 8);
		memset(&v, 0, sizeof(v));
		if (!make_variant(&v, BC_FORMAT_BC1, upload, error, sizeof(error)))
		{
			report_value(s->report, "texture solid", "BC1 %s: %s", names[order], error);
			continue;
		}
		scene_begin(s);
		draw_texture_fill(s, &v.texture.texture, (float)SCENE_SIZE, (float)SCENE_SIZE);
		scene_end(s);
		snprintf(what, sizeof(what), "BC1 red/blue block, linear, %s", names[order]);
		solid_samples(s, what);
		if (order == 0)
		{
			GX2TileMode modes[2] = { GX2_TILE_MODE_TILED_1D_THIN1, GX2_TILE_MODE_TILED_2D_THIN1 };
			const char *mode_names[2] = { "1D_TILED_THIN1", "2D_TILED_THIN1" };

			for (k = 0; k < 2; k++)
			{
				variant tiled;
				GX2Surface *d = &tiled.texture.texture.surface;

				memset(&tiled, 0, sizeof(tiled));
				tiled.texture.texture = v.texture.texture;
				d->tileMode = modes[k];
				d->image = NULL;
				d->mipmaps = NULL;
				d->imageSize = 0;
				d->pitch = 0;
				d->alignment = 0;
				GX2CalcSurfaceSizeAndAlignment(d);
				d->image = gx_alloc(d->imageSize, d->alignment ? d->alignment : 256);
				if (!d->image)
					continue;
				GX2InitTextureRegs(&tiled.texture.texture);
				GX2CopySurface(&v.texture.texture.surface, 0, 0, d, 0, 0);
				GX2DrawDone();
				GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, d->image, d->imageSize);
				scene_begin(s);
				draw_texture_fill(s, &tiled.texture.texture, (float)SCENE_SIZE, (float)SCENE_SIZE);
				scene_end(s);
				snprintf(what, sizeof(what), "BC1 red/blue block, copied to %s", mode_names[k]);
				solid_samples(s, what);
				gx_free(d->image);
			}
		}
		gx_texture_free(&v.texture);
	}
}

/* Every BC1 block a flat color of its own (the block's x in red, y in green), drawn 1:1: the picture says which block the GPU read
for each place, if it is not the block there. */
static void test_block_map(gx_session *s, unsigned row_bytes_override)
{
	static uint8_t upload[BC_BLOCKS * BC_BLOCKS * 8];
	char error[160], text[200];
	unsigned bx, by, right = 0;
	variant v;

	(void)row_bytes_override;
	for (by = 0; by < BC_BLOCKS; by++)
		for (bx = 0; bx < BC_BLOCKS; bx++)
		{
			unsigned color = (((bx * 2u + 1u) & 31u) << 11) | (((by * 4u + 1u) & 63u) << 5); /* RGB565, never black */
			uint8_t *block = upload + (by * BC_BLOCKS + bx) * 8;

			block[0] = (uint8_t)color;
			block[1] = (uint8_t)(color >> 8);
			block[2] = block[0];
			block[3] = block[1];
			memset(block + 4, 0, 4);
		}
	memset(&v, 0, sizeof(v));
	if (!make_variant(&v, BC_FORMAT_BC1, upload, error, sizeof(error)))
	{
		report_value(s->report, "texture blocks", "%s", error);
		return;
	}
	scene_begin(s);
	draw_texture_fill(s, &v.texture.texture, (float)SCENE_SIZE, (float)SCENE_SIZE);
	scene_end(s);
	text[0] = 0;
	for (by = 0; by < BC_BLOCKS; by++)
	{
		for (bx = 0; bx < BC_BLOCKS; bx++)
		{
			uint8_t px[4];
			unsigned seen_x, seen_y;
			size_t used = strlen(text);

			session_pixel(s, bx * 4 + 1, by * 4 + 1, px);
			seen_x = (px[0] * 31u + 127u) / 255u / 2u;
			seen_y = (px[1] * 63u + 127u) / 255u / 4u;
			if (px[0] == 0 && px[1] == 0)
				snprintf(text + used, sizeof(text) - used, " --");
			else
				snprintf(text + used, sizeof(text) - used, " %X%X", seen_x & 15u, seen_y & 15u);
			if (seen_x == bx && seen_y == by)
				right++;
		}
		report_value(s->report, "texture blocks row", "%2u:%s", by, text);
		text[0] = 0;
	}
	report_value(s->report, "texture blocks", "BC1, a color for each block, as it is: %u of %u blocks read as written (rows above: for each place, xy of the block whose color it shows, -- = black)", right,
		BC_BLOCKS * BC_BLOCKS);
	gx_texture_free(&v.texture);
}

static void test_format(gx_session *s, bc_format format, page *p, unsigned row)
{
	report *r = s->report;
	static const char *const keys[BC_FORMAT_COUNT] = { "texture.bc1", "texture.bc2", "texture.bc3" };
	static uint8_t upload[BC_BLOCKS * BC_BLOCKS * 16];
	static uint8_t derived[BC_BLOCKS * BC_BLOCKS * 16];
	static uint8_t observed[BC_EXPLAIN_SAMPLES][16][4];
	char error[200], gpu_name[64], upload_name[64], right_names[96], text[256];
	unsigned o, tile = 1, count = 0;
	size_t bytes = bc_block_bytes(format), i;
	bc_permutation found, inverse;
	int explained = 0, derived_right = -1, any_right = 0;

	right_names[0] = 0;
	if (p)
	{
		page_tile_add(p, 1, row, 12, 8, draw_tile, REFERENCE_TILE + (unsigned)format, s->texture_ok ? STYLE_GOOD : STYLE_DIM);
		page_text(p, 1, row + 8, STYLE_NORMAL, "%s expected", bc_format_name(format));
		page_text(p, 1, row + 9, STYLE_DIM, "CPU decoded");
	}
	for (o = 0; o < BC_ORDER_COUNT; o++)
	{
		variant *v = &variants[format][o];

		if (!bc_order_distinct(format, (bc_order)o))
			continue;
		snprintf(v->name, sizeof(v->name), "%s", bc_order_name((bc_order)o));
		bc_pattern_upload(format, (bc_order)o, upload);
		if (!make_variant(v, format, upload, error, sizeof(error)))
		{
			report_check(r, 0, "texture", "%s upload %s: %s", bc_format_name(format), v->name, error);
			continue;
		}
		if (o == BC_ORDER_AS_IS)
			report_value(r, "texture layout", "%s: pitch %u texels, %u bytes a row of blocks, image %u bytes, alignment %u", bc_format_name(format),
				(unsigned)v->texture.texture.surface.pitch, v->texture.row_bytes, (unsigned)v->texture.texture.surface.imageSize,
				(unsigned)v->texture.texture.surface.alignment);
		draw_and_compare(s, v, format);
		if (o == BC_ORDER_AS_IS)
			capture_samples(s, observed);
		report_value(r, "texture", "%s upload %s: %u of %u pixels differ by more than 6 (largest difference %u)%s", bc_format_name(format), v->name,
			v->compare.mismatches, v->compare.pixels, v->compare.max_error, v->right ? " -> DRAWS RIGHT" : "");
		if (v->compare.mismatches)
			report_value(r, "texture first wrong", "%s upload %s: pixel (%u,%u) is %u,%u,%u,%u; expected %u,%u,%u,%u", bc_format_name(format), v->name,
				v->compare.first_x, v->compare.first_y, v->compare.first_found[0], v->compare.first_found[1], v->compare.first_found[2],
				v->compare.first_found[3], v->compare.first_expected[0], v->compare.first_expected[1], v->compare.first_expected[2],
				v->compare.first_expected[3]);
		if (v->right)
		{
			size_t used = strlen(right_names);

			snprintf(right_names + used, sizeof(right_names) - used, "%s%s", used ? ", " : "", v->name);
			any_right = 1;
		}
		if (p && tile < 6)
		{
			unsigned col = 1 + tile * 14;

			page_tile_add(p, col, row, 12, 8, draw_tile, (unsigned)format * VARIANTS + o, v->right ? STYLE_GOOD : STYLE_DIM);
			page_text(p, col, row + 8, STYLE_NORMAL, "%s", v->name);
			if (v->right)
				page_text(p, col, row + 9, STYLE_GOOD, "draws right");
			else
				page_text(p, col, row + 9, STYLE_DIM, "%u wrong", v->compare.mismatches);
			tile++;
		}
		if (!s->keep_going())
			return;
	}

	if (variants[format][BC_ORDER_AS_IS].built)
	{
		try_tiled(s, format, &variants[format][BC_ORDER_AS_IS], GX2_TILE_MODE_TILED_1D_THIN1, "1D_TILED_THIN1");
		try_tiled(s, format, &variants[format][BC_ORDER_AS_IS], GX2_TILE_MODE_TILED_2D_THIN1, "2D_TILED_THIN1");
	}

	/* what rearrangement of the bytes explains the drawing of the upload as it is? */
	if (variants[format][BC_ORDER_AS_IS].built)
	{
		count = bc_explain(format, observed, 6, &found);
		explained = count > 0;
		if (explained)
		{
			bc_permutation_name(format, &found, gpu_name, sizeof(gpu_name));
			memset(&inverse, 0, sizeof(inverse));
			for (i = 0; i < bytes; i++)
				inverse.source[found.source[i]] = (uint8_t)i;
			bc_permutation_name(format, &inverse, upload_name, sizeof(upload_name));
			report_value(r, "texture explained", "%s: %u byte order%s explain%s the drawing of the upload as it is; the GPU reads each block as: %s; to give it the Xbox's block upload: %s",
				bc_format_name(format), count, count == 1 ? "" : "s", count == 1 ? "s" : "", gpu_name, upload_name);
			if (!any_right && count == 1)
			{
				/* not one of the orders tried: upload what the derived rearrangement asks for, and see */
				variant *v = &variants[format][BC_ORDER_COUNT];
				unsigned b;

				for (b = 0; b < BC_BLOCKS * BC_BLOCKS; b++)
				{
					uint8_t block[16];

					bc_pattern_block(format, b % BC_BLOCKS, b / BC_BLOCKS, block);
					bc_permutation_invert_block(format, &found, block, derived + b * bytes);
				}
				snprintf(v->name, sizeof(v->name), "derived");
				if (make_variant(v, format, derived, error, sizeof(error)))
				{
					draw_and_compare(s, v, format);
					derived_right = v->right;
					report_check(r, v->right, "texture derived order", "%s upload %s: %u of %u pixels differ", bc_format_name(format), upload_name,
						v->compare.mismatches, v->compare.pixels);
				}
			}
		}
		else
		{
			report_value(r, "texture explained", "%s: no rearrangement of the bytes of a block explains what the GPU drew from the upload as it is (a flipped or empty picture?)",
				bc_format_name(format));
		}
	}

	if (any_right)
		snprintf(text, sizeof(text), "draws right when uploaded: %s; the GPU reads a block as: %s", right_names, explained ? gpu_name : "(not explained)");
	else if (explained)
		snprintf(text, sizeof(text), "none of the orders tried draws right; the GPU reads a block as: %s (upload %s: %s)", gpu_name, upload_name,
			derived_right < 0 ? "not tried" : derived_right ? "draws right" : "does not");
	else
		snprintf(text, sizeof(text), "NOTHING DRAWS RIGHT and no byte order explains it: see the report");
	answers_set(s->answers, keys[format], "%s", text);
}

void stage_textures(gx_session *s)
{
	report *r = s->report;
	page *p;
	unsigned f;

	report_section(r, "textures");
	if (!s->ready || !s->readback_ok)
	{
		report_note(r, "skipped: %s", !s->ready ? "the system stage did not finish" : "the target cannot be read back (see control)");
		return;
	}
	if (!s->texture_ok)
		report_note(r, "the RGBA8 control did not pass: what follows is not trustworthy");
	p = session_new_page(s, "BC1, BC2 and BC3 byte order");
	if (p)
	{
		page_wrap(p, 1, 2, 100, STYLE_NORMAL, "A 64x64 pattern of 16x16 blocks uploaded with each block's bytes in each order, drawn 1:1. First tile: what it should look like. Green frame: the order that draws right.");
		page_text(p, 1, 4, STYLE_DIM, "The label says how the Xbox's block was rearranged. Numbers: report [textures].");
	}
	if (s->keep_going())
	{
		test_solid(s);
		test_block_map(s, 0);
		test_cpu_tiled(s);
	}
	for (f = 0; f < BC_FORMAT_COUNT && s->keep_going(); f++)
		test_format(s, (bc_format)f, p, 5 + f * 10);
}
