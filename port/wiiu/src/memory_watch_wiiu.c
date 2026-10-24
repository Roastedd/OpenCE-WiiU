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

/* what was asked a moment ago (the same texture, buffer or palette is asked about at every draw that uses it) is answered as it was while no write has been announced since: any write
moves current_generation */
static struct
{
	unsigned long address, size, result;
	uint32_t serial;
} generation_memo[32];

unsigned long memory_watch_generation(unsigned long address, unsigned long size)
{
	unsigned long first, last, page, newest = 0;
	const unsigned slot = (unsigned)(((address >> 6) ^ (size >> 4)) & 31);
	const uint32_t serial = current_generation;

	if (generation_memo[slot].serial == serial && generation_memo[slot].address == address && generation_memo[slot].size == size)
		return generation_memo[slot].result;
	if (!watch_pages(address, size, &first, &last))
		return 0;
	for (page = first; page <= last; page++)
		if (page_generation[page] > newest)
			newest = page_generation[page];
	generation_memo[slot].serial = serial;
	generation_memo[slot].address = address;
	generation_memo[slot].size = size;
	generation_memo[slot].result = newest;
	return newest;
}

/* the same without the memo, for the GX2 thread (the memo is the game thread's: two threads would tear it) */
unsigned long memory_watch_generation_raw(unsigned long address, unsigned long size);

unsigned long memory_watch_generation_raw(unsigned long address, unsigned long size)
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

/* d3d8_gx2.c: hands the stream of commands over if one of them reads this memory: the game is about to change it */
void halo_stream_before_write(const void *address, unsigned long size);

static void mark(unsigned long address, unsigned long size)
{
	unsigned long first, last, page;
	uint32_t generation;

	if (!watch_pages(address, size, &first, &last))
		return;
	halo_stream_before_write((const void *)address, size);
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
