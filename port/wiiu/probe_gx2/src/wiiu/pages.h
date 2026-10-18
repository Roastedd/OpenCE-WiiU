/*
PAGES.H

What the probe shows, a page at a time: a grid of text cells (106 columns by 40 rows, which is 12 x 18 pixels a cell
on the TV and 8 x 12 on the GamePad) and pictures that are drawn by the GPU into rectangles of cells. The same page is
drawn on both screens. A picture is a callback; it finds the viewport set to its rectangle and draws what it tests
into it (for the 64 x 64 test scenes, the very draw whose result was read back).
*/

#ifndef HALO_WIIU_GX2_PAGES_H
#define HALO_WIIU_GX2_PAGES_H

#include "gx.h"

#define PAGE_COLS 106
#define PAGE_ROWS 40
#define PAGE_MAX_TILES 32

enum
{
	STYLE_NORMAL,
	STYLE_GOOD,
	STYLE_BAD,
	STYLE_DIM,
	STYLE_HEAD,
	STYLE_COUNT
};

typedef struct page_tile
{
	unsigned col, row, cols, rows; /* the cells the picture fills */
	/* width and height in pixels of the viewport the callback finds set; index is its own */
	void (*draw)(unsigned index, float width, float height);
	unsigned index;
	int border; /* a STYLE_ for the frame (GOOD, BAD) or STYLE_DIM for none */
} page_tile;

typedef struct page
{
	char grid[PAGE_ROWS][PAGE_COLS + 1];
	uint8_t style[PAGE_ROWS][PAGE_COLS];
	page_tile tiles[PAGE_MAX_TILES];
	unsigned tile_count;
	unsigned dropped; /* pictures that did not fit */
} page;

void page_clear(page *p);
/* text at a cell, cut off at the page's edge; a format like printf's */
void page_text(page *p, unsigned col, unsigned row, int style, const char *format, ...)
#if defined(__GNUC__) || defined(__clang__)
	__attribute__((format(printf, 5, 6)))
#endif
	;
/* the same, wrapped at spaces to width columns; returns the number of rows used */
unsigned page_wrap(page *p, unsigned col, unsigned row, unsigned width, int style, const char *format, ...)
#if defined(__GNUC__) || defined(__clang__)
	__attribute__((format(printf, 6, 7)))
#endif
	;
/* adds a picture; returns its number or -1 if the page is full */
int page_tile_add(page *p, unsigned col, unsigned row, unsigned cols, unsigned rows, void (*draw)(unsigned, float, float), unsigned index, int border);

/* the two screens' text layers and their textures; 0 with the reason in error */
int pages_init(char *error, size_t error_size);
/* the text of the page being shown has changed (it is drawn again from the page's cells; otherwise the last drawing is kept) */
void pages_dirty(void);
/* draws the page on the TV and the GamePad and swaps (a whole frame) */
void pages_show(const page *p);

#endif
