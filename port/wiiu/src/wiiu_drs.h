/*
WIIU_DRS.H

Dynamic resolution's controller (wiiu_drs.c): from what each frame measured, the step of the screen's lines that the next frames are drawn at.
The screen's render targets keep the size of the most lines, display.render_lines (the ceiling); a lower step draws into their top left part,
and d3d8_gx2.c scales viewports and texture coordinates to it. The controller is a function of its state and of the frame's measures alone,
so that a host test (tools/harness/tests/test_wiiu_drs.py) can run it on frames made up for it.
*/

#ifndef WIIU_DRS_H
#define WIIU_DRS_H

/* the steps: 1, 0.9, 0.8 and 0.75 of the ceiling's lines (720, 648, 576 and 540 of 720; 1080, 972, 864 and 810 of 1080) */
#define WIIU_DRS_STEPS 4
/* the frames the GPU's time is averaged over */
#define WIIU_DRS_AVERAGED 8

/* what a frame's present measured */
struct wiiu_drs_frame
{
	/* the GPU's time for a frame's draws (wiiu_gx2.c's bottom-of-pipe samples, read a frame or two after that frame), and the lines that frame
	was drawn at; gpu_lines 0: no frame's time was read since the last */
	float gpu_ms;
	unsigned gpu_lines;
	/* how long present() waited for the display's flip of the frame before */
	float flip_wait_ms;
	/* the time since the frame before */
	float frame_ms;
	/* the time a frame has: 16.7 ms at 60 frames a second, 33.3 at 30 (display.interpolation false) */
	float budget_ms;
	/* a menu has the screen */
	int menu;
};

struct wiiu_drs
{
	unsigned ceiling; /* the lines of step 0 */
	unsigned lowest; /* the lowest step allowed (display.dynamic_resolution_floor) */
	unsigned step; /* the step decided, which the game draws at a frame or two later */
	float gpu[WIIU_DRS_AVERAGED]; /* the GPU's latest times at that step */
	unsigned gpu_count, gpu_next;
	unsigned hold; /* frames until another change may be made */
	float since_drop_ms; /* the time since the last drop */
	unsigned calm; /* frames in a row that did not wait for the flip */
	unsigned drops, rises; /* the changes made */
};

/* the controller for the ceiling's lines and the fewest lines allowed: at step 0, held for its first frames */
void wiiu_drs_reset(struct wiiu_drs *drs, unsigned ceiling, unsigned floor);
/* the lines a step draws */
unsigned wiiu_drs_lines(const struct wiiu_drs *drs, unsigned step);
/* a frame's measures: the step the next frames are to be drawn at (0 while a menu has the screen) */
unsigned wiiu_drs_frame(struct wiiu_drs *drs, const struct wiiu_drs_frame *frame);
/* the average of the GPU's times measured at the step (for the log), or -1 if none has been */
float wiiu_drs_average(const struct wiiu_drs *drs);

#endif
