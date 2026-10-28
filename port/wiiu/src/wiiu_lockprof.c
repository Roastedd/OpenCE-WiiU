/*
WIIU_LOCKPROF.C

Where the game thread waits for locks: pthread_mutex_lock is wrapped at the link (tools/wiiu_build.py, --wrap), and while the profiler runs
(wiiu_profile.c, halo/profile.txt) a lock the game thread finds taken is timed, by the address it was called from. The profiler's report lists
the callers ("LOCK caller=0x... waited N ms in M waits"); tools/wiiu/profile_report.py's ELF names them. A lock found free costs a trylock.
*/

#include "wiiu_shell.h"

#include <coreinit/thread.h>
#include <coreinit/time.h>

#include <pthread.h>
#include <stdint.h>
#include <string.h>

#define CALLERS 64

static OSThread *volatile watched;
static struct
{
	uint32_t caller;
	uint32_t waits;
	uint64_t ticks;
} callers[CALLERS];

int __real_pthread_mutex_lock(pthread_mutex_t *mutex);
int __wrap_pthread_mutex_lock(pthread_mutex_t *mutex);
void wiiu_lockprof_watch(void *thread);
void wiiu_lockprof_report(void);

void wiiu_lockprof_watch(void *thread)
{
	watched = (OSThread *)thread;
}

int __wrap_pthread_mutex_lock(pthread_mutex_t *mutex)
{
	OSTime start;
	int result;
	uint32_t caller, slot, probes;

	if (!watched || OSGetCurrentThread() != watched || pthread_mutex_trylock(mutex) == 0)
		return watched && OSGetCurrentThread() == watched ? 0 : __real_pthread_mutex_lock(mutex);
	start = OSGetSystemTime();
	result = __real_pthread_mutex_lock(mutex);
	caller = (uint32_t)(uintptr_t)__builtin_return_address(0);
	slot = (caller >> 2) % CALLERS;
	for (probes = 0; probes < CALLERS; probes++, slot = (slot + 1) % CALLERS)
	{
		if (callers[slot].caller == caller || !callers[slot].caller)
		{
			callers[slot].caller = caller;
			callers[slot].waits++;
			callers[slot].ticks += (uint64_t)(OSGetSystemTime() - start);
			break;
		}
	}
	return result;
}

/* (the profiler's thread, every report; the game thread may add to the table meanwhile: a count may be off by one wait) */
void wiiu_lockprof_report(void)
{
	unsigned i;

	for (i = 0; i < CALLERS; i++)
	{
		if (!callers[i].caller)
			continue;
		wiiu_log("LOCK caller=0x%08x waited %.1f ms in %u waits", (unsigned)callers[i].caller,
			(double)OSTicksToMicroseconds(callers[i].ticks) / 1000.0, (unsigned)callers[i].waits);
	}
	memset(callers, 0, sizeof(callers));
}
