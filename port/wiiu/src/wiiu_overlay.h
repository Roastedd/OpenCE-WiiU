/* The performance overlay: the frame rate and what a frame costs, as text and a graph of the last frames' times, drawn into an RGBA layer that the GX2 backend puts at the top of
the TV and the GamePad (wiiu_gx2.c). This file is the arithmetic and the drawing into the layer, with no GX2 in it, so that it runs on a computer's tests. */
#ifndef WIIU_OVERLAY_H
#define WIIU_OVERLAY_H

#include <stdint.h>

#define OVERLAY_WIDTH 400
#define OVERLAY_HEIGHT 24
#define OVERLAY_HISTORY OVERLAY_WIDTH
#define OVERLAY_LINE 100

struct overlay
{
	float frame[OVERLAY_HISTORY]; /* ms between presents, the last OVERLAY_HISTORY frames */
	unsigned head, count;
	/* the last frame's parts, and their averages over the last few frames (exponential) */
	float cpu, present, wait, gpu;
	float cpu_average, present_average, wait_average, gpu_average;
	unsigned draws, ring_kb;
	/* what the game draws and how: set by the caller */
	unsigned render_width, render_height;
	int interpolation;
};

void overlay_reset(struct overlay *overlay);
/* a frame was presented: ms from the last present to this one, ms the game thread worked before presenting, ms the present took of which wait ms were for the display, ms the GPU took
(0: not known), draws made, KB copied into the ring */
void overlay_push(struct overlay *overlay, float frame_ms, float cpu_ms, float present_ms, float wait_ms, float gpu_ms, unsigned draws, unsigned ring_kb);
/* the frame rate over the last frames, and the longest frame of the last `window` ones */
float overlay_fps(const struct overlay *overlay);
float overlay_longest(const struct overlay *overlay, unsigned window);
/* the two lines of text */
void overlay_text(const struct overlay *overlay, char line1[OVERLAY_LINE], char line2[OVERLAY_LINE]);
/* draws text and graph into pixels (OVERLAY_WIDTH x OVERLAY_HEIGHT RGBA, rows OVERLAY_WIDTH * 4 bytes apart): a dark ground, the text lit white (the first line green, yellow or red
by the frame rate), and the frame times of the last frames as bars, a dotted line at 16.7 and 33.3 ms */
void overlay_render(const struct overlay *overlay, uint8_t *pixels);

#endif
