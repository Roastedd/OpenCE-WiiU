/*
XINPUT_WIIU.C

Xbox controllers for the Wii U: the GamePad is the first controller (merged with a Pro Controller on channel 0, if one is connected), and Pro Controllers
on channels 1 to 3 are the others. The debug keyboard and the memory units are not there.

The Xbox pad's four face buttons are matched to the Wii U's in the game by place (the Wii U's B is where the Xbox's A is): the player's thumb finds the
action where it expects it, and the prompts on the screen show the icon of the letter that is there (hud_hires_tags.c, port_button_icon). In the menus
they are matched by letter (the Wii U's A selects, its B goes back, as on every Nintendo console), because the menus' own prompts name the Xbox's
letters. The setting input.wiiu_face_buttons says "position" or "label" for both instead. The Xbox's black and white buttons, which later pads put on the shoulders, are the Wii U's R and L; its triggers are ZR and ZL
(buttons, so all or nothing). The motors rumble with the game (input.wiiu_rumble).

A tap on the GamePad's touch screen outside the menus skips a cinematic that can be skipped (it presses A, as the Android build's touch screen does).

The GamePad's gyro can aim (input.wiiu_gyro_aim, "off" by default; "always", or "zoom" for only while zoomed in): its turn rates are added to the right stick
while no menu has the screen.
*/

#include "platform.h"
#include "port_config.h"
#include "wiiu_shell.h"
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

/* input.wiiu_face_buttons: "label" (matched by letter), "position" (by place), or "auto" (the default: by letter in the menus, where the prompts name the
letter and Nintendo's A accepts, by place in the game, where the thumb knows where jump is) */
int wiiu_face_buttons_by_label(void);
/* (hud_hires_tags.c's: 1 while a menu has the screen) */
int port_menus_active(void);

int wiiu_face_buttons_by_label(void)
{
	static int mode = -1;

	if (mode < 0)
	{
		const char *setting = config_string("input.wiiu_face_buttons");

		mode = !strcmp(setting, "label") ? 1 : !strcmp(setting, "position") ? 0 : 2;
	}
	if (mode == 2)
	{
		static int last = -1;
		int by_label = port_menus_active();

		if (by_label != last)
		{
			last = by_label;
			wiiu_log("Input: the face buttons are now matched by %s", by_label ? "letter (a menu has the screen)" : "place (the game)");
		}
		return by_label;
	}
	return mode;
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
		{
			int target = analog[i].analog;

			/* (by letter, the Wii U's face buttons go to the Xbox's of the same name: A with B and X with Y, as the table has them by place) */
			if (target <= XINPUT_GAMEPAD_Y && wiiu_face_buttons_by_label())
				target ^= 1;
			state->bAnalogButtons[target] = 0xff;
		}
	state->sThumbLX = stick(pad->left_x);
	state->sThumbLY = stick(pad->left_y);
	state->sThumbRX = stick(pad->right_x);
	state->sThumbRY = stick(pad->right_y);
}

/* (the game's: NONE, -1, while the first player is not zoomed in) */
short player_control_get_zoom_level(short local_player_index);

/* the gyro's turn rates, added to the right stick: input.wiiu_gyro_aim ("off", "always" or "zoom": only while zoomed in), input.wiiu_gyro_sensitivity (1: a quarter turn a second is the stick's full push) and
input.wiiu_gyro_invert_y; a rate under 2% of a turn a second is the sensor's noise */
static void add_gyro(const struct wiiu_pad *pad, XINPUT_GAMEPAD *state)
{
	static int mode = -1; /* 0 off, 1 always, 2 only zoomed in */
	static float sensitivity, invert;
	float yaw, pitch;
	int x, y;

	if (mode < 0)
	{
		const char *setting = config_string("input.wiiu_gyro_aim");

		mode = !strcmp(setting, "always") ? 1 : !strcmp(setting, "zoom") ? 2 : 0;
		sensitivity = (float)config_real("input.wiiu_gyro_sensitivity");
		invert = config_boolean("input.wiiu_gyro_invert_y") ? -1.0f : 1.0f;
	}
	if (!mode || port_menus_active() || (mode == 2 && player_control_get_zoom_level(0) < 0))
		return;
	yaw = pad->gyro_yaw;
	pitch = pad->gyro_pitch;
	if (yaw > -0.02f && yaw < 0.02f)
		yaw = 0.0f;
	if (pitch > -0.02f && pitch < 0.02f)
		pitch = 0.0f;
	x = state->sThumbRX + (int)(yaw * 4.0f * sensitivity * 32767.0f);
	y = state->sThumbRY + (int)(pitch * 4.0f * sensitivity * invert * 32767.0f);
	state->sThumbRX = (SHORT)(x < -32768 ? -32768 : x > 32767 ? 32767 : x);
	state->sThumbRY = (SHORT)(y < -32768 ? -32768 : y > 32767 ? 32767 : y);
}

/* a tap on the touch screen, outside the menus, skips a cinematic that can be skipped (touch_game.c: the game's own question): A is held for a few polls,
since a tap is shorter than the game's poll */
int touch_game_cinematic_skippable(void);
int wiiu_input_touch(float *x, float *y);

static void tap_skips_cinematic(XINPUT_GAMEPAD *state)
{
	static int was_down, polls_down, hold;
	static float start_x, start_y;
	float x, y;
	int down = wiiu_input_touch(&x, &y);

	if (down && !was_down)
	{
		polls_down = 0;
		start_x = x;
		start_y = y;
	}
	if (down)
		polls_down++;
	if (!down && was_down && polls_down <= 30 && (x - start_x) * (x - start_x) + (y - start_y) * (y - start_y) < 30.0f * 30.0f &&
		!port_menus_active() && touch_game_cinematic_skippable())
		hold = 6;
	/* (x and y keep the last place of a finger that has gone) */
	was_down = down;
	if (hold > 0)
	{
		hold--;
		state->bAnalogButtons[XINPUT_GAMEPAD_A] = 0xff;
	}
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
	if (port == 0)
	{
		tap_skips_cinematic(&state->Gamepad);
		add_gyro(&pad, &state->Gamepad);
	}
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
	if (config_boolean("input.wiiu_rumble"))
	{
		unsigned int level = feedback->Rumble.wLeftMotorSpeed > feedback->Rumble.wRightMotorSpeed ? feedback->Rumble.wLeftMotorSpeed :
			feedback->Rumble.wRightMotorSpeed;

		wiiu_input_rumble((unsigned int)port, (float)level / 65535.0f);
	}
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
