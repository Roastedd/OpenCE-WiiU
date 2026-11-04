/* wiiu_overlay_detail.c: the detailed performance panel (halo/overlay.txt 2). Draws a perf_view (wiiu_perf.h) into an RGBA buffer, in the overlay's 3 x 5 font and
its colours: grouped lines, a frame-time bar on a 16.7 / 22.2 / 33.3 ms scale, and the memory pools with meters. Drawn about four times a second, so it is
written to be plain and not clever; its cost is measured in the test (a few tenths of a millisecond a draw, four draws a second). */

#include "wiiu_overlay.h"

#include "wiiu_font.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct canvas
{
	uint8_t *pixels;
	int width, height;
} canvas;

typedef struct color
{
	uint8_t c[4];
} color;

/* the compact strip's colours, and the few more the bar and meters need */
static const color white = { { 0xf0, 0xf0, 0xf0, 0xff } };
static const color grey = { { 0x9a, 0x9a, 0x9a, 0xff } };
static const color dim = { { 0x5a, 0x5a, 0x5a, 0xff } };
static const color ground = { { 0x00, 0x00, 0x00, 0xb8 } };
static const color track = { { 0x2c, 0x2c, 0x2c, 0xff } };
static const color green = { { 0x48, 0xd0, 0x50, 0xff } };
static const color amber = { { 0xe8, 0xc4, 0x38, 0xff } };
static const color red = { { 0xe8, 0x48, 0x40, 0xff } };
static const color blue = { { 0x48, 0x90, 0xe8, 0xff } };
static const color slate = { { 0x8a, 0x96, 0xa0, 0xff } };
static const color pale_green = { { 0xa8, 0xf0, 0xac, 0xff } };

static void fill_rect(canvas *canvas, int x, int y, int w, int h, color color)
{
	uint32_t word;
	int i, j;

	if (x < 0)
	{
		w += x;
		x = 0;
	}
	if (y < 0)
	{
		h += y;
		y = 0;
	}
	if (x + w > canvas->width)
		w = canvas->width - x;
	if (y + h > canvas->height)
		h = canvas->height - y;
	if (w <= 0 || h <= 0)
		return;
	memcpy(&word, color.c, 4); /* (a word in the bytes' own order, so that it is right on either endianness) */
	for (j = 0; j < h; j++)
	{
		uint32_t *row = (uint32_t *)(void *)(canvas->pixels + ((size_t)(y + j) * (size_t)canvas->width + (size_t)x) * 4u);

		for (i = 0; i < w; i++)
			row[i] = word;
	}
}

/* text, 4 pixels to a character; returns the x after it */
static int draw_text(canvas *canvas, int x, int y, color color, const char *text)
{
	uint32_t word;

	memcpy(&word, color.c, 4);
	for (; *text; text++, x += FONT_CELL_W)
	{
		uint8_t rows[FONT_GLYPH_H];
		int gx, gy;

		if (*text == ' ')
			continue;
		font_glyph(*text, rows);
		if (x < 0 || x + FONT_GLYPH_W > canvas->width || y < 0 || y + FONT_GLYPH_H > canvas->height)
			continue;
		for (gy = 0; gy < FONT_GLYPH_H; gy++)
		{
			uint32_t *row = (uint32_t *)(void *)(canvas->pixels + ((size_t)(y + gy) * (size_t)canvas->width + (size_t)x) * 4u);

			for (gx = 0; gx < FONT_GLYPH_W; gx++)
				if (rows[gy] & (1u << (FONT_GLYPH_W - 1 - gx)))
					row[gx] = word;
		}
	}
	return x;
}

/* formatted text at *x, which moves past it */
static void put(canvas *canvas, int *x, int y, color color, const char *format, ...)
{
	char text[64];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	*x = draw_text(canvas, *x, y, color, text);
}

/* a number that keeps its width: milliseconds to 99.9 */
static double ms_field(float ms)
{
	return ms < 0.0f ? 0.0 : ms > 99.9f ? 99.9 : (double)ms;
}

/* the frame's colour as the compact strip's graph has it */
static color frame_color(float ms)
{
	return ms <= 17.5f ? green : ms <= 34.0f ? amber : red;
}

static color level_color(int level)
{
	return level == PERF_LEVEL_RED ? red : level == PERF_LEVEL_AMBER ? amber : green;
}

/* ---------- the frame-time bar */

#define BAR_X 3
#define BAR_WIDTH 250
#define BAR_SCALE_MS 33.3f

static int bar_x(float ms)
{
	int x;

	if (ms < 0.0f)
		ms = 0.0f;
	if (ms > BAR_SCALE_MS)
		ms = BAR_SCALE_MS;
	x = (int)(ms * (float)BAR_WIDTH / BAR_SCALE_MS + 0.5f);
	return x > BAR_WIDTH ? BAR_WIDTH : x;
}

static void draw_bar(canvas *canvas, const struct perf_view *view, int y)
{
	const int height = 8;
	float used = 0.0f;
	int x, end;
	/* the frame's time as the game thread spent it: its work, its waits for the GX2 thread, the display's; what is left of the frame is nothing of those (the tick
	thread's wait is in the work) */
	const float parts[3] = { view->work_ms, view->gpu_wait_ms, view->vsync_ms };
	const color colors[3] = { green, blue, slate };
	int part;

	fill_rect(canvas, BAR_X, y, BAR_WIDTH, height, track);
	for (part = 0; part < 3; part++)
	{
		const int from = bar_x(used), to = bar_x(used + parts[part]);

		if (to > from)
			fill_rect(canvas, BAR_X + from, y, to - from, height, colors[part]);
		used += parts[part];
	}
	/* (what the parts do not add to, up to the frame: the part of the frame that the game thread's clocks do not hold, such as the GX2 thread's own wait) */
	end = bar_x(view->frame_ms);
	if (bar_x(used) < end)
		fill_rect(canvas, BAR_X + bar_x(used), y + height - 2, end - bar_x(used), 2, dim);
	/* the 16.7, 22.2 and 33.3 ms marks across the bar, and the frame's end */
	{
		static const float marks[3] = { 1000.0f / 60.0f, 1000.0f / 45.0f, 1000.0f / 30.0f };
		const color mark_colors[3] = { white, amber, red };
		static const char *const names[3] = { "16.7", "22.2", "33.3" };
		int i;

		for (i = 0; i < 3; i++)
		{
			const int at = BAR_X + bar_x(marks[i]) - (i == 2 ? 1 : 0);

			fill_rect(canvas, at, y - 2, 1, height + 4, mark_colors[i]);
			draw_text(canvas, i == 2 ? at - 15 : at - 7, y + height + 3, mark_colors[i], names[i]);
		}
	}
	/* the frame's own end, and the worst frame of the window as a notch above the bar */
	fill_rect(canvas, BAR_X + end - 1 > BAR_X ? BAR_X + end - 1 : BAR_X, y, 2, height, white);
	x = BAR_X + bar_x(view->worst_ms);
	fill_rect(canvas, x - 1, y - 4, 3, 2, frame_color(view->worst_ms));
	fill_rect(canvas, x, y - 2, 1, 1, frame_color(view->worst_ms));
}

/* ---------- the pools */

static void draw_pool(canvas *canvas, const struct perf_view *view, int pool, int y)
{
	const struct perf_pool *p = &view->pool[pool];
	char used[16], capacity[16], rest[16];
	int x = 3, meter_x = 112, meter_w = 96;

	draw_text(canvas, x, y, grey, perf_pool_name(pool));
	if (!p->known)
	{
		draw_text(canvas, 3 + 6 * FONT_CELL_W, y, dim, "NOT READ");
		fill_rect(canvas, meter_x, y, meter_w, 5, track);
		return;
	}
	perf_mib_text(used, sizeof(used), p->used_kib);
	perf_mib_text(capacity, sizeof(capacity), p->capacity_kib);
	x = 3 + 5 * FONT_CELL_W;
	if (pool == PERF_POOL_MEM2)
	{
		/* (the heap's free memory is what matters: the text says it, the meter says how much of the heap is gone) */
		perf_mib_text(rest, sizeof(rest), p->capacity_kib > p->used_kib ? p->capacity_kib - p->used_kib : 0);
		put(canvas, &x, y, white, "%7s", rest);
		put(canvas, &x, y, grey, " / ");
		put(canvas, &x, y, white, "%6s", capacity);
		put(canvas, &x, y, grey, " FREE");
	}
	else
	{
		put(canvas, &x, y, white, "%7s", used);
		put(canvas, &x, y, grey, " / ");
		put(canvas, &x, y, white, "%6s", capacity);
		put(canvas, &x, y, grey, " MIB");
	}
	{
		const int level = perf_pool_level(p);
		const float percent = perf_pool_percent(p);
		int filled = (int)(percent * (float)meter_w / 100.0f + 0.5f);
		int hot = p->hot_kib && p->capacity_kib ? (int)((double)p->hot_kib * (double)meter_w / (double)p->capacity_kib + 0.5) : 0;
		int shown;

		if (filled > meter_w)
			filled = meter_w;
		if (filled < 1 && p->used_kib)
			filled = 1;
		if (hot > filled)
			hot = filled;
		fill_rect(canvas, meter_x, y, meter_w, 5, track);
		fill_rect(canvas, meter_x, y, filled, 5, level_color(level));
		/* (the part touched in the last frames, where a cache keeps what it last held, is brighter) */
		if (hot > 0 && level == PERF_LEVEL_OK)
			fill_rect(canvas, meter_x, y, hot, 5, pale_green);
		shown = (int)(percent + 0.5f);
		x = meter_x + meter_w + 5;
		put(canvas, &x, y, level == PERF_LEVEL_OK ? white : level_color(level), "%3d%%", shown > 999 ? 999 : shown);
	}
}

/* ---------- the panel */

void overlay_render_detail(const struct perf_view *view, uint8_t *pixels)
{
	canvas canvas = { pixels, OVERLAY_WIDTH, OVERLAY_DETAIL_HEIGHT };
	int x, y, pool;
	char text[24];

	fill_rect(&canvas, 0, 0, OVERLAY_WIDTH, OVERLAY_DETAIL_HEIGHT, ground);
	if (!view->valid)
	{
		draw_text(&canvas, 3, 3, grey, "WAITING FOR THE FIRST WINDOW");
		return;
	}

	/* the frame: its time, the rate, the worst frame of the window */
	x = 3;
	y = 3;
	put(&canvas, &x, y, grey, "FRAME ");
	put(&canvas, &x, y, frame_color(view->frame_ms), "%4.1f", ms_field(view->frame_ms));
	put(&canvas, &x, y, grey, " MS   ");
	put(&canvas, &x, y, view->fps >= 55.0f ? green : view->fps >= 28.0f ? amber : red, "%5.1f", (double)(view->fps > 999.9f ? 999.9f : view->fps));
	put(&canvas, &x, y, grey, " FPS   WORST ");
	put(&canvas, &x, y, frame_color(view->worst_ms), "%5.1f", ms_field(view->worst_ms));
	put(&canvas, &x, y, grey, " MS");

	/* the game thread's time: its work, the GPU's own time for a frame, what it waited for the GX2 thread, the wait for the display */
	x = 3;
	y = 11;
	put(&canvas, &x, y, grey, "WORK ");
	put(&canvas, &x, y, white, "%4.1f", ms_field(view->work_ms));
	put(&canvas, &x, y, grey, "  GPU ");
	if (view->gpu_ms > 0.0f)
		put(&canvas, &x, y, white, "%4.1f", ms_field(view->gpu_ms));
	else
		put(&canvas, &x, y, dim, "  --");
	put(&canvas, &x, y, grey, "  GPU WAIT ");
	put(&canvas, &x, y, view->gpu_wait_ms > 1.0f ? amber : white, "%4.1f", ms_field(view->gpu_wait_ms));
	put(&canvas, &x, y, grey, "  VSYNC ");
	put(&canvas, &x, y, white, "%4.1f", ms_field(view->vsync_ms));

	/* the bar, and its key */
	draw_bar(&canvas, view, 21);
	{
		int kx = 3;
		static const color keys[3] = { { { 0x48, 0xd0, 0x50, 0xff } }, { { 0x48, 0x90, 0xe8, 0xff } }, { { 0x8a, 0x96, 0xa0, 0xff } } };
		static const char *const names[3] = { "WORK", "GPU WAIT", "VSYNC" };
		int key;

		for (key = 0; key < 3; key++)
		{
			fill_rect(&canvas, kx, 33, 3, 3, keys[key]);
			kx = draw_text(&canvas, kx + 5, 32, grey, names[key]) + 4;
		}
	}

	/* the split of the work: the game's ticks (with the wait for the tick thread when the overlap is on), the rendering, the GX2 thread, the audio */
	x = 3;
	y = 41;
	put(&canvas, &x, y, grey, "SIM ");
	put(&canvas, &x, y, white, "%4.1f", ms_field(view->sim_ms));
	put(&canvas, &x, y, grey, "  DRAW ");
	put(&canvas, &x, y, white, "%4.1f", ms_field(view->draw_ms));
	put(&canvas, &x, y, grey, "  GX2 ");
	put(&canvas, &x, y, white, "%4.1f", ms_field(view->gx2_ms));
	put(&canvas, &x, y, grey, "  AUDIO ");
	put(&canvas, &x, y, white, "%4.1f", ms_field(view->audio_ms));
	put(&canvas, &x, y, grey, " MS");

	/* the tick thread, and what the overlap is doing */
	x = 3;
	y = 49;
	put(&canvas, &x, y, grey, "TICK THREAD ");
	if (view->overlap)
	{
		put(&canvas, &x, y, white, "%4.1f", ms_field(view->tick_thread_ms));
		put(&canvas, &x, y, grey, " MS ");
		put(&canvas, &x, y, view->tick_thread_busy_percent > 85.0f ? amber : white, "%3.0f%%", (double)(view->tick_thread_busy_percent > 999.0f ? 999.0f : view->tick_thread_busy_percent));
		put(&canvas, &x, y, grey, " WAIT ");
		put(&canvas, &x, y, view->sim_wait_ms > 1.0f ? amber : white, "%4.1f", ms_field(view->sim_wait_ms));
	}
	else
		put(&canvas, &x, y, dim, " --");
	put(&canvas, &x, y, grey, "  OVERLAP ");
	put(&canvas, &x, y, view->overlap == 1 ? green : view->overlap == 2 ? amber : dim, "%s", view->overlap == 1 ? "ON" : view->overlap == 2 ? "SERIAL" : "OFF");
	put(&canvas, &x, y, white, " %.0f", (double)(view->ticks_per_second > 999.0f ? 999.0f : view->ticks_per_second));
	put(&canvas, &x, y, grey, " T/S");

	/* the rendering, as its passes: visibility, begin (which waits for room in the GX2 thread's queue), objects, decals, effects, HUD */
	x = 3;
	y = 57;
	put(&canvas, &x, y, grey, "DRAW");
	{
		static const char *const names[6] = { " VIS ", " BEG ", " OBJ ", " DEC ", " FX ", " HUD " };
		int part;

		for (part = 0; part < 6; part++)
		{
			put(&canvas, &x, y, grey, "%s", names[part]);
			put(&canvas, &x, y, white, "%.1f", ms_field(view->draw_part_ms[part]));
		}
	}

	/* what was drawn: the draws a frame, the objects the visibility search found and the ones it left out */
	x = 3;
	y = 65;
	put(&canvas, &x, y, grey, "DRAWS ");
	put(&canvas, &x, y, white, "%.0f", (double)(view->draws > 99999.0f ? 99999.0f : view->draws));
	put(&canvas, &x, y, grey, "  OBJECTS ");
	put(&canvas, &x, y, white, "%.0f", (double)(view->objects_found > 9999.0f ? 9999.0f : view->objects_found));
	put(&canvas, &x, y, grey, " DRAWN ");
	put(&canvas, &x, y, white, "%.0f", (double)(view->objects_culled > 9999.0f ? 9999.0f : view->objects_culled));
	put(&canvas, &x, y, grey, " CULLED");

	/* the resolution, dynamic resolution, the frame rate of the game, the governor if there is one */
	x = 3;
	y = 73;
	snprintf(text, sizeof(text), "%ux%u", view->state.render_width > 9999 ? 9999u : view->state.render_width,
		view->state.render_height > 9999 ? 9999u : view->state.render_height);
	put(&canvas, &x, y, white, "%s", text);
	put(&canvas, &x, y, grey, "  DRS ");
	if (view->state.drs_on)
	{
		put(&canvas, &x, y, white, "%u/%u", view->state.drs_lines > 9999 ? 9999u : view->state.drs_lines,
			view->state.drs_full_lines > 9999 ? 9999u : view->state.drs_full_lines);
		put(&canvas, &x, y, grey, " STEP ");
		put(&canvas, &x, y, white, "%d", view->state.drs_step);
	}
	else
		put(&canvas, &x, y, dim, "OFF");
	put(&canvas, &x, y, white, "  %s", view->state.interpolation ? "60HZ" : "30HZ");
	if (view->state.governor_step >= 0)
	{
		put(&canvas, &x, y, grey, "  GOV ");
		put(&canvas, &x, y, view->state.governor_step > 0 ? amber : white, "%d/%d", view->state.governor_step, view->state.governor_max);
	}

	/* the memory */
	draw_text(&canvas, 3, 81, grey, "MEMORY");
	fill_rect(&canvas, 3, 87, OVERLAY_WIDTH - 6, 1, dim);
	for (pool = 0; pool < PERF_POOL_COUNT; pool++)
		draw_pool(&canvas, view, pool, 91 + pool * 8);
}
