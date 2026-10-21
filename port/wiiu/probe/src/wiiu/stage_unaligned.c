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
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define ITERATIONS 20000
#define PAGE 4096
#define LINE 32
#define CASE_COUNT 32
#define SLOTS (CASE_COUNT * 5)
#define STACK_BYTES (12 * 1024)
#define PATIENCE_MS 5000

typedef enum access_kind
{
	LOAD_HALF,
	LOAD_WORD,
	LOAD_FLOAT,
	LOAD_DOUBLE,
	STORE_WORD,
	STORE_FLOAT,
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
	{"stfs aligned", STORE_FLOAT, 0},
	{"lfs at +2", LOAD_FLOAT, 2},
	{"lfs across a 32-byte line", LOAD_FLOAT, LINE - 2},
	{"lfd at +1", LOAD_DOUBLE, 1},
	{"lfd across a 32-byte line", LOAD_DOUBLE, LINE - 4},
	{"lfd across a 4 KiB page", LOAD_DOUBLE, PAGE - 4},
	{"stfd at +1", STORE_DOUBLE, 1},
	{"stfs at +2", STORE_FLOAT, 2},
	{"stfs at +1", STORE_FLOAT, 1},
	{"lfs at +1", LOAD_FLOAT, 1},
};

/* Cases known to freeze the console, not to be tried again. 'lfs at +1' froze it in the first two runs (2026-10-18), when the fault
handler wrote to the card from inside the exception; with the handler that does not, the other misaligned float cases came back
(as exceptions it skipped), so it is tried again. Cases an earlier run froze in are in the file on the card as well. */
static const char *const known_fatal[] = {NULL};

typedef struct case_job
{
	volatile int done;
	access_kind kind;
	uint8_t *address;
	uint64_t (*now)(void);
	uint64_t elapsed;
	int verify;   /* check the value of one access before timing the loop */
	int reg;      /* and through which register: 0, 1, 13 or 31 */
	int verified; /* and it was right */
	uint64_t seen_bits;       /* what a load gave */
	uint8_t seen_bytes[12];   /* what a store left, from one byte before */
} case_job;

static OSThread threads[SLOTS];
static uint8_t stacks[SLOTS][STACK_BYTES] __attribute__((aligned(16)));
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
	case STORE_FLOAT:
		for (i = 0; i < ITERATIONS; i++)
			__asm__ volatile("stfs %0, 0(%1)" : : "f"(real), "b"(address) : "memory");
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

/* One access of the kind, at the address, through a given floating-point register (the compiler's choice for the first run was f0,
which came back as zero), with a value that is known, and whether the right value came or went: the check that the emulation
(crash.c, wiiu_align_fixup.c) did what the instruction means. Big-endian bytes written and read as integers, which work at every
alignment. */
#define LOAD_VIA(insn, n) \
	do \
	{ \
		__asm__ volatile(insn " " #n ", 0(%0)" : : "b"(address) : "memory", "fr" #n); \
		__asm__ volatile("fmr %0, " #n : "=f"(value)); \
	} while (0)
#define STORE_VIA(insn, n) \
	do \
	{ \
		__asm__ volatile("fmr " #n ", %0" : : "f"(value) : "fr" #n); \
		__asm__ volatile(insn " " #n ", 0(%0)" : : "b"(address) : "memory"); \
	} while (0)
#define BY_REGISTER(macro, insn) \
	switch (reg) \
	{ \
	case 0: macro(insn, 0); break; \
	case 1: macro(insn, 1); break; \
	case 13: macro(insn, 13); break; \
	default: macro(insn, 31); break; \
	}

static int verify_once(access_kind kind, uint8_t *address, case_job *work, int reg)
{
	static const uint8_t one_and_a_half[4] = {0x3F, 0xC0, 0x00, 0x00};
	static const uint8_t minus_two_and_a_quarter[8] = {0xC0, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	static const uint8_t three_and_a_half_single[4] = {0x40, 0x60, 0x00, 0x00};
	static const uint8_t three_and_a_half_double[8] = {0x40, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	double value = 0.0;

	switch (kind)
	{
	case LOAD_FLOAT:
		memcpy(address, one_and_a_half, 4);
		BY_REGISTER(LOAD_VIA, "lfs")
		memcpy(&work->seen_bits, &value, 8);
		return value == 1.5;
	case LOAD_DOUBLE:
		memcpy(address, minus_two_and_a_quarter, 8);
		BY_REGISTER(LOAD_VIA, "lfd")
		memcpy(&work->seen_bits, &value, 8);
		return value == -2.25;
	case STORE_FLOAT:
		memset(address - 1, 0xEE, 6);
		value = 3.5;
		BY_REGISTER(STORE_VIA, "stfs")
		memcpy(work->seen_bytes, address - 1, 6);
		return !memcmp(address, three_and_a_half_single, 4) && address[-1] == 0xEE && address[4] == 0xEE;
	case STORE_DOUBLE:
		memset(address - 1, 0xEE, 10);
		value = 3.5;
		BY_REGISTER(STORE_VIA, "stfd")
		memcpy(work->seen_bytes, address - 1, 10);
		return !memcmp(address, three_and_a_half_double, 8) && address[-1] == 0xEE && address[8] == 0xEE;
	default:
		return 1;
	}
}

static int case_worker(int argc, const char **argv)
{
	case_job *work = (case_job *)(uintptr_t)argv;

	(void)argc;
	if (work->verify)
		work->verified = verify_once(work->kind, work->address, work, work->reg);
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
/* case index of cases[]; slot is the thread and stack it uses (a case is tried once for each register, each with its own) */
static int run_on_core(stage_context *context, size_t index, size_t slot, unsigned core, uint8_t *buffer, int verify, int reg)
{
	uint32_t waited = 0;

	job.done = 0;
	job.kind = cases[index].kind;
	job.address = buffer + cases[index].offset;
	job.now = context->now_ns;
	job.elapsed = 0;
	job.verify = verify;
	job.reg = reg;
	job.verified = 0;
	if (!OSCreateThread(&threads[slot], case_worker, 0, (char *)&job, stacks[slot] + STACK_BYTES, STACK_BYTES, 16,
			(OSThreadAttributes)((OS_THREAD_ATTRIB_AFFINITY_CPU0 << core) | OS_THREAD_ATTRIB_DETACHED)))
		return -1;
	OSSetThreadName(&threads[slot], "unaligned case");
	OSResumeThread(&threads[slot]);
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
	uint8_t *raw;
	uint8_t *buffer;
	size_t i;
	int usable[3] = {1, 0, 1}; /* the cores the cases may use: not core 1, the main thread's */
	int trapped[CASE_COUNT] = {0};
	double baseline = 0.0;

	if (context->keep_going && !context->keep_going())
		return;
	report_section(r, "unaligned");
	/* two pages to try on, page aligned, with a page of guard either side (the checks write a byte around what they try) */
	raw = memalign(PAGE, 4 * PAGE);
	if (!raw)
	{
		report_check(r, 0, "unaligned buffer", "4 pages not available");
		return;
	}
	memset(raw, 0, 4 * PAGE);
	buffer = raw + PAGE;

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
			if (known_fatal[k] && !strcmp(known_fatal[k], c->name))
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
		came_back = run_on_core(context, i, i, core, buffer, 0, 0);
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

				trapped[i] = 1;
				crash_alignment_last(&pc, &address);
				report_check(r, 0, c->name, "%u alignment exceptions, %.2f us each, pc %08X, address %08X: the handler skipped the instruction and the thread went on",
					(unsigned)traps, per_access / 1000.0, (unsigned)pc, (unsigned)address);
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

	/* the cases that raised the exception again, with the handler making the access (src/wiiu_align_fixup.c) instead of skipping it:
	the value must be the right one, and what it costs is what a stray misaligned float in the game would cost */
	report_note(r, "now the exceptions are answered by making the access in the handler, and the value checked");
	crash_recover_alignment(2);
	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
	{
		static const int registers[4] = {0, 1, 13, 31};
		const access_case *c = &cases[i];
		unsigned slot;

		if (!trapped[i])
			continue;
		/* through four registers: f0 is the compiler's own favourite, and the first run's check came back as zero through it */
		for (slot = 0; slot < 4; slot++)
		{
			unsigned core;
			uint32_t traps_before, emulated_before, refused_before, emulated, refused;
			int came_back;
			char name[64];

			for (core = 0; core < 3 && !usable[core]; core++)
				;
			if (core == 3)
				break;
			snprintf(name, sizeof(name), "%s via f%d", c->name, registers[slot]);
			report_value(r, "trying", "%s, emulated, on core %u", name, core);
			card_write(PROBE_UNALIGNED_PROGRESS_FILE, c->name, strlen(c->name), 0);
			if (context->flush)
				context->flush();
			traps_before = crash_alignment_traps();
			emulated_before = crash_alignment_emulated(&refused_before);
			came_back = run_on_core(context, i, CASE_COUNT * (1 + slot) + i, core, buffer, 1, registers[slot]);
			if (came_back != 1)
			{
				report_check(r, 0, name, "emulated: the thread on core %u did not come back", core);
				usable[core] = 0;
				continue;
			}
			emulated = crash_alignment_emulated(&refused) - emulated_before;
			refused -= refused_before;
			if (!job.verified)
			{
				uint32_t instruction, dsisr;
				uint64_t held;
				unsigned b;
				char bytes[40] = "";

				crash_alignment_detail(&instruction, &dsisr);
				crash_alignment_register(&held);
				for (b = 0; b < 10 && (c->kind == STORE_FLOAT || c->kind == STORE_DOUBLE); b++)
					snprintf(bytes + b * 3, sizeof(bytes) - b * 3, "%02X ", job.seen_bytes[b]);
				report_value(r, "emulation detail",
					"%s: instruction %08X, DSISR %08X; the context held %016llX for its register; a load gave %016llX; a store left (from one byte before) %s",
					name, (unsigned)instruction, (unsigned)dsisr, (unsigned long long)held, (unsigned long long)job.seen_bits, bytes);
			}
			report_check(r, job.verified && refused == 0 && emulated > 0 && emulated == crash_alignment_traps() - traps_before, name,
				"emulated: the value is %s; %u accesses made, %u refused; %.2f us each", job.verified ? "right" : "WRONG",
				(unsigned)emulated, (unsigned)refused, (double)job.elapsed / ITERATIONS / 1000.0);
			card_remove(PROBE_UNALIGNED_PROGRESS_FILE);
		}
	}
	crash_recover_alignment(0);
	card_remove(PROBE_UNALIGNED_PROGRESS_FILE);
	free(raw);
}
