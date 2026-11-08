/*
XINPUT_WIIU.C

Xbox controllers for the Wii U: the GamePad is the first controller (port 0), and the Pro and Classic Controllers take ports as input.wiiu_controller_layout
says (below). The debug keyboard and the memory units are not there.

Players and joining. The game's players are its four gamepad ports: in a network game (split screen is one: the lobby of a game this machine hosts or
joins) the menus make the controller of port n local player n (network_client_manager.c), a campaign's player 1 is the port that chose its profile and
co-op's player 2 the port that chose the second (menu_functions.c's campaign_profile and coop_player2_choose, main.c's create_local_players), and the
windows go to the local players in their ports' order (main.c's main_game_render), player 1 across the top with three. A second person joins with a press on
their own controller: in the lobby a controller that is not playing presses START ("port lobby join") and chooses a profile, or ADD PLAYER first takes the
next START of another controller; co-op's player 2 chooses a profile with their own controller. So a controller that is to join must be a port of its own
when it is pressed, and one that only shares player 1's cannot join.

The layouts. "merged" (as before): the controller of KPAD channel n is port n, so the first shares the GamePad's port, for one person playing on the TV.
"separate": the GamePad alone is port 0 and each controller has a port of its own. "auto" (the default) is merged for one person and separate once a
second joins, as the desktop build does with a lone gamepad and the keyboard (xinput_sdl.c's port_gamepad): while this machine has one player every
controller drives player 1 with the GamePad, and once the menus have two players, or are adding one (pc_menu_split_players: co-op chosen, ADD PLAYER, a
controller choosing its profile, a split screen game), each controller drives its own port; when the machine is back to one player, on the GamePad's
port, they share again (one left on another port, the second of two whose first left, keeps its controller: pc_menu_split_players says so). With
one player the controllers have no port of their own, so a second person's START is player 1's: they choose ADD PLAYER in the lobby (the line under its
players says so) or CO-OP CAMPAIGN, and the controllers take their own ports, whose LEDs then show their numbers. A controller moves only while every
controller is at rest, so that a held button is not pressed again on its new port. The GamePad is player 1 in every layout.

Which port is each controller's own is wiiu_pad_players.c's map: the lowest player no controller has, given when it connects and kept for it, by its
Bluetooth address, while it goes and comes back (its battery, its range, Bloopair's sleep), whatever channel the system then gives it. Each controller's
lights show the number of the player it drives (wiiu_input.c): 1 while it shares the GamePad's, none if it drives nobody.

The Xbox pad's four face buttons are matched to the Wii U's in the game by place (the Wii U's B is where the Xbox's A is): the player's thumb finds the
action where it expects it, and the prompts on the screen show the icon of the letter that is there (hud_hires_tags.c, port_button_icon). In the menus
they are matched by letter (the Wii U's A selects, its B goes back, as on every Nintendo console), because the menus' own prompts name the Xbox's
letters. The setting input.wiiu_face_buttons says "position" or "label" for both instead (a Wii Remote's buttons are laid out in the Xbox's own terms, and
are not matched: wiiu_wimote.c). The Xbox's black and white buttons, which later pads put on the shoulders, are the Wii U's R and L; its triggers are ZR and ZL
(buttons, so all or nothing). The motors rumble with the game (input.wiiu_rumble).

A cinematic that can be skipped is skipped by holding the Xbox's A (the Wii U's B, by place), + (Start) or a finger on the GamePad's screen for a second: a press
that is only a press does nothing then, so that a cinematic is not skipped by accident (the game itself skips at the first A).

L + R + - shows or hides the performance overlay.

The text chat (halo_keyboard.h). D-pad Up opens the chat's on-screen keyboard (network_chat.c); while it is up the game gets nothing from the controller that opened it (the
port's pad is zeroed, and each button held as the keyboard closed is kept from the game until it is let go, so the B that closes it is not a new press), nor the gyro. The
keyboard reads the Wii U's letters (wiiu_chat_input.h): A types, B closes, X deletes, Y gives capitals, + sends. A vote running takes the first port's D-pad left and right.
The GamePad's touch is a tap at the moment a finger comes down.

The GamePad's gyro can aim (input.wiiu_gyro_aim, "off" by default; "always", or "zoom" for only while zoomed in): its turn rates are added to the right stick
while no menu has the screen.

Wii Remotes. One with a Nunchuk is a controller of its own layout (input.wiiu_wiimote_layout: B fires, A jumps, Nunchuk Z throws grenades, ...; wiiu_wimote.c), one
alone has the menus' layout. The view turns by the Remote's IR pointer outside a free-aim box (a turn rate that reaches the game as the right stick, but not in the
menus: WIIU_PAD look_x and look_y) or, for a Remote Plus or one with a MotionPlus adapter that is the first player's, by its MotionPlus (as a mouse, like the
GamePad's gyro, which it replaces while the first player has such a Remote), as input.wiiu_wiimote_look says.
*/

#include "platform.h"
#include "port_config.h"
#include "controller_remap.h"
#include "wiiu_shell.h"
#include "wiiu_input.h"
#include "wiiu_chat_input.h"
#include "wiiu_keyboard.h"
#include "wiiu_pad_players.h"
#include "wiiu_virtual_pads.h"
#include "wiiu_update.h"
#include "test_input_bot.h"

#include <coreinit/time.h>
#include <stdlib.h>
#include <string.h>

#define PORT_COUNT WIIU_PAD_PORTS

/* (network_chat.c's: the chat's keyboard is up, and the port that opened it; network_votekick.c's: a vote the first port's D-pad answers is running) */
extern unsigned char network_chat_keyboard_open(void);
extern short network_chat_controller(void);
extern unsigned char network_vote_pad_open(void);

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
/* (hud_hires_tags.c's: 1 while a menu has the screen; 1 while it is the main menu's) */
int port_menus_active(void);
int port_main_menu_active(void);

int wiiu_face_buttons_by_label(void)
{
	static int mode;
	static unsigned long read_at = (unsigned long)-1;

	if (read_at != config_changes())
	{
		const char *setting = config_string("input.wiiu_face_buttons");

		read_at = config_changes();
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
	/* (a Wii Remote's face buttons are the Xbox's own, whatever the match of the Wii U's) */
	if (pad->xbox_faces & WIIU_XBOX_A)
		state->bAnalogButtons[XINPUT_GAMEPAD_A] = 0xff;
	if (pad->xbox_faces & WIIU_XBOX_B)
		state->bAnalogButtons[XINPUT_GAMEPAD_B] = 0xff;
	if (pad->xbox_faces & WIIU_XBOX_X)
		state->bAnalogButtons[XINPUT_GAMEPAD_X] = 0xff;
	if (pad->xbox_faces & WIIU_XBOX_Y)
		state->bAnalogButtons[XINPUT_GAMEPAD_Y] = 0xff;
	state->sThumbLX = stick(pad->left_x);
	state->sThumbLY = stick(pad->left_y);
	state->sThumbRX = stick(pad->right_x);
	state->sThumbRY = stick(pad->right_y);
	/* a pointer's turn rate is a push of the right stick, when it is more than the stick's, and not in the menus (a Remote pointing off the middle must not scroll them) */
	if ((pad->look_x != 0.0f || pad->look_y != 0.0f) && !port_menus_active() &&
		pad->look_x * pad->look_x + pad->look_y * pad->look_y > pad->right_x * pad->right_x + pad->right_y * pad->right_y)
	{
		state->sThumbRX = stick(pad->look_x);
		state->sThumbRY = stick(pad->look_y);
	}
}

/* debug.virtual_gamepads and debug.virtual_gamepad_script (wiiu_virtual_pads.h): the script's pads for the port, merged with what its controllers do. The script
is updated each time a port is read, as xinput_sdl.c's is, so that a press is read 8 times (a frame or two) before it is let go of. */
static void virtual_pad_apply(unsigned int port, XINPUT_GAMEPAD *state)
{
	static const struct
	{
		unsigned int source;
		WORD mask;
	} digital[] =
	{
		{ WIIU_VPAD_UP, XINPUT_GAMEPAD_DPAD_UP }, { WIIU_VPAD_DOWN, XINPUT_GAMEPAD_DPAD_DOWN }, { WIIU_VPAD_LEFT, XINPUT_GAMEPAD_DPAD_LEFT },
		{ WIIU_VPAD_RIGHT, XINPUT_GAMEPAD_DPAD_RIGHT }, { WIIU_VPAD_START, XINPUT_GAMEPAD_START }, { WIIU_VPAD_BACK, XINPUT_GAMEPAD_BACK },
	};
	static const struct
	{
		unsigned int source;
		int analog;
	} analog[] =
	{
		{ WIIU_VPAD_A, XINPUT_GAMEPAD_A }, { WIIU_VPAD_B, XINPUT_GAMEPAD_B }, { WIIU_VPAD_X, XINPUT_GAMEPAD_X }, { WIIU_VPAD_Y, XINPUT_GAMEPAD_Y },
		{ WIIU_VPAD_LB, XINPUT_GAMEPAD_WHITE }, { WIIU_VPAD_RB, XINPUT_GAMEPAD_BLACK }, { WIIU_VPAD_LT, XINPUT_GAMEPAD_LEFT_TRIGGER },
		{ WIIU_VPAD_RT, XINPUT_GAMEPAD_RIGHT_TRIGGER },
	};
	struct wiiu_virtual_pad pad;
	const long count = config_integer("debug.virtual_gamepads");
	unsigned int i;

	if (count <= 0)
		return;
	wiiu_virtual_pads_update((int)count, config_string("debug.virtual_gamepad_script"), wiiu_input_milliseconds());
	wiiu_virtual_pads_get(port, &pad);
	for (i = 0; i < sizeof(digital) / sizeof(digital[0]); i++)
		if (pad.buttons & digital[i].source)
			state->wButtons |= digital[i].mask;
	for (i = 0; i < sizeof(analog) / sizeof(analog[0]); i++)
		if (pad.buttons & analog[i].source)
			state->bAnalogButtons[analog[i].analog] = 0xff;
	/* (a stick only overrides the pad's when it is pushed further) */
	if (abs(pad.left_x) > abs(state->sThumbLX)) state->sThumbLX = pad.left_x;
	if (abs(pad.left_y) > abs(state->sThumbLY)) state->sThumbLY = pad.left_y;
	if (abs(pad.right_x) > abs(state->sThumbRX)) state->sThumbRX = pad.right_x;
	if (abs(pad.right_y) > abs(state->sThumbRY)) state->sThumbRY = pad.right_y;
}

/* (the game's: NONE, -1, while the first player is not zoomed in) */
short player_control_get_zoom_level(short local_player_index);

/* The GamePad's gyro aims as a mouse does (as Steam Input's "gyro to mouse" and GyroWiki recommend), not as a stick: how far the pad turned since the game last
asked becomes how far the view turns, sensitivity times as far (1: the view turns as far as the pad), through the game's path for the PC's mouse (player_control.c,
halo_linux_mouse_look: after the stick's look curve and the aim assist's magnetism, which would slow the view over a target and pull it after one, as GyroWiki
warns; only the zoom divides it). input.wiiu_gyro_aim "off", "always" or "zoom" (only while zoomed in); input.wiiu_gyro_space "player" (turn the pad left and
right however it is held), "turn" or "lean"; input.wiiu_gyro_invert_y; input.wiiu_gyro_pause "touch" (a thumb on the GamePad's screen holds the view still while the
pad is brought back to a comfortable hold, GyroWiki's "lifting the mouse", Steam Input's held R3 on a pad whose R3 the game does not use), "stick" (while the right
stick is pushed) or "none". Nothing in the menus. */
int wiiu_input_touch(float *x, float *y);
int halo_linux_mouse_look(short gamepad_index, float *yaw, float *pitch);
int halo_linux_mouse_aiming(short gamepad_index);

/* (which aimed last, the gyro or the right stick, counted in the game's polls: halo_linux_mouse_aiming) */
static unsigned long aim_polls, gyro_aimed_at, stick_aimed_at;

int halo_linux_mouse_look(short gamepad_index, float *yaw, float *pitch)
{
	static int mode; /* 0 off, 1 always, 2 only zoomed in */
	static int pause; /* 0 none, 1 a touch, 2 the right stick */
	static float sensitivity, invert;
	static unsigned long read_at = (unsigned long)-1;
	const float full_turn = 6.2831853f;
	float turn, tilt, x, y;
	int remote;

	*yaw = *pitch = 0.0f;
	if (gamepad_index != 0)
		return 0;
	/* (read again whenever the settings change: the Wii U GamePad screen sets them while the game runs) */
	if (read_at != config_changes())
	{
		const char *setting = config_string("input.wiiu_gyro_aim");
		const char *space = config_string("input.wiiu_gyro_space");
		const char *paused_by = config_string("input.wiiu_gyro_pause");

		read_at = config_changes();
		pause = !strcmp(paused_by, "touch") ? 1 : !strcmp(paused_by, "stick") ? 2 : 0;
		mode = !strcmp(setting, "always") ? 1 : !strcmp(setting, "zoom") ? 2 : 0;
		sensitivity = (float)config_real("input.wiiu_gyro_sensitivity");
		if (!(sensitivity > 0.0f && sensitivity <= 10.0f))
			sensitivity = 1.0f;
		invert = config_boolean("input.wiiu_gyro_invert_y") ? -1.0f : 1.0f;
		wiiu_input_set_gyro_space(!strcmp(space, "turn") ? WIIU_GYRO_SPACE_TURN : !strcmp(space, "lean") ? WIIU_GYRO_SPACE_LEAN : WIIU_GYRO_SPACE_PLAYER);
	}
	/* (taken whether it is used or not: a turn made in a menu must not jump the view when the game comes back) */
	wiiu_input_gyro_take(&turn, &tilt);
	/* the first player's Wii Remote, when its MotionPlus is how it looks (input.wiiu_wiimote_look): its turn is the view's, in place of the GamePad's gyro (the pad lies
	in a lap while the Remote aims), and aims whatever input.wiiu_gyro_aim says of the GamePad's: the sensitivity, the space, the invert and the pause are the gyro's */
	remote = wiiu_input_remote_look_take(&x, &y);
	if (remote)
	{
		turn = x;
		tilt = y;
	}
	/* (nothing while the chat's keyboard is up on the first port: the view stays where it was) */
	if ((!mode && !remote) || port_menus_active() || (!remote && mode == 2 && player_control_get_zoom_level(0) < 0) ||
		(network_chat_keyboard_open() && network_chat_controller() == 0))
		return 0;
	/* (the stick's: XInputGetState of this poll, which comes first) */
	if ((pause == 1 && wiiu_input_touch(&x, &y)) || (pause == 2 && stick_aimed_at == aim_polls))
		return 0;
	if (turn == 0.0f && tilt == 0.0f)
		return 0;
	/* (a turn of more than about a fifth of a degree since the last poll, 5 to 10 degrees a second, is aiming, not the hand's tremor) */
	if (turn * turn + tilt * tilt > 0.0005f * 0.0005f)
		gyro_aimed_at = aim_polls;
	/* (the game's yaw and the pad's turn are both left positive, and the game's pitch up positive against the pad's tilt: tried by hand, the pad points the
	view, its top tilted away looking down, unless input.wiiu_gyro_invert_y) */
	*yaw = turn * full_turn * sensitivity;
	*pitch = -tilt * full_turn * sensitivity * invert;
	return 1;
}

/* whether the first player aims with the gyro (it turned after the right stick was last pushed): then the view's magnetism leaves them be, as it does the PC's
mouse; the bullets' autoaim stays */
int halo_linux_mouse_aiming(short gamepad_index)
{
	return gamepad_index == 0 && gyro_aimed_at != 0 && gyro_aimed_at >= stick_aimed_at;
}

/* debug.test_input "bot:<seed>", "tickbot:<seed>" or "look:<seed>" (config.toml's [debug]; HALO_TEST_INPUT does not exist on the console): the first
player's controller (port 0, the GamePad's) is played by the scripted player of test_input_bot.h, as the desktop build's is (xinput_sdl.c), so that a console
run and a host run are the same fight. It is timed by the game's ticks (network_test_game_seconds: the game's time while a game runs), so it plays alike at
any frame rate, each frame's reads taking the pattern at the time of the frame's first tick; with debug.fixed_step (a tick a frame) it is tick for tick.
Only while a game runs, never in the menus (the desktop's runs on in them, on the clock; the network test's own menu steps need no input). Its sticks and
triggers replace the controller's, its buttons are added to them, and it comes before hold_to_skip, which keeps a short press of A from a cinematic. With
the setting empty, or one it does not play ("menu:" is the desktop's), nothing changes. */
double network_test_game_seconds(void);
/* (and where the nearest enemy is from the first player, for "tickhunt:") */
unsigned char network_test_hunt_target(double *distance, double *yaw_error, double *pitch_error);

static void test_input_gamepad(XINPUT_GAMEPAD *state)
{
	static unsigned long read_at = (unsigned long)-1;
	static int seed = -1, mode = TEST_INPUT_BOT;
	double seconds;

	if (read_at != config_changes())
	{
		const char *setting = config_string("debug.test_input");
		int new_seed = -1, new_mode = TEST_INPUT_BOT;

		read_at = config_changes();
		if (!test_input_bot_parse(setting, &new_seed, &new_mode))
		{
			new_seed = -1;
			if (setting[0])
				wiiu_log("Input: debug.test_input \"%s\" is not a scripted player the console plays (bot:, tickbot:, tickhunt: and look: are)", setting);
		}
		if (new_seed != seed || new_mode != mode)
		{
			seed = new_seed;
			mode = new_mode;
			if (seed >= 0)
				wiiu_log("Input: the first player is the scripted player (seed %d, %s), by the game's ticks, while a game runs", seed,
					mode == TEST_INPUT_LOOK ? "standing still, only looking" : mode == TEST_INPUT_HUNT ? "hunting the nearest enemy" :
					"walking, turning and firing");
		}
	}
	if (seed < 0)
		return;
	seconds = network_test_game_seconds();
	if (seconds < 0.0)
		return;
	if (mode == TEST_INPUT_HUNT)
	{
		struct test_input_target target;

		target.valid = network_test_hunt_target(&target.distance, &target.yaw_error, &target.pitch_error);
		test_input_bot_pad(state, seconds, seed, mode, &target);
		return;
	}
	test_input_bot_pad(state, seconds, seed, mode, NULL);
}

/* Hold to skip: while the game says a cinematic can be skipped (touch_game.c: the game's own question) and no menu has the screen, A, Start and a finger on the
touch screen (port 0's) are kept from the game; held for SKIP_HOLD_MS, A is given to the game for a few polls (it skips at A), and nothing more until they are let go. */
#define SKIP_HOLD_MS 1000u
#define SKIP_PRESS_POLLS 6

int touch_game_cinematic_skippable(void);

static struct
{
	OSTime held_since; /* 0: not held */
	int pressing; /* polls of A left to give */
	int spent; /* skipped: wait for the buttons to be let go */
} skip_hold[PORT_COUNT];

/* (wiiu_gx2.c draws a bar of how far the hold is, 0 to 1; port 0's hold, or the furthest) */
void wiiu_skip_progress(float progress);
static float skip_shown[PORT_COUNT];

static void skip_show(unsigned int port, float progress)
{
	unsigned int other;
	float furthest = 0.0f;

	skip_shown[port] = progress;
	for (other = 0; other < PORT_COUNT; other++)
		if (skip_shown[other] > furthest)
			furthest = skip_shown[other];
	wiiu_skip_progress(furthest);
}

static void hold_to_skip(unsigned int port, XINPUT_GAMEPAD *state)
{
	float x, y;
	const int touched = port == 0 && wiiu_input_touch(&x, &y);
	const int held = state->bAnalogButtons[XINPUT_GAMEPAD_A] > 0x40 || (state->wButtons & XINPUT_GAMEPAD_START) || touched;

	if (skip_hold[port].pressing > 0)
	{
		skip_hold[port].pressing--;
		state->bAnalogButtons[XINPUT_GAMEPAD_A] = 0xff;
		state->wButtons &= (WORD)~XINPUT_GAMEPAD_START;
		return;
	}
	if (port_menus_active() || !touch_game_cinematic_skippable())
	{
		skip_hold[port].held_since = 0;
		skip_hold[port].spent = 0;
		skip_show(port, 0.0f);
		return;
	}
	/* (a cinematic that can be skipped is playing: these are the skip's alone) */
	state->bAnalogButtons[XINPUT_GAMEPAD_A] = 0;
	state->wButtons &= (WORD)~XINPUT_GAMEPAD_START;
	if (!held)
	{
		skip_hold[port].held_since = 0;
		skip_hold[port].spent = 0;
		skip_show(port, 0.0f);
		return;
	}
	if (skip_hold[port].spent)
	{
		skip_show(port, 0.0f);
		return;
	}
	if (!skip_hold[port].held_since)
		skip_hold[port].held_since = OSGetSystemTime();
	skip_show(port, (float)OSTicksToMilliseconds(OSGetSystemTime() - skip_hold[port].held_since) / (float)SKIP_HOLD_MS);
	if (OSGetSystemTime() - skip_hold[port].held_since >= (OSTime)OSMillisecondsToTicks(SKIP_HOLD_MS))
	{
		skip_hold[port].spent = 1;
		skip_hold[port].pressing = SKIP_PRESS_POLLS;
		platform_log("input: a cinematic skipped (held %u ms)", SKIP_HOLD_MS);
	}
}

/* L + R + - on the GamePad (or the Pro Controller merged with it) shows or hides the performance overlay (wiiu_gx2.c); the - is kept from the game while the three are down */
void wiiu_overlay_toggle(void);

static void overlay_combo(const struct wiiu_pad *pad, XINPUT_GAMEPAD *state)
{
	static int was_down;
	const unsigned int combo = WIIU_PAD_L | WIIU_PAD_R | WIIU_PAD_MINUS;
	const int down = (pad->buttons & combo) == combo;

	if (down && !was_down)
		wiiu_overlay_toggle();
	was_down = down;
	if (down)
		state->wButtons &= (WORD)~XINPUT_GAMEPAD_BACK;
}

VOID WINAPI XInitDevices(DWORD preallocation_type_count, PXDEVICE_PREALLOC_TYPE preallocation_types)
{
	(void)preallocation_type_count;
	(void)preallocation_types;
}

/* input.wiiu_controller_layout: "auto" (the default), "separate" or "merged" (wiiu_input.h; wiiu_pad_players.h). An unknown value is auto's. */
static void layout_apply(void)
{
	static unsigned long read_at = (unsigned long)-1;

	if (read_at != config_changes())
	{
		const char *layout = config_string("input.wiiu_controller_layout");

		read_at = config_changes();
		wiiu_input_set_layout(!strcmp(layout, "separate") ? WIIU_PAD_LAYOUT_SEPARATE : !strcmp(layout, "merged") ? WIIU_PAD_LAYOUT_MERGED :
			WIIU_PAD_LAYOUT_AUTO);
		/* the Wii Remotes' (input.wiiu_wiimote_*: wiiu_wimote.h) */
		wiiu_input_set_wimote(config_string("input.wiiu_wiimote_layout"), config_string("input.wiiu_wiimote_look"),
			(float)config_real("input.wiiu_wiimote_pointer_box"), (float)config_real("input.wiiu_wiimote_pointer_speed"),
			config_boolean("input.wiiu_wiimote_motion_invert_x"));
		wiiu_input_set_wimote_edge(config_boolean("input.wiiu_wiimote_motion_edge"), (float)config_real("input.wiiu_wiimote_motion_edge_box"),
			(float)config_real("input.wiiu_wiimote_motion_edge_range"), (float)config_real("input.wiiu_wiimote_motion_edge_speed"));
	}
}

/* (port/linux/game/menu_functions.c: two or more players on this machine, or one being added: co-op's player 2, ADD PLAYER or a controller choosing its
profile in the lobby, a split screen game; or one in the lobby on a controller other than the first. "auto" gives the controllers their own players
then, as xinput_sdl.c gives a lone gamepad its own port) */
unsigned char pc_menu_split_players(void);

/* The ports with a pad, once a frame (input_xbox.c asks at the start of each, before it reads them): the controllers polled and arranged first
(wiiu_input_poll), so that the frame's reads and the game's players agree. */
static DWORD connected_gamepads(void)
{
	DWORD mask = 0;
	unsigned int port;

	layout_apply();
	wiiu_input_poll(pc_menu_split_players() != 0);
	for (port = 0; port < PORT_COUNT; port++)
		if (wiiu_input_port_present(port))
			mask |= 1UL << port;
	/* (the script's pads are ports of their own, whatever controllers are connected: debug.virtual_gamepads) */
	mask |= wiiu_virtual_pads_ports((int)config_integer("debug.virtual_gamepads"));
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

/* how many controllers there are, as the game was last told (ADD PLAYER's screen asks: menu_functions.c), and that the last used was one
(network_votekick.c's prompts: the Wii U has no keyboard) */
int halo_gamepad_count(void)
{
	DWORD mask = reported_gamepads;
	int count = 0;

	for (; mask; mask &= mask - 1)
		count++;
	return count;
}

int halo_gamepad_last_used(void)
{
	return 1;
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

/* ---------- the text chat's on-screen keyboard (halo_keyboard.h; wiiu_chat_input.h has the buttons' mapping) */

/* each port's buttons as the pad had them this frame (the bits of halo_chat_pad_button), before the keyboard takes them from the game */
static unsigned long chat_pad[PORT_COUNT];
static struct wiiu_chat_tap chat_tap;

unsigned long halo_chat_pad_buttons_for_port(short port)
{
	return port >= 0 && port < PORT_COUNT ? chat_pad[port] : 0;
}

unsigned long halo_chat_pad_buttons(void)
{
	short port = network_chat_controller();

	return chat_pad[port >= 0 && port < PORT_COUNT ? port : 0];
}

/* (network_chat.c's: the keys, the chips and CLOSE of the GamePad's keyboard) */
void network_chat_keyboard_press(short row, short column);
void network_chat_keyboard_channel(short channel);
void network_chat_keyboard_close(void);

/* A tap while the GamePad shows the keyboard (wiiu_keyboard.h) is on its own keys, chips and CLOSE: it presses what it hit, and the chat is told of no tap (its own keyboard
is hit-tested in the game's picture, which the GamePad's keyboard covers); a tap on none of them does nothing. Without the GamePad's keyboard (the setting off, the glyphs
not made), the tap is the chat's, as a share of the picture. */
int halo_chat_taps(float *x, float *y)
{
	float pixel_x, pixel_y;
	struct wk_hit hit;

	if (!wiiu_keyboard_shown())
		return wiiu_chat_tap_take(&chat_tap, x, y);
	if (wiiu_chat_tap_take_pixels(&chat_tap, &pixel_x, &pixel_y))
	{
		switch (wiiu_keyboard_hit(pixel_x, pixel_y, &hit))
		{
		case WK_HIT_KEY:
			network_chat_keyboard_press((short)hit.row, (short)hit.column);
			break;
		case WK_HIT_CHANNEL:
			network_chat_keyboard_channel((short)hit.channel);
			break;
		case WK_HIT_CLOSE:
			network_chat_keyboard_close();
			break;
		default:
			break;
		}
	}
	return 0;
}

/* The players of the other ports (split screen) open their own keyboards with their D-pad Up, as the desktop's remapped chat button does (controller_remap_pressed(port,
1 << PAD_CHAT), network_chat.c): the first port's is halo_chat_pad_buttons' (it is the GamePad's, with the controllers that share it). A press is latched when it comes and taken
by the question, which network_chat_update asks of every port every frame. */
static unsigned long chat_pad_last[PORT_COUNT];
static unsigned int chat_guest_pressed[PORT_COUNT];

int controller_remap_connected(short port)
{
	return port > 0 && port < PORT_COUNT && wiiu_input_port_present((unsigned int)port);
}

unsigned int controller_remap_pressed(short port, unsigned int mask)
{
	unsigned int result;

	if (port <= 0 || port >= PORT_COUNT)
		return 0;
	result = chat_guest_pressed[port] & mask;
	chat_guest_pressed[port] &= ~mask;
	return result;
}

/* each button held as the keyboard closed (the B that closed it, the + that sent the line) is kept from the game until it is let go: it would see a new press of it, a pause
menu opened, a melee, a jump (the triggers are analog buttons too) */
static WORD chat_held_buttons[PORT_COUNT];
static unsigned char chat_held_analog[PORT_COUNT];

static void chat_pad_hold_over(int port, XINPUT_GAMEPAD *pad)
{
	int index;

	if (network_chat_keyboard_open() && network_chat_controller() == port)
	{
		chat_held_buttons[port] = pad->wButtons;
		chat_held_analog[port] = 0;
		for (index = 0; index < 8; index++)
			if (pad->bAnalogButtons[index])
				chat_held_analog[port] |= (unsigned char)(1 << index);
		memset(pad, 0, sizeof(*pad));
		return;
	}
	chat_held_buttons[port] &= pad->wButtons;
	pad->wButtons &= (WORD)~chat_held_buttons[port];
	for (index = 0; index < 8; index++)
	{
		if (!(chat_held_analog[port] & (1 << index)))
			continue;
		if (pad->bAnalogButtons[index])
			pad->bAnalogButtons[index] = 0;
		else
			chat_held_analog[port] &= (unsigned char)~(1 << index);
	}
}

/* the first port's D-pad left and right held as a vote took them, which the game gets none of until each is let go: else a vote's end would hand it a press */
static WORD vote_held_buttons;

static void vote_pad_hold_over(XINPUT_GAMEPAD *pad)
{
	const WORD sides = XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT;

	if (network_vote_pad_open())
		vote_held_buttons |= (WORD)(pad->wButtons & sides);
	else
		vote_held_buttons &= pad->wButtons;
	if (network_vote_pad_open())
		pad->wButtons &= (WORD)~sides;
	else
		pad->wButtons &= (WORD)~vote_held_buttons;
}

/* A menu's text field (platform_text_field: wiiu_keyboard.h) edited with the GamePad's keyboard: a tap on its keys types into the game's key queue (XInputDebugGetKeystroke below),
and DONE and CANCEL are the Xbox's A and B pressed for a few polls, which the field's row takes as "done" and the menu as "back" (a press the game sees once: it counts a button's
ticks). The tap is the keyboard's alone: d3d8_gx2.c's pointer leaves the screen's touch to it meanwhile. */
#define FIELD_PRESS_POLLS 6
static int field_press_a, field_press_b;

static void field_apply(XINPUT_GAMEPAD *state)
{
	float x, y;

	if (wiiu_keyboard_field_active() && wiiu_chat_tap_take_pixels(&chat_tap, &x, &y))
	{
		switch (wiiu_keyboard_field_press(x, y))
		{
		case WK_ACTION_DONE:
			field_press_a = FIELD_PRESS_POLLS;
			break;
		case WK_ACTION_CANCEL:
			field_press_b = FIELD_PRESS_POLLS;
			break;
		default:
			break;
		}
	}
	if (!wiiu_keyboard_field_active())
		field_press_a = field_press_b = 0;
	if (field_press_a > 0)
	{
		field_press_a--;
		state->bAnalogButtons[XINPUT_GAMEPAD_A] = 0xff;
	}
	if (field_press_b > 0)
	{
		field_press_b--;
		state->bAnalogButtons[XINPUT_GAMEPAD_B] = 0xff;
	}
}

/* the updater's panel is answered by the Wii U's letters: a Wii Remote's Xbox face buttons count as the letters of the same name, as the menus take them */
static unsigned int panel_buttons(const struct wiiu_pad *pad)
{
	unsigned int buttons = pad->buttons;

	if (pad->xbox_faces & WIIU_XBOX_A)
		buttons |= WIIU_PAD_A;
	if (pad->xbox_faces & WIIU_XBOX_B)
		buttons |= WIIU_PAD_B;
	if (pad->xbox_faces & WIIU_XBOX_X)
		buttons |= WIIU_PAD_X;
	if (pad->xbox_faces & WIIU_XBOX_Y)
		buttons |= WIIU_PAD_Y;
	return buttons;
}

DWORD WINAPI XInputGetState(HANDLE device, PXINPUT_STATE state)
{
	int port = controller_port(device);
	struct wiiu_pad pad;
	int have_pad;

	memset(state, 0, sizeof(*state));
	if (port < 0)
		return ERROR_DEVICE_NOT_CONNECTED;
	layout_apply();
	memset(&pad, 0, sizeof(pad));
	have_pad = wiiu_input_read((unsigned int)port, &pad);
	/* the updater's panel at the main menu (wiiu_update.c) is told where the game is, and has the pads' buttons while it is up: the menu
	under it sees none */
	if (port == 0)
		wiiu_update_menus(port_main_menu_active(), port_menus_active());
	if (wiiu_update_panel_input((unsigned int)port, have_pad ? panel_buttons(&pad) : 0u))
	{
		memset(&pad, 0, sizeof(pad));
		have_pad = 0;
	}
	/* (what the pad has, for the chat's keyboard, before it takes it from the game; the finger once a frame, for its taps) */
	chat_pad[port] = have_pad ? wiiu_chat_buttons(&pad) : 0;
	if (port > 0 && (chat_pad[port] & ~chat_pad_last[port] & (1UL << HALO_CHAT_PAD_UP)))
		chat_guest_pressed[port] |= 1u << PAD_CHAT;
	chat_pad_last[port] = chat_pad[port];
	if (port == 0)
	{
		float touch_x, touch_y;
		const int touched = wiiu_input_touch(&touch_x, &touch_y);

		wiiu_chat_tap_poll(&chat_tap, touched, touch_x, touch_y);
	}
	if (have_pad)
		pad_state(&pad, &state->Gamepad);
	virtual_pad_apply((unsigned int)port, &state->Gamepad);
	if (port == 0)
		field_apply(&state->Gamepad);
	if (port == 0)
		test_input_gamepad(&state->Gamepad);
	hold_to_skip((unsigned int)port, &state->Gamepad);
	if (port == 0)
	{
		overlay_combo(&pad, &state->Gamepad);
		aim_polls++;
		/* (the game's dead range: input_xbox.c, GAMEPAD_STICK_DEAD_RANGE) */
		if (state->Gamepad.sThumbRX > 9000 || state->Gamepad.sThumbRX < -9000 || state->Gamepad.sThumbRY > 9000 || state->Gamepad.sThumbRY < -9000)
			stick_aimed_at = aim_polls;
	}
	/* the chat's keyboard takes the controller that opened it from the game; a vote running takes the first port's D-pad left and right */
	chat_pad_hold_over(port, &state->Gamepad);
	if (port == 0)
		vote_pad_hold_over(&state->Gamepad);
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

/* the Xbox's debug keyboard: the GamePad keyboard's keys for a menu's text field (wiiu_keyboard.h), a press and a release each; nothing otherwise */
DWORD WINAPI XInputDebugGetKeystroke(PXINPUT_DEBUG_KEYSTROKE keystroke)
{
	struct wk_keystroke next;

	memset(keystroke, 0, sizeof(*keystroke));
	if (!wiiu_keyboard_keys_next(&next))
		return ERROR_HANDLE_EOF;
	keystroke->VirtualKey = next.virtual_key;
	keystroke->Ascii = next.ascii;
	keystroke->Flags = next.flags;
	return ERROR_SUCCESS;
}
