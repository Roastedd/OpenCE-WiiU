/*
WIIU_PROFILE.C

A sampling profiler for the game's thread, for finding where a frame's time goes (the console has no profiler we can use). A thread on the game's core, at a priority above
the game's, wakes every millisecond; the game thread is then stopped by the system and its context holds the exact address it was at (srr0), which is counted. Every ten
seconds the busiest 16-byte pieces of code are logged ("PROF pc=0x... n=..."); tools/wiiu/profile_report.py groups them by function with the game's ELF. A thread blocked in the
system (waiting for the display, a file or the GPU) shows as an address in the system's code (0x01...), which says how much of the time is waiting.

It runs only if halo/profile.txt exists (its first number: seconds between reports, 10 if none), and costs about 2 percent of the core.
*/

#include "wiiu_shell.h"

#include <coreinit/core.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define TABLE_SIZE 8192
#define REPORT_LINES 200

static struct
{
	uint32_t pc;
	uint32_t lr; /* where the function was called from, which says who the caller of memcpy was */
	uint32_t up; /* where the function the sample is in (or, if it has no frame of its own, the one that called it) was called from: who called csmemcpy */
	uint32_t count;
} table[TABLE_SIZE];
static OSThread *target;
static OSThread thread;
static unsigned report_seconds = 10;
static uint32_t samples, lost;

static void note(uint32_t pc, uint32_t lr, uint32_t up)
{
	uint32_t key = pc >> 4, slot = ((key * 2654435761u) ^ (lr * 40503u) ^ (up * 31u)) % TABLE_SIZE, probes;

	for (probes = 0; probes < 64; probes++, slot = (slot + 1) % TABLE_SIZE)
	{
		if (table[slot].count == 0)
		{
			table[slot].pc = key << 4;
			table[slot].lr = lr;
			table[slot].up = up;
			table[slot].count = 1;
			return;
		}
		if (table[slot].pc == (key << 4) && table[slot].lr == lr && table[slot].up == up)
		{
			table[slot].count++;
			return;
		}
	}
	lost++;
}

static void report(void)
{
	unsigned line, i, best;
	static uint8_t done[TABLE_SIZE];

	wiiu_log("PROF %u samples, %u lost, function addresses: wiiu_log %08x", (unsigned)samples, (unsigned)lost, (unsigned)(uintptr_t)&wiiu_log);
	memset(done, 0, sizeof(done));
	for (line = 0; line < REPORT_LINES; line++)
	{
		best = TABLE_SIZE;
		for (i = 0; i < TABLE_SIZE; i++)
			if (table[i].count && !done[i] && (best == TABLE_SIZE || table[i].count > table[best].count))
				best = i;
		if (best == TABLE_SIZE)
			break;
		done[best] = 1;
		wiiu_log("PROF pc=0x%08x lr=0x%08x up=0x%08x n=%u", (unsigned)table[best].pc, (unsigned)table[best].lr, (unsigned)table[best].up, (unsigned)table[best].count);
	}
	memset(table, 0, sizeof(table));
	samples = lost = 0;
}

/* The game's code is loaded at about 0x0c000000 (the RPX's 0x02000000 and the load offset: the log's function addresses say where). A thread waiting in the system is at
an address of the system's: its stack is followed (each frame's first word is the caller's frame, the second the return address) to the first return address in the game's code,
which says what it was waiting for. */
static uint32_t caller_in_code(uint32_t pc, uint32_t lr)
{
	uint32_t sp = (uint32_t)target->context.gpr[1];
	unsigned depth;
	const uint32_t low = (uint32_t)(uintptr_t)target->stackEnd, high = (uint32_t)(uintptr_t)target->stackStart;

	if (pc >= 0x0c000000u && pc < 0x0c800000u)
		return lr;
	if (lr >= 0x0c000000u && lr < 0x0c800000u)
		return lr;
	for (depth = 0; depth < 12; depth++)
	{
		const uint32_t *frame;
		uint32_t ret;

		if (sp < low || sp + 8 > high || (sp & 7))
			break;
		frame = (const uint32_t *)(uintptr_t)sp;
		ret = frame[1];
		if (ret >= 0x0c000000u && ret < 0x0c800000u)
			return ret;
		sp = frame[0];
	}
	return lr;
}

/* the return address in the frame above the one the stack pointer is in: (the stack pointer is the sampled function's own frame if it has one; if it is a leaf, the frame of its
caller, and then this is the caller's return address) */
static uint32_t return_above(void)
{
	const uint32_t sp = (uint32_t)target->context.gpr[1];
	const uint32_t low = (uint32_t)(uintptr_t)target->stackEnd, high = (uint32_t)(uintptr_t)target->stackStart;
	uint32_t chain;

	if (sp < low || sp + 8 > high || (sp & 7))
		return 0;
	chain = *(const uint32_t *)(uintptr_t)sp;
	if (chain <= sp || chain + 8 > high || (chain & 7))
		return 0;
	return *(const uint32_t *)(uintptr_t)(chain + 4);
}

static int sampler(int argc, const char **argv)
{
	OSTime last_report = OSGetSystemTime();

	(void)argc;
	(void)argv;
	for (;;)
	{
		OSSleepTicks(OSMillisecondsToTicks(1));
		{
			const uint32_t pc = (uint32_t)target->context.srr0, lr = (uint32_t)target->context.lr;

			note(pc, caller_in_code(pc, lr), pc >= 0x0c000000u && pc < 0x0c800000u ? return_above() : 0);
		}
		samples++;
		if (OSGetSystemTime() - last_report >= OSSecondsToTicks(report_seconds))
		{
			last_report = OSGetSystemTime();
			report();
		}
	}
	return 0;
}

/* called by the game's own thread (the renderer's start) */
void wiiu_profile_start(void);

void wiiu_profile_start(void)
{
	FILE *file = fopen(WIIU_DATA_DIRECTORY "/profile.txt", "r");
	void *stack;
	const uint32_t stack_size = 64 * 1024;
	unsigned seconds = 0;

	if (!file || target)
		return;
	if (fscanf(file, "%u", &seconds) == 1 && seconds >= 2)
		report_seconds = seconds;
	fclose(file);
	target = OSGetCurrentThread();
	stack = MEMAllocFromDefaultHeapEx(stack_size, 16);
	if (!stack)
		return;
	if (!OSCreateThread(&thread, sampler, 0, NULL, (uint8_t *)stack + stack_size, stack_size, OSGetThreadPriority(target) - 1,
		(OSThreadAttributes)OS_THREAD_ATTRIB_AFFINITY_CPU1))
		return;
	OSSetThreadName(&thread, "profiler");
	OSResumeThread(&thread);
	wiiu_log("Profiler: sampling the game thread every millisecond, reporting every %u s", report_seconds);
}
