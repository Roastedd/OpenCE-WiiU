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

static int read_pro(unsigned int channel, struct wiiu_pad *pad)
{
	KPADStatus status;
	KPADError error = KPAD_ERROR_OK;

	memset(&status, 0, sizeof(status));
	if (KPADReadEx((KPADChan)channel, &status, 1, &error) < 1 || error != KPAD_ERROR_OK)
		return 0;
	if (status.extensionType == WPAD_EXT_PRO_CONTROLLER)
	{
		pad->buttons |= from_pro(status.pro.hold);
		merge_stick(&pad->left_x, &pad->left_y, status.pro.leftStick.x, status.pro.leftStick.y);
		merge_stick(&pad->right_x, &pad->right_y, status.pro.rightStick.x, status.pro.rightStick.y);
		pro_present |= 1u << channel;
		return 1;
	}
	if (status.extensionType == WPAD_EXT_CLASSIC || status.extensionType == WPAD_EXT_MPLUS_CLASSIC)
	{
		pad->buttons |= from_classic(status.classic.hold);
		/* (the triggers are analog on this pad: a full press also sets L or R, a half one is enough for the Xbox's trigger) */
		if (status.classic.leftTrigger > 0.5f)
			pad->buttons |= WIIU_PAD_ZL;
		if (status.classic.rightTrigger > 0.5f)
			pad->buttons |= WIIU_PAD_ZR;
		merge_stick(&pad->left_x, &pad->left_y, status.classic.leftStick.x, status.classic.leftStick.y);
		merge_stick(&pad->right_x, &pad->right_y, status.classic.rightStick.x, status.classic.rightStick.y);
		pro_present &= ~(1u << channel);
		return 1;
	}
	pro_present &= ~(1u << channel);
	return 0;
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

		memset(&status, 0, sizeof(status));
		/* (the GamePad is always there; a read with no new sample leaves the pad as it was: released) */
		present = 1;
		if (VPADRead(VPAD_CHAN_0, &status, 1, &error) > 0 && error == VPAD_READ_SUCCESS)
		{
			pad->buttons |= from_vpad(status.hold);
			read_touch(&status);
			pad->left_x = status.leftStick.x;
			pad->left_y = status.leftStick.y;
			pad->right_x = status.rightStick.x;
			pad->right_y = status.rightStick.y;
		}
		wiiu_debug_input(pad);
	}
	if (read_pro(port, pad))
		present = 1;
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
	if (pro_present & (1u << port))
		WPADControlMotor((WPADChan)port, step >= 2 ? TRUE : FALSE);
}
