#include "pages.h"

#include "../font_mini.h"
#include "ui.h"

#include <gx2/clear.h>
#include <gx2/context.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define TV_WIDTH 1280u
#define TV_HEIGHT 720u
#define DRC_WIDTH 854u
#define DRC_HEIGHT 480u

typedef struct screen
{
	unsigned width, height;
	unsigned scale; /* of the font: the cell is FONT_CELL_W * scale by FONT_CELL_H * scale */
	text_layer layer;
	gx_texture texture;
} screen;

static uint8_t tv_pixels[TV_WIDTH * TV_HEIGHT * 4];
static uint8_t drc_pixels[DRC_WIDTH * DRC_HEIGHT * 4];
static screen tv_screen, drc_screen;

static const page *rendered_page;
static int text_dirty = 1;

static const float palette[STYLE_COUNT][4] =
{
	{ 0.92f, 0.92f, 0.92f, 1.0f }, /* normal */
	{ 0.35f, 0.95f, 0.45f, 1.0f }, /* good */
	{ 1.00f, 0.35f, 0.35f, 1.0f }, /* bad */
	{ 0.55f, 0.58f, 0.62f, 1.0f }, /* dim */
	{ 1.00f, 0.85f, 0.30f, 1.0f }, /* head */
};

void page_clear(page *p)
{
	unsigned row;

	memset(p, 0, sizeof(*p));
	for (row = 0; row < PAGE_ROWS; row++)
		memset(p->grid[row], ' ', PAGE_COLS);
}

void page_text(page *p, unsigned col, unsigned row, int style, const char *format, ...)
{
	char line[PAGE_COLS + 1];
	va_list arguments;
	unsigned i;

	if (row >= PAGE_ROWS || col >= PAGE_COLS)
		return;
	va_start(arguments, format);
	vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	for (i = 0; line[i] && col + i < PAGE_COLS; i++)
	{
		p->grid[row][col + i] = line[i];
		p->style[row][col + i] = (uint8_t)style;
	}
}

unsigned page_wrap(page *p, unsigned col, unsigned row, unsigned width, int style, const char *format, ...)
{
	char text[512];
	va_list arguments;
	size_t length, done = 0;
	unsigned rows = 0;

	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	length = strlen(text);
	while (done < length)
	{
		size_t take = length - done < width ? length - done : width, back = take;

		if (length - done > width)
		{
			while (back > 0 && text[done + back] != ' ')
				back--;
			if (back > 0)
				take = back;
		}
		page_text(p, col, row + rows, style, "%.*s", (int)take, text + done);
		done += take;
		while (done < length && text[done] == ' ')
			done++;
		rows++;
	}
	return rows;
}

int page_tile_add(page *p, unsigned col, unsigned row, unsigned cols, unsigned rows, void (*draw)(unsigned, float, float), unsigned index, int border)
{
	page_tile *t;

	if (p->tile_count >= PAGE_MAX_TILES)
	{
		p->dropped++;
		return -1;
	}
	t = &p->tiles[p->tile_count];
	t->col = col;
	t->row = row;
	t->cols = cols;
	t->rows = rows;
	t->draw = draw;
	t->index = index;
	t->border = border;
	return (int)p->tile_count++;
}

static int screen_init(screen *s, uint8_t *pixels, unsigned width, unsigned height, unsigned scale, char *error, size_t error_size)
{
	s->width = width;
	s->height = height;
	s->scale = scale;
	text_layer_init(&s->layer, pixels, width, height, scale);
	if (s->layer.cols != PAGE_COLS || s->layer.rows != PAGE_ROWS)
	{
		snprintf(error, error_size, "a %ux%u screen at scale %u holds %ux%u cells, not %ux%u", width, height, scale, s->layer.cols, s->layer.rows,
			PAGE_COLS, PAGE_ROWS);
		return 0;
	}
	return gx_texture_create(&s->texture, width, height, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, 4, 0, error, error_size);
}

int pages_init(char *error, size_t error_size)
{
	return screen_init(&tv_screen, tv_pixels, TV_WIDTH, TV_HEIGHT, 3, error, error_size) &&
		screen_init(&drc_screen, drc_pixels, DRC_WIDTH, DRC_HEIGHT, 2, error, error_size);
}

/* the page's text into the layer: a run of cells of one style at a time */
static void text_to_layer(const page *p, screen *s)
{
	unsigned row, col, start;

	text_layer_clear(&s->layer);
	for (row = 0; row < PAGE_ROWS; row++)
	{
		for (col = 0; col < PAGE_COLS; col = start)
		{
			uint8_t style = p->style[row][col];
			char run[PAGE_COLS + 1];
			unsigned length = 0;
			const float *c = palette[style < STYLE_COUNT ? style : STYLE_NORMAL];

			for (start = col; start < PAGE_COLS && p->style[row][start] == style; start++)
				run[length++] = p->grid[row][start];
			run[length] = 0;
			if (strspn(run, " ") == length)
				continue;
			s->layer.color[0] = (uint8_t)(c[0] * 255.0f);
			s->layer.color[1] = (uint8_t)(c[1] * 255.0f);
			s->layer.color[2] = (uint8_t)(c[2] * 255.0f);
			s->layer.color[3] = 255;
			text_layer_puts(&s->layer, col, row, run);
		}
	}
	gx_texture_upload(&s->texture, s->layer.pixels, (size_t)s->width * 4);
}

/* The page is laid out in cells and drawn on whatever size the system gave the screen's color buffer (the TV may be 480p,
720p or 1080p): the cells, the pictures and the text layer (which has the same 106 x 40 cells) all scale with it. */
static void draw_screen(const page *p, screen *s, const GX2ColorBuffer *buffer)
{
	static const float background[4] = { 0.05f, 0.06f, 0.08f, 1.0f };
	static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	float width = (float)buffer->surface.width, height = (float)buffer->surface.height;
	float cell_w = width / (float)PAGE_COLS, cell_h = height / (float)PAGE_ROWS;
	unsigned i;

	gx_view(0.0f, 0.0f, width, height);
	ui_begin(width, height);
	ui_rect(0.0f, 0.0f, width, height, background);
	for (i = 0; i < p->tile_count; i++)
	{
		const page_tile *t = &p->tiles[i];
		float x = (float)t->col * cell_w, y = (float)t->row * cell_h;
		float w = (float)t->cols * cell_w, h = (float)t->rows * cell_h;

		if (t->border != STYLE_DIM)
		{
			ui_rect(x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f, palette[t->border]);
			ui_rect(x, y, w, h, background);
		}
		ui_flush();
		gx_view(x, y, w, h);
		ui_begin(w, h);
		t->draw(t->index, w, h);
		ui_flush();
		gx_view(0.0f, 0.0f, width, height);
		ui_begin(width, height);
	}
	if (text_dirty || rendered_page != p)
		text_to_layer(p, s);
	ui_quad(0.0f, 0.0f, width, height, &s->texture.texture, 0.0f, 0.0f, 1.0f, 1.0f, white, 1);
	ui_flush();
}

/* The target's own context state, color buffer and depth buffer, cleared. (WHBGfxBeginRenderTV only selects the context state,
and WHBGfxClearColor clears both screens and leaves the GamePad's state selected; the offscreen passes of the tests change
the TV's state too. So the buffers are bound here, every frame.) */
static void begin_target(int tv)
{
	GX2ColorBuffer *color = tv ? WHBGfxGetTVColourBuffer() : WHBGfxGetDRCColourBuffer();
	GX2DepthBuffer *depth = tv ? WHBGfxGetTVDepthBuffer() : WHBGfxGetDRCDepthBuffer();
	GX2ContextState *context = tv ? WHBGfxGetTVContextState() : WHBGfxGetDRCContextState();

	GX2SetContextState(context);
	GX2SetColorBuffer(color, GX2_RENDER_TARGET_0);
	GX2SetDepthBuffer(depth);
	GX2ClearColor(color, 0.05f, 0.06f, 0.08f, 1.0f);
	GX2SetContextState(context);
	GX2SetColorBuffer(color, GX2_RENDER_TARGET_0);
	GX2SetDepthBuffer(depth);
}

void pages_dirty(void)
{
	text_dirty = 1;
}

void pages_show(const page *p)
{
	ui_frame();
	WHBGfxBeginRender();
	begin_target(1);
	draw_screen(p, &tv_screen, WHBGfxGetTVColourBuffer());
	WHBGfxFinishRenderTV();
	begin_target(0);
	draw_screen(p, &drc_screen, WHBGfxGetDRCColourBuffer());
	WHBGfxFinishRenderDRC();
	WHBGfxFinishRender();
	rendered_page = p;
	text_dirty = 0;
}
