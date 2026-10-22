/*
WIIU_INPUT.C

The GamePad (VPAD) and the Pro Controllers (KPAD) read as wiiu_input.h's pads. Classic Controllers and Wii Remotes are not read yet.
*/

#include "wiiu_input.h"

#include "wiiu_debug.h"

#include <padscore/kpad.h>
#include <padscore/wpad.h>
#include <vpad/input.h>

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

/* the Pro Controller on a KPAD channel, if there is one */
static int read_pro(unsigned int channel, struct wiiu_pad *pad)
{
	KPADStatus status;
	KPADError error = KPAD_ERROR_OK;

	memset(&status, 0, sizeof(status));
	if (KPADReadEx((KPADChan)channel, &status, 1, &error) < 1 || error != KPAD_ERROR_OK || status.extensionType != WPAD_EXT_PRO_CONTROLLER)
		return 0;
	pad->buttons |= from_pro(status.pro.hold);
	if (status.pro.leftStick.x * status.pro.leftStick.x + status.pro.leftStick.y * status.pro.leftStick.y >
		pad->left_x * pad->left_x + pad->left_y * pad->left_y)
	{
		pad->left_x = status.pro.leftStick.x;
		pad->left_y = status.pro.leftStick.y;
	}
	if (status.pro.rightStick.x * status.pro.rightStick.x + status.pro.rightStick.y * status.pro.rightStick.y >
		pad->right_x * pad->right_x + pad->right_y * pad->right_y)
	{
		pad->right_x = status.pro.rightStick.x;
		pad->right_y = status.pro.rightStick.y;
	}
	return 1;
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
