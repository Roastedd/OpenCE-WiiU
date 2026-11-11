/*
WIIU_INPUT.H

The Wii U's controllers as plain numbers, for xinput_wiiu.c (built with the Xbox's ABI) to turn into XInput: wiiu_input.c (built with the console's) reads
the GamePad through VPAD, and the Pro Controllers, Classic Controllers and Wii Remotes (with a Nunchuk or alone) through KPAD.
*/

#ifndef HALO_WIIU_INPUT_H
#define HALO_WIIU_INPUT_H

/* the buttons, by where they are on the pad (A is the right-hand one of the four, as on a Nintendo pad) */
enum
{
	WIIU_PAD_A = 1 << 0,
	WIIU_PAD_B = 1 << 1,
	WIIU_PAD_X = 1 << 2,
	WIIU_PAD_Y = 1 << 3,
	WIIU_PAD_L = 1 << 4,
	WIIU_PAD_R = 1 << 5,
	WIIU_PAD_ZL = 1 << 6,
	WIIU_PAD_ZR = 1 << 7,
	WIIU_PAD_PLUS = 1 << 8,
	WIIU_PAD_MINUS = 1 << 9,
	WIIU_PAD_UP = 1 << 10,
	WIIU_PAD_DOWN = 1 << 11,
	WIIU_PAD_LEFT = 1 << 12,
	WIIU_PAD_RIGHT = 1 << 13,
	WIIU_PAD_STICK_L = 1 << 14,
	WIIU_PAD_STICK_R = 1 << 15,
	/* the GamePad's TV button: voice chat's push to talk (wiiu_input_push_to_talk), which no Xbox button is */
	WIIU_PAD_TV = 1 << 16,
};

/* The Xbox's face buttons, by name, for a controller that is laid out in the Xbox's terms (a Wii Remote: wiiu_wimote.h) and not in the Wii U's: they reach the
game as they are, whatever input.wiiu_face_buttons says of the Wii U's A, B, X and Y. */
enum
{
	WIIU_XBOX_A = 1 << 0,
	WIIU_XBOX_B = 1 << 1,
	WIIU_XBOX_X = 1 << 2,
	WIIU_XBOX_Y = 1 << 3,
};

struct wiiu_pad
{
	unsigned int buttons;
	/* -1 to 1, up and right positive */
	float left_x, left_y, right_x, right_y;
	/* the GamePad's gyro, port 0 only: turn rates in rotations a second, about the pad's own horizontal axis (pitch: top away from the player positive) and its
	vertical axis (yaw: turning right positive); 0 where there is none */
	float gyro_pitch, gyro_yaw;
	/* the Xbox's face buttons held, WIIU_XBOX_*, which xinput_wiiu.c gives the game as they are (the Wii Remote's; the other controllers use the buttons above) */
	unsigned int xbox_faces;
	/* a turn rate for the view from a pointer (the Wii Remote's IR pointer outside its free-aim box), -1 to 1, right and up positive, already lifted past the game's
	dead range for the stick; the game gets the larger of it and the right stick, and not in the menus */
	float look_x, look_y;
};

#define WIIU_PAD_PORTS 4

/* How the controllers take the ports (input.wiiu_controller_layout), one of wiiu_pad_players.h's: WIIU_PAD_LAYOUT_AUTO (the default), every Pro and Classic
Controller shares the GamePad's port while this machine has one player, and each has its own port, its player in the map, once a second player joins;
WIIU_PAD_LAYOUT_SEPARATE, port 0 is the GamePad alone and each controller has its own; WIIU_PAD_LAYOUT_MERGED, as before the map, the controller of KPAD
channel n is port n's (channel 0's sharing the GamePad's). */
void wiiu_input_set_layout(int layout);

/* Once a frame, before the ports are read: every channel read, the controllers that came and went given their players (wiiu_pad_players.c), the layout
arranged (several_players: this machine has two or more players, or is adding one, as the menus' pc_menu_split_players says), each controller's lights set
to its player's number. */
void wiiu_input_poll(int several_players);

/* Whether the port has a pad now (port 0, the GamePad's, always). */
int wiiu_input_port_present(unsigned int port);

/* How many controllers share the GamePad's player that would be players of their own once a second player joins ("auto" with one player); 0 otherwise. */
int wiiu_input_controllers_merged(void);

/* The settings for the Wii Remotes (input.wiiu_wiimote_*: wiiu_wimote.h), told whenever they change: layout "default" or "z_fire"; look "auto", "pointer", "motion" or
"off"; the free-aim box (the share of the pointer's range, 0 to 0.9, that does not turn the view) and the pointer's turn speed (1: the full stick at the edge of the
screen); invert_x turns the Remote's MotionPlus turn the other way round. */
void wiiu_input_set_wimote(const char *layout, const char *look, float box, float speed, int invert_x);
/* The MotionPlus's edge turn (input.wiiu_wiimote_motion_edge*: wiiu_wimote.h, wimote_motion_edge): on or off, the box and the range in degrees, the speed. */
void wiiu_input_set_wimote_edge(int on, float box_degrees, float range_degrees, float speed);

/* The first player's Wii Remote as the view's mouse, when its look source is its MotionPlus (input.wiiu_wiimote_look): how far it has turned since the last call, as
wiiu_input_gyro_take gives the GamePad's, in the space chosen (wiiu_input_set_gyro_space), and cleared. Returns 1 if the first player has such a Remote, and then
the view is turned by this and not the GamePad's gyro; 0 if not (the outputs are 0). */
int wiiu_input_remote_look_take(float *turn, float *pitch);

/* The first player's Wii Remote as the menus' cursor, when its MotionPlus is on: where it points in the menus, 0 to 1 across and down the picture (x, y), and whether it
moved since the last call. The cursor starts in the middle and follows how far the Remote has turned (the whole picture is about 40 degrees across and 25 down), held in
the picture; holding the Remote still zeroes the MotionPlus as it does in play, and the cursor does not move then. Returns 0 (and leaves x and y) with no such Remote. */
int wiiu_input_remote_cursor(float *x, float *y, int *moved);

/* The GamePad's gyro as a mouse: how far it has turned since the last call, in rotations (turn: right positive; pitch: the top tilting away positive), worked
out from each sample in the space chosen (wiiu_input_set_gyro_space), and cleared. */
void wiiu_input_gyro_take(float *turn, float *pitch);

enum
{
	/* player space (GyroWiki): the pad's yaw and roll joined about the axis the player turns it around, told apart by gravity: turning the pad left and right, held
	flat, upright or between, turns the view */
	WIIU_GYRO_SPACE_PLAYER,
	/* the pad's own yaw axis only (turning it as if on a table) */
	WIIU_GYRO_SPACE_TURN,
	/* the pad's own roll axis only (leaning it like a steering wheel) */
	WIIU_GYRO_SPACE_LEAN,
};

void wiiu_input_set_gyro_space(int space);

/* Reads the pad of a port (0: the GamePad, with the controllers that share its player; 1 to 3: the controllers the layout gives the port, as the last
wiiu_input_poll arranged them). Returns 1 if there is such a pad, with its state; 0 if not. */
int wiiu_input_read(unsigned int port, struct wiiu_pad *pad);

/* A clock in milliseconds that counts from anywhere (the system's, as OSGetSystemTime has it), for the virtual controllers' script (wiiu_virtual_pads.h). */
unsigned long wiiu_input_milliseconds(void);

/* The GamePad's touch screen, in pixels of the GamePad's picture (854x480), as read by the latest wiiu_input_read of port 0 (or held by the debug server's tap).
Returns 1 while a finger is on it. */
int wiiu_input_touch(float *x, float *y);

/* Rumbles the pad of a port, 0 (still) to 1 (full): the GamePad's motor for port 0 and those of the Pro Controllers and Wii Remotes that drive the port (a Remote not while its MotionPlus turns the view). Does nothing in the
background. */
void wiiu_input_rumble(unsigned int port, float strength);

/* Voice chat's push to talk (platform_wiiu.c's halo_push_to_talk_held, from the game thread): whether the GamePad's TV button is held, as the latest
wiiu_input_read of port 0 found it. Every other button is the game's. Asking takes the button from the system's TV menu (VPADSetTVMenuInvalid) for as
long as it is asked for; a second after the last asking, at the next read of the GamePad, the menu has it back. */
int wiiu_input_push_to_talk(void);

/* The microphone probe's (wiiu_voice_audio.c, from any thread): the TV button taken from the TV menu while take is 1, and its presses logged. */
void wiiu_input_take_tv_button(int take);

/* The GamePad's jack, as the latest read of the GamePad found it (VPADStatus.usingHeadphones): 1 with a headset in it (voice chat then hears the
headset's microphone), 0 without, -1 before the GamePad has been read; and the microphone's status byte (VPADStatus.micStatus). */
int wiiu_input_headset(void);
unsigned int wiiu_input_mic_status(void);

#endif
