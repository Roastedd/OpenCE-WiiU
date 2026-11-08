/*
HALO_KEYBOARD.H

The keyboard and mouse's own controls (port/linux/src/xinput_sdl.c): in the
game they do not drive the first controller, as they do in the menus, but
the player's actions themselves, by the bindings in config.toml's
[controls] (Settings > Controls Setup changes them). The game takes them
with the first controller's (source/input/input_abstraction.c), so the
profile's button layout is the controller's alone, and reloading has a key
of its own (source/game/player_control.c).
*/

#ifndef HALO_KEYBOARD_H
#define HALO_KEYBOARD_H

enum halo_keyboard_action
{
	HALO_KEYBOARD_MOVE_FORWARD,
	HALO_KEYBOARD_MOVE_BACKWARD,
	HALO_KEYBOARD_STRAFE_LEFT,
	HALO_KEYBOARD_STRAFE_RIGHT,
	HALO_KEYBOARD_JUMP,
	HALO_KEYBOARD_CROUCH,
	HALO_KEYBOARD_FIRE,
	HALO_KEYBOARD_THROW_GRENADE,
	HALO_KEYBOARD_MELEE,
	HALO_KEYBOARD_RELOAD,
	HALO_KEYBOARD_ZOOM,
	HALO_KEYBOARD_SWITCH_WEAPON,
	HALO_KEYBOARD_SWITCH_GRENADE,
	HALO_KEYBOARD_ACTION,
	HALO_KEYBOARD_FLASHLIGHT,
	/* (these two press the controller's Back and Start, which the menus
	read from it) */
	HALO_KEYBOARD_SCOREBOARD,
	HALO_KEYBOARD_PAUSE,
	HALO_KEYBOARD_SCREENSHOT,
	/* (voice chat's, which the game itself does not read:
	halo_push_to_talk_held) */
	HALO_KEYBOARD_PUSH_TO_TALK,
	/* (the text chat's, which the game itself does not read: a line to
	everyone, or to the team: port/linux/game/network_chat.c) */
	HALO_KEYBOARD_CHAT,
	HALO_KEYBOARD_TEAM_CHAT,
	/* (the players' votes', yes and no in the vote running: port/linux/game/
	network_votekick.c, read with the chat's keys) */
	HALO_KEYBOARD_VOTE_YES,
	HALO_KEYBOARD_VOTE_NO,
	NUMBER_OF_HALO_KEYBOARD_ACTIONS
};

/* the actions the keyboard and mouse hold for the player on the controller
(a bit for each halo_keyboard_action): theirs is the first's; none while a
menu or the console is up, or a chat line is typed */
unsigned long halo_keyboard_actions(short controller_index);
/* whether voice chat's push to talk is held (controls.push_to_talk), in the
game and in the menus alike */
int halo_push_to_talk_held(void);

/* the first controller's buttons (with the touch controls'), as the text
chat's on-screen keyboard reads them: a bit for each (port/linux/game/
network_chat.c). The game gets none of the controller while that keyboard is
up (network_chat_keyboard_open), nor the touch controls' stick and look */
enum halo_chat_pad_button
{
	HALO_CHAT_PAD_UP,
	HALO_CHAT_PAD_DOWN,
	HALO_CHAT_PAD_LEFT,
	HALO_CHAT_PAD_RIGHT,
	HALO_CHAT_PAD_A,
	HALO_CHAT_PAD_B,
	HALO_CHAT_PAD_X,
	HALO_CHAT_PAD_Y,
	HALO_CHAT_PAD_START,
	/* the left stick, pushed most of the way */
	HALO_CHAT_PAD_STICK_UP,
	HALO_CHAT_PAD_STICK_DOWN,
	HALO_CHAT_PAD_STICK_LEFT,
	HALO_CHAT_PAD_STICK_RIGHT,
	/* the shoulder buttons (the Xbox's white and black; the Wii U's L and R): the quick chat's pages turn with them */
	HALO_CHAT_PAD_PAGE_PREVIOUS,
	HALO_CHAT_PAD_PAGE_NEXT,
	NUMBER_OF_HALO_CHAT_PAD_BUTTONS
};

/* What the platform needs to draw the chat's keyboard on a screen of its own (the Wii U's GamePad: port/wiiu/src/wiiu_keyboard.h): the keyboard as network_chat.c has it,
published once a frame (halo_chat_keyboard_publish) from the game's thread. Plain numbers and characters only, so that the game's code and the platform's, built with
different ABIs, agree on it. */
enum
{
	HALO_CHAT_KEYBOARD_ROWS = 4,
	HALO_CHAT_KEYBOARD_COLUMNS = 10,
	HALO_CHAT_KEYBOARD_TEXT = 64,
};

struct halo_chat_keyboard_view
{
	/* whether the keyboard is up for the first player's controller (the one the GamePad belongs to) */
	int open;
	/* who the line is to: network_chat.h's _chat_channel_* */
	int channel;
	int shift;
	int symbols;
	/* the key picked (row 4 is the last, wide keys, each two columns) */
	int row;
	int column;
	/* the line typed, ended */
	int length;
	char text[HALO_CHAT_KEYBOARD_TEXT];
	/* the characters of the keys of the first four rows, as the page and capitals have them */
	char keys[HALO_CHAT_KEYBOARD_ROWS][HALO_CHAT_KEYBOARD_COLUMNS];
	/* 0: the chat's line; 1: a menu's text field (platform_text_field), with no channels, its line shown as stars if masked, and DONE and CANCEL in place of SEND and CLOSE */
	int mode;
	int masked;
	/* the characters the line may hold (0: the chat's 63) */
	int limit;
};

/* a menu's text field is being edited (menu_functions.c: platform_text_field begins and ends it; the keys typed reach it as the keyboard's keystrokes): its text now and the most it
holds, every frame while it is edited, for a platform that shows a keyboard of its own for it. Nothing on the desktop. */
void platform_text_field_text(char const *text, int maximum);

/* the keyboard as it is now (every frame, after network_chat_update); the platform keeps the latest for the thread that draws it. Nothing on the desktop. */
void halo_chat_keyboard_publish(const struct halo_chat_keyboard_view *view);
/* a network game has started: whatever the platform's keyboard needs made ahead of the first keystroke (glyphs, on the Wii U) may be made now, off the game's thread. Nothing on the desktop. */
void halo_chat_keyboard_prewarm(void);

unsigned long halo_chat_pad_buttons(void);
/* whether the first player last used a controller (a button pressed, a
stick moved) rather than the keyboard and mouse: the vote's box shows the
D-pad's keys then (port/linux/game/network_votekick.c) */
int halo_gamepad_last_used(void);
/* Raw physical snapshot: suppress the button which opened controller chat. */
unsigned long halo_chat_pad_buttons_for_port(short port);
/* the taps on the touchscreen outside the menus since the last call while
that keyboard is up, and where the latest was (0 to 1 across and down the
window) */
int halo_chat_taps(float *x, float *y);
/* whether the touch controls hold the screen's bottom left, where the chat
then goes at the top left: on Android, and in a browser once its touch
controls have been used */
int halo_chat_touch_layout(void);
/* the mouse wheel's notches turned while a chat line is typed, since the
last call (up positive): they scroll the lines, not the weapons */
int halo_chat_wheel_steps(void);

#endif
