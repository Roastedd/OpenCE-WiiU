/*
WIIU_PACING.C

Frame pacing's statistics and policy (wiiu_pacing.h). No GX2 and no clock here: the caller measures.
*/

#include "wiiu_pacing.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

const float wiiu_pacing_edges[WIIU_PACING_BINS - 1] = { 15.0f, 17.5f, 20.0f, 25.0f, 30.0f, 35.0f, 42.0f, 50.0f, 100.0f };

void wiiu_pacing_reset(struct wiiu_pacing_stats *stats)
{
	memset(stats, 0, sizeof(*stats));
}

void wiiu_pacing_frame(struct wiiu_pacing_stats *stats, float frame_ms, float flip_wait_ms, float idle_ms)
{
	unsigned bin = 0;

	if (frame_ms < 0.0f)
		return;
	while (bin < WIIU_PACING_BINS - 1 && frame_ms >= wiiu_pacing_edges[bin])
		bin++;
	stats->bins[bin]++;
	stats->frames++;
	stats->sum_ms += frame_ms;
	if (frame_ms > stats->longest_ms)
		stats->longest_ms = frame_ms;
	stats->over_17 += frame_ms > 17.5f;
	stats->over_25 += frame_ms > 25.0f;
	stats->over_34 += frame_ms > 34.0f;
	if (flip_wait_ms < 0.0f)
		flip_wait_ms = 0.0f;
	if (flip_wait_ms > frame_ms)
		flip_wait_ms = frame_ms;
	stats->flip_ms += flip_wait_ms;
	if (idle_ms < 0.0f)
	{
		/* (not measured: counted as working, so the three still add up to the frames' time) */
		idle_ms = 0.0f;
	}
	if (idle_ms > frame_ms - flip_wait_ms)
		idle_ms = frame_ms - flip_wait_ms;
	stats->idle_ms += idle_ms;
	stats->work_ms += frame_ms - flip_wait_ms - idle_ms;
}

void wiiu_pacing_flip(struct wiiu_pacing_stats *stats, float since_last_flip_ms, float after_vsync_ms)
{
	unsigned refreshes;
	float phase;

	if (since_last_flip_ms < 0.0f)
		return;
	stats->flips++;
	refreshes = (unsigned)(since_last_flip_ms / WIIU_PACING_REFRESH_MS + 0.5f);
	if (refreshes < 1)
		refreshes = 1;
	stats->shown[refreshes > 4 ? 3 : refreshes - 1]++;
	phase = fmodf(after_vsync_ms, WIIU_PACING_REFRESH_MS);
	if (phase < 0.0f)
		phase += WIIU_PACING_REFRESH_MS;
	if (phase > 2.0f && phase < WIIU_PACING_REFRESH_MS - 2.0f)
		stats->away++;
}

int wiiu_pacing_format_frames(const struct wiiu_pacing_stats *stats, char *out, unsigned size)
{
	int length;

	if (!stats->frames)
	{
		out[0] = 0;
		return 0;
	}
	length = snprintf(out, size, "pacing: %u frames in %.1f s (%.1f a second), longest %.1f ms; over 17.5, 25, 34 ms: %u %u %u; ms <15 %u, <17.5 %u, <20 %u, <25 %u, <30 %u, <35 %u, <42 %u, <50 %u, <100 %u, more %u",
		stats->frames, stats->sum_ms / 1000.0, stats->sum_ms > 0.0 ? (double)stats->frames * 1000.0 / stats->sum_ms : 0.0, (double)stats->longest_ms, stats->over_17, stats->over_25,
		stats->over_34, stats->bins[0], stats->bins[1], stats->bins[2], stats->bins[3], stats->bins[4], stats->bins[5], stats->bins[6], stats->bins[7], stats->bins[8], stats->bins[9]);
	return length;
}

int wiiu_pacing_format_threads(const struct wiiu_pacing_stats *stats, char *out, unsigned size)
{
	double total = stats->work_ms + stats->idle_ms + stats->flip_ms;

	if (!stats->frames || total <= 0.0)
	{
		out[0] = 0;
		return 0;
	}
	return snprintf(out, size, "pacing: the GX2 thread's time: working %.0f%%, waiting for the game thread's streams %.0f%%, waiting for the display's flip %.0f%% (%.1f, %.1f, %.1f ms a frame)",
		100.0 * stats->work_ms / total, 100.0 * stats->idle_ms / total, 100.0 * stats->flip_ms / total, stats->work_ms / stats->frames, stats->idle_ms / stats->frames,
		stats->flip_ms / stats->frames);
}

int wiiu_pacing_format_display(const struct wiiu_pacing_stats *stats, char *out, unsigned size)
{
	if (!stats->flips && !stats->swaps_synced && !stats->swaps_tearing)
	{
		out[0] = 0;
		return 0;
	}
	return snprintf(out, size, "pacing: the display: %u flips, shown for 1, 2, 3, 4 or more refreshes: %u %u %u %u; %u flips away from a vsync (torn); swaps made waiting for the vsync %u, not waiting %u",
		stats->flips, stats->shown[0], stats->shown[1], stats->shown[2], stats->shown[3], stats->away, stats->swaps_synced, stats->swaps_tearing);
}

int wiiu_pacing_parse(struct wiiu_pacing_policy *policy, const char *text)
{
	int mode = 0, depth = 1;
	int read = sscanf(text, "%d %d", &mode, &depth);

	policy->mode = 0;
	policy->depth = 1;
	if (read < 1 || mode < 0 || mode > 2)
		return 0;
	policy->mode = mode;
	if (read == 2 && depth >= 1 && depth <= 2)
		policy->depth = (unsigned)depth;
	return 1;
}

unsigned wiiu_pacing_interval(const struct wiiu_pacing_policy *policy, float since_flip_ms, unsigned queued)
{
	if (policy->mode == 2)
		return 0;
	if (policy->mode == 1)
	{
		/* late: the display has been on the frame before for more than a refresh and a little (flips are not exact), and no swap waits for its flip, so
		this frame is the one it waits for */
		if (queued == 0 && since_flip_ms > 17.5f)
			return 0;
	}
	return 1;
}

float wiiu_pacing_hold_ms(const struct wiiu_pacing_policy *policy, float since_last_swap_ms)
{
	const float least = WIIU_PACING_REFRESH_MS * 0.95f;

	if (policy->mode != 2 || since_last_swap_ms >= least)
		return 0.0f;
	return least - since_last_swap_ms;
}
