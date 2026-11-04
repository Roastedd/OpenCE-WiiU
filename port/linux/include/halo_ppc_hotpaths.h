/*
HALO_PPC_HOTPATHS.H

Memory hints for the game's tick on the Wii U's Espresso (and on a PowerPC host that is asked to run the same code): a data-cache touch for
the lines the next object or actor will read. A touch changes no value, no flag and no order of any calculation: the simulation is the same
bytes with or without it, which tools/ppc-be/compare_tick_hash.py checks on the host (HALO_PPC_HOTPATHS=1 in the environment of configure.py
builds the host with them; a Wii U build always has them) and the Wii U build's own tick part timers show the cost of.

Written for this port from its own structures; the shapes (look one active object ahead and ask for its lines while the current one runs) are
the usual ones for a CPU without a hardware prefetcher: the Espresso has dcbt/dcbtst and no stream prefetch.
*/

#ifndef HALO_PPC_HOTPATHS_H
#define HALO_PPC_HOTPATHS_H

#if defined(HALO_WIIU) || defined(HALO_PPC_HOTPATHS)

#define HALO_PPC_HOTPATHS_ON 1

/* the cache lines of a 32-byte-line CPU that [address, address + bytes) touches, asked for to be read (halo_touch) or to be written
(halo_touch_for_write); an address that is not mapped is ignored by the hardware (dcbt and dcbtst never fault), and a null pointer is not asked for */
#define HALO_CACHE_LINE 32

/* (dcbt and dcbtst by name: the compiler's own prefetch is dropped for a generic 32-bit PowerPC target, which the host loop is built for) */
#if defined(__powerpc__) || defined(__PPC__)
#define HALO_TOUCH_LINE(line) __asm__ volatile("dcbt 0,%0" : : "r"(line))
#define HALO_TOUCH_LINE_FOR_WRITE(line) __asm__ volatile("dcbtst 0,%0" : : "r"(line))
#else
#define HALO_TOUCH_LINE(line) __builtin_prefetch(line, 0, 3)
#define HALO_TOUCH_LINE_FOR_WRITE(line) __builtin_prefetch(line, 1, 3)
#endif

static __inline void halo_touch(void const *address, unsigned long bytes)
{
	char const *line = (char const *)((unsigned long)address & ~(unsigned long)(HALO_CACHE_LINE - 1));
	char const *end = (char const *)address + bytes;

	if (!address)
		return;
	for (; line < end; line += HALO_CACHE_LINE)
		HALO_TOUCH_LINE(line);
}

static __inline void halo_touch_for_write(void const *address, unsigned long bytes)
{
	char const *line = (char const *)((unsigned long)address & ~(unsigned long)(HALO_CACHE_LINE - 1));
	char const *end = (char const *)address + bytes;

	if (!address)
		return;
	for (; line < end; line += HALO_CACHE_LINE)
		HALO_TOUCH_LINE_FOR_WRITE(line);
}

#endif

#endif
