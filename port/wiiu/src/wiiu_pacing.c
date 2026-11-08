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

int wiiu_pacing_format_late(const struct wiiu_pacing_stats *stats, char *out, unsigned size)
{
	const double minutes = stats->sum_ms / 60000.0;
	const unsigned late_flips = stats->flips - stats->shown[0];

	if (!stats->frames || minutes <= 0.0)
	{
		out[0] = 0;
		return 0;
	}
	return snprintf(out, size, "pacing: late: %u of %u frames over 17.5 ms (%.1f%%, %.0f a minute); %u of %u flips shown for 2 or more refreshes (%.1f%%, %.0f a minute)",
		stats->over_17, stats->frames, 100.0 * stats->over_17 / stats->frames, stats->over_17 / minutes, late_flips, stats->flips,
		stats->flips ? 100.0 * late_flips / stats->flips : 0.0, late_flips / minutes);
}

int wiiu_pacing_parse(struct wiiu_pacing_policy *policy, const char *text)
{
	int mode = 0, depth = 1;
	int read = sscanf(text, "%d %d", &mode, &depth);

	policy->mode = 0;
	policy->depth = 1;
	if (read < 1 || mode < 0 || mode > 3)
		return 0;
	policy->mode = mode;
	/* (mode 3 is the deeper queue: two swaps may wait for their flip unless the card says one) */
	if (mode == 3)
		policy->depth = 2;
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

/* ---------- the display-clock stamp */

#define STAMP_NOMINAL_REFRESH_MS ((double)WIIU_PACING_REFRESH_MS)

void wiiu_stamp_reset(struct wiiu_stamp *stamp, int mode, double test_ms)
{
	unsigned i;

	memset(stamp, 0, sizeof(*stamp));
	stamp->mode = mode;
	stamp->test_ms = test_ms;
	stamp->tick_ms = 1000.0 / 30.0;
	stamp->refresh_ms = STAMP_NOMINAL_REFRESH_MS;
	stamp->spread_ms = 1.5;
	stamp->gpu_best = 4;
	for (i = 0; i < WIIU_STAMP_GPU_STEPS; i++)
		stamp->hit[i] = 0.5;
}

int wiiu_stamp_parse(const char *text, int *mode, double *test_ms)
{
	int read_mode = 0;
	double read_ms = 0.0;
	int read = sscanf(text, "%d %lf", &read_mode, &read_ms);

	*mode = 0;
	*test_ms = 0.0;
	if (read < 1 || read_mode < 0 || read_mode > 3)
		return 0;
	*mode = read_mode;
	if (read == 2 && read_ms > -100.0 && read_ms < 100.0)
		*test_ms = read_ms;
	return 1;
}

/* the first time of the grid at or after t */
static double stamp_grid_after(const struct wiiu_stamp *stamp, double t)
{
	return stamp->grid_ms + ceil((t - stamp->grid_ms) / stamp->refresh_ms - 1e-9) * stamp->refresh_ms;
}

/* the flip of a frame ready at ready_ms: the first vsync after, and a refresh after the flip before at least */
static double stamp_hard(const struct wiiu_stamp *stamp, double ready_ms, double previous_flip_ms)
{
	const double flip = stamp_grid_after(stamp, ready_ms);
	const double least = previous_flip_ms > 0.0 ? stamp_grid_after(stamp, previous_flip_ms + 0.5 * stamp->refresh_ms) : flip;

	return flip < least ? least : flip;
}

/* the same with the readiness uncertain by +-spread_ms (a uniform spread): the flip's mean, which moves smoothly across a vsync where the hard one jumps a refresh */
static double stamp_soft(const struct wiiu_stamp *stamp, double ready_ms, double previous_flip_ms, double spread_ms)
{
	const double r = stamp->refresh_ms;
	const double lo = ready_ms - spread_ms, hi = ready_ms + spread_ms;
	const double least = previous_flip_ms > 0.0 ? stamp_grid_after(stamp, previous_flip_ms + 0.5 * r) : -1e300;
	double m, sum = 0.0;
	unsigned count;

	if (spread_ms < 0.05)
		return stamp_hard(stamp, ready_ms, previous_flip_ms);
	m = ceil((lo - stamp->grid_ms) / r - 1e-9);
	for (count = 0; count < 12; count++, m += 1.0)
	{
		const double t = stamp->grid_ms + m * r;
		const double a = t - r > lo ? t - r : lo;
		const double b = t < hi ? t : hi;
		const double flip = t < least ? least : t;

		if (b > a)
			sum += (b - a) / (hi - lo) * flip;
		if (t >= hi)
			break;
	}
	return sum;
}

static double stamp_clamp(double value, double low, double high)
{
	return value < low ? low : value > high ? high : value;
}

double wiiu_stamp_begin(struct wiiu_stamp *stamp, double start_ms, unsigned klass, unsigned *sequence)
{
	struct wiiu_stamp_frame *frame;
	double move = 0.0;

	if (klass >= WIIU_STAMP_CLASSES)
		klass = WIIU_STAMP_CLASSES - 1;
	stamp->sequence++;
	if (!stamp->sequence)
		stamp->sequence = 1;
	frame = &stamp->frame[stamp->sequence % WIIU_STAMP_FRAMES];
	memset(frame, 0, sizeof(*frame));
	frame->sequence = stamp->sequence;
	frame->klass = klass;
	frame->start_ms = start_ms;
	*sequence = stamp->sequence;
	if (stamp->have_grid && stamp->work_seen[klass] >= 3)
	{
		const double ready = start_ms + stamp->work_ms[klass] + (double)stamp->gpu_best;
		unsigned predictions = stamp->scored + 1;
		const double weight = 1.0 / (predictions < 64 ? (double)predictions : 64.0);

		frame->predicted_hard_ms = stamp_hard(stamp, ready, stamp->previous_hard_ms);
		frame->predicted_soft_ms = stamp_soft(stamp, ready, stamp->previous_hard_ms, stamp->spread_ms);
		frame->predicted_latency_ms = frame->predicted_soft_ms - start_ms;
		frame->predicted = 1;
		if (stamp->prediction_mean_ms == 0.0)
			stamp->prediction_mean_ms = frame->predicted_latency_ms;
		else
			stamp->prediction_mean_ms += weight * (frame->predicted_latency_ms - stamp->prediction_mean_ms);
		frame->predicted_deviation_ms = frame->predicted_latency_ms - stamp->prediction_mean_ms;
		frame->correction_ms = stamp_clamp(stamp->shrink * frame->predicted_deviation_ms, -stamp->refresh_ms, stamp->refresh_ms);
		stamp->previous_hard_ms = frame->predicted_hard_ms;
		if (stamp->mode == 1)
			move = frame->correction_ms;
	}
	if (stamp->mode == 3)
		move = stamp->test_ms;
	return move;
}

double wiiu_stamp_picture(struct wiiu_stamp *stamp, long tick, double fraction, double move_ms, int held)
{
	const double span = stamp->tick_ms;
	const double base = ((double)tick - 1.0) * span;
	const double time = base + fraction * span;
	double moved = time + move_ms;

	if (held || fraction >= 1.0 || fraction < 0.0 || move_ms == 0.0)
	{
		/* (a pair held at its end, or not a fraction, or no move: the fraction is returned as it came, to the last bit) */
		stamp->shown_ms = time;
		stamp->have_shown = 1;
		if (held)
			stamp->window_held++;
		return fraction;
	}
	if (stamp->have_shown && moved < stamp->shown_ms && stamp->shown_ms - moved < 4.0 * span)
	{
		/* (never back in time; a larger step back is another map's clock) */
		moved = stamp->shown_ms;
		stamp->window_backward++;
	}
	if (moved > base + span)
	{
		moved = base + span;
		stamp->window_clamped++;
	}
	else if (moved < base)
	{
		moved = base;
		stamp->window_clamped++;
	}
	stamp->shown_ms = moved;
	stamp->have_shown = 1;
	return stamp_clamp((moved - base) / span, 0.0, 1.0);
}

void wiiu_stamp_swap(struct wiiu_stamp *stamp, unsigned sequence, double swap_ms, unsigned swap_index)
{
	struct wiiu_stamp_frame *frame;
	double sample;

	if (!sequence)
		return;
	frame = &stamp->frame[sequence % WIIU_STAMP_FRAMES];
	if (frame->sequence != sequence)
		return;
	frame->swap_ms = swap_ms;
	frame->swap_index = swap_index;
	sample = swap_ms - frame->start_ms;
	/* (a hitch is not evidence of what a frame takes) */
	if (sample < 0.0 || sample > 100.0)
		return;
	if (!stamp->work_seen[frame->klass])
		stamp->work_ms[frame->klass] = sample;
	else
	{
		const double error = sample - stamp->work_ms[frame->klass];

		stamp->spread_ms += 0.05 * (stamp_clamp(2.0 * fabs(error), 0.5, 4.0) - stamp->spread_ms);
		stamp->work_ms[frame->klass] += 0.15 * error;
	}
	if (stamp->work_seen[frame->klass] < 1000000)
		stamp->work_seen[frame->klass]++;
}

static void stamp_score(struct wiiu_stamp *stamp, struct wiiu_stamp_frame *frame)
{
	const double latency = frame->flip_ms - frame->start_ms;
	unsigned g;

	if (frame->flip_exact)
	{
		double best_value = -1.0;

		if (frame->predicted)
		{
			const double a = 1.0 / (stamp->scored + 1 < 64 ? (double)(stamp->scored + 1) : 64.0);
			const double b = 1.0 / (stamp->scored + 1 < 200 ? (double)(stamp->scored + 1) : 200.0);
			const double x = frame->predicted_deviation_ms;
			double y;
			double stamped;

			if (!stamp->scored)
				stamp->latency_mean_ms = latency;
			else
				stamp->latency_mean_ms += a * (latency - stamp->latency_mean_ms);
			y = latency - stamp->latency_mean_ms;
			stamp->covariance += b * (x * y - stamp->covariance);
			stamp->variance += b * (x * x - stamp->variance);
			stamp->scored++;
			stamp->shrink = stamp->scored >= 30 && stamp->variance > 1e-6 ? stamp_clamp(stamp->covariance / stamp->variance, 0.0, 1.0) : 0.0;
			stamped = y - frame->correction_ms;
			stamp->window_frames++;
			stamp->sum_raw += y;
			stamp->sum_raw2 += y * y;
			stamp->sum_stamped += stamped;
			stamp->sum_stamped2 += stamped * stamped;
			stamp->sum_error += fabs(frame->predicted_soft_ms - frame->flip_ms);
			stamp->sum_move_abs += fabs(frame->correction_ms);
			if (fabs(frame->correction_ms) > stamp->max_move)
				stamp->max_move = fabs(frame->correction_ms);
		}
		/* which time from the swap to the frame being ready told this flip (given the flip before it) */
		if (frame->swap_ms > 0.0)
		{
			for (g = 0; g < WIIU_STAMP_GPU_STEPS; g++)
			{
				const double flip = stamp_hard(stamp, frame->swap_ms + (double)g, stamp->previous_flip_ms);
				const double hit = fabs(flip - frame->flip_ms) < 0.5 * stamp->refresh_ms ? 1.0 : 0.0;

				stamp->hit[g] += 0.05 * (hit - stamp->hit[g]);
			}
			for (g = 0; g < WIIU_STAMP_GPU_STEPS; g++)
				if (stamp->hit[g] > best_value)
					best_value = stamp->hit[g];
			if (stamp->hit[stamp->gpu_best] < best_value - 0.005)
			{
				for (g = 0; g < WIIU_STAMP_GPU_STEPS; g++)
					if (stamp->hit[g] >= best_value - 1e-12)
					{
						stamp->gpu_best = g;
						break;
					}
			}
		}
	}
	stamp->previous_flip_ms = frame->flip_ms;
}

void wiiu_stamp_flip(struct wiiu_stamp *stamp, double last_flip_ms, unsigned flips)
{
	unsigned back;

	if (stamp->have_grid && flips == stamp->flips_seen)
		return;
	if (stamp->have_grid && flips > stamp->flips_seen)
	{
		/* the display's period, from two looks at its flips a whole number of refreshes apart */
		const double delta = last_flip_ms - stamp->grid_ms;
		const double refreshes = floor(delta / stamp->refresh_ms + 0.5);

		if (refreshes >= 1.0 && refreshes <= 8.0)
		{
			const double period = delta / refreshes;

			if (fabs(period - STAMP_NOMINAL_REFRESH_MS) < 0.03 * STAMP_NOMINAL_REFRESH_MS)
				stamp->refresh_ms += 0.02 * (period - stamp->refresh_ms);
		}
	}
	stamp->grid_ms = last_flip_ms;
	stamp->flips_seen = flips;
	stamp->have_grid = 1;
	/* the frames whose swaps have flipped, oldest first */
	for (back = WIIU_STAMP_FRAMES; back > 0; back--)
	{
		struct wiiu_stamp_frame *frame;
		unsigned sequence;

		if (stamp->sequence + 1 < back)
			continue;
		sequence = stamp->sequence + 1 - back;
		if (!sequence)
			continue;
		frame = &stamp->frame[sequence % WIIU_STAMP_FRAMES];
		if (frame->sequence != sequence || !frame->swap_index || frame->flip_ms != 0.0 || frame->swap_index > flips)
			continue;
		frame->flip_exact = frame->swap_index == flips;
		frame->flip_ms = last_flip_ms - (double)(flips - frame->swap_index) * stamp->refresh_ms;
		stamp_score(stamp, frame);
		if (stamp->sequence == frame->sequence)
			stamp->previous_hard_ms = frame->flip_ms;
	}
}

int wiiu_stamp_format(struct wiiu_stamp *stamp, char *out, unsigned size)
{
	const double n = (double)stamp->window_frames;
	int length;

	if (!stamp->window_frames)
	{
		out[0] = 0;
		return 0;
	}
	length = snprintf(out, size, "pacing: stamp (%s): %u frames, judder rms %.2f ms as drawn, %.2f with the stamp (shrink %.2f, moves %.2f ms mean, %.1f most; %u held, %u clamped, %u not back); "
		"flip predicted to %.2f ms; refresh %.3f ms, ready %u ms after the swap",
		stamp->mode == 1 ? "applied" : stamp->mode == 3 ? "test" : "measured", stamp->window_frames, sqrt(stamp->sum_raw2 / n), sqrt(stamp->sum_stamped2 / n),
		stamp->shrink, stamp->sum_move_abs / n, stamp->max_move, stamp->window_held, stamp->window_clamped, stamp->window_backward, stamp->sum_error / n,
		stamp->refresh_ms, stamp->gpu_best);
	stamp->window_frames = stamp->window_clamped = stamp->window_held = stamp->window_backward = 0;
	stamp->sum_raw = stamp->sum_raw2 = stamp->sum_stamped = stamp->sum_stamped2 = stamp->sum_error = stamp->sum_move_abs = stamp->max_move = 0.0;
	return length;
}

/* ---------- the locked 30 */

void wiiu_lock30_defaults(struct wiiu_lock30_settings *set)
{
	memset(set, 0, sizeof(*set));
	set->lock_over_ms = 20.5;
	set->lock_share = 0.60;
	set->lock_s = 6.0;
	set->hold_ms = 31.0;
	set->unlock_over_ms = 16.0;
	set->unlock_share = 0.10;
	set->unlock_s = 12.0;
	set->dwell_s = 15.0;
	set->quiet_s = 20.0;
	set->max_switches = 4;
	set->switch_window_s = 300.0;
}

int wiiu_lock30_parse(struct wiiu_lock30_settings *set, const char *text)
{
	int mode = 0;
	double v[6] = { 0, 0, 0, 0, 0, 0 };
	int read = sscanf(text, "%d %lf %lf %lf %lf %lf %lf", &mode, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]);

	wiiu_lock30_defaults(set);
	if (read < 1 || mode < 0 || mode > 2)
		return 0;
	if (read >= 2)
	{
		/* (the numbers in order: a slow frame's work, the share of slow frames to lock at, over how many seconds, then the work a frame that holds 60 is under, the share over it to unlock
		at, and over how many seconds) */
		const int count = read - 1;

		if ((count >= 1 && (v[0] < 14.0 || v[0] > 60.0)) || (count >= 2 && (v[1] < 0.05 || v[1] > 1.0)) || (count >= 3 && (v[2] < 1.0 || v[2] > 30.0)) ||
			(count >= 4 && (v[3] < 8.0 || v[3] > 40.0)) || (count >= 5 && (v[4] < 0.0 || v[4] > 0.5)) || (count >= 6 && (v[5] < 1.0 || v[5] > 30.0)))
			return 0;
		if (count >= 1)
			set->lock_over_ms = v[0];
		if (count >= 2)
			set->lock_share = v[1];
		if (count >= 3)
			set->lock_s = v[2];
		if (count >= 4)
			set->unlock_over_ms = v[3];
		if (count >= 5)
			set->unlock_share = v[4];
		if (count >= 6)
			set->unlock_s = v[5];
		if (set->unlock_over_ms >= set->lock_over_ms)
		{
			wiiu_lock30_defaults(set);
			return 0;
		}
	}
	set->mode = mode;
	return 1;
}

void wiiu_lock30_reset(struct wiiu_lock30 *lock, const struct wiiu_lock30_settings *set, double now_ms)
{
	memset(lock, 0, sizeof(*lock));
	lock->set = *set;
	lock->locked = set->mode == 1;
	lock->started_ms = lock->since_ms = lock->bucket_start_ms = now_ms;
}

/* the sums over the last `seconds` closed seconds */
static void lock30_sum(const struct wiiu_lock30 *lock, double seconds, unsigned *frames, unsigned *slow, unsigned *over_hold, unsigned *over_unlock, int *enough)
{
	unsigned want = (unsigned)(seconds + 0.5), i;

	*frames = *slow = *over_hold = *over_unlock = 0;
	*enough = lock->h_count >= want;
	if (want > lock->h_count)
		want = lock->h_count;
	for (i = 0; i < want; i++)
	{
		const unsigned at = (lock->h_next + WIIU_LOCK30_WINDOWS - 1 - i) % WIIU_LOCK30_WINDOWS;

		*frames += lock->h_frames[at];
		*slow += lock->h_slow[at];
		*over_hold += lock->h_hold[at];
		*over_unlock += lock->h_unlock[at];
	}
}

static void lock30_switch(struct wiiu_lock30 *lock, int locked, double now_ms, const char *reason)
{
	unsigned i;

	lock->locked = locked;
	lock->since_ms = now_ms;
	lock->switches_total++;
	for (i = WIIU_LOCK30_SWITCHES - 1; i > 0; i--)
		lock->switch_ms[i] = lock->switch_ms[i - 1];
	lock->switch_ms[0] = now_ms;
	lock->changed = 1;
	snprintf(lock->reason, sizeof(lock->reason), "%s", reason);
	/* (the new state starts on fresh evidence) */
	lock->h_count = 0;
	lock->h_next = 0;
}

static void lock30_decide(struct wiiu_lock30 *lock, double now_ms)
{
	const struct wiiu_lock30_settings *set = &lock->set;
	unsigned frames, slow, over_hold, over_unlock, recent = 0, i;
	int enough;

	if (now_ms - lock->started_ms < set->quiet_s * 1000.0 || now_ms - lock->since_ms < set->dwell_s * 1000.0)
		return;
	for (i = 0; i < WIIU_LOCK30_SWITCHES; i++)
		if (lock->switch_ms[i] > 0.0 && now_ms - lock->switch_ms[i] < set->switch_window_s * 1000.0)
			recent++;
	if (recent >= set->max_switches)
		return;
	if (!lock->locked)
	{
		lock30_sum(lock, set->lock_s, &frames, &slow, &over_hold, &over_unlock, &enough);
		if (enough && frames >= (unsigned)(10.0 * set->lock_s) && (double)slow >= set->lock_share * (double)frames && (double)over_hold <= 0.20 * (double)frames)
		{
			char reason[200];

			snprintf(reason, sizeof(reason), "%.0f%% of the frames of %.0f s took over %.1f ms and %.0f%% over %.1f ms: 45 is out of reach and 30 is in", 100.0 * slow / frames, set->lock_s,
				set->lock_over_ms, 100.0 * over_hold / frames, set->hold_ms);
			lock30_switch(lock, 1, now_ms, reason);
		}
	}
	else
	{
		lock30_sum(lock, set->unlock_s, &frames, &slow, &over_hold, &over_unlock, &enough);
		if (enough && frames >= (unsigned)(8.0 * set->unlock_s) && (double)over_unlock <= set->unlock_share * (double)frames)
		{
			char reason[200];

			snprintf(reason, sizeof(reason), "only %.0f%% of the frames of %.0f s took over %.1f ms: 60 is in reach", 100.0 * over_unlock / frames, set->unlock_s, set->unlock_over_ms);
			lock30_switch(lock, 0, now_ms, reason);
			return;
		}
		lock30_sum(lock, set->lock_s, &frames, &slow, &over_hold, &over_unlock, &enough);
		if (enough && frames >= (unsigned)(8.0 * set->lock_s) && (double)over_hold >= 0.50 * (double)frames)
		{
			char reason[200];

			snprintf(reason, sizeof(reason), "%.0f%% of the frames of %.0f s took over %.1f ms: a locked 30 does not hold", 100.0 * over_hold / frames, set->lock_s, set->hold_ms);
			lock30_switch(lock, 0, now_ms, reason);
		}
	}
}

int wiiu_lock30_frame(struct wiiu_lock30 *lock, double work_ms, double now_ms)
{
	const struct wiiu_lock30_settings *set = &lock->set;

	lock->changed = 0;
	if (set->mode == 0)
		return 0;
	if (set->mode == 1)
		return 1;
	if (now_ms < lock->bucket_start_ms)
		lock->bucket_start_ms = now_ms;
	if (now_ms - lock->bucket_start_ms > 5000.0)
	{
		/* (a gap in the frames: a load, the system's menu: what came before is not evidence of what comes) */
		lock->bucket_start_ms = now_ms;
		lock->frames = lock->slow = lock->over_hold = lock->over_unlock = 0;
		lock->h_count = 0;
		lock->h_next = 0;
	}
	while (now_ms - lock->bucket_start_ms >= 1000.0)
	{
		lock->h_frames[lock->h_next] = lock->frames;
		lock->h_slow[lock->h_next] = lock->slow;
		lock->h_hold[lock->h_next] = lock->over_hold;
		lock->h_unlock[lock->h_next] = lock->over_unlock;
		lock->h_next = (lock->h_next + 1) % WIIU_LOCK30_WINDOWS;
		if (lock->h_count < WIIU_LOCK30_WINDOWS)
			lock->h_count++;
		lock->frames = lock->slow = lock->over_hold = lock->over_unlock = 0;
		lock->bucket_start_ms += 1000.0;
		lock30_decide(lock, lock->bucket_start_ms);
		if (lock->changed)
		{
			lock->frames = lock->slow = lock->over_hold = lock->over_unlock = 0;
			lock->bucket_start_ms = now_ms;
			break;
		}
	}
	if (work_ms > 100.0)
	{
		/* (a hitch is not evidence either way) */
		lock->hitches++;
	}
	else
	{
		lock->frames++;
		lock->slow += work_ms > set->lock_over_ms;
		lock->over_hold += work_ms > set->hold_ms;
		lock->over_unlock += work_ms > set->unlock_over_ms;
	}
	return lock->locked;
}
