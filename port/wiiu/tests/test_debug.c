/* The debug server's lines (wiiu_debug_protocol.c): commands, buttons, limits and the errors. */

#include "../src/wiiu_debug_protocol.h"
#include "../src/wiiu_input.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

static int parse(const char *line, wiiu_debug_request *request)
{
	return wiiu_debug_parse(line, request);
}

int main(void)
{
	wiiu_debug_request r;

	CHECK(parse("hello", &r) && r.command == WIIU_DEBUG_HELLO);
	CHECK(parse("quit", &r) && r.command == WIIU_DEBUG_QUIT);
	CHECK(parse("screenshot tv", &r) && r.command == WIIU_DEBUG_SCREENSHOT && r.which == WIIU_DEBUG_SHOT_TV);
	CHECK(parse("screenshot drc", &r) && r.which == WIIU_DEBUG_SHOT_DRC);
	CHECK(parse("screenshot back", &r) && r.which == WIIU_DEBUG_SHOT_BACK);
	CHECK(parse("screenshot tv", &r) && r.scale == 1);
	CHECK(parse("screenshot tv 2", &r) && r.scale == 2 && r.which == WIIU_DEBUG_SHOT_TV);
	CHECK(parse("screenshot back 4", &r) && r.scale == 4);
	CHECK(!parse("screenshot tv 3", &r));
	CHECK(!parse("screenshot tv 0", &r));
	CHECK(!parse("screenshot tv 8", &r));
	CHECK(!parse("screenshot", &r) && r.error[0]);
	CHECK(!parse("screenshot both", &r));

	CHECK(parse("press A", &r) && r.command == WIIU_DEBUG_PRESS && r.buttons == WIIU_PAD_A && r.milliseconds == WIIU_DEBUG_PRESS_DEFAULT_MS);
	CHECK(parse("press a+up 1000", &r) && r.buttons == (WIIU_PAD_A | WIIU_PAD_UP) && r.milliseconds == 1000);
	CHECK(parse("press STICKR+ZL 10000", &r) && r.buttons == (WIIU_PAD_STICK_R | WIIU_PAD_ZL) && r.milliseconds == 10000);
	/* (voice chat's push to talk) */
	CHECK(parse("press TV 3000", &r) && r.buttons == WIIU_PAD_TV && r.milliseconds == 3000);
	CHECK(!parse("press A 10001", &r));
	CHECK(!parse("press A 0", &r));
	CHECK(!parse("press A -5", &r));
	CHECK(!parse("press TRIANGLE", &r));
	CHECK(!parse("press A+", &r) || r.buttons == WIIU_PAD_A);
	CHECK(!parse("press +A", &r));
	CHECK(!parse("press", &r));

	CHECK(parse("stick 0 1 -0.5 0 500", &r) && r.command == WIIU_DEBUG_STICK && r.stick[0] == 0.0f && r.stick[1] == 1.0f && r.stick[2] == -0.5f &&
		r.stick[3] == 0.0f && r.milliseconds == 500);
	CHECK(!parse("stick 0 2 0 0 500", &r));
	CHECK(!parse("stick 0 nan 0 0 500", &r));
	CHECK(!parse("stick 0 1 0 0", &r));
	CHECK(!parse("stick 0 1 0 500", &r));

	CHECK(parse("tap 427 240", &r) && r.command == WIIU_DEBUG_TAP && r.tap[0] == 427.0f && r.tap[1] == 240.0f && r.milliseconds == WIIU_DEBUG_PRESS_DEFAULT_MS);
	CHECK(parse("tap 0 479.5 300", &r) && r.tap[1] == 479.5f && r.milliseconds == 300);
	CHECK(!parse("tap 854 10", &r));
	CHECK(!parse("tap 10 480", &r));
	CHECK(!parse("tap 10", &r));
	CHECK(!parse("tap -1 10", &r));

	CHECK(parse("gyro 0 0.2 1000", &r) && r.command == WIIU_DEBUG_GYRO && r.gyro[0] == 0.0f && r.gyro[1] == 0.2f && r.milliseconds == 1000);
	CHECK(!parse("gyro 0 11 1000", &r));
	CHECK(!parse("gyro 0 0.2", &r));

	CHECK(parse("log", &r) && r.command == WIIU_DEBUG_LOG && r.count == WIIU_DEBUG_LOG_DEFAULT);
	CHECK(parse("log 100", &r) && r.count == 100);
	CHECK(!parse("log 101", &r));
	CHECK(!parse("log 0", &r));

	CHECK(!parse("", &r));
	CHECK(!parse("   ", &r));
	CHECK(!parse("reboot", &r));
	CHECK(parse("  hello  ", &r) && r.command == WIIU_DEBUG_HELLO);
	CHECK(!parse("a-very-long-word-that-is-longer-than-the-word-buffer-of-the-parser-by-far", &r));

	if (failures)
	{
		printf("%d failed\n", failures);
		return 1;
	}
	printf("debug protocol: all passed\n");
	return 0;
}
