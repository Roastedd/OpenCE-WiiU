#include "wiiu_overlay.h"

#include "wiiu_font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned overlay_scale(unsigned screen_width, unsigned preferred)
{
	unsigned margin = screen_width / 50 + (screen_width % 50 != 0);
	unsigned available = screen_width - margin;
	unsigned fit = available / OVERLAY_WIDTH;

	if (!fit)
		fit = 1;
	if (!preferred)
		preferred = 1;
	return preferred < fit ? preferred : fit;
}

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

float overlay_percentile(const struct overlay *overlay, unsigned percent)
{
	float sorted[OVERLAY_HISTORY];
	unsigned at;

	if (!overlay->count)
		return 0.0f;
	memcpy(sorted, overlay->frame, sizeof(float) * overlay->count);
	qsort(sorted, overlay->count, sizeof(float), compare_floats);
	at = (overlay->count * (percent > 100 ? 100 : percent)) / 100;
	return sorted[at < overlay->count ? at : overlay->count - 1];
}

static float percent_of(float part_ms, float whole_ms)
{
	float percent;

	if (whole_ms <= 0.0f)
		return 0.0f;
	percent = 100.0f * part_ms / whole_ms;
	return percent;
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
static const uint8_t ground[4] = { 0x00, 0x00, 0x00, 0xb8 };

/* Keep a fixed-width field when a long pause produces a large interval. */
static float short_interval(float ms)
{
	return ms < 0.0f ? 0.0f : ms > 99.9f ? 99.9f : ms;
}

static float short_percent(float percent)
{
	return percent < 0.0f ? 0.0f : percent > 999.0f ? 999.0f : percent;
}

void overlay_render(const struct overlay *overlay, uint8_t *pixels)
{
	const struct overlay_view *view = &overlay->view;
	uint8_t color[4];
	char text[32];
	unsigned x;
	const unsigned graph_x = 220, graph_width = OVERLAY_WIDTH - graph_x - 1;
	const int fps = view->fps < 0.0f ? 0 : view->fps > 999.0f ? 999 : (int)(view->fps + 0.5f);
	const int low = view->low_fps < 0.0f ? 0 : view->low_fps > 999.0f ? 999 : (int)(view->low_fps + 0.5f);

	fill_rectangle(pixels, 0, 0, OVERLAY_WIDTH, OVERLAY_HEIGHT, ground);
	color[0] = view->fps >= 55.0f ? 0x60 : 0xff;
	color[1] = view->fps >= 28.0f ? 0xff : 0x50;
	color[2] = view->fps >= 55.0f ? 0x60 : view->fps >= 28.0f ? 0x40 : 0x50;
	color[3] = 0xff;
	snprintf(text, sizeof(text), "FPS%3d", fps);
	draw_text(pixels, 2, 1, 1, color, text);
	snprintf(text, sizeof(text), "F%4.1f", (double)short_interval(view->frame_ms));
	draw_text(pixels, 30, 1, 1, white, text);
	snprintf(text, sizeof(text), "L%3d", low);
	draw_text(pixels, 55, 1, 1, white, text);
	/* Percentages of the frame's time: C the CPU's side of a frame, an elapsed interval; G the GPU's own time for the frame's draws (its clock, wiiu_gx2.c
	gpu_poll), at most what it took. */
	draw_text(pixels, 74, 1, 1, grey, "BUD");
	snprintf(text, sizeof(text), "C%3.0f%%", (double)short_percent(view->cpu_percent));
	draw_text(pixels, 88, 1, 1, grey, text);
	if (view->gpu_ms > 0.0f)
		snprintf(text, sizeof(text), "G%3.0f%%", (double)short_percent(view->gpu_percent));
	else
		snprintf(text, sizeof(text), "G --%%");
	draw_text(pixels, 111, 1, 1, grey, text);
	snprintf(text, sizeof(text), "D%4u", view->draws > 9999 ? 9999u : view->draws);
	draw_text(pixels, 136, 1, 1, grey, text);
	snprintf(text, sizeof(text), "%ux%u", view->render_width > 9999 ? 9999u : view->render_width,
		view->render_height > 9999 ? 9999u : view->render_height);
	draw_text(pixels, 159, 1, 1, grey, text);
	draw_text(pixels, 198, 1, 1, grey, view->interpolation ? "60HZ" : "30HZ");

	/* Preserve the whole history in the short graph: each column shows the
	worst frame in its group, newest at the right. Each row is 10 ms. */
	for (x = 0; x < graph_width; x++)
	{
		const unsigned first = (graph_width - 1 - x) * OVERLAY_HISTORY / graph_width;
		const unsigned end = (graph_width - x) * OVERLAY_HISTORY / graph_width;
		float ms = 0.0f;
		unsigned back, row;
		int height;

		for (back = first; back < end && back < overlay->count; back++)
			if (frame_back(overlay, back) > ms)
				ms = frame_back(overlay, back);
		height = (int)(ms / 10.0f + 0.5f);
		if (height > 5)
			height = 5;
		for (row = 0; row < (unsigned)height; row++)
			put_pixel(pixels, (int)(graph_x + x), 5 - (int)row,
				ms <= 17.5f ? 0x48 : 0xe8, ms <= 17.5f ? 0xd0 : ms <= 34.0f ? 0xc4 : 0x48,
				ms <= 17.5f ? 0x50 : ms <= 34.0f ? 0x38 : 0x40, 255);
	}
}

/* ---------- the modes */

int overlay_mode_next(int mode)
{
	return (mode + 1) % OVERLAY_MODE_COUNT;
}

/* halo/overlay.txt's number: 0 off, 1 the compact strip, 2 the detailed panel; anything else is the compact one if it is above 0 (as 1 was the only on) and off if not */
int overlay_mode_from_file(int number)
{
	if (number <= 0)
		return OVERLAY_MODE_OFF;
	return number == 2 ? OVERLAY_MODE_DETAIL : OVERLAY_MODE_COMPACT;
}
