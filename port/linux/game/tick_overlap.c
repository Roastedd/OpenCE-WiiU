/*
TICK_OVERLAP.C

The overlap of the game's ticks with its drawing (tick_overlap.h).
*/

#include "cseries.h"
#include "tick_overlap.h"

#ifdef HALO_WIIU

#include "cseries/errors.h"
#include "tick_input.h"
#include "smooth_defaults.h" /* port: what the cards and the map decide (smooth_defaults.c) */
#include "tick_perf.h"
#include "../../wiiu/src/wiiu_perf.h" /* port: the detailed overlay's totals (perf_totals) */

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
void wiiu_study_tick_thread_job_report(void);
int wiiu_tick_thread_create(void *(*entry)(void *), unsigned long stack_bytes);
void wiiu_tick_thread_move(int to_the_calling_core);
unsigned wiiu_core_here(void);
unsigned wiiu_tick_thread_core_set(void);
void wiiu_thread_stack_range(unsigned long *low, unsigned long *size);
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
	pthread_t thread; /* (a pthread's: the host's; the console's is an OSThread of the platform's, wiiu_tick_thread_create) */
	/* the tick thread's stack, which it says when it starts (the calling thread is the tick thread if its stack is there: tick_overlap_thread_is_tick) */
	unsigned long tick_stack_low, tick_stack_size;
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
	unsigned long long wait_total_ticks, wait_ticks, wait_max_ticks, frame_ticks, frame_max_ticks, sync_ticks, sync_wait_ticks, post_ticks, draw_ticks, draw_max_ticks, serial_wait_ticks;
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
	/* the cores' counters (tick_perf.h): the tick thread's, a job; the game thread's, a frame; and what the last log took */
	struct tick_perf perf_job, perf_frame, perf_job_logged, perf_frame_logged;
	unsigned long perf_frame_before[4];
	boolean perf_frame_open;
	unsigned perf_job_core, perf_frame_core;
	/* the tick thread being late: the overlap's frames are the serial loop's for a while (tick_overlap_late) */
	boolean late; /* serial frames from here */
	boolean late_moved; /* the tick thread is on the game thread's core meanwhile */
	unsigned long long late_until, late_window_start;
	unsigned long late_frames, late_window_ticks;
	unsigned long long late_ahead_sum, late_wait_sum;
	unsigned long late_backoff_seconds, late_trips, late_clean_windows;
	unsigned long long late_job_sum; /* the jobs' time in the window */
	boolean slow; /* the game's clock is held to one tick a frame: the game runs slow instead of the game thread waiting (the first answer to a late tick thread) */
	unsigned long long slow_until;
	unsigned long slow_trips;
	boolean cores_said;
	/* monotonic counts for the detailed overlay (wiiu_perf.h), never reset by the log: 32 bits, written by the game thread only (the tick thread's job time is
	added at the sync point that collects the job), read whole by another */
	volatile uint32_t ov_frames, ov_overlapped, ov_serial, ov_job, ov_jobs;
} o = { -1, -1, -1 };

/* ---------- the switch */

boolean tick_overlap_requested(
	void)
{
	if (o.requested < 0)
	{
		/* (what the cards and the map decide for this map: smooth_defaults.c; before a first map starts, the cards as they have always been read) */
		const long overlap = smooth_defaults_value(SMOOTH_TICK_OVERLAP);
		const long thread = smooth_defaults_value(SMOOTH_TICK_THREAD);

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
	return o.state > 0 && TICK_STACK_POINTER() - LOAD(o.tick_stack_low) < LOAD(o.tick_stack_size);
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
	boolean first_job = TRUE;

	(void)unused;
	{
		unsigned long low = 0, size = 0;

		wiiu_thread_stack_range(&low, &size);
		STORE(o.tick_stack_low, low);
		STORE(o.tick_stack_size, size);
	}
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
		{
			unsigned long perf_before[4];

			tick_perf_begin(perf_before);
			work(argument);
			tick_perf_end(&o.perf_job, perf_before);
			if (tick_perf_on())
				o.perf_job_core = wiiu_perfmon_core();
		}
		/* (the core it really ran its first job on: the console's log) */
		if (first_job)
		{
			first_job = FALSE;
			wiiu_study_tick_thread_job_report();
		}
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
		/* (the console makes it an OSThread on core 0 (1); the host, none (0): a pthread) */
		const int made = wiiu_tick_thread_create(tick_overlap_thread_run, TICK_OVERLAP_STACK_BYTES);

		if (made > 0)
			o.state = 1;
		else if (made == 0)
		{
			pthread_attr_setstacksize(&attributes, TICK_OVERLAP_STACK_BYTES);
			if (pthread_create(&o.thread, &attributes, tick_overlap_thread_run, NULL) == 0)
				o.state = 1;
		}
		pthread_attr_destroy(&attributes);
	}
	error(_error_silent, "study: the ticks on a thread of their own (halo/tick_thread.txt): %s", o.state ? "running" : "could not be started");
	platform_log("study: the ticks on a thread of their own on core 0 (tick_thread.txt): %s", o.state ? "running" : "could not be started");

	return o.state > 0;
}

/* the queues of the game thread that a producer waits for room in are drained here: the tick thread asks for it, and the game thread, waiting for
the tick thread, is the only one that can (tick_owners.c) */
long tick_owners_drain_waited(void);

/* the tick thread's request for the game thread (tick_overlap_game_thread_do) */
static struct
{
	void (*function)(void *);
	void *argument;
	volatile int pending, done;
} o_request;

boolean tick_overlap_game_thread_do(
	void (*function)(void *),
	void *argument)
{
	if (o.enabled <= 0 || !LOAD(o.in_overlap_job) || !tick_overlap_thread_is_tick())
		return FALSE;
	o_request.function = function;
	o_request.argument = argument;
	__atomic_store_n(&o_request.done, 0, __ATOMIC_RELAXED);
	__atomic_store_n(&o_request.pending, 1, __ATOMIC_RELEASE);
	while (!__atomic_load_n(&o_request.done, __ATOMIC_ACQUIRE))
	{
		struct timespec pause = { 0, 100000 };

		nanosleep(&pause, NULL);
	}
	return TRUE;
}

/* the game thread, at its wait for the job: serves the tick thread's request, if it has one */
static void tick_overlap_serve_request(void)
{
	if (__atomic_load_n(&o_request.pending, __ATOMIC_ACQUIRE))
	{
		__atomic_store_n(&o_request.pending, 0, __ATOMIC_RELAXED);
		o_request.function(o_request.argument);
		__atomic_store_n(&o_request.done, 1, __ATOMIC_RELEASE);
	}
}

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

		tick_overlap_serve_request();
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
		o.late_wait_sum += took;
		o.wait_total_ticks += took;
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
	const unsigned long long late_wait_before = o.late_wait_sum;
	const unsigned long long total_before = o.wait_total_ticks;
	const unsigned long long max_before = o.wait_max_ticks;
	const unsigned long long long_before = o.waits_long;

	tick_overlap_post(work, argument);
	tick_overlap_wait();
	o.serial_wait_ticks += o.wait_ticks - wait_before;
	o.serial_waits++;
	o.wait_ticks = wait_before;
	o.late_wait_sum = late_wait_before;
	o.wait_total_ticks = total_before;
	o.wait_max_ticks = max_before;
	o.waits_long = (unsigned long)long_before;
}

/* ---------- the tick thread being late

In a heavy scene the tick thread can take longer for a tick than the game thread would (the console's first scripted fights: 48 ms a tick on core 0 against 12.6 on core 1), and then the
overlap is a loss: the game thread waits for it at every sync point and the game runs slower than 30 ticks a second. The overlapped frames are judged in windows of LATE_WINDOW_SECONDS (5 s).
A window is LATE only if the game ran under 25 ticks a second AND the tick thread is the reason: it was busy over 95% of the window, or the picture was on average more than 1.5 ticks behind the clock.
(Not a single late tick, not a wait of the game thread over some milliseconds, and not a slow clock alone: the console's second matrix, 2026-11-03, fell back 2 to 3 times in a run where the tick thread
made 27.8 ticks a second at 50% busy, on windows where the game itself had stopped for a moment - 15 ticks a second with the picture 1.06 behind and the tick thread idle.)
The first answer to a late window is to stay overlapped and hold the game's clock to one tick a frame for SLOW_SECONDS: the game runs slow when the tick thread cannot keep up, and the game thread does
not wait for two ticks at a time (the spiral). Only if the next window is late too and the game is under 20 ticks a second is the answer the serial loop for a while (the tick thread moved to the game
thread's core, whose cache is big): 10 s, then if it is late again 20, 40, 60 s, and the overlap is tried again after each. The log says each step. */
#define LATE_WINDOW_SECONDS 5
#define SLOW_SECONDS 20
#define LATE_FRAMES_AT_LEAST 20

boolean tick_overlap_late(
	void)
{
	return o.late;
}

static unsigned long tick_overlap_late_fewest_frames(void);
static long tick_overlap_late_setting; /* (defined with its value below) */

static void tick_overlap_late_judge(
	unsigned long long now)
{
	const double per_second = (double)wiiu_ticks_per_second();
	const double seconds = (double)(now - o.late_window_start) / per_second;
	const double frames = o.late_frames ? (double)o.late_frames : 1.0;
	const double ahead = (double)o.late_ahead_sum / 1000.0 / frames;
	const double wait = (double)o.late_wait_sum / (per_second / 1000.0) / frames;
	const double rate = (double)o.late_window_ticks / seconds;
	const double busy = (double)o.late_job_sum / (double)(now - o.late_window_start);
	/* (a window of fewer frames than that is a slow game, not a quiet one: the console drew 5 to 11 frames a second when the tick thread was far behind. Past 5 s, 6 frames are enough; a window with
	fewer is kept going for up to 12 s) */
	unsigned long fewest = tick_overlap_late_fewest_frames();
	/* (a test with the file holding a number above 1: the host's qemu has a tick thread that is neither busy nor behind, so the slow clock alone is a late window) */
	const boolean test = tick_overlap_late_setting > 1;
	boolean late;

	if (seconds >= 5.0 && fewest > 6)
		fewest = 6;
	if (o.late_frames < fewest && seconds < 12.0)
		return; /* (the window goes on) */
	late = o.late_frames >= fewest && rate < 25.0 && (busy > 0.95 || ahead > 1.5 || test);

	if (late && o.slow && (rate < 20.0 || test))
	{
		/* (the clock held and the game still under 20 ticks a second: the serial loop) */
		o.late_backoff_seconds = o.late_backoff_seconds ? (o.late_backoff_seconds * 2 > 60 ? 60 : o.late_backoff_seconds * 2) : 10;
		o.late = TRUE;
		o.slow = FALSE;
		o.late_trips++;
		o.late_clean_windows = 0;
		o.late_until = now + (unsigned long long)o.late_backoff_seconds * (unsigned long long)per_second;
		/* (the tick thread is idle: this is the game thread's, after its sync point) */
		if (wiiu_core_here() != wiiu_tick_thread_core_set())
		{
			wiiu_tick_thread_move(1);
			o.late_moved = TRUE;
		}
		tick_snapshot_say("tick overlap: serial frames for %lu s (the tick thread was late over %.1f s with the clock held: the tick thread busy %.0f%%, the picture %.2f ticks behind the clock on average, "
			"the game thread waited %.1f ms a frame, %.1f ticks a second); the tick thread runs on core %u meanwhile", o.late_backoff_seconds, seconds, busy * 100.0, ahead, wait, rate,
			wiiu_tick_thread_core_set());
	}
	else if (late)
	{
		/* (stay overlapped, the game's clock held to one tick a frame) */
		if (!o.slow)
			o.slow_trips++;
		o.slow = TRUE;
		o.late_clean_windows = 0;
		o.slow_until = now + (unsigned long long)SLOW_SECONDS * (unsigned long long)per_second;
		tick_snapshot_say("tick overlap: the game's clock held to one tick a frame for %d s (the tick thread was late over %.1f s: busy %.0f%%, the picture %.2f ticks behind the clock on average, "
			"the game thread waited %.1f ms a frame, %.1f ticks a second); the overlap goes on", SLOW_SECONDS, seconds, busy * 100.0, ahead, wait, rate);
	}
	else if (o.late_frames >= fewest && ++o.late_clean_windows >= 2 && o.late_backoff_seconds)
	{
		platform_log("tick overlap: on time again (%.2f ticks behind, %.1f ms waited a frame, %.1f ticks a second): the next time it is late the serial frames last 10 s", ahead, wait, rate);
		o.late_backoff_seconds = 0;
	}
	o.late_window_start = 0;
}

/* halo/tick_overlap_late.txt: 0 keeps the overlap whatever the tick thread does (absent: TICK_OVERLAP_LATE_DEFAULT; 1 is the fall back; a number above 1 is the fewest frames a window judged late
needs, 20 otherwise: the host's qemu draws a few frames a second, and a test of the fall back has to judge a window of a few) */
/* (what the card's file means when it is absent: on in the build that has the overlap's changes, off in the timers build, whose run is to see the tick thread as it is) */
#define TICK_OVERLAP_LATE_DEFAULT 1
static long tick_overlap_late_setting = -1;

static boolean tick_overlap_late_enabled(
	void)
{
	if (tick_overlap_late_setting < 0)
		tick_overlap_late_setting = wiiu_study_number("tick_overlap_late.txt", 1);
	return tick_overlap_late_setting > 0;
}

static unsigned long tick_overlap_late_fewest_frames(
	void)
{
	return tick_overlap_late_setting > 1 ? (unsigned long)tick_overlap_late_setting : LATE_FRAMES_AT_LEAST;
}

/* at the start of a frame, the game thread's, with the tick thread idle */
static void tick_overlap_late_check(
	unsigned long long now)
{
	if (!tick_overlap_late_enabled())
		return;
	if (o.late)
	{
		if (now >= o.late_until)
		{
			o.late = FALSE;
			o.late_window_start = 0;
			if (o.late_moved)
			{
				wiiu_tick_thread_move(0);
				o.late_moved = FALSE;
			}
			platform_log("tick overlap: trying the overlap again (the tick thread back on core %u)", wiiu_tick_thread_core_set());
		}
		return;
	}
	if (o.slow && now >= o.slow_until)
	{
		o.slow = FALSE;
		platform_log("tick overlap: the game's clock is real time again");
	}
	if (o.late_window_start && now - o.late_window_start >= (unsigned long long)LATE_WINDOW_SECONDS * (unsigned long long)wiiu_ticks_per_second())
		tick_overlap_late_judge(now);
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
				platform_log("tick overlap: on for this map (%s): the ticks run beside the drawing, the frame drawn from the ring one tick behind the clock",
					smooth_defaults_decided() ? "smooth defaults or halo/tick_overlap.txt" : "halo/tick_overlap.txt");
				platform_log("tick overlap: the game thread is on core %u, the tick thread on core %u (halo/tick_core.txt, halo/game_core.txt)", wiiu_core_here(), wiiu_tick_thread_core_set());
				platform_log("tick overlap: the fall back to serial frames when the tick thread is late is %s (halo/tick_overlap_late.txt: %s; a file holding 0 turns it off)",
					tick_overlap_late_enabled() ? "on" : "OFF", tick_overlap_late_enabled() ? "absent or 1" : "0");
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
		o.ov_jobs++;
		o.ov_job += (uint32_t)took;
		o.ticks += (unsigned long)o.job.ticks_run;
		o.late_window_ticks += (unsigned long)o.job.ticks_run;
		o.late_job_sum += took;
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

/* the milliseconds the game thread has waited for the tick thread's job in all, not counting the serial frames' (the dynamic quality governor reads it each frame:
while the game thread waits, the frame is the tick thread's and drawing less shortens nothing) */
double tick_overlap_wait_ms_total(void)
{
	return (double)o.wait_total_ticks / ((double)wiiu_ticks_per_second() / 1000.0);
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
	tick_perf_begin(o.perf_frame_before);
	o.perf_frame_open = tick_perf_on();
	tick_overlap_wait();
	tick_overlap_collect();
	/* (what the ticks of this frame hear: the listeners as the frame before left them) */
	sound_manager_freeze_listeners();
	o.sync_ticks += tick_overlap_now() - started;
	o.sync_wait_ticks += o.wait_ticks - wait_before;
}

/* see tick_overlap.h */
void tick_overlap_jitter(
	void)
{
	static long jitter = -1;
	static unsigned long state = 0x9E3779B9UL;

	if (jitter < 0)
		jitter = wiiu_study_number("tick_overlap_jitter.txt", 0);
	if (jitter <= 0 || !LOAD(o.in_overlap_job) || !tick_overlap_thread_is_tick())
		return;
	/* (xorshift: the sequence is the same in each run, the interleaving with the drawing is not) */
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	if ((state & 3) == 0)
	{
		const long microseconds = (long)(((state >> 8) % (unsigned long)jitter) * 1000UL + ((state >> 4) & 0x3FF));
		struct timespec pause = { microseconds / 1000000L, (microseconds % 1000000L) * 1000L };

		nanosleep(&pause, NULL);
	}
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
	tick_overlap_late_check(tick_overlap_now());
	o.ov_frames++;
	if (!main_overlap_eligible() || !game_time_overlap_step(dt, &time_now, &ticks, &leftover_after, &speed) || speed != 1.0f)
	{
		o.serial_frames++;
		o.ov_serial++;
		o.late_window_start = 0; /* (a window is of overlapped frames in a row) */
		return FALSE;
	}
	/* (the clock held: one tick a frame at most, the rest of the time is the game running slow) */
	if (o.slow && ticks > 1)
		ticks = 1;
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
		if (!o.late_window_start)
		{
			o.late_window_start = tick_overlap_now();
			o.late_frames = 0;
			o.late_window_ticks = 0;
			o.late_ahead_sum = 0;
			o.late_wait_sum = 0;
			o.late_job_sum = 0;
		}
		o.late_frames++;
		o.late_ahead_sum += (unsigned long long)((time_now + ticks - tick) * 1000);
		STORE(o.job_in_flight, TRUE);
		o.overlap_frames++;
		o.ov_overlapped++;
		o.ahead_sum += (unsigned long long)((time_now + ticks - tick) * 1000);
		tick_overlap_post(tick_overlap_job_work, job);
		/* (a test aid: halo/tick_overlap_serialize.txt holds 1: the job is waited for before the frame is drawn, so that nothing runs beside the drawing and two runs
		draw the same frames: the pictures' random numbers, which the two threads draw from one seed, come in one order. The picture is the same pair of the ring.) */
		{
			static long serialize = -1;

			if (serialize < 0)
				serialize = wiiu_study_number("tick_overlap_serialize.txt", 0);
			if (serialize > 0)
				tick_overlap_wait();
		}
		return TRUE;
	}
	/* no pair to draw yet (the first frame of a map, or after frames of the serial loop's that kept no ring): the job is run at once, and the
	picture is of the latest tick, as the serial loop's */
	tick_snapshot_unpin();
	STORE(o.job_in_flight, TRUE);
	o.primed_frames++;
	o.ov_overlapped++;
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
	if (o.perf_frame_open)
	{
		tick_perf_end(&o.perf_frame, o.perf_frame_before);
		o.perf_frame_core = wiiu_perfmon_core();
		o.perf_frame_open = FALSE;
	}
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

/* The map's decision (smooth_defaults.c), at a map start with the tick thread idle: the overlap, the snapshot behind it and the owners' queues are made again
for the map from what the cards and the map say. Nothing of the last map's is kept: the job and its numbers are taken, the views and the pin let go, the
late state (the clock held, the serial window, the tick thread moved to the game thread's core) forgotten, the listeners unfrozen and the owners' gate
taken off if the map is not one for the overlap. The thread stays (it is made once, on the first map that wants it) and waits for work. */
void tick_overlap_decide(
	int mid_map)
{
	const boolean was_enabled = o.enabled > 0;

	if (was_enabled)
		tick_overlap_map_reset();
	o.late = FALSE;
	o.slow = FALSE;
	o.late_until = o.slow_until = 0;
	o.late_window_start = 0;
	o.late_window_ticks = 0;
	o.late_frames = 0;
	o.late_ahead_sum = o.late_wait_sum = o.late_job_sum = 0;
	o.late_backoff_seconds = 0;
	o.late_clean_windows = 0;
	if (o.late_moved)
	{
		wiiu_tick_thread_move(0);
		o.late_moved = FALSE;
	}
	if (was_enabled)
	{
		tick_owners_set_threaded(FALSE);
		tick_overlap_listeners_frozen = 0;
		o.frame_active = FALSE;
		o.pair_active = FALSE;
	}
	o.requested = -1;
	o.enabled = -1;
	o.log_started_ticks = 0;
	/* (in this order: the owners' mode is the overlap's request, the snapshot's flags are too, and the overlap needs the snapshot) */
	/* (the owners' mode is not changed in the middle of a map: a sound started as a ticket would be taken for a datum, and the other way round. A map that stops being one for the
	overlap keeps its queues, unthreaded, as the build before the smooth defaults had them in a network game) */
	if (!mid_map)
		tick_owners_decide();
	tick_snapshot_decide();
	if (tick_overlap_enabled())
		sound_manager_freeze_listeners();
}

/* what the map summary (port/wiiu/src/wiiu_map_summary.c) reads: the frames the game thread has drawn, the overlapped and the serial ones among them,
and the times the tick thread was late (the serial window: a fall back) and the clock was held (the answer before it), all of them since the start of the run */
void tick_overlap_summary_counts(
	unsigned long counts[5])
{
	counts[0] = o.ov_frames;
	counts[1] = o.ov_overlapped;
	counts[2] = o.ov_serial;
	counts[3] = o.late_trips;
	counts[4] = o.slow_trips;
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

/* a count with its unit: millions, thousands, or the number */
static void tick_overlap_count_text(
	char *text,
	size_t size,
	double value)
{
	if (value >= 1e6)
		snprintf(text, size, "%.2f M", value / 1e6);
	else if (value >= 1e3)
		snprintf(text, size, "%.1f K", value / 1e3);
	else
		snprintf(text, size, "%.0f", value);
}

/* the counters of a core over a window, a tick or a frame (divisor of them), as the delta since the last log; one log line */
static void tick_overlap_counters_line(
	char const *who,
	unsigned core,
	struct tick_perf const *now,
	struct tick_perf *logged,
	unsigned long divisor,
	char const *per)
{
	double value[4];
	char text[4][24];
	int counter;

	for (counter = 0; counter < 4; counter++)
	{
		value[counter] = (double)(now->sum[counter] - logged->sum[counter]) / (double)(divisor ? divisor : 1);
		tick_overlap_count_text(text[counter], sizeof(text[counter]), value[counter]);
	}
	platform_log("tick overlap, counters of core %u (%s), a %s (%lu): %s %s, %s %s, %s %s, %s %s", core, who, per, divisor, wiiu_perfmon_name(0), text[0], wiiu_perfmon_name(1), text[1],
		wiiu_perfmon_name(2), text[2], wiiu_perfmon_name(3), text[3]);
	if (wiiu_perfmon_set() == 1 && value[0] > 0.0 && value[1] > 0.0)
	{
		platform_log("tick overlap, counters of core %u (%s): %.2f instructions a cycle, %.2f L2 data misses and %.2f castouts for every 1000 instructions", core, who, value[1] / value[0],
			value[2] * 1000.0 / value[1], value[3] * 1000.0 / value[1]);
	}
	*logged = *now;
}

/* the detailed overlay's read (port/wiiu/src/wiiu_gx2.c): the overlap's monotonic counts, in the fields of perf_totals */
void tick_overlap_perf_read(
	struct perf_totals *totals)
{
	totals->overlap_enabled = o.enabled > 0;
	totals->overlap_frames = o.ov_frames;
	totals->overlap_overlapped = o.ov_overlapped;
	totals->overlap_serial = o.ov_serial;
	totals->overlap_wait = (uint32_t)o.wait_total_ticks; /* (the governor's wait total: one set of counts) */
	totals->overlap_job = o.ov_job;
	totals->overlap_jobs = o.ov_jobs;
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
	render_interpolation_blend_log(per_ms, o.active_frames);
	tick_snapshot_view_log(per_ms);
	if (tick_perf_on())
	{
		tick_overlap_counters_line("tick thread", o.perf_job_core, &o.perf_job, &o.perf_job_logged, o.ticks, "tick");
		tick_overlap_counters_line("game thread", o.perf_frame_core, &o.perf_frame, &o.perf_frame_logged, o.active_frames, "frame");
	}
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
