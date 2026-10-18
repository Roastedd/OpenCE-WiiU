/*
PROBE_LOGIC.H

The probe's arithmetic, kept apart from the console so that a desktop test (tests/test_logic.c)
runs it: little-endian and big-endian, under the sanitizers.
*/

#ifndef HALO_WIIU_PROBE_LOGIC_H
#define HALO_WIIU_PROBE_LOGIC_H

#include <stddef.h>
#include <stdint.h>

/* a run of equal pages of the address space: [start, end) in bytes */
typedef struct probe_range
{
	uint32_t start;
	uint32_t end;
	uint8_t state;
} probe_range;

/* The pages' states (states[0] is the page at 0) as runs. Returns the number of runs, which
may be more than max (the first max are written). The address after the last page is page_size
times pages, which wraps to 0 for all of a 32-bit address space: end is then 0, meaning 2^32. */
size_t probe_compress_ranges(const uint8_t *states, size_t pages, uint32_t page_size, probe_range *out, size_t max);

typedef struct probe_stats
{
	double minimum;
	double median;
	double mean;
	double maximum;
	double deviation;
} probe_stats;

/* Sorts the samples in place. A count of 0 gives zeros. */
void probe_stats_compute(double *samples, size_t count, probe_stats *out);

/* "128 MiB", "1.50 GiB", "512 B": the size as the largest unit that keeps it above 1 */
void probe_format_size(uint64_t bytes, char *out, size_t size);

/* A 32-bit FNV-1a over bytes, to chain with its result as seed (probe_fnv_seed to start) */
#define PROBE_FNV_SEED 2166136261u
uint32_t probe_fnv(uint32_t seed, const void *data, size_t size);

/* The bits of a float or double as an unsigned integer (a checksum of results must not depend
on printing them) */
uint32_t probe_float_bits(float value);
uint64_t probe_double_bits(double value);

/* A linear congruential generator for inputs that are the same on every machine */
uint32_t probe_random_next(uint32_t *state);
/* a double in [low, high), from two draws */
double probe_random_double(uint32_t *state, double low, double high);

#endif
