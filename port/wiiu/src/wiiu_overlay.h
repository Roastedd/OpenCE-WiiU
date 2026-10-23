/* The performance overlay, laid out as the usual ones are (MangoHud, the consoles' and engines' own stat panels): a big frame rate, the frame time and the 1 percent low, bars of how much of
a frame the CPU and the GPU are busy, a graph of the last frames' times with the 60 and 30 a second lines, and the draw count and resolution. The numbers on the panel are those of the last
quarter of a second (they are taken at that rate so that they can be read, and every field is a fixed width so that nothing moves); the graph is of every frame.
It is drawn into an RGBA layer here (no GX2 in this file, so that it runs on a computer's tests) and put at the top left of the TV and the GamePad by wiiu_gx2.c. */
#ifndef WIIU_OVERLAY_H
#define WIIU_OVERLAY_H

#include <stdint.h>

#define OVERLAY_WIDTH 256
#define OVERLAY_HEIGHT 52
#define OVERLAY_HISTORY 252 /* the graph's width: a frame a column */

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

void overlay_reset(struct overlay *overlay);
/* a frame was presented: ms from the last present to this one, ms the game thread worked before presenting, ms the present took of which wait ms were for the display, ms the GPU took
(0: not known), draws made, KB copied into the ring */
void overlay_push(struct overlay *overlay, float frame_ms, float cpu_ms, float present_ms, float wait_ms, float gpu_ms, unsigned draws, unsigned ring_kb);
/* the frame rate over the last frames, and the longest frame of the last `window` ones */
float overlay_fps(const struct overlay *overlay);
float overlay_longest(const struct overlay *overlay, unsigned window);
/* takes the panel's numbers from the averages (called a few times a second) */
void overlay_update_view(struct overlay *overlay);
/* draws the panel into pixels (OVERLAY_WIDTH x OVERLAY_HEIGHT RGBA, rows OVERLAY_WIDTH * 4 bytes apart) */
void overlay_render(const struct overlay *overlay, uint8_t *pixels);

#endif
