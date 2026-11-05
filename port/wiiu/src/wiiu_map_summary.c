/*
WIIU_MAP_SUMMARY.C

See wiiu_map_summary.h.
*/

#include "wiiu_map_summary.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define WINDOW_MS 5000.0
#define BIN_MS 0.5
#define HITCH_MS 100.0

/* ---------- the figures */

void wiiu_map_summary_reset(struct wiiu_map_summary *summary, char const *label, unsigned long const governor[WIIU_MAP_SUMMARY_STEPS], unsigned long const overlap[5])
{
	memset(summary, 0, sizeof(*summary));
	snprintf(summary->label, sizeof(summary->label), "%s", label ? label : "");
	if (governor)
		memcpy(summary->governor_at_start, governor, sizeof(summary->governor_at_start));
	if (overlap)
		memcpy(summary->overlap_at_start, overlap, sizeof(summary->overlap_at_start));
	summary->active = 1;
}

static void window_close(struct wiiu_map_summary *summary, int final)
{
	double fps;

	if (!summary->window_frames || summary->window_ms <= 0.0)
		return;
	/* (the last window of a map is counted if it is half a window or more: a shorter one is a map's loading or its end) */
	if (!final || summary->window_ms >= WINDOW_MS / 2.0)
	{
		fps = 1000.0 * (double)summary->window_frames / summary->window_ms;
		summary->windows++;
		if (fps >= 57.0)
			summary->windows_fast++;
		else if (fps >= 45.0)
			summary->windows_middle++;
		else
			summary->windows_slow++;
	}
	summary->window_frames = 0;
	summary->window_ms = 0.0;
}

void wiiu_map_summary_add_frame(struct wiiu_map_summary *summary, double frame_ms)
{
	long bin;

	if (!summary->active || frame_ms <= 0.0)
		return;
	summary->frames++;
	summary->sum_ms += frame_ms;
	if (frame_ms > summary->longest_ms)
		summary->longest_ms = frame_ms;
	if (frame_ms >= HITCH_MS)
		summary->hitches++;
	bin = (long)(frame_ms / BIN_MS);
	if (bin >= WIIU_MAP_SUMMARY_BINS - 1)
		bin = WIIU_MAP_SUMMARY_BINS - 1;
	summary->bins[bin]++;
	summary->window_frames++;
	summary->window_ms += frame_ms;
	if (summary->window_ms >= WINDOW_MS)
		window_close(summary, 0);
}

double wiiu_map_summary_p99_ms(struct wiiu_map_summary const *summary)
{
	/* the 99th percentile: the frame at the rank of the worst 1% (the worst one of a hundred, rounding up) */
	unsigned long worst = (summary->frames + 99) / 100;
	unsigned long seen = 0;
	long bin;

	if (!summary->frames)
		return 0.0;
	if (!worst)
		worst = 1;
	for (bin = WIIU_MAP_SUMMARY_BINS - 1; bin >= 0; bin--)
	{
		seen += summary->bins[bin];
		if (seen >= worst)
		{
			if (bin == WIIU_MAP_SUMMARY_BINS - 1)
				return summary->longest_ms;
			/* (the middle of the bin, the frames of the bin being anywhere in it) */
			return ((double)bin + 0.5) * BIN_MS;
		}
	}
	return 0.0;
}

int wiiu_map_summary_format(struct wiiu_map_summary const *summary, unsigned long const governor[WIIU_MAP_SUMMARY_STEPS], unsigned long const overlap[5],
	char lines[3][200])
{
	struct wiiu_map_summary closed = *summary;
	unsigned long steps[WIIU_MAP_SUMMARY_STEPS];
	unsigned long steps_total = 0;
	unsigned long falls = overlap[3] - summary->overlap_at_start[3];
	unsigned long held = overlap[4] - summary->overlap_at_start[4];
	unsigned long overlapped = overlap[1] - summary->overlap_at_start[1];
	unsigned long serial = overlap[2] - summary->overlap_at_start[2];
	double p99, fps_low;
	int count = 0, step;
	int length;

	if (!summary->frames)
		return 0;
	/* (the last window is counted here, not in the summary: the caller may ask again) */
	window_close(&closed, 1);
	p99 = wiiu_map_summary_p99_ms(summary);
	fps_low = p99 > 0.0 ? 1000.0 / p99 : 0.0;
	snprintf(lines[count++], 200, "map summary (%s): %lu frames in %.1f s, mean %.1f fps, 1%% low %.1f fps (%.1f ms), longest frame %.0f ms, hitches over 100 ms: %lu",
		summary->label, summary->frames, summary->sum_ms / 1000.0, 1000.0 * (double)summary->frames / summary->sum_ms, fps_low, p99, summary->longest_ms, summary->hitches);
	if (closed.windows)
	{
		const double windows = (double)closed.windows;

		snprintf(lines[count++], 200, "map summary: %lu 5 s windows: 57 fps and over %.1f%%, 45 to 57 %.1f%%, under 45 %.1f%%; serial fallbacks %lu (clock held %lu times); frames overlapped %lu, serial %lu in the overlap's maps",
			closed.windows, 100.0 * (double)closed.windows_fast / windows, 100.0 * (double)closed.windows_middle / windows, 100.0 * (double)closed.windows_slow / windows,
			falls, held, overlapped, serial);
	}
	else
	{
		snprintf(lines[count++], 200, "map summary: no complete 5 s window; serial fallbacks %lu (clock held %lu times); frames overlapped %lu, serial %lu in the overlap's maps",
			falls, held, overlapped, serial);
	}
	for (step = 0; step < WIIU_MAP_SUMMARY_STEPS; step++)
	{
		steps[step] = governor[step] - summary->governor_at_start[step];
		steps_total += steps[step];
	}
	if (steps_total)
	{
		length = snprintf(lines[count], 200, "map summary: governor steps, share of %lu frames:", steps_total);
		for (step = 0; step < WIIU_MAP_SUMMARY_STEPS && length > 0 && length < 170; step++)
		{
			if (!steps[step])
				continue;
			length += snprintf(lines[count] + length, 200 - (size_t)length, " %d %.0f%%", step, 100.0 * (double)steps[step] / (double)steps_total);
		}
		count++;
	}
	return count;
}

/* ---------- the game's */

void platform_log(char const *format, ...);
/* render_governor.c and tick_overlap.c's counters, since the start of the run */
void render_governor_step_frames(unsigned long frames[WIIU_MAP_SUMMARY_STEPS]);
void tick_overlap_summary_counts(unsigned long counts[5]);

static struct wiiu_map_summary current;
/* (the game thread feeds it; the end of the run, which can be the GX2 thread's or the debug server's, ends the map's note too) */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static void end_locked(void);

void wiiu_map_summary_begin(char const *label)
{
	unsigned long governor[WIIU_MAP_SUMMARY_STEPS], overlap[5];

	pthread_mutex_lock(&lock);
	/* (a map that begins while another has not ended: the old one is ended first) */
	if (current.active)
		end_locked();
	render_governor_step_frames(governor);
	tick_overlap_summary_counts(overlap);
	wiiu_map_summary_reset(&current, label, governor, overlap);
	pthread_mutex_unlock(&lock);
}

void wiiu_map_summary_frame(double frame_ms)
{
	pthread_mutex_lock(&lock);
	wiiu_map_summary_add_frame(&current, frame_ms);
	pthread_mutex_unlock(&lock);
}

void wiiu_map_summary_end(void)
{
	pthread_mutex_lock(&lock);
	end_locked();
	pthread_mutex_unlock(&lock);
}

static void end_locked(void)
{
	unsigned long governor[WIIU_MAP_SUMMARY_STEPS], overlap[5];
	char lines[3][200];
	int count, line;

	if (!current.active)
		return;
	render_governor_step_frames(governor);
	tick_overlap_summary_counts(overlap);
	count = wiiu_map_summary_format(&current, governor, overlap, lines);
	for (line = 0; line < count; line++)
		platform_log("%s", lines[line]);
	current.active = 0;
}
