/*
MEMPROBE.C

The Wii U memory probe. The question: would the game's hottest data (the object pool, the AI data, the collision
BSP, the script nodes) run faster in another memory than the DDR3 the app's heap is in? Three regions are
measured, on each of the three cores, one at a time:

  MEM2 heap   an ordinary allocation (memalign) from the default heap: what the game's 128 MiB window is
  MEM1 eDRAM  16 MiB from the MEM1 base heap (virtual 0xF4000000): where the render targets are
  FG bucket   16 MiB from the foreground base heap (virtual 0xE0000000), if the system gives it

and reports, per region and core:

  latency    dependent loads (pointer chasing in a random cycle) over working sets of 16 KiB to 16 MiB: the L1
             (32 KiB), the L2 (512 KiB on cores 0 and 2, 2 MiB on core 1) and the memory behind them, in
             nanoseconds, core cycles and L2 data misses per load (the Espresso's performance counters)
  line size  the same at several strides, to show how much of a line a miss brings
  bandwidth  sequential reads (plain, and with dcbt prefetch hints at several distances), stores, dcbz fills,
             and the cost of a line fill when the loads are independent (the memory-level parallelism)
  contention one core chasing while the other two stream: does the eDRAM keep the cores out of each other's way

The accesses are all aligned (a misaligned float access traps on the Espresso), the allocations are bounded
and given back, no fixed addresses are used and nothing is mapped. dcbz runs under an exception guard (it
raises an alignment exception on cache-inhibited memory). The optional "lc" stage (only if lc.txt is in the
app's folder on the card) reads, under the same kind of guard, the registers of the Wii's locked-cache DMA
(it only reads them).

The report is written to sd:/wiiu/apps/opence-memprobe/memprobe-report.txt after every row, so that a run that
stops (HOME takes the eDRAM and the foreground bucket away; do not press it before "finished") still has what
it measured. The workers never touch the card: the main thread writes.
*/

#include <coreinit/cache.h>
#include <coreinit/core.h>
#include <coreinit/exception.h>
#include <coreinit/foreground.h>
#include <coreinit/kernel.h>
#include <coreinit/memexpheap.h>
#include <coreinit/memfrmheap.h>
#include <coreinit/memheap.h>
#include <coreinit/memory.h>
#include <coreinit/memorymap.h>
#include <coreinit/systeminfo.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <whb/log.h>
#include <whb/log_console.h>
#include <whb/proc.h>

#ifndef PROBE_BUILD_ID
#define PROBE_BUILD_ID "unknown"
#endif

#define DIRECTORY "/vol/external01/wiiu/apps/opence-memprobe"
#define REPORT_FILE "memprobe-report.txt"
#define REPORT_CAPACITY (192 * 1024)
#define REGION_BYTES (16u * 1024u * 1024u)
#define CORES 3
#define STACK_SIZE (64 * 1024)
#define MAX_SLOTS 4

/* ---------------------------------------------------------------- report */

static char report_text[REPORT_CAPACITY];
static size_t report_size, report_flushed;
static int card_failed;

static void say(const char *format, ...) __attribute__((format(printf, 1, 2)));

static void say(const char *format, ...)
{
	va_list arguments;
	int n;

	if (report_size + 512 >= sizeof(report_text))
		return;
	va_start(arguments, format);
	n = vsnprintf(report_text + report_size, 480, format, arguments);
	va_end(arguments);
	if (n < 0)
		return;
	if (n > 479)
		n = 479;
	report_size += (size_t)n;
	report_text[report_size++] = '\n';
}

/* the last line, on the screen as well */
static void show_last(void)
{
	size_t end = report_size ? report_size - 1 : 0, start = end;

	while (start > 0 && report_text[start - 1] != '\n')
		start--;
	report_text[end] = 0;
	WHBLogPrint(report_text + start);
	WHBLogConsoleDraw();
	report_text[end] = '\n';
}

static int card_write(const char *name, const char *data, size_t size, int append)
{
	char path[160];
	size_t done = 0;
	int error = 0;
	int file;

	snprintf(path, sizeof(path), "%s/%s", DIRECTORY, name);
	file = open(path, O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0644);
	if (file < 0)
		return errno ? errno : EIO;
	while (done < size)
	{
		ssize_t written = write(file, data + done, size - done);

		if (written <= 0)
		{
			error = errno ? errno : EIO;
			break;
		}
		done += (size_t)written;
	}
	if (!error && fsync(file) != 0)
		error = errno ? errno : EIO;
	if (close(file) != 0 && !error)
		error = errno ? errno : EIO;
	return error;
}

static void flush_report(void)
{
	static int started;
	int error;

	if (report_flushed == report_size)
		return;
	error = card_write(REPORT_FILE, report_text + report_flushed, report_size - report_flushed, started);
	if (error)
	{
		if (!card_failed)
			WHBLogPrintf("cannot write the report to the card: error %d", error);
		card_failed = error;
		return;
	}
	started = 1;
	report_flushed = report_size;
}

static int leaving;

static int keep_going(void)
{
	if (!leaving && !WHBProcIsRunning())
		leaving = 1;
	return !leaving;
}

/* a line of the report, written out and shown */
#define EMIT(...)             \
	do                        \
	{                         \
		say(__VA_ARGS__);     \
		show_last();          \
		flush_report();       \
	} while (0)

/* ---------------------------------------------------------------- clocks and counters */

static uint32_t core_hz, timer_hz;

static inline uint64_t timebase(void)
{
	uint32_t hi, lo, hi2;

	do
	{
		__asm__ volatile("mftbu %0" : "=r"(hi));
		__asm__ volatile("mftb %0" : "=r"(lo));
		__asm__ volatile("mftbu %0" : "=r"(hi2));
	} while (hi != hi2);
	return ((uint64_t)hi << 32) | lo;
}

static double ticks_to_ns(uint64_t ticks)
{
	return (double)ticks * 1e9 / (double)timer_hz;
}

/* the core's counters, as the port's halo/perf_counters.txt set 1 sets them (wiiu_perfmon.c on the overlap branches, which has run on
the console): cycles, instructions completed, L2 data misses, L2 castouts */
void OSSetPerformanceMonitor(uint32_t arg_mask, uint32_t mmcr0, uint32_t mmcr1, uint32_t pmc1, uint32_t pmc2, uint32_t pmc3, uint32_t pmc4);

static void pm_start(void)
{
	OSSetPerformanceMonitor((1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 5), (1u << 6) | 2u, (7u << 27) | (5u << 22), 0, 0, 0, 0);
}

typedef struct pm_values
{
	uint32_t cycles, instructions, l2_misses, castouts;
} pm_values;

static inline void pm_read(pm_values *v)
{
	__asm__ volatile("mfspr %0, 937" : "=r"(v->cycles));
	__asm__ volatile("mfspr %0, 938" : "=r"(v->instructions));
	__asm__ volatile("mfspr %0, 941" : "=r"(v->l2_misses));
	__asm__ volatile("mfspr %0, 942" : "=r"(v->castouts));
}

/* ---------------------------------------------------------------- regions */

typedef struct region
{
	const char *name;
	uint8_t *base;
	size_t size;
	uint32_t physical;
	MEMHeapHandle heap; /* the frame heap it came from, or NULL (memalign) */
	uint32_t state_tag;
	int ok;
} region;

enum { REGION_MEM2, REGION_MEM1, REGION_FG, REGIONS };

static region regions[REGIONS] = { { .name = "MEM2 heap" }, { .name = "MEM1 eDRAM" }, { .name = "FG bucket" } };

static void take_frame_region(region *r, MEMBaseHeapType type, uint32_t tag)
{
	MEMHeapHandle heap = MEMGetBaseHeapHandle(type);
	uint32_t allocatable;
	uint32_t size = REGION_BYTES;

	if (!heap || heap->tag != MEM_FRAME_HEAP_TAG)
	{
		EMIT("%s: no frame heap (%p)", r->name, (void *)heap);
		return;
	}
	allocatable = MEMGetAllocatableSizeForFrmHeapEx(heap, 0x10000);
	EMIT("%s: heap %08X-%08X, %u KiB allocatable at 64 KiB alignment", r->name, (unsigned)(uintptr_t)heap->dataStart, (unsigned)(uintptr_t)heap->dataEnd,
		(unsigned)(allocatable / 1024));
	while (size > 2u * 1024u * 1024u && size + 256u * 1024u > allocatable)
		size /= 2;
	if (size + 256u * 1024u > allocatable)
	{
		EMIT("%s: not enough room (%u KiB)", r->name, (unsigned)(allocatable / 1024));
		return;
	}
	r->heap = heap;
	r->state_tag = tag;
	if (!MEMRecordStateForFrmHeap(heap, tag))
	{
		EMIT("%s: MEMRecordStateForFrmHeap failed", r->name);
		r->heap = NULL;
		return;
	}
	r->base = MEMAllocFromFrmHeapEx(heap, size, 0x10000);
	if (!r->base)
	{
		EMIT("%s: allocation of %u KiB failed", r->name, (unsigned)(size / 1024));
		MEMFreeByStateToFrmHeap(heap, tag);
		r->heap = NULL;
		return;
	}
	r->size = size;
}

static void take_regions(void)
{
	size_t i;

	regions[REGION_MEM2].base = memalign(0x10000, REGION_BYTES);
	if (regions[REGION_MEM2].base)
		regions[REGION_MEM2].size = REGION_BYTES;
	else
		EMIT("MEM2 heap: memalign of 16 MiB failed");
	take_frame_region(&regions[REGION_MEM1], MEM_BASE_HEAP_MEM1, 0x4D454D31u);
	take_frame_region(&regions[REGION_FG], MEM_BASE_HEAP_FG, 0x4D454D46u);

	for (i = 0; i < REGIONS; i++)
	{
		region *r = &regions[i];

		if (!r->base)
			continue;
		memset(r->base, 0, r->size);
		r->physical = OSEffectiveToPhysical((uint32_t)(uintptr_t)r->base);
		r->ok = 1;
		EMIT("%s: %u KiB at virtual %08X, physical %08X (%08X at the end of it)", r->name, (unsigned)(r->size / 1024), (unsigned)(uintptr_t)r->base,
			(unsigned)r->physical, (unsigned)OSEffectiveToPhysical((uint32_t)(uintptr_t)r->base + (uint32_t)r->size - 4));
	}
}

static void give_regions_back(void)
{
	size_t i;

	for (i = 0; i < REGIONS; i++)
	{
		if (regions[i].heap)
			MEMFreeByStateToFrmHeap(regions[i].heap, regions[i].state_tag);
		else if (regions[i].base)
			free(regions[i].base);
		regions[i].base = NULL;
		regions[i].ok = 0;
	}
}

/* ---------------------------------------------------------------- the exception guard
dcbz (and a read of a register the core might not give a user thread) is run in leaf functions written in assembly: a program, alignment or data-access
exception whose pc is in one of them is not a crash but an answer: the handler returns to the caller, which has the function's "0" in r3. Same scheme as
wiiu_bw_kernels.c (which has run on the console). Handlers are taken out again when the stage that needed them is done. */

extern const char memprobe_guard_begin[], memprobe_guard_end[];
uint32_t memprobe_dcbz_fill(void *memory, uint32_t blocks);
uint32_t memprobe_read_spr(uint32_t which);

__asm__(
	".text\n"
	".globl memprobe_guard_begin\n"
	"memprobe_guard_begin:\n"
	/* r3 = memory (32-byte aligned), r4 = number of 32-byte blocks; returns 1 when done, 0 if the first dcbz raised an exception */
	".globl memprobe_dcbz_fill\n"
	"memprobe_dcbz_fill:\n"
	"	mr 5,3\n"
	"	li 3,0\n"
	"	mtctr 4\n"
	"1:	dcbz 0,5\n"
	"	addi 5,5,32\n"
	"	bdnz 1b\n"
	"	li 3,1\n"
	"	blr\n"
	/* r3 = 0: UHID2 (SPR 904), 1: UWPAR (905), 2: UDMAU (906), 3: UDMAL (907); returns the value, or 0xFFFFFFFF when the read raised an exception */
	".globl memprobe_read_spr\n"
	"memprobe_read_spr:\n"
	"	cmpwi 3,0\n"
	"	beq 10f\n"
	"	cmpwi 3,1\n"
	"	beq 11f\n"
	"	cmpwi 3,2\n"
	"	beq 12f\n"
	"	li 3,-1\n"
	"	mfspr 3,907\n"
	"	blr\n"
	"10:	li 3,-1\n"
	"	mfspr 3,904\n"
	"	blr\n"
	"11:	li 3,-1\n"
	"	mfspr 3,905\n"
	"	blr\n"
	"12:	li 3,-1\n"
	"	mfspr 3,906\n"
	"	blr\n"
	".globl memprobe_guard_end\n"
	"memprobe_guard_end:\n"
	"	nop\n");

static const OSExceptionType guard_types[3] = { OS_EXCEPTION_TYPE_PROGRAM, OS_EXCEPTION_TYPE_ALIGNMENT, OS_EXCEPTION_TYPE_DSI };
static OSExceptionCallbackFn guard_previous[3];
static volatile uint32_t guard_faults, guard_pc, guard_dar, guard_type;

static BOOL guard_recover(unsigned slot, OSContext *context)
{
	const uintptr_t pc = context->srr0;

	if (pc >= (uintptr_t)memprobe_guard_begin && pc < (uintptr_t)memprobe_guard_end)
	{
		if (!guard_faults++)
		{
			guard_type = slot;
			guard_pc = (uint32_t)pc;
			guard_dar = (uint32_t)context->dar;
		}
		context->srr0 = context->lr;
		return TRUE;
	}
	return guard_previous[slot] ? guard_previous[slot](context) : FALSE;
}

static BOOL guard_program(OSContext *context) { return guard_recover(0, context); }
static BOOL guard_alignment(OSContext *context) { return guard_recover(1, context); }
static BOOL guard_dsi(OSContext *context) { return guard_recover(2, context); }

static void guard_begin(void)
{
	OSExceptionCallbackFn handlers[3] = { guard_program, guard_alignment, guard_dsi };
	unsigned slot;

	guard_faults = 0;
	for (slot = 0; slot < 3; slot++)
		guard_previous[slot] = OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, guard_types[slot], handlers[slot]);
}

static void guard_end(void)
{
	unsigned slot;

	for (slot = 0; slot < 3; slot++)
		OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, guard_types[slot], guard_previous[slot]);
}

static const char *guard_type_name(void)
{
	static const char *const names[3] = { "program", "alignment", "data access" };

	return names[guard_type % 3];
}

/* ---------------------------------------------------------------- kernels */

/* a random cycle through the nodes (Sattolo's shuffle), one pointer at the start of each node; 0 if there is no memory for the order */
static uint32_t rng_state = 2463534242u;

static uint32_t rng(void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return rng_state;
}

static int build_chain(uint8_t *base, size_t size, unsigned stride)
{
	const uint32_t nodes = (uint32_t)(size / stride);
	uint32_t *order = malloc((size_t)nodes * sizeof(uint32_t));
	uint32_t i;

	if (!order)
		return 0;
	for (i = 0; i < nodes; i++)
		order[i] = i;
	for (i = nodes - 1; i > 0; i--)
	{
		const uint32_t j = rng() % i;
		const uint32_t t = order[i];

		order[i] = order[j];
		order[j] = t;
	}
	for (i = 0; i < nodes; i++)
		*(void *volatile *)(base + (size_t)i * stride) = base + (size_t)order[i] * stride;
	free(order);
	return 1;
}

static void *__attribute__((noinline)) chase(void *p, uint32_t steps)
{
	uint32_t i;

	for (i = 0; i < steps; i += 8)
	{
		p = *(void *volatile *)p;
		p = *(void *volatile *)p;
		p = *(void *volatile *)p;
		p = *(void *volatile *)p;
		p = *(void *volatile *)p;
		p = *(void *volatile *)p;
		p = *(void *volatile *)p;
		p = *(void *volatile *)p;
	}
	return p;
}

/* sequential reads of 32-bit words, 8 a loop, `ahead` bytes of dcbt in front of the loads (0: none) */
static uint32_t __attribute__((noinline)) read_words(const volatile uint32_t *p, size_t bytes, uint32_t ahead)
{
	const volatile uint32_t *end = p + bytes / 4;
	uint32_t a = 0, b = 0, c = 0, d = 0;

	if (ahead)
	{
		for (; p < end; p += 8)
		{
			__asm__ volatile("dcbt %0,%1" : : "b"(ahead), "r"(p));
			a += p[0] + p[4];
			b += p[1] + p[5];
			c += p[2] + p[6];
			d += p[3] + p[7];
		}
	}
	else
	{
		for (; p < end; p += 8)
		{
			a += p[0] + p[4];
			b += p[1] + p[5];
			c += p[2] + p[6];
			d += p[3] + p[7];
		}
	}
	return a + b + c + d;
}

static void __attribute__((noinline)) write_words(volatile uint32_t *p, size_t bytes, uint32_t value)
{
	volatile uint32_t *end = p + bytes / 4;

	for (; p < end; p += 8)
	{
		p[0] = value;
		p[1] = value;
		p[2] = value;
		p[3] = value;
		p[4] = value;
		p[5] = value;
		p[6] = value;
		p[7] = value;
	}
}

/* one load a `stride` bytes, independent of each other: how fast lines come in when the core can overlap them */
static uint32_t __attribute__((noinline)) read_strided(const volatile uint8_t *p, size_t bytes, unsigned stride)
{
	const volatile uint8_t *end = p + bytes;
	uint32_t a = 0, b = 0;

	for (; p + 4 * stride <= end; p += 4 * stride)
	{
		a += *(const volatile uint32_t *)(p);
		b += *(const volatile uint32_t *)(p + stride);
		a += *(const volatile uint32_t *)(p + 2 * stride);
		b += *(const volatile uint32_t *)(p + 3 * stride);
	}
	return a + b;
}

/* ---------------------------------------------------------------- jobs on a core */

typedef enum job_kind
{
	JOB_LATENCY,
	JOB_BANDWIDTH,
	JOB_HOG
} job_kind;

typedef enum bandwidth_kind
{
	BW_READ,
	BW_READ_PREFETCH,
	BW_WRITE,
	BW_DCBZ,
	BW_LINE
} bandwidth_kind;

typedef struct job
{
	job_kind kind;
	region *where;
	uint8_t *base;   /* where in the region */
	size_t size;     /* working set */
	unsigned stride; /* latency: bytes between nodes; line: bytes between loads */
	bandwidth_kind bw;
	unsigned ahead;  /* prefetch distance */
	int writes;      /* hog: store instead of load */
	/* results */
	unsigned ran_on;
	int failed;
	double ns, cycles, l2_misses; /* latency: per load; bandwidth: ns is per byte (line: per line) */
	double extra;                 /* hog: MB/s */
	volatile uint32_t sink;
} job;

static volatile int hog_stop;

static void measure_latency(job *j)
{
	const uint32_t nodes = (uint32_t)(j->size / j->stride);
	uint32_t steps, run;
	void *p;
	pm_values a, b;
	double best = 1e30;

	if (!build_chain(j->base, j->size, j->stride))
	{
		j->failed = 1;
		return;
	}
	steps = (j->size <= 1024 * 1024) ? (4u << 20) : (nodes * 2 > (1u << 20) ? nodes * 2 : (1u << 20));
	steps = (steps + 7) & ~7u;
	p = chase(j->base, (nodes + 7) & ~7u); /* warm: every node once (the cycle is a single one) */
	for (run = 0; run < 3; run++)
	{
		uint64_t started, ticks;

		pm_read(&a);
		started = timebase();
		p = chase(p, steps);
		ticks = timebase() - started;
		pm_read(&b);
		if ((double)ticks < best)
		{
			best = (double)ticks;
			j->ns = ticks_to_ns(ticks) / steps;
			j->cycles = (double)(uint32_t)(b.cycles - a.cycles) / steps;
			j->l2_misses = (double)(uint32_t)(b.l2_misses - a.l2_misses) / steps;
		}
	}
	j->sink = (uint32_t)(uintptr_t)p;
}

static void measure_bandwidth(job *j)
{
	/* repeats until at least 32 MiB have gone through, so that the clock (16 ns) does not matter */
	uint32_t reps = (uint32_t)((32u << 20) / j->size), run;
	double best = 1e30;
	uint32_t sink = 0;

	if (!reps)
		reps = 1;
	for (run = 0; run < 3; run++)
	{
		uint64_t started;
		uint32_t r;
		uint64_t ticks;

		started = timebase();
		for (r = 0; r < reps; r++)
		{
			switch (j->bw)
			{
			case BW_READ:
				sink += read_words((const volatile uint32_t *)j->base, j->size, 0);
				break;
			case BW_READ_PREFETCH:
				sink += read_words((const volatile uint32_t *)j->base, j->size, j->ahead);
				break;
			case BW_WRITE:
				write_words((volatile uint32_t *)j->base, j->size, r);
				break;
			case BW_DCBZ:
				if (!memprobe_dcbz_fill(j->base, (uint32_t)(j->size / 32)))
				{
					j->failed = 1;
					return;
				}
				break;
			case BW_LINE:
				sink += read_strided((const volatile uint8_t *)j->base, j->size, j->stride);
				break;
			}
		}
		ticks = timebase() - started;
		if ((double)ticks < best)
		{
			best = (double)ticks;
			if (j->bw == BW_LINE)
				j->ns = ticks_to_ns(ticks) / ((double)reps * (double)(j->size / j->stride));
			else
				j->ns = ticks_to_ns(ticks) / ((double)reps * (double)j->size);
		}
	}
	j->sink = sink;
}

static void hog(job *j)
{
	uint64_t started = timebase();
	uint64_t bytes = 0;
	uint32_t sink = 0, n = 0;

	while (!hog_stop)
	{
		if (j->writes)
			write_words((volatile uint32_t *)j->base, j->size, n++);
		else
			sink += read_words((const volatile uint32_t *)j->base, j->size, 0);
		bytes += j->size;
	}
	j->extra = (double)bytes / (ticks_to_ns(timebase() - started) / 1000.0); /* bytes per microsecond = MB/s */
	j->sink = sink;
}

static int job_main(int argc, const char **argv)
{
	job *j = (job *)(uintptr_t)argv;

	(void)argc;
	j->ran_on = OSGetCoreId();
	pm_start();
	switch (j->kind)
	{
	case JOB_LATENCY:
		measure_latency(j);
		break;
	case JOB_BANDWIDTH:
		measure_bandwidth(j);
		break;
	case JOB_HOG:
		hog(j);
		break;
	}
	return 0;
}

static OSThread threads[MAX_SLOTS];
static uint8_t stacks[MAX_SLOTS][STACK_SIZE] __attribute__((aligned(16)));

static int start_job(unsigned slot, unsigned core, job *j)
{
	if (!OSCreateThread(&threads[slot], job_main, (int32_t)core, (char *)j, stacks[slot] + STACK_SIZE, STACK_SIZE, 16,
			(OSThreadAttributes)(OS_THREAD_ATTRIB_AFFINITY_CPU0 << core)))
	{
		EMIT("OSCreateThread failed for core %u", core);
		j->failed = 1;
		return 0;
	}
	OSSetThreadName(&threads[slot], "memprobe job");
	OSResumeThread(&threads[slot]);
	return 1;
}

static void finish_job(unsigned slot)
{
	int result = 0;

	OSJoinThread(&threads[slot], &result);
}

static void run_job(unsigned core, job *j)
{
	if (start_job(0, core, j))
		finish_job(0);
	if (j->ran_on != core && !j->failed)
		EMIT("warning: asked for core %u, ran on %u", core, j->ran_on);
}

/* ---------------------------------------------------------------- stages */

static const size_t latency_sizes[] = { 16u << 10, 32u << 10, 64u << 10, 128u << 10, 256u << 10, 512u << 10, 1u << 20, 2u << 20, 4u << 20, 8u << 20, 16u << 20 };
#define LATENCY_SIZES (sizeof(latency_sizes) / sizeof(latency_sizes[0]))

static double lat_ns[REGIONS][CORES][LATENCY_SIZES];
static double lat_cycles[REGIONS][CORES][LATENCY_SIZES];
static double lat_l2[REGIONS][CORES][LATENCY_SIZES];
static int cacheable[REGIONS][CORES];

static void size_label(size_t bytes, char *text, size_t capacity)
{
	if (bytes >= (1u << 20))
		snprintf(text, capacity, "%uM", (unsigned)(bytes >> 20));
	else
		snprintf(text, capacity, "%uK", (unsigned)(bytes >> 10));
}

static void header_row(const char *first)
{
	char line[256];
	size_t i, n = 0;

	n += (size_t)snprintf(line + n, sizeof(line) - n, "%-11s core", first);
	for (i = 0; i < LATENCY_SIZES; i++)
	{
		char label[8];

		size_label(latency_sizes[i], label, sizeof(label));
		n += (size_t)snprintf(line + n, sizeof(line) - n, " %6s", label);
	}
	EMIT("%s", line);
}

static void stage_latency(void)
{
	unsigned r, core;
	size_t s;

	EMIT("%s", "");
	EMIT("[latency] dependent loads, stride 64 B, random cycle, best of 3; the working set is the column. Row groups: ns per load, core cycles per load, L2 data misses per load");
	for (r = 0; r < REGIONS; r++)
	{
		if (!regions[r].ok)
			continue;
		for (core = 0; core < CORES && keep_going(); core++)
		{
			char line[256];
			size_t n = 0;
			job j;

			for (s = 0; s < LATENCY_SIZES && keep_going(); s++)
			{
				if (latency_sizes[s] > regions[r].size)
				{
					lat_ns[r][core][s] = lat_cycles[r][core][s] = lat_l2[r][core][s] = -1;
					continue;
				}
				memset(&j, 0, sizeof(j));
				j.kind = JOB_LATENCY;
				j.where = &regions[r];
				j.base = regions[r].base;
				j.size = latency_sizes[s];
				j.stride = 64;
				run_job(core, &j);
				lat_ns[r][core][s] = j.failed ? -1 : j.ns;
				lat_cycles[r][core][s] = j.cycles;
				lat_l2[r][core][s] = j.l2_misses;
			}
			cacheable[r][core] = lat_ns[r][core][0] > 0 && lat_ns[r][core][0] < 8.0;
			n += (size_t)snprintf(line + n, sizeof(line) - n, "ns    %-11s %4u", regions[r].name, core);
			for (s = 0; s < LATENCY_SIZES; s++)
				n += (size_t)snprintf(line + n, sizeof(line) - n, " %6.1f", lat_ns[r][core][s]);
			EMIT("%s", line);
		}
	}
	EMIT("%s", "");
	header_row("cycles");
	for (r = 0; r < REGIONS; r++)
		for (core = 0; core < CORES && regions[r].ok; core++)
		{
			char line[256];
			size_t n = 0;

			n += (size_t)snprintf(line + n, sizeof(line) - n, "cyc   %-11s %4u", regions[r].name, core);
			for (s = 0; s < LATENCY_SIZES; s++)
				n += (size_t)snprintf(line + n, sizeof(line) - n, " %6.1f", lat_cycles[r][core][s]);
			EMIT("%s", line);
		}
	EMIT("%s", "");
	header_row("L2 miss/load");
	for (r = 0; r < REGIONS; r++)
		for (core = 0; core < CORES && regions[r].ok; core++)
		{
			char line[256];
			size_t n = 0;

			n += (size_t)snprintf(line + n, sizeof(line) - n, "l2m   %-11s %4u", regions[r].name, core);
			for (s = 0; s < LATENCY_SIZES; s++)
				n += (size_t)snprintf(line + n, sizeof(line) - n, " %6.3f", lat_l2[r][core][s]);
			EMIT("%s", line);
		}
	/* the counters' own check: cycles against the clock, for the biggest L1-resident set of the first region */
	if (regions[REGION_MEM2].ok && lat_ns[REGION_MEM2][1][0] > 0)
		EMIT("counter check: %.3f GHz from cycles/ns on 16 KiB, core 1 (the clock is %.3f GHz)", lat_cycles[REGION_MEM2][1][0] / lat_ns[REGION_MEM2][1][0],
			core_hz / 1e9);
}

static void stage_line_size(void)
{
	static const unsigned strides[] = { 32, 64, 128, 256 };
	static const size_t sizes[] = { 1u << 20, 8u << 20 };
	unsigned r, si, zi;

	EMIT("%s", "");
	EMIT("[line size] core 1, dependent loads at several strides (ns per load): one pointer per 32/64/128/256 bytes. If a miss brings 64 bytes, stride 32 is not slower per load than 64");
	for (r = 0; r < REGIONS; r++)
	{
		if (!regions[r].ok)
			continue;
		for (si = 0; si < 2 && keep_going(); si++)
		{
			char line[256];
			size_t n = 0;
			char label[8];

			size_label(sizes[si], label, sizeof(label));
			n += (size_t)snprintf(line + n, sizeof(line) - n, "%-11s %4s:", regions[r].name, label);
			for (zi = 0; zi < 4; zi++)
			{
				job j;

				memset(&j, 0, sizeof(j));
				j.kind = JOB_LATENCY;
				j.where = &regions[r];
				j.base = regions[r].base;
				j.size = sizes[si];
				j.stride = strides[zi];
				run_job(1, &j);
				n += (size_t)snprintf(line + n, sizeof(line) - n, "  stride %u: %.1f", strides[zi], j.failed ? -1.0 : j.ns);
			}
			EMIT("%s", line);
		}
	}
}

static double mbps(double ns_per_byte)
{
	return ns_per_byte > 0 ? 1000.0 / ns_per_byte : 0;
}

static double run_bw(unsigned core, region *where, bandwidth_kind kind, size_t size, unsigned stride, unsigned ahead, int *failed)
{
	job j;

	memset(&j, 0, sizeof(j));
	j.kind = JOB_BANDWIDTH;
	j.where = where;
	j.base = where->base;
	j.size = size;
	j.stride = stride;
	j.bw = kind;
	j.ahead = ahead;
	run_job(core, &j);
	if (failed)
		*failed = j.failed;
	return j.failed ? -1 : j.ns;
}

static void stage_bandwidth(void)
{
	static const size_t sizes[] = { 16u << 10, 256u << 10, 8u << 20 };
	static const unsigned ahead[] = { 32, 64, 128, 256, 512 };
	unsigned r, core, k;

	EMIT("%s", "");
	EMIT("[bandwidth] MB/s of sequential 32-bit loads and stores over a working set of 16 KiB, 256 KiB and 8 MiB (best of 3, 32 MiB moved each)");
	for (r = 0; r < REGIONS; r++)
	{
		if (!regions[r].ok)
			continue;
		for (core = 0; core < CORES && keep_going(); core++)
		{
			double rd[3], wr[3], dz[3] = { 0, 0, 0 };
			int failed = 0;

			for (k = 0; k < 3; k++)
			{
				rd[k] = mbps(run_bw(core, &regions[r], BW_READ, sizes[k], 0, 0, NULL));
				wr[k] = mbps(run_bw(core, &regions[r], BW_WRITE, sizes[k], 0, 0, NULL));
			}
			EMIT("%-11s core %u: read %6.0f / %6.0f / %6.0f   store %6.0f / %6.0f / %6.0f", regions[r].name, core, rd[0], rd[1], rd[2], wr[0], wr[1], wr[2]);
			if (cacheable[r][core] && wr[0] > 400.0)
			{
				guard_begin();
				for (k = 0; k < 3 && !failed; k++)
					dz[k] = mbps(run_bw(core, &regions[r], BW_DCBZ, sizes[k], 0, 0, &failed));
				guard_end();
				if (failed || guard_faults)
					EMIT("%-11s core %u: dcbz raised a %s exception at pc %08X, address %08X (cache-inhibited or write-through?)", regions[r].name, core,
						guard_type_name(), (unsigned)guard_pc, (unsigned)guard_dar);
				else
					EMIT("%-11s core %u: dcbz fill %6.0f / %6.0f / %6.0f", regions[r].name, core, dz[0], dz[1], dz[2]);
			}
			else
				EMIT("%-11s core %u: dcbz skipped (16 KiB latency %.1f ns, 16 KiB store %.0f MB/s: not obviously write-back cached)", regions[r].name, core,
					lat_ns[r][core][0], wr[0]);
		}
	}

	EMIT("%s", "");
	EMIT("[prefetch] 8 MiB sequential load, MB/s, without dcbt and with dcbt N bytes ahead of the loads (one per 32 bytes)");
	for (r = 0; r < REGIONS; r++)
	{
		if (!regions[r].ok)
			continue;
		for (core = 0; core < CORES && keep_going(); core++)
		{
			char line[256];
			size_t n = 0;

			n += (size_t)snprintf(line + n, sizeof(line) - n, "%-11s core %u: none %6.0f", regions[r].name, core,
				mbps(run_bw(core, &regions[r], BW_READ, 8u << 20, 0, 0, NULL)));
			for (k = 0; k < sizeof(ahead) / sizeof(ahead[0]); k++)
				n += (size_t)snprintf(line + n, sizeof(line) - n, "  +%u: %6.0f", ahead[k],
					mbps(run_bw(core, &regions[r], BW_READ_PREFETCH, 8u << 20, 0, ahead[k], NULL)));
			EMIT("%s", line);
		}
	}

	EMIT("%s", "");
	EMIT("[line fill] 8 MiB, independent loads, one per 32 / 64 / 128 bytes: ns per load (compare with the dependent-load latency: the difference is how many misses the core overlaps)");
	for (r = 0; r < REGIONS; r++)
	{
		if (!regions[r].ok)
			continue;
		for (core = 0; core < CORES && keep_going(); core++)
			EMIT("%-11s core %u: %6.1f / %6.1f / %6.1f ns", regions[r].name, core, run_bw(core, &regions[r], BW_LINE, 8u << 20, 32, 0, NULL),
				run_bw(core, &regions[r], BW_LINE, 8u << 20, 64, 0, NULL), run_bw(core, &regions[r], BW_LINE, 8u << 20, 128, 0, NULL));
	}
}

/* core 1 chases through 8 MiB while the other two read or write 4 MiB each in the same kind of memory */
static void stage_contention(void)
{
	unsigned r, mode;
	static const char *const mode_names[3] = { "alone", "cores 0 and 2 read", "cores 0 and 2 store" };

	EMIT("%s", "");
	EMIT("[contention] core 1: dependent loads over 8 MiB (ns per load); cores 0 and 2 stream 4 MiB each of the same kind of memory meanwhile (MB/s each)");
	for (r = 0; r < REGIONS; r++)
	{
		if (!regions[r].ok || regions[r].size < (16u << 20))
			continue;
		for (mode = 0; mode < 3 && keep_going(); mode++)
		{
			job main_job, hog0, hog2;

			memset(&main_job, 0, sizeof(main_job));
			main_job.kind = JOB_LATENCY;
			main_job.where = &regions[r];
			main_job.base = regions[r].base;
			main_job.size = 8u << 20;
			main_job.stride = 64;
			hog_stop = 0;
			if (mode)
			{
				memset(&hog0, 0, sizeof(hog0));
				hog0.kind = JOB_HOG;
				hog0.where = &regions[r];
				hog0.base = regions[r].base + (8u << 20);
				hog0.size = 4u << 20;
				hog0.writes = mode == 2;
				hog2 = hog0;
				hog2.base = regions[r].base + (12u << 20);
				start_job(1, 0, &hog0);
				start_job(2, 2, &hog2);
				OSSleepTicks(OSMillisecondsToTicks(50)); /* let them get going */
			}
			run_job(1, &main_job);
			hog_stop = 1;
			if (mode)
			{
				finish_job(1);
				finish_job(2);
				EMIT("%-11s %-20s: core 1 %.1f ns per load (%.2f L2 misses); hogs %.0f and %.0f MB/s", regions[r].name, mode_names[mode], main_job.ns,
					main_job.l2_misses, hog0.extra, hog2.extra);
			}
			else
				EMIT("%-11s %-20s: core 1 %.1f ns per load (%.2f L2 misses)", regions[r].name, mode_names[mode], main_job.ns, main_job.l2_misses);
		}
	}
}

/* only if lc.txt is on the card: what the kernel says about the locked cache, and the registers (read only) */
static void stage_locked_cache(void)
{
	KernelInfo0 info;
	unsigned core;
	struct stat st;
	char path[160];
	static const char *const spr_names[4] = { "UHID2 (SPR 904)", "UWPAR (SPR 905)", "UDMAU (SPR 906)", "UDMAL (SPR 907)" };

	snprintf(path, sizeof(path), "%s/lc.txt", DIRECTORY);
	if (stat(path, &st) != 0)
	{
		EMIT("%s", "");
		EMIT("[locked cache] not run (put an empty file lc.txt next to the .wuhb on the card to run it: it reads the locked-cache registers of the core under an exception guard)");
		return;
	}
	EMIT("%s", "");
	EMIT("[locked cache] opt-in stage (lc.txt is on the card)");
	for (core = 0; core < CORES; core++)
	{
		memset(&info, 0, sizeof(info));
		__KernelGetInfo0(&info, core);
		EMIT("kernel info core %u: lockedCacheBase %08X / %08X / %08X (core 0 / 1 / 2); stack %08X-%08X; physical data %08X-%08X", core,
			(unsigned)(uintptr_t)info.lockedCacheBase0, (unsigned)(uintptr_t)info.lockedCacheBase1, (unsigned)(uintptr_t)info.lockedCacheBase2,
			(unsigned)(uintptr_t)info.stackEnd1, (unsigned)(uintptr_t)info.stackBase1, (unsigned)(uintptr_t)info.physDataAreaStart,
			(unsigned)(uintptr_t)info.physDataAreaEnd);
		if (!core)
		{
			const uintptr_t bases[3] = { (uintptr_t)info.lockedCacheBase0, (uintptr_t)info.lockedCacheBase1, (uintptr_t)info.lockedCacheBase2 };
			unsigned k;

			for (k = 0; k < 3; k++)
				EMIT("OSQueryVirtAddr(lockedCacheBase%u = %08X) = %d, physical %08X", k, (unsigned)bases[k], (int)OSQueryVirtAddr((uint32_t)bases[k]),
					(unsigned)OSEffectiveToPhysical((uint32_t)bases[k]));
		}
	}
	guard_begin();
	{
		unsigned k;

		for (k = 0; k < 4; k++)
		{
			uint32_t before = guard_faults;
			uint32_t value = memprobe_read_spr(k);

			if (guard_faults != before)
			{
				EMIT("%s: read raised a %s exception at pc %08X: not readable from a user thread", spr_names[k], guard_type_name(), (unsigned)guard_pc);
				guard_faults = 0;
			}
			else
				EMIT("%s = %08X%s", spr_names[k], (unsigned)value,
					k == 0 ? ((value & 8u) ? " (LCE set: the locked-cache is enabled)" : " (LCE clear: the locked cache is off)") : "");
		}
	}
	guard_end();
}

/* ---------------------------------------------------------------- main */

static void system_section(void)
{
	const OSSystemInfo *info = OSGetSystemInfo();
	uint32_t address, size;
	MEMHeapHandle heap;

	core_hz = info->coreClockSpeed;
	timer_hz = OSTimerClockSpeed;
	EMIT("OpenCE Wii U memory probe, build %s", PROBE_BUILD_ID);
	EMIT("core clock %u Hz, bus clock %u Hz, timer %u Hz, main thread on core %u", (unsigned)info->coreClockSpeed, (unsigned)info->busClockSpeed,
		(unsigned)timer_hz, (unsigned)OSGetCoreId());
	if (OSGetMemBound(OS_MEM1, &address, &size) == 0)
		EMIT("MEM1 bound %08X, %u KiB", (unsigned)address, (unsigned)(size / 1024));
	if (OSGetMemBound(OS_MEM2, &address, &size) == 0)
		EMIT("MEM2 bound %08X, %u KiB", (unsigned)address, (unsigned)(size / 1024));
	if (OSGetForegroundBucketFreeArea(&address, &size))
		EMIT("foreground bucket free area %08X, %u KiB", (unsigned)address, (unsigned)(size / 1024));
	heap = MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM1);
	if (heap && heap->tag == MEM_FRAME_HEAP_TAG)
		EMIT("MEM1 base heap %08X-%08X, %u KiB allocatable in one block (libwhb's log console already took its screen buffers)", (unsigned)(uintptr_t)heap->dataStart,
			(unsigned)(uintptr_t)heap->dataEnd, (unsigned)(MEMGetAllocatableSizeForFrmHeapEx(heap, 4) / 1024));
	heap = MEMGetBaseHeapHandle(MEM_BASE_HEAP_FG);
	if (heap && heap->tag == MEM_FRAME_HEAP_TAG)
		EMIT("foreground base heap %08X-%08X, %u KiB allocatable in one block", (unsigned)(uintptr_t)heap->dataStart, (unsigned)(uintptr_t)heap->dataEnd,
			(unsigned)(MEMGetAllocatableSizeForFrmHeapEx(heap, 4) / 1024));
	EMIT("the L1 data cache is 32 KiB; the L2 is 512 KiB on cores 0 and 2 and 2 MiB on core 1 (so the knees of the latency table are at 32K and 512K or 2M)");
}

int main(void)
{
	WHBProcInit();
	WHBLogConsoleInit();
	mkdir("/vol/external01/wiiu", 0755);
	mkdir("/vol/external01/wiiu/apps", 0755);
	mkdir(DIRECTORY, 0755);
	WHBLogPrint("OpenCE Wii U memory probe: do not press HOME until it says finished");
	WHBLogConsoleDraw();

	system_section();
	take_regions();
	if (keep_going())
		stage_latency();
	if (keep_going())
		stage_line_size();
	if (keep_going())
		stage_bandwidth();
	if (keep_going())
		stage_contention();
	if (keep_going())
		stage_locked_cache();
	give_regions_back();

	EMIT("%s", "");
	if (leaving)
		EMIT("stopped early: the user left");
	EMIT("== finished ==");
	if (card_failed)
		WHBLogPrintf("the report could not be written to the card (error %d)", card_failed);
	else
		WHBLogPrint("report: " DIRECTORY "/" REPORT_FILE);
	WHBLogPrint("Press HOME to leave.");
	while (keep_going())
	{
		WHBLogConsoleDraw();
		OSSleepTicks(OSMillisecondsToTicks(50));
	}
	WHBLogConsoleFree();
	WHBProcShutdown();
	return 0;
}
