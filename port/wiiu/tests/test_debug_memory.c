/*
The game's debug memory (source/cseries/debug_memory.c: every malloc, free and realloc of the game's sources goes through it, and it keeps a list of the pointers and totals) with two
threads allocating at once, as the tick thread and the drawing thread do when the ticks run beside the drawing (tick_overlap.h): the tick snapshot's copies of objects it has not
met and the blends' buffers. Without one thread at a time in it the list loses a pointer (the unlink of a free halted a joined game's client on the network overlap's first runs,
2026-11-07: debug_memory.c #446 "previous"). The case runs some hundred thousand operations in each of four threads and then checks that every pointer is accounted for. Run under
the thread sanitizer where there is one.

    test_debug_memory
*/

#define _GNU_SOURCE
#include <unistd.h>
#define HALO_WIIU 1
#include "cseries.h"

#include <pthread.h>

__thread char temporary[256];

void error(int severity, char const *format, ...)
{
	(void)severity;
	(void)format;
}

char *csprintf(char *buffer, char const *format, ...)
{
	(void)format;
	buffer[0] = 0;
	return buffer;
}

void *csmemset(void *destination, int value, size_t size)
{
	return memset(destination, value, size);
}

unsigned long local_random(void)
{
	return 0x12345678UL;
}

void GlobalMemoryStatus(MEMORYSTATUS *status)
{
	status->dwAvailPhys = 0;
}

static volatile int failed;

void debug_memory_test_failed(char const *file, long line, char const *expression)
{
	if (!__sync_lock_test_and_set(&failed, 1))
		printf("check failed: %s,#%ld: %s\n", file, line, expression);
	/* (a list that has lost its way is not walked further: the thread ends the run) */
	fflush(stdout);
	_exit(1);
}

#include "debug_memory.c" /* (a copy of source/cseries/debug_memory.c in the test's folder: its own folder would give it the game's cseries.h) */

#define THREADS 4
#define OPERATIONS 150000
#define SLOTS 64

static void *worker(void *argument)
{
	unsigned long state = 0x9E3779B9UL + (unsigned long)(size_t)argument * 7919UL;
	void *held[SLOTS] = { 0 };
	int operation;
	int slot;

	for (operation = 0; operation < OPERATIONS; operation++)
	{
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		slot = (int)(state % SLOTS);
		switch ((state >> 8) % 3)
		{
			case 0:
				if (!held[slot])
					held[slot] = debug_malloc(16 + (unsigned int)((state >> 12) % 700), FALSE, __FILE__, __LINE__);
				break;
			case 1:
				if (held[slot])
				{
					debug_free(held[slot], __FILE__, __LINE__);
					held[slot] = NULL;
				}
				break;
			default:
				held[slot] = debug_realloc(held[slot], 16 + (unsigned int)((state >> 12) % 900), __FILE__, __LINE__);
				break;
		}
		if (operation % 20000 == 0)
			debug_check_memory(__FILE__, __LINE__);
	}
	for (slot = 0; slot < SLOTS; slot++)
	{
		if (held[slot])
			debug_free(held[slot], __FILE__, __LINE__);
	}
	return NULL;
}

int main(void)
{
	pthread_t threads[THREADS];
	long index;

	debug_memory_manager_initialize();
	for (index = 0; index < THREADS; index++)
		pthread_create(&threads[index], NULL, worker, (void *)(size_t)(index + 1));
	for (index = 0; index < THREADS; index++)
		pthread_join(threads[index], NULL);
	/* every pointer made was freed: the list is empty and the total is nothing */
	if (debug_memory_globals.first_pointer != NULL || debug_memory_globals.current_heap_size != 0)
	{
		printf("the list is not empty: first %p, total %ld\n", (void *)debug_memory_globals.first_pointer, debug_memory_globals.current_heap_size);
		return 1;
	}
	printf("debug memory: ok\n");
	return 0;
}
