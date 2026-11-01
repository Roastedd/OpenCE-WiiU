/* Owned compressed packets and decoded PCM; no stream or decoder references. */
#ifndef WIIU_PACKET_POOL_H
#define WIIU_PACKET_POOL_H

#include <stddef.h>
#include <stdint.h>

struct wiiu_packet_pool_stats
{
	/* Heap counters count attempts, including a retry and a recovered
	failure. Arithmetic size rejection never makes a heap request. */
	uint32_t requests, hits, heap_allocations, heap_failures, returns, heap_frees;
	uint32_t retries, reclaimed_blocks, size_rejections;
	uint32_t cached_blocks, cached_bytes;
};

/* Only pointers returned by allocate may be passed to release. A buffer
belongs to its caller until release, regardless of stream cancellation. */
/* A failed heap request reclaims idle cache storage and retries at most once;
the cache must not make a failed allocation permanent while it retains RAM. */
void *wiiu_packet_pool_allocate(size_t bytes);
void wiiu_packet_pool_release(void *buffer);
void wiiu_packet_pool_stats(struct wiiu_packet_pool_stats *stats, int reset);
/* Detach cached buffers only. Active decoder/mixer buffers are untouched. */
void wiiu_packet_pool_trim(void);

#endif
