#include "harness.h"

/* The GamePad's chat keyboard (port/wiiu/src/wiiu_keyboard.c): the keys' layout (each at least 80 x 60 pixels), where a finger lands, the glyph atlas's packing, the quads
that draw the keyboard (inside the screen, the line's end in its box, nothing when closed) and the keyboard as the game's thread publishes it. */
#include "wiiu_keyboard.h"
#include "wiiu_keyboard_code.inc"
/* the game's own tables, taken from its sources: the virtual keys its input knows (vk_mapped), the chat keyboard's pages (chat_letters, chat_symbols) and the keys' ascii codes
(ascii_mapped) */
#include "reference.inc"

/* a rasterizer with no font: every glyph of a font is a box of its size, filled with its code; a space has none */
static int fail_measures, fail_glyph_code, glyph_width = 10, glyph_height = 16;

static int mock_raster(int font, unsigned long code, unsigned char *buffer, int stride, int max_width, int max_height, int *left, int *top, int *width, int *height, float *advance)
{
	int x, y;

	if (!code)
	{
		if (fail_measures)
			return 0;
		*left = font ? 24 : 20;
		*top = font ? 8 : 6;
		*width = *height = 0;
		*advance = 0.0f;
		return 1;
	}
	if ((int)code == fail_glyph_code)
		return 0;
	*advance = font ? 14.0f : 12.0f;
	*left = 1;
	*top = font ? -20 : -16;
	*width = code == ' ' ? 0 : font ? glyph_width + 2 : glyph_width;
	*height = code == ' ' ? 0 : font ? glyph_height + 4 : glyph_height;
	if (*width > max_width || *height > max_height)
		return 0;
	for (y = 0; y < *height; y++)
		for (x = 0; x < *width; x++)
			buffer[y * stride + x] = (unsigned char)code;
	return 1;
}

static struct wk_atlas atlas;

static void atlas_make(void)
{
	fail_measures = 0;
	fail_glyph_code = -1;
	glyph_width = 10;
	glyph_height = 16;
	CHECK(wiiu_keyboard_atlas_build(&atlas, mock_raster) == 1 && atlas.ready, "atlas");
}

static void view_make(struct halo_chat_keyboard_view *view, const char *text)
{
	static const char *const rows[4] = { "1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm,.?" };
	int row;

	memset(view, 0, sizeof(*view));
	view->open = 1;
	view->channel = 0;
	view->row = 1;
	view->column = 0;
	snprintf(view->text, sizeof(view->text), "%s", text);
	view->length = (int)strlen(view->text);
	for (row = 0; row < 4; row++)
		memcpy(view->keys[row], rows[row], 10);
}

static int overlap(const struct wk_rect *a, const struct wk_rect *b)
{
	return a->x0 < b->x1 && b->x0 < a->x1 && a->y0 < b->y1 && b->y0 < a->y1;
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	int row, column;

	/* every key at least 80 x 60 pixels, the keys tile their 800 x 300 block with no gap or overlap, and the chips, the line and the words fit the screen without touching them */
	CASE("layout")
	{
		struct wk_rect keys[WK_ROWS][WK_COLUMNS], rectangle, chip[WK_CHANNELS], close;
		int channel;

		for (row = 0; row < WK_ROWS; row++)
		{
			const int span = row == WK_ROWS - 1 ? 2 : 1;

			for (column = 0; column < WK_COLUMNS; column += span)
			{
				wiiu_keyboard_key_rect(row, column, &keys[row][column]);
				CHECK(keys[row][column].x1 - keys[row][column].x0 >= 80.0f && keys[row][column].y1 - keys[row][column].y0 >= 60.0f, "key %d,%d is %fx%f", row, column,
					keys[row][column].x1 - keys[row][column].x0, keys[row][column].y1 - keys[row][column].y0);
				CHECK(keys[row][column].x0 >= 0.0f && keys[row][column].y0 >= 0.0f && keys[row][column].x1 <= WK_WIDTH && keys[row][column].y1 <= WK_HEIGHT, "key %d,%d on screen", row, column);
				if (column)
					CHECK(keys[row][column].x0 == keys[row][column - span].x1, "gap in row %d at %d", row, column);
				if (row)
					CHECK(keys[row][column].y0 == keys[row - 1][0].y1, "gap above row %d", row);
			}
		}
		CHECK(keys[0][0].x0 == 27.0f && keys[0][9].x1 == 827.0f && keys[4][8].x1 == 827.0f && keys[0][0].y0 == 128.0f && keys[4][0].y1 == 428.0f, "the block");
		CHECK(keys[4][0].x1 - keys[4][0].x0 == 160.0f, "wide keys");
		for (channel = 0; channel < WK_CHANNELS; channel++)
		{
			wiiu_keyboard_chip_rect(channel, &chip[channel]);
			CHECK(chip[channel].y1 - chip[channel].y0 >= 44.0f && chip[channel].x1 - chip[channel].x0 >= 80.0f, "chip %d size", channel);
			CHECK(chip[channel].y1 <= (float)WK_FIELD_TOP, "chip %d above the line", channel);
			for (row = 0; row < WK_ROWS; row++)
				CHECK(!overlap(&chip[channel], &keys[row][0]), "chip over a key");
			if (channel)
				CHECK(!overlap(&chip[channel], &chip[channel - 1]), "chips overlap");
		}
		wiiu_keyboard_close_rect(&close);
		CHECK(close.y1 - close.y0 >= 44.0f && close.x1 - close.x0 >= 80.0f && close.x1 <= 827.0f && !overlap(&close, &chip[WK_CHANNELS - 1]), "close chip");
		CHECK(WK_FIELD_TOP + WK_FIELD_HEIGHT <= WK_KEYS_TOP && WK_HINT_TOP >= WK_KEYS_TOP + 5 * WK_KEY_HEIGHT && WK_HINT_TOP + WK_HINT_HEIGHT <= WK_HEIGHT, "bands");
		(void)rectangle;
	}

	/* a finger on a key's cell, anywhere in it, is that key; a wide key is its first column; between and around them nothing */
	CASE("hit")
	{
		struct wk_hit hit;
		struct wk_rect rectangle;
		int channel;

		for (row = 0; row < WK_ROWS; row++)
		{
			const int span = row == WK_ROWS - 1 ? 2 : 1;

			for (column = 0; column < WK_COLUMNS; column += span)
			{
				wiiu_keyboard_key_rect(row, column, &rectangle);
				CHECK(wiiu_keyboard_hit((rectangle.x0 + rectangle.x1) / 2.0f, (rectangle.y0 + rectangle.y1) / 2.0f, &hit) == WK_HIT_KEY && hit.row == row && hit.column == column,
					"centre of %d,%d is %d,%d", row, column, hit.row, hit.column);
				CHECK(wiiu_keyboard_hit(rectangle.x0, rectangle.y0, &hit) == WK_HIT_KEY && hit.row == row && hit.column == column, "top left of %d,%d", row, column);
				CHECK(wiiu_keyboard_hit(rectangle.x1 - 0.01f, rectangle.y1 - 0.01f, &hit) == WK_HIT_KEY && hit.row == row && hit.column == column, "bottom right of %d,%d", row, column);
			}
		}
		/* the edges of the block */
		CHECK(wiiu_keyboard_hit(26.9f, 200.0f, &hit) == WK_HIT_NONE, "left of the block");
		CHECK(wiiu_keyboard_hit(827.0f, 200.0f, &hit) == WK_HIT_NONE, "right of the block");
		CHECK(wiiu_keyboard_hit(100.0f, 127.9f, &hit) == WK_HIT_NONE, "above the block");
		CHECK(wiiu_keyboard_hit(100.0f, 428.0f, &hit) == WK_HIT_NONE, "below the block");
		CHECK(wiiu_keyboard_hit(-5.0f, -5.0f, &hit) == WK_HIT_NONE && wiiu_keyboard_hit(900.0f, 500.0f, &hit) == WK_HIT_NONE, "off the screen");
		/* the second half of a wide key */
		CHECK(wiiu_keyboard_hit(27.0f + 120.0f, 400.0f, &hit) == WK_HIT_KEY && hit.row == 4 && hit.column == 0, "second half of Aa");
		CHECK(wiiu_keyboard_hit(27.0f + 799.0f, 400.0f, &hit) == WK_HIT_KEY && hit.row == 4 && hit.column == 8, "second half of SEND");
		/* the chips, the close chip, the line and the gaps */
		for (channel = 0; channel < WK_CHANNELS; channel++)
		{
			wiiu_keyboard_chip_rect(channel, &rectangle);
			CHECK(wiiu_keyboard_hit((rectangle.x0 + rectangle.x1) / 2.0f, (rectangle.y0 + rectangle.y1) / 2.0f, &hit) == WK_HIT_CHANNEL && hit.channel == channel, "chip %d", channel);
			CHECK(wiiu_keyboard_hit(rectangle.x1 + 1.0f, rectangle.y0 + 10.0f, &hit) != WK_HIT_CHANNEL || channel == WK_CHANNELS, "gap after chip %d", channel);
		}
		wiiu_keyboard_close_rect(&rectangle);
		CHECK(wiiu_keyboard_hit((rectangle.x0 + rectangle.x1) / 2.0f, (rectangle.y0 + rectangle.y1) / 2.0f, &hit) == WK_HIT_CLOSE, "close");
		CHECK(wiiu_keyboard_hit(400.0f, 90.0f, &hit) == WK_HIT_NONE, "the line");
		CHECK(wiiu_keyboard_hit(400.0f, 450.0f, &hit) == WK_HIT_NONE, "the words");
		CHECK(wiiu_keyboard_hit(180.0f, 30.0f, &hit) == WK_HIT_NONE, "between the chips");
		CHECK(wiiu_keyboard_hit(600.0f, 30.0f, &hit) == WK_HIT_NONE, "between the chips and CLOSE");
	}

	/* the atlas: a solid block, every glyph packed inside the atlas without touching another, its coverage copied, its measures and a space's advance kept */
	CASE("atlas")
	{
		int font, index, other, solid_ok = 1;

		atlas_make();
		for (row = 0; row < 8; row++)
			for (column = 0; column < 8; column++)
				solid_ok &= atlas.pixels[row * WK_ATLAS_WIDTH + column] == 255;
		CHECK(solid_ok, "solid block");
		CHECK(atlas.solid_u > 0.0f && atlas.solid_u < 8.0f / WK_ATLAS_WIDTH && atlas.solid_v > 0.0f && atlas.solid_v < 8.0f / WK_ATLAS_HEIGHT, "solid uv %f", atlas.solid_u);
		CHECK(atlas.ascent[0] == 20.0f && atlas.descent[0] == 6.0f && atlas.ascent[1] == 24.0f && atlas.descent[1] == 8.0f, "measures");
		for (font = 0; font < WK_FONTS; font++)
		{
			for (index = 0; index < WK_GLYPHS; index++)
			{
				const struct wk_glyph *glyph = &atlas.glyphs[font][index];
				const int code = WK_GLYPH_FIRST + index;
				int x0 = (int)(glyph->u0 * WK_ATLAS_WIDTH + 0.5f), y0 = (int)(glyph->v0 * WK_ATLAS_HEIGHT + 0.5f);
				int x1 = (int)(glyph->u1 * WK_ATLAS_WIDTH + 0.5f), y1 = (int)(glyph->v1 * WK_ATLAS_HEIGHT + 0.5f);

				CHECK(glyph->present, "glyph %c present", code);
				CHECK(glyph->advance == (font ? 14.0f : 12.0f), "advance");
				if (code == ' ')
				{
					CHECK(glyph->right <= glyph->left, "a space has no box");
					continue;
				}
				CHECK(x0 >= 0 && y0 >= 0 && x1 <= WK_ATLAS_WIDTH && y1 <= WK_ATLAS_HEIGHT && x1 > x0 && y1 > y0, "glyph %c inside", code);
				CHECK(glyph->right - glyph->left == (float)(x1 - x0) && glyph->bottom - glyph->top == (float)(y1 - y0), "box %c", code);
				CHECK(atlas.pixels[y0 * WK_ATLAS_WIDTH + x0] == code && atlas.pixels[(y1 - 1) * WK_ATLAS_WIDTH + x1 - 1] == code, "pixels of %c", code);
				/* (not on the solid block, and not touching another glyph) */
				CHECK(!(x0 < 8 && y0 < 8), "glyph on the solid block");
				for (other = 0; other < index + font * WK_GLYPHS; other++)
				{
					const struct wk_glyph *second = &atlas.glyphs[other / WK_GLYPHS][other % WK_GLYPHS];
					int ax0 = (int)(second->u0 * WK_ATLAS_WIDTH + 0.5f), ay0 = (int)(second->v0 * WK_ATLAS_HEIGHT + 0.5f);
					int ax1 = (int)(second->u1 * WK_ATLAS_WIDTH + 0.5f), ay1 = (int)(second->v1 * WK_ATLAS_HEIGHT + 0.5f);

					if (ax1 <= ax0 || ay1 <= ay0)
						continue;
					CHECK(x1 + WK_ATLAS_PADDING <= ax0 || ax1 + WK_ATLAS_PADDING <= x0 || y1 + WK_ATLAS_PADDING <= ay0 || ay1 + WK_ATLAS_PADDING <= y0, "glyphs %c and %c touch", code, WK_GLYPH_FIRST + other % WK_GLYPHS);
				}
			}
		}
	}

	/* a rasterizer that fails, or a font too big for the atlas, gives no atlas */
	CASE("atlas-fails")
	{
		fail_measures = 1;
		CHECK(wiiu_keyboard_atlas_build(&atlas, mock_raster) == 0 && !atlas.ready, "no measures");
		fail_measures = 0;
		glyph_width = 60;
		glyph_height = 60;
		fail_glyph_code = -1;
		CHECK(wiiu_keyboard_atlas_build(&atlas, mock_raster) == 0 && !atlas.ready, "full");
		glyph_width = 10;
		glyph_height = 16;
		CHECK(wiiu_keyboard_atlas_build(&atlas, NULL) == 0, "no rasterizer");
		/* a glyph the font lacks is skipped, not the atlas: it draws as '?' */
		fail_glyph_code = 'z';
		CHECK(wiiu_keyboard_atlas_build(&atlas, mock_raster) == 1 && atlas.ready && !atlas.glyphs[0]['z' - WK_GLYPH_FIRST].present, "a missing glyph");
	}

	/* the quads: inside the screen, with textures in range; one glyph on each key that has a character; the key picked lit; nothing while closed or without an atlas */
	CASE("quads")
	{
		static struct wk_quad quads[WK_MAXIMUM_QUADS];
		struct halo_chat_keyboard_view view;
		int count, index, inside_keys = 0, ok = 1;
		struct wk_rect rectangle;

		atlas_make();
		view_make(&view, "need help at the flag");
		count = wiiu_keyboard_build(&view, &atlas, 0, quads, WK_MAXIMUM_QUADS);
		CHECK(count > 100 && count < WK_MAXIMUM_QUADS, "quad count %d", count);
		for (index = 0; index < count; index++)
			ok &= quads[index].x0 >= 0.0f && quads[index].y0 >= 0.0f && quads[index].x1 <= WK_WIDTH && quads[index].y1 <= WK_HEIGHT && quads[index].x1 >= quads[index].x0 &&
				quads[index].y1 >= quads[index].y0 && quads[index].u0 >= 0.0f && quads[index].u1 <= 1.0f && quads[index].v0 >= 0.0f && quads[index].v1 <= 1.0f &&
				quads[index].a >= 0.0f && quads[index].a <= 1.0f;
		CHECK(ok, "a quad off the screen or the atlas");
		/* each key of the first four rows: its background, and a glyph quad centred in it */
		for (row = 0; row < 4; row++)
			for (column = 0; column < 10; column++)
			{
				int glyphs = 0;

				wiiu_keyboard_key_rect(row, column, &rectangle);
				for (index = 0; index < count; index++)
				{
					const float cx = (quads[index].x0 + quads[index].x1) / 2.0f, cy = (quads[index].y0 + quads[index].y1) / 2.0f;

					if (quads[index].u1 > quads[index].u0 && cx > rectangle.x0 && cx < rectangle.x1 && cy > rectangle.y0 && cy < rectangle.y1)
						glyphs++;
				}
				CHECK(glyphs == 1, "key %d,%d has %d glyph quads", row, column, glyphs);
				inside_keys += glyphs;
			}
		CHECK(inside_keys == 40, "forty labels");
		/* the key picked is blue (0.337, 0.51, 0.863), the others not */
		{
			int picked = 0;

			wiiu_keyboard_key_rect(1, 0, &rectangle);
			for (index = 0; index < count; index++)
				picked += quads[index].u0 == quads[index].u1 && quads[index].x0 == rectangle.x0 + 2.0f && quads[index].y0 == rectangle.y0 + 2.0f && quads[index].r < 0.4f && quads[index].b > 0.85f && quads[index].g > 0.5f;
			CHECK(picked == 1, "the picked key lit: %d", picked);
		}
		/* closed, or no atlas: nothing */
		view.open = 0;
		CHECK(wiiu_keyboard_build(&view, &atlas, 0, quads, WK_MAXIMUM_QUADS) == 0, "closed");
		view.open = 1;
		atlas.ready = 0;
		CHECK(wiiu_keyboard_build(&view, &atlas, 0, quads, WK_MAXIMUM_QUADS) == 0, "no atlas");
		atlas.ready = 1;
		/* the most quads asked for */
		CHECK(wiiu_keyboard_build(&view, &atlas, 0, quads, 10) == 10, "capped");
		CHECK(wiiu_keyboard_build(&view, &atlas, 0, quads, 0) == 0, "capped to none");
		CHECK(wiiu_keyboard_build(NULL, &atlas, 0, quads, 10) == 0 && wiiu_keyboard_build(&view, NULL, 0, quads, 10) == 0, "nulls");
	}

	/* the line: its end stays in its box however long it is, the caret blinks, and a full line is marked */
	CASE("line")
	{
		static struct wk_quad quads[WK_MAXIMUM_QUADS];
		struct halo_chat_keyboard_view view;
		char full[HALO_CHAT_KEYBOARD_TEXT];
		int count, index, past = 0, in_line = 0, amber = 0, with_caret, without_caret;

		atlas_make();
		memset(full, 'W', 63);
		full[63] = 0;
		view_make(&view, full);
		count = wiiu_keyboard_build(&view, &atlas, 1000, quads, WK_MAXIMUM_QUADS);
		for (index = 0; index < count; index++)
		{
			if (quads[index].u1 > quads[index].u0 && quads[index].y0 >= (float)WK_FIELD_TOP && quads[index].y1 <= (float)(WK_FIELD_TOP + WK_FIELD_HEIGHT))
			{
				in_line++;
				past += quads[index].x1 > 827.0f - 10.0f;
			}
			amber += quads[index].r == 1.0f && quads[index].g > 0.68f && quads[index].g < 0.7f && quads[index].u0 == quads[index].u1;
		}
		CHECK(in_line > 8 && in_line < 63 + 20, "glyphs in the line: %d", in_line);
		CHECK(past == 0, "%d glyphs past the box", past);
		CHECK(amber == 1, "a full line has an amber edge: %d", amber);
		/* a short line is whole */
		view_make(&view, "hi");
		count = wiiu_keyboard_build(&view, &atlas, 0, quads, WK_MAXIMUM_QUADS);
		with_caret = count;
		without_caret = wiiu_keyboard_build(&view, &atlas, 500, quads, WK_MAXIMUM_QUADS);
		CHECK(with_caret == without_caret + 1, "the caret blinks: %d and %d", with_caret, without_caret);
		view_make(&view, "");
		CHECK(wiiu_keyboard_build(&view, &atlas, 0, quads, WK_MAXIMUM_QUADS) > 100, "an empty line");
		/* the text is measured as it is drawn */
		CHECK(wiiu_keyboard_text_width(&atlas, WK_FONT_TEXT, 1.0f, "abc", 3) == 42.0f && wiiu_keyboard_text_width(&atlas, WK_FONT_LABEL, 0.5f, "abcd", 4) == 24.0f, "widths");
		CHECK(wiiu_keyboard_text_width(&atlas, WK_FONT_TEXT, 1.0f, "abc", 2) == 28.0f && wiiu_keyboard_text_width(&atlas, WK_FONT_TEXT, 1.0f, "", 0) == 0.0f, "counts");
	}

	/* the states: capitals and the symbols' page light their keys; the channel's chip is the accent blue */
	CASE("states")
	{
		static struct wk_quad quads[WK_MAXIMUM_QUADS];
		struct halo_chat_keyboard_view view;
		int count, index, lit_shift = 0, lit_symbols = 0, accents = 0;
		struct wk_rect shift, symbols, chip;

		atlas_make();
		view_make(&view, "x");
		view.shift = 1;
		view.channel = 1;
		count = wiiu_keyboard_build(&view, &atlas, 0, quads, WK_MAXIMUM_QUADS);
		wiiu_keyboard_key_rect(4, 0, &shift);
		wiiu_keyboard_key_rect(4, 2, &symbols);
		wiiu_keyboard_chip_rect(1, &chip);
		for (index = 0; index < count; index++)
		{
			const struct wk_quad *q = &quads[index];

			if (q->u0 != q->u1)
				continue;
			lit_shift += q->x0 == shift.x0 + 2.0f && q->y0 == shift.y0 + 2.0f && q->b > 0.55f && q->b < 0.58f;
			lit_symbols += q->x0 == symbols.x0 + 2.0f && q->y0 == symbols.y0 + 2.0f && q->b > 0.55f && q->b < 0.58f;
			accents += q->x0 == chip.x0 && q->y0 == chip.y0 && q->g > 0.58f && q->g < 0.6f && q->b == 1.0f;
		}
		CHECK(lit_shift == 1 && lit_symbols == 0, "shift lit %d symbols %d", lit_shift, lit_symbols);
		CHECK(accents == 1, "the team chip %d", accents);
		view.symbols = 1;
		count = wiiu_keyboard_build(&view, &atlas, 0, quads, WK_MAXIMUM_QUADS);
		lit_symbols = 0;
		for (index = 0; index < count; index++)
			lit_symbols += quads[index].u0 == quads[index].u1 && quads[index].x0 == symbols.x0 + 2.0f && quads[index].y0 == symbols.y0 + 2.0f && quads[index].b > 0.55f && quads[index].b < 0.58f;
		CHECK(lit_symbols == 1, "the symbols' page lit");
	}

	/* published whole: the same keyboard comes back, an identical one is not written again, and a half-written one reads closed */
	CASE("publish")
	{
		struct halo_chat_keyboard_view view, back;
		unsigned int sequence;

		view_make(&view, "hello");
		wiiu_keyboard_publish(&view);
		sequence = published.sequence;
		CHECK(!(sequence & 1u), "an even sequence after a write");
		memset(&back, 0, sizeof(back));
		wiiu_keyboard_snapshot(&back);
		CHECK(!memcmp(&back, &view, sizeof(view)), "the same keyboard");
		wiiu_keyboard_publish(&view);
		CHECK(published.sequence == sequence, "an identical keyboard is not written again");
		view.length = 3;
		wiiu_keyboard_publish(&view);
		CHECK(published.sequence == sequence + 2, "a changed one is");
		wiiu_keyboard_snapshot(&back);
		CHECK(back.length == 3, "the new one");
		/* (the writer stopped half way) */
		published.sequence++;
		wiiu_keyboard_snapshot(&back);
		CHECK(!back.open, "a half-written keyboard reads closed");
		published.sequence++;
		wiiu_keyboard_snapshot(&back);
		CHECK(back.open && back.length == 3, "and whole again");
	}
	/* a key's page: the chat keyboard's letters and symbols, capitals on letters alone, none off the keyboard */
	CASE("pages")
	{
		int symbols, shift;

		for (symbols = 0; symbols < 2; symbols++)
			for (shift = 0; shift < 2; shift++)
				for (row = 0; row < 4; row++)
					for (column = 0; column < 10; column++)
					{
						char expected = (symbols ? chat_symbols : chat_letters)[row][column];

						if (shift && expected >= 'a' && expected <= 'z')
							expected = (char)(expected - 'a' + 'A');
						CHECK(wiiu_keyboard_page_key(symbols, shift, row, column) == expected, "page %d %d key %d,%d is '%c' not '%c'", symbols, shift, row, column,
							wiiu_keyboard_page_key(symbols, shift, row, column), expected);
					}
		CHECK(wiiu_keyboard_page_key(0, 0, 4, 0) == 0 && wiiu_keyboard_page_key(0, 0, -1, 0) == 0 && wiiu_keyboard_page_key(0, 0, 0, 10) == 0 && wiiu_keyboard_page_key(0, 0, 0, -1) == 0, "off the pages");
	}

	/* every printable character the keyboard can type is a keystroke the game's input takes: a virtual key its table maps, with shift for a capital or a shifted symbol; a character
	typed by the key it is on gives the character back as the game reads it */
	CASE("keystrokes")
	{
		unsigned char key, flags;
		int character, symbols, shift;

		for (character = 0; character < 256; character++)
		{
			const int printable = character >= ' ' && character <= '~';
			const int got = wiiu_keyboard_keystroke((char)character, &key, &flags);

			if (!printable)
			{
				CHECK(!got, "character %d is a keystroke", character);
				continue;
			}
			CHECK(got, "'%c' has no keystroke", character);
			CHECK(vk_mapped[key], "'%c' is virtual key %02X, which the game's input does not know", character, key);
			CHECK(ascii_mapped[character], "'%c' is an ascii code the game does not take", character);
			CHECK((flags & ~0x02) == 0, "'%c' has flags %02X", character, flags);
			if (character >= 'A' && character <= 'Z')
				CHECK(flags == 0x02 && key == character, "capital %c", character);
			if (character >= 'a' && character <= 'z')
				CHECK(flags == 0 && key == character - 32, "letter %c", character);
			if (character >= '0' && character <= '9')
				CHECK(flags == 0 && key == character, "digit %c", character);
		}
		/* every character on the keyboard's pages */
		for (symbols = 0; symbols < 2; symbols++)
			for (shift = 0; shift < 2; shift++)
				for (row = 0; row < 4; row++)
					for (column = 0; column < 10; column++)
					{
						const char typed = wiiu_keyboard_page_key(symbols, shift, row, column);

						CHECK(wiiu_keyboard_keystroke(typed, &key, &flags) && vk_mapped[key], "'%c' on the pages", typed);
					}
		CHECK(!wiiu_keyboard_keystroke('\n', &key, &flags) && !wiiu_keyboard_keystroke((char)0xE9, &key, &flags) && !wiiu_keyboard_keystroke(0, &key, &flags), "none for a line end, a high byte, nothing");
		/* a shifted symbol is its key with shift: ! is 1, : is the semicolon's */
		CHECK(wiiu_keyboard_keystroke('!', &key, &flags) && key == '1' && flags == 0x02, "!");
		CHECK(wiiu_keyboard_keystroke(':', &key, &flags) && key == 0xBA && flags == 0x02, ":");
		CHECK(wiiu_keyboard_keystroke(';', &key, &flags) && key == 0xBA && flags == 0, ";");
	}

	/* the queue: a press and its release for each character, in order, one pair at a time, dropped when full; delete is VK_BACK with ascii 8 */
	CASE("queue")
	{
		struct wk_keystroke keystroke;
		int count = 0;

		wiiu_keyboard_keys_clear();
		CHECK(!wiiu_keyboard_keys_next(&keystroke), "empty");
		wiiu_keyboard_keys_type('a');
		wiiu_keyboard_keys_type('B');
		wiiu_keyboard_keys_delete();
		wiiu_keyboard_keys_type('|');
		wiiu_keyboard_keys_type('\n');
		CHECK(wiiu_keyboard_keys_next(&keystroke) && keystroke.ascii == 'a' && keystroke.virtual_key == 'A' && keystroke.flags == 0, "a down");
		CHECK(wiiu_keyboard_keys_next(&keystroke) && keystroke.ascii == 'a' && keystroke.virtual_key == 'A' && keystroke.flags == 0x40, "a up");
		CHECK(wiiu_keyboard_keys_next(&keystroke) && keystroke.ascii == 'B' && keystroke.flags == 0x02, "B down with shift");
		CHECK(wiiu_keyboard_keys_next(&keystroke) && keystroke.ascii == 'B' && keystroke.flags == 0x42, "B up");
		CHECK(wiiu_keyboard_keys_next(&keystroke) && keystroke.virtual_key == 0x08 && keystroke.ascii == 8 && keystroke.flags == 0, "delete down");
		CHECK(wiiu_keyboard_keys_next(&keystroke) && keystroke.virtual_key == 0x08 && keystroke.flags == 0x40, "delete up");
		CHECK(!wiiu_keyboard_keys_next(&keystroke), "nothing for a pipe or a line end");
		/* full: whole pairs only */
		for (row = 0; row < 200; row++)
			wiiu_keyboard_keys_type('z');
		while (wiiu_keyboard_keys_next(&keystroke))
			count++;
		CHECK(count == WK_KEYSTROKE_QUEUE, "%d queued", count);
		wiiu_keyboard_keys_type('q');
		wiiu_keyboard_keys_clear();
		CHECK(!wiiu_keyboard_keys_next(&keystroke), "cleared");
	}

	/* a menu's field: taps are characters (on the page and capitals the field has), delete, DONE (the chip or the SEND key), CANCEL; Aa and #+= change the page and give nothing to
	type; nothing happens while it is not edited; the text is the menu's, printable ASCII only, and the view shows it */
	CASE("field")
	{
		struct wk_field field;
		struct wk_action action;
		struct halo_chat_keyboard_view view;
		struct wk_rect key;

		memset(&field, 0, sizeof(field));
		CHECK(wiiu_keyboard_field_tap(&field, 100.0f, 150.0f, &action) == WK_ACTION_NONE, "not edited");
		wiiu_keyboard_field_begin(&field, 0);
		wiiu_keyboard_field_text(&field, "My Server|\x01\xE9", 24);
		CHECK(field.length == 9 && !strcmp(field.text, "My Server") && field.limit == 24, "the menu's text: '%s' %d", field.text, field.limit);
		/* q */
		wiiu_keyboard_key_rect(1, 0, &key);
		CHECK(wiiu_keyboard_field_tap(&field, key.x0 + 5.0f, key.y0 + 5.0f, &action) == WK_ACTION_CHARACTER && action.character == 'q' && field.row == 1 && field.column == 0, "q");
		/* Aa, then q is Q */
		wiiu_keyboard_key_rect(4, 0, &key);
		CHECK(wiiu_keyboard_field_tap(&field, key.x0 + 130.0f, key.y0 + 5.0f, &action) == WK_ACTION_PAGE && field.shift, "Aa");
		wiiu_keyboard_key_rect(1, 0, &key);
		CHECK(wiiu_keyboard_field_tap(&field, key.x0 + 5.0f, key.y0 + 5.0f, &action) == WK_ACTION_CHARACTER && action.character == 'Q', "Q");
		/* #+= and a symbol */
		wiiu_keyboard_key_rect(4, 2, &key);
		CHECK(wiiu_keyboard_field_tap(&field, key.x0 + 5.0f, key.y0 + 5.0f, &action) == WK_ACTION_PAGE && field.symbols, "#+=");
		wiiu_keyboard_key_rect(1, 0, &key);
		CHECK(wiiu_keyboard_field_tap(&field, key.x0 + 5.0f, key.y0 + 5.0f, &action) == WK_ACTION_CHARACTER && action.character == '!', "!");
		/* space, delete, DONE by the key and by the chip, CANCEL by CLOSE's place */
		wiiu_keyboard_key_rect(4, 4, &key);
		CHECK(wiiu_keyboard_field_tap(&field, key.x0 + 5.0f, key.y0 + 5.0f, &action) == WK_ACTION_CHARACTER && action.character == ' ', "space");
		wiiu_keyboard_key_rect(4, 6, &key);
		CHECK(wiiu_keyboard_field_tap(&field, key.x0 + 5.0f, key.y0 + 5.0f, &action) == WK_ACTION_DELETE, "delete");
		wiiu_keyboard_key_rect(4, 8, &key);
		CHECK(wiiu_keyboard_field_tap(&field, key.x0 + 5.0f, key.y0 + 5.0f, &action) == WK_ACTION_DONE, "done by SEND's place");
		wiiu_keyboard_chip_rect(0, &key);
		CHECK(wiiu_keyboard_field_tap(&field, key.x0 + 5.0f, key.y0 + 5.0f, &action) == WK_ACTION_DONE, "done by the first chip");
		wiiu_keyboard_chip_rect(1, &key);
		CHECK(wiiu_keyboard_field_tap(&field, key.x0 + 5.0f, key.y0 + 5.0f, &action) == WK_ACTION_NONE, "the other chips are not there");
		wiiu_keyboard_chip_rect(2, &key);
		CHECK(wiiu_keyboard_field_tap(&field, key.x0 + 5.0f, key.y0 + 5.0f, &action) == WK_ACTION_NONE, "the other chips are not there");
		wiiu_keyboard_close_rect(&key);
		CHECK(wiiu_keyboard_field_tap(&field, key.x0 + 5.0f, key.y0 + 5.0f, &action) == WK_ACTION_CANCEL, "cancel");
		CHECK(wiiu_keyboard_field_tap(&field, 400.0f, 90.0f, &action) == WK_ACTION_NONE, "the line");
		/* the view */
		wiiu_keyboard_field_begin(&field, 1);
		wiiu_keyboard_field_text(&field, "secret", 16);
		field.shift = 1;
		wiiu_keyboard_field_view(&field, &view);
		CHECK(view.open && view.mode == 1 && view.masked && view.limit == 16 && view.length == 6 && !strcmp(view.text, "secret") && view.shift, "the view");
		CHECK(!memcmp(view.keys[1], "QWERTYUIOP", 10) && !memcmp(view.keys[0], "1234567890", 10), "its keys");
		wiiu_keyboard_field_end(&field);
		wiiu_keyboard_field_view(&field, &view);
		CHECK(!view.open && !field.active && field.length == 0, "ended");
		/* the text of a field that is not edited is not kept */
		wiiu_keyboard_field_text(&field, "x", 5);
		CHECK(field.length == 0, "not kept");
	}

	/* the field drawn: DONE and CANCEL, no channel chips, stars for a password, the characters left from the field's own limit */
	CASE("field-quads")
	{
		static struct wk_quad quads[WK_MAXIMUM_QUADS];
		struct halo_chat_keyboard_view chat, menu, masked;
		struct wk_rect chip, close;
		int count_chat, count_menu, index, in_chip1 = 0, in_chip2 = 0, in_line_masked = 0, in_line_clear = 0, ok = 1;

		atlas_make();
		view_make(&chat, "secret");
		view_make(&menu, "secret");
		menu.mode = 1;
		menu.limit = 8;
		masked = menu;
		masked.masked = 1;
		count_chat = wiiu_keyboard_build(&chat, &atlas, 0, quads, WK_MAXIMUM_QUADS);
		count_menu = wiiu_keyboard_build(&menu, &atlas, 0, quads, WK_MAXIMUM_QUADS);
		CHECK(count_menu > 100 && count_menu < count_chat, "the menu's field has fewer chips than the chat: %d against %d", count_menu, count_chat);
		for (index = 0; index < count_menu; index++)
			ok &= quads[index].x0 >= 0.0f && quads[index].x1 <= WK_WIDTH && quads[index].y0 >= 0.0f && quads[index].y1 <= WK_HEIGHT;
		CHECK(ok, "inside the screen");
		wiiu_keyboard_chip_rect(1, &chip);
		wiiu_keyboard_chip_rect(2, &close);
		for (index = 0; index < count_menu; index++)
		{
			const float cx = (quads[index].x0 + quads[index].x1) / 2.0f, cy = (quads[index].y0 + quads[index].y1) / 2.0f;

			in_chip1 += cx > chip.x0 && cx < chip.x1 && cy > chip.y0 && cy < chip.y1;
			in_chip2 += cx > close.x0 && cx < close.x1 && cy > close.y0 && cy < close.y1;
		}
		CHECK(in_chip1 == 0 && in_chip2 == 0, "chips the field has not: %d %d", in_chip1, in_chip2);
		/* the same text, stars or not: the glyph quads of the line are as many, but their texture is the one star's */
		{
			static struct wk_quad again[WK_MAXIMUM_QUADS];
			int count_masked = wiiu_keyboard_build(&masked, &atlas, 0, again, WK_MAXIMUM_QUADS);

			CHECK(count_masked == count_menu, "the same count of quads: %d and %d", count_masked, count_menu);
			for (index = 0; index < count_menu; index++)
			{
				const struct wk_quad *a = &quads[index], *b = &again[index];

				if (a->y0 >= (float)WK_FIELD_TOP && a->y1 <= (float)(WK_FIELD_TOP + WK_FIELD_HEIGHT) && a->x0 > 95.0f && a->u1 > a->u0)
				{
					in_line_clear++;
					in_line_masked += b->u0 == atlas.glyphs[WK_FONT_TEXT]['*' - WK_GLYPH_FIRST].u0;
				}
			}
			CHECK(in_line_clear >= 6 && in_line_masked == in_line_clear, "stars: %d of %d", in_line_masked, in_line_clear);
		}
	}
	return 0;
}
