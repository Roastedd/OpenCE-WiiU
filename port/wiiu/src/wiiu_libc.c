/*
WIIU_LIBC.C

What the platform layer shared with the Linux builds calls of the C library and newlib, the Wii U's, does not have: pause and
sysconf (xbox_kernel.c: Sleep(INFINITE) and GlobalMemoryStatus); sigaction, alarm and getrusage (posix_server.c's, a dedicated
server's signals and use of the processor: the Wii U runs no dedicated server, and its log's and settings' file helpers are there).

Not run on a console.
*/

#include <coreinit/core.h>
#include <coreinit/memexpheap.h>
#include <coreinit/memheap.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

/* waits until a signal arrives: there are none on the Wii U, so for ever */
int pause(void);

int pause(void)
{
	for (;;)
		OSSleepTicks(OSMillisecondsToTicks(1000));
}

/* wut 1.9.1's nanosleep (wut_nanosleep.c) writes the time left through rem without asking whether there is one, so every
nanosleep(&time, NULL) - the shell's own log thread, newlib's sleep and usleep - ends in a write to address 8: the first run of the game on a
console crashed its log thread this way (a DSI at __wut_nanosleep, address 8). This one is the call as POSIX has it: it sleeps, and leaves
nothing to do for rem but to say that no time is left. */
int nanosleep(const struct timespec *request, struct timespec *remaining);

int nanosleep(const struct timespec *request, struct timespec *remaining)
{
	if (!request || request->tv_sec < 0 || request->tv_nsec < 0 || request->tv_nsec >= 1000000000L)
	{
		errno = EINVAL;
		return -1;
	}
	OSSleepTicks((OSTime)request->tv_sec * (OSTime)OSSecondsToTicks(1) + (OSTime)OSNanosecondsToTicks((OSTime)request->tv_nsec));
	if (remaining)
	{
		remaining->tv_sec = 0;
		remaining->tv_nsec = 0;
	}
	return 0;
}

/* pread and pwrite (newlib has neither): a seek, the transfer and the seek back, under one lock, so that two threads using the same descriptor do not
disturb each other's place. (The file layer reads maps with them from several threads.) */
#include <pthread.h>
#include <sys/types.h>

ssize_t pread(int descriptor, void *buffer, size_t size, off_t offset);
ssize_t pwrite(int descriptor, const void *buffer, size_t size, off_t offset);

static pthread_mutex_t positioned_lock = PTHREAD_MUTEX_INITIALIZER;

ssize_t pread(int descriptor, void *buffer, size_t size, off_t offset)
{
	off_t before;
	ssize_t done;

	pthread_mutex_lock(&positioned_lock);
	before = lseek(descriptor, 0, SEEK_CUR);
	if (before < 0 || lseek(descriptor, offset, SEEK_SET) < 0)
	{
		pthread_mutex_unlock(&positioned_lock);
		return -1;
	}
	done = read(descriptor, buffer, size);
	lseek(descriptor, before, SEEK_SET);
	pthread_mutex_unlock(&positioned_lock);
	return done;
}

ssize_t pwrite(int descriptor, const void *buffer, size_t size, off_t offset)
{
	off_t before;
	ssize_t done;

	pthread_mutex_lock(&positioned_lock);
	before = lseek(descriptor, 0, SEEK_CUR);
	if (before < 0 || lseek(descriptor, offset, SEEK_SET) < 0)
	{
		pthread_mutex_unlock(&positioned_lock);
		return -1;
	}
	done = write(descriptor, buffer, size);
	lseek(descriptor, before, SEEK_SET);
	pthread_mutex_unlock(&positioned_lock);
	return done;
}

/* the 4 KiB of a page of Cafe OS's memory management (OS_PAGE_SIZE, 128 KiB, is the granularity of OSMapMemory) */
#define PAGE_BYTES 4096

long sysconf(int name);

long sysconf(int name)
{
	MEMHeapHandle heap;

	switch (name)
	{
	case _SC_PAGESIZE:
		return PAGE_BYTES;
	case _SC_NPROCESSORS_CONF:
	case _SC_NPROCESSORS_ONLN:
		return (long)OSGetCoreCount();
	case _SC_PHYS_PAGES:
	case _SC_AVPHYS_PAGES:
		/* the application's memory, MEM2's heap: the whole of it and what is free in it (-1 if the heap is not the kind
		this knows, and the caller assumes what it assumes without) */
		heap = MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM2);
		if (!heap || heap->tag != MEM_EXPANDED_HEAP_TAG)
			return -1;
		if (name == _SC_PHYS_PAGES)
			return (long)(((uintptr_t)heap->dataEnd - (uintptr_t)heap->dataStart) / PAGE_BYTES);
		return (long)(MEMGetTotalFreeSizeForExpHeap(heap) / PAGE_BYTES);
	default:
		errno = EINVAL;
		return -1;
	}
}

/* (no signals are sent on the Wii U: none is handled, and no alarm is set; nothing counts the process's use) */
struct sigaction;
struct rusage;
int sigaction(int number, const struct sigaction *action, struct sigaction *previous);
int getrusage(int who, struct rusage *usage);

int sigaction(int number, const struct sigaction *action, struct sigaction *previous)
{
	(void)number;
	(void)action;
	(void)previous;
	errno = ENOSYS;
	return -1;
}

unsigned int alarm(unsigned int seconds)
{
	(void)seconds;
	return 0;
}

int getrusage(int who, struct rusage *usage)
{
	(void)who;
	(void)usage;
	errno = ENOSYS;
	return -1;
}
