/*
WIIU_TLS.C

Thread-local storage for the Wii U.

The game and the platform layer use thread-local variables: the C library's wide string state (msvc_wide.c), Winsock's
last error (xnet.c), the debug assertion skipper (cseries.c) and more. An RPX has no TLS segment (elf2rpl refuses the
relocations), so the Wii U build compiles with -femulated-tls: each access calls libgcc's __emutls_get_address, which
keeps a thread's variables behind a pthread key. newlib's pthread keys are the four __syscall_tls_* functions that the
platform provides, and wut 1.9.1 does not: without them pthread_key_create fails with ENOSYS and libgcc aborts at the first
access. These are those, over the thread-specific slots Cafe OS gives every thread.

One slot (OS_THREAD_SPECIFIC_13; wut keeps 14 and 15 for itself and says the first fourteen are the application's) holds a
pointer to the thread's table of values, made when the thread first sets a key. The table is freed, and the keys'
destructors run, when the thread ends: the thread's cleanup callback is replaced by ours, which calls the one that was
there (wut's, which frees the thread's C library state; wut installs it when the thread first uses the C library, which
setting a key does first).

Not run on a console.
*/

#include <coreinit/thread.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/iosupport.h>

#define TLS_SLOT OS_THREAD_SPECIFIC_13
#define MAX_KEYS 32
#define DESTRUCTOR_ITERATIONS 4 /* POSIX's PTHREAD_DESTRUCTOR_ITERATIONS */

typedef void (*destructor_fn)(void *);

struct tls_table
{
	OSThreadCleanupCallbackFn previous_cleanup;
	void *values[MAX_KEYS];
};

/* a bit for each key made and not deleted; a destructor for each */
static volatile uint32_t keys_in_use;
static destructor_fn destructors[MAX_KEYS];

static void thread_ends(OSThread *thread, void *stack)
{
	struct tls_table *table = thread->specific[TLS_SLOT];
	OSThreadCleanupCallbackFn previous;
	int iteration;

	if (!table)
		return;
	/* a destructor may set a key again: go round until none is left, a few times, as POSIX does */
	for (iteration = 0; iteration < DESTRUCTOR_ITERATIONS; iteration++)
	{
		int any = 0;
		uint32_t key;

		for (key = 0; key < MAX_KEYS; key++)
		{
			void *value = table->values[key];

			if (value && destructors[key] && (keys_in_use & (1u << key)))
			{
				table->values[key] = NULL;
				destructors[key](value);
				any = 1;
			}
		}
		if (!any)
			break;
	}
	thread->specific[TLS_SLOT] = NULL;
	previous = table->previous_cleanup;
	free(table);
	if (previous)
		previous(thread, stack);
}

int __syscall_tls_create(uint32_t *key, void (*destructor)(void *))
{
	if (!key)
		return EINVAL;
	for (;;)
	{
		uint32_t used = keys_in_use;
		uint32_t index;

		if (used == 0xFFFFFFFFu)
			return EAGAIN;
		index = (uint32_t)__builtin_ctz(~used);
		if (__sync_bool_compare_and_swap(&keys_in_use, used, used | (1u << index)))
		{
			destructors[index] = destructor;
			__sync_synchronize();
			*key = index;
			return 0;
		}
	}
}

int __syscall_tls_delete(uint32_t key)
{
	if (key >= MAX_KEYS || !(keys_in_use & (1u << key)))
		return EINVAL;
	destructors[key] = NULL;
	/* (the values other threads hold under it stay, as POSIX leaves them) */
	__sync_fetch_and_and(&keys_in_use, ~(1u << key));
	return 0;
}

void *__syscall_tls_get(uint32_t key)
{
	struct tls_table *table = OSGetThreadSpecific(TLS_SLOT);

	return table && key < MAX_KEYS ? table->values[key] : NULL;
}

int __syscall_tls_set(uint32_t key, const void *value)
{
	struct tls_table *table;

	if (key >= MAX_KEYS || !(keys_in_use & (1u << key)))
		return EINVAL;
	table = OSGetThreadSpecific(TLS_SLOT);
	if (!table)
	{
		/* (calloc is the thread's first use of the C library if it is its first: wut's cleanup callback is in place after it) */
		table = calloc(1, sizeof(*table));
		if (!table)
			return ENOMEM;
		table->previous_cleanup = OSSetThreadCleanupCallback(OSGetCurrentThread(), thread_ends);
		OSSetThreadSpecific(TLS_SLOT, table);
	}
	table->values[key] = (void *)value;
	return 0;
}
