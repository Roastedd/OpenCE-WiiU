/*
WIIU_WIMOTE.C

The Wii Remote's buttons, Nunchuk, pointer and MotionPlus as the game's controller (wiiu_wimote.h), from the values wiiu_input.c reads from KPAD: no system call
here, so that the desktop tests it (tools/harness/tests/test_wiiu_wimote.py).

The Xbox pad's buttons, as xinput_wiiu.c's other controllers have them and the README's Controls table says: A jumps, B is the melee, X the action and reload, Y
switches weapons; the right trigger fires and the left throws grenades; the white button is the flashlight and the black one switches grenades; the left stick's
click crouches and the right stick's zooms; Start pauses and Back is Back. In the menus A selects and B goes back, X and Y are the screens' prompts. A Remote lays
those out in the Xbox's own terms (WIIU_XBOX_*, reaching the game as they are), not in the Wii U's, whose A, B, X and Y change places with input.wiiu_face_buttons.
*/

#include "wiiu_wimote.h"

#include <math.h>
#include <string.h>

/* ---------- the settings' words */

int wimote_parse_layout(const char *setting)
{
	return !strcmp(setting, "z_fire") ? WIIMOTE_LAYOUT_Z_FIRE : WIIMOTE_LAYOUT_DEFAULT;
}

const char *wimote_layout_name(int layout)
{
	return layout == WIIMOTE_LAYOUT_Z_FIRE ? "z_fire" : layout == WIIMOTE_LAYOUT_MENU ? "menu" : "default";
}

int wimote_parse_look(const char *setting)
{
	return !strcmp(setting, "pointer") ? WIIMOTE_LOOK_POINTER : !strcmp(setting, "motion") ? WIIMOTE_LOOK_MOTION : !strcmp(setting, "off") ? WIIMOTE_LOOK_OFF :
		WIIMOTE_LOOK_AUTO;
}

const char *wimote_look_name(int look)
{
	return look == WIIMOTE_LOOK_POINTER ? "pointer" : look == WIIMOTE_LOOK_MOTION ? "motion" : look == WIIMOTE_LOOK_OFF ? "off" : "auto";
}

const char *wimote_source_name(int source)
{
	return source == WIIMOTE_SOURCE_POINTER ? "the pointer" : source == WIIMOTE_SOURCE_MOTION ? "the MotionPlus" : "none";
}

/* ---------- the extension */

int wimote_reads(unsigned int extension)
{
	return extension == WIIMOTE_EXT_CORE || extension == WIIMOTE_EXT_NUNCHUK || extension == WIIMOTE_EXT_MPLUS || extension == WIIMOTE_EXT_MPLUS_NUNCHUK;
}

int wimote_has_nunchuk(unsigned int extension)
{
	return extension == WIIMOTE_EXT_NUNCHUK || extension == WIIMOTE_EXT_MPLUS_NUNCHUK;
}

int wimote_has_motion(unsigned int extension)
{
	return extension == WIIMOTE_EXT_MPLUS || extension == WIIMOTE_EXT_MPLUS_NUNCHUK;
}

unsigned int wimote_mpls_mode_wanted(unsigned int extension)
{
	if (!wimote_reads(extension))
		return WIIMOTE_MPLS_NONE;
	return wimote_has_nunchuk(extension) ? WIIMOTE_MPLS_NUNCHUK : WIIMOTE_MPLS_ONLY;
}

const char *wimote_extension_name(unsigned int extension)
{
	switch (extension)
	{
	case WIIMOTE_EXT_CORE: return "no extension";
	case WIIMOTE_EXT_NUNCHUK: return "a Nunchuk";
	case WIIMOTE_EXT_CLASSIC: return "a Classic Controller";
	case WIIMOTE_EXT_MPLUS: return "the MotionPlus";
	case WIIMOTE_EXT_MPLUS_NUNCHUK: return "the MotionPlus and a Nunchuk";
	case WIIMOTE_EXT_MPLUS_CLASSIC: return "the MotionPlus and a Classic Controller";
	case WIIMOTE_EXT_PRO_CONTROLLER: return "a Pro Controller";
	default: return "an extension the game does not read";
	}
}

/* ---------- the buttons and the Nunchuk's stick */

unsigned int wimote_hold(unsigned int hold, unsigned int nunchuk_hold)
{
	return hold | (nunchuk_hold & (WIIMOTE_BUTTON_Z | WIIMOTE_BUTTON_C));
}

int wimote_layout_for(unsigned int extension, int layout)
{
	return wimote_has_nunchuk(extension) ? layout : WIIMOTE_LAYOUT_MENU;
}

/* (a Nunchuk at rest reads within a few hundredths of the centre; its stick's gate stops short of the corners of the square it reports in, at about this radius) */
#define STICK_DEAD 0.10f
#define STICK_REACH 0.85f

void wimote_stick(float x, float y, float *out_x, float *out_y)
{
	const float length = sqrtf(x * x + y * y);
	float rate;

	*out_x = *out_y = 0.0f;
	if (!(length > STICK_DEAD))
		return;
	rate = (length - STICK_DEAD) / (STICK_REACH - STICK_DEAD);
	if (rate > 1.0f)
		rate = 1.0f;
	*out_x = x / length * rate;
	*out_y = y / length * rate;
}

struct binding
{
	unsigned int source;
	/* the Wii U pad's buttons it presses (not the face buttons: those are the Xbox's) */
	unsigned int pad;
	/* the Xbox's face buttons it presses */
	unsigned int face;
};

/* B fires, A jumps, Z throws a grenade, C crouches, 1 reloads, 2 strikes, the d-pad up switches weapons, left is the flashlight, right switches grenades, down zooms */
static const struct binding default_layout[] =
{
	{ WIIMOTE_BUTTON_B, WIIU_PAD_ZR, 0 },
	{ WIIMOTE_BUTTON_A, 0, WIIU_XBOX_A },
	{ WIIMOTE_BUTTON_Z, WIIU_PAD_ZL, 0 },
	{ WIIMOTE_BUTTON_C, WIIU_PAD_STICK_L, 0 },
	{ WIIMOTE_BUTTON_1, 0, WIIU_XBOX_X },
	{ WIIMOTE_BUTTON_2, 0, WIIU_XBOX_B },
	{ WIIMOTE_BUTTON_UP, 0, WIIU_XBOX_Y },
	{ WIIMOTE_BUTTON_LEFT, WIIU_PAD_L, 0 },
	{ WIIMOTE_BUTTON_RIGHT, WIIU_PAD_R, 0 },
	{ WIIMOTE_BUTTON_DOWN, WIIU_PAD_STICK_R, 0 },
	{ WIIMOTE_BUTTON_PLUS, WIIU_PAD_PLUS, 0 },
	{ WIIMOTE_BUTTON_MINUS, WIIU_PAD_MINUS, 0 },
};

/* the same with Z and B changed */
static const struct binding z_fire_layout[] =
{
	{ WIIMOTE_BUTTON_Z, WIIU_PAD_ZR, 0 },
	{ WIIMOTE_BUTTON_A, 0, WIIU_XBOX_A },
	{ WIIMOTE_BUTTON_B, WIIU_PAD_ZL, 0 },
	{ WIIMOTE_BUTTON_C, WIIU_PAD_STICK_L, 0 },
	{ WIIMOTE_BUTTON_1, 0, WIIU_XBOX_X },
	{ WIIMOTE_BUTTON_2, 0, WIIU_XBOX_B },
	{ WIIMOTE_BUTTON_UP, 0, WIIU_XBOX_Y },
	{ WIIMOTE_BUTTON_LEFT, WIIU_PAD_L, 0 },
	{ WIIMOTE_BUTTON_RIGHT, WIIU_PAD_R, 0 },
	{ WIIMOTE_BUTTON_DOWN, WIIU_PAD_STICK_R, 0 },
	{ WIIMOTE_BUTTON_PLUS, WIIU_PAD_PLUS, 0 },
	{ WIIMOTE_BUTTON_MINUS, WIIU_PAD_MINUS, 0 },
};

/* a Remote alone, held pointing: the d-pad, A selects, B goes back, 1 and 2 the prompts' X and Y */
static const struct binding menu_layout[] =
{
	{ WIIMOTE_BUTTON_UP, WIIU_PAD_UP, 0 },
	{ WIIMOTE_BUTTON_DOWN, WIIU_PAD_DOWN, 0 },
	{ WIIMOTE_BUTTON_LEFT, WIIU_PAD_LEFT, 0 },
	{ WIIMOTE_BUTTON_RIGHT, WIIU_PAD_RIGHT, 0 },
	{ WIIMOTE_BUTTON_A, 0, WIIU_XBOX_A },
	{ WIIMOTE_BUTTON_B, 0, WIIU_XBOX_B },
	{ WIIMOTE_BUTTON_1, 0, WIIU_XBOX_X },
	{ WIIMOTE_BUTTON_2, 0, WIIU_XBOX_Y },
	{ WIIMOTE_BUTTON_PLUS, WIIU_PAD_PLUS, 0 },
	{ WIIMOTE_BUTTON_MINUS, WIIU_PAD_MINUS, 0 },
};

void wimote_map(int layout, unsigned int hold, float stick_x, float stick_y, struct wiiu_pad *pad)
{
	const struct binding *table = layout == WIIMOTE_LAYOUT_Z_FIRE ? z_fire_layout : layout == WIIMOTE_LAYOUT_MENU ? menu_layout : default_layout;
	const unsigned int count = layout == WIIMOTE_LAYOUT_Z_FIRE ? (unsigned int)(sizeof(z_fire_layout) / sizeof(z_fire_layout[0])) :
		layout == WIIMOTE_LAYOUT_MENU ? (unsigned int)(sizeof(menu_layout) / sizeof(menu_layout[0])) :
		(unsigned int)(sizeof(default_layout) / sizeof(default_layout[0]));
	unsigned int i;

	memset(pad, 0, sizeof(*pad));
	for (i = 0; i < count; i++)
	{
		if (hold & table[i].source)
		{
			pad->buttons |= table[i].pad;
			pad->xbox_faces |= table[i].face;
		}
	}
	/* (a Remote alone has no stick: with the menu's layout the Nunchuk's, if one is read, is not) */
	if (layout != WIIMOTE_LAYOUT_MENU)
		wimote_stick(stick_x, stick_y, &pad->left_x, &pad->left_y);
}

/* ---------- the pointer */

/* (the game turns a stick's value by fix_dead_zone: nothing up to 9000 of 32767, then 0 to full over the rest; the rate is lifted past it so that it begins from nothing) */
#define GAME_DEAD_RANGE 0.2750f
#define POINTER_BOX_DEFAULT 0.3f
#define POINTER_BOX_MAXIMUM 0.9f
#define POINTER_SPEED_DEFAULT 1.0f
#define POINTER_SPEED_MAXIMUM 4.0f

float wimote_pointer_rate(float position, float box, float speed)
{
	float distance = position < 0.0f ? -position : position;
	float rate;

	if (!(box >= 0.0f && box <= POINTER_BOX_MAXIMUM))
		box = POINTER_BOX_DEFAULT;
	if (!(speed > 0.0f && speed <= POINTER_SPEED_MAXIMUM))
		speed = POINTER_SPEED_DEFAULT;
	if (!(distance > box))
		return 0.0f;
	/* (0 at the box's edge to 1 at the edge of the range, and beyond it no more) */
	distance = (distance - box) / (1.0f - box);
	if (distance > 1.0f)
		distance = 1.0f;
	/* (it picks up gently past the edge and grows faster out: a little way out is a slow turn, the screen's edge the quickest) */
	rate = speed * (0.3f * distance + 0.7f * distance * distance);
	if (rate > 1.0f)
		rate = 1.0f;
	return position < 0.0f ? -rate : rate;
}

static float lifted(float rate)
{
	if (rate == 0.0f)
		return 0.0f;
	return rate < 0.0f ? -(GAME_DEAD_RANGE + (1.0f - GAME_DEAD_RANGE) * -rate) : GAME_DEAD_RANGE + (1.0f - GAME_DEAD_RANGE) * rate;
}

void wimote_pointer_look(int valid, float x, float y, float box, float speed, float *look_x, float *look_y)
{
	*look_x = *look_y = 0.0f;
	if (!valid)
		return;
	*look_x = lifted(wimote_pointer_rate(x, box, speed));
	/* (the pointer's y is positive down, the stick's up) */
	*look_y = lifted(-wimote_pointer_rate(y, box, speed));
}

/* ---------- which look */

int wimote_look_source(int setting, int has_motion, int first_player, int pointer_valid)
{
	const int motion = has_motion && first_player;

	switch (setting)
	{
	case WIIMOTE_LOOK_POINTER:
		return WIIMOTE_SOURCE_POINTER;
	case WIIMOTE_LOOK_MOTION:
		return motion ? WIIMOTE_SOURCE_MOTION : WIIMOTE_SOURCE_NONE;
	case WIIMOTE_LOOK_OFF:
		return WIIMOTE_SOURCE_NONE;
	default:
		return motion ? WIIMOTE_SOURCE_MOTION : pointer_valid ? WIIMOTE_SOURCE_POINTER : WIIMOTE_SOURCE_NONE;
	}
}

int wimote_rumble_allowed(int source)
{
	return source != WIIMOTE_SOURCE_MOTION;
}

/* ---------- the motion */

/* (rates under this many rotations a second, about 3 degrees, are scaled down towards nothing: the GamePad's tightening, wiiu_input.c) */
#define MOTION_TIGHTENING 0.008f
/* (the share of each sample the up estimate takes from the accelerometer) */
#define MOTION_UP_FOLLOW 0.05f
/* (degrees a second under which a Remote is held still, and for how long before it is zeroed) */
#define STILL_RATE 3.0f
#define STILL_SECONDS 1.5f

void wimote_motion_reset(struct wimote_motion *motion)
{
	memset(motion, 0, sizeof(*motion));
}

void wimote_motion_step(struct wimote_motion *motion, int space, float seconds, const float acc[3], const float rate[3], int invert_turn, float *turn, float *tilt)
{
	float length = sqrtf(acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2]);
	float turned[3], magnitude, scale, result;
	int i;

	*turn = *tilt = 0.0f;
	if (!(seconds > 0.0f))
		return;
	if (seconds > 0.1f)
		seconds = 0.1f;
	/* the accelerometer measures the push that holds the Remote up: up, in its axes (a reading far from one g is a shake, not gravity) */
	if (length > 0.5f && length < 2.0f)
	{
		if (!motion->gravity_known)
		{
			for (i = 0; i < 3; i++)
				motion->up[i] = acc[i] / length;
			motion->gravity_known = 1;
		}
		else
		{
			for (i = 0; i < 3; i++)
				motion->up[i] += (acc[i] / length - motion->up[i]) * MOTION_UP_FOLLOW;
			length = sqrtf(motion->up[0] * motion->up[0] + motion->up[1] * motion->up[1] + motion->up[2] * motion->up[2]);
			if (length > 0.0f)
				for (i = 0; i < 3; i++)
					motion->up[i] /= length;
		}
	}
	/* how far it turned about each of its axes, in rotations */
	for (i = 0; i < 3; i++)
		turned[i] = rate[i] / 360.0f * seconds;
	if (space == WIIU_GYRO_SPACE_TURN)
	{
		/* its own yaw */
		result = turned[2];
	}
	else if (space == WIIU_GYRO_SPACE_LEAN)
	{
		/* its own roll, about its length: leaning it to the right turns the view right, which is the negative of the left-positive turn */
		result = -turned[1];
	}
	else if (!motion->gravity_known)
	{
		result = turned[2];
	}
	else
	{
		/* player space (GyroWiki): the direction from the up the yaw and the roll turn about, the size from the two together */
		const float world = turned[2] * motion->up[2] + turned[1] * motion->up[1];
		const float joined = sqrtf(turned[2] * turned[2] + turned[1] * turned[1]);
		float size = fabsf(world) * 1.41f;

		if (size > joined)
			size = joined;
		result = world < 0.0f ? -size : size;
	}
	/* (its nose lifting is positive about x, and looks up: the tilt is the other way, as the GamePad's top away looks down) */
	*tilt = -turned[0];
	*turn = invert_turn ? -result : result;
	magnitude = sqrtf(result * result + turned[0] * turned[0]) / seconds;
	scale = magnitude < MOTION_TIGHTENING ? magnitude / MOTION_TIGHTENING : 1.0f;
	*turn *= scale;
	*tilt *= scale;
}

/* (the share of the kept turn that leaks away each second while the Remote turns slowly, and what is slowly: rotations a second, about 10 degrees) */
#define EDGE_LEAK 0.15f
#define EDGE_SLOW 0.03f

static float edge_axis(float *kept, float step, float seconds, float box, float range, float speed, float slow)
{
	float distance, rate, sign;

	*kept += step;
	if (slow)
		*kept -= *kept * EDGE_LEAK * seconds;
	if (*kept > box + range)
		*kept = box + range;
	else if (*kept < -(box + range))
		*kept = -(box + range);
	sign = *kept < 0.0f ? -1.0f : 1.0f;
	distance = *kept < 0.0f ? -*kept : *kept;
	if (!(distance > box))
		return 0.0f;
	/* (0 at the box's edge to 1 at its range: gently at first, quickest at the far side, as the pointer's) */
	distance = (distance - box) / range;
	if (distance > 1.0f)
		distance = 1.0f;
	rate = speed * WIIMOTE_EDGE_RATE * (0.3f * distance + 0.7f * distance * distance);
	return sign * rate * seconds;
}

void wimote_motion_edge(struct wimote_motion *motion, float turn, float tilt, float seconds, float box, float range, float speed, float *extra_turn, float *extra_tilt)
{
	float rate;

	*extra_turn = *extra_tilt = 0.0f;
	if (!(seconds > 0.0f) || !(box > 0.0f && box <= 0.5f) || !(range > 0.0f && range <= 0.5f) || !(speed > 0.0f && speed <= POINTER_SPEED_MAXIMUM))
		return;
	if (seconds > 0.1f)
		seconds = 0.1f;
	rate = sqrtf(turn * turn + tilt * tilt) / seconds;
	*extra_turn = edge_axis(&motion->edge_x, turn, seconds, box, range, speed, rate < EDGE_SLOW);
	*extra_tilt = edge_axis(&motion->edge_y, tilt, seconds, box, range, speed, rate < EDGE_SLOW);
}

void wimote_motion_recentre(struct wimote_motion *motion)
{
	motion->edge_x = motion->edge_y = 0.0f;
}

int wimote_motion_still(struct wimote_motion *motion, float seconds, const float rate[3])
{
	if (fabsf(rate[0]) >= STILL_RATE || fabsf(rate[1]) >= STILL_RATE || fabsf(rate[2]) >= STILL_RATE)
	{
		motion->still = 0.0f;
		motion->still_told = 0;
		return 0;
	}
	if (seconds > 0.1f)
		seconds = 0.1f;
	if (seconds > 0.0f)
		motion->still += seconds;
	if (motion->still >= STILL_SECONDS && !motion->still_told)
	{
		motion->still_told = 1;
		return 1;
	}
	return 0;
}
