/* GPU-only 2 x 2 mip filtering, with the byte rounding of wiiu_mip.c. */
#ifndef WIIU_GX2_MIP_H
#define WIIU_GX2_MIP_H

#include <gx2/surface.h>

/* The caller retires replaced scratch allocations as it retires its other GPU
memory. All failure checks precede any GPU context change; a successful call
changes context and the caller must restore its draw state. Filtering emits no
completion wait or CPU readback; resizing uses the caller's retirement policy.
Source/destination may be levels of the same surface. */
int wiiu_gx2_mip_derive(const GX2Surface *source, unsigned source_level,
	GX2Surface *destination, unsigned destination_level, void (*retire)(void *));

/* Asset-free native qualification, requested by gpu_test.txt 2. This test
deliberately waits and reads back its own images; it leaves context changed. */
int wiiu_gx2_mip_test(void);

#endif
