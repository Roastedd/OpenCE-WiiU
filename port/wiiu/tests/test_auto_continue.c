/* The decision of game.wiiu_auto_continue (port/wiiu/src/wiiu_auto_continue.c) apart from the console: when the game goes on with the last save, and the cases where it never does. */
#include <stdio.h>

#define WIIU_AUTO_CONTINUE_TEST 1
#include "../src/wiiu_auto_continue.h"

static int failures;
#define CHECK(condition) do { if (!(condition)) { printf("FAIL line %d: %s\n", __LINE__, #condition); failures++; } } while (0)

int main(void)
{
	struct wiiu_auto_continue_state s;
	unsigned i;
	int action = WIIU_AUTO_CONTINUE_WAIT, goes = 0;

	/* off: never */
	wiiu_auto_continue_state_init(&s, 0, 0);
	for (i = 0; i < 1000; i++)
		CHECK(wiiu_auto_continue_step(&s, 0, 1) == WIIU_AUTO_CONTINUE_WAIT);
	CHECK(s.reason == WIIU_AUTO_CONTINUE_OFF);

	/* a crash note: never */
	wiiu_auto_continue_state_init(&s, 1, 1);
	for (i = 0; i < 1000; i++)
		CHECK(wiiu_auto_continue_step(&s, 0, 1) == WIIU_AUTO_CONTINUE_WAIT);
	CHECK(s.reason == WIIU_AUTO_CONTINUE_CRASH_NOTE);

	/* on, nothing held: waits for the main menu, then for the settle polls, then goes once */
	wiiu_auto_continue_state_init(&s, 1, 0);
	for (i = 0; i < 500; i++)
		CHECK(wiiu_auto_continue_step(&s, 0, 0) == WIIU_AUTO_CONTINUE_WAIT);
	for (i = 0; i < WIIU_AUTO_CONTINUE_SETTLE_POLLS - 1; i++)
		CHECK(wiiu_auto_continue_step(&s, 0, 1) == WIIU_AUTO_CONTINUE_WAIT);
	CHECK(wiiu_auto_continue_step(&s, 0, 1) == WIIU_AUTO_CONTINUE_GO);
	for (i = 0; i < 1000; i++)
		if (wiiu_auto_continue_step(&s, 0, 1) != WIIU_AUTO_CONTINUE_WAIT)
			goes++;
	CHECK(goes == 0);

	/* a button held from the start (before the menu): cancelled for good, even if let go */
	wiiu_auto_continue_state_init(&s, 1, 0);
	CHECK(wiiu_auto_continue_step(&s, 0x2, 0) == WIIU_AUTO_CONTINUE_CANCELLED);
	for (i = 0; i < 1000; i++)
		CHECK(wiiu_auto_continue_step(&s, 0, 1) == WIIU_AUTO_CONTINUE_WAIT);
	CHECK(s.reason == WIIU_AUTO_CONTINUE_BUTTON);

	/* a press while the menu settles cancels too */
	wiiu_auto_continue_state_init(&s, 1, 0);
	for (i = 0; i < 40; i++)
		action = wiiu_auto_continue_step(&s, 0, 1);
	CHECK(action == WIIU_AUTO_CONTINUE_WAIT);
	CHECK(wiiu_auto_continue_step(&s, 0x1, 1) == WIIU_AUTO_CONTINUE_CANCELLED);
	for (i = 0; i < 1000; i++)
		CHECK(wiiu_auto_continue_step(&s, 0, 1) == WIIU_AUTO_CONTINUE_WAIT);

	printf(failures ? "FAILED %d\n" : "auto continue: all checks passed\n", failures);
	return failures ? 1 : 0;
}
