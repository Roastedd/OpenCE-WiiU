/*
MEMORY_WATCH_WIIU.C

Write tracking for the memory the renderer caches (port/linux/src/memory_watch.c's interface), for the Wii U. There is no page protection to catch a write with, and hashing
the cached pages every frame (web_memory_watch.c) took a twentieth of the game thread, so, as the Switch's deko3d renderer does, only the writes the game announces count:
memory_watch_prepare_write (the Direct3D texture and buffer locks, the file reads into the window) and memory_watch_forget (a block given out or taken back) give the pages
a new generation, and a cached texture is stale when a page of it has a newer one than the texture was made at.
*/

#include "platform.h"

#define WATCH_PAGE_SIZE 4096UL
#define WATCH_PAGE_COUNT (PLATFORM_CONTIGUOUS_SIZE / WATCH_PAGE_SIZE)

static uint32_t page_generation[WATCH_PAGE_COUNT];
static volatile uint32_t current_generation = 1;

/* the pages of a range of the window, first to last; FALSE if it is not in the window */
static BOOL watch_pages(unsigned long address, unsigned long size, unsigned long *first, unsigned long *last)
{
	unsigned long start = address, end = address + size;

	if (!size || end <= PLATFORM_CONTIGUOUS_BASE || start >= PLATFORM_CONTIGUOUS_BASE + PLATFORM_CONTIGUOUS_SIZE)
		return FALSE;
	if (start < PLATFORM_CONTIGUOUS_BASE)
		start = PLATFORM_CONTIGUOUS_BASE;
	if (end > PLATFORM_CONTIGUOUS_BASE + PLATFORM_CONTIGUOUS_SIZE)
		end = PLATFORM_CONTIGUOUS_BASE + PLATFORM_CONTIGUOUS_SIZE;
	*first = (start - PLATFORM_CONTIGUOUS_BASE) / WATCH_PAGE_SIZE;
	*last = (end - 1 - PLATFORM_CONTIGUOUS_BASE) / WATCH_PAGE_SIZE;
	return TRUE;
}

void memory_watch_initialize(void)
{
}

void memory_watch_protect(unsigned long address, unsigned long size)
{
	(void)address;
	(void)size;
}

unsigned long memory_watch_generation(unsigned long address, unsigned long size)
{
	unsigned long first, last, page, newest = 0;

	if (!watch_pages(address, size, &first, &last))
		return 0;
	for (page = first; page <= last; page++)
		if (page_generation[page] > newest)
			newest = page_generation[page];
	return newest;
}

unsigned long memory_watch_serial(void)
{
	return current_generation;
}

static void mark(unsigned long address, unsigned long size)
{
	unsigned long first, last, page;
	uint32_t generation;

	if (!watch_pages(address, size, &first, &last))
		return;
	generation = __sync_add_and_fetch(&current_generation, 1);
	for (page = first; page <= last; page++)
		page_generation[page] = generation;
}

void memory_watch_prepare_write(void *address, unsigned long size)
{
	mark((unsigned long)address, size);
}

void memory_watch_forget(void *address, unsigned long size)
{
	mark((unsigned long)address, size);
}

void memory_watch_begin_frame(void)
{
}
