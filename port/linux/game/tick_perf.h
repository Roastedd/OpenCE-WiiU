/*
TICK_PERF.H

The performance counters of the core a thread runs on, for the study of why the tick thread is slower on core 0 than the game thread on core 1 in a heavy scene (the tools repository's
reports/research/2026-11-01/ticks-stage5-snapshot-cost.md): a count between two reads is what the core did meanwhile, whichever thread did it. For the Wii U only (HALO_WIIU); halo/perf_counters.txt
(port/wiiu/src/wiiu_perfmon.c has the sets of events) turns them on, and with it absent every call here is a load and a branch.
*/

#ifndef __TICK_PERF_H
#define __TICK_PERF_H

#ifdef HALO_WIIU

/* port/wiiu/src/wiiu_perfmon.c (the digest harness and the hand-off test have stand-ins that say off) */
int wiiu_perfmon_set(void);
void wiiu_perfmon_start(void);
void wiiu_perfmon_read(unsigned long values[4]);
char const *wiiu_perfmon_name(int counter);
unsigned wiiu_perfmon_core(void);

/* sums of the four counters over some stretches (64 bits: the registers are 32 and wrap in 3.4 s of cycles) */
struct tick_perf
{
	unsigned long long sum[4];
	unsigned long stretches;
};

static __inline int tick_perf_on(void)
{
	return wiiu_perfmon_set() != 0;
}

/* the counters now, into before; the counters of the stretch since are added by tick_perf_end */
static __inline void tick_perf_begin(unsigned long before[4])
{
	if (wiiu_perfmon_set())
	{
		wiiu_perfmon_start();
		wiiu_perfmon_read(before);
	}
}

static __inline void tick_perf_end(struct tick_perf *perf, unsigned long const before[4])
{
	if (wiiu_perfmon_set())
	{
		unsigned long after[4];
		int counter;

		wiiu_perfmon_read(after);
		for (counter = 0; counter < 4; counter++)
			perf->sum[counter] += (unsigned long)(after[counter] - before[counter]); /* (modular: right while a stretch is under 3 s) */
		perf->stretches++;
	}
}

#endif /* HALO_WIIU */

#endif /* __TICK_PERF_H */
