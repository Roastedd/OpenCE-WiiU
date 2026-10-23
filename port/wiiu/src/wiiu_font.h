/*
FONT_MINI.H

A 3 x 5 pixel font and a layer of text drawn with it, so that the probe can show its results on the screen with
nothing but a texture and a quad (no font library, no system font: whatever the console's GPU path has to be proven
before it is trusted with text, and this is the text of the pages that prove it).

A character cell is the glyph and a column of space to its right and a row below: 4 x 6 pixels, enlarged by a scale.
Lower case is drawn as upper case. The layer holds RGBA bytes, white and opaque where a glyph is lit and clear elsewhere;
the program tints it (or sets the color the pixels are written with).
*/

#ifndef HALO_WIIU_FONT_H
#define HALO_WIIU_FONT_H

#include <stdint.h>

#define FONT_GLYPH_W 3
#define FONT_GLYPH_H 5
#define FONT_CELL_W 4
#define FONT_CELL_H 6

/* The glyph's five rows, bit 2 the leftmost pixel. 1 if the font has the character (else rows is a filled box). */
int font_glyph(char c, uint8_t rows[FONT_GLYPH_H]);

typedef struct text_layer
{
	uint8_t *pixels; /* width * height RGBA */
	unsigned width, height;
	unsigned scale;
	unsigned cols, rows; /* how many character cells fit */
	uint8_t color[4];    /* what lit pixels are written as: opaque white to begin with */
} text_layer;

void text_layer_init(text_layer *layer, uint8_t *pixels, unsigned width, unsigned height, unsigned scale);
void text_layer_clear(text_layer *layer);
/* the text from cell (col, row) rightwards, cut off at the layer's edge; returns the number of cells written */
unsigned text_layer_puts(text_layer *layer, unsigned col, unsigned row, const char *text);

#endif
