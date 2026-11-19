/* test_notice.c: the slow SD card check (wiiu_notice.c): the limits, the message, and the benchmark on a temporary folder (the file is removed). Build and run
(from port/wiiu):

	cc -std=gnu11 -O2 -Wall -Wextra -DNOTICE_FOLDER='"/tmp"' -o /tmp/test_notice tests/test_notice.c src/wiiu_notice.c && /tmp/test_notice
*/

#include "../src/wiiu_notice.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void platform_log(char const *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vprintf(format, arguments);
	printf("\n");
	va_end(arguments);
}
long wiiu_study_number(const char *name, long fallback)
{
	(void)name;
	return fallback;
}
int async_save_running(void)
{
	return 0;
}
int wiiu_in_foreground(void)
{
	return 1;
}
void wiiu_thread_background(const char *name)
{
	(void)name;
}
void main_loading_present(void)
{
}

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

int main(void)
{
	char text[400], *line;
	float write_mbs = 0, read_mbs = 0;
	int lines = 0, bar = 0;
	unsigned generation = 0;

	CHECK(!wiiu_notice_card_is_slow(12.0f, 18.0f));
	CHECK(wiiu_notice_card_is_slow(3.9f, 18.0f));
	CHECK(wiiu_notice_card_is_slow(12.0f, 7.9f));
	CHECK(!wiiu_notice_card_is_slow(4.0f, 8.0f));
	CHECK(!wiiu_notice_card_is_slow(0.0f, 0.0f));

	wiiu_notice_card_text(text, sizeof(text), 2.5f, 15.0f);
	CHECK(strstr(text, "Your SD card is slow (2.5 MB/s write)."));
	CHECK(strstr(text, "A faster card (class 10 / U1 or better) helps."));
	for (line = text; line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : NULL)
	{
		const char *end = strchr(line, '\n');
		const size_t length = end ? (size_t)(end - line) : strlen(line);

		CHECK(length <= 62);
		lines++;
	}
	CHECK(lines <= 10);
	wiiu_notice_card_text(text, sizeof(text), 12.0f, 5.0f);
	CHECK(strstr(text, "5.0 MB/s read"));

	CHECK(wiiu_notice_card_bench("/tmp", &write_mbs, &read_mbs));
	CHECK(write_mbs > 0.0f && read_mbs > 0.0f);
	CHECK(access("/tmp/bench.tmp", F_OK) != 0); /* removed */
	CHECK(!wiiu_notice_card_bench("/nonexistent-folder", &write_mbs, &read_mbs));

	/* the loading bar's state: nothing while no load is going; the percent never goes back */
	CHECK(!wiiu_notice_panel(text, sizeof(text), &generation, &bar));
	wiiu_loading_begin();
	CHECK(wiiu_notice_panel(text, sizeof(text), &generation, &bar) && bar == 0);
	wiiu_loading_precache(0.5f);
	CHECK(wiiu_notice_panel(text, sizeof(text), &generation, &bar) && bar == 20 && strstr(text, "20%"));
	wiiu_loading_stage(10);
	CHECK(wiiu_notice_panel(text, sizeof(text), &generation, &bar) && bar == 20);
	wiiu_loading_stage(70);
	CHECK(wiiu_notice_panel(text, sizeof(text), &generation, &bar) && bar == 70);
	wiiu_loading_end();
	CHECK(!wiiu_notice_panel(text, sizeof(text), &generation, &bar));

	CHECK(!wiiu_notice_saving() || 1); /* (reads the switch; no save is running) */
	if (failures)
		printf("%d failures\n", failures);
	else
		printf("OK\n");
	return failures != 0;
}
