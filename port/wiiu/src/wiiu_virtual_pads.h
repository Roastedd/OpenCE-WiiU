/*
WIIU_VIRTUAL_PADS.H

Virtual controllers for testing split screen without as many people (debug.virtual_gamepads and debug.virtual_gamepad_script, the desktop build's
settings of the same names, which xinput_sdl.c plays on SDL's virtual gamepads). Plain C with no system call, so that the desktop tests it
(tools/harness/tests/test_wiiu_virtual_pads.py): the script and the clock go in, each pad's buttons and sticks come out, and xinput_wiiu.c puts them on
the Xbox pads of the ports.

The pads are the game's players: pad n drives the port n - 1, so pad 1 is the GamePad's port (merged with whatever the GamePad and the controllers
that share it do), and pads 2 to 4 are the ports 1 to 3, present for as long as the setting says, whatever controllers are connected.
The script's entries, "<seconds> <pad> <what>" separated by semicolons: the seconds from when the network lobby first opens (start), the pad 1 to 4
(0 is the desktop's keyboard: the Wii U has none, and its entries do nothing), and what is one of a, b, x, y, start, back, up, down, left, right, lb,
rb, lt or rt (the Xbox's buttons, pressed for 150 ms and until the game has read it 8 times), "walk" (the sticks move from then on, each pad its own
way) or "still".
*/

#ifndef HALO_WIIU_VIRTUAL_PADS_H
#define HALO_WIIU_VIRTUAL_PADS_H

#define WIIU_VIRTUAL_PADS 4

/* the buttons of the Xbox pad (by name: what the game gets, whatever the face buttons' match is) */
enum
{
	WIIU_VPAD_A = 1 << 0,
	WIIU_VPAD_B = 1 << 1,
	WIIU_VPAD_X = 1 << 2,
	WIIU_VPAD_Y = 1 << 3,
	WIIU_VPAD_START = 1 << 4,
	WIIU_VPAD_BACK = 1 << 5,
	WIIU_VPAD_UP = 1 << 6,
	WIIU_VPAD_DOWN = 1 << 7,
	WIIU_VPAD_LEFT = 1 << 8,
	WIIU_VPAD_RIGHT = 1 << 9,
	/* the Xbox's white and black buttons, where later pads have shoulders */
	WIIU_VPAD_LB = 1 << 10,
	WIIU_VPAD_RB = 1 << 11,
	WIIU_VPAD_LT = 1 << 12,
	WIIU_VPAD_RT = 1 << 13,
};

struct wiiu_virtual_pad
{
	unsigned int buttons;
	/* the sticks as the Xbox pad's, -32768 to 32767 */
	short left_x, left_y, right_x, right_y;
};

/* Nothing pressed, no clock, no walking (a test's start). */
void wiiu_virtual_pads_reset(void);

/* The script's clock starts, the first time only and only with pads (count, as debug.virtual_gamepads says): the network lobby opening
(menu_functions.c, through platform_wiiu.c's halo_virtual_gamepads_start). milliseconds: any clock that counts them. */
void wiiu_virtual_pads_start(int count, unsigned long milliseconds);

/* Whether the clock has started (the first start with pads). */
int wiiu_virtual_pads_started(void);

/* The pads as the script has them at this time, once for each time the game reads a pad (a button the script presses stays down until it has been
read 8 times, whatever the frames' speed). count is debug.virtual_gamepads (0 to 4: less is 0, more is 4), script debug.virtual_gamepad_script. */
void wiiu_virtual_pads_update(int count, const char *script, unsigned long milliseconds);

/* The ports that have a pad of the script's besides the GamePad's own (a bit for each of ports 1 to 3): none with no pads. */
unsigned int wiiu_virtual_pads_ports(int count);

/* The pad of a port (0 to 3), as the latest update left it; all at rest for a port without one. */
void wiiu_virtual_pads_get(unsigned int port, struct wiiu_virtual_pad *pad);

#endif
