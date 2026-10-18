/*
What a PowerPC 750 core does with an access that is not aligned to its size. The engine reads
several map streams at odd offsets (16-bit times and floats in animation and recorded-animation
data); x86 does not mind, a 750 does some of them in hardware, some by an alignment exception that
the system may emulate (slowly) or may end the application for. The plan's open question 'which
accesses trap' is this stage's.

The accesses are single load and store instructions (inline assembly: the compiler would otherwise
split an unaligned access into bytes, and nothing would be measured). Each kind is announced in the
report and written to the card before it runs, so that if the console does not come back from one,
the report ends at it; the kinds go from the ones that are known to work to the ones that may not,
and this is the last stage for that reason.
*/

#include "../stage.h"

#include <malloc.h>
#include <stdint.h>
#include <string.h>

#define ITERATIONS 20000
#define PAGE 4096
#define LINE 32

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

/* the cases: aligned first, as the baseline of its kind */
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
	{"lfs at +1", LOAD_FLOAT, 1},
	{"lfs at +2", LOAD_FLOAT, 2},
	{"lfs across a 32-byte line", LOAD_FLOAT, LINE - 2},
	{"lfd aligned", LOAD_DOUBLE, 0},
	{"lfd at +4", LOAD_DOUBLE, 4},
	{"lfd at +1", LOAD_DOUBLE, 1},
	{"lfd across a 32-byte line", LOAD_DOUBLE, LINE - 4},
	{"lfd across a 4 KiB page", LOAD_DOUBLE, PAGE - 4},
	{"stfd at +4", STORE_DOUBLE, 4},
	{"stfd at +1", STORE_DOUBLE, 1},
};

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

void stage_unaligned(stage_context *context)
{
	report *r = context->report;
	uint8_t *buffer;
	size_t i;
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
	report_note(r, "a case that does not come back ends this report; the last 'trying' line is the case, the crash file says why");

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
	{
		const access_case *c = &cases[i];
		uint64_t elapsed = 0;
		double per_access;

		report_value(r, "trying", "%s", c->name);
		if (context->flush)
			context->flush();
		run_loop(c->kind, buffer + c->offset, context->now_ns, &elapsed);
		per_access = (double)elapsed / ITERATIONS;
		if (c->offset == 0)
			baseline = per_access;
		report_check(r, 1, c->name, "works, %.1f ns per access%s", per_access,
			c->offset && baseline > 0.0 && per_access > 10.0 * baseline ? " (more than ten times the aligned access: emulated)" : "");
		if (context->keep_going && !context->keep_going())
			break;
	}
	free(buffer);
}
