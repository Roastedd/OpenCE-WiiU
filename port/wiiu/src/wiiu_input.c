/*
WIIU_INPUT.C

The GamePad (VPAD), the Pro Controllers, the Classic Controllers and the Wii Remotes (KPAD) read as wiiu_input.h's pads, and their motors rumble. A Wii Remote
with a Nunchuk is a controller of the game's own layout (wiiu_wimote.c: input.wiiu_wiimote_layout): the Nunchuk's stick is the left stick, and the view turns by
the Remote's IR pointer outside a free-aim box in the middle, or by its MotionPlus (a Wii Remote Plus, or a MotionPlus adapter), as input.wiiu_wiimote_look says;
a Remote alone has the menus' layout (d-pad, A, B, 1, 2). A Remote with a Classic Controller is read as the Classic Controller.

Which port (the game's player) each controller drives is wiiu_pad_players.c's: a map from the controllers, known by their Bluetooth addresses, to players,
which gives one that goes and comes back its player again whatever channel the system gives it, and the layout input.wiiu_controller_layout asks for. Once a
frame (wiiu_input_poll, from xinput_wiiu.c's XGetDeviceChanges) every channel is read, the controllers that came and went are told to the map (the system's
connect callback counts the changes a read could miss), the layout is arranged, and each controller's lights are set to the number of the player it drives.
*/

#include "wiiu_input.h"
#include "wiiu_pad_players.h"
#include "wiiu_wimote.h"

#include "wiiu_debug.h"
#include "wiiu_shell.h"

#include <padscore/kpad.h>
#include <padscore/wpad.h>
#include <vpad/input.h>
#include <vpadbase/base.h>

#include <coreinit/time.h>

#include <malloc.h>
#include <stdlib.h>
#include <string.h>

/* the Remote's button and extension numbers in wiiu_wimote.h are wut's */
_Static_assert(WIIMOTE_BUTTON_LEFT == WPAD_BUTTON_LEFT && WIIMOTE_BUTTON_RIGHT == WPAD_BUTTON_RIGHT && WIIMOTE_BUTTON_DOWN == WPAD_BUTTON_DOWN &&
	WIIMOTE_BUTTON_UP == WPAD_BUTTON_UP && WIIMOTE_BUTTON_PLUS == WPAD_BUTTON_PLUS && WIIMOTE_BUTTON_2 == WPAD_BUTTON_2 && WIIMOTE_BUTTON_1 == WPAD_BUTTON_1 &&
	WIIMOTE_BUTTON_B == WPAD_BUTTON_B && WIIMOTE_BUTTON_A == WPAD_BUTTON_A && WIIMOTE_BUTTON_MINUS == WPAD_BUTTON_MINUS && WIIMOTE_BUTTON_Z == WPAD_BUTTON_Z &&
	WIIMOTE_BUTTON_C == WPAD_BUTTON_C, "wiiu_wimote.h's buttons are wut's");
_Static_assert(WIIMOTE_EXT_CORE == WPAD_EXT_CORE && WIIMOTE_EXT_NUNCHUK == WPAD_EXT_NUNCHUK && WIIMOTE_EXT_CLASSIC == WPAD_EXT_CLASSIC &&
	WIIMOTE_EXT_MPLUS == WPAD_EXT_MPLUS && WIIMOTE_EXT_MPLUS_NUNCHUK == WPAD_EXT_MPLUS_NUNCHUK && WIIMOTE_EXT_MPLUS_CLASSIC == WPAD_EXT_MPLUS_CLASSIC &&
	WIIMOTE_EXT_PRO_CONTROLLER == WPAD_EXT_PRO_CONTROLLER, "wiiu_wimote.h's extensions are wut's");
_Static_assert(WIIMOTE_MPLS_ONLY == WPAD_MPLS_MODE_MPLS_ONLY && WIIMOTE_MPLS_NUNCHUK == WPAD_MPLS_MODE_MPLS_NUNCHUK, "wiiu_wimote.h's MotionPlus modes are wut's");

static int started;

/* The system's connections and disconnections (KPADSetConnectCallback). The callback runs on the system's thread and only counts; the game's thread compares
the counts in its next poll, so that a controller that went and came back between two polls is seen to have, and is named and lit again (the system lights a
controller with its channel's number when it connects). */
static volatile unsigned int connections[WIIU_PAD_CHANNELS];
static volatile int connection_status[WIIU_PAD_CHANNELS];
static unsigned int connections_seen[WIIU_PAD_CHANNELS];
static KPADConnectCallback connect_chained[WIIU_PAD_CHANNELS];

static void connect_callback(WPADChan channel, WPADError status)
{
	if ((unsigned int)channel >= WIIU_PAD_CHANNELS)
		return;
	connection_status[channel] = (int)status;
	connections[channel]++;
	/* (one set before this one, if there was: nothing else in the game sets one, but a plugin could) */
	if (connect_chained[channel])
		connect_chained[channel](channel, status);
}

/* The MotionPlus's start-up (KPADSetControlMplsCallback): like the connect callback it runs on the system's thread and only notes what happened; the game's thread
logs it and takes the failure into account in its next poll. */
static volatile unsigned int mpls_events[WIIU_PAD_CHANNELS];
static volatile int mpls_result[WIIU_PAD_CHANNELS];
static unsigned int mpls_events_seen[WIIU_PAD_CHANNELS];

static void mpls_callback(KPADChan channel, KPADControlMplsStatus status)
{
	if ((unsigned int)channel >= WIIU_PAD_CHANNELS)
		return;
	/* (started when KPADEnableMpls is called; the others say how it ended) */
	if (status == KPAD_CONTROL_MPLS_STATUS_STARTED)
		return;
	mpls_result[channel] = (int)status;
	mpls_events[channel]++;
}

static void start(void)
{
	unsigned int channel;

	if (started)
		return;
	started = 1;
	VPADInit();
	KPADInit();
	WPADEnableURCC(TRUE);
	for (channel = 0; channel < WIIU_PAD_CHANNELS; channel++)
	{
		const KPADConnectCallback previous = KPADSetConnectCallback((KPADChan)channel, connect_callback);

		/* (never itself: it would call itself for ever) */
		if (previous != connect_callback)
			connect_chained[channel] = previous;
		KPADSetControlMplsCallback((KPADChan)channel, mpls_callback);
	}
	wiiu_log("Input: KPAD channels 1 to %u read (the system allows %u)", WIIU_PAD_CHANNELS, (unsigned)KPADGetMaxControllers());
}

static unsigned int from_vpad(uint32_t hold)
{
	static const struct
	{
		uint32_t source;
		unsigned int button;
	} table[] =
	{
		{ VPAD_BUTTON_A, WIIU_PAD_A }, { VPAD_BUTTON_B, WIIU_PAD_B }, { VPAD_BUTTON_X, WIIU_PAD_X }, { VPAD_BUTTON_Y, WIIU_PAD_Y },
		{ VPAD_BUTTON_L, WIIU_PAD_L }, { VPAD_BUTTON_R, WIIU_PAD_R }, { VPAD_BUTTON_ZL, WIIU_PAD_ZL }, { VPAD_BUTTON_ZR, WIIU_PAD_ZR },
		{ VPAD_BUTTON_PLUS, WIIU_PAD_PLUS }, { VPAD_BUTTON_MINUS, WIIU_PAD_MINUS }, { VPAD_BUTTON_UP, WIIU_PAD_UP },
		{ VPAD_BUTTON_DOWN, WIIU_PAD_DOWN }, { VPAD_BUTTON_LEFT, WIIU_PAD_LEFT }, { VPAD_BUTTON_RIGHT, WIIU_PAD_RIGHT },
		{ VPAD_BUTTON_STICK_L, WIIU_PAD_STICK_L }, { VPAD_BUTTON_STICK_R, WIIU_PAD_STICK_R }, { VPAD_BUTTON_TV, WIIU_PAD_TV },
	};
	unsigned int result = 0;
	unsigned int i;

	for (i = 0; i < sizeof(table) / sizeof(table[0]); i++)
		if (hold & table[i].source)
			result |= table[i].button;
	return result;
}

static unsigned int from_pro(uint32_t hold)
{
	static const struct
	{
		uint32_t source;
		unsigned int button;
	} table[] =
	{
		{ WPAD_PRO_BUTTON_A, WIIU_PAD_A }, { WPAD_PRO_BUTTON_B, WIIU_PAD_B }, { WPAD_PRO_BUTTON_X, WIIU_PAD_X }, { WPAD_PRO_BUTTON_Y, WIIU_PAD_Y },
		{ WPAD_PRO_BUTTON_L, WIIU_PAD_L }, { WPAD_PRO_BUTTON_R, WIIU_PAD_R }, { WPAD_PRO_BUTTON_ZL, WIIU_PAD_ZL },
		{ WPAD_PRO_BUTTON_ZR, WIIU_PAD_ZR }, { WPAD_PRO_BUTTON_PLUS, WIIU_PAD_PLUS }, { WPAD_PRO_BUTTON_MINUS, WIIU_PAD_MINUS },
		{ WPAD_PRO_BUTTON_UP, WIIU_PAD_UP }, { WPAD_PRO_BUTTON_DOWN, WIIU_PAD_DOWN }, { WPAD_PRO_BUTTON_LEFT, WIIU_PAD_LEFT },
		{ WPAD_PRO_BUTTON_RIGHT, WIIU_PAD_RIGHT }, { WPAD_PRO_BUTTON_STICK_L, WIIU_PAD_STICK_L }, { WPAD_PRO_BUTTON_STICK_R, WIIU_PAD_STICK_R },
	};
	unsigned int result = 0;
	unsigned int i;

	for (i = 0; i < sizeof(table) / sizeof(table[0]); i++)
		if (hold & table[i].source)
			result |= table[i].button;
	return result;
}

static unsigned int from_classic(uint32_t hold)
{
	static const struct
	{
		uint32_t source;
		unsigned int button;
	} table[] =
	{
		{ WPAD_CLASSIC_BUTTON_A, WIIU_PAD_A }, { WPAD_CLASSIC_BUTTON_B, WIIU_PAD_B }, { WPAD_CLASSIC_BUTTON_X, WIIU_PAD_X },
		{ WPAD_CLASSIC_BUTTON_Y, WIIU_PAD_Y }, { WPAD_CLASSIC_BUTTON_L, WIIU_PAD_L }, { WPAD_CLASSIC_BUTTON_R, WIIU_PAD_R },
		{ WPAD_CLASSIC_BUTTON_ZL, WIIU_PAD_ZL }, { WPAD_CLASSIC_BUTTON_ZR, WIIU_PAD_ZR }, { WPAD_CLASSIC_BUTTON_PLUS, WIIU_PAD_PLUS },
		{ WPAD_CLASSIC_BUTTON_MINUS, WIIU_PAD_MINUS }, { WPAD_CLASSIC_BUTTON_UP, WIIU_PAD_UP }, { WPAD_CLASSIC_BUTTON_DOWN, WIIU_PAD_DOWN },
		{ WPAD_CLASSIC_BUTTON_LEFT, WIIU_PAD_LEFT }, { WPAD_CLASSIC_BUTTON_RIGHT, WIIU_PAD_RIGHT },
	};
	unsigned int result = 0;
	unsigned int i;

	for (i = 0; i < sizeof(table) / sizeof(table[0]); i++)
		if (hold & table[i].source)
			result |= table[i].button;
	return result;
}

/* the stick with the larger push of the two wins (a GamePad and a controller on the same port) */
static void merge_stick(float *x, float *y, float other_x, float other_y)
{
	if (other_x * other_x + other_y * other_y > *x * *x + *y * *y)
	{
		*x = other_x;
		*y = other_y;
	}
}

/* the Pro Controllers on KPAD channels (they have motors; a Classic Controller's Wii Remote is not rumbled) */
static unsigned int pro_present;

/* What each controller last said. A read with no new sample (the game asks faster than the pads sample, about 200 times a second; the menus and the
loops that wait for a frame do) leaves the pad as it was, as the system's own examples keep it: reporting it released made the sticks fall to the centre and
the buttons let go between samples, a stutter in every look and a flicker in every held button. Only an error says the pad is gone. connected: something
answers on the channel, read or not (a Wii Remote alone is connected, not present); sampled: the last read had a new sample. */
static struct
{
	int present;
	int connected;
	int sampled;
	struct wiiu_pad pad;
} last_pro[WIIU_PAD_CHANNELS];
static struct wiiu_pad last_vpad;

/* the controllers' players (wiiu_pad_players.c) and the layout asked for (input.wiiu_controller_layout: xinput_wiiu.c) */
static struct wiiu_pad_players pad_players;
static int pad_layout = WIIU_PAD_LAYOUT_AUTO;

/* each channel's lights: the player number they were last set to (-1: not known, as after a connection, which the system lights with its channel's number),
when a setting was last tried (one at a time a tenth of a second apart: the controller's queue refuses one while it is busy), and when to set them once more
after a connection or the HOME Menu (0: no need), in case the system's own lighting of the controller came after ours */
static struct
{
	int shown;
	OSTime tried;
	OSTime again_at;
} lights[WIIU_PAD_CHANNELS] = { { -1, 0, 0 }, { -1, 0, 0 }, { -1, 0, 0 }, { -1, 0, 0 } };
static int lights_foreground;

/* the lights not known: set at the next poll, and again a second later */
static void lights_forget(unsigned int channel, OSTime now)
{
	lights[channel].shown = -1;
	lights[channel].again_at = now + (OSTime)OSSecondsToTicks(1);
}

/* The ranges the GamePad's sticks and gyro have given, in the log every few seconds while they grow: how far a stick goes each way (the look feels different
up and down than left and right when the ranges are) and in what units the gyro is. */
static struct
{
	float low[6], high[6];
	int changed;
	OSTime told;
} ranges;

/* ---------- the gyro as a mouse */

#include <math.h>

static struct
{
	int space;
	int gravity_known;
	float gravity[3]; /* the direction gravity pulls, in the pad's axes, smoothed */
	OSTime last;
	float turn, pitch; /* rotations not yet taken */
	OSTime told;
	int angle_known;
	float last_angle[3]; /* VPADStatus.angle at the last sample */
} gyro_mouse;

void wiiu_input_set_gyro_space(int space)
{
	gyro_mouse.space = space;
}

void wiiu_input_gyro_take(float *turn, float *pitch)
{
	*turn = gyro_mouse.turn;
	*pitch = gyro_mouse.pitch;
	gyro_mouse.turn = gyro_mouse.pitch = 0.0f;
}

/* (rates under this many rotations a second, about 3 degrees, are scaled down towards nothing: the sensor's noise and a steady hand's tremor, which would otherwise
make the view swim; "tightening" in GyroWiki's terms) */
#define GYRO_TIGHTENING 0.008f
/* (the share of each sample the gravity estimate takes from the accelerometer: a few tenths of a second to follow a change of grip) */
#define GRAVITY_FOLLOW 0.05f

static void gyro_mouse_note(const VPADStatus *status, float pitch_rate, float yaw_rate, float roll_rate)
{
	const OSTime now = OSGetSystemTime();
	float seconds = gyro_mouse.last ? (float)OSTicksToMicroseconds(now - gyro_mouse.last) / 1000000.0f : 0.0f;
	/* the accelerometer measures the push that holds the pad up, opposite to gravity */
	float down[3] = { -status->accelorometer.acc.x, -status->accelorometer.acc.y, -status->accelorometer.acc.z };
	float length = sqrtf(down[0] * down[0] + down[1] * down[1] + down[2] * down[2]);
	float turned[3], turn, magnitude, scale;
	int i;

	gyro_mouse.last = now;
	if (seconds > 0.1f)
		seconds = 0.1f;
	if (length > 0.5f && length < 2.0f)
	{
		for (i = 0; i < 3; i++)
			down[i] /= length;
		if (!gyro_mouse.gravity_known)
		{
			memcpy(gyro_mouse.gravity, down, sizeof(down));
			gyro_mouse.gravity_known = 1;
		}
		else
		{
			for (i = 0; i < 3; i++)
				gyro_mouse.gravity[i] += (down[i] - gyro_mouse.gravity[i]) * GRAVITY_FOLLOW;
			length = sqrtf(gyro_mouse.gravity[0] * gyro_mouse.gravity[0] + gyro_mouse.gravity[1] * gyro_mouse.gravity[1] +
				gyro_mouse.gravity[2] * gyro_mouse.gravity[2]);
			if (length > 0.0f)
				for (i = 0; i < 3; i++)
					gyro_mouse.gravity[i] /= length;
		}
	}
	{
		/* how far the pad turned since the last sample, each axis in rotations: the difference of the system's own angle (VPADStatus.angle, the gyro added
		up at the pad's own rate, so every sample between the game's reads counts; on the console it agrees with the rates added up here, same axes and signs),
		or the rates times the time between reads when the angle cannot be had (the first read, a jump) or the debug server holds the rates */
		const float angle[3] = { status->angle.x, status->angle.y, status->angle.z };
		const float rate[3] = { pitch_rate, yaw_rate, roll_rate };
		const int held = pitch_rate != status->gyro.x || yaw_rate != status->gyro.y;

		for (i = 0; i < 3; i++)
		{
			float step = angle[i] - gyro_mouse.last_angle[i];

			if (!gyro_mouse.angle_known || held || step > 0.25f || step < -0.25f)
				step = rate[i] * seconds;
			gyro_mouse.last_angle[i] = angle[i];
			turned[i] = step;
		}
		gyro_mouse.angle_known = 1;
	}
	if (gyro_mouse.space == WIIU_GYRO_SPACE_TURN)
	{
		turn = turned[1];
	}
	else if (gyro_mouse.space == WIIU_GYRO_SPACE_LEAN)
	{
		turn = turned[2];
	}
	else
	{
		/* player space (GyroWiki, "Player Space Gyro (and Alternatives) Explained"): the direction from gravity, the size from the pad's yaw and roll together;
		with the pad held upright gravity pulls along +y (measured: 0.99) and this is the pad's own yaw, positive to the left */
		const float world = turned[1] * gyro_mouse.gravity[1] + turned[2] * gyro_mouse.gravity[2];
		const float joined = sqrtf(turned[1] * turned[1] + turned[2] * turned[2]);
		float size = fabsf(world) * 1.41f;

		if (!gyro_mouse.gravity_known)
		{
			turn = turned[1];
		}
		else
		{
			if (size > joined)
				size = joined;
			turn = world < 0.0f ? -size : size;
		}
	}
	/* (tightening by the speed of the turn, in rotations a second) */
	magnitude = seconds > 0.0f ? sqrtf(turn * turn + turned[0] * turned[0]) / seconds : 0.0f;
	scale = magnitude < GYRO_TIGHTENING ? magnitude / GYRO_TIGHTENING : 1.0f;
	gyro_mouse.turn += turn * scale;
	gyro_mouse.pitch += turned[0] * scale;
	/* (the gravity the pad is held against, in the log now and then: which way the axes point is the system's) */
	if (now - gyro_mouse.told >= OSSecondsToTicks(10))
	{
		gyro_mouse.told = now;
		wiiu_log("Input: GamePad gravity %.2f %.2f %.2f (accelerometer %.2f %.2f %.2f)", gyro_mouse.gravity[0], gyro_mouse.gravity[1], gyro_mouse.gravity[2],
			status->accelorometer.acc.x, status->accelorometer.acc.y, status->accelorometer.acc.z);
	}
}

static void ranges_note(const VPADStatus *status)
{
	const float values[6] = { status->leftStick.x, status->leftStick.y, status->rightStick.x, status->rightStick.y, status->gyro.x, status->gyro.y };
	const OSTime now = OSGetSystemTime();
	int i;

	for (i = 0; i < 6; i++)
	{
		if (values[i] < ranges.low[i] - 0.01f)
		{
			ranges.low[i] = values[i];
			ranges.changed = 1;
		}
		if (values[i] > ranges.high[i] + 0.01f)
		{
			ranges.high[i] = values[i];
			ranges.changed = 1;
		}
	}
	if (ranges.changed && now - ranges.told >= OSSecondsToTicks(3))
	{
		ranges.changed = 0;
		ranges.told = now;
		wiiu_log("Input: GamePad ranges: left x %.2f..%.2f y %.2f..%.2f, right x %.2f..%.2f y %.2f..%.2f, gyro x %.3f..%.3f y %.3f..%.3f", ranges.low[0],
			ranges.high[0], ranges.low[1], ranges.high[1], ranges.low[2], ranges.high[2], ranges.low[3], ranges.high[3], ranges.low[4], ranges.high[4],
			ranges.low[5], ranges.high[5]);
	}
}

void wiiu_input_set_layout(int layout)
{
	pad_layout = layout;
}

/* ---------- the Wii Remotes (wiiu_wimote.h) */

/* input.wiiu_wiimote_* (wiiu_input_set_wimote), and a count that changes with them, so that each Remote has the pointer and the MotionPlus switched as they say */
static struct
{
	int layout;
	int look;
	float box;
	float speed;
	int invert_x;
	unsigned int epoch;
} wimote_settings = { WIIMOTE_LAYOUT_DEFAULT, WIIMOTE_LOOK_AUTO, 0.3f, 1.0f, 0, 1 };

/* What each channel's Remote was last found to be and was asked. A channel that is not a Remote read here has all zero. */
static struct
{
	int remote; /* the last sample was a Remote the game reads */
	unsigned int extension; /* KPADStatus.extensionType of the last sample */
	int layout; /* the layout it has (wimote_layout_for) */
	int source; /* the look it has (WIIMOTE_SOURCE_*) */
	int pointer_valid; /* KPADStatus.posValid, as a yes or no */
	int plus; /* WPADIsMplsIntegrated: 1 a Wii Remote Plus, 0 a Remote, -1 not known */
	int announced; /* the connection has been told in the log: from now on the changes are */
	unsigned int announced_extension;
	int announced_source;
	OSTime announced_at;
	unsigned int applied; /* the settings' epoch the pointer was last switched for */
	OSTime mpls_asked_at;
	int mpls_attempts; /* of KPADEnableMpls for this extension */
	int mpls_failed; /* the extension (plus one) KPAD said the MotionPlus cannot be had with: not asked again until it changes */
	OSTime grace_since; /* the extension was unknown since, a Remote's state is kept for a second meanwhile */
	OSTime last_sample;
	struct wimote_motion motion;
	/* the units of the MotionPlus's rates, in the log now and then: the rates' peak, and how the angles changed over the same time */
	float peak[3];
	float angle_ref[3];
	OSTime angle_ref_at;
	OSTime pointer_told;
	int pointer_logged; /* 1 the pointer was told to see the sensor bar, 2 not to */
	OSTime motion_told;
	unsigned int motion_told_count;
} remotes[WIIU_PAD_CHANNELS];
/* the channels with a Remote the game reads, and those that may rumble (not while their motion turns the view) */
static unsigned int remote_present, remote_rumble;
/* the work area KPAD needs for the MotionPlus (KPADSetMplsWorkarea), given once and kept: 0 not yet, 1 given, -1 could not be had */
static void *mpls_work;
static int mpls_work_state;
/* the first player's Remote's turn, not yet taken (wiiu_input_remote_look_take) */
static struct
{
	float turn, pitch;
} remote_mouse;

static void rumble_stop_all(void);

static void pad_gone(unsigned int channel)
{
	pro_present &= ~(1u << channel);
	remote_present &= ~(1u << channel);
	remote_rumble &= ~(1u << channel);
	memset(&remotes[channel], 0, sizeof(remotes[channel]));
	last_pro[channel].present = 0;
	last_pro[channel].connected = 0;
	memset(&last_pro[channel].pad, 0, sizeof(last_pro[channel].pad));
	/* (whatever connects next is lit again: the system lights it with its channel's number) */
	lights_forget(channel, OSGetSystemTime());
}

void wiiu_input_set_wimote(const char *layout, const char *look, float box, float speed, int invert_x)
{
	const int new_layout = wimote_parse_layout(layout);
	const int new_look = wimote_parse_look(look);
	const int inverted = invert_x ? 1 : 0;

	if (new_layout != wimote_settings.layout || new_look != wimote_settings.look || box != wimote_settings.box || speed != wimote_settings.speed ||
		inverted != wimote_settings.invert_x)
	{
		wimote_settings.layout = new_layout;
		wimote_settings.look = new_look;
		wimote_settings.box = box;
		wimote_settings.speed = speed;
		wimote_settings.invert_x = inverted;
		wimote_settings.epoch++;
		wiiu_log("Input: Wii Remotes: layout %s, look %s, pointer box %.2f, pointer speed %.2f, MotionPlus turn %s", wimote_layout_name(new_layout),
			wimote_look_name(new_look), (double)box, (double)speed, inverted ? "inverted" : "as it is");
	}
}

/* the work area the MotionPlus needs, from the first time it is wanted */
static int mpls_work_ready(void)
{
	if (mpls_work_state == 0)
	{
		const uint32_t size = KPADGetMplsWorkSize();

		mpls_work = size ? memalign(32, (size + 31u) & ~31u) : NULL;
		if (mpls_work)
		{
			memset(mpls_work, 0, (size + 31u) & ~31u);
			KPADSetMplsWorkarea(mpls_work);
			mpls_work_state = 1;
			wiiu_log("Input: the MotionPlus's work area, %u bytes, is given to KPAD", (unsigned)size);
		}
		else
		{
			mpls_work_state = -1;
			wiiu_log("Input: the MotionPlus's work area (%u bytes) could not be had: no Wii Remote turns the view by its MotionPlus", (unsigned)size);
		}
	}
	return mpls_work_state > 0;
}

/* the look a channel's Remote has now, from what it was last found to be */
static int remote_source(unsigned int channel)
{
	return wimote_look_source(wimote_settings.look, wimote_has_motion(remotes[channel].extension) && mpls_work_state > 0,
		wiiu_pad_players_port(&pad_players, (int)channel) == 0, remotes[channel].pointer_valid);
}

/* what the log says of a channel's Remote: its extension, layout and look */
static void remote_describe(unsigned int channel, const char *what)
{
	wiiu_log("Input: %s channel %u's Wii Remote%s: extension %s, layout %s, look %s (the setting is %s%s)", what, channel + 1,
		remotes[channel].plus == 1 ? " Plus" : "", wimote_extension_name(remotes[channel].extension), wimote_layout_name(remotes[channel].layout),
		wimote_source_name(remotes[channel].source), wimote_look_name(wimote_settings.look),
		wimote_settings.look == WIIMOTE_LOOK_AUTO && remotes[channel].source == WIIMOTE_SOURCE_NONE ? ": the pointer once it sees the sensor bar" : "");
}

/* A Remote's pointer and MotionPlus switched as the settings say and the extension needs: the IR pointer on for "auto" and "pointer", off otherwise (the camera
costs the Remote's battery); the MotionPlus asked for ("auto" and "motion") in the mode the extension needs, again if the extension changes (a Nunchuk plugged in or
out), not again where KPAD said it cannot be had (a Remote without a MotionPlus: the answer is the callback's) and at most three times without an answer. */
static void remote_control(unsigned int channel, unsigned int extension, OSTime now)
{
	const KPADChan chan = (KPADChan)channel;
	const int wants_pointer = wimote_settings.look == WIIMOTE_LOOK_AUTO || wimote_settings.look == WIIMOTE_LOOK_POINTER;
	const int wants_motion = wimote_settings.look == WIIMOTE_LOOK_AUTO || wimote_settings.look == WIIMOTE_LOOK_MOTION;
	const unsigned int mode = wimote_mpls_mode_wanted(extension);

	if (remotes[channel].applied != wimote_settings.epoch)
	{
		remotes[channel].applied = wimote_settings.epoch;
		if (wants_pointer)
			KPADEnableDPD(chan);
		else
			KPADDisableDPD(chan);
		if (!wants_motion && KPADGetMplsStatus(chan) != WPAD_MPLS_MODE_DISABLE)
			KPADDisableMpls(chan);
		remotes[channel].mpls_attempts = 0;
		remotes[channel].mpls_failed = 0;
	}
	if (!wants_motion || !mode || remotes[channel].mpls_failed == (int)extension + 1 || remotes[channel].mpls_attempts >= 3)
		return;
	if ((unsigned int)KPADGetMplsStatus(chan) == mode)
		return;
	if (remotes[channel].mpls_asked_at && now - remotes[channel].mpls_asked_at < (OSTime)OSSecondsToTicks(2))
		return;
	if (!mpls_work_ready())
		return;
	remotes[channel].mpls_asked_at = now;
	remotes[channel].mpls_attempts++;
	KPADEnableMpls(chan, (KPADMplsMode)mode);
	wiiu_log("Input: channel %u's Wii Remote is asked for the MotionPlus (%s mode, extension %s, try %d)", channel + 1,
		mode == WIIMOTE_MPLS_NUNCHUK ? "Nunchuk" : "alone", wimote_extension_name(extension), remotes[channel].mpls_attempts);
}

/* A channel's Remote, from a sample: the pad it makes, its look, and the pointer and MotionPlus asked for. */
static void remote_sample(unsigned int channel, const KPADStatus *status, struct wiiu_pad *pad, OSTime now)
{
	const unsigned int extension = status->extensionType;
	const int nunchuk = wimote_has_nunchuk(extension);
	const int first = wiiu_pad_players_port(&pad_players, (int)channel) == 0;
	const int valid = status->posValid > 0;
	const unsigned int bit = 1u << channel;
	float seconds = remotes[channel].last_sample ? (float)OSTicksToMicroseconds(now - remotes[channel].last_sample) / 1000000.0f : 0.0f;
	int source;

	if (!remotes[channel].remote)
	{
		/* a Remote has come (or its extension was one the game does not read): not known yet whether it has a MotionPlus built in */
		remotes[channel].plus = WPADIsMplsIntegrated((WPADChan)channel);
		remotes[channel].applied = 0;
		wimote_motion_reset(&remotes[channel].motion);
	}
	if (remotes[channel].extension != extension)
	{
		/* (the extension changed: a Nunchuk plugged in or out, or the MotionPlus came on: asked for afresh, and the up it knew is not the hold it will have) */
		remotes[channel].mpls_attempts = 0;
		remotes[channel].mpls_failed = 0;
	}
	remotes[channel].remote = 1;
	remotes[channel].extension = extension;
	remotes[channel].pointer_valid = valid;
	remotes[channel].layout = wimote_layout_for(extension, wimote_settings.layout);
	wimote_map(remotes[channel].layout, wimote_hold(status->hold, nunchuk ? status->nunchuk.hold : 0u), nunchuk ? status->nunchuk.stick.x : 0.0f,
		nunchuk ? status->nunchuk.stick.y : 0.0f, pad);
	source = wimote_look_source(wimote_settings.look, wimote_has_motion(extension) && mpls_work_state > 0, first, valid);
	remotes[channel].source = source;
	if (source == WIIMOTE_SOURCE_POINTER)
	{
		wimote_pointer_look(valid, status->pos.x, status->pos.y, wimote_settings.box, wimote_settings.speed, &pad->look_x, &pad->look_y);
	}
	else if (source == WIIMOTE_SOURCE_MOTION)
	{
		/* the rates (KPADStatus.mplus.acc: "angular acceleration" in wut's words, the rates in degrees a second as KPAD's MotionPlus status has them) and the Remote's
		accelerometer for the direction of up */
		const float rate[3] = { status->mplus.acc.x, status->mplus.acc.y, status->mplus.acc.z };
		const float acc[3] = { status->acc.x, status->acc.y, status->acc.z };
		float turn, tilt;
		unsigned int axis, lower;
		int owns = first;

		/* (the view turns by one Remote: when several share the first player's port, the lowest channel's) */
		for (lower = 0; lower < channel; lower++)
			if (remote_present & (1u << lower) && remotes[lower].source == WIIMOTE_SOURCE_MOTION && wiiu_pad_players_port(&pad_players, (int)lower) == 0)
				owns = 0;
		wimote_motion_step(&remotes[channel].motion, gyro_mouse.space, seconds, acc, rate, wimote_settings.invert_x, &turn, &tilt);
		if (owns)
		{
			remote_mouse.turn += turn;
			remote_mouse.pitch += tilt;
		}
		/* held still for a second and a half: the library zeroes the MotionPlus (KPADResetMpls), once for each time it is held still */
		if (wimote_motion_still(&remotes[channel].motion, seconds, rate))
		{
			KPADResetMpls((KPADChan)channel);
			wiiu_log("Input: channel %u's Wii Remote was held still: the MotionPlus is zeroed (KPADResetMpls)", channel + 1);
		}
		/* the units, in the log while it turns (the rates' peak, and how far the angle KPAD adds up changed over the same second: they should agree, rate times
		a second = the angle's change, if the rates are in the units of the angles) */
		for (axis = 0; axis < 3; axis++)
			if (fabsf(rate[axis]) > fabsf(remotes[channel].peak[axis]))
				remotes[channel].peak[axis] = rate[axis];
		if (!remotes[channel].angle_ref_at)
		{
			remotes[channel].angle_ref_at = now;
			remotes[channel].angle_ref[0] = status->mplus.angles.x;
			remotes[channel].angle_ref[1] = status->mplus.angles.y;
			remotes[channel].angle_ref[2] = status->mplus.angles.z;
		}
		else if (now - remotes[channel].angle_ref_at >= OSSecondsToTicks(1))
		{
			const float elapsed = (float)OSTicksToMicroseconds(now - remotes[channel].angle_ref_at) / 1000000.0f;
			const float big = fmaxf(fabsf(remotes[channel].peak[0]), fmaxf(fabsf(remotes[channel].peak[1]), fabsf(remotes[channel].peak[2])));

			if (big > 30.0f && remotes[channel].motion_told_count < 12u && now - remotes[channel].motion_told >= OSSecondsToTicks(5))
			{
				remotes[channel].motion_told = now;
				remotes[channel].motion_told_count++;
				wiiu_log("Input: channel %u's MotionPlus: rates peaked at %.1f %.1f %.1f (x y z); the angles changed %.1f %.1f %.1f in %.2f s (acc %.2f %.2f %.2f)",
					channel + 1, (double)remotes[channel].peak[0], (double)remotes[channel].peak[1], (double)remotes[channel].peak[2],
					(double)(status->mplus.angles.x - remotes[channel].angle_ref[0]), (double)(status->mplus.angles.y - remotes[channel].angle_ref[1]),
					(double)(status->mplus.angles.z - remotes[channel].angle_ref[2]), (double)elapsed, (double)acc[0], (double)acc[1], (double)acc[2]);
			}
			remotes[channel].angle_ref_at = now;
			remotes[channel].angle_ref[0] = status->mplus.angles.x;
			remotes[channel].angle_ref[1] = status->mplus.angles.y;
			remotes[channel].angle_ref[2] = status->mplus.angles.z;
			remotes[channel].peak[0] = remotes[channel].peak[1] = remotes[channel].peak[2] = 0.0f;
		}
	}
	remotes[channel].last_sample = now;
	remote_present |= bit;
	if (wimote_rumble_allowed(source))
	{
		remote_rumble |= bit;
	}
	else if (remote_rumble & bit)
	{
		/* (its motor would shake the view: stopped, by the thread that drives the motors) */
		remote_rumble &= ~bit;
		rumble_stop_all();
	}
	remote_control(channel, extension, now);
	/* the changes, once the connection has been told: the extension (a Nunchuk plugged in, the MotionPlus on) and the look (the pointer seeing the sensor bar) */
	if (remotes[channel].announced && (remotes[channel].announced_extension != extension || remotes[channel].announced_source != source) &&
		now - remotes[channel].announced_at >= (OSTime)OSSecondsToTicks(2))
	{
		remotes[channel].announced_extension = extension;
		remotes[channel].announced_source = source;
		remotes[channel].announced_at = now;
		remote_describe(channel, "now");
	}
	/* the pointer finding and losing the sensor bar, in the log now and then */
	if (wimote_settings.look != WIIMOTE_LOOK_MOTION && wimote_settings.look != WIIMOTE_LOOK_OFF &&
		(remotes[channel].pointer_told == 0 || now - remotes[channel].pointer_told >= (OSTime)OSSecondsToTicks(3)))
	{
		if (remotes[channel].pointer_logged != (valid ? 1 : 2))
		{
			remotes[channel].pointer_logged = valid ? 1 : 2;
			remotes[channel].pointer_told = now;
			wiiu_log("Input: channel %u's Wii Remote %s the sensor bar (posValid %d, pos %.2f %.2f, %.2f m)", channel + 1, valid ? "sees" : "does not see",
				(int)status->posValid, (double)status->pos.x, (double)status->pos.y, (double)status->dist);
		}
	}
}

/* the MotionPlus's start-up, as the system's callback noted it (wiiu_input_poll, for each channel) */
static void mpls_events_poll(unsigned int channel)
{
	const unsigned int count = mpls_events[channel];

	if (count == mpls_events_seen[channel])
		return;
	mpls_events_seen[channel] = count;
	if (mpls_result[channel] == KPAD_CONTROL_MPLS_STATUS_FINISHED)
	{
		wiiu_log("Input: channel %u's MotionPlus is on (the mode is %d)", channel + 1, (int)KPADGetMplsStatus((KPADChan)channel));
		return;
	}
	/* (it cannot be had with this extension: a Remote that has no MotionPlus says so; asked for again only if the extension changes) */
	remotes[channel].mpls_failed = (int)remotes[channel].extension + 1;
	wiiu_log("Input: channel %u's MotionPlus could not be started (status %d, extension %s): this Remote has none, or not with this extension", channel + 1,
		mpls_result[channel], wimote_extension_name(remotes[channel].extension));
}

int wiiu_input_remote_look_take(float *turn, float *pitch)
{
	unsigned int channel;
	int owner = 0;

	*turn = remote_mouse.turn;
	*pitch = remote_mouse.pitch;
	remote_mouse.turn = remote_mouse.pitch = 0.0f;
	for (channel = 0; channel < WIIU_PAD_CHANNELS; channel++)
	{
		if (last_pro[channel].present && remotes[channel].remote && remotes[channel].source == WIIMOTE_SOURCE_MOTION &&
			wiiu_pad_players_port(&pad_players, (int)channel) == 0)
		{
			owner = 1;
		}
	}
	if (!owner)
		*turn = *pitch = 0.0f;
	return owner;
}

/* The controller on a KPAD channel now, into last_pro: a sample if there is a new one, else as it was. Returns 1 if it is one the game reads. */
static int pad_sample(unsigned int channel)
{
	KPADStatus status;
	KPADError error = KPAD_ERROR_OK;
	int got;

	memset(&status, 0, sizeof(status));
	got = KPADReadEx((KPADChan)channel, &status, 1, &error);
	last_pro[channel].sampled = got >= 1 && error == KPAD_ERROR_OK;
	if (last_pro[channel].sampled)
	{
		struct wiiu_pad now;

		memset(&now, 0, sizeof(now));
		last_pro[channel].connected = 1;
		if (status.extensionType == WPAD_EXT_PRO_CONTROLLER)
		{
			now.buttons = from_pro(status.pro.hold);
			now.left_x = status.pro.leftStick.x;
			now.left_y = status.pro.leftStick.y;
			now.right_x = status.pro.rightStick.x;
			now.right_y = status.pro.rightStick.y;
			pro_present |= 1u << channel;
			remote_present &= ~(1u << channel);
			remote_rumble &= ~(1u << channel);
			remotes[channel].remote = 0;
			last_pro[channel].present = 1;
		}
		else if (status.extensionType == WPAD_EXT_CLASSIC || status.extensionType == WPAD_EXT_MPLUS_CLASSIC)
		{
			now.buttons = from_classic(status.classic.hold);
			/* (the triggers are analog on this pad: a full press also sets L or R, a half one is enough for the Xbox's trigger) */
			if (status.classic.leftTrigger > 0.5f)
				now.buttons |= WIIU_PAD_ZL;
			if (status.classic.rightTrigger > 0.5f)
				now.buttons |= WIIU_PAD_ZR;
			now.left_x = status.classic.leftStick.x;
			now.left_y = status.classic.leftStick.y;
			now.right_x = status.classic.rightStick.x;
			now.right_y = status.classic.rightStick.y;
			pro_present &= ~(1u << channel);
			remote_present &= ~(1u << channel);
			remote_rumble &= ~(1u << channel);
			remotes[channel].remote = 0;
			last_pro[channel].present = 1;
		}
		else if (wimote_reads(status.extensionType))
		{
			/* a Wii Remote, with a Nunchuk or alone (a Remote with a Classic Controller is the Classic Controller above) */
			pro_present &= ~(1u << channel);
			remote_sample(channel, &status, &now, OSGetSystemTime());
			last_pro[channel].present = 1;
		}
		else if ((status.extensionType == WPAD_EXT_UNKNOWN || status.extensionType == WPAD_EXT_DEV_NOT_FOUND) && remotes[channel].remote &&
			(remotes[channel].grace_since == 0 || OSGetSystemTime() - remotes[channel].grace_since < (OSTime)OSSecondsToTicks(1)))
		{
			/* (the extension changing under the library, as the MotionPlus comes on: the Remote as it was for a second, not a controller that went and came back) */
			if (remotes[channel].grace_since == 0)
				remotes[channel].grace_since = OSGetSystemTime();
			now = last_pro[channel].pad;
		}
		else
		{
			pro_present &= ~(1u << channel);
			remote_present &= ~(1u << channel);
			remote_rumble &= ~(1u << channel);
			remotes[channel].remote = 0;
			last_pro[channel].present = 0;
		}
		if (wimote_reads(status.extensionType))
			remotes[channel].grace_since = 0;
		last_pro[channel].pad = now;
	}
	else if (error != KPAD_ERROR_NO_SAMPLES && error != KPAD_ERROR_BUSY)
	{
		pad_gone(channel);
	}
	return last_pro[channel].present;
}

/* the channel's controller's lights set to the player it drives (all off: none), once they differ from what was set and a tenth of a second after the last try */
static void lights_update(unsigned int channel, OSTime now)
{
	const int player = last_pro[channel].present ? wiiu_pad_players_lights(&pad_players, (int)channel) : 0;
	WPADError error;

	if (lights[channel].again_at && now >= lights[channel].again_at)
	{
		lights[channel].again_at = 0;
		lights[channel].shown = -1;
	}
	if (!last_pro[channel].connected || lights[channel].shown == player)
		return;
	if (lights[channel].tried && now - lights[channel].tried < (OSTime)OSMillisecondsToTicks(100))
		return;
	lights[channel].tried = now;
	error = WPADControlLed((WPADChan)channel, player ? (WPADLed)(WPAD_LED_ONE << (player - 1)) : (WPADLed)0, NULL);
	if (error == WPAD_ERROR_NONE)
	{
		lights[channel].shown = player;
		wiiu_log("Input: channel %u's lights show player %d", channel + 1, player);
	}
}

void wiiu_input_poll(int several_players)
{
	static OSTime probed_at;
	const OSTime now = OSGetSystemTime();
	const int foreground = wiiu_in_foreground();
	const int probe_due = now - probed_at >= (OSTime)OSSecondsToTicks(1);
	unsigned int channel;
	int at_rest = 1;

	start();
	if (probe_due)
		probed_at = now;
	/* (in the background the HOME Menu has the controllers and may light them its way: lit again on coming back) */
	if (foreground != lights_foreground)
	{
		lights_foreground = foreground;
		for (channel = 0; channel < WIIU_PAD_CHANNELS; channel++)
			lights_forget(channel, now);
	}
	for (channel = 0; channel < WIIU_PAD_CHANNELS; channel++)
	{
		const unsigned int count = connections[channel];
		const int changed = count != connections_seen[channel];
		int present;

		if (changed)
		{
			connections_seen[channel] = count;
			wiiu_log("Input: channel %u %s", channel + 1, connection_status[channel] == WPAD_ERROR_NONE ? "connected" : "disconnected");
			/* (what was on the channel has gone, or may have: only a sample from now on says what is there, so that a controller that went and
			another that came between two polls, a Wii Remote alone say, are not taken for one another, nor the old one's state for the new one's) */
			pad_gone(channel);
		}
		/* (the system asked now and then whether anything is on the channel at all: the reads keep a controller's last state between samples, and
		would keep one that went if neither a read's error nor the callback said so) */
		if (probe_due && last_pro[channel].connected)
		{
			WPADExtensionType extension;

			if (WPADProbe((WPADChan)channel, &extension) == WPAD_ERROR_NO_CONTROLLER)
				pad_gone(channel);
		}
		present = pad_sample(channel);
		mpls_events_poll(channel);
		/* (named from a sample of its own, not from a state the reads kept: a channel the system has not yet said is empty keeps the last
		controller's, whose address the system may still give) */
		if (present && last_pro[channel].sampled && (changed || !pad_players.channels[channel].present))
		{
			WPADAddress address;
			int player;

			memset(&address, 0, sizeof(address));
			WPADGetAddress((WPADChan)channel, &address);
			player = wiiu_pad_players_connect(&pad_players, (int)channel, address.btDeviceAddress);
			wiiu_log("Input: a %s on channel %u (%02x:%02x:%02x:%02x:%02x:%02x) is player %d's", (pro_present & (1u << channel)) ? "Pro Controller" :
				remotes[channel].remote ? "Wii Remote" : "Classic Controller", channel + 1, address.btDeviceAddress[0], address.btDeviceAddress[1],
				address.btDeviceAddress[2], address.btDeviceAddress[3], address.btDeviceAddress[4], address.btDeviceAddress[5], player + 1);
			if (remotes[channel].remote)
			{
				/* (the extension, the layout and the look it has now; the changes follow in the log as they come) */
				remotes[channel].source = remote_source(channel);
				remotes[channel].announced = 1;
				remotes[channel].announced_extension = remotes[channel].extension;
				remotes[channel].announced_source = remotes[channel].source;
				remotes[channel].announced_at = now;
				remote_describe(channel, "at connect:");
			}
		}
		else if (!present && pad_players.channels[channel].present)
		{
			wiiu_pad_players_disconnect(&pad_players, (int)channel);
			wiiu_log("Input: channel %u's controller has gone", channel + 1);
		}
		if (present && !wiiu_pad_players_at_rest(&last_pro[channel].pad))
			at_rest = 0;
	}
	if (wiiu_pad_players_arrange(&pad_players, pad_layout, several_players, at_rest))
	{
		int ports[WIIU_PAD_CHANNELS];

		for (channel = 0; channel < WIIU_PAD_CHANNELS; channel++)
			ports[channel] = wiiu_pad_players_port(&pad_players, (int)channel);
		wiiu_log("Input: the controllers of channels 1 to 4 drive players %d %d %d %d (0: none)", ports[0] + 1, ports[1] + 1, ports[2] + 1, ports[3] + 1);
		/* (a motor left running by the port a controller had would run on for the port it has, whose player may never rumble it) */
		rumble_stop_all();
	}
	for (channel = 0; foreground && channel < WIIU_PAD_CHANNELS; channel++)
		lights_update(channel, now);
}

int wiiu_input_port_present(unsigned int port)
{
	unsigned int channel;

	if (port == 0)
		return 1;
	for (channel = 0; channel < WIIU_PAD_CHANNELS; channel++)
	{
		if (last_pro[channel].present && wiiu_pad_players_port(&pad_players, (int)channel) == (int)port)
			return 1;
	}
	return 0;
}

int wiiu_input_controllers_merged(void)
{
	unsigned int channel;
	int count = 0;

	if (pad_players.layout != WIIU_PAD_LAYOUT_AUTO || pad_players.separate)
		return 0;
	for (channel = 0; channel < WIIU_PAD_CHANNELS; channel++)
	{
		if (last_pro[channel].present && pad_players.channels[channel].player)
			count++;
	}
	return count;
}

static int touch_down;
static float touch_x, touch_y;
static unsigned int touch_logged;

int wiiu_input_touch(float *x, float *y)
{
	int down = touch_down;

	*x = touch_x;
	*y = touch_y;
	/* (the debug server's tap: a finger the client holds on the screen) */
	wiiu_debug_touch(&down, x, y);
	return down;
}

/* the GamePad's finger, from a sample (the system's own calibration, to the GamePad's 854x480) */
static void read_touch(const VPADStatus *status)
{
	VPADTouchData calibrated;

	VPADGetTPCalibratedPointEx(VPAD_CHAN_0, VPAD_TP_854X480, &calibrated, &status->tpNormal);
	touch_down = calibrated.touched && calibrated.validity == VPAD_VALID;
	if (touch_down)
	{
		if (!touch_logged || touch_x != (float)calibrated.x || touch_y != (float)calibrated.y)
		{
			/* (a few, to learn where a touch lands: the calibration is the system's) */
			if (touch_logged < 24u && (touch_logged == 0 || touch_logged % 4 == 0))
				wiiu_log("Input: touch at %u,%u (raw %u,%u)", (unsigned)calibrated.x, (unsigned)calibrated.y, (unsigned)status->tpNormal.x,
					(unsigned)status->tpNormal.y);
			touch_logged++;
		}
		touch_x = (float)calibrated.x;
		touch_y = (float)calibrated.y;
	}
}

/* ---------- the TV button (push to talk) and the GamePad's jack */

static int tv_taken;
static int tv_asked;
static uint32_t tv_asked_ms;
static int tv_probe;
static int tv_held;
static int headset = -1;
static unsigned int mic_status;
static unsigned int mic_status_told;

static uint32_t input_ms(void)
{
	return (uint32_t)OSTicksToMilliseconds(OSGetSystemTime());
}

unsigned long wiiu_input_milliseconds(void)
{
	return (unsigned long)input_ms();
}

/* (from the game thread, which reads the GamePad: the TV menu taken while voice chat or the probe asks for the button, given back a second
after the last asking, and never in the background, where the GamePad is the system's) */
static void tv_update(void)
{
	const int probe = __atomic_load_n(&tv_probe, __ATOMIC_ACQUIRE);
	const int want = (tv_asked && input_ms() - tv_asked_ms < 1000u) || probe;

	if (want == tv_taken || !wiiu_in_foreground())
		return;
	VPADSetTVMenuInvalid(VPAD_CHAN_0, want ? TRUE : FALSE);
	tv_taken = want;
	if (!want)
		tv_asked = 0;
	wiiu_log(want ? "Input: the GamePad's TV button is push to talk (the system's TV menu is off)" :
		"Input: the GamePad's TV button opens the system's TV menu again");
}

int wiiu_input_push_to_talk(void)
{
	start();
	tv_asked = 1;
	tv_asked_ms = input_ms();
	tv_update();
	return tv_taken && tv_held;
}

void wiiu_input_take_tv_button(int take)
{
	__atomic_store_n(&tv_probe, take ? 1 : 0, __ATOMIC_RELEASE);
}

int wiiu_input_headset(void)
{
	return __atomic_load_n(&headset, __ATOMIC_ACQUIRE);
}

unsigned int wiiu_input_mic_status(void)
{
	return __atomic_load_n(&mic_status, __ATOMIC_ACQUIRE);
}

/* the jack and the microphone's status byte, in the log as they change (the status byte at most 16 times: what its bits mean is unknown,
0x08 has been read as the headset's microphone being the one used) */
static void jack_note(const VPADStatus *status)
{
	const int now_headset = status->usingHeadphones ? 1 : 0;

	if (now_headset != headset || (status->micStatus != mic_status && mic_status_told < 16u))
	{
		if (now_headset == headset)
			mic_status_told++;
		wiiu_log("Input: the GamePad's jack: %s (VPADBASEGetHeadphoneStatus %d), its microphone status 0x%02x", now_headset ? "a headset" : "empty",
			(int)VPADBASEGetHeadphoneStatus(VPAD_CHAN_0), (unsigned)status->micStatus);
	}
	__atomic_store_n(&mic_status, (unsigned int)status->micStatus, __ATOMIC_RELEASE);
	__atomic_store_n(&headset, now_headset, __ATOMIC_RELEASE);
}

int wiiu_input_read(unsigned int port, struct wiiu_pad *pad)
{
	unsigned int channel;
	int present = 0;

	memset(pad, 0, sizeof(*pad));
	if (port >= WIIU_PAD_PORTS)
		return 0;
	start();
	if (port == 0)
	{
		VPADStatus status;
		VPADReadError error = VPAD_READ_SUCCESS;
		int fresh = 0;

		memset(&status, 0, sizeof(status));
		/* (the GamePad is always there) */
		present = 1;
		if (VPADRead(VPAD_CHAN_0, &status, 1, &error) > 0 && error == VPAD_READ_SUCCESS)
		{
			memset(&last_vpad, 0, sizeof(last_vpad));
			last_vpad.buttons = from_vpad(status.hold);
			last_vpad.left_x = status.leftStick.x;
			last_vpad.left_y = status.leftStick.y;
			last_vpad.right_x = status.rightStick.x;
			last_vpad.right_y = status.rightStick.y;
			last_vpad.gyro_pitch = status.gyro.x;
			last_vpad.gyro_yaw = status.gyro.y;
			ranges_note(&status);
			fresh = 1;
			read_touch(&status);
			jack_note(&status);
		}
		else if (error != VPAD_READ_NO_SAMPLES && error != VPAD_READ_BUSY)
		{
			memset(&last_vpad, 0, sizeof(last_vpad));
		}
		*pad = last_vpad;
		wiiu_debug_input(pad);
		/* (the TV button as the game sees it: the debug server's held one too) */
		{
			const int held = (pad->buttons & WIIU_PAD_TV) != 0;

			if (held != tv_held && __atomic_load_n(&tv_probe, __ATOMIC_ACQUIRE))
				wiiu_log("mic probe: the TV button is %s", held ? "down" : "up");
			tv_held = held;
		}
		tv_update();
		/* (after the debug server's held rates, which stand for the gyro's) */
		if (fresh)
			gyro_mouse_note(&status, pad->gyro_pitch, pad->gyro_yaw, status.gyro.z);
	}
	/* the controllers that drive this port now (wiiu_pad_players.c: the layout and the map, arranged by the frame's poll), read again for the latest sample */
	for (channel = 0; channel < WIIU_PAD_CHANNELS; channel++)
	{
		if (wiiu_pad_players_port(&pad_players, (int)channel) != (int)port || !pad_sample(channel))
			continue;
		pad->buttons |= last_pro[channel].pad.buttons;
		pad->xbox_faces |= last_pro[channel].pad.xbox_faces;
		merge_stick(&pad->left_x, &pad->left_y, last_pro[channel].pad.left_x, last_pro[channel].pad.left_y);
		merge_stick(&pad->look_x, &pad->look_y, last_pro[channel].pad.look_x, last_pro[channel].pad.look_y);
		merge_stick(&pad->right_x, &pad->right_y, last_pro[channel].pad.right_x, last_pro[channel].pad.right_y);
		present = 1;
	}
	return present;
}

/* ---------- rumble */

static float rumble_level[WIIU_PAD_PORTS];
static uint32_t rumble_refreshed_ms[WIIU_PAD_PORTS];

/* Every Pro Controller's motor to be stopped, and every port's level forgotten, so that the next rumble of each is sent whatever it is. The game's
rumble is sent from its input thread (input_xbox.c's input_flush_rumble, every other refresh, port 0 always among them), the poll that moves the
controllers runs on the game's: the poll asks, and the next rumble does it, so that the motors are driven from one thread. */
static volatile int motors_stop;

static void rumble_stop_all(void)
{
	motors_stop = 1;
}

void wiiu_input_rumble(unsigned int port, float strength)
{
	/* (the pattern's bits are 60ths of a second, a bit on when the motor runs: a share of them on is a share of the strength) */
	static const uint8_t patterns[4][8] =
	{
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88 },
		{ 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA },
		{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	};
	const uint32_t now = (uint32_t)(OSTicksToMilliseconds(OSGetSystemTime()));
	unsigned int step;

	if (port >= WIIU_PAD_PORTS || !wiiu_in_foreground())
		return;
	start();
	if (motors_stop)
	{
		unsigned int channel, other;

		motors_stop = 0;
		for (channel = 0; channel < WIIU_PAD_CHANNELS; channel++)
		{
			if ((pro_present | remote_present) & (1u << channel))
				WPADControlMotor((WPADChan)channel, FALSE);
		}
		for (other = 0; other < WIIU_PAD_PORTS; other++)
		{
			rumble_level[other] = 0.0f;
			rumble_refreshed_ms[other] = 0;
		}
	}
	step = strength <= 0.05f ? 0u : strength < 0.35f ? 1u : strength < 0.7f ? 2u : 3u;
	/* (a pattern runs for about a tenth of a second per byte: the same level is sent again before it ends, and a change at once) */
	if (step == (unsigned int)(rumble_level[port] * 3.0f + 0.5f) && now - rumble_refreshed_ms[port] < 100u)
		return;
	rumble_level[port] = (float)step / 3.0f;
	rumble_refreshed_ms[port] = now;
	if (port == 0)
	{
		if (step)
		{
			uint8_t pattern[8];

			memcpy(pattern, patterns[step], sizeof(pattern));
			VPADControlMotor(VPAD_CHAN_0, pattern, sizeof(pattern));
		}
		else
		{
			VPADStopMotor(VPAD_CHAN_0);
		}
	}
	{
		unsigned int channel;

		/* (every Pro Controller and Wii Remote that drives the port, a Remote not while its motion turns the view: in "auto" with one player, all of them with the GamePad. The map is the game thread's poll's,
		read here on the input thread: a controller that moves between the two is rumbled for its old port once more, and its motor stopped at the
		next call: motors_stop) */
		for (channel = 0; channel < WIIU_PAD_CHANNELS; channel++)
		{
			if (((pro_present | remote_rumble) & (1u << channel)) && wiiu_pad_players_port(&pad_players, (int)channel) == (int)port)
				WPADControlMotor((WPADChan)channel, step >= 2 ? TRUE : FALSE);
		}
	}
}
