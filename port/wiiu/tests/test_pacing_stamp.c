/* wiiu_pacing.c: the display-clock stamp and the locked 30 rule (halo/pacing_stamp.txt, halo/pacing_lock.txt), on a model of the display pipeline.

The model: the game thread starts a frame at S, builds it in w ms (a base, plus a tick's share on the frames that carry one, plus noise), the swap is made at Z = S + w, the
frame is ready for a flip `GPU_MS` later, and the display flips it at the first vsync after that and a refresh after the flip before at least; the swap waits for the flip of
the one before it (one swap in flight, as present() does) so the next frame starts when both are done. The GX2 thread looks at the flips when it makes a swap and while it
waits, so the flip of swap n is seen at max(its time, the time of swap n + 1). What the picture should show at a flip is the time of that flip less a constant (D - T
constant): the "judder" of a stamping is the rms of D - T over the run, the mean taken out, T being the clock step of the frame plus the move. The program prints the figures and
exits 1 if a check fails. */

#include "../src/wiiu_pacing.h"

#include <math.h>
#include <stdint.h>
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

#define REFRESH (1000.0 / 60.0)
#define GPU_MS 4.0

static uint64_t random_state = 88172645463325252ULL;

static double uniform(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 7;
	random_state ^= random_state << 17;
	return (double)(random_state >> 11) / 9007199254740992.0;
}

static double gauss(double sigma)
{
	return sigma * (uniform() + uniform() + uniform() + uniform() + uniform() + uniform() - 3.0) * 1.4142135623730951;
}

struct scenario
{
	double base_ms, tick_ms, sigma_ms;
	int tell_class; /* the frame's class is given to the stamp (a tick frame is 1), or always 0 */
	int mode;
	unsigned frames;
	int walk; /* the base moves at random (a prediction that cannot be right) */
};

struct result
{
	double raw_rms, stamped_rms; /* the model's own measure of the real D - T, with the move that was applied (mode 1) and with none */
	double fps;
	double shrink;
	unsigned scored;
	unsigned backward;
};

static struct wiiu_stamp stamp;

static struct result run_pipeline(const struct scenario *s)
{
	struct result r;
	static double flip_time[100000], swap_time[100000];
	double t = 100.0, last_flip = 0.0;
	double sum_raw = 0, sum_raw2 = 0, sum_st = 0, sum_st2 = 0, base = s->base_ms, first_flip = 0, last_seen = 0;
	unsigned k, observed = 0, counted = 0;
	const double grid0 = 3.0;

	memset(&r, 0, sizeof(r));
	wiiu_stamp_reset(&stamp, s->mode, 0.0);
	for (k = 0; k < s->frames && k < 100000; k++)
	{
		const unsigned klass = (k & 1) ? 1u : 0u;
		const double start = t;
		unsigned sequence;
		double move, w, z, ready, flip;

		/* the flips the GX2 thread has seen by now (it looks when it makes a swap, and while it waits for the one before) are told to the stamp */
		while (observed + 1 < k)
		{
			const double seen = flip_time[observed] > swap_time[observed + 1] ? flip_time[observed] : swap_time[observed + 1];

			if (seen > start)
				break;
			wiiu_stamp_flip(&stamp, flip_time[observed], observed + 1);
			observed++;
		}
		move = wiiu_stamp_begin(&stamp, start, s->tell_class ? klass : 0u, &sequence);
		w = base + (klass ? s->tick_ms : 0.0) + gauss(s->sigma_ms);
		if (w < 4.0)
			w = 4.0;
		if (s->walk)
		{
			base += gauss(1.0);
			if (base < 8.0)
				base = 8.0;
			if (base > 30.0)
				base = 30.0;
		}
		z = start + w;
		ready = z + GPU_MS;
		flip = grid0 + ceil((ready - grid0) / REFRESH - 1e-9) * REFRESH;
		if (flip < last_flip + REFRESH - 1e-6)
			flip = last_flip + REFRESH;
		wiiu_stamp_swap(&stamp, sequence, z, k + 1);
		swap_time[k] = z;
		flip_time[k] = flip;
		last_flip = flip;
		/* the present waits while a second swap is in flight: the next frame starts when the flip of the swap before is done (or at the swap) */
		t = z;
		if (k > 0 && flip_time[k - 1] > t)
			t = flip_time[k - 1];
		if (k >= 100)
		{
			/* the figures: the actual D - T, T being start + move (the move as applied) */
			const double raw = flip - start;
			const double stamped = flip - (start + move);

			sum_raw += raw;
			sum_raw2 += raw * raw;
			sum_st += stamped;
			sum_st2 += stamped * stamped;
			counted++;
			if (!first_flip)
				first_flip = flip;
			last_seen = flip;
		}
	}
	if (counted)
	{
		const double mean_raw = sum_raw / counted, mean_st = sum_st / counted;

		r.raw_rms = sqrt(sum_raw2 / counted - mean_raw * mean_raw);
		r.stamped_rms = sqrt(sum_st2 / counted - mean_st * mean_st);
		r.fps = 1000.0 * counted / (last_seen - first_flip);
	}
	r.shrink = stamp.shrink;
	r.scored = stamp.scored;
	r.backward = stamp.window_backward;
	return r;
}

/* ---------- the locked 30 */

static unsigned lock_switches;
static double first_lock_s, first_unlock_s;
static double min_gap_s;

/* frames of `work_ms` (alternating with `other_ms` every `block_s` seconds if block_s > 0), 30 a second while locked, 60 while not, from `from_s` to `to_s` of the run */
static void lock_run(struct wiiu_lock30 *lock, double from_s, double to_s, double work_ms, double other_ms, double block_s, double hitch_every_s)
{
	double now = from_s * 1000.0, next_hitch = from_s * 1000.0 + hitch_every_s * 1000.0, last_switch = -1e9;

	while (now < to_s * 1000.0)
	{
		const int other = block_s > 0.0 && ((long)(now / 1000.0 / block_s) & 1);
		double work = other ? other_ms : work_ms;
		int was = lock->locked, locked;

		if (hitch_every_s > 0.0 && now >= next_hitch)
		{
			work = 250.0;
			next_hitch += hitch_every_s * 1000.0;
		}
		locked = wiiu_lock30_frame(lock, work, now);
		if (locked != was)
		{
			if (last_switch > -1e8 && (now - last_switch) / 1000.0 < min_gap_s)
				min_gap_s = (now - last_switch) / 1000.0;
			last_switch = now;
			lock_switches++;
			if (locked && first_lock_s < 0)
				first_lock_s = now / 1000.0;
			if (!locked && first_unlock_s < 0)
				first_unlock_s = now / 1000.0;
		}
		now += locked ? 33.333 : 16.667;
	}
}

static void lock_start(struct wiiu_lock30 *lock, int mode)
{
	struct wiiu_lock30_settings set;

	wiiu_lock30_defaults(&set);
	set.mode = mode;
	wiiu_lock30_reset(lock, &set, 0.0);
	lock_switches = 0;
	first_lock_s = first_unlock_s = -1.0;
	min_gap_s = 1e9;
}

int main(void)
{
	struct result r;
	struct scenario s;
	char text[400];
	unsigned sequence;
	struct wiiu_lock30 lock;
	struct wiiu_lock30_settings set;
	int mode;
	double test_ms;

	memset(&s, 0, sizeof(s));
	s.frames = 4000;
	s.mode = 1;

	/* ---- the stamp */
	/* A: a steady 22 ms frame with 0.5 ms of noise: the vsync it lands on is predictable, and the stamp takes out most of the judder */
	s.base_ms = 22.0;
	s.sigma_ms = 0.5;
	r = run_pipeline(&s);
	printf("A steady 22 ms +-0.5: %.1f fps, judder rms %.2f ms as drawn, %.2f stamped (shrink %.2f, %u scored)\n", r.fps, r.raw_rms, r.stamped_rms, r.shrink, r.scored);
	CHECK(r.raw_rms > 4.0 && r.stamped_rms < 0.65 * r.raw_rms && r.shrink > 0.7);
	/* B: a tick frame every second frame, 6 ms longer, told to the stamp as its class: the same, with 1 ms of noise */
	s.base_ms = 17.0;
	s.tick_ms = 6.0;
	s.sigma_ms = 1.0;
	s.tell_class = 1;
	r = run_pipeline(&s);
	printf("B ticks every second frame (+6), class told, 17 ms +-1: %.1f fps, rms %.2f as drawn, %.2f stamped (shrink %.2f)\n", r.fps, r.raw_rms, r.stamped_rms, r.shrink);
	CHECK(r.stamped_rms < 0.85 * r.raw_rms);
	/* C: the same ticks, the class not told, and 3 ms of noise: the stamp cannot know, and does no harm (the shrinkage) */
	s.tell_class = 0;
	s.sigma_ms = 3.0;
	r = run_pipeline(&s);
	printf("C ticks, class not told, noise 3 ms: %.1f fps, rms %.2f as drawn, %.2f stamped (shrink %.2f)\n", r.fps, r.raw_rms, r.stamped_rms, r.shrink);
	CHECK(r.stamped_rms < 1.06 * r.raw_rms);
	/* D: a frame time that walks: the prediction is no good, and the stamp does no harm */
	memset(&s, 0, sizeof(s));
	s.frames = 4000;
	s.mode = 1;
	s.base_ms = 20.0;
	s.sigma_ms = 2.0;
	s.walk = 1;
	r = run_pipeline(&s);
	printf("D a random walk of 8 to 30 ms: %.1f fps, rms %.2f as drawn, %.2f stamped (shrink %.2f)\n", r.fps, r.raw_rms, r.stamped_rms, r.shrink);
	CHECK(r.stamped_rms < 1.06 * r.raw_rms);
	/* E: 60 fps: every frame flips a refresh after the one before, the judder is nil either way */
	s.walk = 0;
	s.base_ms = 11.0;
	s.sigma_ms = 0.5;
	r = run_pipeline(&s);
	printf("E 11 ms: %.1f fps, rms %.2f as drawn, %.2f stamped\n", r.fps, r.raw_rms, r.stamped_rms);
	CHECK(r.fps > 59.0 && r.stamped_rms < r.raw_rms + 0.3);
	/* F: measure only (mode 2) moves nothing: the stamped figure it logs is what mode 1 would have done, the picture is as drawn */
	s.base_ms = 22.0;
	s.mode = 2;
	r = run_pipeline(&s);
	printf("F measure only: rms %.2f as drawn, %.2f stamped (the model applies no move)\n", r.raw_rms, r.stamped_rms);
	CHECK(fabs(r.raw_rms - r.stamped_rms) < 1e-9);
	CHECK(wiiu_stamp_format(&stamp, text, sizeof(text)) > 100 && strstr(text, "measured"));
	{
		/* (two lines, each under the log's line) */
		const char *newline = strchr(text, '\n');

		CHECK(newline && (size_t)(newline - text) < 190 && strlen(newline + 1) < 190 && !strchr(newline + 1, '\n'));
	}
	printf("%s\n", text);
	CHECK(wiiu_stamp_format(&stamp, text, sizeof(text)) == 0);
	/* the learnt numbers: the swap-to-ready time the model has (4 ms) and the display's period (the model's grid is 16.667) */
	s.mode = 1;
	s.base_ms = 22.0;
	s.sigma_ms = 0.5;
	r = run_pipeline(&s);
	CHECK(stamp.gpu_best >= 3 && stamp.gpu_best <= 5);
	CHECK(fabs(stamp.refresh_ms - REFRESH) < 0.01);
	/* off: nothing is predicted */
	wiiu_stamp_reset(&stamp, 0, 0.0);
	CHECK(wiiu_stamp_begin(&stamp, 10.0, 0, &sequence) == 0.0 && sequence == 1);

	/* ---- the picture's time: never backward, never outside the pair, a held pair left alone */
	{
		unsigned i;
		double previous = -1e9, shown;
		long tick = 100;
		double clock_ms = 0.0, fraction;

		wiiu_stamp_reset(&stamp, 1, 0.0);
		for (i = 0; i < 3000; i++)
		{
			const double move = (uniform() - 0.5) * 33.0;
			double base;

			clock_ms += 16.667;
			while (clock_ms >= 33.333)
			{
				clock_ms -= 33.333;
				tick++;
			}
			fraction = wiiu_stamp_picture(&stamp, tick, clock_ms / 33.333, move, 0);
			base = ((double)tick - 1.0) * 33.3333333333;
			shown = base + fraction * 33.3333333333;
			if (!(fraction >= 0.0 && fraction <= 1.0))
				printf("fraction %.9f at %u (tick %ld clock %.6f move %.3f)\n", fraction, i, tick, clock_ms, move);
			CHECK(fraction >= 0.0 && fraction <= 1.0);
			CHECK(shown >= previous - 1e-6);
			previous = shown;
		}
		CHECK(stamp.window_clamped > 0 && stamp.window_backward > 0);
		/* a held pair (the tick thread late: the fraction is 1) is as it was */
		CHECK(wiiu_stamp_picture(&stamp, tick, 1.0, 10.0, 1) == 1.0 && stamp.window_held == 1);
		CHECK(wiiu_stamp_picture(&stamp, tick, 1.0, -10.0, 0) == 1.0);
		/* a new map's clock (far back) is taken */
		fraction = wiiu_stamp_picture(&stamp, 3, 0.5, 0.0, 0);
		CHECK(fraction == 0.5);
		/* a move of 0 is the identity */
		wiiu_stamp_reset(&stamp, 1, 0.0);
		for (i = 0; i < 100; i++)
			CHECK(wiiu_stamp_picture(&stamp, 10 + i / 2, (i % 2) * 0.5, 0.0, 0) == (i % 2) * 0.5);
	}

	/* ---- the card */
	CHECK(wiiu_stamp_parse("1\n", &mode, &test_ms) && mode == 1);
	CHECK(wiiu_stamp_parse("2", &mode, &test_ms) && mode == 2);
	CHECK(wiiu_stamp_parse("3 8.5", &mode, &test_ms) && mode == 3 && test_ms == 8.5);
	CHECK(wiiu_stamp_parse("0", &mode, &test_ms) && mode == 0);
	CHECK(!wiiu_stamp_parse("4", &mode, &test_ms) && mode == 0);
	CHECK(!wiiu_stamp_parse("-1", &mode, &test_ms) && mode == 0);
	CHECK(!wiiu_stamp_parse("on", &mode, &test_ms) && mode == 0);
	/* a test move is the move, at any time (mode 3) */
	wiiu_stamp_reset(&stamp, 3, 5.0);
	CHECK(wiiu_stamp_begin(&stamp, 1.0, 0, &sequence) == 5.0);

	/* ---- the locked 30 */
	/* 12 ms frames: never locks */
	lock_start(&lock, 2);
	lock_run(&lock, 0, 600, 12.0, 0, 0, 0);
	CHECK(lock_switches == 0 && !lock.locked);
	/* 25 ms frames: not in the first 20 s, then six seconds of evidence, then locked, and stays so */
	lock_start(&lock, 2);
	lock_run(&lock, 0, 600, 25.0, 0, 0, 0);
	printf("L2 25 ms from the start: locked at %.1f s, %u switches\n", first_lock_s, lock_switches);
	CHECK(first_lock_s >= 20.0 && first_lock_s <= 28.0 && lock_switches == 1 && lock.locked);
	CHECK(strstr(lock.reason, "45 is out of reach") != NULL);
	/* 36 ms frames: 30 would not hold, so it does not lock */
	lock_start(&lock, 2);
	lock_run(&lock, 0, 600, 36.0, 0, 0, 0);
	CHECK(lock_switches == 0);
	/* locked at 25 ms, then 12 ms frames: unlocks after the dwell and twelve seconds of evidence, and stays unlocked */
	lock_start(&lock, 2);
	lock_run(&lock, 0, 100, 25.0, 0, 0, 0);
	CHECK(lock.locked);
	lock_run(&lock, 100, 400, 12.0, 0, 0, 0);
	printf("L4 back to 12 ms at 100 s: unlocked at %.1f s, %u switches (%s)\n", first_unlock_s, lock_switches, lock.reason);
	CHECK(first_unlock_s >= 110.5 && first_unlock_s <= 120.0 && lock_switches == 2 && !lock.locked);
	/* locked at 25 ms, then 18 ms frames (between the thresholds): stays locked */
	lock_start(&lock, 2);
	lock_run(&lock, 0, 100, 25.0, 0, 0, 0);
	lock_run(&lock, 100, 700, 18.0, 0, 0, 0);
	CHECK(lock_switches == 1 && lock.locked);
	/* locked at 25 ms, then 40 ms frames: a locked 30 does not hold, so it lets go */
	lock_start(&lock, 2);
	lock_run(&lock, 0, 100, 25.0, 0, 0, 0);
	lock_run(&lock, 100, 400, 40.0, 0, 0, 0);
	CHECK(lock_switches == 2 && !lock.locked && strstr(lock.reason, "does not hold"));
	/* a scene on the edge: 22 ms for fourteen seconds, 15 ms for fourteen seconds, over and over: the cadence changes at most four times in five minutes, and never twice within 15 s */
	lock_start(&lock, 2);
	lock_run(&lock, 0, 300, 22.0, 15.0, 14.0, 0);
	printf("L6 22 and 15 ms in turn every 14 s for 300 s: %u switches, the nearest %.1f s apart\n", lock_switches, min_gap_s);
	CHECK(lock_switches <= 4 && min_gap_s >= 15.0);
	/* hitches (a load, a save) in a quiet scene are not evidence */
	lock_start(&lock, 2);
	lock_run(&lock, 0, 600, 12.0, 0, 0, 1.0);
	CHECK(lock_switches == 0 && lock.hitches > 100);
	lock_start(&lock, 2);
	lock_run(&lock, 0, 600, 25.0, 0, 0, 1.0);
	CHECK(lock_switches == 1 && lock.locked); /* (25 ms frames with a hitch a second still lock: the hitches are left out of both sides) */
	/* a gap in the frames (the HOME menu) forgets what came before */
	lock_start(&lock, 2);
	lock_run(&lock, 0, 15, 25.0, 0, 0, 0);
	CHECK(!lock.locked && lock.h_count >= 4);
	wiiu_lock30_frame(&lock, 25.0, 40000.0);
	CHECK(lock.h_count == 0);
	/* a reset (a map's start) is quiet again */
	lock_start(&lock, 2);
	lock_run(&lock, 0, 100, 25.0, 0, 0, 0);
	CHECK(lock.locked);
	wiiu_lock30_reset(&lock, &lock.set, 100000.0);
	CHECK(!lock.locked);
	{
		double now = 100000.0;
		int locked_early = 0;

		while (now < 100000.0 + 19000.0)
		{
			locked_early |= wiiu_lock30_frame(&lock, 25.0, now);
			now += 16.667;
		}
		CHECK(!locked_early);
	}
	/* always and never */
	lock_start(&lock, 1);
	CHECK(wiiu_lock30_frame(&lock, 5.0, 10.0) == 1 && lock.locked);
	lock_start(&lock, 0);
	CHECK(wiiu_lock30_frame(&lock, 60.0, 10.0) == 0);

	/* the card */
	CHECK(wiiu_lock30_parse(&set, "2\n") && set.mode == 2 && set.lock_over_ms == 20.5 && set.dwell_s == 15.0);
	CHECK(wiiu_lock30_parse(&set, "1") && set.mode == 1);
	CHECK(wiiu_lock30_parse(&set, "2 22 0.5 8 15 0.05 20") && set.lock_over_ms == 22.0 && set.lock_share == 0.5 && set.lock_s == 8.0 && set.unlock_over_ms == 15.0 && set.unlock_share == 0.05 &&
		set.unlock_s == 20.0);
	CHECK(wiiu_lock30_parse(&set, "0") && set.mode == 0);
	CHECK(!wiiu_lock30_parse(&set, "3") && set.mode == 0);
	CHECK(!wiiu_lock30_parse(&set, "-1") && set.mode == 0);
	CHECK(!wiiu_lock30_parse(&set, "2 5") && set.mode == 0); /* (a slow frame under 14 ms: out of range) */
	CHECK(!wiiu_lock30_parse(&set, "2 20 0.5 6 20") && set.mode == 0); /* (the unlock level at or over the lock level: no gap) */
	CHECK(!wiiu_lock30_parse(&set, "2 20 2.0") && set.mode == 0);

	if (failures)
	{
		printf("%d checks failed\n", failures);
		return 1;
	}
	printf("pacing stamp and locked 30: all checks passed\n");
	return 0;
}
