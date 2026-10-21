/*
WIIU_WINDOW.C

The Xbox memory window's memory (halo_port_capacity.h, HALO_PORT_WINDOW_BASE): 128 MiB taken from the top of the app's MEM2 heap before anything else
can take it, so that it is where the maps and the game's fixed addresses expect it. The Wii U gives an app no way to map memory at an address of its
choosing (the probe, WIIU_PORT_PLAN.md 6.2), but the heap's end is the same on every start (0x49FA0000), and an expanded heap gives a block from its
end when the alignment asked for is negative.

The allocator over it (xbox_memory_wiiu.c) is built with the Xbox's ABI and asks this file, built with the console's, where the window is.
*/

#include "wiiu_shell.h"

#include <coreinit/memdefaultheap.h>
#include <stdint.h>

/* the window's size and base (halo_port_capacity.h and platform.h, which this file cannot include: they are the Xbox's ABI) */
#define WINDOW_BYTES 0x08000000u
#define WINDOW_BASE 0x41EA0000u
#define PAGE 4096u

static void *window;
static int window_tried;

/* The heap gives a block from the start of its free space: the lowest address that fits. The window is wanted at the end, so a filler is taken
first, as large as the space before the window's place, and given back once the window is taken. How large the filler must be (the blocks have
headers, and the heap's start moves with the size of the app) is found by trying: the block's first place says how far off the filler was. */
static void *take_at_the_end(void)
{
	void *probe = MEMAllocFromDefaultHeapEx(WINDOW_BYTES, PAGE);
	uint32_t first, filler_bytes;
	int attempt;

	if (!probe)
	{
		wiiu_log("Memory window: no 128 MiB block in the heap at all");
		return NULL;
	}
	first = (uint32_t)(uintptr_t)probe;
	MEMFreeToDefaultHeap(probe);
	if (first > WINDOW_BASE)
	{
		wiiu_log("Memory window: the heap's free space starts at %08x, past where the window goes (%08x)", (unsigned)first, (unsigned)WINDOW_BASE);
		return NULL;
	}
	filler_bytes = WINDOW_BASE - first;
	for (attempt = 0; attempt < 6; attempt++)
	{
		void *filler = filler_bytes ? MEMAllocFromDefaultHeapEx(filler_bytes, PAGE) : NULL;
		void *block;
		uint32_t got;

		if (filler_bytes && !filler)
		{
			wiiu_log("Memory window: no filler of %u bytes", (unsigned)filler_bytes);
			return NULL;
		}
		block = MEMAllocFromDefaultHeapEx(WINDOW_BYTES, PAGE);
		got = block ? (uint32_t)(uintptr_t)block : 0;
		if (filler)
			MEMFreeToDefaultHeap(filler);
		if (block && got == WINDOW_BASE)
			return block;
		wiiu_log("Memory window: attempt %d: filler %u bytes, the block came at %08x", attempt + 1, (unsigned)filler_bytes, (unsigned)got);
		if (block)
			MEMFreeToDefaultHeap(block);
		if (!got || got < WINDOW_BASE)
		{
			/* too little filler: the block fell short of the end by the difference */
			filler_bytes += got ? WINDOW_BASE - got : 0x10u;
		}
		else
		{
			/* too much (or the space is not one piece) */
			if (got - WINDOW_BASE >= filler_bytes)
				return NULL;
			filler_bytes -= got - WINDOW_BASE;
		}
	}
	return NULL;
}

/* the window's memory, taken the first time it is asked for (the shell's log is ready by then); NULL if the system did not give it where it is
expected */
void *wiiu_window_memory(void);

void *wiiu_window_memory(void)
{
	if (window_tried)
		return window;
	window_tried = 1;
	window = take_at_the_end();
	if (!window)
	{
		wiiu_log("Memory window: not made");
		return NULL;
	}
	wiiu_log("Memory window: %u MiB at %08x", (unsigned)(WINDOW_BYTES >> 20), (unsigned)(uintptr_t)window);
	return window;
}

/* The allocation is made before the game's code runs (a constructor): the heap is unfragmented then, and the end of it is the end that was
measured. */
__attribute__((constructor(101))) static void window_reserve(void)
{
	(void)wiiu_window_memory();
}
