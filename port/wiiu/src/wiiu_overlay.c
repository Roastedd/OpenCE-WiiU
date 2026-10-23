#include "wiiu_overlay.h"

#include "wiiu_font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void overlay_reset(struct overlay *overlay)
{
	memset(overlay, 0, sizeof(*overlay));
}

void overlay_push(struct overlay *overlay, float frame_ms, float cpu_ms, float present_ms, float wait_ms, float gpu_ms, unsigned draws, unsigned ring_kb)
{
	const float keep = overlay->count ? 0.92f : 0.0f; /* (an average moves a twelfth of the way to each new frame) */

	overlay->frame[overlay->head] = frame_ms;
	overlay->head = (overlay->head + 1) % OVERLAY_HISTORY;
	if (overlay->count < OVERLAY_HISTORY)
		overlay->count++;
	overlay->frame_average = overlay->frame_average * keep + frame_ms * (1.0f - keep);
	overlay->cpu_average = overlay->cpu_average * keep + cpu_ms * (1.0f - keep);
	overlay->present_average = overlay->present_average * keep + present_ms * (1.0f - keep);
	overlay->wait_average = overlay->wait_average * keep + wait_ms * (1.0f - keep);
	if (gpu_ms > 0.0f)
		overlay->gpu_average = overlay->gpu_average > 0.0f ? overlay->gpu_average * keep + gpu_ms * (1.0f - keep) : gpu_ms;
	overlay->draws = draws;
	overlay->ring_kb = ring_kb;
}

/* the frame at `back` frames ago (0 the last) */
static float frame_back(const struct overlay *overlay, unsigned back)
{
	return overlay->frame[(overlay->head + OVERLAY_HISTORY - 1 - back) % OVERLAY_HISTORY];
}

float overlay_fps(const struct overlay *overlay)
{
	const unsigned frames = overlay->count < 30 ? overlay->count : 30;
	float total = 0.0f;
	unsigned i;

	for (i = 0; i < frames; i++)
		total += frame_back(overlay, i);
	return total > 0.0f ? 1000.0f * (float)frames / total : 0.0f;
}

float overlay_longest(const struct overlay *overlay, unsigned window)
{
	float longest = 0.0f;
	unsigned i;

	if (window > overlay->count)
		window = overlay->count;
	for (i = 0; i < window; i++)
		if (frame_back(overlay, i) > longest)
			longest = frame_back(overlay, i);
	return longest;
}

static int compare_floats(const void *a, const void *b)
{
	const float x = *(const float *)a, y = *(const float *)b;

	return x < y ? -1 : x > y;
}

static float percent_of(float part_ms, float whole_ms)
{
	float percent;

	if (whole_ms <= 0.0f)
		return 0.0f;
	percent = 100.0f * part_ms / whole_ms;
	return percent > 100.0f ? 100.0f : percent;
}

void overlay_update_view(struct overlay *overlay)
{
	struct overlay_view *view = &overlay->view;
	float sorted[OVERLAY_HISTORY];

	view->fps = overlay_fps(overlay);
	view->frame_ms = view->fps > 0.0f ? 1000.0f / view->fps : 0.0f;
	/* the 1 percent low: the frame rate at the 99th percentile frame time of the history (the worst of a hundred) */
	if (overlay->count >= 20)
	{
		memcpy(sorted, overlay->frame, sizeof(float) * overlay->count);
		qsort(sorted, overlay->count, sizeof(float), compare_floats);
		view->low_fps = sorted[(overlay->count * 99) / 100 < overlay->count ? (overlay->count * 99) / 100 : overlay->count - 1] > 0.0f ?
			1000.0f / sorted[(overlay->count * 99) / 100 < overlay->count ? (overlay->count * 99) / 100 : overlay->count - 1] : 0.0f;
	}
	else
		view->low_fps = view->fps;
	view->cpu_ms = overlay->cpu_average;
	view->cpu_percent = percent_of(overlay->cpu_average, overlay->frame_average);
	view->gpu_ms = overlay->gpu_average;
	view->gpu_percent = percent_of(overlay->gpu_average, overlay->frame_average);
	view->draws = overlay->draws;
	view->ring_kb = overlay->ring_kb;
	view->render_width = overlay->render_width;
	view->render_height = overlay->render_height;
	view->interpolation = overlay->interpolation;
}

/* ---------- drawing */

static void put_pixel(uint8_t *pixels, int x, int y, unsigned r, unsigned g, unsigned b, unsigned a)
{
	uint8_t *p;

	if (x < 0 || y < 0 || x >= OVERLAY_WIDTH || y >= OVERLAY_HEIGHT)
		return;
	p = pixels + ((size_t)y * OVERLAY_WIDTH + (unsigned)x) * 4;
	p[0] = (uint8_t)r;
	p[1] = (uint8_t)g;
	p[2] = (uint8_t)b;
	p[3] = (uint8_t)a;
}

static void fill_rectangle(uint8_t *pixels, int x, int y, int width, int height, const uint8_t color[4])
{
	int i, j;

	for (j = 0; j < height; j++)
		for (i = 0; i < width; i++)
			put_pixel(pixels, x + i, y + j, color[0], color[1], color[2], color[3]);
}

/* text in the font's 3 x 5 glyphs, `scale` pixels to a font pixel, a cell 4 x 6 font pixels; returns the x after it */
static int draw_text(uint8_t *pixels, int x, int y, int scale, const uint8_t color[4], const char *text)
{
	for (; *text; text++, x += FONT_CELL_W * scale)
	{
		uint8_t rows[FONT_GLYPH_H];
		int gx, gy;

		font_glyph(*text, rows);
		for (gy = 0; gy < FONT_GLYPH_H; gy++)
			for (gx = 0; gx < FONT_GLYPH_W; gx++)
				if (rows[gy] & (1u << (FONT_GLYPH_W - 1 - gx)))
					fill_rectangle(pixels, x + gx * scale, y + gy * scale, scale, scale, color);
	}
	return x;
}

static const uint8_t white[4] = { 0xf0, 0xf0, 0xf0, 0xff };
static const uint8_t grey[4] = { 0x9a, 0x9a, 0x9a, 0xff };
static const uint8_t track[4] = { 0x30, 0x30, 0x30, 0xff };
static const uint8_t ground[4] = { 0x00, 0x00, 0x00, 0xb8 };

static void level_color(float percent, uint8_t color[4])
{
	/* green below 70 percent of a frame, yellow below 90, red above */
	color[0] = percent < 70.0f ? 0x50 : 0xe8;
	color[1] = percent < 70.0f ? 0xd0 : percent < 90.0f ? 0xc8 : 0x48;
	color[2] = percent < 70.0f ? 0x58 : 0x38;
	color[3] = 0xff;
}

/* a labelled bar of how busy something is: LABEL, a track of 96 px with the filled part, the percentage and the milliseconds, in fixed places */
static void draw_bar(uint8_t *pixels, int y, const char *label, float percent, float ms)
{
	char text[16];
	uint8_t color[4];
	const int fill = (int)(percent * 96.0f / 100.0f + 0.5f);

	draw_text(pixels, 3, y, 1, grey, label);
	fill_rectangle(pixels, 20, y, 96, 5, track);
	level_color(percent, color);
	fill_rectangle(pixels, 20, y, fill, 5, color);
	snprintf(text, sizeof(text), "%3.0f%%", (double)percent);
	draw_text(pixels, 121, y, 1, white, text);
	snprintf(text, sizeof(text), "%4.1f MS", (double)ms);
	draw_text(pixels, 145, y, 1, grey, text);
}

void overlay_render(const struct overlay *overlay, uint8_t *pixels)
{
	const struct overlay_view *view = &overlay->view;
	uint8_t color[4];
	char text[32];
	unsigned x;
	int graph_top = 33, rows = 16;
	unsigned frames = overlay->count < OVERLAY_HISTORY ? overlay->count : OVERLAY_HISTORY;
	int fps_whole = (int)(view->fps + 0.5f);

	/* the ground, and a line under the top rows */
	fill_rectangle(pixels, 0, 0, OVERLAY_WIDTH, OVERLAY_HEIGHT, ground);

	/* the frame rate, big (two pixels to a font pixel): green at 55 or more, yellow at 28 or more, red below; three digits in a fixed place */
	color[0] = view->fps >= 55.0f ? 0x60 : 0xff;
	color[1] = view->fps >= 28.0f ? 0xff : 0x50;
	color[2] = view->fps >= 55.0f ? 0x60 : view->fps >= 28.0f ? 0x40 : 0x50;
	color[3] = 0xff;
	snprintf(text, sizeof(text), "%3d", fps_whole > 999 ? 999 : fps_whole);
	draw_text(pixels, 3, 3, 2, color, text);
	draw_text(pixels, 3 + 3 * FONT_CELL_W * 2, 8, 1, grey, "FPS");

	/* the frame time, the 1 percent low, and the game's own numbers, in two small lines each side */
	snprintf(text, sizeof(text), "FRAME %5.1f MS", (double)view->frame_ms);
	draw_text(pixels, 58, 3, 1, white, text);
	snprintf(text, sizeof(text), "1%% LOW %3.0f FPS", (double)view->low_fps);
	draw_text(pixels, 58, 10, 1, white, text);
	snprintf(text, sizeof(text), "DRAWS %4u", view->draws > 9999 ? 9999u : view->draws);
	draw_text(pixels, 150, 3, 1, grey, text);
	snprintf(text, sizeof(text), "%4ux%-4u %s", view->render_width > 9999 ? 9999u : view->render_width, view->render_height > 9999 ? 9999u : view->render_height,
		view->interpolation ? "60HZ" : "30HZ");
	draw_text(pixels, 150, 10, 1, grey, text);

	/* how busy the CPU (the game's thread) and the GPU are, in the frame's time */
	draw_bar(pixels, 19, "CPU", view->cpu_percent, view->cpu_ms);
	draw_bar(pixels, 26, "GPU", view->gpu_percent, view->gpu_ms);

	/* the graph: a bar a frame, the newest at the right, 3 ms a row (the top is 48 ms); the dotted lines are 60 a second (16.7 ms) and 30 (33.3) */
	for (x = 0; x < OVERLAY_HISTORY; x++)
	{
		const int has = (OVERLAY_HISTORY - 1 - x) < frames;
		const float ms = has ? frame_back(overlay, OVERLAY_HISTORY - 1 - x) : 0.0f;
		int height = (int)(ms / 3.0f + 0.5f), row;

		if (height > rows)
			height = rows;
		for (row = 0; row < rows; row++)
		{
			const int y = graph_top + rows - 1 - row;

			if (row < height)
			{
				if (ms <= 17.5f)
					put_pixel(pixels, 2 + (int)x, y, 0x48, 0xd0, 0x50, 255);
				else if (ms <= 34.0f)
					put_pixel(pixels, 2 + (int)x, y, 0xe8, 0xc4, 0x38, 255);
				else
					put_pixel(pixels, 2 + (int)x, y, 0xe8, 0x48, 0x40, 255);
			}
			else if ((row == 5 || row == 11) && (x & 3) < 2)
				put_pixel(pixels, 2 + (int)x, y, 0x90, 0x90, 0x90, 255);
		}
	}
}
