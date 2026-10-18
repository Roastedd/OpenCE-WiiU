/*
The control stage: before anything is judged by reading the target back, check the reading. A clear color comes back with
its channels in the order the bytes of the target have (and the same everywhere in the buffer); an RGBA8 texture drawn
1:1 comes back as it was uploaded, right way up. These are the pictures the BC1 and BC3 tests are compared with, and
the order the vertex and index tests read their pixels in.
*/

#include "stages.h"

#include <stdio.h>
#include <string.h>

static gx_texture reference[BC_FORMAT_COUNT];
static uint8_t expected_image[BC_FORMAT_COUNT][BC_TEXTURE_SIZE * BC_TEXTURE_SIZE * 4];

const GX2Texture *control_reference(bc_format format)
{
	return reference[format].ok ? &reference[format].texture : NULL;
}

const uint8_t *control_expected(bc_format format)
{
	return expected_image[format];
}

static void draw_clear(unsigned index, float width, float height)
{
	(void)index;
	tile_background(width, height);
}

static void draw_reference(unsigned index, float width, float height)
{
	if (reference[index].ok)
	{
		draw_texture_fill(session_current(), &reference[index].texture, width, height);
		ui_flush();
	}
}

static void flip_rows(const uint8_t *in, uint8_t *out)
{
	unsigned y;

	for (y = 0; y < BC_TEXTURE_SIZE; y++)
		memcpy(out + (size_t)y * BC_TEXTURE_SIZE * 4, in + (size_t)(BC_TEXTURE_SIZE - 1 - y) * BC_TEXTURE_SIZE * 4, (size_t)BC_TEXTURE_SIZE * 4);
}

void stage_control(gx_session *s)
{
	report *r = s->report;
	static uint8_t flipped[BC_TEXTURE_SIZE * BC_TEXTURE_SIZE * 4];
	static uint8_t uniform[SCENE_SIZE * SCENE_SIZE * 4];
	char error[200];
	readback_compare result;
	uint8_t found[4];
	unsigned i, f;
	page *p;
	const uint8_t *pixels;

	report_section(r, "control");
	if (!s->ready)
	{
		report_note(r, "skipped: the system stage did not finish");
		return;
	}

	/* 1. a clear color, read back */
	ui_frame();
	gx_target_begin(&s->scene);
	gx_target_clear(&s->scene, scene_clear_rgba);
	gx_target_finish(&s->scene);
	pixels = gx_target_pixels(&s->scene);
	memcpy(found, pixels, 4);
	report_value(r, "clear color written", "%s (rgba)", pixel_text(scene_clear_rgba));
	report_value(r, "bytes found at pixel 0", "%u,%u,%u,%u", found[0], found[1], found[2], found[3]);
	if (readback_detect_order(found, scene_clear_rgba, &s->order))
	{
		report_value(r, "target byte order", "r from byte %u, g from byte %u, b from byte %u, a from byte %u%s", s->order.source[0], s->order.source[1],
			s->order.source[2], s->order.source[3],
			(s->order.source[0] == 0 && s->order.source[1] == 1 && s->order.source[2] == 2 && s->order.source[3] == 3) ? " (R, G, B, A in memory)" : "");
		for (i = 0; i < SCENE_SIZE * SCENE_SIZE; i++)
			memcpy(uniform + (size_t)i * 4, scene_clear_rgba, 4);
		readback_compare_image(pixels, s->scene.pitch_bytes, SCENE_SIZE, SCENE_SIZE, &s->order, uniform, 0, &result);
		s->readback_ok = result.mismatches == 0;
		report_check(r, s->readback_ok, "clear read back", "%u of %u pixels differ from the clear color", result.mismatches, result.pixels);
	}
	else
	{
		s->readback_ok = 0;
		report_check(r, 0, "clear read back", "the four bytes are not the four channels in any order: the CPU does not see what the GPU wrote (cache or tile mode?)");
	}
	answers_set(s->answers, "readback", "%s", s->readback_ok ? "works: a linear RGBA8 color buffer read by the CPU after GX2DrawDone" : "DOES NOT WORK: numbers below are not trustworthy");
	if (s->flush)
		s->flush();

	/* 2. an RGBA8 texture, drawn 1:1 */
	for (f = 0; f < BC_FORMAT_COUNT; f++)
	{
		bc_pattern_image((bc_format)f, expected_image[f]);
		if (!gx_texture_create(&reference[f], BC_TEXTURE_SIZE, BC_TEXTURE_SIZE, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, 4, 0, error, sizeof(error)))
		{
			report_check(r, 0, "reference texture", "%s: %s", bc_format_name((bc_format)f), error);
			continue;
		}
		gx_texture_upload(&reference[f], expected_image[f], (size_t)BC_TEXTURE_SIZE * 4);
	}
	if (reference[BC_FORMAT_BC1].ok)
	{
		scene_begin(s);
		draw_texture_fill(s, &reference[BC_FORMAT_BC1].texture, (float)SCENE_SIZE, (float)SCENE_SIZE);
		scene_end(s);
		readback_compare_image(gx_target_pixels(&s->scene), s->scene.pitch_bytes, SCENE_SIZE, SCENE_SIZE, &s->order, expected_image[BC_FORMAT_BC1], 2, &result);
		s->texture_ok = result.mismatches == 0;
		report_check(r, s->texture_ok, "RGBA8 texture drawn 1:1", "%u of %u pixels differ by more than 2; largest difference %u", result.mismatches, result.pixels,
			result.max_error);
		if (!s->texture_ok)
		{
			report_note(r, "first difference at (%u,%u): found %s, expected %s", result.first_x, result.first_y, pixel_text(result.first_found),
				pixel_text(result.first_expected));
			flip_rows(expected_image[BC_FORMAT_BC1], flipped);
			readback_compare_image(gx_target_pixels(&s->scene), s->scene.pitch_bytes, SCENE_SIZE, SCENE_SIZE, &s->order, flipped, 2, &result);
			if (result.mismatches == 0)
			{
				s->rows_flipped = 1;
				s->texture_ok = 1;
				report_note(r, "the picture is upside down: the texture's first row is drawn at the bottom; later draws flip v");
			}
		}
		answers_set(s->answers, "texture.rgba8", "%s", s->texture_ok ? (s->rows_flipped ? "works, rows upside down (v is flipped)" : "works: sampled 1:1, right way up") : "DOES NOT draw as uploaded");
	}
	if (s->flush)
		s->flush();

	p = session_new_page(s, "controls");
	if (!p)
		return;
	page_wrap(p, 1, 2, 100, STYLE_NORMAL, "Before anything is judged by reading the target back, the reading is checked: the clear color, and an RGBA8 texture drawn 1:1.");
	page_wrap(p, 1, 4, 100, s->readback_ok ? STYLE_GOOD : STYLE_BAD, "clear color %s read back as bytes %u,%u,%u,%u: %s", pixel_text(scene_clear_rgba), found[0],
		found[1], found[2], found[3], s->readback_ok ? "channel order found, the whole target agrees" : "NOT READABLE");
	page_wrap(p, 1, 6, 100, s->texture_ok ? STYLE_GOOD : STYLE_BAD, "an RGBA8 texture drawn 1:1 into the 64x64 target: %s",
		s->texture_ok ? (s->rows_flipped ? "same, upside down" : "same, right way up") : "DIFFERENT");
	page_wrap(p, 1, 8, 100, STYLE_DIM, "Below: the clear color, then the BC1, BC2 and BC3 patterns decoded by the CPU and drawn as RGBA8 textures; the BC tests are compared with them.");
	page_tile_add(p, 4, 12, 16, 11, draw_clear, 0, STYLE_DIM);
	page_text(p, 4, 24, STYLE_NORMAL, "clear color");
	page_tile_add(p, 28, 12, 16, 11, draw_reference, 0, s->texture_ok ? STYLE_GOOD : STYLE_BAD);
	page_text(p, 28, 24, STYLE_NORMAL, "BC1 pattern as RGBA8");
	page_tile_add(p, 52, 12, 16, 11, draw_reference, 1, s->texture_ok ? STYLE_GOOD : STYLE_BAD);
	page_text(p, 52, 24, STYLE_NORMAL, "BC2 pattern as RGBA8");
	page_tile_add(p, 76, 12, 16, 11, draw_reference, 2, s->texture_ok ? STYLE_GOOD : STYLE_BAD);
	page_text(p, 76, 24, STYLE_NORMAL, "BC3 pattern as RGBA8");
}
