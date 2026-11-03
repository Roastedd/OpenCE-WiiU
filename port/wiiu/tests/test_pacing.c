/* wiiu_pacing.c: the frame time statistics, their log lines, and the swap policy of halo/pacing.txt. */

#include "../src/wiiu_pacing.h"

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

int main(void)
{
	struct wiiu_pacing_stats stats;
	struct wiiu_pacing_policy policy;
	char text[400];
	unsigned i, total;

	/* 60 frames at 16.7 ms: all in one bin, none over */
	wiiu_pacing_reset(&stats);
	for (i = 0; i < 60; i++)
		wiiu_pacing_frame(&stats, 16.7f, 12.0f, 2.0f);
	CHECK(stats.frames == 60 && stats.bins[1] == 60);
	CHECK(stats.over_17 == 0 && stats.over_25 == 0 && stats.over_34 == 0);
	CHECK(stats.work_ms > 60 * 2.6 && stats.work_ms < 60 * 2.8); /* 16.7 - 12 - 2 */
	CHECK(stats.idle_ms > 119.9 && stats.idle_ms < 120.1 && stats.flip_ms > 719.9 && stats.flip_ms < 720.1);
	/* the edges: 17.5 is not over 17.5, 17.6 is; 25.0 and 34.0 likewise */
	wiiu_pacing_reset(&stats);
	wiiu_pacing_frame(&stats, 17.5f, 0, 0);
	wiiu_pacing_frame(&stats, 17.6f, 0, 0);
	wiiu_pacing_frame(&stats, 25.0f, 0, 0);
	wiiu_pacing_frame(&stats, 25.1f, 0, 0);
	wiiu_pacing_frame(&stats, 34.0f, 0, 0);
	wiiu_pacing_frame(&stats, 34.1f, 0, 0);
	CHECK(stats.over_17 == 5 && stats.over_25 == 3 && stats.over_34 == 1);
	wiiu_pacing_frame(&stats, 14.9f, 0, 0);
	wiiu_pacing_frame(&stats, 500.0f, 0, 0);
	wiiu_pacing_frame(&stats, 100.0f, 0, 0);
	CHECK(stats.bins[0] == 1 && stats.bins[9] == 2 && stats.longest_ms == 500.0f);
	total = 0;
	for (i = 0; i < WIIU_PACING_BINS; i++)
		total += stats.bins[i];
	CHECK(total == stats.frames);
	/* a flip wait longer than the frame, an idle longer than what is left: clamped, the three shares add to the frame */
	wiiu_pacing_reset(&stats);
	wiiu_pacing_frame(&stats, 20.0f, 30.0f, 30.0f);
	CHECK(stats.flip_ms == 20.0 && stats.idle_ms == 0.0 && stats.work_ms == 0.0);
	wiiu_pacing_reset(&stats);
	wiiu_pacing_frame(&stats, 20.0f, 5.0f, 30.0f);
	CHECK(stats.flip_ms == 5.0 && stats.idle_ms == 15.0 && stats.work_ms == 0.0);
	wiiu_pacing_reset(&stats);
	wiiu_pacing_frame(&stats, 20.0f, 5.0f, -1.0f);
	CHECK(stats.idle_ms == 0.0 && stats.work_ms == 15.0);
	wiiu_pacing_frame(&stats, -1.0f, 0, 0);
	CHECK(stats.frames == 1);

	/* the display: flips of 1, 2, 3 and 6 refreshes; one flip 8 ms after a vsync is torn, 0.5 ms and 16.3 ms after are not */
	wiiu_pacing_reset(&stats);
	wiiu_pacing_flip(&stats, 16.7f, 0.5f);
	wiiu_pacing_flip(&stats, 33.3f, 16.3f);
	wiiu_pacing_flip(&stats, 50.0f, 8.0f);
	wiiu_pacing_flip(&stats, 100.0f, 0.0f);
	wiiu_pacing_flip(&stats, 5.0f, 41.0f); /* (5 ms: counted as one refresh; 41 ms after a vsync is 7.7 into a refresh) */
	CHECK(stats.flips == 5 && stats.shown[0] == 2 && stats.shown[1] == 1 && stats.shown[2] == 1 && stats.shown[3] == 1);
	CHECK(stats.away == 2);
	wiiu_pacing_flip(&stats, 16.7f, -8.0f); /* (a vsync after the flip: 8.7 ms into the refresh before) */
	CHECK(stats.away == 3);

	/* the log lines fit a log line (under 190 characters) even with the biggest numbers */
	wiiu_pacing_reset(&stats);
	for (i = 0; i < 300; i++)
		wiiu_pacing_frame(&stats, 99999.9f, 99999.9f, 99999.9f);
	stats.over_17 = stats.over_25 = stats.over_34 = 999999;
	for (i = 0; i < WIIU_PACING_BINS; i++)
		stats.bins[i] = 999999;
	stats.longest_ms = 999999.9f;
	CHECK(wiiu_pacing_format_frames(&stats, text, sizeof(text)) > 100);
	printf("%s\n", text);
	CHECK(strlen(text) < 330); /* (the biggest numbers; the log line is cut at 200: the ordinary line is checked below) */
	wiiu_pacing_reset(&stats);
	for (i = 0; i < 300; i++)
		wiiu_pacing_frame(&stats, 23.4f, 0.5f, 3.0f);
	CHECK(wiiu_pacing_format_frames(&stats, text, sizeof(text)) > 100 && strlen(text) < 200);
	printf("%s\n", text);
	CHECK(wiiu_pacing_format_threads(&stats, text, sizeof(text)) > 50 && strlen(text) < 200);
	printf("%s\n", text);
	for (i = 0; i < 290; i++)
		wiiu_pacing_flip(&stats, i % 3 ? 16.7f : 33.3f, 0.2f);
	stats.swaps_synced = 250;
	stats.swaps_tearing = 50;
	CHECK(wiiu_pacing_format_display(&stats, text, sizeof(text)) > 50 && strlen(text) < 200);
	printf("%s\n", text);
	wiiu_pacing_reset(&stats);
	CHECK(wiiu_pacing_format_frames(&stats, text, sizeof(text)) == 0 && text[0] == 0);
	CHECK(wiiu_pacing_format_threads(&stats, text, sizeof(text)) == 0);
	CHECK(wiiu_pacing_format_display(&stats, text, sizeof(text)) == 0);

	/* the card */
	CHECK(wiiu_pacing_parse(&policy, "1\n") && policy.mode == 1 && policy.depth == 1);
	CHECK(wiiu_pacing_parse(&policy, "2 3") && policy.mode == 2 && policy.depth == 3);
	CHECK(wiiu_pacing_parse(&policy, "1 9") && policy.mode == 1 && policy.depth == 1); /* (a depth out of 1 to 3 is ignored) */
	CHECK(wiiu_pacing_parse(&policy, "0") && policy.mode == 0);
	CHECK(!wiiu_pacing_parse(&policy, "7") && policy.mode == 0);
	CHECK(!wiiu_pacing_parse(&policy, "-1") && policy.mode == 0);
	CHECK(!wiiu_pacing_parse(&policy, "") && policy.mode == 0);
	CHECK(!wiiu_pacing_parse(&policy, "fast") && policy.mode == 0);

	/* the policy: off is always the vsync */
	policy.mode = 0;
	policy.depth = 1;
	CHECK(wiiu_pacing_interval(&policy, 5.0f, 0) == 1 && wiiu_pacing_interval(&policy, 80.0f, 0) == 1);
	CHECK(wiiu_pacing_hold_ms(&policy, 1.0f) == 0.0f);
	/* tear when late: not late while the display has been on the frame for a refresh or less, or a swap waits to be flipped */
	policy.mode = 1;
	CHECK(wiiu_pacing_interval(&policy, 0.0f, 0) == 1);
	CHECK(wiiu_pacing_interval(&policy, 16.7f, 0) == 1);
	CHECK(wiiu_pacing_interval(&policy, 17.5f, 0) == 1);
	CHECK(wiiu_pacing_interval(&policy, 17.6f, 0) == 0);
	CHECK(wiiu_pacing_interval(&policy, 23.4f, 0) == 0);
	CHECK(wiiu_pacing_interval(&policy, 23.4f, 1) == 1);
	CHECK(wiiu_pacing_interval(&policy, 500.0f, 2) == 1);
	CHECK(wiiu_pacing_hold_ms(&policy, 1.0f) == 0.0f);
	/* never wait for the vsync, but not faster than a refresh each */
	policy.mode = 2;
	CHECK(wiiu_pacing_interval(&policy, 0.0f, 0) == 0 && wiiu_pacing_interval(&policy, 40.0f, 3) == 0);
	CHECK(wiiu_pacing_hold_ms(&policy, 0.0f) > 15.8f && wiiu_pacing_hold_ms(&policy, 0.0f) < 15.9f);
	CHECK(wiiu_pacing_hold_ms(&policy, 10.0f) > 5.8f && wiiu_pacing_hold_ms(&policy, 10.0f) < 5.9f);
	CHECK(wiiu_pacing_hold_ms(&policy, 15.9f) == 0.0f && wiiu_pacing_hold_ms(&policy, 40.0f) == 0.0f);

	if (failures)
	{
		printf("%d FAILED\n", failures);
		return 1;
	}
	printf("pacing: all passed\n");
	return 0;
}
