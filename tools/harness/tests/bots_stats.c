/*
BOTS_STATS.C (test)

The bots' think-time counters (bots_stats.c, compiled as it is): the mean, the longest and the percentiles of a
histogram of microseconds. test_bots_stats.py takes the code and its header from the sources.
*/

#include "harness.h"

#include "bots_stats.h"
#include "under_test.inc"

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	struct bots_think_stats stats, other;
	long index;

	bots_think_reset(&stats);
	bots_think_reset(&other);

	/* nothing counted says nothing */
	CASE("empty")
	{
		CHECK(bots_think_mean(&stats) == 0 && bots_think_percentile(&stats, 99) == 0 && stats.samples == 0,
			"an empty set has a time");
		return 0;
	}
	/* the mean rounds to the nearest microsecond and the longest is kept */
	CASE("mean-and-longest")
	{
		bots_think_note(&stats, 10);
		bots_think_note(&stats, 20);
		bots_think_note(&stats, 32);
		CHECK(stats.samples == 3 && stats.total_microseconds == 62, "%ld samples, %llu us", stats.samples,
			stats.total_microseconds);
		CHECK(bots_think_mean(&stats) == 21, "the mean of 10, 20, 32 is %ld, not 21 (20.67 rounded)", bots_think_mean(&stats));
		CHECK(stats.maximum_microseconds == 32, "the longest is %ld", stats.maximum_microseconds);
		bots_think_note(&stats, 100);
		CHECK(stats.maximum_microseconds == 100, "the longest did not follow");
		return 0;
	}
	/* a clock that stepped back is a zero, not a huge unsigned time */
	CASE("negative")
	{
		bots_think_note(&stats, -5);
		CHECK(stats.samples == 1 && stats.total_microseconds == 0 && stats.maximum_microseconds == 0 && stats.buckets[0] == 1,
			"a negative time counted as %llu", stats.total_microseconds);
		return 0;
	}
	/* 99 times 1 us and once 1000: the 99th percentile is the common time, the 100th the longest; never above the longest */
	CASE("percentile")
	{
		for (index = 0; index < 99; index++)
			bots_think_note(&stats, 1);
		bots_think_note(&stats, 1000);
		CHECK(bots_think_percentile(&stats, 99) == 1, "the 99th percentile is %ld, not 1", bots_think_percentile(&stats, 99));
		CHECK(bots_think_percentile(&stats, 100) == 1000, "the 100th is %ld, not the longest 1000",
			bots_think_percentile(&stats, 100));
		CHECK(bots_think_percentile(&stats, 50) == 1, "the median is %ld", bots_think_percentile(&stats, 50));
		bots_think_reset(&stats);
		for (index = 0; index < 100; index++)
			bots_think_note(&stats, 5);
		/* (the bucket 4 to 7 reaches 7: an over-estimate, held to the longest) */
		CHECK(bots_think_percentile(&stats, 99) == 5, "the percentile %ld went past the longest 5",
			bots_think_percentile(&stats, 99));
		/* (the nearest rank, rounded up: 9 of 10 samples are 1, so the 95th percentile is the tenth, the longest) */
		bots_think_reset(&stats);
		for (index = 0; index < 9; index++)
			bots_think_note(&stats, 1);
		bots_think_note(&stats, 1000);
		CHECK(bots_think_percentile(&stats, 90) == 1 && bots_think_percentile(&stats, 95) == 1000,
			"the 90th and 95th of 9 ones and a thousand are %ld and %ld", bots_think_percentile(&stats, 90),
			bots_think_percentile(&stats, 95));
		bots_think_reset(&stats);
		for (index = 0; index < 100; index++)
			bots_think_note(&stats, 5);
		/* (zero and out-of-range per cents) */
		CHECK(bots_think_percentile(&stats, 0) == 5 && bots_think_percentile(&stats, 250) == 5, "a per cent out of range");
		return 0;
	}
	/* a time past the last bucket is counted there, and the percentile there is the longest */
	CASE("huge")
	{
		bots_think_note(&stats, 1);
		bots_think_note(&stats, 100000000L);
		CHECK(stats.buckets[BOTS_THINK_BUCKETS - 1] == 1, "the huge time is in no last bucket");
		CHECK(bots_think_percentile(&stats, 100) == 100000000L, "%ld", bots_think_percentile(&stats, 100));
		return 0;
	}
	/* two sets as one */
	CASE("add")
	{
		bots_think_note(&stats, 4);
		bots_think_note(&stats, 8);
		bots_think_note(&other, 100);
		bots_think_add(&stats, &other);
		CHECK(stats.samples == 3 && stats.total_microseconds == 112 && stats.maximum_microseconds == 100,
			"%ld samples, %llu us, longest %ld", stats.samples, stats.total_microseconds, stats.maximum_microseconds);
		CHECK(bots_think_percentile(&stats, 100) == 100, "the sum's longest");
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
