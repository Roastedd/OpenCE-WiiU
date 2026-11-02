/*
TICK_OVERLAP.C

The overlap of the game's ticks with its drawing (tick_overlap.h).
*/

#include "cseries.h"
#include "tick_overlap.h"

#ifdef HALO_WIIU

#include "cseries/errors.h"
#include "tick_input.h"

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* the platform layer's (port/wiiu/src/wiiu_shell.c, wiiu_study.c; the digest harness's glue on the host) */
void platform_log(char const *format, ...);
long wiiu_study_number(const char *name, long fallback);
void wiiu_fpu_setup(void);
void wiiu_thread_off_game_core(void);
void wiiu_tag_thread(const char *name);
int wiiu_tick_thread_priority(void);
void wiiu_tick_thread_gate(void);
void wiiu_foreground_gate(int is_game_thread);
void wiiu_study_tick_thread_report(void);
void wiiu_tick_idle_set(int idle);
void wiiu_game_waiting_set(int waiting);
unsigned long long wiiu_now_ticks(void);
unsigned long wiiu_ticks_per_second(void);
int halo_interpolation_enabled(void);

/* the main loop's (main.c) */
boolean main_overlap_eligible(void);

/* ---------- constants */

enum
{
	/* the same 8 MiB as the game's own thread: port/wiiu/src/wiiu_shell.c */
	TICK_OVERLAP_STACK_BYTES = 8 * 1024 * 1024,
	/* a log line of the numbers every this many seconds of the game thread's frames */
	TICK_OVERLAP_LOG_SECONDS = 5,
};

/* the words the two threads read of each other: single words, read and written with the builtins (the order that matters is the job's: its writes
before it is over, and the game thread's before it is posted; the others only say what the other thread may do) */
#define LOAD(field) __atomic_load_n(&(field), __ATOMIC_RELAXED)
#define STORE(field, value) __atomic_store_n(&(field), (value), __ATOMIC_RELAXED)

/* ---------- structures */

struct tick_overlap_job
{
	real dt;
	long ticks_elapsed;
	real leftover_after;
	long time_before;
	long previous_job_tick;
	unsigned long sequence;
	/* the result */
	long ticks_run;
	unsigned long long started_ticks;
	unsigned long long ended_ticks;
};

/* ---------- globals */

volatile int tick_overlap_markers_split = 0;
volatile int tick_overlap_listeners_frozen = 0;

/* (data.c's lock: a thread that waits long for it gives the core up) */
void sched_yield_for_data(
	void);

void sched_yield_for_data(
	void)
{
	sched_yield();
}

static struct
{
	/* the switch */
	int requested; /* -1 not read */
	int enabled; /* -1 not decided */
	/* the thread */
	int state; /* -1 not tried, 0 could not be started, 1 running */
	pthread_t thread;
	pthread_mutex_t lock;
	pthread_cond_t posted;
	volatile boolean pending;
	tick_overlap_work work;
	void *argument;
	volatile int idle; /* the tick thread waits for work */
	volatile int game_waiting; /* the game thread waits for the work in flight */
	/* the overlap's job */
	struct tick_overlap_job job;
	volatile unsigned long posted_jobs; /* jobs posted so far (the tags of the game thread's pushes) */
	volatile int in_overlap_job; /* the tick thread is in an overlap job (its drains stop at the tag) */
	volatile unsigned long running_sequence;
	boolean job_in_flight; /* (the game thread's) */
	boolean frame_active; /* the frame about to be drawn, or being drawn, is the overlap's: from the post to the frame's end */
	boolean pair_active; /* ... and its picture is of a pair of the ring */
	volatile int forcing;
	/* the view of the frame */
	long view_tick;
	real view_fraction;
	boolean job_collected; /* a job has finished since the last sync point */
	/* the numbers (the game thread's, but for the tick thread's, which it adds under the lock at a job's end) */
	unsigned long long log_started_ticks;
	unsigned long frames, overlap_frames, serial_frames, primed_frames, held_frames, no_pair_frames;
	unsigned long long wait_ticks, wait_max_ticks, frame_ticks, frame_max_ticks, sync_ticks, sync_wait_ticks, post_ticks, draw_ticks, draw_max_ticks, serial_wait_ticks;
	unsigned long waits_long, serial_waits, active_frames, clock_disagreements;
	unsigned long jobs, ticks, ticks_total;
	unsigned long long job_ticks, job_max_ticks, tick_work_ticks;
	unsigned long long ahead_sum; /* (clock tick after the job) - (tick drawn), in thousandths, summed over overlap frames */
	unsigned long long frame_started_ticks;
	unsigned long long idle_started_ticks;
	unsigned long push_waits, pool_waits;
	unsigned long long blend_ticks;
	unsigned long blends;
	unsigned long capture_ticks, publish_ticks, tick_idle_ticks; /* (the tick thread's: one word each, so that a read and an add by another thread cannot tear) */
	unsigned long captures, publishes;
	unsigned long long first_ticks;
	unsigned long reports;
} o = { -1, -1, -1 };

/* ---------- the switch */

boolean tick_overlap_requested(
	void)
{
	if (o.requested < 0)
	{
		const long overlap = wiiu_study_number("tick_overlap.txt", 0);
		const long thread = wiiu_study_number("tick_thread.txt", 0);

		o.requested = overlap > 0 && thread > 0;
		if (overlap > 0 && thread <= 0)
			platform_log("tick overlap: off (halo/tick_overlap.txt is %ld but halo/tick_thread.txt is not on)", overlap);
	}
	return o.requested != 0;
}

unsigned long tick_overlap_stack_low = 0;
unsigned long tick_overlap_stack_size = 0;

/* the game thread's own stack (port/wiiu/src/wiiu_shell.c; the digest harness's glue on the host) */
void wiiu_thread_stack_range(unsigned long *low, unsigned long *size);

void tick_overlap_note_render_thread(
	void)
{
	unsigned long low = 0, size = 0;

	wiiu_thread_stack_range(&low, &size);
	tick_overlap_stack_low = low;
	tick_overlap_stack_size = size;
}

boolean tick_overlap_thread_is_tick(
	void)
{
	return o.state > 0 && pthread_equal(pthread_self(), o.thread);
}

/* ---------- the thread */

/* the time on the console's clock, in the clock's ticks (the host's elsewhere: microseconds): the numbers are kept in them and said in milliseconds by the log */
unsigned long long tick_overlap_now(
	void)
{
	return wiiu_now_ticks();
}

static void *tick_overlap_thread_run(
	void *unused)
{
	(void)unused;
	/* (what every thread that runs game code needs: wiiu_shell.h) */
	wiiu_fpu_setup();
	wiiu_thread_off_game_core();
	wiiu_tick_thread_priority();
	wiiu_tag_thread("ticks");
	wiiu_study_tick_thread_report();
	/* (no part in the profile: its stack of sections and the frame's record are the game thread's) */
	profile_thread_disable();
	pthread_mutex_lock(&o.lock);
	for (;;)
	{
		tick_overlap_work work;
		void *argument;
		unsigned long long idle_from;

		STORE(o.idle, 1);
		wiiu_tick_idle_set(1);
		idle_from = tick_overlap_now();
		while (!__atomic_load_n(&o.pending, __ATOMIC_ACQUIRE))
			pthread_cond_wait(&o.posted, &o.lock);
		STORE(o.idle, 0);
		wiiu_tick_idle_set(0);
		work = o.work;
		argument = o.argument;
		__atomic_add_fetch(&o.tick_idle_ticks, (unsigned long)(tick_overlap_now() - idle_from), __ATOMIC_RELAXED);
		pthread_mutex_unlock(&o.lock);
		/* (HOME and closing hold this thread before a job, as they hold the game thread at its safe points) */
		wiiu_tick_thread_gate();
		work(argument);
		pthread_mutex_lock(&o.lock);
		__atomic_store_n(&o.pending, FALSE, __ATOMIC_RELEASE);
	}
	return NULL;
}

boolean tick_overlap_start_thread(
	void)
{
	pthread_attr_t attributes;

	if (o.state >= 0)
		return o.state > 0;
	o.state = 0;
	tick_overlap_markers_split = 1;
	if (pthread_mutex_init(&o.lock, NULL) == 0 && pthread_cond_init(&o.posted, NULL) == 0 &&
		pthread_attr_init(&attributes) == 0)
	{
		pthread_attr_setstacksize(&attributes, TICK_OVERLAP_STACK_BYTES);
		if (pthread_create(&o.thread, &attributes, tick_overlap_thread_run, NULL) == 0)
			o.state = 1;
		pthread_attr_destroy(&attributes);
	}
	error(_error_silent, "study: the ticks on a thread of their own (halo/tick_thread.txt): %s", o.state ? "running" : "could not be started");
	platform_log("study: the ticks on a thread of their own on core 0 (tick_thread.txt): %s", o.state ? "running" : "could not be started");

	return o.state > 0;
}

/* the queues of the game thread that a producer waits for room in are drained here: the tick thread asks for it, and the game thread, waiting for
the tick thread, is the only one that can (tick_owners.c) */
long tick_owners_drain_waited(void);

/* waits for the work in flight to end. Between looks the game thread serves the queues that are full and the HOME gate: it is not drawing, so it may
be held, and the tick thread, held at its own gate, would never end the work otherwise. (It looks rather than sleeps on a condition: the platform's
time functions are not the game's, and a look every 100 microseconds costs a wait no more than that late.) */
static void tick_overlap_wait(void)
{
	unsigned long long started = 0;
	long looks = 0;

	if (!__atomic_load_n(&o.pending, __ATOMIC_ACQUIRE))
		return;
	started = tick_overlap_now();
	STORE(o.game_waiting, 1);
	wiiu_game_waiting_set(1);
	while (__atomic_load_n(&o.pending, __ATOMIC_ACQUIRE))
	{
		struct timespec pause = { 0, 100000 };

		/* (a few quick looks first: a job that is nearly over ends within them) */
		if (++looks > 20)
		{
			tick_owners_drain_waited();
			wiiu_foreground_gate(1);
			nanosleep(&pause, NULL);
		}
		else
		{
			sched_yield();
		}
	}
	STORE(o.game_waiting, 0);
	wiiu_game_waiting_set(0);
	{
		const unsigned long long took = tick_overlap_now() - started;

		o.wait_ticks += took;
		if (took > o.wait_max_ticks)
			o.wait_max_ticks = took;
		if (took > 4 * (unsigned long long)wiiu_ticks_per_second() / 1000)
			o.waits_long++;
	}
}

static void tick_overlap_post(
	tick_overlap_work work,
	void *argument)
{
	pthread_mutex_lock(&o.lock);
	o.work = work;
	o.argument = argument;
	__atomic_store_n(&o.pending, TRUE, __ATOMIC_RELEASE);
	pthread_cond_signal(&o.posted);
	pthread_mutex_unlock(&o.lock);
}

/* work given to the tick thread and waited for at once: the serial loop's frame (stage 1's mode), whose wait is counted apart from the overlap's */
void tick_overlap_run_and_wait(
	tick_overlap_work work,
	void *argument)
{
	const unsigned long long wait_before = o.wait_ticks;
	const unsigned long long max_before = o.wait_max_ticks;
	const unsigned long long long_before = o.waits_long;

	tick_overlap_post(work, argument);
	tick_overlap_wait();
	o.serial_wait_ticks += o.wait_ticks - wait_before;
	o.serial_waits++;
	o.wait_ticks = wait_before;
	o.wait_max_ticks = max_before;
	o.waits_long = (unsigned long)long_before;
}

/* ---------- the switch, decided */

boolean tick_overlap_enabled(
	void)
{
	if (o.enabled < 0)
	{
		o.enabled = 0;
		if (tick_overlap_requested())
		{
			int tick_snapshot_enabled(void);
					if (!tick_snapshot_enabled())
				platform_log("tick overlap: off (halo/tick_snapshot.txt is not on)");
			else if (!halo_interpolation_enabled())
				platform_log("tick overlap: off (display.interpolation is off: there are no frames between ticks to overlap)");
			else if (!tick_overlap_start_thread())
				platform_log("tick overlap: off (the tick thread could not be started)");
			else
			{
				/* (the queues are what makes a second thread safe to give the tick side to: at their owners' points, in order) */
				if (tick_owners_mode() < 2)
					platform_log("tick overlap: the tick owners' queues must be on in mode 2; they are mode %ld", tick_owners_mode());
				tick_owners_set_threaded(TRUE);
				tick_overlap_listeners_frozen = 1;
				o.enabled = 1;
				o.first_ticks = tick_overlap_now();
				platform_log("tick overlap: on (halo/tick_overlap.txt): the ticks run beside the drawing on core 0, the frame drawn from the ring one tick behind the clock");
			}
		}
	}
	return o.enabled > 0;
}

/* ---------- the queues */

boolean tick_overlap_forcing_drain(
	void)
{
	return LOAD(o.forcing) != 0;
}

boolean tick_overlap_defer_drain(
	struct tick_queue const *queue)
{
	if (o.enabled <= 0 || !LOAD(o.job_in_flight) || LOAD(o.forcing))
		return FALSE;
	if (queue->owner == TICK_OWNER_TICK || !tick_overlap_thread_is_render())
		return FALSE;
	/* (a producer waiting for room in it: now) */
	return __atomic_load_n(&queue->waiters, __ATOMIC_RELAXED) == 0;
}

unsigned long tick_overlap_push_tag(
	void)
{
	if (o.enabled > 0 && tick_overlap_thread_is_render())
		return LOAD(o.posted_jobs);
	return 0;
}

unsigned long tick_overlap_drain_limit(
	void)
{
	if (o.enabled > 0 && LOAD(o.in_overlap_job) && tick_overlap_thread_is_tick())
		return LOAD(o.running_sequence) - 1;
	return (unsigned long)-1;
}

boolean tick_overlap_owner_blocked(
	long owner)
{
	/* only the tick thread's queues: the game thread, their producer, finds the thread idle and has nothing to wait for. The game thread's own are
	served by the game thread, at its drain points and in its wait for the tick thread, whenever a producer waits (queue->waiters) */
	return o.state > 0 && owner == TICK_OWNER_TICK && LOAD(o.idle) != 0 && !__atomic_load_n(&o.pending, __ATOMIC_ACQUIRE) &&
		!tick_overlap_thread_is_tick();
}

boolean tick_overlap_owner_is_caller(
	long owner)
{
	if (o.state <= 0)
		return TRUE;
	if (owner == TICK_OWNER_TICK)
		return tick_overlap_thread_is_tick();
	return !tick_overlap_thread_is_tick();
}

boolean tick_overlap_frame_active(
	void)
{
	return o.pair_active;
}

boolean tick_overlap_job_in_flight(
	void)
{
	return LOAD(o.job_in_flight);
}

boolean tick_overlap_reading_published(
	void)
{
	return o.frame_active && tick_overlap_thread_is_render();
}

real tick_overlap_view_fraction(
	void)
{
	return o.view_fraction;
}

long tick_overlap_view_tick(
	void)
{
	return o.view_tick;
}

/* ---------- the frame */

/* the job that was in flight is over (the tick thread idle): its numbers, and what it left for the game thread, taken: the facing and the camera the
tick side published (copied, so that the next job's cannot tear them), and what the job asked of the game thread's queues, applied in one place,
whichever way the threads were timed */
static void tick_overlap_collect(void)
{
	void player_control_sync_display_facing(void);
	void observer_sync_published(void);

	if (LOAD(o.job_in_flight))
	{
		const unsigned long long took = o.job.ended_ticks - o.job.started_ticks;

		STORE(o.job_in_flight, FALSE);
		o.job_collected = TRUE;
		o.jobs++;
		o.ticks += (unsigned long)o.job.ticks_run;
		o.ticks_total += (unsigned long)o.job.ticks_run;
		/* the clock the game thread decided and the clock the tick thread ended with agree, or the picture's tick was chosen on a wrong count */
		if (o.job.ticks_run != o.job.ticks_elapsed || game_time_overlap_time() != o.job.time_before + o.job.ticks_run)
			o.clock_disagreements++;
		o.job_ticks += took;
		if (took > o.job_max_ticks)
			o.job_max_ticks = took;
	}
	if (o.job_collected)
	{
		o.job_collected = FALSE;
		STORE(o.forcing, 1);
		player_control_sync_display_facing();
		observer_sync_published();
		tick_owners_drain(TICK_OWNER_GAME);
		tick_owners_drain(TICK_OWNER_SOUND);
		tick_owners_drain(TICK_OWNER_RENDER);
		STORE(o.forcing, 0);
	}
}

void tick_overlap_sync_point(
	void)
{
	unsigned long long started;
	unsigned long long wait_before;

	if (!tick_overlap_enabled())
		return;
	wait_before = o.wait_ticks;
	started = tick_overlap_now();
	o.frame_started_ticks = started;
	tick_overlap_wait();
	tick_overlap_collect();
	/* (what the ticks of this frame hear: the listeners as the frame before left them) */
	sound_manager_freeze_listeners();
	o.sync_ticks += tick_overlap_now() - started;
	o.sync_wait_ticks += o.wait_ticks - wait_before;
}

/* the tick thread's job */
static void tick_overlap_job_work(
	void *argument)
{
	struct tick_overlap_job *job = (struct tick_overlap_job *)argument;

	job->started_ticks = tick_overlap_now();
	/* (a test aid: halo/tick_overlap_stall.txt, in milliseconds, holds each job back by up to that and a third of it again, so that the tick thread is late
	for the frame drawn beside it, as a long tick makes it) */
	{
		static long stall = -1;

		if (stall < 0)
			stall = wiiu_study_number("tick_overlap_stall.txt", 0);
		if (stall > 0)
		{
			const long milliseconds = stall + (long)(job->sequence % 3) * stall / 3;
			struct timespec pause = { milliseconds / 1000, (milliseconds % 1000) * 1000000L };

			nanosleep(&pause, NULL);
		}
	}
	STORE(o.running_sequence, job->sequence);
	STORE(o.in_overlap_job, 1);
	job->ticks_run = main_overlap_tick_side(job->dt, job->ticks_elapsed, job->leftover_after, job->previous_job_tick);
	STORE(o.in_overlap_job, 0);
	job->ended_ticks = tick_overlap_now();
}

static void tick_overlap_set_view(
	long tick,
	real fraction,
	long ticks_elapsed)
{
	o.view_tick = tick;
	o.view_fraction = fraction;
	game_time_set_view(TRUE, tick, (short)ticks_elapsed, fraction);
	render_interpolation_overlap_set(TRUE, tick);
	o.pair_active = TRUE;
}

boolean tick_overlap_post_frame(
	real dt)
{
	struct tick_overlap_job *job = &o.job;
	long time_now, ticks, want, tick, published;
	real leftover_after, speed, fraction;

	if (!tick_overlap_enabled())
		return FALSE;
	o.frames++;
	if (!main_overlap_eligible() || !game_time_overlap_step(dt, &time_now, &ticks, &leftover_after, &speed) || speed != 1.0f)
	{
		o.serial_frames++;
		return FALSE;
	}
	published = tick_snapshot_published_tick();
	tick_input_capture();
	memset(job, 0, sizeof(*job));
	job->dt = dt;
	job->ticks_elapsed = ticks;
	job->leftover_after = leftover_after;
	job->time_before = time_now;
	job->previous_job_tick = published;
	job->sequence = LOAD(o.posted_jobs) + 1;
	fraction = leftover_after * speed * TICKS_PER_SECOND;
	fraction = PIN(fraction, 0.0f, 1.0f);
	/* the picture is of the clock one tick behind: the tick before the one the job ends on, which the ring has if the tick thread was not behind */
	want = time_now + ticks - 1;
	tick = want < published ? want : published;
	o.frame_active = TRUE;
	STORE(o.posted_jobs, LOAD(o.posted_jobs) + 1);
	if (published != tick_snapshot_none() && tick_snapshot_pin(tick))
	{
		/* the tick thread was late (the tick wanted is not committed yet): the last pose is held, the pair at its end */
		if (tick < want)
		{
			fraction = 1.0f;
			o.held_frames++;
		}
		tick_overlap_set_view(tick, fraction, ticks);
		STORE(o.job_in_flight, TRUE);
		o.overlap_frames++;
		o.ahead_sum += (unsigned long long)((time_now + ticks - tick) * 1000);
		tick_overlap_post(tick_overlap_job_work, job);
		return TRUE;
	}
	/* no pair to draw yet (the first frame of a map, or after frames of the serial loop's that kept no ring): the job is run at once, and the
	picture is of the latest tick, as the serial loop's */
	tick_snapshot_unpin();
	STORE(o.job_in_flight, TRUE);
	o.primed_frames++;
	tick_overlap_post(tick_overlap_job_work, job);
	tick_overlap_wait();
	tick_overlap_collect();
	published = tick_snapshot_published_tick();
	want = game_time_overlap_time();
	tick = want < published ? want : published;
	if (published != tick_snapshot_none() && tick_snapshot_pin(tick))
	{
		if (tick < want)
			fraction = 1.0f;
		tick_overlap_set_view(tick, fraction, ticks);
	}
	else
	{
		o.no_pair_frames++;
		tick_snapshot_unpin();
		game_time_set_view(FALSE, 0, 0, 1.0f);
	}
	return TRUE;
}

/* the frame's work done (the sync point to the end of the draw), without the throttle and the present, which wait for the display */
void tick_overlap_draw_done(
	void)
{
	if (o.frame_active)
	{
		const unsigned long long took = tick_overlap_now() - o.frame_started_ticks;

		o.draw_ticks += took;
		if (took > o.draw_max_ticks)
			o.draw_max_ticks = took;
	}
}

/* the end of a frame, after its present: the view let go of, and the numbers said now and then */
void tick_overlap_frame_end(
	void)
{
	unsigned long long now;

	if (!o.frame_active)
		return;
	game_time_set_view(FALSE, 0, 0, 1.0f);
	render_interpolation_overlap_set(FALSE, 0);
	tick_snapshot_unpin();
	o.pair_active = FALSE;
	o.frame_active = FALSE;
	now = tick_overlap_now();
	o.active_frames++;
	o.frame_ticks += now - o.frame_started_ticks;
	if (now - o.frame_started_ticks > o.frame_max_ticks)
		o.frame_max_ticks = now - o.frame_started_ticks;
	if (!o.log_started_ticks)
		o.log_started_ticks = now;
	if (now - o.log_started_ticks >= (unsigned long long)TICK_OVERLAP_LOG_SECONDS * (unsigned long long)wiiu_ticks_per_second())
		tick_overlap_log(now);
}

void tick_overlap_map_reset(
	void)
{
	if (o.enabled <= 0)
		return;
	tick_overlap_wait();
	tick_overlap_collect();
	o.frame_active = FALSE;
	o.pair_active = FALSE;
	game_time_set_view(FALSE, 0, 0, 1.0f);
	render_interpolation_overlap_set(FALSE, 0);
	tick_snapshot_unpin();
}

void tick_overlap_note_blend(
	unsigned long long elapsed)
{
	o.blend_ticks += elapsed;
	o.blends++;
}

void tick_overlap_note_push_wait(
	void)
{
	__atomic_add_fetch(&o.push_waits, 1, __ATOMIC_RELAXED);
}

void tick_overlap_note_capture(
	unsigned long long elapsed)
{
	__atomic_add_fetch(&o.capture_ticks, (unsigned long)elapsed, __ATOMIC_RELAXED);
	__atomic_add_fetch(&o.captures, 1, __ATOMIC_RELAXED);
}

void tick_overlap_note_publish(
	unsigned long long elapsed)
{
	__atomic_add_fetch(&o.publish_ticks, (unsigned long)elapsed, __ATOMIC_RELAXED);
	__atomic_add_fetch(&o.publishes, 1, __ATOMIC_RELAXED);
}

/* the numbers, in the style of port/wiiu/src/wiiu_render_passes.c (a log line holds 199 characters) */
void tick_overlap_log(
	unsigned long long now)
{
	const double per_ms = (double)wiiu_ticks_per_second() / 1000.0;
	const double window = (double)(now - o.log_started_ticks);
	const double seconds = window / (per_ms * 1000.0);
	const double active = o.active_frames ? (double)o.active_frames : 1.0;
	const double overlap_frames = o.overlap_frames ? (double)o.overlap_frames : 1.0;
	const double jobs = o.jobs ? (double)o.jobs : 1.0;
	const double ticks = o.ticks ? (double)o.ticks : 1.0;
	const unsigned long captures = __atomic_exchange_n(&o.captures, 0, __ATOMIC_RELAXED);
	const unsigned long capture_ticks = __atomic_exchange_n(&o.capture_ticks, 0, __ATOMIC_RELAXED);
	const unsigned long publishes = __atomic_exchange_n(&o.publishes, 0, __ATOMIC_RELAXED);
	const unsigned long publish_ticks = __atomic_exchange_n(&o.publish_ticks, 0, __ATOMIC_RELAXED);
	const unsigned long tick_idle_ticks = __atomic_exchange_n(&o.tick_idle_ticks, 0, __ATOMIC_RELAXED);
	const unsigned long push_waits = __atomic_exchange_n(&o.push_waits, 0, __ATOMIC_RELAXED);

	platform_log("tick overlap (%.1f s, %lu frames: %lu overlapped, %lu serial, %lu primed, %lu held, %lu no pair): %.1f ticks a second, %.1f frames a second",
		seconds, o.frames, o.overlap_frames, o.serial_frames, o.primed_frames, o.held_frames, o.no_pair_frames, (double)o.ticks / seconds,
		(double)o.frames / seconds);
	platform_log("tick overlap, tick thread: %.2f ms a job (most %.2f), %.2f ms a tick (%.2f ticks a job), busy %.0f%% of the time, waiting for a job %.0f%%",
		(double)o.job_ticks / per_ms / jobs, (double)o.job_max_ticks / per_ms, (double)o.job_ticks / per_ms / ticks, ticks / jobs,
		100.0 * (double)o.job_ticks / window, 100.0 * (double)(tick_idle_ticks > window ? window : tick_idle_ticks) / window);
	platform_log("tick overlap, ahead of the picture: %.2f ticks on average (1 is the delay; more means the tick thread was late), %lu ticks in all this game, "
		"%lu jobs whose ticks were not the clock's decision",
		(double)o.ahead_sum / 1000.0 / overlap_frames, o.ticks_total, o.clock_disagreements);
	platform_log("tick overlap, game thread, ms a frame (%lu): %.2f (most %.2f) in all, %.2f (most %.2f) of work to the end of the draw, waiting for the "
		"tick thread %.2f (most %.2f, %lu waits over 4 ms), sync point %.3f",
		o.active_frames, (double)o.frame_ticks / per_ms / active, (double)o.frame_max_ticks / per_ms, (double)o.draw_ticks / per_ms / active,
		(double)o.draw_max_ticks / per_ms, (double)o.wait_ticks / per_ms / active, (double)o.wait_max_ticks / per_ms, o.waits_long,
		(double)(o.sync_ticks - o.sync_wait_ticks) / per_ms / active);
	if (o.serial_waits)
	{
		platform_log("tick overlap, serial frames: the game thread waited %.2f ms a frame for the tick thread (%lu frames)",
			(double)o.serial_wait_ticks / per_ms / (double)o.serial_waits, o.serial_waits);
	}
	platform_log("tick overlap, snapshot: capture %.3f ms a tick (%lu), publish %.3f ms a job (%lu), blends %.3f ms a frame (%lu objects), "
		"queue waits %lu",
		captures ? (double)capture_ticks / per_ms / (double)captures : 0.0, captures,
		publishes ? (double)publish_ticks / per_ms / (double)publishes : 0.0, publishes, (double)o.blend_ticks / per_ms / active, o.blends,
		push_waits);
	o.log_started_ticks = now;
	o.frames = o.overlap_frames = o.serial_frames = o.primed_frames = o.held_frames = o.no_pair_frames = 0;
	o.wait_ticks = o.wait_max_ticks = o.frame_ticks = o.frame_max_ticks = o.sync_ticks = o.sync_wait_ticks = o.post_ticks = o.draw_ticks = o.draw_max_ticks = 0;
	o.serial_wait_ticks = 0;
	o.waits_long = o.serial_waits = o.active_frames = 0;
	o.jobs = o.ticks = 0;
	o.job_ticks = o.job_max_ticks = o.tick_work_ticks = 0;
	o.ahead_sum = 0;
	o.blend_ticks = 0;
	o.blends = 0;
	o.reports++;
}

#else /* HALO_WIIU */

typedef int tick_overlap_not_for_this_platform;

#endif
