/*
WIIU_CHAT_INPUT.H

The Wii U's controllers as the text chat's on-screen keyboard reads them (halo_keyboard.h: halo_chat_pad_buttons, halo_chat_taps), as plain code with no system calls, so
that the harness builds it alone (tools/harness/tests/wiiu_chat_input.c). xinput_wiiu.c records each port's buttons with it once a frame and platform_wiiu.c answers the
chat with them.

The buttons are the Wii U's letters, not their places: the chat's keyboard is a menu, where the Wii U's A (the right-hand button) types, B (the bottom one) closes, X deletes
and Y gives capitals, as in every menu of the console (input.wiiu_face_buttons does not move them). A Wii Remote's Xbox face buttons count as the letters of the same name, as
the updater's panel takes them. The left stick counts as a direction past WIIU_CHAT_STICK_PUSH of its range. L and R are the quick chat's page buttons.

A touch of the GamePad is a tap at the moment the finger comes down (the key is pressed as it is touched, as a keyboard's are), at the place it came down, and only a tap of
the last frames: one that came while no keyboard was asking is gone before one opens, so a finger resting on the screen as it opens presses nothing.
*/

#ifndef HALO_WIIU_CHAT_INPUT_H
#define HALO_WIIU_CHAT_INPUT_H

#include "wiiu_input.h"
#include "halo_keyboard.h"

/* the share of the left stick's range that is a direction pressed (22,000 of 32,768 on the desktop) */
#define WIIU_CHAT_STICK_PUSH 0.7f

/* the GamePad's picture, in pixels: the touch's own units (wiiu_input_touch) */
#define WIIU_CHAT_TOUCH_WIDTH 854.0f
#define WIIU_CHAT_TOUCH_HEIGHT 480.0f

/* a tap is asked for within this many polls of the frame it came in (the frame it came in, and the next) */
#define WIIU_CHAT_TAP_POLLS 2

/* a pad's buttons as the halo_chat_pad_button bits (1 << HALO_CHAT_PAD_*): L and R turn the quick chat's pages */
static inline unsigned long wiiu_chat_buttons(const struct wiiu_pad *pad)
{
	unsigned long buttons = 0;
	unsigned int held = pad->buttons;

	if (pad->xbox_faces & WIIU_XBOX_A)
		held |= WIIU_PAD_A;
	if (pad->xbox_faces & WIIU_XBOX_B)
		held |= WIIU_PAD_B;
	if (pad->xbox_faces & WIIU_XBOX_X)
		held |= WIIU_PAD_X;
	if (pad->xbox_faces & WIIU_XBOX_Y)
		held |= WIIU_PAD_Y;
	if (held & WIIU_PAD_UP)
		buttons |= 1UL << HALO_CHAT_PAD_UP;
	if (held & WIIU_PAD_DOWN)
		buttons |= 1UL << HALO_CHAT_PAD_DOWN;
	if (held & WIIU_PAD_LEFT)
		buttons |= 1UL << HALO_CHAT_PAD_LEFT;
	if (held & WIIU_PAD_RIGHT)
		buttons |= 1UL << HALO_CHAT_PAD_RIGHT;
	if (held & WIIU_PAD_A)
		buttons |= 1UL << HALO_CHAT_PAD_A;
	if (held & WIIU_PAD_B)
		buttons |= 1UL << HALO_CHAT_PAD_B;
	if (held & WIIU_PAD_X)
		buttons |= 1UL << HALO_CHAT_PAD_X;
	if (held & WIIU_PAD_Y)
		buttons |= 1UL << HALO_CHAT_PAD_Y;
	if (held & WIIU_PAD_PLUS)
		buttons |= 1UL << HALO_CHAT_PAD_START;
	if (held & WIIU_PAD_L)
		buttons |= 1UL << HALO_CHAT_PAD_PAGE_PREVIOUS;
	if (held & WIIU_PAD_R)
		buttons |= 1UL << HALO_CHAT_PAD_PAGE_NEXT;
	if (pad->left_y > WIIU_CHAT_STICK_PUSH)
		buttons |= 1UL << HALO_CHAT_PAD_STICK_UP;
	if (pad->left_y < -WIIU_CHAT_STICK_PUSH)
		buttons |= 1UL << HALO_CHAT_PAD_STICK_DOWN;
	if (pad->left_x < -WIIU_CHAT_STICK_PUSH)
		buttons |= 1UL << HALO_CHAT_PAD_STICK_LEFT;
	if (pad->left_x > WIIU_CHAT_STICK_PUSH)
		buttons |= 1UL << HALO_CHAT_PAD_STICK_RIGHT;
	return buttons;
}

/* the GamePad's finger, polled once a frame */
struct wiiu_chat_tap
{
	int was_down;
	/* a tap not yet taken, its age in polls, and where it came down (the touch's pixels) */
	int pending;
	int age;
	float x, y;
};

/* once a frame with the finger as wiiu_input_touch has it (down, x, y in the GamePad's pixels) */
static inline void wiiu_chat_tap_poll(struct wiiu_chat_tap *tap, int down, float x, float y)
{
	if (tap->pending && ++tap->age >= WIIU_CHAT_TAP_POLLS)
		tap->pending = 0;
	if (down && !tap->was_down)
	{
		tap->pending = 1;
		tap->age = 0;
		tap->x = x;
		tap->y = y;
	}
	tap->was_down = down != 0;
}

/* the tap, if one is waiting (taken once): where it came down in the GamePad's pixels; 1 if there was one */
static inline int wiiu_chat_tap_take_pixels(struct wiiu_chat_tap *tap, float *x, float *y)
{
	if (!tap->pending)
		return 0;
	tap->pending = 0;
	*x = tap->x;
	*y = tap->y;
	return 1;
}

/* ... as a share of the picture across and down, 0 to 1 (halo_chat_taps' units: the window's) */
static inline int wiiu_chat_tap_take(struct wiiu_chat_tap *tap, float *x, float *y)
{
	float pixel_x, pixel_y;

	if (!wiiu_chat_tap_take_pixels(tap, &pixel_x, &pixel_y))
		return 0;
	*x = pixel_x / WIIU_CHAT_TOUCH_WIDTH;
	*y = pixel_y / WIIU_CHAT_TOUCH_HEIGHT;
	if (*x < 0.0f)
		*x = 0.0f;
	if (*x > 0.9999f)
		*x = 0.9999f;
	if (*y < 0.0f)
		*y = 0.0f;
	if (*y > 0.9999f)
		*y = 0.9999f;
	return 1;
}

#endif
