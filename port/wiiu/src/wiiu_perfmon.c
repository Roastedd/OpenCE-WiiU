/*
WIIU_PERFMON.C

The Espresso's performance counters, for the study of the tick thread (port/linux/game/tick_perf.h): four counters a core, 32 bits each, counting what the core does whichever thread
does it (a thread that is not running does not count, a thread of another kind on the same core does). Cafe OS lets an app set them (OSSetPerformanceMonitor, a call to the kernel,
which the registers' own instructions would not be) and read them (mfupmc1 to mfupmc4, the user copies of the registers).

halo/perf_counters.txt holds the set of events (absent or 0: nothing is touched):
  1  cycles, instructions completed, L2 data misses, L2 castouts (the lines the L2 writes back)
  2  L2 hits, L1 lines cast out to the L2, cycles a load waited for the L1 (a miss), instructions completed
  3  cycles, loads and stores, L1 data cache misses, stores on the bus
Each thread that wants numbers calls wiiu_perfmon_start on its core once (the registers are the core's own: a thread that may move to another core would read another's). The events are
the names wut's header gives the encodings; if a count is 0 or equal on every read, the encoding is not that event on this CPU, and the log says so in numbers.
*/

#include <coreinit/core.h>

#include <stdint.h>

/* wut's header (coreinit/performancemonitor.h) has the call and the events' encodings, and also inline functions that read the counters with the mnemonics mfupmc1 to mfupmc4, which the compiler
for -mcpu=750 does not know: the call is declared here, the encodings copied from it (the arguments, the events of MMCR0's counters 1 and 2, and of MMCR1's 3 and 4) and the user registers
read as mfspr 937, 938, 941 and 942 (UPMC1 to UPMC4) */
void OSSetPerformanceMonitor(uint32_t arg_mask, uint32_t mmcr0, uint32_t mmcr1, uint32_t pmc1, uint32_t pmc2, uint32_t pmc3, uint32_t pmc4);

#define PM_ARG_MMCR0 (1u << 0)
#define PM_ARG_MMCR1 (1u << 1)
#define PM_ARG_PMC1 (1u << 2)
#define PM_ARG_PMC2 (1u << 3)
#define PM_ARG_PMC3 (1u << 4)
#define PM_ARG_PMC4 (1u << 5)

#define PM_PMC1_CPU_CYCLES (1u << 6)
#define PM_PMC1_L2_HITS (7u << 6)
#define PM_PMC2_INSTRUCTIONS_COMPLETED 2u
#define PM_PMC2_LOADS_AND_STORES 11u
#define PM_PMC2_L1_TO_L2_CASTOUTS 13u
#define PM_PMC3_L1_DCACHE_MISSES (5u << 27)
#define PM_PMC3_L2_DATA_MISSES (7u << 27)
#define PM_PMC3_L1_LOAD_MISS_CYCLES (15u << 27)
#define PM_PMC4_INSTRUCTIONS_COMPLETED (2u << 22)
#define PM_PMC4_L2_CASTOUTS (5u << 22)
#define PM_PMC4_BIU_STORE_REQUESTS (21u << 22)

long wiiu_study_number(const char *name, long fallback);
void wiiu_log(const char *format, ...);

static int event_set = -1;
static volatile int started[3];

static const char *const names[4][4] =
{
	{ "", "", "", "" },
	{ "cycles", "instructions", "L2 data misses", "L2 castouts" },
	{ "L2 hits", "L1 castouts to the L2", "L1 load miss cycles", "instructions" },
	{ "cycles", "loads and stores", "L1 data misses", "bus stores" },
};

/* the set of events (0: off), from the card, read once */
int wiiu_perfmon_set(void)
{
	if (event_set < 0)
	{
		const long number = wiiu_study_number("perf_counters.txt", 0);

		event_set = number >= 1 && number <= 3 ? (int)number : 0;
		if (event_set)
			wiiu_log("Performance counters: event set %d (halo/perf_counters.txt)", event_set);
	}
	return event_set;
}

const char *wiiu_perfmon_name(int counter)
{
	return counter >= 0 && counter < 4 ? names[wiiu_perfmon_set()][counter] : "";
}

unsigned wiiu_perfmon_core(void)
{
	return (unsigned)OSGetCoreId();
}

/* sets the calling thread's core to count the set's events (once for each core) */
void wiiu_perfmon_start(void)
{
	const unsigned core = (unsigned)OSGetCoreId();
	uint32_t mmcr0 = 0, mmcr1 = 0;

	if (!wiiu_perfmon_set() || core > 2 || started[core])
		return;
	started[core] = 1;
	switch (event_set)
	{
	case 1:
		mmcr0 = PM_PMC1_CPU_CYCLES | PM_PMC2_INSTRUCTIONS_COMPLETED;
		mmcr1 = PM_PMC3_L2_DATA_MISSES | PM_PMC4_L2_CASTOUTS;
		break;
	case 2:
		mmcr0 = PM_PMC1_L2_HITS | PM_PMC2_L1_TO_L2_CASTOUTS;
		mmcr1 = PM_PMC3_L1_LOAD_MISS_CYCLES | PM_PMC4_INSTRUCTIONS_COMPLETED;
		break;
	default:
		mmcr0 = PM_PMC1_CPU_CYCLES | PM_PMC2_LOADS_AND_STORES;
		mmcr1 = PM_PMC3_L1_DCACHE_MISSES | PM_PMC4_BIU_STORE_REQUESTS;
		break;
	}
	OSSetPerformanceMonitor(PM_ARG_MMCR0 | PM_ARG_MMCR1 | PM_ARG_PMC1 | PM_ARG_PMC2 | PM_ARG_PMC3 | PM_ARG_PMC4, mmcr0, mmcr1, 0, 0, 0, 0);
	wiiu_log("Performance counters: core %u counts %s, %s, %s, %s", core, names[event_set][0], names[event_set][1], names[event_set][2], names[event_set][3]);
}

void wiiu_perfmon_read(unsigned long values[4])
{
	uint32_t pmc1, pmc2, pmc3, pmc4;

	__asm__ volatile("mfspr %0, 937" : "=r"(pmc1));
	__asm__ volatile("mfspr %0, 938" : "=r"(pmc2));
	__asm__ volatile("mfspr %0, 941" : "=r"(pmc3));
	__asm__ volatile("mfspr %0, 942" : "=r"(pmc4));
	values[0] = pmc1;
	values[1] = pmc2;
	values[2] = pmc3;
	values[3] = pmc4;
}
