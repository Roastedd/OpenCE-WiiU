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
	for (f = 0; f < BC_FORMAT_COUNT && s->keep_going(); f++)
		test_format(s, (bc_format)f, p, 5 + f * 10);
}
