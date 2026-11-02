/*
TICK_OVERLAP.C

The overlap of the game's ticks with its drawing (tick_overlap.h).
*/

#include "cseries.h"
#include "tick_overlap.h"

#ifdef HALO_WIIU

#include "cseries/errors.h"
#include "game/game.h"
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
unsigned long long wiiu_now_us(void);
int wiiu_current_core(void);

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
	unsigned long long started_us;
	unsigned long long ended_us;
};

/* ---------- globals */

volatile int tick_overlap_markers_split = 0;

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
	int render_core;
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
	unsigned long long log_started_us;
	unsigned long frames, overlap_frames, serial_frames, primed_frames, held_frames, no_pair_frames;
	unsigned long long wait_us, wait_max_us, frame_us, frame_max_us, sync_us, post_us, draw_us;
	unsigned long waits_long;
	unsigned long jobs, ticks, ticks_total;
	unsigned long long job_us, job_max_us, tick_work_us;
	unsigned long long ahead_sum; /* (clock tick after the job) - (tick drawn), in thousandths, summed over overlap frames */
	unsigned long long frame_started_us;
	unsigned long long idle_started_us;
	unsigned long push_waits, pool_waits;
	unsigned long long blend_us;
	unsigned long blends;
	unsigned long capture_us, publish_us, tick_idle_us; /* (the tick thread's: one word each, so that a read and an add by another thread cannot tear) */
	unsigned long captures, publishes;
	unsigned long long first_us;
	unsigned long reports;
} o = { -1, -1, 1, -1 };

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

int tick_overlap_render_core(
	void)
{
	return o.render_core;
}

boolean tick_overlap_thread_is_render(
	void)
{
	return wiiu_current_core() == o.render_core;
}

boolean tick_overlap_thread_is_tick(
	void)
{
	return o.state > 0 && pthread_equal(pthread_self(), o.thread);
}

/* ---------- the thread */

/* the time since a start, in microseconds, on the console's clock (the host's elsewhere) */
unsigned long long tick_overlap_now_us(
	void)
{
	return wiiu_now_us();
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

		o.idle = 1;
		wiiu_tick_idle_set(1);
		idle_from = tick_overlap_now_us();
		while (!o.pending)
			pthread_cond_wait(&o.posted, &o.lock);
		o.idle = 0;
		wiiu_tick_idle_set(0);
		work = o.work;
		argument = o.argument;
		__atomic_add_fetch(&o.tick_idle_us, (unsigned long)(tick_overlap_now_us() - idle_from), __ATOMIC_RELAXED);
		pthread_mutex_unlock(&o.lock);
		/* (HOME and closing hold this thread before a job, as they hold the game thread at its safe points) */
		wiiu_tick_thread_gate();
		work(argument);
		pthread_mutex_lock(&o.lock);
		o.pending = FALSE;
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
	o.render_core = wiiu_current_core();
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

	if (!o.pending)
		return;
	started = tick_overlap_now_us();
	o.game_waiting = 1;
	wiiu_game_waiting_set(1);
	while (o.pending)
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
	o.game_waiting = 0;
	wiiu_game_waiting_set(0);
	/* (the job's writes, which happened before it said it was over) */
	pthread_mutex_lock(&o.lock);
	pthread_mutex_unlock(&o.lock);
	{
		const unsigned long long took = tick_overlap_now_us() - started;

		o.wait_us += took;
		if (took > o.wait_max_us)
			o.wait_max_us = took;
		if (took > 4000)
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
	o.pending = TRUE;
	pthread_cond_signal(&o.posted);
	pthread_mutex_unlock(&o.lock);
}

void tick_overlap_run_and_wait(
	tick_overlap_work work,
	void *argument)
{
	tick_overlap_post(work, argument);
	tick_overlap_wait();
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
				o.enabled = 1;
				o.first_us = tick_overlap_now_us();
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
	return o.forcing != 0;
}

boolean tick_overlap_defer_drain(
	struct tick_queue const *queue)
{
	if (o.enabled <= 0 || !o.job_in_flight || o.forcing)
		return FALSE;
	if (queue->owner == TICK_OWNER_TICK || !tick_overlap_thread_is_render())
		return FALSE;
	/* (a producer waiting for room in it: now) */
	return queue->waiters == 0;
}

unsigned long tick_overlap_push_tag(
	void)
{
	if (o.enabled > 0 && tick_overlap_thread_is_render())
		return o.posted_jobs;
	return 0;
}

unsigned long tick_overlap_drain_limit(
	void)
{
	if (o.enabled > 0 && o.in_overlap_job && tick_overlap_thread_is_tick())
		return o.running_sequence - 1;
	return (unsigned long)-1;
}

boolean tick_overlap_owner_blocked(
	long owner)
{
	/* only the tick thread's queues: the game thread, their producer, finds the thread idle and has nothing to wait for. The game thread's own are
	served by the game thread, at its drain points and in its wait for the tick thread, whenever a producer waits (queue->waiters) */
	return o.state > 0 && owner == TICK_OWNER_TICK && o.idle != 0 && !o.pending && !tick_overlap_thread_is_tick();
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
	return o.job_in_flight;
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

	if (o.job_in_flight)
	{
		const unsigned long long took = o.job.ended_us - o.job.started_us;

		o.job_in_flight = FALSE;
		o.job_collected = TRUE;
		o.jobs++;
		o.ticks += (unsigned long)o.job.ticks_run;
		o.ticks_total += (unsigned long)o.job.ticks_run;
		o.job_us += took;
		if (took > o.job_max_us)
			o.job_max_us = took;
	}
	if (o.job_collected)
	{
		o.job_collected = FALSE;
		o.forcing = 1;
		player_control_sync_display_facing();
		observer_sync_published();
		tick_owners_drain(TICK_OWNER_GAME);
		tick_owners_drain(TICK_OWNER_SOUND);
		tick_owners_drain(TICK_OWNER_RENDER);
		o.forcing = 0;
	}
}

void tick_overlap_sync_point(
	void)
{
	unsigned long long started;

	if (!tick_overlap_enabled())
		return;
	started = tick_overlap_now_us();
	o.frame_started_us = started;
	tick_overlap_wait();
	tick_overlap_collect();
	o.sync_us += tick_overlap_now_us() - started;
}

/* the tick thread's job */
static void tick_overlap_job_work(
	void *argument)
{
	struct tick_overlap_job *job = (struct tick_overlap_job *)argument;

	job->started_us = tick_overlap_now_us();
	o.running_sequence = job->sequence;
	o.in_overlap_job = 1;
	job->ticks_run = main_overlap_tick_side(job->dt, job->ticks_elapsed, job->leftover_after, job->previous_job_tick);
	o.in_overlap_job = 0;
	job->ended_us = tick_overlap_now_us();
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
	job->sequence = o.posted_jobs + 1;
	fraction = leftover_after * speed * TICKS_PER_SECOND;
	fraction = PIN(fraction, 0.0f, 1.0f);
	/* the picture is of the clock one tick behind: the tick before the one the job ends on, which the ring has if the tick thread was not behind */
	want = time_now + ticks - 1;
	tick = want < published ? want : published;
	o.frame_active = TRUE;
	o.posted_jobs++;
	if (published != tick_snapshot_none() && tick_snapshot_pin(tick))
	{
		/* the tick thread was late (the tick wanted is not committed yet): the last pose is held, the pair at its end */
		if (tick < want)
		{
			fraction = 1.0f;
			o.held_frames++;
		}
		tick_overlap_set_view(tick, fraction, ticks);
		o.job_in_flight = TRUE;
		o.overlap_frames++;
		o.ahead_sum += (unsigned long long)((time_now + ticks - tick) * 1000);
		tick_overlap_post(tick_overlap_job_work, job);
		return TRUE;
	}
	/* no pair to draw yet (the first frame of a map, or after frames of the serial loop's that kept no ring): the job is run at once, and the
	picture is of the latest tick, as the serial loop's */
	tick_snapshot_unpin();
	o.job_in_flight = TRUE;
	o.primed_frames++;
	tick_overlap_run_and_wait(tick_overlap_job_work, job);
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
	now = tick_overlap_now_us();
	o.frame_us += now - o.frame_started_us;
	if (now - o.frame_started_us > o.frame_max_us)
		o.frame_max_us = now - o.frame_started_us;
	if (!o.log_started_us)
		o.log_started_us = now;
	if (now - o.log_started_us >= (unsigned long long)TICK_OVERLAP_LOG_SECONDS * 1000000ULL)
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
	unsigned long long microseconds)
{
	o.blend_us += microseconds;
	o.blends++;
}

void tick_overlap_note_push_wait(
	void)
{
	__atomic_add_fetch(&o.push_waits, 1, __ATOMIC_RELAXED);
}

void tick_overlap_note_capture(
	unsigned long long microseconds)
{
	__atomic_add_fetch(&o.capture_us, (unsigned long)microseconds, __ATOMIC_RELAXED);
	__atomic_add_fetch(&o.captures, 1, __ATOMIC_RELAXED);
}

void tick_overlap_note_publish(
	unsigned long long microseconds)
{
	__atomic_add_fetch(&o.publish_us, (unsigned long)microseconds, __ATOMIC_RELAXED);
	__atomic_add_fetch(&o.publishes, 1, __ATOMIC_RELAXED);
}

/* the numbers, in the style of port/wiiu/src/wiiu_render_passes.c (a log line holds 199 characters) */
void tick_overlap_log(
	unsigned long long now)
{
	const double seconds = (double)(now - o.log_started_us) / 1.0e6;
	const double frames = o.frames ? (double)o.frames : 1.0;
	const double overlap_frames = o.overlap_frames ? (double)o.overlap_frames : 1.0;
	const double jobs = o.jobs ? (double)o.jobs : 1.0;
	const double ticks = o.ticks ? (double)o.ticks : 1.0;
	const unsigned long captures = __atomic_exchange_n(&o.captures, 0, __ATOMIC_RELAXED);
	const unsigned long capture_us = __atomic_exchange_n(&o.capture_us, 0, __ATOMIC_RELAXED);
	const unsigned long publishes = __atomic_exchange_n(&o.publishes, 0, __ATOMIC_RELAXED);
	const unsigned long publish_us = __atomic_exchange_n(&o.publish_us, 0, __ATOMIC_RELAXED);
	const unsigned long tick_idle_us = __atomic_exchange_n(&o.tick_idle_us, 0, __ATOMIC_RELAXED);

	platform_log("tick overlap (%.1f s, %lu frames: %lu overlapped, %lu serial, %lu primed, %lu held, %lu no pair): %.1f ticks a second, %.1f frames a second",
		seconds, o.frames, o.overlap_frames, o.serial_frames, o.primed_frames, o.held_frames, o.no_pair_frames, (double)o.ticks / seconds,
		(double)o.frames / seconds);
	platform_log("tick overlap, tick thread: %.2f ms a job (most %.2f), %.2f ms a tick (%.2f ticks a job), waiting for a job %.0f%% of the time",
		(double)o.job_us / 1000.0 / jobs, (double)o.job_max_us / 1000.0, (double)o.job_us / 1000.0 / ticks, ticks / jobs,
		100.0 * (double)tick_idle_us / (seconds * 1.0e6));
	platform_log("tick overlap, ahead of the picture: %.2f ticks on average (1 is the delay; more means the tick thread was late), %lu ticks in all this game",
		(double)o.ahead_sum / 1000.0 / overlap_frames, o.ticks_total);
	platform_log("tick overlap, game thread, ms a frame: %.2f (most %.2f), of that waiting for the tick thread %.2f (most %.2f, %lu waits over 4 ms), "
		"without the wait %.2f, sync point %.3f",
		(double)o.frame_us / 1000.0 / frames, (double)o.frame_max_us / 1000.0, (double)o.wait_us / 1000.0 / frames,
		(double)o.wait_max_us / 1000.0, o.waits_long, (double)(o.frame_us - o.wait_us) / 1000.0 / frames,
		(double)(o.sync_us - o.wait_us) / 1000.0 / frames);
	platform_log("tick overlap, snapshot: capture %.3f ms a tick (%lu), publish %.3f ms a job (%lu), blends %.3f ms a frame (%lu objects), "
		"queue waits %lu",
		captures ? (double)capture_us / 1000.0 / (double)captures : 0.0, captures,
		publishes ? (double)publish_us / 1000.0 / (double)publishes : 0.0, publishes, (double)o.blend_us / 1000.0 / frames, o.blends,
		o.push_waits);
	o.log_started_us = now;
	o.frames = o.overlap_frames = o.serial_frames = o.primed_frames = o.held_frames = o.no_pair_frames = 0;
	o.wait_us = o.wait_max_us = o.frame_us = o.frame_max_us = o.sync_us = o.post_us = o.draw_us = 0;
	o.waits_long = 0;
	o.jobs = o.ticks = 0;
	o.job_us = o.job_max_us = o.tick_work_us = 0;
	o.ahead_sum = 0;
	o.blend_us = 0;
	o.blends = 0;
	o.push_waits = 0;
	o.reports++;
}

#else /* HALO_WIIU */

typedef int tick_overlap_not_for_this_platform;

#endif
