#include "bench.h"

#include <string.h>

#define RUNS 5
#define CHAIN_LENGTH 1000000u

static double best(double a, double b)
{
	return b < a ? b : a;
}

double bench_copy(bench_clock now, void *destination, const void *source, size_t size)
{
	double result = 1e30;
	int run;

	for (run = 0; run < RUNS; run++)
	{
		uint64_t started = now();

		memcpy(destination, source, size);
		/* the compiler may not see that the copy is unused */
		__asm__ volatile("" : : "r"(destination) : "memory");
		result = best(result, (double)(now() - started) / (double)size);
	}
	return result;
}

double bench_fill(bench_clock now, void *destination, size_t size)
{
	double result = 1e30;
	int run;

	for (run = 0; run < RUNS; run++)
	{
		uint64_t started = now();

		memset(destination, run, size);
		__asm__ volatile("" : : "r"(destination) : "memory");
		result = best(result, (double)(now() - started) / (double)size);
	}
	return result;
}

double bench_read(bench_clock now, const void *source, size_t size, uint32_t *sink)
{
	const volatile uint32_t *words = source;
	const size_t count = size / sizeof(uint32_t);
	double result = 1e30;
	int run;

	for (run = 0; run < RUNS; run++)
	{
		uint32_t sum = 0;
		size_t i;
		uint64_t started = now();

		for (i = 0; i < count; i++)
			sum += words[i];
		result = best(result, (double)(now() - started) / (double)size);
		*sink += sum;
	}
	return result;
}

double bench_swap32(bench_clock now, void *buffer, size_t size)
{
	uint32_t *words = buffer;
	const size_t count = size / sizeof(uint32_t);
	double result = 1e30;
	int run;

	for (run = 0; run < RUNS; run++)
	{
		size_t i;
		uint64_t started = now();

		for (i = 0; i < count; i++)
			words[i] = __builtin_bswap32(words[i]);
		__asm__ volatile("" : : "r"(words) : "memory");
		result = best(result, (double)(now() - started) / (double)size);
	}
	return result;
}

double bench_float_chain(bench_clock now, double *sink)
{
	double result = 1e30;
	int run;

	for (run = 0; run < RUNS; run++)
	{
		volatile double seed = 1.0000001;
		double x = seed;
		uint32_t i;
		uint64_t started = now();

		for (i = 0; i < CHAIN_LENGTH; i++)
		{
			x = x * 1.0000001;
			x = x + 0.0000001;
		}
		result = best(result, (double)(now() - started) / (double)(CHAIN_LENGTH * 2u));
		*sink += x;
	}
	return result;
}

double bench_integer_chain(bench_clock now, uint32_t *sink)
{
	double result = 1e30;
	int run;

	for (run = 0; run < RUNS; run++)
	{
		volatile uint32_t seed = 12345u;
		uint32_t x = seed;
		uint32_t i;
		uint64_t started = now();

		for (i = 0; i < CHAIN_LENGTH; i++)
		{
			x = x * 1664525u;
			x = x + 1013904223u;
		}
		result = best(result, (double)(now() - started) / (double)(CHAIN_LENGTH * 2u));
		*sink += x;
	}
	return result;
}
