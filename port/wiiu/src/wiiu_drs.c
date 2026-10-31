/*
WIIU_DRS.C

Dynamic resolution's controller (wiiu_drs.h): it lowers the screen's lines a step when the GPU is behind, and raises them again when it has
room. wiiu_gx2.c runs it at each present on the GX2 thread; the game thread takes up its step between frames (d3d8_gx2.c's halo_screen_commit).

The GPU's time for a frame (wiiu_gx2.c's bottom-of-pipe samples) includes the moments the GPU waited for commands still being made, so it reads
high whenever the game thread is late, and on its own would lower the lines for frames the CPU held back. A wait in present() for the display's
flip is what shows that the GPU is behind: the frame before is not yet shown when the next one is handed over. So a step is dropped only when
the average of the last 8 frames' GPU time is over 0.9 of the frame's budget and the frame waited more than 1 ms for the flip. A step is raised
when the time predicted at the step above (the time now, times the ratio of their pixels) fits in 0.75 of the budget, or when 120 frames in a
row have not waited for the flip (the GPU keeps up, whatever its time reads). After any change no other is made for 30 frames: the new step's
frames are measured a frame or two after the game thread takes it up, and the average needs 8 of them. None is raised for 3 s after a drop,
so that a scene at the edge of the budget does not swing between two steps every half second. While a menu has the screen the controller is
off: the menus are drawn at the ceiling, and their frames say nothing of the game's.

This is the method Intel published for dynamic resolution (one set of targets at the largest size, a viewport and texture coordinates scaled
to the part drawn), with the knobs Unreal's controller has (the share of the budget to drop and to rise at, the frames to hold).
*/

#include "wiiu_drs.h"

/* the share of the ceiling's lines each step draws */
static const float step_fraction[WIIU_DRS_STEPS] = { 1.0f, 0.9f, 0.8f, 0.75f };

/* the share of the budget an average over which drops a step (with a wait for the flip) */
#define DROP_SHARE 0.9f
/* the share of the budget a predicted time within which raises a step */
#define RISE_SHARE 0.75f
/* a wait for the flip longer than this: the GPU is behind */
#define FLIP_WAIT_MS 1.0f
/* the frames after a change before another */
#define HOLD_FRAMES 30u
/* the time after a drop before a rise */
#define DROP_HOLD_MS 3000.0f
/* the frames in a row without a wait for the flip that raise a step */
#define CALM_FRAMES 120u

unsigned wiiu_drs_lines(const struct wiiu_drs *drs, unsigned step)
{
	if (step >= WIIU_DRS_STEPS)
		step = WIIU_DRS_STEPS - 1;
	return (unsigned)((float)drs->ceiling * step_fraction[step] + 0.5f);
}

void wiiu_drs_reset(struct wiiu_drs *drs, unsigned ceiling, unsigned floor)
{
	unsigned step;

	drs->ceiling = ceiling;
	drs->lowest = 0;
	for (step = 1; step < WIIU_DRS_STEPS; step++)
	{
		if (wiiu_drs_lines(drs, step) >= floor)
			drs->lowest = step;
	}
	drs->step = 0;
	drs->gpu_count = drs->gpu_next = 0;
	/* (the first frames, a map's loading and its first draws, are not the game's) */
	drs->hold = HOLD_FRAMES;
	drs->since_drop_ms = DROP_HOLD_MS;
	drs->calm = 0;
	drs->drops = drs->rises = 0;
}

/* a new step: the measures were of the one before, and the frames that follow are held */
static void change(struct wiiu_drs *drs, unsigned step)
{
	drs->step = step;
	drs->gpu_count = drs->gpu_next = 0;
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

unsigned wiiu_drs_frame(struct wiiu_drs *drs, const struct wiiu_drs_frame *frame)
{
	float average = -1.0f;

	drs->since_drop_ms += frame->frame_ms;
	if (frame->menu)
	{
		/* (the step the game had is kept for when it has the screen again, and measured anew then) */
		drs->gpu_count = drs->gpu_next = 0;
		drs->hold = HOLD_FRAMES;
		drs->calm = 0;
		return 0;
	}
	/* (a frame drawn at another step, before the game thread took up the last change, is no measure of this one) */
	if (frame->gpu_lines && frame->gpu_lines == wiiu_drs_lines(drs, drs->step))
	{
		drs->gpu[drs->gpu_next] = frame->gpu_ms;
		drs->gpu_next = (drs->gpu_next + 1) % WIIU_DRS_AVERAGED;
		if (drs->gpu_count < WIIU_DRS_AVERAGED)
			drs->gpu_count++;
	}
	drs->calm = frame->flip_wait_ms > FLIP_WAIT_MS ? 0 : drs->calm + 1;
	if (drs->hold)
	{
		drs->hold--;
		return drs->step;
	}
	/* (the average of 8 frames, or none: a step is not dropped or raised by its prediction on fewer) */
	if (drs->gpu_count == WIIU_DRS_AVERAGED)
		average = wiiu_drs_average(drs);
	if (drs->step < drs->lowest && average > DROP_SHARE * frame->budget_ms && frame->flip_wait_ms > FLIP_WAIT_MS)
	{
		change(drs, drs->step + 1);
		drs->since_drop_ms = 0.0f;
		drs->drops++;
	}
	else if (drs->step > 0 && drs->since_drop_ms >= DROP_HOLD_MS)
	{
		/* (the time at the step above, if it is the pixels that take it) */
		const float ratio = step_fraction[drs->step - 1] / step_fraction[drs->step];

		if ((average >= 0.0f && average * ratio * ratio <= RISE_SHARE * frame->budget_ms) || drs->calm >= CALM_FRAMES)
		{
			change(drs, drs->step - 1);
			drs->rises++;
		}
	}
	return drs->step;
}
