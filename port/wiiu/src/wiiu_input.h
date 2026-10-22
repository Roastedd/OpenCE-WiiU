/*
WIIU_INPUT.H

The Wii U's controllers as plain numbers, for xinput_wiiu.c (built with the Xbox's ABI) to turn into XInput: wiiu_input.c (built with the console's) reads
the GamePad through VPAD and Pro Controllers through KPAD.
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
};

struct wiiu_pad
{
	unsigned int buttons;
	/* -1 to 1, up and right positive */
	float left_x, left_y, right_x, right_y;
};

#define WIIU_PAD_PORTS 4

/* Reads the pad of a port (0: the GamePad, with the Pro Controller of channel 0 if one is there; 1 to 3: the Pro Controllers of channels 1 to 3). Returns 1
if there is such a pad, with its state; 0 if not. */
int wiiu_input_read(unsigned int port, struct wiiu_pad *pad);

/* The GamePad's touch screen, in pixels of the GamePad's picture (854x480), as read by the latest wiiu_input_read of port 0 (or held by the debug server's tap).
Returns 1 while a finger is on it. */
int wiiu_input_touch(float *x, float *y);

/* Rumbles the pad of a port, 0 (still) to 1 (full): the GamePad's motor for port 0 and a Pro Controller's for its channel. Does nothing in the background. */
void wiiu_input_rumble(unsigned int port, float strength);

#endif
