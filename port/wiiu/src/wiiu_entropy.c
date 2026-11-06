/*
WIIU_ENTROPY.C

The Wii U's source of random bytes (wiiu_entropy.h, which says what it is and is not). Built with the C library's ABI; the same source runs on a
desktop for the tests (tools/test_wiiu_https.py, port/wiiu/tests/test_entropy.c).
*/

#include "wiiu_entropy.h"

#include "monocypher.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __WIIU__
#include "wiiu_shell.h"

#include <coreinit/core.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#else
#include <sched.h>
#include <time.h>
#endif

/* ---------- the pool */

static void store64(uint8_t *out, uint64_t value)
{
	int i;

	for (i = 0; i < 8; i++)
		out[i] = (uint8_t)(value >> (8 * i));
}

static void ratchet(wiiu_entropy *pool)
{
	uint8_t message[15];
	uint8_t next[WIIU_ENTROPY_STATE_SIZE];

	memcpy(message, "ratchet", 7);
	store64(message + 7, pool->counter);
	crypto_blake2b_keyed(next, sizeof(next), pool->state, sizeof(pool->state), message, sizeof(message));
	memcpy(pool->state, next, sizeof(next));
	pool->counter++;
	crypto_wipe(next, sizeof(next));
}

void wiiu_entropy_init(wiiu_entropy *pool)
{
	static const char text[] = "OpenCE Wii U entropy pool 1";

	crypto_blake2b(pool->state, sizeof(pool->state), (const uint8_t *)text, sizeof(text) - 1);
	pool->counter = 0;
}

void wiiu_entropy_mix(wiiu_entropy *pool, const void *data, size_t size)
{
	crypto_blake2b_ctx context;
	uint8_t number[8];
	uint8_t next[WIIU_ENTROPY_STATE_SIZE];

	crypto_blake2b_init(&context, sizeof(next));
	crypto_blake2b_update(&context, (const uint8_t *)"mix", 3);
	crypto_blake2b_update(&context, pool->state, sizeof(pool->state));
	store64(number, pool->counter);
	crypto_blake2b_update(&context, number, sizeof(number));
	store64(number, (uint64_t)size);
	crypto_blake2b_update(&context, number, sizeof(number));
	if (size)
		crypto_blake2b_update(&context, (const uint8_t *)data, size);
	crypto_blake2b_final(&context, next);
	memcpy(pool->state, next, sizeof(next));
	pool->counter++;
	crypto_wipe(next, sizeof(next));
}

void wiiu_entropy_extract(wiiu_entropy *pool, void *output, size_t size)
{
	uint8_t *bytes = (uint8_t *)output;
	uint32_t block = 0;

	while (size)
	{
		uint8_t message[15];
		uint8_t hash[32];
		const size_t piece = size < sizeof(hash) ? size : sizeof(hash);

		memcpy(message, "out", 3);
		store64(message + 3, pool->counter);
		message[11] = (uint8_t)block;
		message[12] = (uint8_t)(block >> 8);
		message[13] = (uint8_t)(block >> 16);
		message[14] = (uint8_t)(block >> 24);
		crypto_blake2b_keyed(hash, sizeof(hash), pool->state, sizeof(pool->state), message, sizeof(message));
		memcpy(bytes, hash, piece);
		crypto_wipe(hash, sizeof(hash));
		bytes += piece;
		size -= piece;
		block++;
	}
	ratchet(pool);
}

void wiiu_entropy_seed(wiiu_entropy *pool, uint8_t seed[WIIU_ENTROPY_SEED_SIZE])
{
	uint8_t message[12];

	memcpy(message, "seed", 4);
	store64(message + 4, pool->counter);
	crypto_blake2b_keyed(seed, WIIU_ENTROPY_SEED_SIZE, pool->state, sizeof(pool->state), message, sizeof(message));
	ratchet(pool);
}

/* ---------- gathering */

void wiiu_entropy_collect(wiiu_entropy *pool, const wiiu_entropy_hooks *hooks)
{
	uint64_t samples[2 * WIIU_ENTROPY_ROUNDS + 8];
	unsigned count = 0, i;
	int stack_variable = 0;
	void *heap_block = malloc(48);
	uint64_t address;

	memset(samples, 0, sizeof(samples));
	for (i = 0; i < WIIU_ENTROPY_ROUNDS; i++)
	{
		/* (the time either side of a yield: how long the other threads and the scheduler kept the processor is not the same twice) */
		samples[count++] = hooks->ticks ? hooks->ticks() : 0;
		if (hooks->yield)
			hooks->yield();
		samples[count++] = hooks->ticks ? hooks->ticks() : 0;
	}
	samples[count++] = hooks->wall ? hooks->wall() : 0;
	samples[count++] = hooks->thread ? hooks->thread() : 0;
	address = (uint64_t)(uintptr_t)&stack_variable;
	samples[count++] = address;
	address = (uint64_t)(uintptr_t)heap_block;
	samples[count++] = address;
	address = (uint64_t)(uintptr_t)pool;
	samples[count++] = address;
	samples[count++] = hooks->ticks ? hooks->ticks() : 0;
	wiiu_entropy_mix(pool, samples, count * sizeof(samples[0]));
	free(heap_block);
}

/* ---------- the port's pool */

#ifdef __WIIU__
static uint64_t default_ticks(void)
{
	return (uint64_t)OSGetSystemTime();
}

static uint64_t default_wall(void)
{
	return (uint64_t)OSGetTime();
}

static void default_yield(void)
{
	OSYieldThread();
}

static uint32_t default_thread(void)
{
	return (uint32_t)(uintptr_t)OSGetCurrentThread() ^ (OSGetCoreId() << 28) ^ ((uint32_t)OSGetTick() << 3);
}

static int default_card(void)
{
	return wiiu_in_foreground();
}

static void default_log(const char *text)
{
	wiiu_log_text(text);
}

#define DEFAULT_SEED_PATH WIIU_DATA_DIRECTORY "/entropy.seed"
#define DEFAULT_CARD default_card
#define DEFAULT_LOG default_log
#else
static uint64_t default_ticks(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static uint64_t default_wall(void)
{
	struct timespec now;

	clock_gettime(CLOCK_REALTIME, &now);
	return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static void default_yield(void)
{
	sched_yield();
}

static uint32_t default_thread(void)
{
	return (uint32_t)(uintptr_t)pthread_self();
}

#define DEFAULT_SEED_PATH NULL
#define DEFAULT_CARD NULL
#define DEFAULT_LOG NULL
#endif

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static wiiu_entropy pool;
static int pool_ready;
/* the pool was started since the last save, which wrote its seed file */
static int just_started;
static int hooks_set;
static wiiu_entropy_hooks hooks;
static const char *seed_path = DEFAULT_SEED_PATH;
static char seed_path_copy[512];

static const wiiu_entropy_hooks *current_hooks(void)
{
	if (!hooks_set)
	{
		hooks.ticks = default_ticks;
		hooks.wall = default_wall;
		hooks.yield = default_yield;
		hooks.thread = default_thread;
		hooks.card_may_be_used = DEFAULT_CARD;
		hooks.log = DEFAULT_LOG;
		hooks_set = 1;
	}
	return &hooks;
}

static void say(const char *text)
{
	if (current_hooks()->log)
		current_hooks()->log(text);
}

/* under the lock: a new seed derived from the pool, written to the card (the file is the whole 64 bytes or, cut off by a power loss, short, which is read as no seed) */
static void write_seed(void)
{
	uint8_t seed[WIIU_ENTROPY_SEED_SIZE];
	FILE *file;
	int written;

	if (!seed_path || (current_hooks()->card_may_be_used && !current_hooks()->card_may_be_used()))
		return;
	wiiu_entropy_seed(&pool, seed);
	file = fopen(seed_path, "wb");
	written = file && fwrite(seed, 1, sizeof(seed), file) == sizeof(seed);
	if (file && fclose(file) != 0)
		written = 0;
	crypto_wipe(seed, sizeof(seed));
	if (!written)
		say("entropy: the seed file could not be written");
}

/* under the lock */
static void start_pool(void)
{
	const wiiu_entropy_hooks *h = current_hooks();
	uint8_t seed[WIIU_ENTROPY_SEED_SIZE];
	size_t got = 0;
	char line[96];

	memset(seed, 0, sizeof(seed));
	wiiu_entropy_init(&pool);
	if (seed_path && (!h->card_may_be_used || h->card_may_be_used()))
	{
		FILE *file = fopen(seed_path, "rb");

		if (file)
		{
			got = fread(seed, 1, sizeof(seed), file);
			fclose(file);
		}
		/* (what was read, and how much: a short file is mixed as it is and counted as no seed) */
		wiiu_entropy_mix(&pool, seed, got);
		crypto_wipe(seed, sizeof(seed));
	}
	wiiu_entropy_collect(&pool, h);
	snprintf(line, sizeof(line), "entropy: pool started from the clocks%s", got == WIIU_ENTROPY_SEED_SIZE ? " and the seed file" : (seed_path ? " (no seed file)" : ""));
	say(line);
	pool_ready = 1;
	write_seed();
	just_started = 1;
}

void wiiu_entropy_bytes(void *output, size_t size)
{
	pthread_mutex_lock(&lock);
	if (!pool_ready)
		start_pool();
	else
	{
		/* (one read of the clocks for each use: the same pool, asked twice in a moment, is asked at different times) */
		uint64_t now[2];

		now[0] = current_hooks()->ticks ? current_hooks()->ticks() : 0;
		now[1] = current_hooks()->wall ? current_hooks()->wall() : 0;
		wiiu_entropy_mix(&pool, now, sizeof(now));
	}
	wiiu_entropy_extract(&pool, output, size);
	pthread_mutex_unlock(&lock);
}

void wiiu_entropy_save_seed(void)
{
	pthread_mutex_lock(&lock);
	if (!pool_ready)
		start_pool();
	else if (!just_started)
		write_seed();
	just_started = 0;
	pthread_mutex_unlock(&lock);
}

void wiiu_entropy_configure(const wiiu_entropy_hooks *new_hooks, const char *path)
{
	pthread_mutex_lock(&lock);
	if (new_hooks)
	{
		hooks = *new_hooks;
		hooks_set = 1;
	}
	if (path)
	{
		snprintf(seed_path_copy, sizeof(seed_path_copy), "%s", path);
		seed_path = seed_path_copy;
	}
	else
		seed_path = NULL;
	pool_ready = 0;
	just_started = 0;
	pthread_mutex_unlock(&lock);
}
