/* The high-res text (port/linux/src/text_hires.c) on the machines the port runs on: the fonts embedded as the Wii U build embeds them (big-endian words,
tools/embed_assets.py --fonts-only) are read by stb_truetype, a font tag gets a font, its letters have glyphs, the atlas gets their coverage, and the rows
the Wii U's texture cache asks for are those that changed. */

#include "text_hires.h"

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

/* what text_hires.c takes from the rest of the port */
void platform_log(const char *format, ...)
{
	(void)format;
}

int config_boolean(const char *name)
{
	(void)name;
	return 1;
}

unsigned long config_changes(void)
{
	return 1;
}

float halo_screen_pixel_scale(void)
{
	return 2.25f;
}

int main(void)
{
	short widths[TEXT_HIRES_FIT_LETTERS];
	unsigned long placeholder[2] = { 0, 0x12345678 };
	struct text_hires_glyph glyph;
	const unsigned char *coverage;
	long size, top, bottom, font, row, column, total = 0;
	int i;

	for (i = 0; i < TEXT_HIRES_FIT_LETTERS; i++)
		widths[i] = 12;
	CHECK(text_hires_embedded_count >= 1);
	font = text_hires_font("ui\\large_ui", 14.0f, widths, 1.0f);
	CHECK(font >= 0);
	CHECK(text_hires_font("no\\such\\font", 14.0f, widths, 1.0f) < 0);
	CHECK(text_hires_covers(font, 'A'));
	CHECK(text_hires_covers(font, 'z'));
	text_hires_register_atlas(placeholder, 256, 256);
	CHECK(text_hires_glyph(font, 'A', &glyph));
	CHECK(glyph.right > glyph.left && glyph.bottom > glyph.top && glyph.advance > 0.0f);
	CHECK(glyph.u1 > glyph.u0 && glyph.v1 > glyph.v0);
	CHECK(text_hires_glyph(font, 'g', &glyph));
	/* the rows: everything the first time, then what changed, then nothing */
	coverage = text_hires_atlas_rows(0x12345678, &size, 1, &top, &bottom);
	CHECK(coverage && size == 2048 && top == 0 && bottom == 2048);
	CHECK(!text_hires_atlas_rows(0x1, &size, 1, &top, &bottom));
	coverage = text_hires_atlas_rows(0x12345678, &size, 0, &top, &bottom);
	CHECK(coverage && top >= bottom);
	CHECK(text_hires_glyph(font, 'W', &glyph));
	coverage = text_hires_atlas_rows(0x12345678, &size, 0, &top, &bottom);
	CHECK(coverage && top < bottom && bottom <= 2048);
	/* the glyphs drew something: the coverage has texels, not all of them full */
	coverage = text_hires_atlas_rows(0x12345678, &size, 1, &top, &bottom);
	for (row = 0; row < 64; row++)
		for (column = 0; column < 2048; column++)
			total += coverage[row * size + column];
	CHECK(total > 1000);
	if (failures)
	{
		printf("%d failed\n", failures);
		return 1;
	}
	printf("high-res text: all passed\n");
	return 0;
}
