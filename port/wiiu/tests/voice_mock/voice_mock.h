/*
VOICE_MOCK.H

Cafe OS as port/wiiu/src/wiiu_voice_audio.c uses it, declared for test_voice_audio.c, which defines it all: threads, a
clock, mutexes, the cache and mic.rpl. A tick is a microsecond here. Written from wut's documented interface, not copied.
*/

#ifndef HALO_VOICE_MOCK_H
#define HALO_VOICE_MOCK_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

typedef int32_t BOOL;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

/* ---------- coreinit/time.h */

typedef int64_t OSTime;
#define OSMillisecondsToTicks(milliseconds) ((OSTime)(milliseconds) * 1000)
#define OSSecondsToTicks(seconds) ((OSTime)(seconds) * 1000000)
#define OSTicksToMilliseconds(ticks) ((OSTime)(ticks) / 1000)
#define OSTicksToMicroseconds(ticks) ((OSTime)(ticks))
OSTime OSGetSystemTime(void);

/* ---------- coreinit/thread.h */

typedef uint8_t OSThreadAttributes;
#define OS_THREAD_ATTRIB_AFFINITY_CPU0 (1u << 0)
#define OS_THREAD_ATTRIB_AFFINITY_CPU1 (1u << 1)
#define OS_THREAD_ATTRIB_AFFINITY_CPU2 (1u << 2)
#define OS_THREAD_ATTRIB_DETACHED (1u << 3)

typedef int (*OSThreadEntryPointFn)(int argc, const char **argv);

typedef struct OSThread
{
	OSThreadEntryPointFn entry;
	int32_t argc;
	int32_t priority;
	OSThreadAttributes attributes;
	const char *name;
	pthread_t host;
	int created, resumed, terminated, joined;
} OSThread;

BOOL OSCreateThread(OSThread *thread, OSThreadEntryPointFn entry, int32_t argc, char *argv, void *stack, uint32_t stack_size,
	int32_t priority, OSThreadAttributes attributes);
int32_t OSResumeThread(OSThread *thread);
void OSSetThreadName(OSThread *thread, const char *name);
OSThread *OSGetCurrentThread(void);
int32_t OSGetThreadPriority(OSThread *thread);
BOOL OSIsThreadTerminated(OSThread *thread);
BOOL OSJoinThread(OSThread *thread, int *result);
void OSSleepTicks(OSTime ticks);

/* ---------- coreinit/core.h */

uint32_t OSGetCoreId(void);

/* ---------- coreinit/mutex.h */

typedef struct OSMutex
{
	pthread_mutex_t lock;
} OSMutex;

void OSInitMutex(OSMutex *mutex);
void OSLockMutex(OSMutex *mutex);
void OSUnlockMutex(OSMutex *mutex);
BOOL OSTryLockMutex(OSMutex *mutex);

/* ---------- coreinit/cache.h */

void DCFlushRange(void *address, uint32_t size);
void DCInvalidateRange(void *address, uint32_t size);

/* ---------- mic/mic.h */

typedef int MICHandle;

typedef enum MICError
{
	MIC_ERROR_OK = 0,
	MIC_ERROR_NOT_OPENED = -1,
	MIC_ERROR_INVALID_HANDLE = -2,
	MIC_ERROR_INIT = -5,
	MIC_ERROR_ALREADY_CLOSED = -7,
	MIC_ERROR_INVALID_INSTANCE = -8,
} MICError;

typedef enum MICInstance
{
	MIC_INSTANCE_0 = 0,
	MIC_INSTANCE_1 = 1,
} MICInstance;

typedef struct MICWorkMemory
{
	size_t sampleMaxCount;
	void *sampleBuffer;
} MICWorkMemory;

typedef struct MICStatus
{
	int state;
	int availableData;
	int bufferPos;
} MICStatus;

MICHandle MICInit(MICInstance instance, int unused, MICWorkMemory *work_memory, MICError *error);
MICError MICOpen(MICHandle handle);
MICError MICGetState(MICHandle handle, int state, uint32_t *value);
MICError MICSetState(MICHandle handle, int state, uint32_t value);
MICError MICGetStatus(MICHandle handle, MICStatus *status);
MICError MICSetDataConsumed(MICHandle handle, int consumed);
MICError MICClose(MICHandle handle);
MICError MICUninit(MICHandle handle);

#endif
