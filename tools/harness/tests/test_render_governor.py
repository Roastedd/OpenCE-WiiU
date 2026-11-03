"""The Wii U's dynamic quality governor (port/linux/game/render_governor.c, the whole file, built as the Wii U builds it against a stand-in platform
layer): it holds step 0 and the neutral values when off; goes down a step at a time when frames run over the target and no faster than its dwell, a
step in a window and a half faster when they are 40% over; goes up only when the frame plus what the step saves is well under the target, slowly, and
waits longer when an up has to be taken back; settles on a step where the frame fits and stays there (no oscillation); does not shed for a hitch (a
load, a save); holds the step halo/governor.txt names, and changes a step's values and the target when it says so; and says its step in the log."""
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import ROOT

FIXTURE = r'''
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render_governor.h"

static const char *settings_text;
static int config_on;
static unsigned long changes = 1;
static int logs;
static char last_log[512];

void platform_log(char const *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	vsnprintf(last_log, sizeof(last_log), format, arguments);
	va_end(arguments);
	logs++;
	printf("LOG %s\n", last_log);
}
int config_boolean(const char *name) { return !strcmp(name, "display.dynamic_quality") && config_on; }
unsigned long config_changes(void) { return changes; }
int render_governor_settings_text(char *text, unsigned long size)
{
	if (!settings_text)
		return 0;
	snprintf(text, size, "%s", settings_text);
	return 1;
}
unsigned long render_governor_pass_milliseconds(double milliseconds[13])
{
	memset(milliseconds, 0, 13 * sizeof(double));
	return 0;
}

/* the milliseconds a frame each step takes off the one before, in a fight (render_governor.c's default_save_ms, less a little: the model is worse) */
static const double saved_up_to[RENDER_GOVERNOR_STEPS] = { 0, 0.5, 1.7, 2.9, 4.4, 6.4, 8.0, 9.6 };
static double now_ms = 1000.0;
static unsigned long noise_state = 12345;
static int last_step;
static double first_change = -1;

static double noise(void)
{
	noise_state = noise_state * 1103515245ul + 12345ul;
	return (double)((noise_state >> 16) & 0x7fff) / 32768.0 - 0.5;
}

/* frames of a scene that takes base ms at step 0, with the steps' savings and a jitter of +-jitter/2, vsynced (a frame is never under 16.7 ms) */
static void run(double seconds, double base, double jitter)
{
	const double end = now_ms + seconds * 1000.0;

	while (now_ms < end)
	{
		double work = base - saved_up_to[render_governor_step()] + noise() * jitter;

		render_governor_frame(work, work * 0.4, work * 0.6, now_ms);
		now_ms += work > 16.7 ? work : 16.7;
		if (render_governor_step() != last_step)
		{
			printf("T %.0f %d %d\n", now_ms - 1000.0, last_step, render_governor_step());
			if (first_change < 0)
				first_change = now_ms - 1000.0;
			last_step = render_governor_step();
		}
	}
}

int main(int argc, char **argv)
{
	const char *scenario = argc > 1 ? argv[1] : "";

	if (!strcmp(scenario, "off"))
	{
		run(30, 40, 0);
		printf("RESULT step %d cull %.1f lod %.2f active %d\n", render_governor_step(), render_governor_values()->cull_scenery_pixels,
			render_governor_values()->lod_scale, render_governor_active());
	}
	else if (!strcmp(scenario, "down_then_up"))
	{
		config_on = 1;
		run(25, 32, 0);
		printf("RESULT after_heavy %d\n", render_governor_step());
		run(150, 14, 0);
		printf("RESULT after_light %d\n", render_governor_step());
	}
	else if (!strcmp(scenario, "settles"))
	{
		/* a scene of 24 ms: step 3 takes it to 21.9, which fits; nothing takes it back up (21.9 and 0.8 more is over 88% of 22) */
		config_on = 1;
		run(300, 24, 0);
		printf("RESULT step %d\n", render_governor_step());
	}
	else if (!strcmp(scenario, "edge"))
	{
		/* a scene at the edge of a step with a jitter in its frames: it goes down and stays down */
		config_on = 1;
		run(300, 22.8, 5);
		printf("RESULT step %d\n", render_governor_step());
	}
	else if (!strcmp(scenario, "hitch"))
	{
		int index;

		config_on = 1;
		run(10, 15, 0);
		for (index = 0; index < 6; index++)
		{
			/* (a load: three frames of a second and a half, a first-use frame of 200 ms, then the game as it was) */
			render_governor_frame(1500, 600, 900, now_ms);
			now_ms += 1500;
			render_governor_frame(1500, 600, 900, now_ms);
			now_ms += 1500;
			render_governor_frame(1500, 600, 900, now_ms);
			now_ms += 1500;
			render_governor_frame(200, 80, 120, now_ms);
			now_ms += 200;
			run(0.1, 15, 0);
		}
		run(10, 15, 0);
		printf("RESULT step %d\n", render_governor_step());
	}
	else if (!strcmp(scenario, "severe"))
	{
		config_on = 1;
		run(5, 40, 0);
		printf("RESULT first %.0f\n", first_change);
	}
	else if (!strcmp(scenario, "ticks_only"))
	{
		/* a frame of 30 ms of which the drawing is 2: nothing the governor sheds is in it */
		int index;

		config_on = 1;
		for (index = 0; index < 1500; index++)
		{
			render_governor_frame(30, 28, 2, now_ms);
			now_ms += 30;
		}
		printf("RESULT step %d\n", render_governor_step());
	}
	else if (!strcmp(scenario, "mild"))
	{
		config_on = 1;
		run(5, 23.5, 0);
		printf("RESULT first %.0f\n", first_change);
	}
	else if (!strcmp(scenario, "forced"))
	{
		config_on = 0;
		settings_text = "step 4; s4.cull_scenery 99; s4.decal_keep 3; target 30";
		run(20, 10, 0);
		printf("RESULT step %d cull %.0f keep %d active %d\n", render_governor_step(), render_governor_values()->cull_scenery_pixels,
			render_governor_values()->decal_keep, render_governor_active());
	}
	else if (!strcmp(scenario, "card_off"))
	{
		config_on = 1;
		settings_text = "off";
		run(20, 40, 0);
		printf("RESULT step %d active %d\n", render_governor_step(), render_governor_active());
	}
	else if (!strcmp(scenario, "card_on"))
	{
		config_on = 0;
		settings_text = "1\ntarget 18\nmax_step 2";
		run(60, 30, 0);
		printf("RESULT step %d active %d\n", render_governor_step(), render_governor_active());
	}
	else if (!strcmp(scenario, "target"))
	{
		/* the same 24 ms scene with a target of 30: nothing to shed */
		config_on = 1;
		settings_text = "target 30";
		run(60, 24, 0);
		printf("RESULT step %d\n", render_governor_step());
	}
	else if (!strcmp(scenario, "log"))
	{
		config_on = 1;
		run(31, 24, 0);
		printf("RESULT logs %d\n", logs);
	}
	else if (!strcmp(scenario, "toggle"))
	{
		/* turned off in the settings: the picture is back at once, and on again starts from the full picture */
		config_on = 1;
		run(20, 30, 0);
		printf("RESULT before %d\n", render_governor_step());
		config_on = 0;
		changes++;
		run(1, 30, 0);
		printf("RESULT off %d active %d\n", render_governor_step(), render_governor_active());
		config_on = 1;
		changes++;
		run(0.1, 30, 0);
		printf("RESULT on %d\n", render_governor_step());
	}
	else if (!strcmp(scenario, "backoff"))
	{
		/* light, heavy, light, heavy: an up taken back within 20 s makes the next up wait twice as long */
		config_on = 1;
		run(10, 26, 0);
		run(20, 14, 0);
		run(8, 26, 0);
		run(40, 14, 0);
	}
	return 0;
}
'''


def run_scenario(tmp_path, scenario, source=None):
    (tmp_path / "checks.c").write_text(FIXTURE)
    (tmp_path / "render_governor.c").write_text(source or (ROOT / "port/linux/game/render_governor.c").read_text(encoding="latin-1"))
    executable = tmp_path / "render_governor"
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-DHALO_WIIU", "-I", str(ROOT / "port/linux/game"),
                    str(tmp_path / "render_governor.c"), str(tmp_path / "checks.c"), "-lm", "-o", str(executable)], check=True)
    out = subprocess.run([str(executable), scenario], check=True, capture_output=True, text=True).stdout
    transitions = [tuple(map(float, line.split()[1:])) for line in out.splitlines() if line.startswith("T ")]
    results = {}
    for line in out.splitlines():
        if line.startswith("RESULT "):
            words = line.split()[1:]
            results.update(dict(zip(words[0::2], map(float, words[1::2]))))
    logs = [line[4:] for line in out.splitlines() if line.startswith("LOG ")]
    return transitions, results, logs


def test_off_changes_nothing(tmp_path):
    transitions, results, logs = run_scenario(tmp_path, "off")
    assert transitions == [] and results["step"] == 0 and results["cull"] == 0 and results["lod"] == 1 and results["active"] == 0
    assert logs == []


def test_down_by_steps_and_back_up_slowly(tmp_path):
    transitions, results, _ = run_scenario(tmp_path, "down_then_up")
    downs = [t for t in transitions if t[2] > t[1]]
    ups = [t for t in transitions if t[2] < t[1]]
    # (a step at a time, none sooner than the dwell after the one before: a second, half of it when 40% over, down to the last step the scene still runs over the target at)
    assert all(abs(t[2] - t[1]) == 1 for t in transitions)
    assert all(b[0] - a[0] >= 500 for a, b in zip(downs, downs[1:]))
    assert results["after_heavy"] == 7
    # (back up: a step each eight seconds at the soonest, all the way to the full picture in the light scene)
    assert all(b[0] - a[0] >= 8000 for a, b in zip(ups, ups[1:]))
    assert results["after_light"] == 0


def test_settles_on_a_step_that_fits(tmp_path):
    transitions, results, _ = run_scenario(tmp_path, "settles")
    assert results["step"] == 3
    assert [t[2] for t in transitions] == [1, 2, 3]


def test_a_scene_at_the_edge_does_not_oscillate(tmp_path):
    transitions, results, _ = run_scenario(tmp_path, "edge")
    ups = [t for t in transitions if t[2] < t[1]]
    assert ups == [] and len(transitions) <= 3 and results["step"] >= 1


def test_a_hitch_is_not_shed_for(tmp_path):
    transitions, results, _ = run_scenario(tmp_path, "hitch")
    assert transitions == [] and results["step"] == 0


def test_the_worse_the_faster(tmp_path):
    _, severe, _ = run_scenario(tmp_path, "severe")
    _, mild, _ = run_scenario(tmp_path, "mild")
    # (40% over: one window; a little over: the dwell, a second, and two windows)
    assert severe["first"] < mild["first"]
    assert severe["first"] <= 700 and mild["first"] >= 1000


def test_a_frame_of_ticks_is_not_drawings_to_shed(tmp_path):
    transitions, results, _ = run_scenario(tmp_path, "ticks_only")
    assert transitions == [] and results["step"] == 0


def test_the_card_file_holds_a_step_and_tunes_the_values(tmp_path):
    transitions, results, _ = run_scenario(tmp_path, "forced")
    assert results["step"] == 4 and results["cull"] == 99 and results["keep"] == 3 and results["active"] == 1
    assert len(transitions) == 1


def test_the_card_file_turns_it_off_or_on(tmp_path):
    _, off, _ = run_scenario(tmp_path, "card_off")
    assert off["step"] == 0 and off["active"] == 0
    transitions, on, _ = run_scenario(tmp_path, "card_on")
    assert on["active"] == 1 and on["step"] == 2 and len(transitions) == 2     # (max_step 2)


def test_the_target_is_the_files(tmp_path):
    transitions, results, _ = run_scenario(tmp_path, "target")
    assert transitions == [] and results["step"] == 0


def test_the_log_says_the_step_every_five_seconds(tmp_path):
    _, results, logs = run_scenario(tmp_path, "log")
    steps = [line for line in logs if line.startswith("governor: step")]
    assert len(steps) >= 5 + 3 and any("frame work" in line and "target 22.0" in line for line in steps)


def test_turned_off_the_picture_is_back_at_once(tmp_path):
    _, results, _ = run_scenario(tmp_path, "toggle")
    assert results["before"] >= 1 and results["off"] == 0 and results["active"] == 0 and results["on"] == 0


def test_an_up_that_is_taken_back_waits_longer(tmp_path):
    transitions, _, _ = run_scenario(tmp_path, "backoff")
    ups = [t for t in transitions if t[2] < t[1]]
    downs_after_first_up = [t for t in transitions if t[2] > t[1] and t[0] > ups[0][0]]
    assert downs_after_first_up, transitions
    later_ups = [t for t in ups if t[0] > downs_after_first_up[0][0]]
    assert later_ups and later_ups[0][0] - downs_after_first_up[0][0] >= 16000


@pytest.mark.parametrize("fault", ["no_hysteresis", "no_hitch_filter", "no_backoff"])
def test_negative_controls(tmp_path, fault):
    source = (ROOT / "port/linux/game/render_governor.c").read_text(encoding="latin-1")
    changes = {
        # (up as soon as the frame is under the target, without the step's saving)
        "no_hysteresis": [("mean + (double)save_ms[governor.step] < target * settings.up_margin", "mean < target")],
        # (a load's frame counts, and a window of one frame is a window)
        "no_hitch_filter": [("if (work_ms > 250.0)", "if (work_ms > 2.5e9)"), ("if (governor.frames >= 8)", "if (governor.frames >= 1)")],
        # (an up taken back soon waits no longer)
        "no_backoff": [("governor.up_backoff *= 2;", "governor.up_backoff *= 1;")],
    }
    for before, after in changes[fault]:
        assert source.count(before) == 1
        source = source.replace(before, after, 1)
    scenario = {"no_hysteresis": "settles", "no_hitch_filter": "hitch", "no_backoff": "backoff"}[fault]
    transitions, _, _ = run_scenario(tmp_path, scenario, source)
    if fault == "no_hysteresis":
        assert any(t[2] < t[1] for t in transitions)
    elif fault == "no_hitch_filter":
        assert transitions
    else:
        ups = [t for t in transitions if t[2] < t[1]]
        downs = [t for t in transitions if t[2] > t[1] and t[0] > ups[0][0]]
        later = [t for t in ups if t[0] > downs[0][0]]
        assert later[0][0] - downs[0][0] < 16000
