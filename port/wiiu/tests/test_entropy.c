/*
The Wii U's random bytes (port/wiiu/src/wiiu_entropy.c) on a desktop, driven by tools/test_wiiu_https.py.

    test_entropy kat
    test_entropy global DIRECTORY

"kat" prints the pool's states and outputs for a fixed sequence of calls, which the test computes again with Python's hashlib from the description in
wiiu_entropy.h. "global" runs the port's pool with the test's clocks and seed files in DIRECTORY and prints key=value results.
*/

#include "wiiu_entropy.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void hex(const char *label, const void *data, size_t size)
{
	const unsigned char *bytes = data;
	size_t i;

	printf("%s=", label);
	for (i = 0; i < size; i++)
		printf("%02x", bytes[i]);
	printf("\n");
}

/* ---------- the fake world */

static unsigned long long fake_ticks_now;
static unsigned long long tick_step = 1;
static unsigned long tick_calls, yield_calls, wall_calls;
static int card_ok = 1;
static char log_text[2048];

static uint64_t fake_ticks(void)
{
	tick_calls++;
	fake_ticks_now += tick_step;
	return fake_ticks_now;
}

static uint64_t fake_wall(void)
{
	wall_calls++;
	return 1000000007ull;
}

static void fake_yield(void)
{
	yield_calls++;
}

static uint32_t fake_thread(void)
{
	return 7;
}

static int fake_card(void)
{
	return card_ok;
}

static void fake_log(const char *text)
{
	size_t used = strlen(log_text);

	snprintf(log_text + used, sizeof(log_text) - used, "%s|", text);
}

static const wiiu_entropy_hooks hooks = { fake_ticks, fake_wall, fake_yield, fake_thread, fake_card, fake_log };

static long file_size(const char *path)
{
	FILE *file = fopen(path, "rb");
	long size;

	if (!file)
		return -1;
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fclose(file);
	return size;
}

static int read_all(const char *path, unsigned char *data, size_t capacity)
{
	FILE *file = fopen(path, "rb");
	size_t got;

	if (!file)
		return -1;
	got = fread(data, 1, capacity, file);
	fclose(file);
	return (int)got;
}

static void write_all(const char *path, const void *data, size_t size)
{
	FILE *file = fopen(path, "wb");

	fwrite(data, 1, size, file);
	fclose(file);
}

static int contains(const unsigned char *haystack, size_t size, const unsigned char *needle, size_t length)
{
	size_t i;

	for (i = 0; i + length <= size; i++)
		if (!memcmp(haystack + i, needle, length))
			return 1;
	return 0;
}

static void restart(const char *path)
{
	fake_ticks_now = 5000;
	tick_calls = yield_calls = wall_calls = 0;
	log_text[0] = 0;
	wiiu_entropy_configure(&hooks, path);
}

/* ---------- the threads */

enum { THREADS = 8, CALLS = 200, SIZE = 16 };
static unsigned char thread_output[THREADS][CALLS][SIZE];

static void *thread_main(void *argument)
{
	int index = (int)(size_t)argument, i;

	for (i = 0; i < CALLS; i++)
		wiiu_entropy_bytes(thread_output[index][i], SIZE);
	return NULL;
}

static int compare_blocks(const void *a, const void *b)
{
	return memcmp(a, b, SIZE);
}

int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "kat"))
	{
		wiiu_entropy pool;
		unsigned char output[80], big[200], seed[WIIU_ENTROPY_SEED_SIZE], tail[7];
		int i;

		wiiu_entropy_init(&pool);
		hex("init", pool.state, sizeof(pool.state));
		wiiu_entropy_mix(&pool, "abc", 3);
		hex("mixed", pool.state, sizeof(pool.state));
		wiiu_entropy_extract(&pool, output, sizeof(output));
		hex("out80", output, sizeof(output));
		hex("after_extract", pool.state, sizeof(pool.state));
		for (i = 0; i < (int)sizeof(big); i++)
			big[i] = (unsigned char)i;
		wiiu_entropy_mix(&pool, big, sizeof(big));
		wiiu_entropy_mix(&pool, NULL, 0);
		hex("after_mix200", pool.state, sizeof(pool.state));
		wiiu_entropy_seed(&pool, seed);
		hex("seed", seed, sizeof(seed));
		wiiu_entropy_extract(&pool, tail, sizeof(tail));
		hex("out7", tail, sizeof(tail));
		printf("counter=%llu\n", (unsigned long long)pool.counter);
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "global"))
	{
		char path[512], missing[512];
		unsigned char first[32], second[32], seed1[64], seed2[64], third[32], again[32];
		unsigned char *many;
		unsigned long collected_yields, collected_ticks;
		int i, seed1_size, ones = 0, repeated = 0, distinct_blocks = 0;
		long histogram[256];
		double chi = 0;
		const size_t many_size = 65536;

		snprintf(path, sizeof(path), "%s/entropy.seed", argv[2]);
		snprintf(missing, sizeof(missing), "%s/no/such/folder/entropy.seed", argv[2]);

		/* 1: no seed file: the pool starts from the clocks, writes a seed file of 64 bytes, and the seed is not an output */
		remove(path);
		restart(path);
		wiiu_entropy_bytes(first, sizeof(first));
		collected_yields = yield_calls;
		collected_ticks = tick_calls;
		seed1_size = read_all(path, seed1, sizeof(seed1));
		printf("start_seed_size=%d yields=%lu ticks=%lu\n", seed1_size, collected_yields, collected_ticks);
		printf("start_log_no_seed=%d\n", strstr(log_text, "(no seed file)") != NULL);
		printf("seed_in_first=%d\n", seed1_size == 64 && contains(first, sizeof(first), seed1, 8));
		wiiu_entropy_bytes(second, sizeof(second));
		printf("second_differs=%d\n", memcmp(first, second, sizeof(first)) != 0);
		printf("yields_after_second=%lu\n", yield_calls);

		/* 2: the next start reads that seed, says so, and writes another */
		restart(path);
		wiiu_entropy_bytes(third, sizeof(third));
		seed2[0] = 0;
		printf("restart_read_seed=%d\n", strstr(log_text, "and the seed file") != NULL);
		printf("seed_rewritten=%d\n", read_all(path, seed2, sizeof(seed2)) == 64 && memcmp(seed1, seed2, 64) != 0);
		printf("seed_not_the_output=%d\n", !contains(seed2, 64, third, 8) && !contains(seed1, 64, third, 8));
		printf("third_differs=%d\n", memcmp(first, third, sizeof(first)) != 0);

		/* 3: the same clocks and a different seed give other bytes (the seed is in the pool) */
		write_all(path, seed1, 64);
		restart(path);
		wiiu_entropy_bytes(again, sizeof(again));
		write_all(path, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcd!!", 64);
		restart(path);
		wiiu_entropy_bytes(third, sizeof(third));
		printf("seed_changes_output=%d\n", memcmp(again, third, sizeof(again)) != 0);

		/* 4: a short seed file (cut by a power loss) is no seed, and is replaced by a whole one */
		write_all(path, "short", 5);
		restart(path);
		wiiu_entropy_bytes(third, sizeof(third));
		printf("short_log_no_seed_file=%d short_replaced=%ld\n", strstr(log_text, "and the seed file") == NULL, file_size(path));

		/* 5: the card may not be used: nothing read, nothing written, and bytes all the same */
		write_all(path, seed1, 64);
		card_ok = 0;
		restart(path);
		wiiu_entropy_bytes(third, sizeof(third));
		wiiu_entropy_save_seed();
		seed2[0] = 0;
		printf("card_off_untouched=%d card_off_bytes=%d\n", read_all(path, seed2, sizeof(seed2)) == 64 && !memcmp(seed1, seed2, 64),
			memcmp(third, "\0\0\0\0\0\0\0\0", 8) != 0);
		card_ok = 1;

		/* 6: a path that cannot be written: bytes all the same, and it says so */
		restart(missing);
		wiiu_entropy_bytes(third, sizeof(third));
		printf("unwritable_logged=%d unwritable_bytes=%d\n", strstr(log_text, "could not be written") != NULL,
			memcmp(third, "\0\0\0\0\0\0\0\0", 8) != 0);

		/* 7: no seed path at all, and clocks that never move: successive bytes still differ */
		tick_step = 0;
		restart(NULL);
		wiiu_entropy_bytes(first, sizeof(first));
		wiiu_entropy_bytes(second, sizeof(second));
		printf("still_clocks_differ=%d\n", memcmp(first, second, sizeof(first)) != 0);
		tick_step = 1;

		/* 8: a save writes a new seed from the pool in use (not on the call that started it, which wrote one) */
		restart(path);
		wiiu_entropy_bytes(first, sizeof(first));
		read_all(path, seed1, 64);
		wiiu_entropy_save_seed();
		read_all(path, seed2, 64);
		printf("save_after_start_skipped=%d\n", !memcmp(seed1, seed2, 64));
		wiiu_entropy_bytes(first, sizeof(first));
		wiiu_entropy_save_seed();
		read_all(path, seed2, 64);
		printf("save_later_writes=%d\n", memcmp(seed1, seed2, 64) != 0);

		/* 9: 64 KiB of output: bits balanced, bytes spread, no block twice */
		restart(path);
		many = malloc(many_size);
		wiiu_entropy_bytes(many, many_size);
		memset(histogram, 0, sizeof(histogram));
		for (i = 0; i < (int)many_size; i++)
		{
			unsigned char byte = many[i];
			int bit;

			histogram[byte]++;
			for (bit = 0; bit < 8; bit++)
				ones += (byte >> bit) & 1;
		}
		for (i = 0; i < 256; i++)
		{
			const double expected = (double)many_size / 256.0, difference = (double)histogram[i] - expected;

			chi += difference * difference / expected;
		}
		for (i = 0; i + 32 <= (int)many_size; i += 32)
		{
			int j;

			for (j = 0; j < i; j += 32)
				if (!memcmp(many + i, many + j, 32))
					repeated++;
			distinct_blocks++;
		}
		printf("ones_percent=%.3f chi_square=%.1f blocks=%d repeated=%d\n", 100.0 * ones / (many_size * 8.0), chi, distinct_blocks, repeated);
		free(many);

		/* 10: eight threads asking at once: every answer different (the lock holds the pool together) */
		{
			pthread_t threads[THREADS];
			unsigned char (*all)[SIZE] = (unsigned char (*)[SIZE])thread_output;
			int distinct = 1;

			restart(path);
			for (i = 0; i < THREADS; i++)
				pthread_create(&threads[i], NULL, thread_main, (void *)(size_t)i);
			for (i = 0; i < THREADS; i++)
				pthread_join(threads[i], NULL);
			qsort(all, THREADS * CALLS, SIZE, compare_blocks);
			for (i = 1; i < THREADS * CALLS; i++)
				distinct += memcmp(all[i], all[i - 1], SIZE) != 0;
			printf("threads_outputs=%d distinct=%d\n", THREADS * CALLS, distinct);
		}
		return 0;
	}
	fprintf(stderr, "usage: see the top of this file\n");
	return 2;
}
