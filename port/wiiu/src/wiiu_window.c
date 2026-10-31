/*
WIIU_WINDOW.C

The Xbox memory window's memory (halo_port_capacity.h, HALO_PORT_WINDOW_BASE): 128 MiB taken from the top of the app's MEM2 heap before anything else
can take it, so that it is where the maps and the game's fixed addresses expect it. The Wii U gives an app no way to map memory at an address of its
choosing (the probe, WIIU_PORT_PLAN.md 6.2), but the heap's end is the same on every start (0x49FA0000), and an expanded heap gives a block from its
end when the alignment asked for is negative.

The allocator over it (xbox_memory_wiiu.c) is built with the Xbox's ABI and asks this file, built with the console's, where the window is.

Halo Custom Edition maps' tag data is linked at 0x40440000, 23 MiB (cache_file_formats.h): that range is in the same heap, 3.4 MiB below the window, and free at
start-up (the window's filler covers it), so it is taken the same way, once the window is, and kept for the whole run (xbox_memory_wiiu.c gives it to the game
when the game.custom_edition setting is on, and back to the heap when it is off).
*/

#include "wiiu_shell.h"

#include <coreinit/memdefaultheap.h>
#include <stdint.h>

/* the window's size and base (halo_port_capacity.h and platform.h, which this file cannot include: they are the Xbox's ABI), and the Custom Edition tag
cache's (cache_file_formats.h) */
#define WINDOW_BYTES 0x08000000u
#define WINDOW_BASE 0x41EA0000u
#define CUSTOM_EDITION_BYTES 0x01700000u
#define CUSTOM_EDITION_BASE 0x40440000u
#define PAGE 4096u

static void *window;
static int window_tried;
static void *custom_edition;

/* The heap gives a block from the start of its free space: the lowest address that fits. The window is wanted at the end, so a filler is taken
first, as large as the space before the window's place, and given back once the window is taken. How large the filler must be (the blocks have
headers, and the heap's start moves with the size of the app) is found by trying: the block's first place says how far off the filler was. The Custom
Edition tag cache is taken the same way, at its own place below the window's. */
static void *take_at(uint32_t base, uint32_t bytes, const char *name)
{
	void *probe = MEMAllocFromDefaultHeapEx(bytes, PAGE);
	uint32_t first, filler_bytes;
	int attempt;

	if (!probe)
	{
		wiiu_log("%s: no %u MiB block in the heap at all", name, (unsigned)(bytes >> 20));
		return NULL;
	}
	first = (uint32_t)(uintptr_t)probe;
	MEMFreeToDefaultHeap(probe);
	if (first > base)
	{
		wiiu_log("%s: the heap's free space starts at %08x, past where it goes (%08x)", name, (unsigned)first, (unsigned)base);
		return NULL;
	}
	filler_bytes = base - first;
	for (attempt = 0; attempt < 6; attempt++)
	{
		void *filler = filler_bytes ? MEMAllocFromDefaultHeapEx(filler_bytes, PAGE) : NULL;
		void *block;
		uint32_t got;

		if (filler_bytes && !filler)
		{
			wiiu_log("%s: no filler of %u bytes", name, (unsigned)filler_bytes);
			return NULL;
		}
		block = MEMAllocFromDefaultHeapEx(bytes, PAGE);
		got = block ? (uint32_t)(uintptr_t)block : 0;
		if (filler)
			MEMFreeToDefaultHeap(filler);
		if (block && got == base)
			return block;
		wiiu_log("%s: attempt %d: filler %u bytes, the block came at %08x", name, attempt + 1, (unsigned)filler_bytes, (unsigned)got);
		if (block)
			MEMFreeToDefaultHeap(block);
		if (!got || got < base)
		{
			/* too little filler: the block fell short of its place by the difference */
			filler_bytes += got ? base - got : 0x10u;
		}
		else
		{
			/* too much (or the space is not one piece) */
			if (got - base >= filler_bytes)
				return NULL;
			filler_bytes -= got - base;
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
	window = take_at(WINDOW_BASE, WINDOW_BYTES, "Memory window");
	if (!window)
	{
		wiiu_log("Memory window: not made");
		return NULL;
	}
	wiiu_log("Memory window: %u MiB at %08x", (unsigned)(WINDOW_BYTES >> 20), (unsigned)(uintptr_t)window);
	/* (once the window is placed: its filler is the heap's free space again, which the tag cache's place is in) */
	custom_edition = take_at(CUSTOM_EDITION_BASE, CUSTOM_EDITION_BYTES, "Custom Edition tag cache");
	if (custom_edition)
		wiiu_log("Custom Edition tag cache: %u MiB at %08x", (unsigned)(CUSTOM_EDITION_BYTES >> 20), (unsigned)(uintptr_t)custom_edition);
	else
		wiiu_log("Custom Edition tag cache: not made; Custom Edition maps cannot run");
	return window;
}

/* the Custom Edition tag cache's memory, taken with the window; NULL if the heap did not give it where its maps' tag data is linked */
void *wiiu_custom_edition_tag_cache_memory(void);

void *wiiu_custom_edition_tag_cache_memory(void)
{
	(void)wiiu_window_memory();
	return custom_edition;
}

/* gives the Custom Edition tag cache back to the heap (Custom Edition maps are turned off: game.custom_edition) */
void wiiu_custom_edition_tag_cache_release(void);

void wiiu_custom_edition_tag_cache_release(void)
{
	if (custom_edition)
	{
		MEMFreeToDefaultHeap(custom_edition);
		custom_edition = NULL;
		wiiu_log("Custom Edition tag cache: given back to the heap (game.custom_edition is off)");
	}
}

/* The allocation is made before the game's code runs (a constructor): the heap is unfragmented then, and the end of it is the end that was
measured. */
__attribute__((constructor(101))) static void window_reserve(void)
{
	(void)wiiu_window_memory();
}
