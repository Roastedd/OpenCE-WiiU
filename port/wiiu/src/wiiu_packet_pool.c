/* Small audio allocations are reused by size class. The cache retains at
most one MiB of payload plus 128 headers; live buffers remain independent
allocations. Its short critical sections still use a blocking mutex.
The game uses at most 32 KiB per ordinary PCM packet (8 KiB times the rate
and stereo factors). Larger requests keep exact heap allocation/free.

No allocator call, stream access or callback runs while the pool lock is
held. In particular this lock never waits for the mixer or decoder lock. */

#include "wiiu_packet_pool.h"

#include <pthread.h>
#include <stdlib.h>

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define PACKET_POOL_ASAN
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) && !defined(PACKET_POOL_ASAN)
#define PACKET_POOL_ASAN
#endif
#ifdef PACKET_POOL_ASAN
#include <sanitizer/asan_interface.h>
#define PACKET_POOL_POISON(pointer, bytes) __asan_poison_memory_region(pointer, bytes)
#define PACKET_POOL_UNPOISON(pointer, bytes) __asan_unpoison_memory_region(pointer, bytes)
#else
#define PACKET_POOL_POISON(pointer, bytes) ((void)0)
#define PACKET_POOL_UNPOISON(pointer, bytes) ((void)0)
#endif

#define PACKET_POOL_CLASSES 9
#define PACKET_POOL_MINIMUM 128u
#define PACKET_POOL_BLOCKS 128u
#define PACKET_POOL_BYTES (1024u * 1024u)

union packet_pool_header
{
	struct
	{
		union packet_pool_header *next;
		unsigned index;
	} entry;
	uint64_t alignment;
};

static struct
{
	pthread_mutex_t lock;
	union packet_pool_header *free[PACKET_POOL_CLASSES];
	struct wiiu_packet_pool_stats stats;
} packet_pool = { PTHREAD_MUTEX_INITIALIZER };

static unsigned packet_pool_reclaim(void);

static unsigned packet_pool_class(size_t bytes)
{
	size_t capacity = PACKET_POOL_MINIMUM;
	unsigned index;

	for (index = 0; index < PACKET_POOL_CLASSES; index++, capacity <<= 1)
		if (bytes <= capacity)
			return index;
	return PACKET_POOL_CLASSES;
}

void *wiiu_packet_pool_allocate(size_t bytes)
{
	const unsigned index = packet_pool_class(bytes);
	const size_t capacity = index < PACKET_POOL_CLASSES ? (size_t)PACKET_POOL_MINIMUM << index : bytes;
	union packet_pool_header *header = NULL;

	pthread_mutex_lock(&packet_pool.lock);
	packet_pool.stats.requests++;
	if (capacity > (size_t)-1 - sizeof(*header))
	{
		packet_pool.stats.size_rejections++;
		pthread_mutex_unlock(&packet_pool.lock);
		return NULL;
	}
	if (index < PACKET_POOL_CLASSES && packet_pool.free[index])
	{
		header = packet_pool.free[index];
		packet_pool.free[index] = header->entry.next;
		packet_pool.stats.cached_blocks--;
		packet_pool.stats.cached_bytes -= (uint32_t)capacity;
		packet_pool.stats.hits++;
	}
	else
	{
		packet_pool.stats.heap_allocations++;
	}
	pthread_mutex_unlock(&packet_pool.lock);
	if (!header)
	{
		header = malloc(sizeof(*header) + capacity);
		if (!header)
		{
			unsigned reclaimed;

			pthread_mutex_lock(&packet_pool.lock);
			packet_pool.stats.heap_failures++;
			pthread_mutex_unlock(&packet_pool.lock);
			/* This only detaches idle buffers. A borrowed source or PCM
			buffer is never linked in the cache, even after cancellation. */
			reclaimed = packet_pool_reclaim();
			if (reclaimed)
			{
				pthread_mutex_lock(&packet_pool.lock);
				packet_pool.stats.reclaimed_blocks += reclaimed;
				packet_pool.stats.retries++;
				packet_pool.stats.heap_allocations++;
				pthread_mutex_unlock(&packet_pool.lock);
				header = malloc(sizeof(*header) + capacity);
				if (!header)
				{
					pthread_mutex_lock(&packet_pool.lock);
					packet_pool.stats.heap_failures++;
					pthread_mutex_unlock(&packet_pool.lock);
				}
			}
			if (!header)
				return NULL;
		}
	}
	header->entry.index = index;
	PACKET_POOL_UNPOISON(header + 1, capacity);
	return header + 1;
}

void wiiu_packet_pool_release(void *buffer)
{
	union packet_pool_header *header;
	unsigned index;
	size_t capacity;
	int cached = 0;

	if (!buffer)
		return;
	header = (union packet_pool_header *)buffer - 1;
	index = header->entry.index;
	capacity = index < PACKET_POOL_CLASSES ? (size_t)PACKET_POOL_MINIMUM << index : 0;
	pthread_mutex_lock(&packet_pool.lock);
	packet_pool.stats.returns++;
	if (index < PACKET_POOL_CLASSES && packet_pool.stats.cached_blocks < PACKET_POOL_BLOCKS &&
		capacity <= PACKET_POOL_BYTES - packet_pool.stats.cached_bytes)
	{
		header->entry.next = packet_pool.free[index];
		packet_pool.free[index] = header;
		packet_pool.stats.cached_blocks++;
		packet_pool.stats.cached_bytes += (uint32_t)capacity;
		/* Returning storage is a lifetime boundary even though libc keeps
		its allocation. Sanitized host tests must still detect late readers. */
		PACKET_POOL_POISON(header + 1, capacity);
		cached = 1;
	}
	else
	{
		packet_pool.stats.heap_frees++;
	}
	pthread_mutex_unlock(&packet_pool.lock);
	if (!cached)
		free(header);
}

void wiiu_packet_pool_stats(struct wiiu_packet_pool_stats *stats, int reset)
{
	pthread_mutex_lock(&packet_pool.lock);
	*stats = packet_pool.stats;
	if (reset)
	{
		const uint32_t blocks = packet_pool.stats.cached_blocks, bytes = packet_pool.stats.cached_bytes;

		packet_pool.stats = (struct wiiu_packet_pool_stats){ 0 };
		packet_pool.stats.cached_blocks = blocks;
		packet_pool.stats.cached_bytes = bytes;
	}
	pthread_mutex_unlock(&packet_pool.lock);
}

static unsigned packet_pool_reclaim(void)
{
	union packet_pool_header *detached = NULL, *header;
	unsigned index, reclaimed = 0;

	pthread_mutex_lock(&packet_pool.lock);
	for (index = 0; index < PACKET_POOL_CLASSES; index++)
	{
		while ((header = packet_pool.free[index]) != NULL)
		{
			packet_pool.free[index] = header->entry.next;
			header->entry.next = detached;
			detached = header;
			packet_pool.stats.heap_frees++;
			reclaimed++;
		}
	}
	packet_pool.stats.cached_blocks = packet_pool.stats.cached_bytes = 0;
	pthread_mutex_unlock(&packet_pool.lock);
	while ((header = detached) != NULL)
	{
		detached = header->entry.next;
		free(header);
	}
	return reclaimed;
}

void wiiu_packet_pool_trim(void)
{
	packet_pool_reclaim();
}
