/*
WIIU_DRS.C

Dynamic resolution's controller (wiiu_drs.h): it lowers the screen's lines a step when the GPU is behind, and raises them again when it has
room. wiiu_gx2.c runs it at each present on the GX2 thread; the game thread takes up its step between frames (d3d8_gx2.c's halo_screen_commit).

The GPU's time for a frame (wiiu_gx2.c's bottom-of-pipe samples) is an upper bound of what the GPU did: it includes the moments the GPU waited
for commands still being made. And a GPU that works 17 ms on a frame is not the limit of a game that takes 31 ms to make one: lowering the lines
would cost picture and buy nothing. The console's logs (2,760 windows of 5 s, 2026-10-31 and 11-01) show it: in the a30 fight the GPU's time
reads 16 to 18 ms at 30 to 38 frames a second, with the display holding some frames (1.5 to 6 ms waits for the flip), so that the old rule (the
GPU's time over 0.9 of the budget and a wait for the flip) would have dropped there. What shows that the GPU is the limit is that the GPU's
time explains the frame's interval: in the views that are GPU-bound (c10, b40, a shore of a30: 45 to 50 frames a second) the GPU's time plus
the blits is 0.86 to 1.02 of the interval; in the fights the game thread limits it is 0.59 to 0.85. A wait for the flip alone is no sign: the
display holds nearly every early frame (11 to 16 ms a frame at 60 frames a second).

So a step is dropped only when ALL of these hold, on the frames since the last change:
  - the median of the last 8 frames' GPU time is over 0.9 of the frame's budget (the median, not the average: one frame of 200 ms, a hitch, is
    not eight frames of a slow GPU);
  - over the last 64 frames the GPU's time plus the blits is at least 0.9 of the frames' interval (the GPU is what the interval is made of);
  - the median of the last 8 frames' backlog is over 0.06 of the budget (the part of the GPU's time still to run when the game thread had handed
    over the frame's last draw: a GPU that only waited for a slow game thread has none);
  - the frame waited more than 1 ms for the flip.
A step is raised when the time predicted at the step above (the median now, times the ratio of their pixels) fits in 0.75 of the budget, or when
120 frames in a row have not waited for the flip (the game thread is the limit, whatever the GPU's time reads). After any change no other is
made for 30 frames, and a drop needs 64 frames at the new step: the new step's frames are measured a frame or two after the game thread takes
it up. None is raised for 3 s after a drop, so that a scene at the edge of the budget does not swing between two steps every half second.
While a menu has the screen the controller is off: the menus are drawn at the ceiling, and their frames say nothing of the game's.

This is the method Intel published for dynamic resolution (one set of targets at the largest size, a viewport and texture coordinates scaled
to the part drawn), with the knobs Unreal's controller has (the share of the budget to drop and to rise at, the frames to hold).
*/

#include "wiiu_drs.h"

/* The share of the ceiling's lines each step draws. The lowest is 0.75, so that the HUD's text, whose glyphs are made for the ceiling
(d3d8_gx2.c's halo_screen_pixel_scale), is never drawn more than 1.33 times smaller than they were made. */
static const float step_fraction[WIIU_DRS_STEPS] = { 1.0f, 0.9f, 0.8f, 0.75f };

/* An average GPU time over this share of the budget, with a wait for the flip, drops a step. The rest of the budget is for what the time
leaves out: the blits to the TV and the GamePad, and the swap. */
#define DROP_SHARE 0.9f
/* A time predicted at the step above within this share of the budget raises a step. It is well under DROP_SHARE, so that the step it
rises to is not dropped again at once. */
#define RISE_SHARE 0.75f
/* The share of the budget that the GPU must have been behind by when the frame was handed over, in the median, to drop a step: 1 ms at 60 frames a
second. Only a floor, to tell a GPU that had work left after the game thread's last draw from one that only waited for it: the frame's interval
(below) says whether the GPU is the limit. From the console's logs, with the game thread's rendering time standing in for the time to hand a frame
over: the GPU-bound views had 3 to 8 ms, the views the game thread limits 0 to 2.6. The real measure runs about a stream's worth (1 to 4 ms) under
that stand-in, and the console's 5 s lines give the real figures to tune this by. */
#define BACKLOG_SHARE 0.06f
/* What the GPU does after a frame's last draw and before the screens show it: the blits to the TV and the GamePad, and the swap. 2.5 ms is what the
GPU-bound windows leave between their GPU time and the frame interval (c10 18.2 and 21.2, b40 18.4 and 21.3, a30 18.4 and 22.2), less a little. */
#define BLIT_MS 2.5f
/* The least share of the frames' interval that the GPU's time plus the blits must make for a drop, over UTILISATION_FRAMES frames. The windows of the
logs split at it: 19 GPU-bound windows at 0.86 to 1.02 (18 at 0.9 or over) and 10 windows of a game thread that is the limit at 0.59 to 0.85, with
the GPU's time and a wait for the flip over the old rule in all 29. */
#define UTILISATION_MIN 0.9f
/* A wait for the flip longer than this counts. A few turns of the wait's loop (200 us each) are the flip arriving as the frame is handed
over, not the GPU behind. */
#define FLIP_WAIT_MS 1.0f
/* The frames after a change before another may be made: the new step's frames are measured a frame or two after the game thread takes
it up, and the average needs 8 of them. */
#define HOLD_FRAMES 30u
/* The time after a drop before a rise, so that a scene at the edge of the budget does not swing between two steps. */
#define DROP_HOLD_MS 3000.0f
/* Frames in a row that did not wait for the flip raise a step whatever the GPU's time reads (it reads high while the game thread is
late): with no wait the GPU keeps up. 120 frames are 2 s at 60 frames a second. */
#define CALM_FRAMES 120u

unsigned wiiu_drs_lines(const struct wiiu_drs *drs, unsigned step)
{
	if (step >= WIIU_DRS_STEPS)
		step = WIIU_DRS_STEPS - 1;
	return (unsigned)((float)drs->ceiling * step_fraction[step] + 0.5f);
}

void wiiu_drs_reset(struct wiiu_drs *drs, unsigned ceiling, unsigned fewest)
{
	unsigned step;

	drs->ceiling = ceiling;
	drs->lowest = 0;
	for (step = 1; step < WIIU_DRS_STEPS; step++)
	{
		if (wiiu_drs_lines(drs, step) >= fewest)
			drs->lowest = step;
	}
	drs->step = 0;
	drs->gpu_count = drs->gpu_next = 0;
	drs->busy_count = drs->busy_next = 0;
	/* The first frames, a map's loading and its first draws, are no measure of the game's. */
	drs->hold = HOLD_FRAMES;
	drs->since_drop_ms = DROP_HOLD_MS;
	drs->calm = 0;
	drs->drops = drs->rises = 0;
}

/* A new step. The measures were of the one before, and the frames that follow are held. */
static void change(struct wiiu_drs *drs, unsigned step)
{
	drs->step = step;
	drs->gpu_count = drs->gpu_next = 0;
	drs->busy_count = drs->busy_next = 0;
	drs->hold = HOLD_FRAMES;
	drs->calm = 0;
}

float wiiu_drs_average(const struct wiiu_drs *drs)
{
	float sum = 0.0f;
	unsigned i;

	if (!drs->gpu_count)
		return -1.0f;
	for (i = 0; i < drs->gpu_count; i++)
		sum += drs->gpu[i];
	return sum / (float)drs->gpu_count;
}

/* the median of count values (the mean of the middle two when the count is even); the values are not changed */
static float median_of(const float *values, unsigned count)
{
	float sorted[WIIU_DRS_AVERAGED];
	unsigned i, j;

	for (i = 0; i < count; i++)
	{
		float value = values[i];

		for (j = i; j > 0 && sorted[j - 1] > value; j--)
			sorted[j] = sorted[j - 1];
		sorted[j] = value;
	}
	return count & 1 ? sorted[count / 2] : 0.5f * (sorted[count / 2 - 1] + sorted[count / 2]);
}

float wiiu_drs_median(const struct wiiu_drs *drs)
{
	return drs->gpu_count ? median_of(drs->gpu, drs->gpu_count) : -1.0f;
}

float wiiu_drs_utilisation(const struct wiiu_drs *drs)
{
	float busy = 0.0f, interval = 0.0f;
	unsigned i;

	if (drs->busy_count < WIIU_DRS_UTILISED)
		return -1.0f;
	for (i = 0; i < WIIU_DRS_UTILISED; i++)
	{
		busy += drs->busy[i];
		interval += drs->interval[i];
	}
	return interval > 0.0f ? busy / interval : -1.0f;
}

float wiiu_drs_backlog(const struct wiiu_drs *drs)
{
	unsigned i;

	if (!drs->gpu_count)
		return -1.0f;
	for (i = 0; i < drs->gpu_count; i++)
		if (drs->backlog[i] < 0.0f)
			return -1.0f;
	return median_of(drs->backlog, drs->gpu_count);
}

unsigned wiiu_drs_frame(struct wiiu_drs *drs, const struct wiiu_drs_frame *frame)
{
	float typical = -1.0f, backlog = -1.0f, utilisation = -1.0f;

	drs->since_drop_ms += frame->frame_ms;
	if (frame->menu)
	{
		/* The menus are drawn with every line. The step the game had is kept for when it has the screen again, and measured anew then. */
		drs->gpu_count = drs->gpu_next = 0;
		drs->busy_count = drs->busy_next = 0;
		drs->hold = HOLD_FRAMES;
		drs->calm = 0;
		return 0;
	}
	/* A frame drawn at another step, before the game thread took up the last change, is no measure of this one. */
	if (frame->gpu_lines && frame->gpu_lines == wiiu_drs_lines(drs, drs->step))
	{
		drs->gpu[drs->gpu_next] = frame->gpu_ms;
		drs->backlog[drs->gpu_next] = frame->gpu_backlog_ms;
		drs->gpu_next = (drs->gpu_next + 1) % WIIU_DRS_AVERAGED;
		if (drs->gpu_count < WIIU_DRS_AVERAGED)
			drs->gpu_count++;
		drs->busy[drs->busy_next] = frame->gpu_ms + BLIT_MS;
		drs->interval[drs->busy_next] = frame->frame_ms;
		drs->busy_next = (drs->busy_next + 1) % WIIU_DRS_UTILISED;
		if (drs->busy_count < WIIU_DRS_UTILISED)
			drs->busy_count++;
	}
	drs->calm = frame->flip_wait_ms > FLIP_WAIT_MS ? 0 : drs->calm + 1;
	if (drs->hold)
	{
		drs->hold--;
		return drs->step;
	}
	/* A step is not dropped, nor raised by its prediction, on the measures of fewer than 8 frames. */
	if (drs->gpu_count == WIIU_DRS_AVERAGED)
	{
		typical = wiiu_drs_median(drs);
		backlog = wiiu_drs_backlog(drs);
		utilisation = wiiu_drs_utilisation(drs);
	}
	if (drs->step < drs->lowest && typical > DROP_SHARE * frame->budget_ms && backlog > BACKLOG_SHARE * frame->budget_ms &&
		utilisation >= UTILISATION_MIN && frame->flip_wait_ms > FLIP_WAIT_MS)
	{
		change(drs, drs->step + 1);
		drs->since_drop_ms = 0.0f;
		drs->drops++;
	}
	else if (drs->step > 0 && drs->since_drop_ms >= DROP_HOLD_MS)
	{
		/* The time at the step above, if it is the pixels that take it. */
		const float ratio = step_fraction[drs->step - 1] / step_fraction[drs->step];

		if ((typical >= 0.0f && typical * ratio * ratio <= RISE_SHARE * frame->budget_ms) || drs->calm >= CALM_FRAMES)
		{
			change(drs, drs->step - 1);
			drs->rises++;
		}
	}
	return drs->step;
}
