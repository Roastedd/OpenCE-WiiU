#include "wiiu_indices.h"

/* The Espresso has no conditional move and weak branch prediction, and a list of indices makes a compare-and-branch for the lowest and the highest a coin toss whenever the list is
not sorted: a branch-free minimum and maximum (a difference's sign as a mask) three times faster than the compares. Both operands are below 2^17, so the difference does not overflow. */
static inline uint32_t minimum_of(uint32_t x, uint32_t y)
{
	int32_t d = (int32_t)x - (int32_t)y;

	__asm__("" : "+r"(d)); /* (so that the compiler does not see a minimum and make a compare and a branch of it again) */
	return (uint32_t)((int32_t)y + (d & (d >> 31)));
}

static inline uint32_t maximum_of(uint32_t x, uint32_t y)
{
	int32_t d = (int32_t)x - (int32_t)y;

	__asm__("" : "+r"(d));
	return (uint32_t)((int32_t)x - (d & (d >> 31)));
}

/* makes the cache line at address without reading it (the whole line is about to be written): the memory a write would first read. Only for memory the application owns, where lines are
cached (0x10000000 to 0x50000000), as in wiiu_memcpy.c */
static inline int line_makeable(const void *address)
{
#if defined(__powerpc__)
	return (uintptr_t)address >= 0x10000000u && (uintptr_t)address < 0x50000000u;
#elif defined(WIIU_INDICES_TEST_LINES)
	(void)address;
	return 1; /* (the host's test goes through the line loop too, which makes nothing) */
#else
	(void)address;
	return 0;
#endif
}

static inline void line_make(void *address)
{
#if defined(__powerpc__)
	__asm__ volatile("dcbz 0,%0" : : "r"(address) : "memory");
#else
	(void)address;
#endif
}

/* asks for the line at address (the Espresso does not prefetch: it waits for each line it reads the first time) */
static inline void touch(const void *address)
{
#if defined(__powerpc__)
	__asm__ volatile("dcbt 0,%0" : : "r"(address));
#else
	__builtin_prefetch(address);
#endif
}

void wiiu_index_range(const uint16_t *indices, size_t count, uint32_t *minimum, uint32_t *maximum)
{
	uint32_t lo = 0xffff, hi = 0;
	size_t pairs;

	/* (the word loads are aligned: an odd first index is taken alone) */
	if (count && ((uintptr_t)indices & 2))
	{
		lo = hi = indices[0];
		indices++;
		count--;
	}
	/* a word holds two indices, and which is which does not matter to a minimum and a maximum; four words a pass, a line */
	for (pairs = count / 2; pairs >= 4; pairs -= 4, indices += 8)
	{
		const uint32_t *words = (const uint32_t *)(const void *)indices;
		unsigned i;

		touch(words + 128);
		for (i = 0; i < 4; i++)
		{
			const uint32_t a = words[i] >> 16, b = words[i] & 0xffff;

			lo = minimum_of(lo, minimum_of(a, b));
			hi = maximum_of(hi, maximum_of(a, b));
		}
	}
	for (; pairs; pairs--, indices += 2)
	{
		lo = minimum_of(lo, minimum_of(indices[0], indices[1]));
		hi = maximum_of(hi, maximum_of(indices[0], indices[1]));
	}
	if (count & 1)
	{
		lo = minimum_of(lo, indices[0]);
		hi = maximum_of(hi, indices[0]);
	}
	*minimum = lo;
	*maximum = hi;
}

void wiiu_index_copy_range(uint16_t *destination, const uint16_t *source, size_t count, uint32_t *minimum, uint32_t *maximum)
{
	uint32_t lo = 0xffff, hi = 0;
	size_t i = 0;

	if (((uintptr_t)destination ^ (uintptr_t)source) & 2)
	{
		/* (alignments differ: one at a time) */
		for (; i < count; i++)
		{
			const uint32_t a = source[i];

			destination[i] = (uint16_t)a;
			lo = minimum_of(lo, a);
			hi = maximum_of(hi, a);
		}
		*minimum = lo;
		*maximum = hi;
		return;
	}
	if (count && ((uintptr_t)source & 2))
	{
		lo = hi = source[0];
		destination[0] = source[0];
		i = 1;
	}
	/* (words until the destination is at a line, then a line of 16 indices a pass: made, not read, before it is written) */
	if (line_makeable(destination))
	{
		for (; i + 2 <= count && ((uintptr_t)(destination + i) & 31); i += 2)
		{
			const uint32_t word = *(const uint32_t *)(const void *)(source + i), a = word >> 16, b = word & 0xffff;

			*(uint32_t *)(void *)(destination + i) = word;
			lo = minimum_of(lo, minimum_of(a, b));
			hi = maximum_of(hi, maximum_of(a, b));
		}
		for (; i + 16 <= count; i += 16)
		{
			const uint32_t *in = (const uint32_t *)(const void *)(source + i);
			uint32_t *out = (uint32_t *)(void *)(destination + i);
			unsigned j;

			touch(in + 64);
			line_make(out);
			for (j = 0; j < 8; j++)
			{
				const uint32_t word = in[j], a = word >> 16, b = word & 0xffff;

				out[j] = word;
				lo = minimum_of(lo, minimum_of(a, b));
				hi = maximum_of(hi, maximum_of(a, b));
			}
		}
	}
	for (; i + 8 <= count; i += 8)
	{
		const uint32_t *in = (const uint32_t *)(const void *)(source + i);
		uint32_t *out = (uint32_t *)(void *)(destination + i);
		unsigned j;

		touch(in + 128);
		for (j = 0; j < 4; j++)
		{
			const uint32_t word = in[j], a = word >> 16, b = word & 0xffff;

			out[j] = word;
			lo = minimum_of(lo, minimum_of(a, b));
			hi = maximum_of(hi, maximum_of(a, b));
		}
	}
	for (; i < count; i++)
	{
		const uint32_t a = source[i];

		destination[i] = (uint16_t)a;
		lo = minimum_of(lo, a);
		hi = maximum_of(hi, a);
	}
	*minimum = lo;
	*maximum = hi;
}
