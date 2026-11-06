/*
TICK_OWNERS.C

The mode and the queues of tick_owners.h.
*/

#include "cseries.h"
#include "tick_owners.h"
#include "tick_overlap.h"

#ifdef HALO_WIIU

#include <stdlib.h>
#include <string.h>
#include <time.h>

/* (the platform layer's log: a line of the game's error() reads the game's clock, which a log of this should not move) */
void platform_log(const char *format, ...);

#ifndef HALO_TICK_QUEUE_RECORD_MAXIMUM
#define HALO_TICK_QUEUE_RECORD_MAXIMUM 512
#endif

/* ---------- globals */

long tick_owners_mode_value = -1;
unsigned long tick_owners_off_mask = 0;
static boolean tick_owners_threaded_value = FALSE;
static unsigned long tick_owners_frames = 0;
static struct tick_queue *tick_owners_queues = NULL;
static pthread_mutex_t tick_owners_queues_lock = PTHREAD_MUTEX_INITIALIZER;

/* ---------- public code */

long tick_owners_mode(
	void)
{
	if (tick_owners_mode_value < 0)
	{
		char const *value = getenv("HALO_TICK_OWNERS");
		long mode = value && *value ? strtol(value, NULL, 10) : 0;

		if (mode < 0 || mode > 2)
			mode = 0;
		/* the overlap runs the tick side on a thread of its own: the queues are how the threads hand each other work, in mode 2, at their points
		(halo/tick_overlap.txt) */
		if (mode < 2 && tick_overlap_requested())
		{
			platform_log("tick owners: mode %ld raised to 2 (the tick overlap is on)", mode);
			mode = 2;
		}
		tick_owners_mode_value = mode;
		if (mode)
		{
			char const *off = getenv("HALO_TICK_OWNERS_OFF");

			tick_owners_off_mask = off && *off ? strtoul(off, NULL, 0) : 0;
			if (tick_owners_off_mask)
				platform_log("tick owners: parts left out: 0x%lx (HALO_TICK_OWNERS_OFF)", tick_owners_off_mask);
			platform_log("tick owners: queues on, %s (HALO_TICK_OWNERS=%ld)",
				mode == 1 ? "applied where the direct call was" : "applied at their owner's points", mode);
		}
	}

	return tick_owners_mode_value;
}

/* The map's decision (smooth_defaults.c, tick_overlap_decide), at a map start: the mode is read again, as the overlap now is or is not requested for the map
(mode 2 for the overlap, else the environment's). Requests still waiting from the old mode have no owner's point in the new one: they are let go, as a map's end
lets go of them. */
void tick_owners_decide(
	void)
{
	const long before = tick_owners_mode_value;

	if (before < 0)
		return;
	tick_owners_mode_value = -1;
	if (tick_owners_mode() != before)
		tick_owners_discard_all();
}

/* the shell's (port/wiiu/src/wiiu_shell.c) */
void wiiu_tick_gate_register(int registered);
void wiiu_tick_gate(void);

boolean tick_owners_threaded(
	void)
{
	return tick_owners_threaded_value;
}

void tick_owners_set_threaded(
	boolean threaded)
{
	tick_owners_threaded_value = threaded;
	wiiu_tick_gate_register(threaded);
}

void tick_owners_safe_point(
	void)
{
	if (tick_owners_threaded_value)
		wiiu_tick_gate();
}

static void tick_queue_register(
	struct tick_queue *queue)
{
	struct tick_queue *other;

	/* (the first push of each queue gets here; the list is a short one of a few queues, and two queues' first pushes at once
	take turns) */
	pthread_mutex_lock(&tick_owners_queues_lock);
	for (other = tick_owners_queues; other; other = other->next)
	{
		if (other == queue)
		{
			pthread_mutex_unlock(&tick_owners_queues_lock);
			return;
		}
	}
	queue->next = tick_owners_queues;
	/* (published for the readers that walk the list without the lock: a queue's own fields are in place before it is the head) */
	__atomic_store_n(&tick_owners_queues, queue, __ATOMIC_RELEASE);
	pthread_mutex_unlock(&tick_owners_queues_lock);
}

void tick_queue_push(
	struct tick_queue *queue,
	void const *record)
{
	boolean full;

	if (queue->record_size > HALO_TICK_QUEUE_RECORD_MAXIMUM)
	{
		platform_log("tick owners: the %s queue's records are %ld bytes (%d at most)", queue->name, queue->record_size,
			HALO_TICK_QUEUE_RECORD_MAXIMUM);
		return;
	}

	for (;;)
	{
		pthread_mutex_lock(&queue->lock);
		if (!queue->pushed_total && !queue->tail)
			tick_queue_register(queue);
		full = queue->tail - queue->head >= (unsigned long)queue->capacity;
		if (!full)
		{
			memcpy(queue->records + (queue->tail % (unsigned long)queue->capacity) * (unsigned long)queue->record_size, record,
				(size_t)queue->record_size);
			queue->tags[queue->tail % (unsigned long)queue->capacity] = tick_overlap_push_tag();
			queue->tail++;
			queue->pushed_total++;
			if (queue->tail - queue->head > queue->high_water)
				queue->high_water = queue->tail - queue->head;
			pthread_mutex_unlock(&queue->lock);
			break;
		}
		pthread_mutex_unlock(&queue->lock);
		/* (full: one thread doing both jobs makes room itself; with two, the owner will) */
		if (!tick_owners_threaded() || tick_overlap_owner_is_caller(queue->owner))
		{
			/* (a request made by the queue's own drain, with no room: it cannot wait for the drain it is in) */
			if (queue->draining)
			{
				platform_log("tick owners: the %s queue is full inside its own drain: a request is dropped", queue->name);
				return;
			}
			__atomic_add_fetch(&queue->forced_drains, 1, __ATOMIC_RELAXED);
			tick_queue_drain(queue);
		}
		else if (tick_overlap_owner_blocked(queue->owner))
		{
			/* (the owner is waiting for this thread, or has nothing to run: it cannot take the record, so this thread does) */
			__atomic_add_fetch(&queue->forced_drains, 1, __ATOMIC_RELAXED);
			tick_queue_drain(queue);
		}
		else
		{
			struct timespec pause = { 0, 100000 };

			/* (the owner is running and will drain it, or is held at the HOME gate and the gate is this thread's too) */
			__sync_fetch_and_add(&queue->waiters, 1);
			tick_overlap_note_push_wait();
			nanosleep(&pause, NULL);
			tick_owners_safe_point();
			__sync_fetch_and_sub(&queue->waiters, 1);
		}
	}

	if (TICK_OWNERS_APPLY_AT_ONCE())
		tick_queue_drain(queue);
}

long tick_queue_drain(
	struct tick_queue *queue)
{
	/* (8-byte units, for the alignment of the records' structs) */
	unsigned long long record[HALO_TICK_QUEUE_RECORD_MAXIMUM / 8];
	long applied = 0;
	unsigned long limit;

	if (queue->draining)
		return 0;
	/* (the overlap: the game thread leaves its queues to the sync point while the tick thread runs a job, which pushes to them) */
	if (tick_overlap_defer_drain(queue))
		return 0;
	limit = tick_overlap_drain_limit();
	queue->draining = TRUE;
	for (;;)
	{
		pthread_mutex_lock(&queue->lock);
		/* (the tick thread takes what the game thread pushed before its job was posted, not what it pushes beside it) */
		if (queue->head == queue->tail || queue->tags[queue->head % (unsigned long)queue->capacity] > limit)
		{
			pthread_mutex_unlock(&queue->lock);
			break;
		}
		memcpy(record, queue->records + (queue->head % (unsigned long)queue->capacity) * (unsigned long)queue->record_size,
			(size_t)queue->record_size);
		queue->head++;
		pthread_mutex_unlock(&queue->lock);
		queue->apply(record);
		applied++;
	}
	queue->draining = FALSE;

	return applied;
}

long tick_owners_drain(
	long owner)
{
	struct tick_queue *queue;
	long applied = 0;

	if (!TICK_OWNERS_ON())
		return 0;
	/* (and the log says what the queues carried every 600 frames, a map's end not coming in a short run) */
	if (owner == TICK_OWNER_GAME && ++tick_owners_frames % 600 == 0)
		tick_owners_report();
	for (queue = __atomic_load_n(&tick_owners_queues, __ATOMIC_ACQUIRE); queue; queue = queue->next)
	{
		if (queue->owner == owner)
			applied += tick_queue_drain(queue);
	}

	return applied;
}

long tick_owners_drain_waited(
	void)
{
	struct tick_queue *queue;
	long applied = 0;

	if (!TICK_OWNERS_ON())
		return 0;
	for (queue = __atomic_load_n(&tick_owners_queues, __ATOMIC_ACQUIRE); queue; queue = queue->next)
	{
		if (__atomic_load_n(&queue->waiters, __ATOMIC_RELAXED) && queue->owner != TICK_OWNER_TICK)
			applied += tick_queue_drain(queue);
	}

	return applied;
}

void tick_owners_discard_all(
	void)
{
	struct tick_queue *queue;

	for (queue = __atomic_load_n(&tick_owners_queues, __ATOMIC_ACQUIRE); queue; queue = queue->next)
		tick_queue_discard(queue);
}

long tick_queue_discard(
	struct tick_queue *queue)
{
	long dropped;

	pthread_mutex_lock(&queue->lock);
	dropped = (long)(queue->tail - queue->head);
	queue->head = queue->tail;
	pthread_mutex_unlock(&queue->lock);

	return dropped;
}

long tick_queue_count(
	struct tick_queue *queue)
{
	long count;

	pthread_mutex_lock(&queue->lock);
	count = (long)(queue->tail - queue->head);
	pthread_mutex_unlock(&queue->lock);

	return count;
}

/* ---------- the object memory pool's hand-off */

static pthread_mutex_t tick_owners_pool_lock;
static pthread_once_t tick_owners_pool_once = PTHREAD_ONCE_INIT;
static unsigned long tick_owners_pool_compactions;
static unsigned long tick_owners_pool_waits;
static unsigned long tick_owners_pool_reported_compactions;
static unsigned long tick_owners_pool_reported_waits;

static void tick_owners_pool_lock_make(
	void)
{
	pthread_mutexattr_t attributes;

	pthread_mutexattr_init(&attributes);
#ifdef PTHREAD_MUTEX_RECURSIVE
	pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);
#else
	pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE_NP);
#endif
	pthread_mutex_init(&tick_owners_pool_lock, &attributes);
	pthread_mutexattr_destroy(&attributes);
}

void tick_owners_object_pool_reader_begin(
	void)
{
	if (!TICK_OWNERS_FEATURE(TICK_FEATURE_POOL))
		return;
	pthread_once(&tick_owners_pool_once, tick_owners_pool_lock_make);
	pthread_mutex_lock(&tick_owners_pool_lock);
}

void tick_owners_object_pool_reader_end(
	void)
{
	if (!TICK_OWNERS_FEATURE(TICK_FEATURE_POOL))
		return;
	pthread_mutex_unlock(&tick_owners_pool_lock);
}

void tick_owners_object_pool_compact(
	void (*compact)(void))
{
	if (!TICK_OWNERS_FEATURE(TICK_FEATURE_POOL))
	{
		compact();
		return;
	}
	pthread_once(&tick_owners_pool_once, tick_owners_pool_lock_make);
	if (pthread_mutex_trylock(&tick_owners_pool_lock))
	{
		/* (a frame is being drawn by another thread: this waits for its end) */
		tick_owners_pool_waits++;
		pthread_mutex_lock(&tick_owners_pool_lock);
	}
	compact();
	tick_owners_pool_compactions++;
	pthread_mutex_unlock(&tick_owners_pool_lock);
}

void tick_owners_report(
	void)
{
	struct tick_queue *queue;

	for (queue = __atomic_load_n(&tick_owners_queues, __ATOMIC_ACQUIRE); queue; queue = queue->next)
	{
		unsigned long pushed, forced, high_water, left;

		/* (the producers change the counts under the queue's lock) */
		pthread_mutex_lock(&queue->lock);
		pushed = queue->pushed_total - queue->pushed_at_report;
		forced = __atomic_load_n(&queue->forced_drains, __ATOMIC_RELAXED) - queue->forced_at_report;
		high_water = queue->high_water;
		left = queue->tail - queue->head;
		queue->pushed_at_report = queue->pushed_total;
		queue->forced_at_report += forced;
		queue->high_water = 0;
		pthread_mutex_unlock(&queue->lock);
		if (pushed || forced)
		{
			platform_log("tick owners: the %s queue took %lu requests since the last report (most waiting at once %lu of %ld, drained to make room %lu times, %lu left)",
				queue->name, pushed, high_water, queue->capacity, forced, left);
		}
	}
	if (tick_owners_pool_compactions != tick_owners_pool_reported_compactions)
	{
		platform_log("tick owners: the object memory pool was compacted %lu times since the last report (%lu waited for a frame to end)",
			tick_owners_pool_compactions - tick_owners_pool_reported_compactions, tick_owners_pool_waits - tick_owners_pool_reported_waits);
		tick_owners_pool_reported_compactions = tick_owners_pool_compactions;
		tick_owners_pool_reported_waits = tick_owners_pool_waits;
	}
}

#else /* HALO_WIIU */

/* (an empty unit is not C) */
typedef int tick_owners_not_for_this_platform;

#endif
