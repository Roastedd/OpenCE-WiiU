/* memory_watch_wiiu.c: a write the game announces gives its pages a newer generation, and only those pages. */

#include "platform.h"

#include <stdio.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

#define BASE PLATFORM_CONTIGUOUS_BASE

/* d3d8_gx2.c's: told of every write in the window before the generation moves */
static unsigned announced, announced_at_generation;
static unsigned long announced_size;

void halo_stream_before_write(const void *address, unsigned long size);

void halo_stream_before_write(const void *address, unsigned long size)
{
	(void)address;
	announced++;
	announced_size = size;
	announced_at_generation = (unsigned)memory_watch_serial();
}

int main(void)
{
	unsigned long before, after, serial;

	/* nothing announced: every range is at generation 0, and what is outside the window never changes */
	CHECK(memory_watch_generation(BASE, 4096) == 0);
	CHECK(memory_watch_generation(0x1000, 4096) == 0);
	CHECK(memory_watch_generation(BASE, 0) == 0);

	/* a write to the second page: that page and ranges touching it are newer, the first and third are not */
	serial = memory_watch_serial();
	memory_watch_prepare_write((void *)(BASE + 4096 + 100), 50);
	CHECK(memory_watch_serial() == serial + 1);
	after = memory_watch_generation(BASE + 4096, 4096);
	CHECK(after == serial + 1);
	CHECK(memory_watch_generation(BASE, 4096) == 0);
	CHECK(memory_watch_generation(BASE + 8192, 4096) == 0);
	CHECK(memory_watch_generation(BASE, 3 * 4096) == after);
	CHECK(memory_watch_generation(BASE + 4095, 2) == after); /* (touches the first page's last byte and the second's first) */

	/* (the stream of commands is told first, with the generation not yet moved) */
	CHECK(announced == 1 && announced_size == 50 && announced_at_generation == serial);

	/* a write across a page boundary marks both pages with one generation */
	memory_watch_prepare_write((void *)(BASE + 3 * 4096 - 8), 16);
	CHECK(memory_watch_generation(BASE + 2 * 4096, 4096) == memory_watch_generation(BASE + 3 * 4096, 4096));
	CHECK(memory_watch_generation(BASE + 2 * 4096, 4096) > after);

	/* taking a block back counts as a write; a later one is newer than an earlier one */
	before = memory_watch_generation(BASE + 4096, 1);
	memory_watch_forget((void *)(BASE + 4096), 4096);
	CHECK(memory_watch_generation(BASE + 4096, 1) > before);

	/* a range partly outside the window is clipped to it; wholly outside is ignored (and the stream is not told) */
	memory_watch_prepare_write((void *)(BASE - 100), 200);
	CHECK(memory_watch_generation(BASE, 1) == memory_watch_serial());
	serial = memory_watch_serial();
	memory_watch_prepare_write((void *)(BASE + PLATFORM_CONTIGUOUS_SIZE + 4096), 16);
	CHECK(memory_watch_serial() == serial);
	{
		const unsigned told = announced;

		memory_watch_prepare_write((void *)(BASE + PLATFORM_CONTIGUOUS_SIZE + 8192), 16);
		CHECK(announced == told);
	}
	memory_watch_prepare_write((void *)(BASE + PLATFORM_CONTIGUOUS_SIZE - 8), 64);
	CHECK(memory_watch_generation(BASE + PLATFORM_CONTIGUOUS_SIZE - 4096, 4096) == memory_watch_serial());

	printf(failures ? "%d failures\n" : "memory watch: all passed\n", failures);
	return failures != 0;
}
