/*
TEST_INPUT_BOT.H

The scripted player of debug.test_input "bot:<seed>", "tickbot:<seed>" and "look:<seed>", shared by the desktop build's xinput_sdl.c and the Wii U's
xinput_wiiu.c, so that the two play the same pattern: a run on the console and a run on the host are the same fight to compare (reports/research/
2026-11-01/wiiu-scripted-input.md).

The pattern is a function of the seed and of the player's clock in seconds, nothing else: the game's time while a game runs (the host's
network_test_game_seconds, so ticks, and the same at any frame rate), so the callers pass the clock in. The hunter ("tickhunt:<seed>") also takes where the nearest
enemy is from the game (network_test_hunt_target), which is a function of the game's state, so it is as much the same fight as the game's state is. Include it after
platform.h (XINPUT_GAMEPAD).
*/
#ifndef TEST_INPUT_BOT_H
#define TEST_INPUT_BOT_H

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* the scripted players: the pattern of "bot:<seed>" and "tickbot:<seed>" (walking, turning, firing in turns), "look:<seed>" (standing still, only looking) and
"tickhunt:<seed>" ("hunt:<seed>"; the pattern's walking, strafing, jumping and grenades, but turning to the nearest living enemy, walking up to it and firing at it) */
enum
{
	TEST_INPUT_BOT = 0,
	TEST_INPUT_LOOK,
	TEST_INPUT_HUNT,
};

/* where the nearest living enemy is, from the first player (port/linux/game/network_test.c's network_test_hunt_target): the distance in world units,
the turn to face it in radians (positive: to the left) and the tilt (positive: up) */
struct test_input_target
{
	int valid;
	double distance, yaw_error, pitch_error;
};

/* whether setting is a scripted player's: "bot:<seed>", "tickbot:<seed>", "bot" (seed 0), "look:<seed>" or "tickhunt:<seed>". The seed and the kind (TEST_INPUT_) are
set; both are left alone when it is not. */
static inline int test_input_bot_parse(const char *setting, int *seed, int *mode)
{
	if (!strncmp(setting, "bot:", 4))
		*seed = atoi(setting + 4), *mode = TEST_INPUT_BOT;
	else if (!strncmp(setting, "tickbot:", 8))
		*seed = atoi(setting + 8), *mode = TEST_INPUT_BOT;
	else if (!strcmp(setting, "bot"))
		*seed = 0, *mode = TEST_INPUT_BOT;
	else if (!strncmp(setting, "look:", 5))
		*seed = atoi(setting + 5), *mode = TEST_INPUT_LOOK;
	else if (!strncmp(setting, "tickhunt:", 9))
		*seed = atoi(setting + 9), *mode = TEST_INPUT_HUNT;
	else if (!strncmp(setting, "hunt:", 5))
		*seed = atoi(setting + 5), *mode = TEST_INPUT_HUNT;
	else
		return 0;
	return 1;
}

static inline SHORT test_input_stick(double value)
{
	return (SHORT)(value > 1.0 ? 32000.0 : value < -1.0 ? -32000.0 : value * 32000.0);
}

/* the scripted player's sticks and buttons at a time: it walks and strafes in circles, turns, fires every few seconds, jumps now and then and throws a grenade
every seven seconds (or, looking, stands still and turns and looks up and down; or, hunting with an enemy in target, turns to it, walks up to it and fires
while it faces it, the strafing, jumps and grenades as the pattern's). They are set into pad, over the controller's. */
static inline void test_input_bot_pad(XINPUT_GAMEPAD *pad, double seconds, int seed, int mode, const struct test_input_target *target)
{
	double t = seconds + seed * 1.7;

	if (mode == TEST_INPUT_LOOK)
	{
		pad->sThumbRX = (SHORT)(sin(t * 0.5) * 14000.0);
		pad->sThumbRY = (SHORT)(sin(t * 0.3) * 32000.0);
		return;
	}
	if (mode == TEST_INPUT_HUNT && target && target->valid)
	{
		/* (a stick to the right turns right, and up looks up; the enemy to the left has a positive turn) */
		pad->sThumbRX = test_input_stick(-target->yaw_error * 2.5);
		pad->sThumbRY = test_input_stick(target->pitch_error * 2.5);
		pad->sThumbLY = target->distance > 8.0 ? 32000 : 0;
		pad->sThumbLX = (SHORT)(cos(t * 0.6 + seed) * 20000.0);
		if (target->distance < 60.0 && fabs(target->yaw_error) < 0.2)
			pad->bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] = 255;
		if (fmod(t, 5.0) < 0.1)
			pad->bAnalogButtons[XINPUT_GAMEPAD_A] = 255;
		if (fmod(t, 7.0) < 0.2)
			pad->bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER] = 255;
		return;
	}
	pad->sThumbLY = (SHORT)(sin(t * 0.9) * 32000.0);
	pad->sThumbLX = (SHORT)(cos(t * 0.6 + seed) * 20000.0);
	pad->sThumbRX = (SHORT)(sin(t * 0.4) * 14000.0);
	if (fmod(t, 3.0) < 0.3)
		pad->bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] = 255;
	if (fmod(t, 5.0) < 0.1)
		pad->bAnalogButtons[XINPUT_GAMEPAD_A] = 255;
	if (fmod(t, 7.0) < 0.2)
		pad->bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER] = 255;
}

#endif
