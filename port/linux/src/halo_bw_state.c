/*
HALO_BW_STATE.C

The Broadway kernels' switch and counters (port/linux/include/halo_bw_kernels.h). On the Wii U the switch is turned on by the
self-test (port/wiiu/src/wiiu_bw_kernels.c); in the host test builds (HALO_BW_MODEL), where the kernels are scalar C models, it is on
from the start, unless the environment says HALO_BW_KERNELS=0, so that one binary runs both ways for the tick-by-tick comparison.
Elsewhere this file is empty.
*/

#ifdef HALO_BW_KERNELS

#include "halo_bw_kernels.h"

#ifdef HALO_BW_MODEL
#include <stdlib.h>
#endif

unsigned halo_bw_flags;
unsigned long halo_bw_calls[HALO_BW_K_COUNT];
unsigned long halo_bw_used[HALO_BW_K_COUNT];
unsigned long halo_bw_walked[HALO_BW_K_COUNT];

#ifdef HALO_BW_MODEL
__attribute__((constructor)) static void halo_bw_model_start(void)
{
	const char *setting = getenv("HALO_BW_KERNELS");

	halo_bw_flags = setting && setting[0] == '0' ? 0u : HALO_BW_BIT(HALO_BW_K_COUNT) - 1u;
}
#endif

#else

typedef int halo_bw_state_is_empty_without_the_kernels;

#endif
