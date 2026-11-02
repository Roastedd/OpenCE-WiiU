/*
The render view snapshot's ring (port/linux/game/tick_ring.h), apart from the game: the rules for which slot a capture is written to, what is committed,
and what a frame has pinned, checked against a tick thread and a drawing thread that really run beside each other (under the thread sanitizer, where
there is one), with random jobs of 0 to 7 ticks and the pair a frame draws from chosen as tick_overlap.c chooses it.

    test_tick_ring rules | threads | broken
Exit status 0 when everything holds; a failing check says which and exits 1. "broken" must fail: the same threads with the tick thread allowed to
write a pinned slot.
*/

#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../linux/game/tick_ring.h"

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("check failed, line %d: %s\n", __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

#define SLOTS 4
#define WORDS 256

/* a capture's data: every word its tick */
static long slot_words[SLOTS][WORDS];

static void write_capture(
	int index,
	long tick)
{
	int word;

	for (word = 0; word < WORDS; word++)
		slot_words[index][word] = tick;
}

static int read_capture_ok(
	int index,
	long tick)
{
	int word;

	for (word = 0; word < WORDS; word++)
	{
		if (slot_words[index][word] != tick)
			return 0;
	}
	return 1;
}

/* ---------- the rules, on one thread */

static void test_rules(void)
{
	struct tick_ring ring;
	int slot, other;
	long tick;

	tick_ring_reset(&ring, SLOTS);
	CHECK(tick_ring_published(&ring) == TICK_RING_NONE);
	CHECK(tick_ring_pin(&ring, 5) == 0);
	CHECK(tick_ring_find(&ring, 5) < 0);

	/* four unused slots are taken first, none twice */
	for (tick = 1; tick <= SLOTS; tick++)
	{
		slot = tick_ring_slot_for(&ring, tick);
		CHECK(slot >= 0);
		tick_ring_begin_write(&ring, slot, tick);
		write_capture(slot, tick);
		tick_ring_end_write(&ring, slot, 0);
	}
	CHECK(tick_ring_published(&ring) == TICK_RING_NONE);
	/* nothing committed: no slot may be overwritten (the captures of the job being run are all newer than "committed") */
	CHECK(tick_ring_slot_for(&ring, 5) < 0);
	tick_ring_commit(&ring);
	CHECK(tick_ring_published(&ring) == 4);
	/* committed 4: ticks 3 and 4 are kept, 1 is the oldest to go */
	slot = tick_ring_slot_for(&ring, 5);
	CHECK(slot == tick_ring_find(&ring, 1));
	/* a capture of a tick that has a slot is written to it (the serial mode's catch-up) */
	CHECK(tick_ring_slot_for(&ring, 4) == tick_ring_find(&ring, 4));
	/* pinned: a frame on 4 and 3 */
	CHECK(tick_ring_pin(&ring, 4) == 1);
	CHECK(tick_ring_pinned(&ring, tick_ring_find(&ring, 4)));
	CHECK(tick_ring_pinned(&ring, tick_ring_find(&ring, 3)));
	CHECK(!tick_ring_pinned(&ring, tick_ring_find(&ring, 2)));
	/* a catch-up capture of 4 while 4 is pinned goes elsewhere */
	other = tick_ring_slot_for(&ring, 4);
	CHECK(other >= 0 && other != tick_ring_find(&ring, 4) && other != tick_ring_find(&ring, 3));
	/* two captures of a job, the oldest slots, neither pinned nor the newest two committed */
	slot = tick_ring_slot_for(&ring, 5);
	CHECK(slot == tick_ring_find(&ring, 1));
	tick_ring_begin_write(&ring, slot, 5);
	write_capture(slot, 5);
	tick_ring_end_write(&ring, slot, 0);
	other = tick_ring_slot_for(&ring, 6);
	CHECK(other == tick_ring_find(&ring, 2));
	tick_ring_begin_write(&ring, other, 6);
	write_capture(other, 6);
	tick_ring_end_write(&ring, other, 0);
	/* a third would find none: more than a job may capture */
	CHECK(tick_ring_slot_for(&ring, 7) < 0);
	/* the frame's pair is intact */
	CHECK(read_capture_ok(tick_ring_find(&ring, 4), 4));
	CHECK(read_capture_ok(tick_ring_find(&ring, 3), 3));
	tick_ring_commit(&ring);
	CHECK(tick_ring_published(&ring) == 6);
	tick_ring_unpin(&ring);
	CHECK(!tick_ring_pinned(&ring, tick_ring_find(&ring, 4)));
	/* a pin of a tick not yet committed fails */
	CHECK(tick_ring_pin(&ring, 7) == 0);
	/* a pin of the first tick (no tick before it) has one pinned */
	tick_ring_reset(&ring, SLOTS);
	slot = tick_ring_slot_for(&ring, 1);
	tick_ring_begin_write(&ring, slot, 1);
	tick_ring_end_write(&ring, slot, 1);
	CHECK(tick_ring_pin(&ring, 1) == 1);
	CHECK(ring.pinned[1] == -1);
}

/* ---------- a tick thread and a drawing thread */

static struct tick_ring ring;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t posted = PTHREAD_COND_INITIALIZER;
static int pending;
static int job_ticks;
static int broken_mode;
static long clock_time; /* the tick thread's, read by the drawing thread only when the tick thread is idle */
static int stop;

/* a ring that writes over the newest committed capture, which a frame draws from (what a broken tick_ring_slot_for would do) */
static int naive_slot_for(long tick)
{
	int index, best = -1;

	(void)tick;
	for (index = 0; index < SLOTS; index++)
	{
		if (!ring.ready[index])
			return index;
		if (best < 0 || ring.tick[index] > ring.tick[best])
			best = index;
	}
	return best;
}

/* the tick thread: for each job, n ticks, capturing the last two, and committing at the end */
static void *tick_main(void *unused)
{
	(void)unused;
	pthread_mutex_lock(&lock);
	for (;;)
	{
		int ticks, tick_index;

		while (!__atomic_load_n(&pending, __ATOMIC_RELAXED) && !stop)
			pthread_cond_wait(&posted, &lock);
		if (stop)
			break;
		ticks = job_ticks;
		pthread_mutex_unlock(&lock);
		for (tick_index = 0; tick_index < ticks; tick_index++)
		{
			const long tick = clock_time + 1;

			clock_time = tick;
			if (ticks - tick_index <= 2)
			{
				int slot = broken_mode ? naive_slot_for(tick) : tick_ring_slot_for(&ring, tick);

				if (slot < 0)
				{
					printf("the ring had no slot for tick %ld\n", tick);
					failures++;
					continue;
				}
				tick_ring_begin_write(&ring, slot, tick);
				write_capture(slot, tick);
				tick_ring_end_write(&ring, slot, 0);
			}
			sched_yield();
		}
		tick_ring_commit(&ring);
		pthread_mutex_lock(&lock);
		__atomic_store_n(&pending, 0, __ATOMIC_RELEASE);
	}
	pthread_mutex_unlock(&lock);
	return NULL;
}

static void wait_job(void)
{
	while (__atomic_load_n(&pending, __ATOMIC_ACQUIRE))
		sched_yield();
	pthread_mutex_lock(&lock);
	pthread_mutex_unlock(&lock);
}

static unsigned long random_state = 12345;

static unsigned long next_random(void)
{
	random_state = random_state * 1103515245UL + 12345UL;
	return (random_state >> 16) & 0x7FFF;
}

static void test_threads(void)
{
	pthread_t thread;
	long time_now = 0;
	int frame, frames = 4000;
	long drawn_ticks = 0, held = 0, unpinned = 0;

	tick_ring_reset(&ring, SLOTS);
	clock_time = 0;
	pthread_create(&thread, NULL, tick_main, NULL);
	for (frame = 0; frame < frames; frame++)
	{
		/* the sync point: the last job is over */
		int ticks;
		long want, tick, published;
		int pinned_ok, reads, pinned_latest, pinned_before;

		wait_job();
		/* a frame's worth of time: mostly 0 or 1 ticks, now and then a long frame */
		ticks = (int)(next_random() % 8);
		if (ticks > 2 && next_random() % 4)
			ticks = (int)(next_random() % 2);
		published = tick_ring_published(&ring);
		want = time_now + ticks - 1;
		tick = want < published ? want : published;
		pinned_ok = published != TICK_RING_NONE && tick_ring_pin(&ring, tick);
		pinned_latest = ring.pinned[0];
		pinned_before = ring.pinned[1];
		/* (the job is handed over: the pins are in place before it runs) */
		pthread_mutex_lock(&lock);
		job_ticks = ticks;
		__atomic_store_n(&pending, 1, __ATOMIC_RELEASE);
		pthread_cond_signal(&posted);
		pthread_mutex_unlock(&lock);
		time_now += ticks;
		if (!pinned_ok)
		{
			unpinned++;
			/* no pair yet: wait for the job and go on, as the prime does */
			wait_job();
			continue;
		}
		if (tick < want)
			held++;
		/* the frame: reads its pair, many times, while the tick thread writes the next captures */
		for (reads = 0; reads < 20; reads++)
		{
			/* (the slots it pinned: it does not look at the others', which the tick thread may be writing) */
			const int latest = pinned_latest;
			const int before = pinned_before;

			CHECK(latest >= 0);
			if (latest >= 0)
			{
				CHECK(read_capture_ok(latest, tick));
				if (before >= 0)
					CHECK(read_capture_ok(before, tick - 1));
			}
			if (failures > 20)
				break;
			sched_yield();
		}
		drawn_ticks++;
		tick_ring_unpin(&ring);
		if (failures > 20)
			break;
	}
	wait_job();
	pthread_mutex_lock(&lock);
	stop = 1;
	pthread_cond_signal(&posted);
	pthread_mutex_unlock(&lock);
	pthread_join(thread, NULL);
	printf("%ld frames drawn, %ld with the last pose held, %ld with no pair; the clock at %ld\n", drawn_ticks, held, unpinned, clock_time);
	CHECK(drawn_ticks > frames / 2);
}

int main(int count, char **arguments)
{
	const char *which = count > 1 ? arguments[1] : "rules";

	if (!strcmp(which, "rules"))
		test_rules();
	else if (!strcmp(which, "threads"))
		test_threads();
	else if (!strcmp(which, "broken"))
	{
		broken_mode = 1;
		test_threads();
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
