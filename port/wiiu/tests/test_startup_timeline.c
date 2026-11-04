/*
TEST_STARTUP_TIMELINE.C

port/linux/game/startup_timeline.c on its own: marks, notes and work added up from several threads at once, and the report it makes in the log once the menu
has been live for a few frames: every line within the log's 240 bytes, the marks in the order they were made with the milliseconds between them, the work's
sums right, and nothing made after the report. Built by tools/test_wiiu_startup_timeline.py (HALO_WIIU, as the Wii U's build and the digest harness are).
*/

#define HALO_WIIU 1
#include "../../linux/game/startup_timeline.c"

#include <pthread.h>

static char lines[64][512];
static int line_count;
static unsigned long clock_ms;
static unsigned long long clock_ticks;

void platform_log(char const *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	if (line_count < 64)
		vsnprintf(lines[line_count++], sizeof(lines[0]), format, arguments);
	va_end(arguments);
}

unsigned long wiiu_shell_ms(void)
{
	return __atomic_load_n(&clock_ms, __ATOMIC_RELAXED);
}

long wiiu_study_number(const char *name, long fallback)
{
	(void)name;
	return fallback;
}

unsigned long long wiiu_now_ticks(void)
{
	return clock_ticks;
}

unsigned long wiiu_ticks_per_second(void)
{
	return 1000;
}

#define CHECK(condition) do { if (!(condition)) { printf("FAIL line %d: %s\n", __LINE__, #condition); failures++; } } while (0)

static int failures;

static void *worker(void *argument)
{
	int index;
	long long started;

	(void)argument;
	for (index = 0; index < 10; index++)
	{
		started = startup_timeline_now();
		startup_timeline_add("helper_work", started, 1, 2048);
	}
	startup_timeline_note("helper_end");
	return NULL;
}

int main(void)
{
	pthread_t threads[4];
	int index, found_marks = 0, found_work = 0, found_note = 0;

	/* before anything: the option is all on without a file */
	CHECK(startup_option(1) && startup_option(2) && startup_option(8));
	clock_ms = 100;
	startup_timeline_mark("one");
	clock_ms = 350;
	startup_timeline_mark("two");
	clock_ticks = 1000;
	for (index = 0; index < 4; index++)
		pthread_create(&threads[index], NULL, worker, NULL);
	for (index = 0; index < 4; index++)
		pthread_join(threads[index], NULL);
	clock_ticks = 1500;
	{
		const long long started = 1000;

		startup_timeline_add("game_work", started, 1, 1024 * 5);
	}
	clock_ms = 900;
	startup_timeline_mark("menu_live");
	CHECK(startup_timeline_running());
	startup_timeline_frame();
	startup_timeline_frame();
	CHECK(startup_timeline_running());
	startup_timeline_frame();
	CHECK(!startup_timeline_running());
	CHECK(startup_timeline_finished);
	for (index = 0; index < line_count; index++)
	{
		CHECK(strlen(lines[index]) < 240);
		if (strstr(lines[index], "900 ms to a live menu") && strstr(lines[index], "one 100, two 250, menu_live 550"))
			found_marks = 1;
		if (strstr(lines[index], "helper_work 40x 0.00 ms 80 KB") && strstr(lines[index], "game_work 1x 500.00 ms 5 KB"))
			found_work = 1;
		if (strstr(lines[index], "events") && strstr(lines[index], "helper_end 350, helper_end 350, helper_end 350, helper_end 350"))
			found_note = 1;
	}
	CHECK(found_marks);
	CHECK(found_work);
	CHECK(found_note);
	/* after the report nothing is added or said */
	{
		const int before = line_count;

		startup_timeline_mark("late");
		startup_timeline_frame();
		CHECK(line_count == before);
		CHECK(startup_timeline_now() == 0);
	}
	for (index = 0; index < line_count; index++)
		printf("%s\n", lines[index]);
	printf("startup timeline: %d failures\n", failures);
	return failures ? 1 : 0;
}
