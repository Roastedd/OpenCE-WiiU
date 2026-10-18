/*
The probe's arithmetic (src/probe_logic.c) on known answers. Built for the desktop, for 32-bit
big-endian PowerPC (run under qemu-ppc) and with the sanitizers: tools/wiiu/probe_tests.sh.
Exit status 0 when everything holds; a failing check says which and exits 1.
*/

#include "../src/probe_logic.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("check failed, line %d: %s\n", __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

static void test_ranges(void)
{
	uint8_t states[8] = { 0, 0, 2, 2, 2, 0, 3, 3 };
	probe_range ranges[8];
	size_t count = probe_compress_ranges(states, 8, 0x20000, ranges, 8);

	CHECK(count == 4);
	CHECK(ranges[0].start == 0 && ranges[0].end == 0x40000 && ranges[0].state == 0);
	CHECK(ranges[1].start == 0x40000 && ranges[1].end == 0xA0000 && ranges[1].state == 2);
	CHECK(ranges[2].start == 0xA0000 && ranges[2].end == 0xC0000 && ranges[2].state == 0);
	CHECK(ranges[3].start == 0xC0000 && ranges[3].end == 0x100000 && ranges[3].state == 3);

	/* more runs than room: the count is still the whole, the first are written */
	count = probe_compress_ranges(states, 8, 0x20000, ranges, 2);
	CHECK(count == 4 && ranges[1].state == 2);

	/* no pages, one page, a space that wraps (4 GiB in 128 KiB pages: end is 0) */
	CHECK(probe_compress_ranges(states, 0, 0x20000, ranges, 8) == 0);
	CHECK(probe_compress_ranges(states, 1, 0x20000, ranges, 8) == 1 && ranges[0].end == 0x20000);
	{
		static uint8_t all[32768];

		memset(all, 1, sizeof(all));
		CHECK(probe_compress_ranges(all, 32768, 0x20000, ranges, 8) == 1);
		CHECK(ranges[0].start == 0 && ranges[0].end == 0);
	}
}

static void test_stats(void)
{
	double samples[5] = { 5.0, 1.0, 3.0, 2.0, 4.0 };
	probe_stats stats;

	probe_stats_compute(samples, 5, &stats);
	CHECK(stats.minimum == 1.0 && stats.maximum == 5.0 && stats.median == 3.0 && stats.mean == 3.0);
	CHECK(fabs(stats.deviation - sqrt(2.5)) < 1e-12);

	{
		double even[4] = { 4.0, 1.0, 3.0, 2.0 };

		probe_stats_compute(even, 4, &stats);
		CHECK(stats.median == 2.5);
	}
	probe_stats_compute(samples, 0, &stats);
	CHECK(stats.minimum == 0.0 && stats.maximum == 0.0 && stats.deviation == 0.0);
	{
		double one[1] = { 7.0 };

		probe_stats_compute(one, 1, &stats);
		CHECK(stats.median == 7.0 && stats.deviation == 0.0);
	}
}

static void test_size(void)
{
	char text[32];

	probe_format_size(0, text, sizeof(text));
	CHECK(!strcmp(text, "0 B"));
	probe_format_size(512, text, sizeof(text));
	CHECK(!strcmp(text, "512 B"));
	probe_format_size(128 * 1024 * 1024, text, sizeof(text));
	CHECK(!strcmp(text, "128 MiB"));
	probe_format_size(1610612736ull, text, sizeof(text));
	CHECK(!strcmp(text, "1.50 GiB"));
	probe_format_size(4294967296ull, text, sizeof(text));
	CHECK(!strcmp(text, "4 GiB"));
	probe_format_size(1023, text, sizeof(text));
	CHECK(!strcmp(text, "1023 B"));
	probe_format_size(128 * 1024 * 1024, text, 4);
	CHECK(strlen(text) == 3);
}

static void test_hash_and_bits(void)
{
	uint32_t state = 1;

	/* FNV-1a 32-bit of "a" and "foobar" are published values */
	CHECK(probe_fnv(PROBE_FNV_SEED, "a", 1) == 0xE40C292Cu);
	CHECK(probe_fnv(PROBE_FNV_SEED, "foobar", 6) == 0xBF9CF968u);
	CHECK(probe_fnv(PROBE_FNV_SEED, "", 0) == PROBE_FNV_SEED);
	CHECK(probe_float_bits(1.0f) == 0x3F800000u);
	CHECK(probe_float_bits(-0.0f) == 0x80000000u);
	CHECK(probe_double_bits(1.0) == 0x3FF0000000000000ull);
	/* the generator is the same on every machine: its first values are fixed */
	CHECK(probe_random_next(&state) == 1015568748u);
	CHECK(probe_random_next(&state) == 1586005467u);
	{
		uint32_t seed = 12345;
		int i;

		for (i = 0; i < 1000; i++)
		{
			double value = probe_random_double(&seed, -2.0, 6.0);

			CHECK(value >= -2.0 && value < 6.0);
		}
	}
}

int main(void)
{
	test_ranges();
	test_stats();
	test_size();
	test_hash_and_bits();
	if (failures)
	{
		printf("%d checks failed\n", failures);
		return 1;
	}
	printf("probe logic: all checks hold\n");
	return 0;
}
