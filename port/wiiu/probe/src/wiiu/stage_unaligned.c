/*
What a PowerPC 750 core does with an access that is not aligned to its size. The engine reads
several map streams at odd offsets (16-bit times and floats in animation and recorded-animation
data); x86 does not mind, a 750 does some of them in hardware, some by an alignment exception.

The first run on a console found the ones that matter: integer loads and stores work at every
misalignment, across a cache line and across a page, at about a nanosecond; an lfs (a single float
load) at an address one past a word boundary froze the console, so that it had to be powered off, and
no note was written. This stage is built so that it can find out about the rest without that:

 - each case runs on a thread of its own on another core than the main thread, which waits for it
   for a few seconds; a case that does not come back is a core lost to it, said so, recorded, and the
   next case goes to another core (the main thread, which the system needs for HOME, is not at risk);
 - the fault handler (crash.c) counts an alignment exception, skips the instruction and resumes,
   which tells whether the system delivers one at all, and costs nothing if it does not;
 - the case in progress is written to the card before it runs; a case that froze the console is in
   the file of fatal cases at the next start, and is not tried again. The cases already known to
   freeze it are in the table below.

The accesses are single load and store instructions (inline assembly: the compiler would otherwise
split an unaligned access into bytes, and nothing would be measured).
*/

#include "../stage.h"
#include "card.h"
#include "crash.h"

#include <coreinit/core.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <malloc.h>
#include <stdint.h>
#include <string.h>

#define ITERATIONS 20000
#define PAGE 4096
#define LINE 32
#define CASE_COUNT 24
#define STACK_BYTES (12 * 1024)
#define PATIENCE_MS 5000

typedef enum access_kind
{
	LOAD_HALF,
	LOAD_WORD,
	LOAD_FLOAT,
	LOAD_DOUBLE,
	STORE_WORD,
	STORE_DOUBLE
} access_kind;

typedef struct access_case
{
	const char *name;
	access_kind kind;
	size_t offset; /* from a page-aligned buffer of two pages */
} access_case;

/* the cases: the ones that cannot hurt first, then those that might, the likeliest to hurt last */
static const access_case cases[] = {
	{"lhz aligned", LOAD_HALF, 0},
	{"lhz at odd address", LOAD_HALF, 1},
	{"lwz aligned", LOAD_WORD, 0},
	{"lwz at +1", LOAD_WORD, 1},
	{"lwz at +2", LOAD_WORD, 2},
	{"lwz at +3", LOAD_WORD, 3},
	{"lwz across a 32-byte line", LOAD_WORD, LINE - 2},
	{"lwz across a 4 KiB page", LOAD_WORD, PAGE - 2},
	{"stw at +1", STORE_WORD, 1},
	{"stw across a 4 KiB page", STORE_WORD, PAGE - 2},
	{"lfs aligned", LOAD_FLOAT, 0},
	{"lfd aligned", LOAD_DOUBLE, 0},
	{"lfd at +4", LOAD_DOUBLE, 4},
	{"stfd at +4", STORE_DOUBLE, 4},
	{"lfs at +2", LOAD_FLOAT, 2},
	{"lfs across a 32-byte line", LOAD_FLOAT, LINE - 2},
	{"lfd at +1", LOAD_DOUBLE, 1},
	{"lfd across a 32-byte line", LOAD_DOUBLE, LINE - 4},
	{"lfd across a 4 KiB page", LOAD_DOUBLE, PAGE - 4},
	{"stfd at +1", STORE_DOUBLE, 1},
	{"lfs at +1", LOAD_FLOAT, 1},
};

/* froze the console, in the first two runs (2026-10-18), whatever was around it: not to be tried again */
static const char *const known_fatal[] = {"lfs at +1"};

typedef struct case_job
{
	volatile int done;
	access_kind kind;
	uint8_t *address;
	uint64_t (*now)(void);
	uint64_t elapsed;
} case_job;

static OSThread threads[CASE_COUNT];
static uint8_t stacks[CASE_COUNT][STACK_BYTES] __attribute__((aligned(16)));
static case_job job;

static void run_loop(access_kind kind, uint8_t *address, uint64_t (*now)(void), uint64_t *elapsed)
{
	uint32_t integer = 0x01020304u;
	double real = 1.5;
	float single = 2.5f;
	unsigned i;
	uint64_t started = now();

	switch (kind)
	{
	case LOAD_HALF:
		for (i = 0; i < ITERATIONS; i++)
			__asm__ volatile("lhz %0, 0(%1)" : "=r"(integer) : "b"(address) : "memory");
		break;
	case LOAD_WORD:
		for (i = 0; i < ITERATIONS; i++)
			__asm__ volatile("lwz %0, 0(%1)" : "=r"(integer) : "b"(address) : "memory");
		break;
	case LOAD_FLOAT:
		for (i = 0; i < ITERATIONS; i++)
			__asm__ volatile("lfs %0, 0(%1)" : "=f"(single) : "b"(address) : "memory");
		break;
	case LOAD_DOUBLE:
		for (i = 0; i < ITERATIONS; i++)
			__asm__ volatile("lfd %0, 0(%1)" : "=f"(real) : "b"(address) : "memory");
		break;
	case STORE_WORD:
		for (i = 0; i < ITERATIONS; i++)
			__asm__ volatile("stw %0, 0(%1)" : : "r"(integer), "b"(address) : "memory");
		break;
	case STORE_DOUBLE:
		for (i = 0; i < ITERATIONS; i++)
			__asm__ volatile("stfd %0, 0(%1)" : : "f"(real), "b"(address) : "memory");
		break;
	}
	*elapsed = now() - started;
	/* (the values are not used: what was measured is the instruction) */
	(void)integer;
	(void)real;
	(void)single;
}

static int case_worker(int argc, const char **argv)
{
	case_job *work = (case_job *)(uintptr_t)argv;

	(void)argc;
	run_loop(work->kind, work->address, work->now, &work->elapsed);
	work->done = 1;
	return 0;
}

/* is name one of the lines of list? */
static int listed(const char *list, const char *name)
{
	size_t length = strlen(name);
	const char *at = list;

	while (*at)
	{
		const char *end = strchr(at, '\n');
		size_t line = end ? (size_t)(end - at) : strlen(at);

		if (line == length && !memcmp(at, name, length))
			return 1;
		at += line + (end ? 1 : 0);
	}
	return 0;
}

/* Starts the case on a core and waits. Returns 1 if it came back, 0 if it did not (that core is lost). */
static int run_on_core(stage_context *context, size_t index, unsigned core, uint8_t *buffer)
{
	uint32_t waited = 0;

	job.done = 0;
	job.kind = cases[index].kind;
	job.address = buffer + cases[index].offset;
	job.now = context->now_ns;
	job.elapsed = 0;
	if (!OSCreateThread(&threads[index], case_worker, 0, (char *)&job, stacks[index] + STACK_BYTES, STACK_BYTES, 16,
			(OSThreadAttributes)((OS_THREAD_ATTRIB_AFFINITY_CPU0 << core) | OS_THREAD_ATTRIB_DETACHED)))
		return -1;
	OSSetThreadName(&threads[index], "unaligned case");
	OSResumeThread(&threads[index]);
	while (!job.done && waited < PATIENCE_MS)
	{
		OSSleepTicks(OSMillisecondsToTicks(1));
		waited++;
	}
	return job.done;
}

void stage_unaligned(stage_context *context)
{
	report *r = context->report;
	static char fatal[2048];
	char progress[128];
	uint8_t *buffer;
	size_t i;
	int usable[3] = {1, 0, 1}; /* the cores the cases may use: not core 1, the main thread's */
	double baseline = 0.0;

	if (context->keep_going && !context->keep_going())
		return;
	report_section(r, "unaligned");
	buffer = memalign(PAGE, 2 * PAGE);
	if (!buffer)
	{
		report_check(r, 0, "unaligned buffer", "2 pages not available");
		return;
	}
	memset(buffer, 0, 2 * PAGE);

	/* what the earlier runs knew: the file of cases that froze the console, and the case an earlier run was in when it did */
	if (card_read(PROBE_UNALIGNED_FATAL_FILE, fatal, sizeof(fatal)) < 0)
		fatal[0] = 0;
	if (card_read(PROBE_UNALIGNED_PROGRESS_FILE, progress, sizeof(progress)) > 0 && progress[0])
	{
		report_note(r, "an earlier run froze the console in the case '%s': recorded, and not tried again", progress);
		if (!listed(fatal, progress) && strlen(fatal) + strlen(progress) + 2 < sizeof(fatal))
		{
			card_write(PROBE_UNALIGNED_FATAL_FILE, progress, strlen(progress), 1);
			card_write(PROBE_UNALIGNED_FATAL_FILE, "\n", 1, 1);
			strcat(fatal, progress);
			strcat(fatal, "\n");
		}
	}
	card_remove(PROBE_UNALIGNED_PROGRESS_FILE);
	report_note(r, "each case runs on a thread on core 0 or 2; one that does not come back in %u s is reported and its core given up", PATIENCE_MS / 1000);
	crash_recover_alignment(1);

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
	{
		const access_case *c = &cases[i];
		uint32_t traps_before;
		unsigned core;
		int came_back = 0;
		size_t k;
		int known = listed(fatal, c->name);

		for (k = 0; k < sizeof(known_fatal) / sizeof(known_fatal[0]); k++)
			if (!strcmp(known_fatal[k], c->name))
				known = 1;
		if (known)
		{
			report_check(r, 0, c->name, "froze the console in an earlier run: not tried");
			continue;
		}
		for (core = 0; core < 3 && !usable[core]; core++)
			;
		if (core == 3)
		{
			report_check(r, 0, c->name, "not tried: no core is left to try it on");
			continue;
		}
		report_value(r, "trying", "%s on core %u", c->name, core);
		card_write(PROBE_UNALIGNED_PROGRESS_FILE, c->name, strlen(c->name), 0);
		if (context->flush)
			context->flush();
		traps_before = crash_alignment_traps();
		came_back = run_on_core(context, i, core, buffer);
		if (came_back < 0)
		{
			report_check(r, 0, c->name, "no thread could be made to try it");
			continue;
		}
		if (!came_back)
		{
			/* the console may not be fit to write; try anyway, the report is already on the card up to 'trying' */
			report_check(r, 0, c->name, "the thread on core %u did not come back in %u s: that core is lost until the console is restarted",
				core, PATIENCE_MS / 1000);
			card_write(PROBE_UNALIGNED_FATAL_FILE, c->name, strlen(c->name), 1);
			card_write(PROBE_UNALIGNED_FATAL_FILE, "\n", 1, 1);
			usable[core] = 0;
		}
		else
		{
			const uint32_t traps = crash_alignment_traps() - traps_before;
			const double per_access = (double)job.elapsed / ITERATIONS;

			if (c->offset == 0)
				baseline = per_access;
			if (traps)
			{
				uint32_t pc, address;

				crash_alignment_last(&pc, &address);
				report_check(r, 0, c->name, "%u alignment exceptions, pc %08X, address %08X: the handler skipped the instruction and the thread went on",
					(unsigned)traps, (unsigned)pc, (unsigned)address);
			}
			else
			{
				report_check(r, 1, c->name, "works, %.1f ns per access%s", per_access,
					c->offset && baseline > 0.0 && per_access > 10.0 * baseline ? " (more than ten times the aligned access: emulated)" : "");
			}
		}
		card_remove(PROBE_UNALIGNED_PROGRESS_FILE);
		if (context->keep_going && !context->keep_going())
			break;
	}
	crash_recover_alignment(0);
	card_remove(PROBE_UNALIGNED_PROGRESS_FILE);
	free(buffer);
}
