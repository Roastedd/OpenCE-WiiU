/*
The hand-off between the game thread and the tick thread (port/linux/game/tick_overlap.c, with the queues of tick_owners.c and the ring's rules of
tick_ring.h), apart from the game: the game thread's frames (the sync point, the job posted, a frame drawn from the pinned pair while the tick thread runs
its job, the frame's end) and a tick thread that runs jobs of 0 to 3 ticks, captures the last two into a mock ring, asks for the game thread's queues'
room, and takes what the game thread pushed. Run under the thread sanitizer where there is one.

What it checks, over some thousands of frames:
- the pair a frame draws from is whole all the while the tick thread works (every word of a capture is its tick's);
- the picture's tick is the clock's less one, or the newest committed if the tick thread was late (and then held at the end of the pair);
- the tick thread takes, in each job, what the game thread pushed to its queue before it posted the job, and nothing it pushed after, whichever way
  the threads are timed;
- what the tick thread pushed to the game thread's queues is applied at the next sync point and nowhere else, even when the queue is so small that the
  tick thread waits for room (the game thread then serves it, and nothing deadlocks);
- a frame with the overlap off (not eligible) posts no job.

    test_tick_overlap frames | small
Exit status 0 when everything holds; a failing check says which and exits 1.
*/

#define _GNU_SOURCE
#define HALO_WIIU 1
#include "cseries.h"
#include "tick_owners.c"
#include "tick_overlap.c"
#include "tick_ring.h"

#include <stdarg.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			if (failures < 30) \
				printf("check failed, line %d: %s\n", __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

/* ---------- the platform and the game, in the little that tick_overlap.c asks of them */

static pthread_t game_thread;
static int small_queues;

void error(int severity, char const *format, ...)
{
	(void)severity;
	(void)format;
}

void profile_thread_disable(void)
{
}

void platform_log(char const *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	va_end(arguments);
}

long wiiu_study_number(const char *name, long fallback)
{
	/* (every study file is on, but the test aid that waits for each job: the hand-off is tested with the threads beside each other) */
	if (!strcmp(name, "tick_overlap_serialize.txt"))
		return fallback;
	/* (nor the fall back to the serial loop when the tick thread is late: the test's frames are not a game's) */
	if (!strcmp(name, "tick_overlap_late.txt"))
		return 0;
	if (!strcmp(name, "tick_overlap_jitter.txt") || !strcmp(name, "tick_overlap_force_serial.txt") || !strcmp(name, "tick_overlap_frame_stall.txt"))
		return 0;
	return 1;
}

int tick_snapshot_enabled(void)
{
	return 1;
}

/* the smooth defaults' (smooth_defaults.c): what the map gets of the switches; the test turns the map into one that is not */
static long smooth_stub_value = 1;
long smooth_defaults_value(enum smooth_switch which)
{
	(void)which;
	return smooth_stub_value;
}
int smooth_defaults_decided(void) { return 1; }
void tick_snapshot_decide(void) {}

int halo_interpolation_enabled(void)
{
	return 1;
}


/* a network game's frames (halo/net_overlap.txt): game_connection says a joined game, the main loop's network end of the frame is called once for each frame that posts a job, with the tick thread idle */
static int network_run;
static long net_hook_calls, net_hook_busy;

short game_connection(void)
{
	return network_run ? 1 : 0;
}

void main_overlap_net_end_frame(void)
{
	net_hook_calls++;
	if (__atomic_load_n(&o.pending, __ATOMIC_ACQUIRE) || __atomic_load_n(&o.in_overlap_job, __ATOMIC_ACQUIRE))
		net_hook_busy++;
}

/* the calling thread's stack: the game thread's, when it asks (tick_stack.h) */
void wiiu_thread_stack_range(unsigned long *low, unsigned long *size)
{
#ifdef __APPLE__
	const pthread_t self = pthread_self();
	const unsigned long high = (unsigned long)(uintptr_t)pthread_get_stackaddr_np(self);

	*size = (unsigned long)pthread_get_stacksize_np(self);
	*low = high - *size;
#else
	pthread_attr_t attributes;
	void *address = NULL;
	size_t length = 0;

	pthread_getattr_np(pthread_self(), &attributes);
	pthread_attr_getstack(&attributes, &address, &length);
	pthread_attr_destroy(&attributes);
	*low = (unsigned long)(uintptr_t)address;
	*size = (unsigned long)length;
#endif
}

void wiiu_fpu_setup(void) {}
void wiiu_thread_off_game_core(void) {}
void wiiu_tag_thread(const char *name) { (void)name; }
int wiiu_tick_thread_priority(void) { return 16; }
void wiiu_tick_thread_gate(void) {}
void wiiu_foreground_gate(int is_game_thread) { (void)is_game_thread; }
void wiiu_study_tick_thread_report(void) {}
void wiiu_study_tick_thread_job_report(void) {}
int wiiu_tick_thread_create(void *(*entry)(void *), unsigned long stack_bytes) { (void)entry; (void)stack_bytes; return 0; }
void wiiu_tick_idle_set(int idle) { (void)idle; }
void wiiu_game_waiting_set(int waiting) { (void)waiting; }
void wiiu_tick_gate_register(int registered) { (void)registered; }
void wiiu_tick_gate(void) {}

unsigned long long wiiu_now_ticks(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (unsigned long long)now.tv_sec * 1000000ULL + (unsigned long long)now.tv_nsec / 1000ULL;
}

unsigned long wiiu_ticks_per_second(void)
{
	return 1000000UL;
}

void sound_manager_freeze_listeners(void) {}
void tick_input_capture(void) {}
void tick_input_release(void) {}
void player_control_sync_display_facing(void) {}
void observer_sync_published(void) {}
void render_interpolation_overlap_set(boolean on, long tick) { (void)on; (void)tick; }
void render_interpolation_blend_log(double ticks_per_ms, unsigned long frames) { (void)ticks_per_ms; (void)frames; }
void tick_snapshot_view_log(double ticks_per_ms) { (void)ticks_per_ms; }
void tick_snapshot_say(char const *format, ...) { (void)format; }
int wiiu_perfmon_set(void) { return 0; }
void wiiu_perfmon_start(void) {}
void wiiu_perfmon_read(unsigned long values[4]) { values[0] = values[1] = values[2] = values[3] = 0; }
const char *wiiu_perfmon_name(int counter) { (void)counter; return ""; }
unsigned wiiu_perfmon_core(void) { return 0; }
void wiiu_tick_thread_move(int to_the_calling_core) { (void)to_the_calling_core; }
unsigned wiiu_core_here(void) { return 1; }
unsigned wiiu_tick_thread_core_set(void) { return 0; }

#define SLOTS 4
#define WORDS 128
static struct tick_ring ring;
static long slot_words[SLOTS][WORDS];

/* the clock the game thread's decisions come from: the tick thread's time, read only while it is idle */
static long world_time;
static int frame_ticks[4] = { 1, 0, 1, 1 };
static int eligible = 1;
static long view_tick;
static real view_fraction;
static int view_on;
static long step_time_now, step_ticks, step_published;

boolean main_overlap_eligible(void)
{
	return eligible;
}

boolean game_time_overlap_step(real dt, long *time_now, long *ticks_elapsed, real *leftover_after, real *speed)
{
	static unsigned long state = 99;

	state = state * 1103515245UL + 12345UL;
	/* mostly 0 or 1 ticks, now and then 2 or 3 (a long frame) */
	*ticks_elapsed = (state >> 16) % 16 == 0 ? 2 + (long)((state >> 20) % 2) : frame_ticks[(state >> 16) % 4];
	*time_now = world_time;
	*leftover_after = dt * 0.5f;
	*speed = 1.0f;
	step_time_now = world_time;
	step_ticks = *ticks_elapsed;
	step_published = tick_ring_published(&ring);
	return TRUE;
}

void game_time_set_view(boolean on, long time, short elapsed, real fraction)
{
	(void)elapsed;
	view_on = on;
	view_tick = time;
	view_fraction = fraction;
}

long game_time_overlap_time(void)
{
	return world_time;
}

/* the ring, as tick_snapshot.c keeps it, with a capture's data every word its tick (above) */
long tick_snapshot_none(void)
{
	return TICK_RING_NONE;
}

long tick_snapshot_published_tick(void)
{
	return tick_ring_published(&ring);
}

boolean tick_snapshot_pin(long tick)
{
	return tick_ring_pin(&ring, tick) != 0;
}

void tick_snapshot_unpin(void)
{
	tick_ring_unpin(&ring);
}

/* the tick thread's queues and the game thread's, of numbers */
struct request
{
	long frame;
	long kind; /* 0: pushed by the game thread before its job was posted, 1: after, 2: pushed by the tick thread */
	long expected_job; /* the job that must take it: the number of jobs posted when it was pushed */
};

#define TICK_QUEUE_SMALL 4
#define BIG 4096
static long current_job; /* the tick thread's, the job it runs now */
static long writes_done; /* the jobs that have made their captures and pushes (the tick thread's, read by the game thread) */
static long tick_requests_applied;
static long game_applied[200000];
static long game_applied_count;

static void apply_to_tick_thread(void const *record)
{
	struct request const *request = (struct request const *)record;

	/* (a request is taken by the job that was posted next after it was pushed: not the one running beside the push, not one before) */
	CHECK(request->kind == 0 || request->kind == 1);
	CHECK(current_job == request->expected_job);
	tick_requests_applied++;
}

static void apply_to_game_thread(void const *record)
{
	struct request const *request = (struct request const *)record;

	CHECK(request->kind == 2);
	if (game_applied_count < 200000)
		game_applied[game_applied_count++] = request->frame;
}

TICK_QUEUE_DEFINE(to_tick_queue, "to tick", TICK_OWNER_TICK, struct request, BIG, apply_to_tick_thread);
TICK_QUEUE_DEFINE(to_game_queue, "to game", TICK_OWNER_GAME, struct request, BIG, apply_to_game_thread);
TICK_QUEUE_DEFINE(to_game_small_queue, "to game small", TICK_OWNER_RENDER, struct request, TICK_QUEUE_SMALL, apply_to_game_thread);

/* the tick thread's job: the ticks, the last two captured, the pushes to the game thread's queues, the commit */
static long job_count;

long main_overlap_tick_side(real dt, long ticks_elapsed, real leftover_after, long previous_job_tick)
{
	long tick_index;
	const long job = job_count++;
	struct request push_request;

	(void)dt;
	(void)leftover_after;
	current_job = job;
	CHECK(!tick_overlap_thread_is_render());
	/* (what the game thread left it) */
	tick_owners_drain(TICK_OWNER_TICK);
	CHECK(previous_job_tick == tick_ring_published(&ring));
	push_request.expected_job = 0;
	for (tick_index = 0; tick_index < ticks_elapsed; tick_index++)
	{
		const long tick = world_time + 1;

		world_time = tick;
		if (ticks_elapsed - tick_index <= 2)
		{
			const int slot = tick_ring_slot_for(&ring, tick);
			int word;

			CHECK(slot >= 0);
			if (slot < 0)
				continue;
			tick_ring_begin_write(&ring, slot, tick);
			for (word = 0; word < WORDS; word++)
				slot_words[slot][word] = tick;
			tick_ring_end_write(&ring, slot, 0);
		}
		/* (the tick asks for the game thread's queues: a few records a tick, more than the small one has room for) */
		push_request.frame = job;
		push_request.kind = 2;
		tick_queue_push(&to_game_queue, &push_request);
		if (small_queues)
		{
			int more;

			for (more = 0; more < 6; more++)
				tick_queue_push(&to_game_small_queue, &push_request);
		}
		sched_yield();
	}
	/* (the job has written its captures and made its pushes: the game thread's frame, which is reading its pair beside it, may look at them) */
	__atomic_store_n(&writes_done, job + 1, __ATOMIC_RELEASE);
	tick_ring_commit(&ring);
	return ticks_elapsed;
}

/* the drawing's barrier: when it returns the job is over, every word it writes in place (the captures, the pushes), not only the commit */
static void barrier_and_check(long *done)
{
	const long job_posted = (long)o.posted_jobs;

	CHECK(tick_overlap_net_pending);
	TICK_OVERLAP_NET_BARRIER();
	CHECK(!tick_overlap_net_pending);
	CHECK(!__atomic_load_n(&o.pending, __ATOMIC_ACQUIRE));
	CHECK(__atomic_load_n(&writes_done, __ATOMIC_ACQUIRE) == job_posted);
	(*done)++;
	/* (a second one in the same frame is nothing) */
	TICK_OVERLAP_NET_BARRIER();
}

/* ---------- the game thread's frames */

static void test_frames(long frames)
{
	long frame;
	long drawn = 0, primed = 0, serial = 0, held = 0, mid_frame_applied = 0;
	long applied_at_sync = 0;
	long barriers_done = 0, posted_frames = 0;

	game_thread = pthread_self();
	tick_overlap_note_render_thread();
	CHECK(tick_overlap_thread_is_render());
	tick_ring_reset(&ring, SLOTS);
	setenv("HALO_TICK_OWNERS", "2", 1);
	CHECK(tick_overlap_enabled());
	for (frame = 0; frame < frames; frame++)
	{
		struct request request;
		int reads;
		boolean posted;

		/* the sync point: what the tick thread pushed in the job before is applied here */
		tick_overlap_sync_point();
		applied_at_sync = game_applied_count;
		if (frame == frames / 2)
		{
			/* a map ends and another starts, and the smooth defaults are decided again for it: the tick thread idle, everything forgotten */
			tick_overlap_decide(0);
			CHECK(tick_overlap_enabled());
			tick_ring_reset(&ring, SLOTS);
			world_time = 0;
		}
		eligible = (frame % 97) != 96;
		/* a request for the tick thread before the post: the next job takes it */
		request.frame = frame;
		request.kind = 0;
		request.expected_job = (long)o.posted_jobs;
		tick_queue_push(&to_tick_queue, &request);
		posted = tick_overlap_post_frame(0.033f);
		posted_frames += posted;
		/* (a network game: the barrier is armed for a frame whose job is in flight, and for no other) */
		CHECK(!tick_overlap_net_pending || (network_run && posted && tick_overlap_job_in_flight()));
		/* a request after the post: the job after takes it, not the one running beside it */
		request.kind = 1;
		request.expected_job = (long)o.posted_jobs;
		tick_queue_push(&to_tick_queue, &request);
		if (!posted)
		{
			serial++;
		}
		else if (tick_overlap_frame_active())
		{
			/* the pair this frame draws: whole all the while the tick thread works (the slots pinned are not the tick thread's) */
			const int latest = ring.pinned[0];
			const int before = ring.pinned[1];
			const long tick = view_tick;
			const long expected = step_time_now + step_ticks - 1 < step_published ? step_time_now + step_ticks - 1 : step_published;

			CHECK(latest >= 0);
			CHECK(view_on);
			CHECK(view_fraction >= 0.0f && view_fraction <= 1.0f);
			if (!tick_overlap_job_in_flight())
			{
				/* (the first frame of a map: the job was run at once, and the picture is of the newest tick) */
				primed++;
			}
			else
			{
				/* the tick of the clock one behind, or the newest committed if the tick thread was late, then held at the pair's end */
				CHECK(tick == expected);
				if (tick < step_time_now + step_ticks - 1)
				{
					held++;
					CHECK(view_fraction == 1.0f);
				}
			}
			/* (a network game's drawing reaches its barrier in some frames before the job has written anything, in the others after it is over) */
			if (network_run && tick_overlap_job_in_flight() && frame % 2 == 0)
				barrier_and_check(&barriers_done);
			/* (the reads go on until the tick thread's job has written what it will: a frame that is read from only before that proves nothing) */
			{
				const long expected_job = (long)o.posted_jobs - 1;
				long spins = 0;

				if (tick_overlap_job_in_flight())
				{
					while (__atomic_load_n(&writes_done, __ATOMIC_ACQUIRE) <= expected_job && ++spins < 200000)
					{
						/* (a tick thread that waits for room in a queue is served, as the game thread's wait for it would) */
						tick_owners_drain_waited();
						sched_yield();
					}
				}
			}
			for (reads = 0; reads < 15 && latest >= 0; reads++)
			{
				int word;

				for (word = 0; word < WORDS; word++)
				{
					CHECK(slot_words[latest][word] == tick);
					if (before >= 0)
						CHECK(slot_words[before][word] == tick - 1);
				}
				/* the game thread leaves its queues to the sync point while the job runs: a drain here applies none, unless a producer waits */
				if (tick_overlap_job_in_flight())
				{
					const long applied = game_applied_count;

					tick_owners_drain(TICK_OWNER_GAME);
					tick_owners_drain(TICK_OWNER_RENDER);
					if (!small_queues)
						CHECK(game_applied_count == applied);
					mid_frame_applied += game_applied_count - applied;
				}
				sched_yield();
			}
			if (network_run && tick_overlap_job_in_flight() && frame % 2 == 1)
				barrier_and_check(&barriers_done);
			drawn++;
		}
		else
		{
			primed++;
		}
		tick_overlap_draw_done();
		tick_overlap_frame_end();
	}
	tick_overlap_sync_point();
	(void)applied_at_sync;
	printf("%ld frames: %ld drawn from a pair, %ld primed, %ld serial, %ld with the last pose held; %ld jobs, the clock at %ld; the tick thread's pushes "
		"applied %ld (%ld by the game thread in a frame); the game thread's pushes taken by the tick thread %ld\n",
		frames, drawn, primed, serial, held, job_count, world_time, game_applied_count, mid_frame_applied, tick_requests_applied);
	CHECK(drawn > frames / 2);
	if (network_run)
	{
		/* the network end of the frame once for each frame that posted a job, never with the tick thread running, and a barrier in every frame whose job was in flight */
		CHECK(net_hook_calls == posted_frames);
		CHECK(net_hook_busy == 0);
		CHECK(barriers_done > frames / 2);
		printf("network: %ld end-of-frame calls, %ld barriers, %ld waited, none with the tick thread busy\n", net_hook_calls, barriers_done, (long)o.net_barrier_waits);
	}
	else
	{
		CHECK(net_hook_calls == 0 && barriers_done == 0);
	}
	CHECK(serial > 0);
	CHECK(held > 0);
	/* the tick thread's pushes, in the order made, none lost (the last job's are applied by the last sync point) */
	{
		long index;

		for (index = 1; index < game_applied_count; index++)
		{
			if (game_applied[index] < game_applied[index - 1])
			{
				CHECK(0);
				break;
			}
		}
	}
	if (!small_queues)
		CHECK(mid_frame_applied == 0);
	CHECK(tick_requests_applied > frames);
}

/* the overlap stops cleanly for a map that is not one for it, and starts again for the next */
static void test_decisions(void)
{
	long frame;

	game_thread = pthread_self();
	tick_overlap_note_render_thread();
	tick_ring_reset(&ring, SLOTS);
	setenv("HALO_TICK_OWNERS", "2", 1);
	smooth_stub_value = 1;
	tick_overlap_decide(0);
	CHECK(tick_overlap_enabled() && tick_owners_threaded() && tick_overlap_listeners_frozen);
	for (frame = 0; frame < 300; frame++)
	{
		tick_overlap_sync_point();
		eligible = 1;
		tick_overlap_post_frame(0.033f);
		if (tick_overlap_frame_active())
			tick_overlap_draw_done();
		tick_overlap_frame_end();
	}
	/* a hosted game: off, with a job possibly in flight */
	smooth_stub_value = 0;
	tick_overlap_decide(1);
	CHECK(!tick_overlap_enabled() && !tick_owners_threaded() && !tick_overlap_listeners_frozen);
	/* (in the middle of a map the owners' queues keep their mode: a sound started as a ticket is not a datum) */
	CHECK(tick_owners_mode() == 2);
	CHECK(!tick_overlap_job_in_flight() && !tick_overlap_frame_active());
	for (frame = 0; frame < 100; frame++)
	{
		tick_overlap_sync_point();
		CHECK(!tick_overlap_post_frame(0.033f));
		CHECK(!tick_overlap_frame_active());
		tick_overlap_frame_end();
	}
	/* and a level of one player again */
	smooth_stub_value = 1;
	world_time = 0;
	tick_ring_reset(&ring, SLOTS);
	tick_overlap_decide(0);
	CHECK(tick_overlap_enabled() && tick_owners_threaded() && tick_overlap_listeners_frozen);
	for (frame = 0; frame < 300; frame++)
	{
		tick_overlap_sync_point();
		eligible = 1;
		tick_overlap_post_frame(0.033f);
		if (tick_overlap_frame_active())
			tick_overlap_draw_done();
		tick_overlap_frame_end();
	}
	tick_overlap_sync_point();
	tick_overlap_decide(0);
	CHECK(!tick_overlap_job_in_flight());
}

int main(int count, char **arguments)
{
	const char *which = count > 1 ? arguments[1] : "frames";

	if (!strcmp(which, "frames"))
		test_frames(3000);
	else if (!strcmp(which, "decisions"))
		test_decisions();
	else if (!strcmp(which, "network"))
	{
		network_run = 1;
		test_frames(3000);
	}
	else if (!strcmp(which, "network-small"))
	{
		network_run = 1;
		small_queues = 1;
		test_frames(1500);
	}
	else if (!strcmp(which, "small"))
	{
		small_queues = 1;
		test_frames(1500);
	}
	else
	{
		printf("unknown case %s\n", which);
		return 2;
	}
	if (failures)
	{
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("%s: ok\n", which);
	return 0;
}
