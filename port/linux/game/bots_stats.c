/*
BOTS_STATS.C

The bots' think times, counted (bots_stats.h). Pure arithmetic: the harness tests compile this file as it is.
*/

#include "bots_stats.h"

static int bots_think_bucket(long microseconds)
{
	int bucket = 0;

	while (microseconds > 0 && bucket < BOTS_THINK_BUCKETS - 1)
	{
		bucket++;
		microseconds >>= 1;
	}
	return bucket;
}

void bots_think_reset(struct bots_think_stats *stats)
{
	int index;

	stats->samples = 0;
	stats->total_microseconds = 0;
	stats->maximum_microseconds = 0;
	for (index = 0; index < BOTS_THINK_BUCKETS; index++)
		stats->buckets[index] = 0;
}

void bots_think_note(struct bots_think_stats *stats, long microseconds)
{
	if (microseconds < 0)
		microseconds = 0;
	stats->samples++;
	stats->total_microseconds += (unsigned long long)microseconds;
	if (microseconds > stats->maximum_microseconds)
		stats->maximum_microseconds = microseconds;
	stats->buckets[bots_think_bucket(microseconds)]++;
}

long bots_think_mean(struct bots_think_stats const *stats)
{
	if (stats->samples <= 0)
		return 0;
	return (long)((stats->total_microseconds + (unsigned long long)stats->samples / 2) / (unsigned long long)stats->samples);
}

long bots_think_percentile(struct bots_think_stats const *stats, int per_cent)
{
	long needed, seen = 0;
	int bucket;

	if (stats->samples <= 0)
		return 0;
	if (per_cent < 0)
		per_cent = 0;
	if (per_cent > 100)
		per_cent = 100;
	/* (the sample of that rank: at least the first, rounded up) */
	needed = (stats->samples * per_cent + 99) / 100;
	if (needed < 1)
		needed = 1;
	for (bucket = 0; bucket < BOTS_THINK_BUCKETS; bucket++)
	{
		seen += stats->buckets[bucket];
		if (seen >= needed)
		{
			long top = bucket == 0 ? 0 : (bucket >= BOTS_THINK_BUCKETS - 1 ? stats->maximum_microseconds : (1L << bucket) - 1);

			return top < stats->maximum_microseconds ? top : stats->maximum_microseconds;
		}
	}
	return stats->maximum_microseconds;
}

void bots_think_add(struct bots_think_stats *total, struct bots_think_stats const *other)
{
	int index;

	total->samples += other->samples;
	total->total_microseconds += other->total_microseconds;
	if (other->maximum_microseconds > total->maximum_microseconds)
		total->maximum_microseconds = other->maximum_microseconds;
	for (index = 0; index < BOTS_THINK_BUCKETS; index++)
		total->buckets[index] += other->buckets[index];
}
