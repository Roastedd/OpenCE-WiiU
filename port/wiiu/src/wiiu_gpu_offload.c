/*
WIIU_GPU_OFFLOAD.C

halo/gpu_offload.txt: see wiiu_gpu_offload.h.
*/

#include "wiiu_gpu_offload.h"

#include <stdlib.h>
#include <string.h>

unsigned wiiu_gpu_offload_parse(const char *text)
{
	char *end;
	long value;

	if (!text)
		return 0;
	while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')
		text++;
	value = strtol(text, &end, 0);
	if (end == text || value <= 0)
		return 0;
	return (unsigned)value & WIIU_GPU_OFFLOAD_KNOWN;
}

#ifndef WIIU_GPU_OFFLOAD_TEST
#include "wiiu_shell.h"

#include <stdio.h>

static volatile int offload_read;
static unsigned offload_bits;
static volatile int offload_found;
/* display.wiiu_performance "smoother" (smooth_defaults.c, at each map start): the blends are on for a map the smooth settings are for, where there is no card file */
static volatile int offload_default_on;

void wiiu_gpu_offload_set_default(int on)
{
	const int was = __atomic_exchange_n(&offload_default_on, on != 0, __ATOMIC_RELAXED);

	if (was != (on != 0))
		wiiu_log("gpu offload: the cheap blends are %s for this map (display.wiiu_performance)", on ? "on" : "off");
}

unsigned wiiu_gpu_offload_bits(void)
{
	if (!__atomic_load_n(&offload_read, __ATOMIC_ACQUIRE))
	{
		char text[32] = "";
		FILE *file = fopen(WIIU_DATA_DIRECTORY "/gpu_offload.txt", "r");
		const int found = file != NULL;
		unsigned bits;

		if (file)
		{
			if (!fgets(text, sizeof(text), file))
				text[0] = 0;
			fclose(file);
		}
		bits = wiiu_gpu_offload_parse(text);
		/* (two threads may both get here the first time: they read the same file) */
		offload_bits = bits;
		offload_found = found;
		__atomic_store_n(&offload_read, 1, __ATOMIC_RELEASE);
		wiiu_log("gpu offload (halo/gpu_offload.txt %s): blends %s, census %s, blends of poses at rest %s", found ? "found" : "absent", bits & WIIU_GPU_OFFLOAD_BLENDS ? "cheap" : "as before",
			bits & WIIU_GPU_OFFLOAD_CENSUS ? "on" : "off", bits & WIIU_GPU_OFFLOAD_NO_REUSE ? "made every frame" : "kept from the frame before");
	}
	/* (a card file wins; with none, the Performance row's smoother word turns the blends on) */
	if (!offload_found && __atomic_load_n(&offload_default_on, __ATOMIC_RELAXED))
		return offload_bits | WIIU_GPU_OFFLOAD_BLENDS;
	return offload_bits;
}
#endif
