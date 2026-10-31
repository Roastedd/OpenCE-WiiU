"""Dynamic resolution's controller (port/wiiu/src/wiiu_drs.c) on a made-up console: a step is dropped only when the GPU's time is near the budget
and the frame waited for the flip, one at a time with 30 frames between changes; a step is raised when the time predicted at the step above fits
in 0.75 of the budget, or after 120 frames without a wait for the flip, never within 3 s of a drop; a menu holds it at the ceiling; a frame drawn
at the step before is no measure of the new one; the floor and the 30-frames-a-second budget hold. Each fault must be caught."""
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import mutated, read

FAULTS = {
    "kept": None,
    # (a GPU time that reads high because the game thread is late drops a step)
    "no_flip_wait": ("frame->budget_ms && frame->flip_wait_ms > FLIP_WAIT_MS)", "frame->budget_ms)"),
    # (no frames held after a change: the steps change every frame)
    "no_hold": ("\tif (drs->hold)\n", "\tif (0 && drs->hold)\n"),
    # (a step raised as soon as the hold is over after a drop)
    "no_drop_hold": ("drs->since_drop_ms >= DROP_HOLD_MS", "drs->since_drop_ms >= 0.0f"),
    # (the menus' frames run the controller)
    "menu_runs": ("\tif (frame->menu)\n", "\tif (0 && frame->menu)\n"),
    # (the frames drawn at the step before averaged with the new step's)
    "stale_measures": ("if (frame->gpu_lines && frame->gpu_lines == wiiu_drs_lines(drs, drs->step))", "if (frame->gpu_lines)"),
    # (the 60-frames-a-second budget at 30)
    "fixed_budget": ("DROP_SHARE * frame->budget_ms", "DROP_SHARE * 16.7f"),
    # (the time at the step above predicted by its lines, not its pixels)
    "linear_prediction": ("average * ratio * ratio <=", "average * ratio <="),
    # (no rise for a GPU that keeps up but whose time reads high)
    "no_calm_rise": (" || drs->calm >= CALM_FRAMES)", ")"),
    # (every step allowed whatever the floor)
    "no_floor": ("if (wiiu_drs_lines(drs, step) >= floor)", "if (wiiu_drs_lines(drs, step) >= floor || 1)"),
}

FIXTURE = r'''
#include <stdio.h>
#include <string.h>
#include "wiiu_drs.h"

/* A made-up console. The GPU takes base ms a frame at the ceiling, and the share of its pixels at a lower step. The game thread draws a step
LAG frames after it is decided (the stream queue), and a frame's GPU time is read READ_LAG frames after its present. A frame waits for the
flip when the GPU took longer than the budget (it is behind), and vsync_wait ms when it is ahead (the display holds it). read_ms, if set, is
the GPU time read whatever the GPU took: a game thread that is late, which the GPU waited for. frozen: frames for which the game thread
keeps drawing the step it had after the next drop. */
#define LAG 2
#define READ_LAG 2

struct sim
{
	struct wiiu_drs drs;
	unsigned decided[LAG + 1];
	unsigned drawn_lines[READ_LAG + 1];
	float drawn_gpu[READ_LAG + 1];
	unsigned game_step, frozen, freeze_after_drop;
	long frame, last_change, closest, change_frames[8];
	double time_ms, last_drop_ms, quickest_rise_ms;
	unsigned step, drops, rises, changes, menu_steps;
	float budget, base, vsync_wait, read_ms;
	int menu;
};

static void sim_start(struct sim *s, unsigned ceiling, unsigned floor, float budget)
{
	memset(s, 0, sizeof(*s));
	wiiu_drs_reset(&s->drs, ceiling, floor);
	s->budget = budget;
	s->last_change = -1000000;
	s->closest = 1000000;
	s->last_drop_ms = -1e9;
	s->quickest_rise_ms = 1e9;
}

static void sim_run(struct sim *s, long frames)
{
	long n;
	unsigned i;

	for (n = 0; n < frames; n++, s->frame++)
	{
		struct wiiu_drs_frame f;
		unsigned lines, step;
		float share, gpu;

		if (s->frozen)
			s->frozen--;
		else
			s->game_step = s->decided[LAG];
		lines = s->menu ? s->drs.ceiling : wiiu_drs_lines(&s->drs, s->game_step);
		share = (float)lines / (float)s->drs.ceiling;
		gpu = s->base * share * share;
		for (i = READ_LAG; i > 0; i--)
		{
			s->drawn_lines[i] = s->drawn_lines[i - 1];
			s->drawn_gpu[i] = s->drawn_gpu[i - 1];
		}
		s->drawn_lines[0] = lines;
		s->drawn_gpu[0] = gpu;
		memset(&f, 0, sizeof(f));
		f.gpu_lines = s->drawn_lines[READ_LAG];
		f.gpu_ms = s->read_ms > 0.0f ? s->read_ms : s->drawn_gpu[READ_LAG];
		f.flip_wait_ms = gpu > s->budget ? gpu - s->budget + 2.0f : s->vsync_wait;
		f.frame_ms = gpu > s->budget ? gpu : s->budget;
		f.budget_ms = s->budget;
		f.menu = s->menu;
		step = wiiu_drs_frame(&s->drs, &f);
		s->time_ms += f.frame_ms;
		if (s->menu)
			s->menu_steps += step != 0;
		else if (step != s->step)
		{
			if (s->frame - s->last_change < s->closest)
				s->closest = s->frame - s->last_change;
			s->last_change = s->frame;
			if (s->changes < 8)
				s->change_frames[s->changes] = s->frame;
			s->changes++;
			if (step > s->step)
			{
				s->drops++;
				s->last_drop_ms = s->time_ms;
				if (s->freeze_after_drop)
				{
					s->frozen = s->freeze_after_drop;
					s->freeze_after_drop = 0;
				}
			}
			else
			{
				s->rises++;
				if (s->time_ms - s->last_drop_ms < s->quickest_rise_ms)
					s->quickest_rise_ms = s->time_ms - s->last_drop_ms;
			}
			s->step = step;
		}
		for (i = LAG; i > 0; i--)
			s->decided[i] = s->decided[i - 1];
		s->decided[0] = step;
	}
}

int main(void)
{
	struct sim s;

	/* Blood Gulch at 720 lines: 4 ms of the GPU a frame, the display holding each frame: the ceiling throughout */
	sim_start(&s, 720, 540, 16.7f);
	s.base = 4.0f;
	s.vsync_wait = 3.0f;
	sim_run(&s, 1200);
	if (s.step != 0 || s.drops || s.rises)
		return 1;
	/* 18 ms at 720: one step down (14.6 ms at 648, under 0.9 of the budget), and no rise (18 ms predicted at 720) */
	sim_start(&s, 720, 540, 16.7f);
	s.base = 18.0f;
	s.vsync_wait = 3.0f;
	sim_run(&s, 1200);
	if (s.step != 1 || s.drops != 1 || s.rises || s.last_change < 30)
		return 2;
	if (wiiu_drs_lines(&s.drs, 1) != 648 || wiiu_drs_lines(&s.drs, 2) != 576 || wiiu_drs_lines(&s.drs, 3) != 540)
		return 3;
	/* 30 ms: a step at a time to the lowest, 30 frames or more between changes */
	sim_start(&s, 720, 540, 16.7f);
	s.base = 30.0f;
	s.vsync_wait = 3.0f;
	sim_run(&s, 600);
	if (s.step != 3 || s.drops != 3 || s.closest < 30)
		return 4;
	/* then light again: back up a step at a time, the first rise 3 s or more after the last drop */
	s.base = 4.0f;
	sim_run(&s, 600);
	if (s.step != 0 || s.rises != 3 || s.closest < 30 || s.quickest_rise_ms < 3000.0)
		return 5;
	/* a moment of 30 ms, then light: one drop, and its rise held for 3 s */
	sim_start(&s, 720, 540, 16.7f);
	s.base = 30.0f;
	s.vsync_wait = 3.0f;
	sim_run(&s, 45);
	s.base = 4.0f;
	sim_run(&s, 600);
	if (s.drops != 1 || s.rises != 1 || s.step != 0 || s.quickest_rise_ms < 3000.0)
		return 6;
	/* the game thread late: the GPU's time reads 20 ms, but no frame waits for the flip: no drop */
	sim_start(&s, 720, 540, 16.7f);
	s.base = 4.0f;
	s.read_ms = 20.0f;
	sim_run(&s, 1200);
	if (s.step != 0 || s.drops)
		return 7;
	/* down at the lowest step, then the game thread late as above: back up, a step for each 120 frames without a wait for the flip */
	sim_start(&s, 720, 540, 16.7f);
	s.base = 30.0f;
	s.vsync_wait = 3.0f;
	sim_run(&s, 300);
	if (s.step != 3)
		return 8;
	s.base = 4.0f;
	s.read_ms = 20.0f;
	s.vsync_wait = 0.0f;
	sim_run(&s, 700);
	if (s.step != 0 || s.rises != 3)
		return 9;
	/* a menu has the screen: drawn at the ceiling whatever the GPU takes, and nothing decided */
	sim_start(&s, 720, 540, 16.7f);
	s.base = 30.0f;
	s.vsync_wait = 3.0f;
	s.menu = 1;
	sim_run(&s, 600);
	if (s.menu_steps || s.drops || s.drs.step)
		return 10;
	/* the game at the lowest step, then a menu: the menu at the ceiling, and the game back at its step with no change for 30 frames */
	s.menu = 0;
	sim_run(&s, 300);
	if (s.step != 3)
		return 11;
	s.menu = 1;
	sim_run(&s, 100);
	if (s.menu_steps)
		return 12;
	s.menu = 0;
	s.base = 4.0f;
	{
		const long back = s.frame;

		sim_run(&s, 30);
		if (s.step != 3 || s.last_change >= back)
			return 13;
	}
	/* 30 frames a second (a budget of 33.3 ms): 25 ms is within it; 32 ms is over 0.9 of it, and one step down fits */
	sim_start(&s, 1080, 540, 33.3f);
	s.base = 25.0f;
	s.vsync_wait = 2.0f;
	sim_run(&s, 600);
	if (s.step != 0 || s.drops)
		return 14;
	sim_start(&s, 1080, 540, 33.3f);
	s.base = 32.0f;
	s.vsync_wait = 2.0f;
	sim_run(&s, 600);
	if (s.step != 1 || s.drops != 1 || wiiu_drs_lines(&s.drs, 1) != 972 || wiiu_drs_lines(&s.drs, 3) != 810)
		return 15;
	/* the floor: 576 lines of 720 allow two steps down; 810 of 1080, all three; at 480 lines, 540 allows none */
	sim_start(&s, 720, 576, 16.7f);
	s.base = 30.0f;
	s.vsync_wait = 3.0f;
	sim_run(&s, 600);
	if (s.drs.lowest != 2 || s.step != 2)
		return 16;
	sim_start(&s, 1080, 810, 16.7f);
	if (s.drs.lowest != 3)
		return 17;
	sim_start(&s, 480, 540, 16.7f);
	s.base = 30.0f;
	s.vsync_wait = 3.0f;
	sim_run(&s, 600);
	if (s.drs.lowest != 0 || s.step != 0 || s.drops)
		return 18;
	/* a game thread that keeps drawing the old step for 60 frames after a drop: those frames are no measure of the new step, so the next
	drop waits for 8 of its own */
	sim_start(&s, 720, 540, 16.7f);
	s.base = 30.0f;
	s.vsync_wait = 3.0f;
	s.freeze_after_drop = 60;
	sim_run(&s, 600);
	if (s.step != 3 || s.changes != 3 || s.change_frames[1] - s.change_frames[0] < 60 + READ_LAG + WIIU_DRS_AVERAGED - 1)
		return 19;
	/* the time at the step above is predicted by its pixels: 11 ms at 648 lines is 13.6 at 720, over 0.75 of the budget: no rise (the
	display holds every frame, so that no rise comes of 120 frames without a wait); 8.1 ms at 648 is 10 at 720: a rise */
	sim_start(&s, 720, 540, 16.7f);
	s.base = 18.0f;
	s.vsync_wait = 3.0f;
	sim_run(&s, 300);
	if (s.step != 1)
		return 20;
	s.base = 11.0f / 0.81f;
	sim_run(&s, 600);
	if (s.step != 1 || s.rises)
		return 21;
	s.base = 10.0f;
	sim_run(&s, 300);
	if (s.step != 0 || s.rises != 1)
		return 22;
	return 0;
}
'''


@pytest.mark.parametrize("fault", list(FAULTS))
def test_dynamic_resolution_controller(tmp_path, fault):
    source = read("port/wiiu/src/wiiu_drs.c")
    if FAULTS[fault]:
        source = mutated(source, *FAULTS[fault])
    (tmp_path / "wiiu_drs.h").write_text(read("port/wiiu/src/wiiu_drs.h"))
    (tmp_path / "wiiu_drs.c").write_text(source)
    (tmp_path / "check.c").write_text(FIXTURE)
    executable = tmp_path / "check"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-ffp-contract=off", "-I", str(tmp_path), str(tmp_path / "check.c"),
                    str(tmp_path / "wiiu_drs.c"), "-o", str(executable)], check=True)
    result = subprocess.run([str(executable)], check=False)
    if fault == "kept":
        assert result.returncode == 0
    else:
        assert result.returncode != 0
