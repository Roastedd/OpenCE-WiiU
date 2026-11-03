/*
The scripted player of debug.test_input (port/linux/include/test_input_bot.h, shared by the desktop's xinput_sdl.c and the console's xinput_wiiu.c), apart from the game.

    test_input_bot parse SETTING                  yes SEED MODE | no (MODE: 0 the bot, 1 looking, 2 the hunter)
    test_input_bot table SEED MODE TICKS          a line a tick of the game's time: the sticks and the three buttons the pattern sets at tick N / 30 seconds
    test_input_bot hunt SEED DISTANCE YAW PITCH   the hunter's sticks and buttons with an enemy there, at ticks 0 to 899 (a line each, as table's, and the right stick's Y)
*/

#include <stdio.h>
#include <stdlib.h>

typedef short SHORT;
typedef unsigned short WORD;

typedef struct
{
	WORD wButtons;
	unsigned char bAnalogButtons[8];
	SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY;
} XINPUT_GAMEPAD;

/* (port/include/xdk/xdk_xbox.h's values; tools/test_wiiu_scripted_input.py checks them) */
#define XINPUT_GAMEPAD_A 0
#define XINPUT_GAMEPAD_LEFT_TRIGGER 6
#define XINPUT_GAMEPAD_RIGHT_TRIGGER 7

#include "../../linux/include/test_input_bot.h"

int main(int argc, char **argv)
{
	if (argc == 3 && !strcmp(argv[1], "parse"))
	{
		int seed = -1, mode = 0;

		if (test_input_bot_parse(argv[2], &seed, &mode))
			printf("yes %d %d\n", seed, mode);
		else
			printf("no %d %d\n", seed, mode);
		return 0;
	}
	if (argc == 5 && !strcmp(argv[1], "table"))
	{
		int seed = atoi(argv[2]), mode = atoi(argv[3]), ticks = atoi(argv[4]), tick;

		for (tick = 0; tick < ticks; tick++)
		{
			XINPUT_GAMEPAD pad;

			memset(&pad, 0, sizeof(pad));
			test_input_bot_pad(&pad, (double)tick / 30.0, seed, mode, NULL);
			printf("%d %d %d %d %d %d %d\n", tick, pad.sThumbLX, pad.sThumbLY, pad.sThumbRX, pad.bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER],
				pad.bAnalogButtons[XINPUT_GAMEPAD_A], pad.bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER]);
		}
		return 0;
	}
	if (argc == 6 && !strcmp(argv[1], "hunt"))
	{
		struct test_input_target target;
		int seed = atoi(argv[2]), tick;

		target.valid = 1;
		target.distance = atof(argv[3]);
		target.yaw_error = atof(argv[4]);
		target.pitch_error = atof(argv[5]);
		for (tick = 0; tick < 900; tick++)
		{
			XINPUT_GAMEPAD pad;

			memset(&pad, 0, sizeof(pad));
			test_input_bot_pad(&pad, (double)tick / 30.0, seed, TEST_INPUT_HUNT, &target);
			printf("%d %d %d %d %d %d %d %d\n", tick, pad.sThumbLX, pad.sThumbLY, pad.sThumbRX, pad.bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER],
				pad.bAnalogButtons[XINPUT_GAMEPAD_A], pad.bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER], pad.sThumbRY);
		}
		return 0;
	}
	fprintf(stderr, "usage: test_input_bot parse SETTING | table SEED MODE TICKS | hunt SEED DISTANCE YAW PITCH\n");
	return 2;
}
