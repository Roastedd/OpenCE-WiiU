"""The Wii U's Wii Remote (port/wiiu/src/wiiu_wimote.c), the unit itself compiled with cc against injected KPAD values: every button of the
layouts (the default's, the Nunchuk's Z firing, and the menus' for a Remote alone) with the Xbox pad's meaning each should have; a Nunchuk's
emulated d-pad presses are not the d-pad; the Nunchuk's stick (its dead centre, its reach, its direction); the pointer's free-aim box (nothing
inside, a turn rate that grows past the edge up to the full stick and no more, the speed, the lift past the game's dead range, up positive, nothing
while the pointer is not valid); which look a Remote has for each setting, extension, player and pointer; the MotionPlus mode each extension
needs; the MotionPlus's turn and tilt from injected rates (the player space with the Remote level, tilted and pointed up, the Remote's own yaw and
roll, the tightening of small rates, the time limit of a sample, the sign of the pitch and the inverted turn); and the zeroing after a Remote has
been held still. Each fault below must fail one of the checks."""
import os
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import ROOT, mutated, read, runner

SOURCE = "port/wiiu/src/wiiu_wimote.c"

FAULTS = {
    "kept": None,
    # (the Nunchuk's emulated d-pad presses, in the d-pad's own bits, are taken for the Remote's)
    "nunchuk_leaks": ("return hold | (nunchuk_hold & (WIIMOTE_BUTTON_Z | WIIMOTE_BUTTON_C));", "return hold | nunchuk_hold;"),
    # (B throws grenades in the default layout)
    "b_not_fire": ("{ WIIMOTE_BUTTON_B, WIIU_PAD_ZR, 0 },", "{ WIIMOTE_BUTTON_B, WIIU_PAD_ZL, 0 },"),
    # (a Remote alone has no stick, but the menu's layout reads the Nunchuk's)
    "menu_stick": ("if (layout != WIIMOTE_LAYOUT_MENU)\n\t\twimote_stick", "if (1)\n\t\twimote_stick"),
    # (a Remote alone has the game's layout, whose A jumps and B fires)
    "alone_has_game_layout": ("return wimote_has_nunchuk(extension) ? layout : WIIMOTE_LAYOUT_MENU;", "return layout;"),
    # (the rate does not begin at the box's edge)
    "box_not_normalized": ("distance = (distance - box) / (1.0f - box);", "distance = distance - box;"),
    # (the rate has no maximum)
    "rate_unclamped": ("\tif (rate > 1.0f)\n\t\trate = 1.0f;\n\treturn position < 0.0f", "\treturn position < 0.0f"),
    # (the rate is not lifted past the game's dead range: the view would not turn until the pointer is well outside the box)
    "no_lift": ("\tif (rate == 0.0f)\n\t\treturn 0.0f;\n\treturn rate < 0.0f", "\treturn rate;\n\treturn rate < 0.0f"),
    # (a pointer that does not see the sensor bar still turns the view)
    "invalid_turns": ("\tif (!valid)\n\t\treturn;\n\t*look_x", "\t*look_x"),
    # (a pointer above the middle looks down)
    "y_not_flipped": ("*look_y = lifted(-wimote_pointer_rate(y, box, speed));", "*look_y = lifted(wimote_pointer_rate(y, box, speed));"),
    # (a MotionPlus turns the view of a Remote that is not the first player's)
    "motion_any_player": ("const int motion = has_motion && first_player;", "const int motion = has_motion;"),
    # ("auto" turns by the pointer whether it sees the bar or not)
    "auto_ignores_valid": ("return motion ? WIIMOTE_SOURCE_MOTION : pointer_valid ? WIIMOTE_SOURCE_POINTER : WIIMOTE_SOURCE_NONE;",
                           "return motion ? WIIMOTE_SOURCE_MOTION : WIIMOTE_SOURCE_POINTER;"),
    # (lifting the Remote's nose looks down)
    "pitch_sign": ("*tilt = -turned[0];", "*tilt = turned[0];"),
    # (the sensor's noise turns the view)
    "no_tightening": ("scale = magnitude < MOTION_TIGHTENING ? magnitude / MOTION_TIGHTENING : 1.0f;", "scale = 1.0f;"),
    # (a late sample counts for its whole time)
    "no_time_limit": ("\tif (seconds > 0.1f)\n\t\tseconds = 0.1f;\n\t/* the accelerometer", "\t/* the accelerometer"),
    # (the Remote is zeroed again and again while it lies still)
    "still_repeats": ("\t\tmotion->still_told = 1;\n\t\treturn 1;", "\t\treturn 1;"),
    # (a Remote with a Nunchuk is asked for the MotionPlus alone, which loses the Nunchuk)
    "mpls_mode": ("return wimote_has_nunchuk(extension) ? WIIMOTE_MPLS_NUNCHUK : WIIMOTE_MPLS_ONLY;", "return WIIMOTE_MPLS_ONLY;"),
    # (the Nunchuk's stick has no dead centre)
    "stick_no_dead": ("if (!(length > STICK_DEAD))", "if (!(length > 0.0f))"),
    # (a Remote with a Nunchuk and no MotionPlus is taken to have it)
    "nunchuk_is_motion": ("return extension == WIIMOTE_EXT_MPLUS || extension == WIIMOTE_EXT_MPLUS_NUNCHUK;",
                          "return extension == WIIMOTE_EXT_MPLUS || extension == WIIMOTE_EXT_MPLUS_NUNCHUK || extension == WIIMOTE_EXT_NUNCHUK;"),
    # (the setting that turns the MotionPlus's turn round does nothing)
    "invert_ignored": ("*turn = invert_turn ? -result : result;", "*turn = result;"),
    # (the Remote's own roll turns the view the wrong way round)
    "lean_sign": ("result = -turned[1];", "result = turned[1];"),
    # (the edge turn turns the view the wrong way round)
    "edge_sign": ("return sign * rate * seconds;", "return -sign * rate * seconds;"),
    # (the edge turn begins at the middle, not at the box's edge)
    "edge_no_box": ("if (!(distance > box))\n\t\treturn 0.0f;\n\t/* (0 at the box's edge to 1 at its range", "/* (0 at the box's edge to 1 at its range"),
    # (the kept turn is not held at the range, so coming back takes as long as going out did)
    "edge_unclamped": ("\tif (*kept > box + range)\n\t\t*kept = box + range;\n\telse if (*kept < -(box + range))\n\t\t*kept = -(box + range);\n", ""),
    # (the kept turn never leaks: a drift in the sensor turns the view for good)
    "edge_no_leak": ("if (slow)\n\t\t*kept -= *kept * EDGE_LEAK * seconds;", ""),
    # (the kept turn leaks while the Remote is turned fast too)
    "edge_leaks_always": ("rate < EDGE_SLOW);\n\t*extra_tilt", "1);\n\t*extra_tilt"),
    # (the speed does nothing)
    "edge_speed_ignored": ("rate = speed * WIIMOTE_EDGE_RATE *", "rate = WIIMOTE_EDGE_RATE *"),
    # (a late sample counts for its whole time in the edge turn)
    "edge_no_time_limit": ("\tif (seconds > 0.1f)\n\t\tseconds = 0.1f;\n\trate = sqrtf", "\trate = sqrtf"),
    # (zeroing the Remote does not make where it points the middle)
    "edge_no_recentre": ("motion->edge_x = motion->edge_y = 0.0f;", ""),
}

CHECKS = r'''
#include "wiiu_wimote.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition, code) do { if (!(condition)) { printf("check %d failed: %s\n", code, #condition); return code; } } while (0)
#define NEAR(a, b, tolerance) (fabsf((a) - (b)) <= (tolerance))

/* every Remote and Nunchuk button, by name: what each layout should do with it, as the README's tables say (Xbox terms) */
struct expected
{
	unsigned int source;
	unsigned int pad;
	unsigned int face;
};

static const unsigned int ALL[] = { WIIMOTE_BUTTON_LEFT, WIIMOTE_BUTTON_RIGHT, WIIMOTE_BUTTON_DOWN, WIIMOTE_BUTTON_UP, WIIMOTE_BUTTON_PLUS, WIIMOTE_BUTTON_2,
	WIIMOTE_BUTTON_1, WIIMOTE_BUTTON_B, WIIMOTE_BUTTON_A, WIIMOTE_BUTTON_MINUS, WIIMOTE_BUTTON_Z, WIIMOTE_BUTTON_C, WIIMOTE_BUTTON_HOME };

/* default: fire is the right trigger (ZR), grenade the left (ZL), jump the Xbox's A, melee its B, reload and action its X, switch weapons its Y, flashlight white (L),
switch grenades black (R), crouch the left stick's click, zoom the right stick's, + Start, - Back */
static const struct expected DEFAULT[] =
{
	{ WIIMOTE_BUTTON_B, WIIU_PAD_ZR, 0 }, { WIIMOTE_BUTTON_A, 0, WIIU_XBOX_A }, { WIIMOTE_BUTTON_Z, WIIU_PAD_ZL, 0 }, { WIIMOTE_BUTTON_C, WIIU_PAD_STICK_L, 0 },
	{ WIIMOTE_BUTTON_1, 0, WIIU_XBOX_X }, { WIIMOTE_BUTTON_2, 0, WIIU_XBOX_B }, { WIIMOTE_BUTTON_UP, 0, WIIU_XBOX_Y }, { WIIMOTE_BUTTON_LEFT, WIIU_PAD_L, 0 },
	{ WIIMOTE_BUTTON_RIGHT, WIIU_PAD_R, 0 }, { WIIMOTE_BUTTON_DOWN, WIIU_PAD_STICK_R, 0 }, { WIIMOTE_BUTTON_PLUS, WIIU_PAD_PLUS, 0 },
	{ WIIMOTE_BUTTON_MINUS, WIIU_PAD_MINUS, 0 }, { WIIMOTE_BUTTON_HOME, 0, 0 },
};
static const struct expected Z_FIRE[] =
{
	{ WIIMOTE_BUTTON_Z, WIIU_PAD_ZR, 0 }, { WIIMOTE_BUTTON_A, 0, WIIU_XBOX_A }, { WIIMOTE_BUTTON_B, WIIU_PAD_ZL, 0 }, { WIIMOTE_BUTTON_C, WIIU_PAD_STICK_L, 0 },
	{ WIIMOTE_BUTTON_1, 0, WIIU_XBOX_X }, { WIIMOTE_BUTTON_2, 0, WIIU_XBOX_B }, { WIIMOTE_BUTTON_UP, 0, WIIU_XBOX_Y }, { WIIMOTE_BUTTON_LEFT, WIIU_PAD_L, 0 },
	{ WIIMOTE_BUTTON_RIGHT, WIIU_PAD_R, 0 }, { WIIMOTE_BUTTON_DOWN, WIIU_PAD_STICK_R, 0 }, { WIIMOTE_BUTTON_PLUS, WIIU_PAD_PLUS, 0 },
	{ WIIMOTE_BUTTON_MINUS, WIIU_PAD_MINUS, 0 }, { WIIMOTE_BUTTON_HOME, 0, 0 },
};
/* menu: the d-pad, A and B the Xbox's A and B, 1 its X, 2 its Y, + and -; Z and C (a Nunchuk that is not read) nothing */
static const struct expected MENU[] =
{
	{ WIIMOTE_BUTTON_UP, WIIU_PAD_UP, 0 }, { WIIMOTE_BUTTON_DOWN, WIIU_PAD_DOWN, 0 }, { WIIMOTE_BUTTON_LEFT, WIIU_PAD_LEFT, 0 },
	{ WIIMOTE_BUTTON_RIGHT, WIIU_PAD_RIGHT, 0 }, { WIIMOTE_BUTTON_A, 0, WIIU_XBOX_A }, { WIIMOTE_BUTTON_B, 0, WIIU_XBOX_B }, { WIIMOTE_BUTTON_1, 0, WIIU_XBOX_X },
	{ WIIMOTE_BUTTON_2, 0, WIIU_XBOX_Y }, { WIIMOTE_BUTTON_PLUS, WIIU_PAD_PLUS, 0 }, { WIIMOTE_BUTTON_MINUS, WIIU_PAD_MINUS, 0 }, { WIIMOTE_BUTTON_Z, 0, 0 },
	{ WIIMOTE_BUTTON_C, 0, 0 }, { WIIMOTE_BUTTON_HOME, 0, 0 },
};

static int layout_checks(int layout, const struct expected *table, unsigned int count, int base)
{
	struct wiiu_pad pad;
	unsigned int i, all_pad = 0, all_face = 0;

	for (i = 0; i < count; i++)
	{
		wimote_map(layout, table[i].source, 0.0f, 0.0f, &pad);
		if (pad.buttons != table[i].pad || pad.xbox_faces != table[i].face)
		{
			printf("layout %d: button 0x%04x gives pad 0x%x face 0x%x, expected 0x%x and 0x%x\n", layout, table[i].source, pad.buttons, pad.xbox_faces,
				table[i].pad, table[i].face);
			return base;
		}
		all_pad |= table[i].pad;
		all_face |= table[i].face;
		CHECK(pad.left_x == 0.0f && pad.left_y == 0.0f && pad.right_x == 0.0f && pad.right_y == 0.0f && pad.look_x == 0.0f && pad.look_y == 0.0f, base + 1);
	}
	/* (nothing held, nothing pressed; everything held, everything of the layout) */
	wimote_map(layout, 0, 0.0f, 0.0f, &pad);
	CHECK(pad.buttons == 0 && pad.xbox_faces == 0, base + 2);
	{
		unsigned int hold = 0;

		for (i = 0; i < sizeof(ALL) / sizeof(ALL[0]); i++)
			hold |= ALL[i];
		wimote_map(layout, hold, 0.0f, 0.0f, &pad);
		CHECK(pad.buttons == all_pad && pad.xbox_faces == all_face, base + 3);
	}
	/* (a pad given is cleared first: what was in it is not kept) */
	pad.buttons = 0xffff;
	pad.xbox_faces = 0xf;
	pad.right_x = 1.0f;
	wimote_map(layout, 0, 0.0f, 0.0f, &pad);
	CHECK(pad.buttons == 0 && pad.xbox_faces == 0 && pad.right_x == 0.0f, base + 4);
	return 0;
}

static int rate_checks(void)
{
	/* the box 0.3, the speed 1: nothing inside or on the box, a little past it a little, the screen's edge the full stick, beyond it no more */
	CHECK(wimote_pointer_rate(0.0f, 0.3f, 1.0f) == 0.0f, 200);
	CHECK(wimote_pointer_rate(0.29f, 0.3f, 1.0f) == 0.0f && wimote_pointer_rate(-0.3f, 0.3f, 1.0f) == 0.0f, 201);
	CHECK(wimote_pointer_rate(0.31f, 0.3f, 1.0f) > 0.0f && wimote_pointer_rate(0.31f, 0.3f, 1.0f) < 0.02f, 202);
	CHECK(NEAR(wimote_pointer_rate(1.0f, 0.3f, 1.0f), 1.0f, 0.0001f), 203);
	CHECK(wimote_pointer_rate(3.0f, 0.3f, 1.0f) == wimote_pointer_rate(1.0f, 0.3f, 1.0f) && wimote_pointer_rate(-3.0f, 0.3f, 1.0f) <= 1.0f, 204);
	CHECK(wimote_pointer_rate(-0.8f, 0.3f, 1.0f) == -wimote_pointer_rate(0.8f, 0.3f, 1.0f) && wimote_pointer_rate(-0.8f, 0.3f, 1.0f) < 0.0f, 205);
	/* growing with the distance, strictly, until the edge */
	{
		float before = 0.0f, position;

		for (position = 0.32f; position < 1.0f; position += 0.04f)
		{
			const float rate = wimote_pointer_rate(position, 0.3f, 1.0f);

			CHECK(rate > before, 206);
			before = rate;
		}
	}
	/* the middle of the way out is under half the speed (the curve is gentle at first) */
	CHECK(wimote_pointer_rate(0.65f, 0.3f, 1.0f) < 0.5f, 207);
	/* the speed: twice as fast reaches the full stick sooner; the box: a bigger one turns nothing further in */
	CHECK(wimote_pointer_rate(0.5f, 0.3f, 2.0f) > wimote_pointer_rate(0.5f, 0.3f, 1.0f), 208);
	CHECK(NEAR(wimote_pointer_rate(0.8f, 0.3f, 2.0f), 1.0f, 0.0001f), 209);
	CHECK(wimote_pointer_rate(0.5f, 0.6f, 1.0f) == 0.0f && wimote_pointer_rate(0.5f, 0.1f, 1.0f) > 0.0f, 210);
	CHECK(NEAR(wimote_pointer_rate(1.0f, 0.6f, 1.0f), 1.0f, 0.0001f), 211);
	/* bad values: a box under 0 or over 0.9 or not a number is 0.3, a speed of 0, over 4, or not a number is 1 */
	CHECK(wimote_pointer_rate(0.5f, -1.0f, 1.0f) == wimote_pointer_rate(0.5f, 0.3f, 1.0f), 212);
	CHECK(wimote_pointer_rate(0.5f, 0.95f, 1.0f) == wimote_pointer_rate(0.5f, 0.3f, 1.0f), 213);
	CHECK(wimote_pointer_rate(0.5f, NAN, 1.0f) == wimote_pointer_rate(0.5f, 0.3f, 1.0f), 214);
	CHECK(wimote_pointer_rate(0.5f, 0.3f, 0.0f) == wimote_pointer_rate(0.5f, 0.3f, 1.0f), 215);
	CHECK(wimote_pointer_rate(0.5f, 0.3f, 5.0f) == wimote_pointer_rate(0.5f, 0.3f, 1.0f), 216);
	CHECK(wimote_pointer_rate(0.5f, 0.3f, NAN) == wimote_pointer_rate(0.5f, 0.3f, 1.0f), 217);
	/* a box of 0 turns at once; the biggest, 0.9, turns from 0.9 out */
	CHECK(wimote_pointer_rate(0.01f, 0.0f, 1.0f) > 0.0f, 218);
	CHECK(wimote_pointer_rate(0.9f, 0.9f, 1.0f) == 0.0f && wimote_pointer_rate(0.95f, 0.9f, 1.0f) > 0.0f, 219);
	return 0;
}

static int pointer_checks(void)
{
	float x, y;
	/* the stick values (0.275: the game's dead range, 9000 of 32767, and a little) */
	wimote_pointer_look(1, 0.0f, 0.0f, 0.3f, 1.0f, &x, &y);
	CHECK(x == 0.0f && y == 0.0f, 220);
	/* inside the box, either axis: no turn; the box is per axis (a pointer far right and level turns right only) */
	wimote_pointer_look(1, 0.25f, -0.25f, 0.3f, 1.0f, &x, &y);
	CHECK(x == 0.0f && y == 0.0f, 221);
	wimote_pointer_look(1, 0.9f, 0.1f, 0.3f, 1.0f, &x, &y);
	CHECK(x > 0.5f && y == 0.0f, 222);
	/* just outside the box's edge the push is just past the game's dead range: the view begins to turn from nothing */
	wimote_pointer_look(1, 0.3001f, 0.0f, 0.3f, 1.0f, &x, &y);
	CHECK(x >= 0.2749f && x < 0.2800f, 223);
	/* the full stick at the edge, and not more */
	wimote_pointer_look(1, 1.0f, 0.0f, 0.3f, 1.0f, &x, &y);
	CHECK(x <= 1.0f && x > 0.9999f, 224);
	wimote_pointer_look(1, 5.0f, 0.0f, 0.3f, 1.0f, &x, &y);
	CHECK(x <= 1.0f && x > 0.9999f, 225);
	/* left is negative; up (the pointer's y negative) is positive, down negative */
	wimote_pointer_look(1, -0.9f, -0.9f, 0.3f, 1.0f, &x, &y);
	CHECK(x < -0.5f && y > 0.5f, 226);
	wimote_pointer_look(1, 0.0f, 0.9f, 0.3f, 1.0f, &x, &y);
	CHECK(x == 0.0f && y < -0.5f, 227);
	/* a pointer that is not valid turns nothing, wherever it says it is */
	wimote_pointer_look(0, 0.9f, -0.9f, 0.3f, 1.0f, &x, &y);
	CHECK(x == 0.0f && y == 0.0f, 228);
	/* the push grows with the distance */
	{
		float a, b, ignore;

		wimote_pointer_look(1, 0.5f, 0.0f, 0.3f, 1.0f, &a, &ignore);
		wimote_pointer_look(1, 0.7f, 0.0f, 0.3f, 1.0f, &b, &ignore);
		CHECK(b > a && a > 0.275f, 229);
	}
	return 0;
}

static int look_checks(void)
{
	int setting, motion, first, valid;

	for (setting = WIIMOTE_LOOK_AUTO; setting <= WIIMOTE_LOOK_OFF; setting++)
		for (motion = 0; motion < 2; motion++)
			for (first = 0; first < 2; first++)
				for (valid = 0; valid < 2; valid++)
				{
					const int got = wimote_look_source(setting, motion, first, valid);
					int want;

					if (setting == WIIMOTE_LOOK_OFF)
						want = WIIMOTE_SOURCE_NONE;
					else if (setting == WIIMOTE_LOOK_POINTER)
						want = WIIMOTE_SOURCE_POINTER;
					else if (setting == WIIMOTE_LOOK_MOTION)
						want = motion && first ? WIIMOTE_SOURCE_MOTION : WIIMOTE_SOURCE_NONE;
					else
						want = motion && first ? WIIMOTE_SOURCE_MOTION : valid ? WIIMOTE_SOURCE_POINTER : WIIMOTE_SOURCE_NONE;
					if (got != want)
					{
						printf("look: setting %d motion %d first %d valid %d gives %d, expected %d\n", setting, motion, first, valid, got, want);
						return 300;
					}
				}
	/* the words */
	CHECK(wimote_parse_look("auto") == WIIMOTE_LOOK_AUTO && wimote_parse_look("pointer") == WIIMOTE_LOOK_POINTER && wimote_parse_look("motion") == WIIMOTE_LOOK_MOTION &&
		wimote_parse_look("off") == WIIMOTE_LOOK_OFF, 301);
	CHECK(wimote_parse_look("") == WIIMOTE_LOOK_AUTO && wimote_parse_look("gyro") == WIIMOTE_LOOK_AUTO, 302);
	CHECK(wimote_parse_layout("default") == WIIMOTE_LAYOUT_DEFAULT && wimote_parse_layout("z_fire") == WIIMOTE_LAYOUT_Z_FIRE &&
		wimote_parse_layout("") == WIIMOTE_LAYOUT_DEFAULT && wimote_parse_layout("menu") == WIIMOTE_LAYOUT_DEFAULT, 303);
	CHECK(!strcmp(wimote_layout_name(WIIMOTE_LAYOUT_DEFAULT), "default") && !strcmp(wimote_layout_name(WIIMOTE_LAYOUT_Z_FIRE), "z_fire") &&
		!strcmp(wimote_look_name(WIIMOTE_LOOK_MOTION), "motion"), 304);
	/* a Remote rumbles unless its motion turns the view */
	CHECK(wimote_rumble_allowed(WIIMOTE_SOURCE_NONE) && wimote_rumble_allowed(WIIMOTE_SOURCE_POINTER) && !wimote_rumble_allowed(WIIMOTE_SOURCE_MOTION), 305);
	return 0;
}

static int extension_checks(void)
{
	/* the Remotes the game reads: bare, with a Nunchuk, with the MotionPlus, with both; not the other extensions */
	CHECK(wimote_reads(WIIMOTE_EXT_CORE) && wimote_reads(WIIMOTE_EXT_NUNCHUK) && wimote_reads(WIIMOTE_EXT_MPLUS) && wimote_reads(WIIMOTE_EXT_MPLUS_NUNCHUK), 400);
	CHECK(!wimote_reads(WIIMOTE_EXT_CLASSIC) && !wimote_reads(WIIMOTE_EXT_MPLUS_CLASSIC) && !wimote_reads(WIIMOTE_EXT_PRO_CONTROLLER) && !wimote_reads(0x03) &&
		!wimote_reads(0xfd) && !wimote_reads(0xff), 401);
	CHECK(!wimote_has_nunchuk(WIIMOTE_EXT_CORE) && wimote_has_nunchuk(WIIMOTE_EXT_NUNCHUK) && !wimote_has_nunchuk(WIIMOTE_EXT_MPLUS) &&
		wimote_has_nunchuk(WIIMOTE_EXT_MPLUS_NUNCHUK), 402);
	CHECK(!wimote_has_motion(WIIMOTE_EXT_CORE) && !wimote_has_motion(WIIMOTE_EXT_NUNCHUK) && wimote_has_motion(WIIMOTE_EXT_MPLUS) &&
		wimote_has_motion(WIIMOTE_EXT_MPLUS_NUNCHUK), 403);
	/* the MotionPlus modes KPADEnableMpls is asked for: with a Nunchuk its passthrough (5), alone 4, and none for what is not a Remote read here */
	CHECK(wimote_mpls_mode_wanted(WIIMOTE_EXT_CORE) == 4 && wimote_mpls_mode_wanted(WIIMOTE_EXT_MPLUS) == 4, 404);
	CHECK(wimote_mpls_mode_wanted(WIIMOTE_EXT_NUNCHUK) == 5 && wimote_mpls_mode_wanted(WIIMOTE_EXT_MPLUS_NUNCHUK) == 5, 405);
	CHECK(wimote_mpls_mode_wanted(WIIMOTE_EXT_CLASSIC) == 0 && wimote_mpls_mode_wanted(WIIMOTE_EXT_PRO_CONTROLLER) == 0 && wimote_mpls_mode_wanted(0xff) == 0, 406);
	/* the layout a Remote has: the one asked for with a Nunchuk (also under the MotionPlus), the menus' without */
	CHECK(wimote_layout_for(WIIMOTE_EXT_NUNCHUK, WIIMOTE_LAYOUT_DEFAULT) == WIIMOTE_LAYOUT_DEFAULT, 407);
	CHECK(wimote_layout_for(WIIMOTE_EXT_MPLUS_NUNCHUK, WIIMOTE_LAYOUT_Z_FIRE) == WIIMOTE_LAYOUT_Z_FIRE, 408);
	CHECK(wimote_layout_for(WIIMOTE_EXT_CORE, WIIMOTE_LAYOUT_DEFAULT) == WIIMOTE_LAYOUT_MENU && wimote_layout_for(WIIMOTE_EXT_MPLUS, WIIMOTE_LAYOUT_Z_FIRE) == WIIMOTE_LAYOUT_MENU, 409);
	/* a Nunchuk's stick emulates the d-pad's presses in nunchuk.hold, in the d-pad's own bits: only its Z and C are taken */
	CHECK(wimote_hold(0x0800, 0x0001 | 0x0002 | 0x0004 | 0x0008) == 0x0800, 410);
	CHECK(wimote_hold(0x0800, 0x2000 | 0x4000 | 0x0008) == (0x0800 | 0x2000 | 0x4000), 411);
	CHECK(wimote_hold(0x2000, 0) == 0x2000, 412);
	return 0;
}

static int stick_checks(void)
{
	struct wiiu_pad pad;
	float x, y;

	/* a centred stick, and one within the dead centre, is nothing */
	wimote_stick(0.0f, 0.0f, &x, &y);
	CHECK(x == 0.0f && y == 0.0f, 500);
	wimote_stick(0.05f, -0.06f, &x, &y);
	CHECK(x == 0.0f && y == 0.0f, 501);
	/* its reach is the full throw, in the direction it points; further than the reach is no more */
	wimote_stick(0.85f, 0.0f, &x, &y);
	CHECK(NEAR(x, 1.0f, 0.0001f) && y == 0.0f, 502);
	wimote_stick(0.0f, -1.0f, &x, &y);
	CHECK(x == 0.0f && NEAR(y, -1.0f, 0.0001f), 503);
	wimote_stick(1.0f, 1.0f, &x, &y);
	CHECK(NEAR(sqrtf(x * x + y * y), 1.0f, 0.0001f) && NEAR(x, y, 0.0001f), 504);
	/* in between, in proportion, with the direction kept */
	wimote_stick(0.5f, 0.0f, &x, &y);
	CHECK(NEAR(x, (0.5f - 0.10f) / 0.75f, 0.001f), 505);
	wimote_stick(-0.3f, 0.4f, &x, &y);
	CHECK(x < 0.0f && y > 0.0f && NEAR(y / x, -4.0f / 3.0f, 0.001f), 506);
	/* the stick is the left stick, with the layouts of a Nunchuk, and none in the menus' layout */
	wimote_map(WIIMOTE_LAYOUT_DEFAULT, 0, 0.85f, -0.85f, &pad);
	CHECK(pad.left_x > 0.69f && pad.left_y < -0.69f && pad.right_x == 0.0f && pad.right_y == 0.0f, 507);
	wimote_map(WIIMOTE_LAYOUT_Z_FIRE, 0, -0.85f, 0.0f, &pad);
	CHECK(NEAR(pad.left_x, -1.0f, 0.0001f), 508);
	wimote_map(WIIMOTE_LAYOUT_MENU, 0, 0.85f, 0.85f, &pad);
	CHECK(pad.left_x == 0.0f && pad.left_y == 0.0f, 509);
	return 0;
}

static int motion_checks(void)
{
	static const float LEVEL[3] = { 0.0f, 0.0f, 1.0f }, UP[3] = { 0.0f, 1.0f, 0.0f }, TILT[3] = { 0.0f, 0.70710678f, 0.70710678f };
	struct wimote_motion m;
	float turn, tilt;
	float rate[3];

	/* the Remote level (buttons up), turned left about its z at 36 degrees a second for a tenth of a second: a hundredth of a rotation to the left */
	wimote_motion_reset(&m);
	rate[0] = 0.0f; rate[1] = 0.0f; rate[2] = 36.0f;
	wimote_motion_step(&m, WIIU_GYRO_SPACE_PLAYER, 0.1f, LEVEL, rate, 0, &turn, &tilt);
	CHECK(NEAR(turn, 0.01f, 0.00005f) && tilt == 0.0f, 600);
	rate[2] = -36.0f;
	wimote_motion_step(&m, WIIU_GYRO_SPACE_PLAYER, 0.1f, LEVEL, rate, 0, &turn, &tilt);
	CHECK(NEAR(turn, -0.01f, 0.00005f), 601);
	/* inverted: the other way round */
	wimote_motion_step(&m, WIIU_GYRO_SPACE_PLAYER, 0.1f, LEVEL, rate, 1, &turn, &tilt);
	CHECK(NEAR(turn, 0.01f, 0.00005f), 602);
	/* the nose lifting (positive about x) is a negative tilt (the GamePad's top away looks down); no turn */
	rate[0] = 36.0f; rate[2] = 0.0f;
	wimote_motion_step(&m, WIIU_GYRO_SPACE_PLAYER, 0.1f, LEVEL, rate, 0, &turn, &tilt);
	CHECK(NEAR(tilt, -0.01f, 0.00005f) && turn == 0.0f, 603);
	rate[0] = -36.0f;
	wimote_motion_step(&m, WIIU_GYRO_SPACE_PLAYER, 0.1f, LEVEL, rate, 1, &turn, &tilt);
	CHECK(NEAR(tilt, 0.01f, 0.00005f), 604);
	/* the roll about its length does not turn the view when it is level (player space) */
	rate[0] = 0.0f; rate[1] = 36.0f; rate[2] = 0.0f;
	wimote_motion_step(&m, WIIU_GYRO_SPACE_PLAYER, 0.1f, LEVEL, rate, 0, &turn, &tilt);
	CHECK(turn == 0.0f, 605);
	/* the Remote's own spaces: the yaw alone (the roll is nothing), the roll alone (to the right is a negative turn) */
	wimote_motion_reset(&m);
	rate[0] = 0.0f; rate[1] = 36.0f; rate[2] = 36.0f;
	wimote_motion_step(&m, WIIU_GYRO_SPACE_TURN, 0.1f, LEVEL, rate, 0, &turn, &tilt);
	CHECK(NEAR(turn, 0.01f, 0.00005f), 606);
	wimote_motion_step(&m, WIIU_GYRO_SPACE_LEAN, 0.1f, LEVEL, rate, 0, &turn, &tilt);
	CHECK(NEAR(turn, -0.01f, 0.00005f), 607);
	rate[1] = 0.0f;
	wimote_motion_step(&m, WIIU_GYRO_SPACE_LEAN, 0.1f, LEVEL, rate, 0, &turn, &tilt);
	CHECK(turn == 0.0f, 608);
	/* player space, the Remote pointed at the ceiling (up along its length): turning about the vertical is its roll, and its yaw is nothing */
	wimote_motion_reset(&m);
	rate[0] = 0.0f; rate[1] = 36.0f; rate[2] = 0.0f;
	wimote_motion_step(&m, WIIU_GYRO_SPACE_PLAYER, 0.1f, UP, rate, 0, &turn, &tilt);
	CHECK(NEAR(turn, 0.01f, 0.00005f), 609);
	rate[1] = 0.0f; rate[2] = 36.0f;
	wimote_motion_step(&m, WIIU_GYRO_SPACE_PLAYER, 0.1f, UP, rate, 0, &turn, &tilt);
	CHECK(turn == 0.0f, 610);
	/* tilted half way: the share of the yaw that is about the vertical (0.7071 of 0.01), a little over it by GyroWiki's 1.41 and no more than the yaw and roll together */
	wimote_motion_reset(&m);
	wimote_motion_step(&m, WIIU_GYRO_SPACE_PLAYER, 0.1f, TILT, rate, 0, &turn, &tilt);
	CHECK(NEAR(turn, 0.00997f, 0.0001f), 611);
	/* a sample's time counts for at most a tenth of a second */
	wimote_motion_reset(&m);
	rate[2] = 36.0f;
	wimote_motion_step(&m, WIIU_GYRO_SPACE_PLAYER, 5.0f, LEVEL, rate, 0, &turn, &tilt);
	CHECK(NEAR(turn, 0.01f, 0.00005f), 612);
	/* no time, no turn */
	wimote_motion_step(&m, WIIU_GYRO_SPACE_PLAYER, 0.0f, LEVEL, rate, 0, &turn, &tilt);
	CHECK(turn == 0.0f && tilt == 0.0f, 613);
	/* the sensor's noise is scaled down towards nothing: a degree a second is 0.0028 rotations a second, 35% of the 0.008 that is taken whole */
	wimote_motion_reset(&m);
	rate[2] = 1.0f;
	wimote_motion_step(&m, WIIU_GYRO_SPACE_PLAYER, 0.1f, LEVEL, rate, 0, &turn, &tilt);
	CHECK(turn > 0.0f && turn < 0.000278f * 0.5f && NEAR(turn, 0.000278f * (0.002778f / 0.008f), 0.00002f), 614);
	/* a shaken Remote (the accelerometer far from one g) does not move the idea of up, and with none yet known the yaw is the turn */
	wimote_motion_reset(&m);
	{
		static const float SHAKE[3] = { 2.0f, 2.0f, 2.0f };

		rate[0] = 0.0f; rate[1] = 36.0f; rate[2] = 36.0f;
		wimote_motion_step(&m, WIIU_GYRO_SPACE_PLAYER, 0.1f, SHAKE, rate, 0, &turn, &tilt);
		CHECK(!m.gravity_known && NEAR(turn, 0.01f, 0.00005f), 615);
		wimote_motion_step(&m, WIIU_GYRO_SPACE_PLAYER, 0.1f, LEVEL, rate, 0, &turn, &tilt);
		CHECK(m.gravity_known, 616);
		wimote_motion_step(&m, WIIU_GYRO_SPACE_PLAYER, 0.1f, SHAKE, rate, 0, &turn, &tilt);
		CHECK(NEAR(m.up[2], 1.0f, 0.0001f), 617);
	}
	return 0;
}

static int still_checks(void)
{
	struct wimote_motion m;
	static const float REST[3] = { 0.5f, -1.0f, 0.2f }, MOVING[3] = { 0.0f, 10.0f, 0.0f };
	int step, told = 0, first_told = -1;

	wimote_motion_reset(&m);
	/* held still, in tenths of a second: zeroed once, after a second and a half and not before, and not again while it lies there */
	for (step = 1; step <= 40; step++)
	{
		if (wimote_motion_still(&m, 0.1f, REST))
		{
			told++;
			if (first_told < 0)
				first_told = step;
		}
	}
	CHECK(told == 1 && first_told >= 15 && first_told <= 16, 700);
	/* moved: it may be zeroed again after the next time it is held still, not before */
	CHECK(!wimote_motion_still(&m, 0.1f, MOVING), 701);
	told = 0;
	for (step = 1; step <= 10; step++)
		told += wimote_motion_still(&m, 0.1f, REST);
	CHECK(told == 0, 702);
	for (step = 1; step <= 10; step++)
		told += wimote_motion_still(&m, 0.1f, REST);
	CHECK(told == 1, 703);
	/* a movement in the middle of the wait starts it over */
	wimote_motion_reset(&m);
	for (step = 1; step <= 12; step++)
		told += wimote_motion_still(&m, 0.1f, REST);
	CHECK(!wimote_motion_still(&m, 0.1f, MOVING), 704);
	told = 0;
	for (step = 1; step <= 12; step++)
		told += wimote_motion_still(&m, 0.1f, REST);
	CHECK(told == 0, 705);
	return 0;
}

/* the edge turn: the box in rotations (20 degrees), the range (25 degrees); samples of a tenth of a second */
#define BOX (20.0f / 360.0f)
#define RANGE (25.0f / 360.0f)

static int edge_checks(void)
{
	struct wimote_motion m;
	float extra_turn, extra_tilt, last = 0.0f;
	int step;

	/* inside the box: nothing is added */
	wimote_motion_reset(&m);
	for (step = 0; step < 5; step++)
	{
		wimote_motion_edge(&m, 0.01f, 0.0f, 0.1f, BOX, RANGE, 1.0f, &extra_turn, &extra_tilt);
		CHECK(extra_turn == 0.0f && extra_tilt == 0.0f, 800);
	}
	/* past the box: it keeps turning the same way, more the further out, and no more than the quickest at the range */
	wimote_motion_reset(&m);
	for (step = 0; step < 6; step++)
		wimote_motion_edge(&m, 0.02f, 0.0f, 0.1f, BOX, RANGE, 1.0f, &extra_turn, &extra_tilt);
	CHECK(m.edge_x > BOX, 801);
	CHECK(extra_turn > 0.0f && extra_tilt == 0.0f, 802);
	last = extra_turn;
	wimote_motion_edge(&m, 0.02f, 0.0f, 0.1f, BOX, RANGE, 1.0f, &extra_turn, &extra_tilt);
	CHECK(extra_turn >= last, 803);
	for (step = 0; step < 40; step++)
		wimote_motion_edge(&m, 0.02f, 0.0f, 0.1f, BOX, RANGE, 1.0f, &extra_turn, &extra_tilt);
	/* (clamped at box + range, the quickest: 0.30 rotations a second, 0.03 a sample) */
	CHECK(NEAR(m.edge_x, BOX + RANGE, 0.0001f), 804);
	CHECK(NEAR(extra_turn, 0.03f, 0.0005f), 805);
	/* the other way round it turns the other way, and the tilt has its own axis */
	wimote_motion_reset(&m);
	for (step = 0; step < 10; step++)
		wimote_motion_edge(&m, -0.03f, 0.03f, 0.1f, BOX, RANGE, 1.0f, &extra_turn, &extra_tilt);
	CHECK(extra_turn < 0.0f && extra_tilt > 0.0f, 806);
	/* speed 2 is twice as quick at the quickest, speed 0 and a bad box or range are no edge turn */
	wimote_motion_reset(&m);
	for (step = 0; step < 40; step++)
		wimote_motion_edge(&m, 0.02f, 0.0f, 0.1f, BOX, RANGE, 2.0f, &extra_turn, &extra_tilt);
	CHECK(NEAR(extra_turn, 0.06f, 0.001f), 807);
	wimote_motion_reset(&m);
	for (step = 0; step < 40; step++)
	{
		wimote_motion_edge(&m, 0.02f, 0.0f, 0.1f, BOX, RANGE, 0.0f, &extra_turn, &extra_tilt);
		CHECK(extra_turn == 0.0f, 808);
		wimote_motion_edge(&m, 0.02f, 0.0f, 0.1f, 0.0f, RANGE, 1.0f, &extra_turn, &extra_tilt);
		CHECK(extra_turn == 0.0f, 809);
		wimote_motion_edge(&m, 0.02f, 0.0f, 0.1f, BOX, -1.0f, 1.0f, &extra_turn, &extra_tilt);
		CHECK(extra_turn == 0.0f, 810);
	}
	/* a late sample counts for no more than a tenth of a second, and no time is no turn */
	wimote_motion_reset(&m);
	m.edge_x = BOX + RANGE;
	wimote_motion_edge(&m, 0.0f, 0.0f, 5.0f, BOX, RANGE, 1.0f, &extra_turn, &extra_tilt);
	CHECK(NEAR(extra_turn, 0.03f, 0.0015f), 811);
	wimote_motion_edge(&m, 0.0f, 0.0f, 0.0f, BOX, RANGE, 1.0f, &extra_turn, &extra_tilt);
	CHECK(extra_turn == 0.0f, 812);
	/* the kept turn leaks away while the Remote is held still, so that a drift does not turn the view for ever */
	wimote_motion_reset(&m);
	m.edge_x = BOX + RANGE;
	for (step = 0; step < 150; step++)
		wimote_motion_edge(&m, 0.0f, 0.0f, 0.1f, BOX, RANGE, 1.0f, &extra_turn, &extra_tilt);
	CHECK(m.edge_x < BOX && extra_turn == 0.0f, 813);
	/* but not while it is turned at a rate (about 10 degrees a second and over): the kept turn stays where it is */
	wimote_motion_reset(&m);
	m.edge_x = BOX + RANGE * 0.5f;
	wimote_motion_edge(&m, 0.0f, 0.05f, 0.1f, BOX, RANGE, 1.0f, &extra_turn, &extra_tilt);
	CHECK(NEAR(m.edge_x, BOX + RANGE * 0.5f, 0.00001f), 814);
	wimote_motion_reset(&m);
	m.edge_x = BOX + RANGE * 0.5f;
	wimote_motion_edge(&m, 0.0f, 0.001f, 0.1f, BOX, RANGE, 1.0f, &extra_turn, &extra_tilt);
	CHECK(m.edge_x < BOX + RANGE * 0.5f - 0.0001f, 815);
	/* recentred (the Remote was zeroed): the middle is where it points now */
	m.edge_x = 0.2f;
	m.edge_y = -0.1f;
	wimote_motion_recentre(&m);
	CHECK(m.edge_x == 0.0f && m.edge_y == 0.0f, 816);
	return 0;
}

int main(void)
{
	int result;

	if ((result = layout_checks(WIIMOTE_LAYOUT_DEFAULT, DEFAULT, sizeof(DEFAULT) / sizeof(DEFAULT[0]), 100)))
		return result;
	if ((result = layout_checks(WIIMOTE_LAYOUT_Z_FIRE, Z_FIRE, sizeof(Z_FIRE) / sizeof(Z_FIRE[0]), 110)))
		return result;
	if ((result = layout_checks(WIIMOTE_LAYOUT_MENU, MENU, sizeof(MENU) / sizeof(MENU[0]), 120)))
		return result;
	if ((result = rate_checks()) || (result = pointer_checks()) || (result = look_checks()) || (result = extension_checks()) || (result = stick_checks()) ||
		(result = motion_checks()) || (result = still_checks()) || (result = edge_checks()))
		return result;
	return 0;
}
'''


def compiler():
    """cc on the build machine; with HALO_HARNESS_TARGET (tools/ppc-be/run.sh harness), clang for that machine"""
    target = os.environ.get("HALO_HARNESS_TARGET")
    return [os.environ.get("CC", "clang"), *target.split()] if target else ["cc"]


@pytest.mark.parametrize("fault", list(FAULTS))
def test_wii_remote(tmp_path, fault):
    source = read(SOURCE)
    if FAULTS[fault]:
        source = mutated(source, *FAULTS[fault])
    (tmp_path / "wiiu_wimote.c").write_text(source)
    (tmp_path / "checks.c").write_text(CHECKS)
    executable = tmp_path / "wimote"
    # (a fault may leave a function or an argument unused, which is no warning to fail on)
    relaxed = ["-Wno-unused-function", "-Wno-unused-parameter", "-Wno-unused-variable", "-Wno-unused-but-set-variable"] if FAULTS[fault] else []
    subprocess.run([*compiler(), "-std=c99", "-O2", "-ffp-contract=off", "-Wall", "-Wextra", "-Werror", *relaxed, "-I", str(ROOT / "port/wiiu/src"),
                    str(tmp_path / "wiiu_wimote.c"), str(tmp_path / "checks.c"), "-o", str(executable), "-lm"], check=True)
    result = subprocess.run([*runner(), str(executable)], capture_output=True, text=True, check=False)
    if fault == "kept":
        assert result.returncode == 0, result.stdout
    else:
        assert result.returncode != 0, f"the fault {fault} passed every check"
