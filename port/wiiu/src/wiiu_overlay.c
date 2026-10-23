#include "wiiu_overlay.h"

#include "wiiu_font.h"

#include <stdio.h>
#include <string.h>

void overlay_reset(struct overlay *overlay)
{
	memset(overlay, 0, sizeof(*overlay));
}

void overlay_push(struct overlay *overlay, float frame_ms, float cpu_ms, float present_ms, float wait_ms, float gpu_ms, unsigned draws, unsigned ring_kb)
{
	const float keep = overlay->count ? 0.9f : 0.0f; /* (the average moves a tenth of the way to each new frame) */

	overlay->frame[overlay->head] = frame_ms;
	overlay->head = (overlay->head + 1) % OVERLAY_HISTORY;
	if (overlay->count < OVERLAY_HISTORY)
		overlay->count++;
	overlay->cpu = cpu_ms;
	overlay->present = present_ms;
	overlay->wait = wait_ms;
	overlay->cpu_average = overlay->cpu_average * keep + cpu_ms * (1.0f - keep);
	overlay->present_average = overlay->present_average * keep + present_ms * (1.0f - keep);
	overlay->wait_average = overlay->wait_average * keep + wait_ms * (1.0f - keep);
	if (gpu_ms > 0.0f)
	{
		overlay->gpu = gpu_ms;
		overlay->gpu_average = overlay->gpu_average > 0.0f ? overlay->gpu_average * keep + gpu_ms * (1.0f - keep) : gpu_ms;
	}
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

void overlay_text(const struct overlay *overlay, char line1[OVERLAY_LINE], char line2[OVERLAY_LINE])
{
	const float fps = overlay_fps(overlay);

	snprintf(line1, OVERLAY_LINE, "FPS %.1f  FRAME %.1f MS  MAX %.0f  CPU %.1f  GPU %.1f  WAIT %.1f", (double)fps, fps > 0.0f ? (double)(1000.0f / fps) : 0.0,
		(double)overlay_longest(overlay, 120), (double)overlay->cpu_average, (double)overlay->gpu_average, (double)overlay->wait_average);
	snprintf(line2, OVERLAY_LINE, "DRAWS %u  RING %u KB  %ux%u  INTERP %s", overlay->draws, overlay->ring_kb, overlay->render_width, overlay->render_height,
		overlay->interpolation ? "ON" : "OFF");
}

static void put_pixel(uint8_t *pixels, unsigned x, unsigned y, unsigned r, unsigned g, unsigned b, unsigned a)
{
	uint8_t *p = pixels + ((size_t)y * OVERLAY_WIDTH + x) * 4;

	p[0] = (uint8_t)r;
	p[1] = (uint8_t)g;
	p[2] = (uint8_t)b;
	p[3] = (uint8_t)a;
}

void overlay_render(const struct overlay *overlay, uint8_t *pixels)
{
	char line1[OVERLAY_LINE], line2[OVERLAY_LINE];
	text_layer layer;
	const float fps = overlay_fps(overlay);
	unsigned x, y, frames;

	overlay_text(overlay, line1, line2);
	text_layer_init(&layer, pixels, OVERLAY_WIDTH, OVERLAY_HEIGHT, 1);
	text_layer_clear(&layer);
	/* the first line is green at 55 a second or more, yellow at 28 or more, red below */
	layer.color[0] = fps >= 55.0f ? 0x60 : 0xff;
	layer.color[1] = fps >= 28.0f ? 0xff : 0x50;
	layer.color[2] = fps >= 55.0f ? 0x60 : (fps >= 28.0f ? 0x40 : 0x50);
	layer.color[3] = 0xff;
	text_layer_puts(&layer, 0, 0, line1);
	memset(layer.color, 0xff, 4);
	text_layer_puts(&layer, 0, 1, line2);
	/* the ground: dark and mostly opaque where no glyph is lit */
	for (y = 0; y < OVERLAY_HEIGHT; y++)
		for (x = 0; x < OVERLAY_WIDTH; x++)
			if (!pixels[((size_t)y * OVERLAY_WIDTH + x) * 4 + 3])
				put_pixel(pixels, x, y, 0, 0, 0, 176);
	/* the graph: rows 12 to 23, a bar a frame, the newest at the right, 4 ms a row (the top is 48 ms); dotted lines at 16.7 and 33.3 ms */
	frames = overlay->count < OVERLAY_WIDTH ? overlay->count : OVERLAY_WIDTH;
	for (x = 0; x < OVERLAY_WIDTH; x++)
	{
		const int has = (OVERLAY_WIDTH - 1 - x) < frames;
		const float ms = has ? frame_back(overlay, OVERLAY_WIDTH - 1 - x) : 0.0f;
		unsigned height = (unsigned)(ms / 4.0f + 0.5f);
		unsigned row;

		if (height > 12)
			height = 12;
		for (row = 0; row < 12; row++)
		{
			const unsigned yy = 23 - row;

			if (row < height)
			{
				if (ms <= 17.5f)
					put_pixel(pixels, x, yy, 0x40, 0xd0, 0x40, 255);
				else if (ms <= 34.0f)
					put_pixel(pixels, x, yy, 0xe0, 0xc0, 0x30, 255);
				else
					put_pixel(pixels, x, yy, 0xe0, 0x40, 0x40, 255);
			}
			else if ((row == 4 || row == 8) && (x & 3) < 2)
				put_pixel(pixels, x, yy, 0x80, 0x80, 0x80, 255); /* (dotted: 16.7 ms is 4.2 rows up, 33.3 is 8.3) */
		}
	}
}
