/* The hand-over protocol with a real second thread: the stream queue and the in-flight ranges. A producer thread makes "draws" that read a region of a shared memory, writes regions again after
asking (as the game's Lock does) what is in flight, and posts streams; a consumer thread, with random delays, runs them and checks that every draw still finds the bytes the producer made it
from. The producer that does not ask (the control) must be caught, or the test would prove nothing. */

#include "../src/wiiu_inflight.h"
#include "../src/wiiu_stream_queue.h"

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLOTS 3
#define REGIONS 24
#define REGION_BYTES 256
#define DRAWS_PER_STREAM_MAX 12
#define STREAMS 3000

struct draw
{
	unsigned region;
	uint32_t value; /* what the region held when the draw was made */
};

struct stream
{
	unsigned count;
	struct draw draws[DRAWS_PER_STREAM_MAX];
};

static struct stream_queue queue;
static struct stream streams[SLOTS];
static uint32_t memory[REGIONS][REGION_BYTES / 4];
static struct inflight inflight;
static volatile int producer_done;
static unsigned violations;
static int asks; /* the producer asks (1) or does not (0, the control) */

static void pause_a_moment(void)
{
	sched_yield();
}

static void *consumer(void *unused)
{
	unsigned slot, spins = 0;
	uint32_t used, submission;

	(void)unused;
	for (;;)
	{
		if (!stream_queue_take(&queue, &slot, &used, &submission))
		{
			if (producer_done && __atomic_load_n(&queue.posted, __ATOMIC_ACQUIRE) == queue.taken)
				return NULL;
			pause_a_moment();
			continue;
		}
		/* (a random delay: the consumer is slow now and then, as a GPU waiting for the display is) */
		if (rand() % 4 == 0)
			for (spins = 0; spins < (unsigned)(rand() % 3000); spins++)
				__asm__ volatile("" : : : "memory");
		{
			unsigned i, word;
			const struct stream *stream = &streams[slot];

			for (i = 0; i < stream->count; i++)
				for (word = 0; word < REGION_BYTES / 4; word++)
					if (__atomic_load_n(&memory[stream->draws[i].region][word], __ATOMIC_RELAXED) != stream->draws[i].value)
					{
						__atomic_fetch_add(&violations, 1, __ATOMIC_RELAXED);
						break;
					}
		}
		stream_queue_retire(&queue);
	}
}

/* the producer posts what it has written and waits for a free slot for the next */
static void post_and_prepare(unsigned *count)
{
	streams[stream_queue_slot(&queue)].count = *count;
	stream_queue_post(&queue, *count);
	*count = 0;
	while (!stream_queue_slot_free(&queue))
		pause_a_moment();
}

/* a write to a region the game announces: what reads it must be done first */
static void announce_write(unsigned region, unsigned *count)
{
	uint32_t hazard;

	if (!asks)
		return;
	hazard = inflight_hazard(&inflight, memory[region], REGION_BYTES, stream_queue_retired(&queue));
	if (!hazard)
		return;
	if (hazard == stream_queue_submission(&queue))
		post_and_prepare(count); /* (the stream still being written is handed over first) */
	while (stream_queue_retired(&queue) < hazard)
		pause_a_moment();
}

static unsigned run(int ask)
{
	pthread_t thread;
	unsigned count = 0, stream_number;
	uint32_t value = 1;
	unsigned region;

	asks = ask;
	violations = 0;
	producer_done = 0;
	stream_queue_init(&queue, SLOTS);
	inflight_clear(&inflight);
	for (region = 0; region < REGIONS; region++)
		for (stream_number = 0; stream_number < REGION_BYTES / 4; stream_number++)
			memory[region][stream_number] = value;
	pthread_create(&thread, NULL, consumer, NULL);
	for (stream_number = 0; stream_number < STREAMS; stream_number++)
	{
		unsigned operations = 3 + (unsigned)rand() % 20, i;

		for (i = 0; i < operations; i++)
		{
			region = (unsigned)rand() % REGIONS;
			if (rand() % 3 == 0)
			{
				unsigned word;

				announce_write(region, &count);
				value++;
				for (word = 0; word < REGION_BYTES / 4; word++)
					__atomic_store_n(&memory[region][word], value, __ATOMIC_RELAXED);
			}
			else
			{
				/* a draw reading the region as it is now (the draw's record is in the current stream's slot, which is free: the producer waited for it) */
				streams[stream_queue_slot(&queue)].draws[count].region = region;
				streams[stream_queue_slot(&queue)].draws[count].value = __atomic_load_n(&memory[region][0], __ATOMIC_RELAXED);
				inflight_add(&inflight, memory[region], REGION_BYTES, stream_queue_submission(&queue), stream_queue_retired(&queue));
				count++;
				if (count == DRAWS_PER_STREAM_MAX)
					post_and_prepare(&count);
			}
		}
		if (count)
			post_and_prepare(&count);
	}
	producer_done = 1;
	pthread_join(thread, NULL);
	return violations;
}

int main(void)
{
	int failures = 0;
	unsigned with, without;

	srand(12345);
	with = run(1);
	if (with)
	{
		printf("FAIL: the producer that asks was caught %u times\n", with);
		failures++;
	}
	if (stream_queue_retired(&queue) != STREAMS + 0 && stream_queue_retired(&queue) < STREAMS)
	{
		printf("FAIL: %u of the streams were retired\n", (unsigned)stream_queue_retired(&queue));
		failures++;
	}
	without = run(0);
	if (!without)
	{
		printf("FAIL: the control (a producer that does not ask) was never caught: the test cannot see a violation\n");
		failures++;
	}
	/* the queue's own rules */
	{
		struct stream_queue q;
		unsigned slot;
		uint32_t used, submission;

		stream_queue_init(&q, 3);
		if (!stream_queue_slot_free(&q) || stream_queue_submission(&q) != 1 || stream_queue_slot(&q) != 0)
		{
			printf("FAIL: a new queue\n");
			failures++;
		}
		stream_queue_post(&q, 10);
		stream_queue_post(&q, 20);
		if (!stream_queue_slot_free(&q))
		{
			printf("FAIL: two in flight of three slots leave the third free\n");
			failures++;
		}
		stream_queue_post(&q, 30);
		if (stream_queue_slot_free(&q))
		{
			printf("FAIL: three in flight of three slots leave none free\n");
			failures++;
		}
		if (!stream_queue_take(&q, &slot, &used, &submission) || slot != 0 || used != 10 || submission != 1)
		{
			printf("FAIL: the first stream taken\n");
			failures++;
		}
		stream_queue_retire(&q);
		if (!stream_queue_slot_free(&q) || stream_queue_retired(&q) != 1)
		{
			printf("FAIL: one retired frees a slot\n");
			failures++;
		}
		if (!stream_queue_take(&q, &slot, &used, &submission) || used != 20 || submission != 2 || !stream_queue_take(&q, &slot, &used, &submission) || used != 30 ||
			stream_queue_take(&q, &slot, &used, &submission))
		{
			printf("FAIL: the second and third streams taken, and no fourth\n");
			failures++;
		}
	}
	/* the in-flight table never fails and never forgets */
	{
		static unsigned char big[400000];
		unsigned long k;
		struct inflight table;

		inflight_clear(&table);
		for (k = 0; k < 20000; k++)
			inflight_add(&table, big + (k * 7919ul) % 390000ul, 16, (uint32_t)(k / 100 + 1), 0);
		for (k = 0; k < 20000; k++)
			if (inflight_hazard(&table, big + (k * 7919ul) % 390000ul, 16, 0) < (uint32_t)(k / 100 + 1))
			{
				printf("FAIL: an in-flight range lost its submission\n");
				failures++;
				break;
			}
		if (inflight_hazard(&table, big, 16, 1000000))
		{
			printf("FAIL: a retired submission is a hazard\n");
			failures++;
		}
	}
	printf(failures ? "%d failures (the control: %u violations)\n" : "stream queue: all passed (the control was caught %u times)\n", failures ? failures : (int)without, without);
	return failures != 0;
}
