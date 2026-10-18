/*
What Cafe OS lets the port do with memory. The questions this answers (plan section 6.2):

 - Can an app have the 128 MiB at 0x80000000, where the Xbox game state window sits and where tag
   pointers were meant to point? (OSAllocVirtAddr at the exact address, then OSMapMemory of the
   physical memory the system calls available.) If it can, the maps are converted without rebasing.
 - If not, where does the system put a 128 MiB range, and how large a block of the heap can the
   game have? (Then the offline converter rebases every tag pointer by the difference.)
 - Who is in the address space already, as OSQueryVirtAddr sees it, page by page.

Each mapping attempt is written to the card before it is made: if the console does not return from
one, the report ends at it.
*/

#include "../bench.h"
#include "../probe_logic.h"
#include "../stage.h"

#include <coreinit/memexpheap.h>
#include <coreinit/memfrmheap.h>
#include <coreinit/memheap.h>
#include <coreinit/memory.h>
#include <coreinit/memorymap.h>
#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define WINDOW_START 0x80000000u
#define WINDOW_SIZE 0x08000000u
#define PAGE_COUNT (0x100000000ull / OS_PAGE_SIZE)
#define MAX_RUNS_SHOWN 40
#define BENCH_SIZE (8u * 1024u * 1024u)

static uint8_t page_states[PAGE_COUNT];

static const char *state_name(unsigned state)
{
	switch (state)
	{
	case OS_MAP_MEMORY_INVALID:
		return "invalid";
	case OS_MAP_MEMORY_READ_ONLY:
		return "read-only";
	case OS_MAP_MEMORY_READ_WRITE:
		return "read-write";
	case OS_MAP_MEMORY_FREE:
		return "free";
	case OS_MAP_MEMORY_ALLOCATED:
		return "allocated";
	default:
		return "unknown";
	}
}

static void size_text(uint64_t bytes, char *text, size_t size)
{
	probe_format_size(bytes, text, size);
}

static double megabytes_per_second(double nanoseconds_per_byte)
{
	return nanoseconds_per_byte > 0.0 ? 1000.0 / nanoseconds_per_byte : 0.0;
}

static void flush(stage_context *context)
{
	if (context->flush)
		context->flush();
}

static int stopping(const stage_context *context)
{
	return context->keep_going && !context->keep_going();
}

static void survey_pages(void)
{
	uint64_t page;

	for (page = 0; page < PAGE_COUNT; page++)
		page_states[page] = (uint8_t)OSQueryVirtAddr((uint32_t)(page * OS_PAGE_SIZE));
}

static void report_ranges(stage_context *context)
{
	report *r = context->report;
	static probe_range ranges[MAX_RUNS_SHOWN];
	size_t count = probe_compress_ranges(page_states, PAGE_COUNT, OS_PAGE_SIZE, ranges, MAX_RUNS_SHOWN);
	size_t shown = count < MAX_RUNS_SHOWN ? count : MAX_RUNS_SHOWN;
	unsigned in_window[8] = {0};
	size_t i;
	uint64_t page;
	uint64_t first = WINDOW_START / OS_PAGE_SIZE;

	report_value(r, "address space runs", "%u", (unsigned)count);
	for (i = 0; i < shown; i++)
		report_value(r, "address space", "%08X-%08X %s", (unsigned)ranges[i].start, (unsigned)ranges[i].end,
			state_name(ranges[i].state));
	if (count > shown)
		report_note(r, "%u more runs not listed", (unsigned)(count - shown));

	for (page = first; page < first + WINDOW_SIZE / OS_PAGE_SIZE; page++)
		in_window[page_states[page] & 7]++;
	report_value(r, "0x80000000 window",
		"%u pages: %u free, %u allocated, %u read-write, %u read-only, %u invalid", (unsigned)(WINDOW_SIZE / OS_PAGE_SIZE),
		in_window[OS_MAP_MEMORY_FREE], in_window[OS_MAP_MEMORY_ALLOCATED], in_window[OS_MAP_MEMORY_READ_WRITE],
		in_window[OS_MAP_MEMORY_READ_ONLY], in_window[OS_MAP_MEMORY_INVALID]);
}

static void report_heap(report *r, const char *name, MEMBaseHeapType type)
{
	MEMHeapHandle heap = MEMGetBaseHeapHandle(type);
	char text[24];
	char total[24];

	if (!heap)
	{
		report_value(r, name, "no heap");
		return;
	}
	size_text((uint64_t)((uintptr_t)heap->dataEnd - (uintptr_t)heap->dataStart), total, sizeof(total));
	switch (heap->tag)
	{
	case MEM_EXPANDED_HEAP_TAG:
		size_text(MEMGetAllocatableSizeForExpHeapEx(heap, 4), text, sizeof(text));
		report_value(r, name, "expanded heap %08X-%08X (%s), %s allocatable in one block", (unsigned)(uintptr_t)heap->dataStart,
			(unsigned)(uintptr_t)heap->dataEnd, total, text);
		break;
	case MEM_FRAME_HEAP_TAG:
		size_text(MEMGetAllocatableSizeForFrmHeapEx(heap, 4), text, sizeof(text));
		report_value(r, name, "frame heap %08X-%08X (%s), %s allocatable in one block", (unsigned)(uintptr_t)heap->dataStart,
			(unsigned)(uintptr_t)heap->dataEnd, total, text);
		break;
	default:
		report_value(r, name, "heap of tag %08X, %08X-%08X (%s)", (unsigned)heap->tag, (unsigned)(uintptr_t)heap->dataStart,
			(unsigned)(uintptr_t)heap->dataEnd, total);
		break;
	}
}

static void report_areas(report *r)
{
	uint32_t address;
	uint32_t size;
	char text[24];

	if (OSGetMemBound(OS_MEM1, &address, &size) == 0)
	{
		size_text(size, text, sizeof(text));
		report_value(r, "MEM1 bound", "%08X, %s", (unsigned)address, text);
	}
	if (OSGetMemBound(OS_MEM2, &address, &size) == 0)
	{
		size_text(size, text, sizeof(text));
		report_value(r, "MEM2 bound", "%08X, %s", (unsigned)address, text);
	}
	if (OSGetForegroundBucketFreeArea(&address, &size))
	{
		size_text(size, text, sizeof(text));
		report_value(r, "foreground bucket free area", "%08X, %s", (unsigned)address, text);
	}
	OSGetMapVirtAddrRange(&address, &size);
	size_text(size, text, sizeof(text));
	report_value(r, "virtual range for mapping", "%08X-%08X (%s)", (unsigned)address, (unsigned)(address + size), text);
	OSGetAvailPhysAddrRange(&address, &size);
	size_text(size, text, sizeof(text));
	report_value(r, "available physical memory", "%08X-%08X (%s)", (unsigned)address, (unsigned)(address + size), text);
	OSGetDataPhysAddrRange(&address, &size);
	size_text(size, text, sizeof(text));
	report_value(r, "data physical memory", "%08X-%08X (%s)", (unsigned)address, (unsigned)(address + size), text);
	report_heap(r, "MEM1 base heap", MEM_BASE_HEAP_MEM1);
	report_heap(r, "MEM2 base heap", MEM_BASE_HEAP_MEM2);
	report_heap(r, "foreground base heap", MEM_BASE_HEAP_FG);
}

/* the largest block the C heap gives, in 8 MiB steps up to 1 GiB, one at a time and freed at once */
static void report_heap_blocks(report *r)
{
	uint32_t megabytes;
	uint32_t largest = 0;
	void *block128 = NULL;

	for (megabytes = 8; megabytes <= 1024; megabytes += 8)
	{
		void *block = memalign(OS_PAGE_SIZE, (size_t)megabytes * 1024u * 1024u);

		if (!block)
			break;
		largest = megabytes;
		free(block);
	}
	report_value(r, "largest C heap block (128 KiB aligned)", "%u MiB (searched to 1024 MiB in steps of 8)", (unsigned)largest);
	block128 = memalign(OS_PAGE_SIZE, WINDOW_SIZE);
	report_check(r, block128 != NULL, "128 MiB from the heap", "%08X", (unsigned)(uintptr_t)block128);
	free(block128);
}

/* writes a word per 4 KiB, reads them back, and says how many came back wrong */
static unsigned write_and_verify(volatile uint32_t *memory, size_t bytes)
{
	const size_t step = 4096 / sizeof(uint32_t);
	const size_t words = bytes / sizeof(uint32_t);
	unsigned wrong = 0;
	size_t i;

	for (i = 0; i < words; i += step)
		memory[i] = (uint32_t)i * 2654435761u + 1u;
	for (i = 0; i < words; i += step)
		if (memory[i] != (uint32_t)i * 2654435761u + 1u)
			wrong++;
	return wrong;
}

static void bench_memory(stage_context *context, const char *label, void *memory, size_t size, void *reference)
{
	report *r = context->report;
	uint32_t sink = 0;
	double fill = bench_fill(context->now_ns, memory, size);
	double read = bench_read(context->now_ns, memory, size, &sink);
	double copy_to = bench_copy(context->now_ns, memory, reference, size);
	double copy_from = bench_copy(context->now_ns, reference, memory, size);

	report_value(r, label, "fill %.0f MB/s, read %.0f MB/s, copy to %.0f MB/s, copy from %.0f MB/s (checksum %08X)",
		megabytes_per_second(fill), megabytes_per_second(read), megabytes_per_second(copy_to), megabytes_per_second(copy_from),
		(unsigned)sink);
}

/*
One attempt: reserve WINDOW_SIZE of address space (at `requested`, or wherever the system likes if 0),
map the largest part of the available physical memory into it that the system accepts, use it, and
give everything back.
*/
static void try_window(stage_context *context, const char *label, uint32_t requested, void *reference)
{
	report *r = context->report;
	static const uint32_t sizes_mib[] = {128, 64, 32, 16, 8, 4, 1};
	uint32_t physical;
	uint32_t physical_size;
	uint32_t aligned;
	uint32_t usable;
	uint32_t got;
	size_t i;
	int mapped = 0;

	OSGetAvailPhysAddrRange(&physical, &physical_size);
	aligned = (physical + OS_PAGE_SIZE - 1) & ~(uint32_t)(OS_PAGE_SIZE - 1);
	usable = physical_size > aligned - physical ? physical_size - (aligned - physical) : 0;

	report_value(r, label, "OSAllocVirtAddr(%08X, %08X, %X)", (unsigned)requested, (unsigned)WINDOW_SIZE, (unsigned)OS_PAGE_SIZE);
	flush(context);
	got = OSAllocVirtAddr(requested, WINDOW_SIZE, OS_PAGE_SIZE);
	report_check(r, got != 0, label, "reserved %08X-%08X", (unsigned)got, (unsigned)(got + WINDOW_SIZE));
	if (!got)
		return;
	if (requested)
		report_check(r, got == requested, label, "the range is at %08X, asked for %08X", (unsigned)got, (unsigned)requested);
	report_value(r, label, "state after reserving: %s", state_name(OSQueryVirtAddr(got)));

	for (i = 0; i < sizeof(sizes_mib) / sizeof(sizes_mib[0]) && !mapped; i++)
	{
		const uint32_t size = sizes_mib[i] * 1024u * 1024u;
		char text[24];

		if (size > usable)
			continue;
		size_text(size, text, sizeof(text));
		report_value(r, label, "OSMapMemory(%08X, %08X, %s, read-write)", (unsigned)got, (unsigned)aligned, text);
		flush(context);
		mapped = OSMapMemory(got, aligned, size, OS_MAP_MEMORY_READ_WRITE);
		if (!mapped)
		{
			report_value(r, label, "refused %s", text);
			continue;
		}
		report_check(r, 1, label, "mapped %s at %08X", text, (unsigned)got);
		report_check(r, OSEffectiveToPhysical(got) == aligned, label, "%08X is physical %08X, expected %08X", (unsigned)got,
			(unsigned)OSEffectiveToPhysical(got), (unsigned)aligned);
		report_check(r, OSQueryVirtAddr(got) == OS_MAP_MEMORY_READ_WRITE, label, "state after mapping: %s",
			state_name(OSQueryVirtAddr(got)));
		report_check(r, write_and_verify((volatile uint32_t *)(uintptr_t)got, size) == 0, label, "a word every 4 KiB written and read back");
		if (reference)
			bench_memory(context, label, (void *)(uintptr_t)got, size < BENCH_SIZE ? size : BENCH_SIZE, reference);
		report_check(r, OSUnmapMemory(got, size), label, "unmapped");
	}
	if (!mapped)
		report_check(r, 0, label, "no size from 128 MiB down to 1 MiB could be mapped (%u bytes of physical memory available)", (unsigned)usable);
	report_check(r, OSFreeVirtAddr(got, WINDOW_SIZE), label, "address range given back");
}

void stage_memory(stage_context *context)
{
	report *r = context->report;
	void *reference;
	uint64_t page;
	int restored = 1;
	static uint8_t before[PAGE_COUNT];

	if (stopping(context))
		return;
	report_section(r, "memory");
	report_areas(r);

	survey_pages();
	memcpy(before, page_states, sizeof(before));
	report_ranges(context);
	report_heap_blocks(r);

	reference = memalign(OS_PAGE_SIZE, BENCH_SIZE);
	if (reference)
	{
		uint32_t sink = 0;
		double fill = bench_fill(context->now_ns, reference, BENCH_SIZE);
		double read = bench_read(context->now_ns, reference, BENCH_SIZE, &sink);

		report_value(r, "heap 8 MiB", "fill %.0f MB/s, read %.0f MB/s (checksum %08X)", megabytes_per_second(fill),
			megabytes_per_second(read), (unsigned)sink);
	}

	if (!stopping(context))
		try_window(context, "map at 0x80000000", WINDOW_START, reference);
	if (!stopping(context))
		try_window(context, "map anywhere", 0, reference);

	/* everything taken has been given back: the address space is as it was */
	survey_pages();
	for (page = 0; page < PAGE_COUNT; page++)
		if (page_states[page] != before[page])
			restored = 0;
	report_check(r, restored, "address space restored", "every page is in the state it was in before the attempts");
	free(reference);
}
