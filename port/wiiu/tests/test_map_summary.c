/* The map summary's figures (port/wiiu/src/wiiu_map_summary.c) on frame times made up for them: the shares of the 5 s windows at 57 fps and over, 45 to 57 and
under 45, the 1% low, the hitches, the counters taken from their start, and the lines. */

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/wiiu_map_summary.h"

static int failures;
#define CHECK(condition) do { if (!(condition)) { printf("FAIL line %d: %s\n", __LINE__, #condition); failures++; } } while (0)

/* the stand-ins for the game's */
static char said[8][300];
static int said_count;
static unsigned long governor_now[WIIU_MAP_SUMMARY_STEPS];
static unsigned long overlap_now[5];

void platform_log(char const *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	if (said_count < 8)
		vsnprintf(said[said_count++], sizeof(said[0]), format, arguments);
	va_end(arguments);
}
void render_governor_step_frames(unsigned long frames[WIIU_MAP_SUMMARY_STEPS]) { memcpy(frames, governor_now, sizeof(governor_now)); }
void tick_overlap_summary_counts(unsigned long counts[5]) { memcpy(counts, overlap_now, sizeof(overlap_now)); }

static struct wiiu_map_summary summary;

static void frames(long count, double ms)
{
	long i;

	for (i = 0; i < count; i++)
		wiiu_map_summary_add_frame(&summary, ms);
}

static void begin(void)
{
	unsigned long governor[WIIU_MAP_SUMMARY_STEPS] = { 0 }, overlap[5] = { 0 };

	wiiu_map_summary_reset(&summary, "levels\\test", governor, overlap);
}

int main(void)
{
	char lines[3][200];
	unsigned long governor[WIIU_MAP_SUMMARY_STEPS] = { 100, 50, 25, 25, 0, 0, 0, 0 };
	unsigned long overlap[5] = { 1000, 900, 100, 2, 3 };
	int count, i;

	/* 20 s at 60 fps (16.7 ms): 4 windows, all fast */
	begin();
	frames(1200, 1000.0 / 60.0);
	CHECK(summary.windows == 4 && summary.windows_fast == 4 && summary.windows_middle == 0 && summary.windows_slow == 0);
	CHECK(summary.frames == 1200 && summary.hitches == 0);
	CHECK(fabs(wiiu_map_summary_p99_ms(&summary) - 16.75) < 0.3);

	/* 5 s at 50 fps, 5 s at 40 fps, 5 s at 58 fps: one in each class */
	begin();
	frames(250, 20.0);
	frames(200, 25.0);
	frames(295, 1000.0 / 58.0);
	CHECK(summary.windows == 3 && summary.windows_fast == 1 && summary.windows_middle == 1 && summary.windows_slow == 1);

	/* a window is five seconds of frames: the fps is frames over time, not an average of fps (a hitch of 1 s in 4 s of 60 fps is 5 s of 241 frames: 48 fps) */
	begin();
	frames(241, 1000.0 / 60.0);
	frames(1, 1000.0);
	CHECK(summary.windows == 1 && summary.windows_middle == 1);
	CHECK(summary.hitches == 1 && summary.longest_ms == 1000.0);

	/* the 1% low: 2% of the frames at 50 ms is a 1% low of 20 fps; 0.5% is not */
	begin();
	frames(980, 16.0);
	frames(20, 50.0);
	CHECK(fabs(wiiu_map_summary_p99_ms(&summary) - 50.25) < 0.3);
	begin();
	frames(995, 16.0);
	frames(5, 50.0);
	CHECK(fabs(wiiu_map_summary_p99_ms(&summary) - 16.25) < 0.3);

	/* hitches: frames of 100 ms and over, 99.9 is not one */
	begin();
	frames(3, 99.9);
	frames(2, 100.0);
	frames(1, 450.0);
	CHECK(summary.hitches == 3);

	/* nothing is counted before a map begins, or for a frame of no time */
	memset(&summary, 0, sizeof(summary));
	frames(10, 16.0);
	CHECK(summary.frames == 0);
	begin();
	frames(10, 0.0);
	CHECK(summary.frames == 0);

	/* the last window: counted if it is 2.5 s or more, left out if it is less */
	begin();
	frames(160, 1000.0 / 60.0); /* 2.7 s */
	count = wiiu_map_summary_format(&summary, governor, overlap, lines);
	CHECK(count >= 2 && strstr(lines[1], "1 5 s windows"));
	begin();
	frames(100, 1000.0 / 60.0); /* 1.67 s */
	count = wiiu_map_summary_format(&summary, governor, overlap, lines);
	CHECK(count >= 2 && strstr(lines[1], "no complete 5 s window"));

	/* the lines: counters are taken from the map's start, the governor's shares sum to what it ran */
	{
		unsigned long start_governor[WIIU_MAP_SUMMARY_STEPS] = { 100, 50, 25, 25, 0, 0, 0, 0 }, start_overlap[5] = { 1000, 900, 100, 2, 3 };

		wiiu_map_summary_reset(&summary, "levels\\b30\\b30, smooth defaults on", start_governor, start_overlap);
		frames(600, 1000.0 / 60.0);
		governor[0] = 100 + 400; governor[1] = 50 + 100; governor[2] = 25 + 60; governor[3] = 25 + 40;
		overlap[0] = 1600; overlap[1] = 1500; overlap[2] = 100; overlap[3] = 3; overlap[4] = 5;
		count = wiiu_map_summary_format(&summary, governor, overlap, lines);
		CHECK(count == 3);
		for (i = 0; i < count; i++)
		{
			printf("%s\n", lines[i]);
			CHECK(strlen(lines[i]) < 190);
		}
		CHECK(strstr(lines[0], "levels\\b30\\b30, smooth defaults on") && strstr(lines[0], "600 frames") && strstr(lines[0], "hitches over 100 ms: 0"));
		CHECK(strstr(lines[1], "serial fallbacks 1 (clock held 2 times)") && strstr(lines[1], "frames overlapped 600, serial 0"));
		CHECK(strstr(lines[2], "0 67%") && strstr(lines[2], "1 17%") && strstr(lines[2], "2 10%") && strstr(lines[2], "3 7%"));
		/* the game's three calls */
		said_count = 0;
		memcpy(governor_now, start_governor, sizeof(governor_now));
		memcpy(overlap_now, start_overlap, sizeof(overlap_now));
		wiiu_map_summary_begin("levels\\a10\\a10, smooth defaults on");
		wiiu_map_summary_frame(4000.0); /* (the map's load: the first frame after a begin is not counted) */
		for (i = 0; i < 300; i++)
			wiiu_map_summary_frame(1000.0 / 60.0);
		governor_now[0] += 300;
		wiiu_map_summary_end();
		CHECK(said_count == 3 && strstr(said[0], "a10") && strstr(said[0], "300 frames") && strstr(said[0], "hitches over 100 ms: 0") && strstr(said[2], "0 100%"));
		wiiu_map_summary_end(); /* (a second end says nothing more) */
		CHECK(said_count == 3);
		/* a map that begins while another has not ended ends it first */
		wiiu_map_summary_begin("one");
		wiiu_map_summary_frame(16.0);
		wiiu_map_summary_frame(16.0);
		wiiu_map_summary_begin("two");
		CHECK(said_count == 5 && strstr(said[3], "(one)"));
	}
	/* a map with no frames says nothing */
	said_count = 0;
	wiiu_map_summary_end();
	wiiu_map_summary_begin("empty");
	wiiu_map_summary_end();
	CHECK(said_count == 0);

	printf(failures ? "FAILED %d\n" : "map summary: all checks passed\n", failures);
	return failures ? 1 : 0;
}
