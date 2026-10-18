#include "probe_logic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

size_t probe_compress_ranges(const uint8_t *states, size_t pages, uint32_t page_size, probe_range *out, size_t max)
{
	size_t runs = 0;
	size_t first = 0;
	size_t page;

	for (page = 1; page <= pages; page++)
	{
		if (page == pages || states[page] != states[first])
		{
			if (runs < max)
			{
				out[runs].start = (uint32_t)(first * (uint64_t)page_size);
				out[runs].end = (uint32_t)(page * (uint64_t)page_size);
				out[runs].state = states[first];
			}
			runs++;
			first = page;
		}
	}
	return pages ? runs : 0;
}

static int compare_doubles(const void *a, const void *b)
{
	double x = *(const double *)a;
	double y = *(const double *)b;

	return (x > y) - (x < y);
}

void probe_stats_compute(double *samples, size_t count, probe_stats *out)
{
	double sum = 0.0;
	double squares = 0.0;
	size_t i;

	memset(out, 0, sizeof(*out));
	if (!count)
		return;
	qsort(samples, count, sizeof(double), compare_doubles);
	for (i = 0; i < count; i++)
		sum += samples[i];
	out->mean = sum / (double)count;
	for (i = 0; i < count; i++)
		squares += (samples[i] - out->mean) * (samples[i] - out->mean);
	out->minimum = samples[0];
	out->maximum = samples[count - 1];
	out->median = count & 1 ? samples[count / 2] : 0.5 * (samples[count / 2 - 1] + samples[count / 2]);
	out->deviation = count > 1 ? __builtin_sqrt(squares / (double)(count - 1)) : 0.0;
}

void probe_format_size(uint64_t bytes, char *out, size_t size)
{
	static const char *const units[] = { "B", "KiB", "MiB", "GiB" };
	double value = (double)bytes;
	int unit = 0;

	while (value >= 1024.0 && unit < 3)
	{
		value /= 1024.0;
		unit++;
	}
	if (unit == 0 || value == (double)(uint64_t)value)
		snprintf(out, size, "%llu %s", (unsigned long long)(uint64_t)value, units[unit]);
	else
		snprintf(out, size, "%.2f %s", value, units[unit]);
}

uint32_t probe_fnv(uint32_t seed, const void *data, size_t size)
{
	const uint8_t *bytes = data;
	uint32_t hash = seed;
	size_t i;

	for (i = 0; i < size; i++)
		hash = (hash ^ bytes[i]) * 16777619u;
	return hash;
}

uint32_t probe_float_bits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

uint64_t probe_double_bits(double value)
{
	uint64_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

uint32_t probe_random_next(uint32_t *state)
{
	*state = *state * 1664525u + 1013904223u;
	return *state;
}

double probe_random_double(uint32_t *state, double low, double high)
{
	uint32_t a = probe_random_next(state) >> 5;
	uint32_t b = probe_random_next(state) >> 6;

	return low + (high - low) * (((double)a * 67108864.0 + (double)b) / 9007199254740992.0);
}
