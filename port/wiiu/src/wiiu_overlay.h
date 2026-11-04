/* A thin performance strip: frame rate, frame time, 1 percent low, CPU and GPU
intervals as a percentage of frame time, draws, resolution and a compressed
frame-time graph. The intervals include scheduling and polling; they are not
hardware utilization. Percentages over 999 are displayed as 999%; GPU --%
means no timestamp sample is available.
Numbers update four times a second. Drawn at the top of the TV and GamePad. */
#ifndef WIIU_OVERLAY_H
#define WIIU_OVERLAY_H

#include <stdint.h>

#include "wiiu_perf.h"

#define OVERLAY_WIDTH 256
#define OVERLAY_HEIGHT 7
/* the detailed panel (halo/overlay.txt 2): the same width, a taller texture; the compact strip is drawn from the texture's top rows */
#define OVERLAY_DETAIL_HEIGHT 156
#define OVERLAY_HISTORY 252 /* frame-time history, compressed for the graph */

struct overlay_view
{
	float fps, frame_ms, low_fps; /* low_fps: the frame rate of the 99th percentile frame of the last history */
	float cpu_ms, cpu_percent, gpu_ms, gpu_percent;
	unsigned draws, ring_kb, render_width, render_height;
	int interpolation;
};

struct overlay
{
	float frame[OVERLAY_HISTORY]; /* ms between presents, the last OVERLAY_HISTORY frames */
	unsigned head, count;
	/* the parts of the last frames, averaged (exponentially) */
	float cpu_average, present_average, wait_average, gpu_average, frame_average;
	unsigned draws, ring_kb;
	unsigned render_width, render_height;
	int interpolation;
	struct overlay_view view; /* what the panel shows, until overlay_update_view */
};

/* The overlay's modes: halo/overlay.txt's number, and what L + R + - cycles through. */
enum { OVERLAY_MODE_OFF, OVERLAY_MODE_COMPACT, OVERLAY_MODE_DETAIL, OVERLAY_MODE_COUNT };
int overlay_mode_next(int mode);
int overlay_mode_from_file(int number);

/* Whole-pixel scale that fits the strip alongside the two percent left margin. */
unsigned overlay_scale(unsigned screen_width, unsigned preferred);
void overlay_reset(struct overlay *overlay);
/* a frame was presented: ms from the last present to this one, ms the game thread worked before presenting, ms the present took of which wait ms were for the display, ms the GPU took
(0: not known), draws made, KB copied into the ring */
void overlay_push(struct overlay *overlay, float frame_ms, float cpu_ms, float present_ms, float wait_ms, float gpu_ms, unsigned draws, unsigned ring_kb);
/* the frame rate over the last frames, and the longest frame of the last `window` ones */
float overlay_fps(const struct overlay *overlay);
float overlay_longest(const struct overlay *overlay, unsigned window);
/* the frame time at a percentile (0 to 100) of the history: 50 the median, 99 the worst of a hundred; 0 with no frames */
float overlay_percentile(const struct overlay *overlay, unsigned percent);
/* takes the panel's numbers from the averages (called a few times a second) */
void overlay_update_view(struct overlay *overlay);
/* draws the panel into pixels (OVERLAY_WIDTH x OVERLAY_HEIGHT RGBA, rows OVERLAY_WIDTH * 4 bytes apart) */
void overlay_render(const struct overlay *overlay, uint8_t *pixels);
/* draws the detailed panel (wiiu_overlay_detail.c) into pixels (OVERLAY_WIDTH x OVERLAY_DETAIL_HEIGHT RGBA): the window's numbers in grouped lines, the
frame-time bar with its 16.7, 22.2 and 33.3 ms marks, and the memory pools with their meters */
void overlay_render_detail(const struct perf_view *view, uint8_t *pixels);

#endif
