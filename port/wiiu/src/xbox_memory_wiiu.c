/*
XBOX_MEMORY_WIILU.C

Xbox contiguous memory (XPhysicalAlloc) for the Wii U: xbox_memory.c without the pages. The window (HALO_PORT_WINDOW_BASE, 128 MiB) is one block
of the app's heap, reserved by wiiu_window.c, and always readable and writable: Cafe OS has no page protection for an app, so what the Linux build
protects (the tag cache once loaded, the textures the renderer watches) is only remembered, as in the web build, and the renderer finds a rewritten
texture by hashing its pages (web_memory_watch.c). A block is cleared when it is given out, as fresh pages would be.

Halo Custom Edition's tag cache at 0x40440000 (23 MiB) is a block of the same heap, taken with the window by wiiu_window.c; the game has it when the
game.custom_edition setting is on (as xbox_memory.c reserves it), and the heap has it back when the setting is off.
*/

#include "platform.h"
#include "port_config.h"

#include <string.h>

#define PAGE_SIZE_BYTES 0x1000UL
#define CONTIGUOUS_PAGE_COUNT (PLATFORM_CONTIGUOUS_SIZE / PAGE_SIZE_BYTES)

/* wiiu_window.c's (the console's ABI) */
void *wiiu_window_memory(void);
void *wiiu_custom_edition_tag_cache_memory(void);
void wiiu_custom_edition_tag_cache_release(void);

/* per page: 0 free, otherwise the protection of the block (PAGE_*); the first page of a block also records the block length */
static DWORD page_protection[CONTIGUOUS_PAGE_COUNT];
static unsigned long block_page_count[CONTIGUOUS_PAGE_COUNT];
static pthread_mutex_t arena_lock = PTHREAD_MUTEX_INITIALIZER;

/* The Custom Edition tag cache, when Custom Edition maps may run; asked for often (each draw's indices, d3d8_gx2.c), so decided once, the first
time, when the settings are read */
void *halo_custom_edition_tag_cache(void)
{
	static int decided;
	static void *tag_cache;

	if (!decided)
	{
		decided = 1;
		if (config_boolean("game.custom_edition"))
		{
			tag_cache = wiiu_custom_edition_tag_cache_memory();
			if (!tag_cache)
				platform_log("cannot reserve the Custom Edition tag cache at 0x40440000 (the log's first lines say why): Custom Edition maps cannot run");
		}
		else
		{
			wiiu_custom_edition_tag_cache_release();
		}
	}
	return tag_cache;
}

BOOL platform_is_contiguous(const void *address)
{
	unsigned long value = (unsigned long)address;

	return value >= PLATFORM_CONTIGUOUS_BASE && value - PLATFORM_CONTIGUOUS_BASE < PLATFORM_CONTIGUOUS_SIZE;
}

static BOOL pages_free(unsigned long first, unsigned long count)
{
	unsigned long page;

	if (first + count > CONTIGUOUS_PAGE_COUNT)
		return FALSE;
	for (page = first; page < first + count; page++)
	{
		if (page_protection[page])
			return FALSE;
	}
	return TRUE;
}

void *platform_contiguous_alloc(unsigned long size, unsigned long alignment, unsigned long physical_address, DWORD protect)
{
	unsigned long count = (size + PAGE_SIZE_BYTES - 1) / PAGE_SIZE_BYTES;
	unsigned long alignment_pages = alignment > PAGE_SIZE_BYTES ? alignment / PAGE_SIZE_BYTES : 1;
	unsigned long first = CONTIGUOUS_PAGE_COUNT;
	unsigned long page;
	void *address;

	if (!count)
		count = 1;
	protect &= ~(PAGE_WRITECOMBINE | PAGE_NOCACHE);
	if (!protect)
		protect = PAGE_READWRITE;

	if ((unsigned long)wiiu_window_memory() != PLATFORM_CONTIGUOUS_BASE)
		return NULL;
	pthread_mutex_lock(&arena_lock);
	if (physical_address != PLATFORM_ANY_PHYSICAL_ADDRESS)
	{
		unsigned long wanted = physical_address / PAGE_SIZE_BYTES;

		if (pages_free(wanted, count))
			first = wanted;
	}
	else if (count <= CONTIGUOUS_PAGE_COUNT)
	{
		/* top-down first fit, like the Xbox contiguous allocator */
		unsigned long candidate = CONTIGUOUS_PAGE_COUNT - count;

		for (;;)
		{
			candidate -= candidate % alignment_pages;
			if (pages_free(candidate, count))
			{
				first = candidate;
				break;
			}
			if (candidate == 0)
				break;
			candidate--;
		}
	}
	if (first == CONTIGUOUS_PAGE_COUNT)
	{
		pthread_mutex_unlock(&arena_lock);
		return NULL;
	}

	address = (void *)(PLATFORM_CONTIGUOUS_BASE + first * PAGE_SIZE_BYTES);
	memory_watch_forget(address, count * PAGE_SIZE_BYTES);
	memset(address, 0, count * PAGE_SIZE_BYTES);
	for (page = first; page < first + count; page++)
		page_protection[page] = protect;
	block_page_count[first] = count;
	pthread_mutex_unlock(&arena_lock);
	return address;
}

void platform_contiguous_free(void *address)
{
	unsigned long first, count, page;

	if (!platform_is_contiguous(address))
		return;
	first = ((unsigned long)address - PLATFORM_CONTIGUOUS_BASE) / PAGE_SIZE_BYTES;
	pthread_mutex_lock(&arena_lock);
	count = block_page_count[first];
	if (count)
	{
		memory_watch_forget(address, count * PAGE_SIZE_BYTES);
		for (page = first; page < first + count; page++)
			page_protection[page] = 0;
		block_page_count[first] = 0;
	}
	pthread_mutex_unlock(&arena_lock);
}

/* ---------- XAPI */

LPVOID WINAPI XPhysicalAlloc(SIZE_T size, ULONG_PTR physical_address, ULONG_PTR alignment, DWORD protect)
{
	/* A highest-acceptable address inside the window places the block there, which is how the game gets its fixed game state and tag cache
	addresses; anything else may go anywhere. */
	void *result = platform_contiguous_alloc(size, alignment,
		physical_address < PLATFORM_CONTIGUOUS_SIZE ? physical_address : PLATFORM_ANY_PHYSICAL_ADDRESS, protect);

	if (!result)
	{
		platform_log("XPhysicalAlloc: cannot allocate %lu bytes (physical address 0x%08lx)", (unsigned long)size, (unsigned long)physical_address);
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
	}
	return result;
}

VOID WINAPI XPhysicalFree(LPVOID address)
{
	platform_contiguous_free(address);
}

BOOL WINAPI VirtualProtect(LPVOID address, SIZE_T size, DWORD new_protect, PDWORD old_protect)
{
	unsigned long start = (unsigned long)address & ~(PAGE_SIZE_BYTES - 1);
	unsigned long end = ((unsigned long)address + size + PAGE_SIZE_BYTES - 1) & ~(PAGE_SIZE_BYTES - 1);

	if (old_protect)
		*old_protect = platform_is_contiguous(address) ? page_protection[(start - PLATFORM_CONTIGUOUS_BASE) / PAGE_SIZE_BYTES] : PAGE_READWRITE;
	memory_watch_forget((void *)start, end - start);
	if (platform_is_contiguous((void *)start))
	{
		unsigned long page;

		pthread_mutex_lock(&arena_lock);
		for (page = (start - PLATFORM_CONTIGUOUS_BASE) / PAGE_SIZE_BYTES;
			page < (end - PLATFORM_CONTIGUOUS_BASE) / PAGE_SIZE_BYTES && page < CONTIGUOUS_PAGE_COUNT; page++)
		{
			if (page_protection[page])
				page_protection[page] = new_protect & ~(PAGE_WRITECOMBINE | PAGE_NOCACHE);
		}
		pthread_mutex_unlock(&arena_lock);
	}
	return TRUE;
}

VOID WINAPI XPhysicalProtect(LPVOID address, SIZE_T size, DWORD new_protect)
{
	VirtualProtect(address, size, new_protect, NULL);
}

DWORD WINAPI XQueryMemoryProtect(LPVOID address)
{
	DWORD protect = PAGE_READWRITE;

	if (platform_is_contiguous(address))
	{
		pthread_mutex_lock(&arena_lock);
		protect = page_protection[((unsigned long)address - PLATFORM_CONTIGUOUS_BASE) / PAGE_SIZE_BYTES];
		pthread_mutex_unlock(&arena_lock);
		if (!protect)
			protect = PAGE_NOACCESS;
	}
	return protect;
}
