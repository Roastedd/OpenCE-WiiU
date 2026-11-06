/*
WIIU_WIMOTE.H

The Wii Remote as a controller, as plain C with no system call that the desktop tests (tools/harness/tests/test_wiiu_wimote.py): the buttons of a Remote and its
Nunchuk matched to the Xbox pad's by a layout (input.wiiu_wiimote_layout), the Nunchuk's stick, the IR pointer as a turn rate for the view (a free-aim box in the
middle, outside it the view turns faster the further out, as Metroid Prime 3's advanced controls do), the MotionPlus's turn rates as the view's mouse (the same
player-space maths as the GamePad's gyro), which of the two looks a Remote has (input.wiiu_wiimote_look), and what the KPAD library needs to be asked for the
MotionPlus. wiiu_input.c reads KPAD and calls these with the values.

The axes of a Remote, as KPAD gives them: x to the right, y along its length to the front (the way it points), z up out of its buttons; the accelerometer in g
(a Remote lying on its back reads z = +1); the MotionPlus's turn rates in degrees a second, right-handed about those axes (turning it left, seen from above,
is positive about z; lifting its nose is positive about x). None of the signs or the units has been measured on a console: wiiu_input.c logs the rates, and
input.wiiu_wiimote_motion_invert_x turns the view the other way round.
*/

#ifndef HALO_WIIU_WIMOTE_H
#define HALO_WIIU_WIMOTE_H

#include "wiiu_input.h"

/* the Remote's buttons in KPADStatus.hold (and the Nunchuk's two in KPADStatus.nunchuk.hold): wut's WPAD_BUTTON_* numbers, checked against them by wiiu_input.c */
enum
{
	WIIMOTE_BUTTON_LEFT = 0x0001,
	WIIMOTE_BUTTON_RIGHT = 0x0002,
	WIIMOTE_BUTTON_DOWN = 0x0004,
	WIIMOTE_BUTTON_UP = 0x0008,
	WIIMOTE_BUTTON_PLUS = 0x0010,
	WIIMOTE_BUTTON_2 = 0x0100,
	WIIMOTE_BUTTON_1 = 0x0200,
	WIIMOTE_BUTTON_B = 0x0400,
	WIIMOTE_BUTTON_A = 0x0800,
	WIIMOTE_BUTTON_MINUS = 0x1000,
	WIIMOTE_BUTTON_Z = 0x2000,
	WIIMOTE_BUTTON_C = 0x4000,
	WIIMOTE_BUTTON_HOME = 0x8000,
};

/* KPADStatus.extensionType: wut's WPAD_EXT_* numbers */
enum
{
	WIIMOTE_EXT_CORE = 0x00,
	WIIMOTE_EXT_NUNCHUK = 0x01,
	WIIMOTE_EXT_CLASSIC = 0x02,
	WIIMOTE_EXT_MPLUS = 0x05,
	WIIMOTE_EXT_MPLUS_NUNCHUK = 0x06,
	WIIMOTE_EXT_MPLUS_CLASSIC = 0x07,
	WIIMOTE_EXT_PRO_CONTROLLER = 0x1f,
};

/* KPADEnableMpls's modes: wut's WPAD_MPLS_MODE_* numbers (0: none wanted) */
enum
{
	WIIMOTE_MPLS_NONE = 0,
	WIIMOTE_MPLS_ONLY = 4,
	WIIMOTE_MPLS_NUNCHUK = 5,
};

/* the layouts (input.wiiu_wiimote_layout names the first two; a Remote with no Nunchuk always has the menu's) */
enum
{
	/* B fires, Nunchuk Z throws grenades */
	WIIMOTE_LAYOUT_DEFAULT,
	/* Nunchuk Z fires, B throws grenades */
	WIIMOTE_LAYOUT_Z_FIRE,
	/* d-pad, A, B, 1, 2, + and -: the menus' */
	WIIMOTE_LAYOUT_MENU,
};

/* input.wiiu_wiimote_look */
enum
{
	WIIMOTE_LOOK_AUTO,
	WIIMOTE_LOOK_POINTER,
	WIIMOTE_LOOK_MOTION,
	WIIMOTE_LOOK_OFF,
};

/* the look a Remote has now (wimote_look_source) */
enum
{
	WIIMOTE_SOURCE_NONE,
	WIIMOTE_SOURCE_POINTER,
	WIIMOTE_SOURCE_MOTION,
};

/* ---------- the settings' words */

/* "default" or "z_fire" (an unknown word is "default"); and the words back */
int wimote_parse_layout(const char *setting);
const char *wimote_layout_name(int layout);
/* "auto", "pointer", "motion" or "off" (an unknown word is "auto") */
int wimote_parse_look(const char *setting);
const char *wimote_look_name(int look);
const char *wimote_source_name(int source);

/* ---------- the extension */

/* Whether the game reads a controller with this KPAD extension type as a Wii Remote: a Remote with no extension, with a Nunchuk, or with the MotionPlus (alone,
or with a Nunchuk). A Remote with a Classic Controller or a Pro Controller is read as those. */
int wimote_reads(unsigned int extension);
int wimote_has_nunchuk(unsigned int extension);
/* the MotionPlus is on (its extension types) */
int wimote_has_motion(unsigned int extension);
/* The MotionPlus mode to ask KPADEnableMpls for with this extension (WIIMOTE_MPLS_*): with the Nunchuk, the Nunchuk's passthrough mode; without, the
MotionPlus alone; 0 for the extensions that are not a Remote read here. */
unsigned int wimote_mpls_mode_wanted(unsigned int extension);
const char *wimote_extension_name(unsigned int extension);

/* ---------- the buttons and the Nunchuk's stick */

/* KPADStatus.hold with the Nunchuk's Z and C: nunchuk.hold also carries the stick's emulated d-pad presses in the d-pad's own bits, which are not taken. */
unsigned int wimote_hold(unsigned int hold, unsigned int nunchuk_hold);

/* The layout a Remote with this extension has, of the one asked for: the menu's with no Nunchuk. */
int wimote_layout_for(unsigned int extension, int layout);

/* The Nunchuk's stick as a stick of the pad: a dead centre, and its reach (a Nunchuk's stick does not go to the corners) taken as the stick's full throw, in the
direction it points. */
void wimote_stick(float x, float y, float *out_x, float *out_y);

/* The pad a Remote makes of its buttons: hold from wimote_hold, the Nunchuk's stick (ignored by the menu's layout). The buttons and the Xbox's face buttons
are added to the pad's (a pad is cleared first); its left stick is set. */
void wimote_map(int layout, unsigned int hold, float stick_x, float stick_y, struct wiiu_pad *pad);

/* ---------- the pointer */

/* The pointer (KPADStatus.pos: -1 to 1, right and down positive, from the middle of the sensor bar) as the view's turn rate: look_x right positive, look_y up
positive, as a stick, lifted past the game's dead range. Inside the free-aim box (box: its half width as a share of the range, 0 to 0.9; a bad value is 0.3) it is 0,
the view does not turn; past its edge the rate grows with the distance (speed: 1 reaches the full stick at the screen's edge, 2 about two thirds of the way out; a bad value is 1),
up to the full stick. Nothing turns while valid is 0 (KPADStatus.posValid). */
void wimote_pointer_look(int valid, float x, float y, float box, float speed, float *look_x, float *look_y);

/* the rate of one axis, 0 to 1 and signed, before the lift (for the tests) */
float wimote_pointer_rate(float position, float box, float speed);

/* ---------- which look */

/* The look a Remote has: the setting (WIIMOTE_LOOK_*), whether its MotionPlus is on, whether it is the first player's (the view turns by one Remote's motion, the
first player's) and whether the pointer sees the sensor bar. "auto": the MotionPlus if there is one, else the pointer while it sees the bar, else none. */
int wimote_look_source(int setting, int has_motion, int first_player, int pointer_valid);

/* A Remote rumbles unless its motion turns the view (the motor would shake it). */
int wimote_rumble_allowed(int source);

/* ---------- the motion */

struct wimote_motion
{
	int gravity_known;
	/* the direction up in the Remote's axes (the accelerometer, smoothed) */
	float up[3];
	/* seconds held still, and whether that has been told */
	float still;
	int still_told;
};

void wimote_motion_reset(struct wimote_motion *motion);

/* One sample of the MotionPlus (rate: degrees a second about the Remote's x, y and z; acc: the accelerometer in g), seconds after the one before (at most a tenth of a
second is counted): how far the Remote turned, in rotations, as the view's mouse turns: turn, left positive, in the space (WIIU_GYRO_SPACE_*: player, the Remote's own
yaw, or its roll about its length), and tilt, its nose lifting negative (the sign of wiiu_input_gyro_take's tilt: the GamePad's top away). Rates under about 3 degrees a
second are scaled down towards nothing (the sensor's noise and a steady hand's tremor). invert_turn turns the turn round. */
void wimote_motion_step(struct wimote_motion *motion, int space, float seconds, const float acc[3], const float rate[3], int invert_turn, float *turn, float *tilt);

/* Whether the Remote has now been held still for long enough to ask the library to zero the MotionPlus (KPADResetMpls): 1 once for each time it is held still
(about a second and a half), and again only after it has moved. */
int wimote_motion_still(struct wimote_motion *motion, float seconds, const float rate[3]);

#endif
