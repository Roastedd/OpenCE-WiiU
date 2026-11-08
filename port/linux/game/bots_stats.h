/* bots_stats.h: what the bots cost and do, counted (port/linux/game/bots_stats.c)

A bot's think time in a tick is noted in microseconds into a small histogram (a bucket for each power of
two), so that a long run says the mean, the longest and the 99th percentile without keeping every tick.
The counters are statistics for the log and the tests: nothing in the game depends on them, and no number
here comes from the game's state, so two runs of the same input count the same things (the times aside). */
#ifndef HALO_BOTS_STATS_H
#define HALO_BOTS_STATS_H

/* bucket k holds the times of 2^(k-1) to 2^k - 1 microseconds (bucket 0: none), the last one all longer */
#define BOTS_THINK_BUCKETS 24

struct bots_think_stats
{
	/* the ticks it was thought for */
	long samples;
	unsigned long long total_microseconds;
	long maximum_microseconds;
	long buckets[BOTS_THINK_BUCKETS];
};

void bots_think_reset(struct bots_think_stats *stats);
/* one tick's think time (a negative one, from a clock that stepped back, counts as none) */
void bots_think_note(struct bots_think_stats *stats, long microseconds);
/* the mean, in microseconds (0 with no sample) */
long bots_think_mean(struct bots_think_stats const *stats);
/* the time that per_cent of the samples were within, in microseconds: the top of the bucket they reach, no more than the
longest (so an over-estimate of up to twice, never above the maximum); 0 with no sample */
long bots_think_percentile(struct bots_think_stats const *stats, int per_cent);
/* the stats of two sets of samples as one */
void bots_think_add(struct bots_think_stats *total, struct bots_think_stats const *other);

#endif
