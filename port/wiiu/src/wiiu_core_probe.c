/*
WIIU_CORE_PROBE.C

What the plan to run the GX2 backend on another core (reports/research/2026-10-23/two-core-render-plan.md) has to know of the Espresso, measured at startup when halo/probe_core.txt exists (a line of "PROBE" in the
log; the game thread is on core 1, the probe's thread on core 2):
 1. whether a write on one core is seen by the other with no cache flush (a handshake through a word and four words of data, 3000 times each way: the data seen stale are counted), with and without
    DCFlushRange / DCInvalidateRange, and what a round trip costs;
 2. what it costs a core to read 512 KB another wrote a moment ago, flushed or not, against 512 KB it wrote itself;
 3. what an OS semaphore costs for the same round trip (the wake-up of a thread that sleeps).
*/

#include "wiiu_shell.h"

#include <coreinit/cache.h>
#include <coreinit/core.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/semaphore.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>

#include <stdint.h>
#include <string.h>

#define ROUNDS 3000
#define BUFFER_WORDS (512 * 1024 / 4)

static volatile uint32_t go, ack, mode, stop_thread;
static volatile uint32_t data[8] __attribute__((aligned(64)));
static uint32_t *big;
static volatile uint32_t big_sum, big_ticks;
static OSSemaphore to_other, to_game;
static OSThread other;

static void barrier(void)
{
	__asm__ volatile("sync" : : : "memory");
}

/* the other core: does what mode says, then acknowledges */
static int other_main(int argc, const char **argv)
{
	uint32_t seen = 0;

	(void)argc;
	(void)argv;
	while (!stop_thread)
	{
		const uint32_t asked = go;

		if (asked == seen)
			continue;
		seen = asked;
		switch (mode)
		{
		case 1: /* handshake: the data must be what the game wrote, as the word says */
		case 2:
			{
				uint32_t i, bad = 0;

				if (mode == 2)
					DCInvalidateRange((void *)data, sizeof(data));
				for (i = 0; i < 8; i++)
					if (data[i] != asked)
						bad++;
				/* (the answer carries the count of stale words in the top bits) */
				barrier();
				ack = asked | (bad << 24);
			}
			break;
		case 3: /* reads the big buffer the game wrote */
		case 4:
			{
				const OSTime start = OSGetSystemTime();
				uint32_t sum = 0, i;

				if (mode == 4)
					DCInvalidateRange(big, BUFFER_WORDS * 4);
				for (i = 0; i < BUFFER_WORDS; i++)
					sum += big[i];
				big_ticks = (uint32_t)(OSGetSystemTime() - start);
				big_sum = sum;
				barrier();
				ack = asked;
			}
			break;
		case 5: /* writes the big buffer itself, then the game reads it */
			{
				uint32_t i;

				for (i = 0; i < BUFFER_WORDS; i++)
					big[i] = asked;
				barrier();
				ack = asked;
			}
			break;
		case 6: /* a semaphore round trip */
			OSWaitSemaphore(&to_other);
			OSSignalSemaphore(&to_game);
			ack = asked;
			break;
		default:
			ack = asked;
			break;
		}
	}
	return 0;
}

static uint32_t next_id;

/* asks the other core to do a mode and waits for it: the time in microseconds, 0 if it did not answer in 200 ms; *answer is its word */
static uint32_t ask(uint32_t what, uint32_t *answer)
{
	const OSTime start = OSGetSystemTime();
	const uint32_t id = ++next_id & 0xffffff;

	mode = what;
	barrier();
	go = id;
	while ((ack & 0xffffff) != id)
	{
		if (OSGetSystemTime() - start > OSMillisecondsToTicks(200))
			return 0;
	}
	if (answer)
		*answer = ack >> 24;
	return (uint32_t)OSTicksToMicroseconds(OSGetSystemTime() - start) + 1;
}

void wiiu_core_probe(void);

void wiiu_core_probe(void)
{
	const uint32_t stack_size = 64 * 1024;
	uint8_t *stack = MEMAllocFromDefaultHeapEx(stack_size, 16);
	uint32_t round, stale_words, bad, total, answers;
	OSTime start;
	unsigned variant;

	big = MEMAllocFromDefaultHeapEx(BUFFER_WORDS * 4, 64);
	if (!stack || !big)
		return;
	OSInitSemaphore(&to_other, 0);
	OSInitSemaphore(&to_game, 0);
	if (!OSCreateThread(&other, other_main, 0, NULL, stack + stack_size, stack_size, OSGetThreadPriority(OSGetCurrentThread()), OS_THREAD_ATTRIB_AFFINITY_CPU2))
	{
		wiiu_log("PROBE: no thread on core 2");
		return;
	}
	OSSetThreadName(&other, "probe");
	OSResumeThread(&other);
	wiiu_log("PROBE: the game is on core %u, the probe on core 2", (unsigned)OSGetCoreId());
	OSSleepTicks(OSMillisecondsToTicks(20));

	/* 1: handshakes. The game writes eight words and a flag, the other core reads them: without any flush, then with the game flushing and the other invalidating */
	for (variant = 0; variant < 2; variant++)
	{
		uint32_t worst = 0, lost = 0, total_us = 0;

		stale_words = 0;
		for (round = 1; round <= ROUNDS; round++)
		{
			uint32_t i, us;

			for (i = 0; i < 8; i++)
				data[i] = next_id + 1; /* (the id ask() will use) */
			if (variant == 1)
				DCFlushRange((void *)data, sizeof(data));
			barrier();
			us = ask(variant + 1, &bad);
			if (!us)
				lost++;
			else
			{
				stale_words += bad;
				total_us += us;
				if (us > worst)
					worst = us;
			}
		}
		wiiu_log("PROBE: handshake %s: %u rounds, %u answered, stale words seen %u, round trip %.2f us on average, worst %u us", variant ? "with DCFlushRange and DCInvalidateRange" : "with no flush at all",
			(unsigned)ROUNDS, (unsigned)(ROUNDS - lost), (unsigned)stale_words, ROUNDS - lost ? (double)total_us / (double)(ROUNDS - lost) : 0.0, (unsigned)worst);
	}

	/* 2: 512 KB written by one core and read by the other */
	for (variant = 0; variant < 3; variant++)
	{
		uint32_t i, sum = 0, expected = 0;
		OSTime t;

		for (i = 0; i < BUFFER_WORDS; i++)
		{
			big[i] = i * 3u + variant;
			expected += i * 3u + variant;
		}
		if (variant == 1)
			DCFlushRange(big, BUFFER_WORDS * 4);
		barrier();
		total = ask(variant == 1 ? 4 : 3, &answers);
		(void)answers;
		(void)total;
		sum = big_sum;
		wiiu_log("PROBE: the other core reads 512 KB the game wrote %s: %.2f ms, the sum is %s", variant == 0 ? "with no flush" : variant == 1 ? "flushed (DCFlushRange, DCInvalidateRange)" : "with no flush, a second time",
			(double)OSTicksToMicroseconds(big_ticks) / 1000.0, sum == expected ? "right" : "WRONG (stale data)");
		if (variant == 2)
			break;
		(void)t;
	}
	/* the game reads 512 KB the other core wrote, and 512 KB it wrote itself */
	{
		uint32_t i, sum = 0;

		ask(5, NULL);
		start = OSGetSystemTime();
		for (i = 0; i < BUFFER_WORDS; i++)
			sum += big[i];
		wiiu_log("PROBE: the game reads 512 KB the other core wrote (no flush): %.2f ms, the sum is %s", (double)OSTicksToMicroseconds(OSGetSystemTime() - start) / 1000.0,
			sum == next_id * BUFFER_WORDS ? "right" : "WRONG (stale data)");
		for (i = 0; i < BUFFER_WORDS; i++)
			big[i] = i;
		start = OSGetSystemTime();
		for (i = 0; i < BUFFER_WORDS; i++)
			sum += big[i];
		wiiu_log("PROBE: the game reads 512 KB it wrote itself: %.2f ms", (double)OSTicksToMicroseconds(OSGetSystemTime() - start) / 1000.0);
	}

	/* 3: semaphores: the game signals, the other core wakes, signals back */
	{
		uint32_t us = 0;

		start = OSGetSystemTime();
		for (round = 0; round < 1000; round++)
		{
			mode = 6;
			barrier();
			go = ++next_id;
			OSSignalSemaphore(&to_other);
			OSWaitSemaphore(&to_game);
			while ((ack & 0xffffff) != (next_id & 0xffffff))
				;
		}
		us = (uint32_t)OSTicksToMicroseconds(OSGetSystemTime() - start);
		wiiu_log("PROBE: an OS semaphore round trip between the cores: %.1f us on average over 1000", (double)us / 1000.0);
	}
	stop_thread = 1;
	barrier();
	OSJoinThread(&other, NULL);
	wiiu_log("PROBE: done");
}
