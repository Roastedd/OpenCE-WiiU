#include "wiiu_font.h"

#include <string.h>

typedef struct glyph
{
	char c;
	const char *bits; /* 5 rows of 3, '1' lit, top row first */
} glyph;

static const glyph glyphs[] =
{
	{ 'A', "010101111101101" }, { 'B', "110101110101110" }, { 'C', "011100100100011" }, { 'D', "110101101101110" },
	{ 'E', "111100110100111" }, { 'F', "111100110100100" }, { 'G', "011100101101011" }, { 'H', "101101111101101" },
	{ 'I', "111010010010111" }, { 'J', "001001001101010" }, { 'K', "101101110101101" }, { 'L', "100100100100111" },
	{ 'M', "101111111101101" }, { 'N', "110101101101101" }, { 'O', "010101101101010" }, { 'P', "110101110100100" },
	{ 'Q', "010101101110011" }, { 'R', "110101110101101" }, { 'S', "011100010001110" }, { 'T', "111010010010010" },
	{ 'U', "101101101101111" }, { 'V', "101101101101010" }, { 'W', "101101111111101" }, { 'X', "101101010101101" },
	{ 'Y', "101101010010010" }, { 'Z', "111001010100111" },
	{ '0', "111101101101111" }, { '1', "010110010010111" }, { '2', "110001010100111" }, { '3', "110001010001110" },
	{ '4', "101101111001001" }, { '5', "111100110001110" }, { '6', "011100111101111" }, { '7', "111001010010010" },
	{ '8', "111101111101111" }, { '9', "111101111001110" },
	{ '.', "000000000000010" }, { ',', "000000000010100" }, { ':', "000010000010000" }, { ';', "000010000010100" },
	{ '-', "000000111000000" }, { '+', "000010111010000" }, { '=', "000111000111000" }, { '_', "000000000000111" },
	{ '/', "001001010100100" }, { '(', "010100100100010" }, { ')', "010001001001010" }, { '[', "110100100100110" },
	{ ']', "011001001001011" }, { '<', "001010100010001" }, { '>', "100010001010100" }, { '!', "010010010000010" },
	{ '?', "110001010000010" }, { '%', "101001010100101" }, { '#', "101111101111101" }, { '*', "000101010101000" },
	{ '\'', "010010000000000" }, { '"', "101101000000000" }, { ' ', "000000000000000" },
};

int font_glyph(char c, uint8_t rows[FONT_GLYPH_H])
{
	unsigned i, r, b;

	if (c >= 'a' && c <= 'z')
		c = (char)(c - 'a' + 'A');
	for (i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); i++)
	{
		if (glyphs[i].c != c)
			continue;
		for (r = 0; r < FONT_GLYPH_H; r++)
		{
			rows[r] = 0;
			for (b = 0; b < FONT_GLYPH_W; b++)
				if (glyphs[i].bits[r * FONT_GLYPH_W + b] == '1')
					rows[r] |= (uint8_t)(1u << (FONT_GLYPH_W - 1 - b));
		}
		return 1;
	}
	for (r = 0; r < FONT_GLYPH_H; r++)
		rows[r] = 7;
	return 0;
}

void text_layer_init(text_layer *layer, uint8_t *pixels, unsigned width, unsigned height, unsigned scale)
{
	layer->pixels = pixels;
	layer->width = width;
	layer->height = height;
	layer->scale = scale;
	layer->cols = width / (FONT_CELL_W * scale);
	layer->rows = height / (FONT_CELL_H * scale);
	memset(layer->color, 0xFF, 4);
}

void text_layer_clear(text_layer *layer)
{
	memset(layer->pixels, 0, (size_t)layer->width * layer->height * 4);
}

static void fill(text_layer *layer, unsigned x, unsigned y)
{
	unsigned dx, dy;

	for (dy = 0; dy < layer->scale; dy++)
	{
		uint8_t *row = layer->pixels + ((size_t)(y + dy) * layer->width + x) * 4;

		for (dx = 0; dx < layer->scale; dx++)
			memcpy(row + dx * 4, layer->color, 4);
	}
}

unsigned text_layer_puts(text_layer *layer, unsigned col, unsigned row, const char *text)
{
	unsigned written = 0;

	if (row >= layer->rows)
		return 0;
	for (; *text && col < layer->cols; text++, col++, written++)
	{
		uint8_t rows[FONT_GLYPH_H];
		unsigned gx, gy;

		font_glyph(*text, rows);
		for (gy = 0; gy < FONT_GLYPH_H; gy++)
			for (gx = 0; gx < FONT_GLYPH_W; gx++)
				if (rows[gy] & (1u << (FONT_GLYPH_W - 1 - gx)))
					fill(layer, (col * FONT_CELL_W + gx) * layer->scale, (row * FONT_CELL_H + gy) * layer->scale);
	}
	return written;
}
