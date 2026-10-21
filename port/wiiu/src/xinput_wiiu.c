/*
XINPUT_WIIU.C

Xbox controllers for the Wii U: the GamePad is the first controller (merged with a Pro Controller on channel 0, if one is connected), and Pro Controllers
on channels 1 to 3 are the others. The debug keyboard and the memory units are not there.

The Xbox pad's four face buttons are matched by position, not by name (the Wii U's B is where the Xbox's A is): the player's thumb finds the action
where it expects it. The Xbox's black and white buttons, which later pads put on the shoulders, are the Wii U's R and L; its triggers are ZR and ZL
(buttons, so all or nothing).

Not yet: rumble, the GamePad's gyro and touch screen.
*/

#include "platform.h"
#include "wiiu_input.h"

#include <string.h>

#define PORT_COUNT WIIU_PAD_PORTS

XPP_DEVICE_TYPE XDEVICE_TYPE_GAMEPAD_TABLE;
XPP_DEVICE_TYPE XDEVICE_TYPE_MEMORY_UNIT_TABLE;
XPP_DEVICE_TYPE XDEVICE_TYPE_DEBUG_KEYBOARD_TABLE;

struct controller
{
	BOOL open;
	DWORD packet_number;
	XINPUT_GAMEPAD previous;
};

static struct controller controllers[PORT_COUNT];
static DWORD reported_gamepads;

static SHORT stick(float value)
{
	int result = (int)(value * 32767.0f);

	if (result < -32768)
		result = -32768;
	if (result > 32767)
		result = 32767;
	return (SHORT)result;
}

static void pad_state(const struct wiiu_pad *pad, XINPUT_GAMEPAD *state)
{
	static const struct
	{
		unsigned int source;
		WORD mask;
	} digital[] =
	{
		{ WIIU_PAD_UP, XINPUT_GAMEPAD_DPAD_UP }, { WIIU_PAD_DOWN, XINPUT_GAMEPAD_DPAD_DOWN }, { WIIU_PAD_LEFT, XINPUT_GAMEPAD_DPAD_LEFT },
		{ WIIU_PAD_RIGHT, XINPUT_GAMEPAD_DPAD_RIGHT }, { WIIU_PAD_PLUS, XINPUT_GAMEPAD_START }, { WIIU_PAD_MINUS, XINPUT_GAMEPAD_BACK },
		{ WIIU_PAD_STICK_L, XINPUT_GAMEPAD_LEFT_THUMB }, { WIIU_PAD_STICK_R, XINPUT_GAMEPAD_RIGHT_THUMB },
	};
	static const struct
	{
		unsigned int source;
		int analog;
	} analog[] =
	{
		{ WIIU_PAD_B, XINPUT_GAMEPAD_A }, { WIIU_PAD_A, XINPUT_GAMEPAD_B }, { WIIU_PAD_Y, XINPUT_GAMEPAD_X }, { WIIU_PAD_X, XINPUT_GAMEPAD_Y },
		{ WIIU_PAD_L, XINPUT_GAMEPAD_WHITE }, { WIIU_PAD_R, XINPUT_GAMEPAD_BLACK }, { WIIU_PAD_ZL, XINPUT_GAMEPAD_LEFT_TRIGGER },
		{ WIIU_PAD_ZR, XINPUT_GAMEPAD_RIGHT_TRIGGER },
	};
	unsigned int i;

	for (i = 0; i < sizeof(digital) / sizeof(digital[0]); i++)
		if (pad->buttons & digital[i].source)
			state->wButtons |= digital[i].mask;
	for (i = 0; i < sizeof(analog) / sizeof(analog[0]); i++)
		if (pad->buttons & analog[i].source)
			state->bAnalogButtons[analog[i].analog] = 0xff;
	state->sThumbLX = stick(pad->left_x);
	state->sThumbLY = stick(pad->left_y);
	state->sThumbRX = stick(pad->right_x);
	state->sThumbRY = stick(pad->right_y);
}

VOID WINAPI XInitDevices(DWORD preallocation_type_count, PXDEVICE_PREALLOC_TYPE preallocation_types)
{
	(void)preallocation_type_count;
	(void)preallocation_types;
}

static DWORD connected_gamepads(void)
{
	DWORD mask = 0;
	unsigned int port;
	struct wiiu_pad pad;

	for (port = 0; port < PORT_COUNT; port++)
		if (wiiu_input_read(port, &pad))
			mask |= 1UL << port;
	return mask | XDEVICE_PORT0_MASK;
}

BOOL WINAPI XGetDeviceChanges(PXPP_DEVICE_TYPE device_type, PDWORD insertions, PDWORD removals)
{
	*insertions = 0;
	*removals = 0;
	if (device_type == XDEVICE_TYPE_GAMEPAD)
	{
		DWORD connected = connected_gamepads();

		*insertions = connected & ~reported_gamepads;
		*removals = reported_gamepads & ~connected;
		reported_gamepads = connected;
	}
	return *insertions || *removals;
}

HANDLE WINAPI XInputOpen(PXPP_DEVICE_TYPE device_type, DWORD port, DWORD slot, PXINPUT_POLLING_PARAMETERS polling_parameters)
{
	(void)slot;
	(void)polling_parameters;
	if (device_type == XDEVICE_TYPE_GAMEPAD && port < PORT_COUNT)
	{
		memset(&controllers[port], 0, sizeof(controllers[port]));
		controllers[port].open = TRUE;
		return (HANDLE)&controllers[port];
	}
	SetLastError(ERROR_DEVICE_NOT_CONNECTED);
	return NULL;
}

VOID WINAPI XInputClose(HANDLE device)
{
	struct controller *controller = (struct controller *)device;

	if (controller)
		controller->open = FALSE;
}

static int controller_port(HANDLE device)
{
	int port;

	for (port = 0; port < PORT_COUNT; port++)
		if (device == (HANDLE)&controllers[port] && controllers[port].open)
			return port;
	return -1;
}

DWORD WINAPI XInputGetState(HANDLE device, PXINPUT_STATE state)
{
	int port = controller_port(device);
	struct wiiu_pad pad;

	memset(state, 0, sizeof(*state));
	if (port < 0)
		return ERROR_DEVICE_NOT_CONNECTED;
	if (wiiu_input_read((unsigned int)port, &pad))
		pad_state(&pad, &state->Gamepad);
	if (memcmp(&state->Gamepad, &controllers[port].previous, sizeof(state->Gamepad)))
	{
		controllers[port].packet_number++;
		controllers[port].previous = state->Gamepad;
	}
	state->dwPacketNumber = controllers[port].packet_number;
	return ERROR_SUCCESS;
}

DWORD WINAPI XInputSetState(HANDLE device, PXINPUT_FEEDBACK feedback)
{
	int port = controller_port(device);

	if (!feedback)
		return ERROR_INVALID_PARAMETER;
	feedback->Header.dwStatus = ERROR_SUCCESS;
	if (port < 0)
		return ERROR_DEVICE_NOT_CONNECTED;
	/* (no rumble yet) */
	return ERROR_SUCCESS;
}

DWORD WINAPI XInputDebugInitKeyboardQueue(PXINPUT_DEBUG_KEYQUEUE_PARAMETERS parameters)
{
	(void)parameters;
	return ERROR_SUCCESS;
}

DWORD WINAPI XInputDebugGetKeystroke(PXINPUT_DEBUG_KEYSTROKE keystroke)
{
	memset(keystroke, 0, sizeof(*keystroke));
	return ERROR_HANDLE_EOF;
}
