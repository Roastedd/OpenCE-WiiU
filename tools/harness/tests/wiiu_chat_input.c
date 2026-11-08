#include "harness.h"

/* The Wii U's controllers as the text chat's on-screen keyboard reads them (port/wiiu/src/wiiu_chat_input.h): every button's bit, the Wii U's letters and not their places,
a Wii Remote's Xbox letters, the stick's push, and the GamePad's finger as a tap (once, where it came down, as a share of the picture, only while fresh). */
#include "wiiu_chat_input.h"

#define BIT(name) (1UL << (name))

static unsigned long map(unsigned int buttons, unsigned int xbox_faces, float left_x, float left_y)
{
	struct wiiu_pad pad;

	memset(&pad, 0, sizeof(pad));
	pad.buttons = buttons;
	pad.xbox_faces = xbox_faces;
	pad.left_x = left_x;
	pad.left_y = left_y;
	return wiiu_chat_buttons(&pad);
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	/* each of the thirteen chat buttons from the one pad button that is it, and no other bit with it */
	CASE("buttons")
	{
		static const struct { unsigned int source; int chat; } table[] = {
			{ WIIU_PAD_UP, HALO_CHAT_PAD_UP }, { WIIU_PAD_DOWN, HALO_CHAT_PAD_DOWN }, { WIIU_PAD_LEFT, HALO_CHAT_PAD_LEFT }, { WIIU_PAD_RIGHT, HALO_CHAT_PAD_RIGHT },
			{ WIIU_PAD_A, HALO_CHAT_PAD_A }, { WIIU_PAD_B, HALO_CHAT_PAD_B }, { WIIU_PAD_X, HALO_CHAT_PAD_X }, { WIIU_PAD_Y, HALO_CHAT_PAD_Y },
			{ WIIU_PAD_PLUS, HALO_CHAT_PAD_START }, { WIIU_PAD_L, HALO_CHAT_PAD_PAGE_PREVIOUS }, { WIIU_PAD_R, HALO_CHAT_PAD_PAGE_NEXT },
		};
		unsigned long all = 0;
		unsigned i;

		for (i = 0; i < sizeof(table) / sizeof(table[0]); i++)
		{
			CHECK(map(table[i].source, 0, 0.0f, 0.0f) == BIT(table[i].chat), "button %u gives %lx", table[i].source, map(table[i].source, 0, 0.0f, 0.0f));
			all |= BIT(table[i].chat);
		}
		CHECK(all == (0x1FFUL | BIT(HALO_CHAT_PAD_PAGE_PREVIOUS) | BIT(HALO_CHAT_PAD_PAGE_NEXT)), "eleven buttons");
		/* four more are the left stick's, and 15 in all */
		CHECK(NUMBER_OF_HALO_CHAT_PAD_BUTTONS == 15, "fifteen bits");
		CHECK(map(0, 0, 0.0f, 1.0f) == BIT(HALO_CHAT_PAD_STICK_UP) && map(0, 0, 0.0f, -1.0f) == BIT(HALO_CHAT_PAD_STICK_DOWN) &&
			map(0, 0, -1.0f, 0.0f) == BIT(HALO_CHAT_PAD_STICK_LEFT) && map(0, 0, 1.0f, 0.0f) == BIT(HALO_CHAT_PAD_STICK_RIGHT), "stick directions");
		/* all together */
		CHECK(map(WIIU_PAD_UP | WIIU_PAD_DOWN | WIIU_PAD_LEFT | WIIU_PAD_RIGHT | WIIU_PAD_A | WIIU_PAD_B | WIIU_PAD_X | WIIU_PAD_Y | WIIU_PAD_PLUS | WIIU_PAD_L | WIIU_PAD_R, 0, 1.0f, 1.0f) == (0x1FFUL | BIT(HALO_CHAT_PAD_PAGE_PREVIOUS) | BIT(HALO_CHAT_PAD_PAGE_NEXT) | BIT(HALO_CHAT_PAD_STICK_UP) | BIT(HALO_CHAT_PAD_STICK_RIGHT)),
			"all %lx", map(WIIU_PAD_UP | WIIU_PAD_DOWN | WIIU_PAD_LEFT | WIIU_PAD_RIGHT | WIIU_PAD_A | WIIU_PAD_B | WIIU_PAD_X | WIIU_PAD_Y | WIIU_PAD_PLUS | WIIU_PAD_L | WIIU_PAD_R, 0, 1.0f, 1.0f));
		CHECK(map(0, 0, 0.0f, 0.0f) == 0, "nothing");
	}

	/* the buttons the chat does not read give nothing: the triggers, -, the stick clicks and the TV button (L and R turn the quick chat's pages) */
	CASE("others")
	{
		CHECK(map(WIIU_PAD_ZL | WIIU_PAD_ZR | WIIU_PAD_MINUS | WIIU_PAD_STICK_L | WIIU_PAD_STICK_R | WIIU_PAD_TV, 0, 0.0f, 0.0f) == 0, "others");
	}

	/* the Wii U's letters, not the places: A is HALO_CHAT_PAD_A whatever input.wiiu_face_buttons says (the mapping takes no setting), and a Wii Remote's Xbox letters
	are the letters of the same name */
	CASE("letters")
	{
		CHECK(map(WIIU_PAD_A, 0, 0.0f, 0.0f) == BIT(HALO_CHAT_PAD_A), "A is A");
		CHECK(map(WIIU_PAD_B, 0, 0.0f, 0.0f) == BIT(HALO_CHAT_PAD_B), "B is B");
		CHECK(map(0, WIIU_XBOX_A, 0.0f, 0.0f) == BIT(HALO_CHAT_PAD_A), "Xbox A");
		CHECK(map(0, WIIU_XBOX_B, 0.0f, 0.0f) == BIT(HALO_CHAT_PAD_B), "Xbox B");
		CHECK(map(0, WIIU_XBOX_X, 0.0f, 0.0f) == BIT(HALO_CHAT_PAD_X), "Xbox X");
		CHECK(map(0, WIIU_XBOX_Y, 0.0f, 0.0f) == BIT(HALO_CHAT_PAD_Y), "Xbox Y");
		CHECK(map(WIIU_PAD_A, WIIU_XBOX_A, 0.0f, 0.0f) == BIT(HALO_CHAT_PAD_A), "both");
	}

	/* the stick is a direction past 70% of its range, strictly, on each axis and both together; the right stick is not read */
	CASE("stick")
	{
		struct wiiu_pad pad;

		CHECK(map(0, 0, 0.7f, 0.0f) == 0 && map(0, 0, 0.0f, 0.7f) == 0 && map(0, 0, -0.7f, 0.0f) == 0, "at the threshold");
		CHECK(map(0, 0, 0.71f, 0.0f) == BIT(HALO_CHAT_PAD_STICK_RIGHT), "just past");
		CHECK(map(0, 0, 0.69f, 0.69f) == 0, "diagonal short of it");
		CHECK(map(0, 0, -0.9f, 0.9f) == (BIT(HALO_CHAT_PAD_STICK_LEFT) | BIT(HALO_CHAT_PAD_STICK_UP)), "diagonal past it");
		memset(&pad, 0, sizeof(pad));
		pad.right_x = 1.0f;
		pad.right_y = -1.0f;
		pad.look_x = 1.0f;
		CHECK(wiiu_chat_buttons(&pad) == 0, "the right stick");
	}

	/* a finger is a tap as it comes down, once, where it came down */
	CASE("tap-edge")
	{
		struct wiiu_chat_tap tap;
		float x, y;

		memset(&tap, 0, sizeof(tap));
		wiiu_chat_tap_poll(&tap, 0, 0.0f, 0.0f);
		CHECK(!wiiu_chat_tap_take_pixels(&tap, &x, &y), "no finger");
		wiiu_chat_tap_poll(&tap, 1, 100.0f, 200.0f);
		CHECK(wiiu_chat_tap_take_pixels(&tap, &x, &y) && x == 100.0f && y == 200.0f, "down");
		CHECK(!wiiu_chat_tap_take_pixels(&tap, &x, &y), "taken once");
		/* held, and moved: not another */
		wiiu_chat_tap_poll(&tap, 1, 300.0f, 100.0f);
		wiiu_chat_tap_poll(&tap, 1, 310.0f, 100.0f);
		CHECK(!wiiu_chat_tap_take_pixels(&tap, &x, &y), "held");
		wiiu_chat_tap_poll(&tap, 0, 310.0f, 100.0f);
		CHECK(!wiiu_chat_tap_take_pixels(&tap, &x, &y), "up");
		wiiu_chat_tap_poll(&tap, 1, 5.0f, 6.0f);
		CHECK(wiiu_chat_tap_take_pixels(&tap, &x, &y) && x == 5.0f && y == 6.0f, "down again");
		/* two touches in a frame's gap: the second one's place */
		wiiu_chat_tap_poll(&tap, 0, 0.0f, 0.0f);
		wiiu_chat_tap_poll(&tap, 1, 7.0f, 8.0f);
		wiiu_chat_tap_poll(&tap, 0, 0.0f, 0.0f);
		wiiu_chat_tap_poll(&tap, 1, 9.0f, 10.0f);
		CHECK(wiiu_chat_tap_take_pixels(&tap, &x, &y) && x == 9.0f && y == 10.0f, "latest");
	}

	/* a tap is fresh for the frame it came in and the next, no longer: one made while no keyboard asked is gone before one opens, and a finger resting on the screen as it
	opens presses nothing */
	CASE("tap-fresh")
	{
		struct wiiu_chat_tap tap;
		float x, y;
		int polls;

		memset(&tap, 0, sizeof(tap));
		wiiu_chat_tap_poll(&tap, 1, 10.0f, 10.0f);
		wiiu_chat_tap_poll(&tap, 1, 10.0f, 10.0f);
		CHECK(wiiu_chat_tap_take_pixels(&tap, &x, &y), "the next frame");
		wiiu_chat_tap_poll(&tap, 0, 0.0f, 0.0f);
		wiiu_chat_tap_poll(&tap, 1, 20.0f, 20.0f);
		wiiu_chat_tap_poll(&tap, 1, 20.0f, 20.0f);
		wiiu_chat_tap_poll(&tap, 1, 20.0f, 20.0f);
		CHECK(!wiiu_chat_tap_take_pixels(&tap, &x, &y), "two frames on");
		memset(&tap, 0, sizeof(tap));
		wiiu_chat_tap_poll(&tap, 1, 1.0f, 1.0f);
		for (polls = 0; polls < 100; polls++)
			wiiu_chat_tap_poll(&tap, 1, 1.0f, 1.0f);
		CHECK(!wiiu_chat_tap_take_pixels(&tap, &x, &y), "resting");
	}

	/* a tap as a share of the picture, 0 to 1, never 1 */
	CASE("tap-scale")
	{
		struct wiiu_chat_tap tap;
		float x, y;

		memset(&tap, 0, sizeof(tap));
		wiiu_chat_tap_poll(&tap, 1, 427.0f, 240.0f);
		CHECK(wiiu_chat_tap_take(&tap, &x, &y) && x == 0.5f && y == 0.5f, "middle %f %f", x, y);
		wiiu_chat_tap_poll(&tap, 0, 0.0f, 0.0f);
		wiiu_chat_tap_poll(&tap, 1, 0.0f, 0.0f);
		CHECK(wiiu_chat_tap_take(&tap, &x, &y) && x == 0.0f && y == 0.0f, "corner");
		wiiu_chat_tap_poll(&tap, 0, 0.0f, 0.0f);
		wiiu_chat_tap_poll(&tap, 1, 854.0f, 480.0f);
		CHECK(wiiu_chat_tap_take(&tap, &x, &y) && x < 1.0f && y < 1.0f && x > 0.99f && y > 0.99f, "far corner %f %f", x, y);
		wiiu_chat_tap_poll(&tap, 0, 0.0f, 0.0f);
		wiiu_chat_tap_poll(&tap, 1, -5.0f, 900.0f);
		CHECK(wiiu_chat_tap_take(&tap, &x, &y) && x == 0.0f && y < 1.0f, "outside %f %f", x, y);
		CHECK(!wiiu_chat_tap_take(&tap, &x, &y), "none left");
	}
	return 0;
}
