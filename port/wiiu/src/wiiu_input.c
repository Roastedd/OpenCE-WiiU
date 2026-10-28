/*
WIIU_INPUT.C

The GamePad (VPAD), the Pro Controllers and the Classic Controllers (KPAD) read as wiiu_input.h's pads, and their motors rumble. Wii Remotes alone (with or
without a Nunchuk) are not read.
*/

#include "wiiu_input.h"

#include "wiiu_debug.h"
#include "wiiu_shell.h"

#include <padscore/kpad.h>
#include <padscore/wpad.h>
#include <vpad/input.h>

#include <coreinit/time.h>

#include <string.h>

static int started;

static void start(void)
{
	if (started)
		return;
	started = 1;
	VPADInit();
	KPADInit();
	WPADEnableURCC(TRUE);
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
		{ VPAD_BUTTON_STICK_L, WIIU_PAD_STICK_L }, { VPAD_BUTTON_STICK_R, WIIU_PAD_STICK_R },
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

/* the Pro Controller or the Classic Controller on a KPAD channel, if there is one */
static unsigned int pro_present;

/* What each controller last said. A read with no new sample (the game asks faster than the pads sample, about 200 times a second; the menus and the
loops that wait for a frame do) leaves the pad as it was, as the system's own examples keep it: reporting it released made the sticks fall to the centre and
the buttons let go between samples, a stutter in every look and a flicker in every held button. Only an error says the pad is gone. */
static struct
{
	int present;
	struct wiiu_pad pad;
} last_pro[WIIU_PAD_PORTS];
static struct wiiu_pad last_vpad;
static int controllers_separate;

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

void wiiu_input_set_layout(int separate)
{
	controllers_separate = separate != 0;
}

/* the Pro Controller or the Classic Controller on a KPAD channel, if there is one */
static int read_pro(unsigned int channel, struct wiiu_pad *pad)
{
	KPADStatus status;
	KPADError error = KPAD_ERROR_OK;
	int got;

	memset(&status, 0, sizeof(status));
	got = KPADReadEx((KPADChan)channel, &status, 1, &error);
	if (got >= 1 && error == KPAD_ERROR_OK)
	{
		struct wiiu_pad now;

		memset(&now, 0, sizeof(now));
		if (status.extensionType == WPAD_EXT_PRO_CONTROLLER)
		{
			now.buttons = from_pro(status.pro.hold);
			now.left_x = status.pro.leftStick.x;
			now.left_y = status.pro.leftStick.y;
			now.right_x = status.pro.rightStick.x;
			now.right_y = status.pro.rightStick.y;
			pro_present |= 1u << channel;
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
			last_pro[channel].present = 1;
		}
		else
		{
			pro_present &= ~(1u << channel);
			last_pro[channel].present = 0;
		}
		last_pro[channel].pad = now;
	}
	else if (error != KPAD_ERROR_NO_SAMPLES && error != KPAD_ERROR_BUSY)
	{
		pro_present &= ~(1u << channel);
		last_pro[channel].present = 0;
		memset(&last_pro[channel].pad, 0, sizeof(last_pro[channel].pad));
	}
	if (!last_pro[channel].present)
		return 0;
	pad->buttons |= last_pro[channel].pad.buttons;
	merge_stick(&pad->left_x, &pad->left_y, last_pro[channel].pad.left_x, last_pro[channel].pad.left_y);
	merge_stick(&pad->right_x, &pad->right_y, last_pro[channel].pad.right_x, last_pro[channel].pad.right_y);
	return 1;
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

int wiiu_input_read(unsigned int port, struct wiiu_pad *pad)
{
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
		}
		else if (error != VPAD_READ_NO_SAMPLES && error != VPAD_READ_BUSY)
		{
			memset(&last_vpad, 0, sizeof(last_vpad));
		}
		*pad = last_vpad;
		wiiu_debug_input(pad);
		/* (after the debug server's held rates, which stand for the gyro's) */
		if (fresh)
			gyro_mouse_note(&status, pad->gyro_pitch, pad->gyro_yaw, status.gyro.z);
	}
	/* (merged: the controller of KPAD channel port joins this port; separate: the GamePad is port 0 alone and channel n-1 is port n) */
	if (controllers_separate)
	{
		if (port > 0 && read_pro(port - 1, pad))
			present = 1;
	}
	else if (read_pro(port, pad))
	{
		present = 1;
	}
	return present;
}

/* ---------- rumble */

static float rumble_level[WIIU_PAD_PORTS];
static uint32_t rumble_refreshed_ms[WIIU_PAD_PORTS];

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
		const unsigned int channel = controllers_separate ? port - 1 : port;

		if ((controllers_separate ? port > 0 : 1) && (pro_present & (1u << channel)))
			WPADControlMotor((WPADChan)channel, step >= 2 ? TRUE : FALSE);
	}
}
