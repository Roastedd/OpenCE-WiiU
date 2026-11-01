/* The production pool with counted libc calls, including concurrent trim.
No GPU, audio device, game data or console is used. */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static unsigned long heap_calls, heap_frees, heap_bytes, fail_next;
static int track_heap = 1;
static int require_reclaim;
static unsigned long frees_required;
static _Thread_local unsigned pool_lock_depth;
static unsigned lock_violations;
static void *counted_malloc(size_t size)
{
	if (pool_lock_depth) __atomic_add_fetch(&lock_violations, 1, __ATOMIC_RELAXED);
	if (track_heap)
	{
		__atomic_add_fetch(&heap_calls, 1, __ATOMIC_RELAXED);
		__atomic_add_fetch(&heap_bytes, size, __ATOMIC_RELAXED);
	}
	if (fail_next)
	{
		fail_next--;
		return NULL;
	}
	if (require_reclaim && heap_frees < frees_required)
		return NULL;
	return malloc(size);
}
static void counted_free(void *memory)
{
	if (pool_lock_depth) __atomic_add_fetch(&lock_violations, 1, __ATOMIC_RELAXED);
	if (memory && track_heap)
		__atomic_add_fetch(&heap_frees, 1, __ATOMIC_RELAXED);
	free(memory);
}
#ifndef POOL_BENCHMARK
static int counted_lock(pthread_mutex_t *lock)
{
	int result = pthread_mutex_lock(lock);
	if (!result) pool_lock_depth++;
	return result;
}
static int counted_unlock(pthread_mutex_t *lock)
{
	pool_lock_depth--;
	return pthread_mutex_unlock(lock);
}
#endif
#define malloc counted_malloc
#define free counted_free
#ifdef POOL_BENCHMARK
/* The game and pool are separate platform translation units. Keep their
public call boundary when this fixture includes the source for accounting. */
__attribute__((noinline)) void *wiiu_packet_pool_allocate(size_t bytes);
__attribute__((noinline)) void wiiu_packet_pool_release(void *buffer);
#endif
#ifndef POOL_BENCHMARK
#define pthread_mutex_lock counted_lock
#define pthread_mutex_unlock counted_unlock
#endif
#include POOL_SOURCE
#undef malloc
#undef free
#undef pthread_mutex_lock
#undef pthread_mutex_unlock

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "pool check failed at %d: %s\n", __LINE__, #condition); return 1; } } while (0)

static int basic(void)
{
	void *buffers[256], *a, *b;
	struct wiiu_packet_pool_stats stats;
	unsigned i;

	wiiu_packet_pool_trim();
	wiiu_packet_pool_stats(&stats, 1);
	a = wiiu_packet_pool_allocate(2304);
	b = wiiu_packet_pool_allocate(2304);
	CHECK(a && b && a != b && ((uintptr_t)a & 7) == 0);
	memset(a, 11, 2304); memset(b, 29, 2304);
	wiiu_packet_pool_release(a);
	a = wiiu_packet_pool_allocate(2304);
	CHECK(a && a != b && ((unsigned char *)b)[2303] == 29);
	wiiu_packet_pool_release(a); wiiu_packet_pool_release(b);
	wiiu_packet_pool_stats(&stats, 0);
	CHECK(stats.requests == 3 && stats.hits == 1 && stats.heap_allocations == 2);
	CHECK(stats.cached_blocks == 2 && stats.cached_bytes == 8192);
	a = wiiu_packet_pool_allocate(2304); b = wiiu_packet_pool_allocate(2304);
	CHECK(a && b && a != b);
	wiiu_packet_pool_release(a); wiiu_packet_pool_release(b);
	for (i = 0; i < 256; i++)
	{
		buffers[i] = wiiu_packet_pool_allocate((i & 1) ? 32768 : 128);
		CHECK(buffers[i]);
		memset(buffers[i], (int)i, (i & 1) ? 32768 : 128);
	}
	for (i = 0; i < 256; i++)
		wiiu_packet_pool_release(buffers[i]);
	wiiu_packet_pool_stats(&stats, 0);
	CHECK(stats.cached_blocks <= 128 && stats.cached_bytes <= 1024 * 1024);
	CHECK(stats.heap_frees != 0);
	wiiu_packet_pool_trim();
	wiiu_packet_pool_stats(&stats, 0);
	CHECK(stats.cached_blocks == 0 && stats.cached_bytes == 0);
	a = wiiu_packet_pool_allocate(65537);
	CHECK(a); memset(a, 81, 65537); wiiu_packet_pool_release(a);
	wiiu_packet_pool_stats(&stats, 0);
	CHECK(stats.cached_blocks == 0);
	CHECK(wiiu_packet_pool_allocate((size_t)-1) == NULL);
	a = wiiu_packet_pool_allocate(0);
	CHECK(a); wiiu_packet_pool_release(a);
	wiiu_packet_pool_trim();
	fail_next = 1;
	CHECK(wiiu_packet_pool_allocate(4096) == NULL);
	a = wiiu_packet_pool_allocate(4096);
	CHECK(a); wiiu_packet_pool_release(a);
	fail_next = 1;
	b = wiiu_packet_pool_allocate(4096);
	CHECK(b == a); /* A warm hit does not ask the failing heap. */
	fail_next = 0;
	wiiu_packet_pool_trim(); /* Trimming never owns b. */
	memset(b, 63, 4096);
	CHECK(((unsigned char *)b)[4095] == 63);
	a = wiiu_packet_pool_allocate(8192); CHECK(a); wiiu_packet_pool_release(a);
	/* An invalid request does not evict a useful cache entry. */
	wiiu_packet_pool_stats(&stats, 0);
	{ unsigned cached = stats.cached_blocks; unsigned long calls = heap_calls;
	  CHECK(wiiu_packet_pool_allocate((size_t)-1) == NULL);
	  wiiu_packet_pool_stats(&stats, 0); CHECK(stats.cached_blocks == cached && heap_calls == calls); }
	/* A real failed miss reclaims only idle memory and retries once. */
	require_reclaim = 1; frees_required = heap_frees + 1;
	a = wiiu_packet_pool_allocate(16384); CHECK(a);
	require_reclaim = 0; CHECK(heap_frees == frees_required);
	CHECK(((unsigned char *)b)[4095] == 63);
	wiiu_packet_pool_stats(&stats, 0);
	CHECK(stats.retries == 1 && stats.reclaimed_blocks == 1 && stats.cached_blocks == 0);
	wiiu_packet_pool_release(a);
	fail_next = 2;
	CHECK(wiiu_packet_pool_allocate(32768) == NULL);
	CHECK(((unsigned char *)b)[4095] == 63);
	wiiu_packet_pool_stats(&stats, 0);
	CHECK(stats.retries == 2 && stats.reclaimed_blocks == 2 && stats.cached_blocks == 0);
	wiiu_packet_pool_release(b);
	wiiu_packet_pool_trim();
	wiiu_packet_pool_stats(&stats, 1);
	CHECK(stats.cached_blocks == 0 && stats.cached_bytes == 0 && stats.heap_failures == 4 && stats.size_rejections == 2);
	CHECK(lock_violations == 0);
	puts("pool ownership, capacity, zero/oversized requests, OOM and live trim passed");
	return 0;
}

static unsigned errors;
static void *concurrent(void *argument)
{
	const unsigned id = (unsigned)(uintptr_t)argument;
	unsigned n;

	for (n = 0; n < 5000; n++)
	{
		const unsigned size = 128u << ((n + id) % 9);
		unsigned char *a = wiiu_packet_pool_allocate(size), *b = wiiu_packet_pool_allocate(size);
		if (!a || !b || a == b)
			__atomic_add_fetch(&errors, 1, __ATOMIC_RELAXED);
		else
		{
			memset(a, (int)id, size); memset(b, (int)(id + 17), size);
			if (a[0] != id || a[size - 1] != id || b[0] != id + 17 || b[size - 1] != id + 17)
				__atomic_add_fetch(&errors, 1, __ATOMIC_RELAXED);
		}
		wiiu_packet_pool_release(a); wiiu_packet_pool_release(b);
		if (!(n % 127))
			wiiu_packet_pool_trim();
	}
	return NULL;
}

static int threads(void)
{
	pthread_t workers[4];
	struct wiiu_packet_pool_stats stats;
	unsigned i;

	for (i = 0; i < 4; i++) CHECK(!pthread_create(&workers[i], NULL, concurrent, (void *)(uintptr_t)(i + 1)));
	for (i = 0; i < 4; i++) CHECK(!pthread_join(workers[i], NULL));
	CHECK(!errors);
	CHECK(lock_violations == 0);
	wiiu_packet_pool_stats(&stats, 0);
	CHECK(stats.cached_blocks <= 128 && stats.cached_bytes <= 1024 * 1024);
	wiiu_packet_pool_trim();
	puts("40000 concurrent allocations with retained live buffers and trim passed");
	return 0;
}

static uint64_t nanos(void)
{
	struct timespec stamp;
	clock_gettime(CLOCK_MONOTONIC, &stamp);
	return (uint64_t)stamp.tv_sec * 1000000000u + (uint64_t)stamp.tv_nsec;
}

static volatile unsigned char sink;
static int benchmark(unsigned size, unsigned batch, unsigned waves, unsigned reverse)
{
	void *a[256];
	uint64_t started = 0, elapsed = 0;
	unsigned pooled, order, wave, i, counted;
	struct wiiu_packet_pool_stats stats;

	CHECK(batch && batch <= 256);
	for (order = 0; order < 2; order++)
	{
		pooled = order ^ reverse;
		wiiu_packet_pool_trim();
		/* Time ordinary libc calls, then count the same operation sequence
		separately. Atomic accounting is not charged to the baseline timing. */
		for (counted = 0; counted < 2; counted++)
		{
			track_heap = counted;
			for (i = 0; i < batch; i++) a[i] = pooled ? wiiu_packet_pool_allocate(size) : malloc(size);
			for (i = 0; i < batch; i++) { if (pooled) wiiu_packet_pool_release(a[i]); else free(a[i]); }
			heap_calls = heap_frees = heap_bytes = 0;
			wiiu_packet_pool_stats(&stats, 1);
			if (!counted) started = nanos();
			for (wave = 0; wave < waves; wave++)
			{
				for (i = 0; i < batch; i++)
				{
					a[i] = pooled ? wiiu_packet_pool_allocate(size) : counted ? counted_malloc(size) : malloc(size);
					CHECK(a[i]); ((unsigned char *)a[i])[0] = (unsigned char)i;
				}
				for (i = 0; i < batch; i++)
				{
					sink ^= ((unsigned char *)a[i])[0];
					if (pooled) wiiu_packet_pool_release(a[i]); else if (counted) counted_free(a[i]); else free(a[i]);
				}
			}
			if (!counted) elapsed = nanos() - started;
		}
		wiiu_packet_pool_stats(&stats, 0);
		printf("{\"pooled\":%u,\"size\":%u,\"batch\":%u,\"waves\":%u,\"elapsed_ns\":%llu,\"malloc_calls\":%lu,\"free_calls\":%lu,\"requested_heap_bytes\":%lu,\"cache_hits\":%u,\"cached_bytes\":%u,\"counts_from_separate_pass\":true}\n",
			pooled, size, batch, waves, (unsigned long long)elapsed, heap_calls, heap_frees, heap_bytes, stats.hits, stats.cached_bytes);
	}
	track_heap = 1;
	wiiu_packet_pool_trim();
	return 0;
}

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "late-read"))
	{
		volatile unsigned char *bytes = wiiu_packet_pool_allocate(128);
		bytes[0] = 17;
		wiiu_packet_pool_release((void *)bytes);
		return bytes[0] == 17 ? 0 : 1; /* Sanitized control: access after return. */
	}
	if (argc > 1 && !strcmp(argv[1], "threads")) return threads();
	if (argc > 1 && (!strcmp(argv[1], "bench") || !strcmp(argv[1], "bench-reverse")))
		return benchmark(argc > 2 ? (unsigned)strtoul(argv[2], NULL, 0) : 8192, argc > 3 ? (unsigned)strtoul(argv[3], NULL, 0) : 64,
			argc > 4 ? (unsigned)strtoul(argv[4], NULL, 0) : 4000, !strcmp(argv[1], "bench-reverse"));
	return basic();
}
