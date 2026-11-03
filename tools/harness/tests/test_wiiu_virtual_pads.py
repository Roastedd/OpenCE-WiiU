"""The Wii U's virtual controllers (port/wiiu/src/wiiu_virtual_pads.c), the unit itself compiled with cc against a made-up clock: nothing is pressed before the
lobby opens (the clock's start), and a script with no pads does nothing; a pad n drives the port n - 1 and pads past the setting's count, the keyboard's 0 and
malformed entries do nothing; an entry presses its button for 150 ms and until the game has read it 8 times, however slowly it reads; a button followed by seconds is held that long; the sticks walk from "walk"
until "still", each pad its own way; the ports with a pad of the script's are 1 to count - 1, and at most 3. Each fault below must fail one of the checks."""
import os
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import ROOT, mutated, read, runner

SOURCE = "port/wiiu/src/wiiu_virtual_pads.c"

FAULTS = {
    "kept": None,
    # (the script plays before the lobby has opened)
    "before_start": ("const double now = started ? (double)(unsigned long)(milliseconds - started_at) / 1000.0 : -1.0;",
                     "const double now = (double)(unsigned long)(milliseconds - started_at) / 1000.0;"),
    # (a press lasts its 150 ms only, whether or not the game has read it)
    "time_only": ("now < at + PRESS_MS / 1000.0 || entry_polls[entry_index] < PRESS_POLLS", "now < at + PRESS_MS / 1000.0"),
    # (a press lasts until it has been read, however long that takes, and is never let go)
    "never_released": ("now < at + PRESS_MS / 1000.0 || entry_polls[entry_index] < PRESS_POLLS", "1"),
    # (pad n drives port n)
    "pad_is_port": ("pad = number - 1;", "pad = number;"),
    # (the start of the clock is taken again each time the lobby opens)
    "restart": ("if (!started && pad_count(count) > 0)", "if (pad_count(count) > 0)"),
    # (the sticks go on walking after "still")
    "no_still": ("walking[pad] = 0;", "(void)0;"),
    # (the ports with a pad of the script's include the GamePad's)
    "ports_from_zero": ("for (port = 1; port < pad_count(count); port++)", "for (port = 0; port < pad_count(count); port++)"),
    # (more than four pads)
    # (a number after a button does not hold it)
    "no_duration": ("held = entry_index < ENTRIES && (duration > 0.0 ?", "held = entry_index < ENTRIES && (0 ?"),
    "no_clamp": ("return count < 0 ? 0 : count > WIIU_VIRTUAL_PADS ? WIIU_VIRTUAL_PADS : count;", "return count < 0 ? 0 : count;"),
}

CHECKS = r'''
#include "wiiu_virtual_pads.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition, code) do { if (!(condition)) { printf("check %d failed: %s\n", code, #condition); return code; } } while (0)

static struct wiiu_virtual_pad get(unsigned int port)
{
	struct wiiu_virtual_pad pad;

	wiiu_virtual_pads_get(port, &pad);
	return pad;
}

/* the game reads the pads n times, at the time */
static void read_pads(int count, const char *script, unsigned long ms, int n)
{
	while (n-- > 0)
		wiiu_virtual_pads_update(count, script, ms);
}

int main(void)
{
	const char *script = "3 2 start; 6 2 a; 8 1 a; 10 3 start; 11 4 lt; 12 0 a; 13 2 bogus; garbage; 14 2";
	struct wiiu_virtual_pad pad;
	int index;

	wiiu_virtual_pads_reset();
	/* before the lobby opens nothing is pressed, whatever the time says */
	wiiu_virtual_pads_update(4, script, 4000);
	CHECK(get(1).buttons == 0, 1);
	/* no pads: the start does nothing, and the script never plays */
	wiiu_virtual_pads_start(0, 1000);
	CHECK(!wiiu_virtual_pads_started(), 25);
	wiiu_virtual_pads_update(0, script, 4000);
	CHECK(get(1).buttons == 0 && wiiu_virtual_pads_ports(0) == 0, 2);
	/* the clock starts once, when the lobby first opens (at 1000 ms): a second opening does not start it again */
	wiiu_virtual_pads_start(4, 1000);
	CHECK(wiiu_virtual_pads_started(), 26);
	wiiu_virtual_pads_start(4, 2500);
	wiiu_virtual_pads_update(4, script, 3900);
	CHECK(get(1).buttons == 0, 3);
	/* pad 2 is port 1: START at 3 s (4000 ms) */
	wiiu_virtual_pads_update(4, script, 4000);
	CHECK(get(1).buttons == WIIU_VPAD_START && get(0).buttons == 0 && get(2).buttons == 0, 4);
	/* it is held 150 ms and until read 8 times: 200 ms on, it has been read twice, so it is still down (a slow game) */
	wiiu_virtual_pads_update(4, script, 4200);
	CHECK(get(1).buttons == WIIU_VPAD_START, 5);
	read_pads(4, script, 4300, 5);
	CHECK(get(1).buttons == WIIU_VPAD_START, 6);
	/* (read 7 times by now, 1 at 4000, 1 at 4200 and 5 at 4300: the eighth still sees it down, the ninth does not) */
	read_pads(4, script, 4400, 1);
	CHECK(get(1).buttons == WIIU_VPAD_START, 7);
	read_pads(4, script, 4400, 1);
	CHECK(get(1).buttons == 0, 71);
	/* a fast game reads it 8 times inside the 150 ms and it is still down until they are over, then let go */
	wiiu_virtual_pads_reset();
	wiiu_virtual_pads_start(4, 0);
	read_pads(4, "1 1 a", 1000, 3);
	CHECK(get(0).buttons == WIIU_VPAD_A, 8);
	read_pads(4, "1 1 a", 1200, 20);
	CHECK(get(0).buttons == 0, 9);
	/* the other buttons and triggers, each its own bit; pad 1 is port 0 */
	wiiu_virtual_pads_reset();
	wiiu_virtual_pads_start(4, 0);
	read_pads(4, "0 1 up; 0 1 lt; 0 4 rb; 0 3 right", 10, 1);
	CHECK(get(0).buttons == (WIIU_VPAD_UP | WIIU_VPAD_LT), 10);
	CHECK(get(3).buttons == WIIU_VPAD_RB && get(2).buttons == WIIU_VPAD_RIGHT && get(1).buttons == 0, 11);
	/* a pad past the setting's count, the keyboard's 0 and a pad out of range press nothing */
	wiiu_virtual_pads_reset();
	wiiu_virtual_pads_start(2, 0);
	read_pads(2, "0 3 a; 0 0 a; 0 5 a; 0 2 b", 10, 1);
	CHECK(get(2).buttons == 0 && get(0).buttons == 0 && get(3).buttons == 0 && get(1).buttons == WIIU_VPAD_B, 12);
	/* walking, from the entry's time until "still": nonzero sticks past the game's dead range, each pad its own */
	wiiu_virtual_pads_reset();
	wiiu_virtual_pads_start(3, 0);
	read_pads(3, "1 2 walk; 5 2 still; 2 3 walk", 500, 1);
	CHECK(get(1).left_y == 0 && get(1).left_x == 0, 13);
	read_pads(3, "1 2 walk; 5 2 still; 2 3 walk", 2000, 1);
	CHECK(get(1).left_y != 0 || get(1).left_x != 0, 14);
	CHECK(get(2).left_y != 0 || get(2).left_x != 0, 15);
	CHECK(get(1).left_y != get(2).left_y, 16);
	CHECK(get(0).left_x == 0 && get(0).left_y == 0 && get(0).right_x == 0, 17);
	read_pads(3, "1 2 walk; 5 2 still; 2 3 walk", 6000, 1);
	CHECK(get(1).left_x == 0 && get(1).left_y == 0 && get(1).right_x == 0, 18);
	CHECK(get(2).left_y != 0 || get(2).left_x != 0, 19);
	/* the ports a script's pads have besides the GamePad's: 1 to count - 1, never more than 3 */
	CHECK(wiiu_virtual_pads_ports(0) == 0 && wiiu_virtual_pads_ports(1) == 0, 20);
	CHECK(wiiu_virtual_pads_ports(2) == 2u && wiiu_virtual_pads_ports(3) == 6u && wiiu_virtual_pads_ports(4) == 14u, 21);
	CHECK(wiiu_virtual_pads_ports(9) == 14u && wiiu_virtual_pads_ports(-3) == 0, 22);
	/* a port without a pad, or out of range, is at rest */
	pad = get(9);
	CHECK(pad.buttons == 0 && pad.left_x == 0, 23);
	/* a long script: the entries past the 256th are never pressed */
	{
		static char longer[8000];
		size_t used = 0;

		for (index = 0; index < 300; index++)
			used += (size_t)snprintf(longer + used, sizeof(longer) - used, "0 2 a; ");
		wiiu_virtual_pads_reset();
		wiiu_virtual_pads_start(2, 0);
		read_pads(2, longer, 10, 1);
		CHECK(get(1).buttons == WIIU_VPAD_A, 24);
	}
	/* a button with a number after it is held that many seconds from its time, then let go (however few times it was read), and read at least once */
	wiiu_virtual_pads_reset();
	wiiu_virtual_pads_start(3, 0);
	read_pads(3, "5 3 rt 30; 5 2 a", 4900, 1);
	CHECK(get(2).buttons == 0, 27);
	read_pads(3, "5 3 rt 30; 5 2 a", 5000, 1);
	CHECK(get(2).buttons == WIIU_VPAD_RT && get(1).buttons == WIIU_VPAD_A, 28);
	read_pads(3, "5 3 rt 30; 5 2 a", 20000, 40);
	CHECK(get(2).buttons == WIIU_VPAD_RT && get(1).buttons == 0, 29);
	read_pads(3, "5 3 rt 30; 5 2 a", 35100, 1);
	CHECK(get(2).buttons == 0, 30);
	/* (a duration that is not a number, or not past 0, is an ordinary press) */
	wiiu_virtual_pads_reset();
	wiiu_virtual_pads_start(2, 0);
	read_pads(2, "1 2 a bogus; 1 2 b -4", 1000, 1);
	CHECK(get(1).buttons == (WIIU_VPAD_A | WIIU_VPAD_B), 31);
	read_pads(2, "1 2 a bogus; 1 2 b -4", 1200, 30);
	CHECK(get(1).buttons == 0, 32);
	return 0;
}
'''


def compiler():
    target = os.environ.get("HALO_HARNESS_TARGET")
    return [os.environ.get("CC", "clang"), *target.split()] if target else ["cc"]


@pytest.mark.parametrize("fault", list(FAULTS))
def test_the_script_plays_on_the_ports(tmp_path, fault):
    source = read(SOURCE)
    if FAULTS[fault]:
        source = mutated(source, *FAULTS[fault])
    (tmp_path / "wiiu_virtual_pads.c").write_text(source)
    (tmp_path / "checks.c").write_text(CHECKS)
    executable = tmp_path / "virtual_pads"
    relaxed = ["-Wno-unused-function", "-Wno-unused-parameter", "-Wno-unused-variable"] if FAULTS[fault] else []
    subprocess.run([*compiler(), "-std=c99", "-Wall", "-Wextra", "-Werror", *relaxed, "-I", str(ROOT / "port/wiiu/src"),
                    str(tmp_path / "wiiu_virtual_pads.c"), str(tmp_path / "checks.c"), "-o", str(executable), "-lm"], check=True)
    result = subprocess.run([*runner(), str(executable)], capture_output=True, text=True, check=False)
    if fault == "kept":
        assert result.returncode == 0, result.stdout
    else:
        assert result.returncode != 0, f"the fault {fault} passed every check"
