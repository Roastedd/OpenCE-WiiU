#ifndef HALO_WIIU_PROBE_CLOCK_H
#define HALO_WIIU_PROBE_CLOCK_H

#include <stdint.h>

/* the console's timer in nanoseconds (its resolution is 16 ns: the timer runs at a quarter of the bus clock) */
uint64_t clock_now_ns(void);

#endif
