/* The loop of structure_render_dynamic_triangles_from_bitvector (source/structures/structure_render.c), the set bits of each word lowest first with the last word cut at the surface count,
against the loop it replaced (every bit tested): the same surfaces in the same order, for every count. */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define LONG_BITS 32

static long old_way(long count, const uint32_t *flags, long *out)
{
	long n = 0, surface_index = 0;

	do
	{
		if (*flags)
		{
			short bit;

			for (bit = 0; bit < LONG_BITS; bit++, surface_index++)
			{
				if (surface_index >= count)
					break;
				if ((*flags >> bit) & 1)
					out[n++] = surface_index;
			}
		}
		else
			surface_index += LONG_BITS;
		flags++;
	} while (surface_index < count);
	return n;
}

static long new_way(long count, const uint32_t *flags, long *out)
{
	long n = 0, surface_index = 0;

	do
	{
		uint32_t word = *flags;

		if (word)
		{
			long valid = count - surface_index;

			if (valid < LONG_BITS)
				word &= (1u << valid) - 1;
			while (word)
			{
				const long bit = (long)__builtin_ctz(word);

				word &= word - 1;
				out[n++] = surface_index + bit;
			}
		}
		surface_index += LONG_BITS;
		flags++;
	} while (surface_index < count);
	return n;
}

int main(void)
{
	static uint32_t flags[200];
	static long a[6400], b[6400];
	long count, i, failures = 0;
	unsigned seed;

	for (seed = 1; seed <= 20; seed++)
	{
		srand(seed);
		for (i = 0; i < 200; i++)
			flags[i] = seed % 4 == 0 ? 0xffffffffu : seed % 4 == 1 ? (uint32_t)((unsigned)rand() * 2654435761u) : seed % 4 == 2 ? (rand() % 5 ? 0 : (uint32_t)rand()) : 1u << (rand() % 32);
		for (count = 1; count < 6400; count += (count < 100 ? 1 : 37))
		{
			const long n1 = old_way(count, flags, a), n2 = new_way(count, flags, b);

			if (n1 != n2)
			{
				printf("FAIL count %ld: %ld against %ld\n", count, n1, n2);
				failures++;
				continue;
			}
			for (i = 0; i < n1; i++)
				if (a[i] != b[i])
				{
					printf("FAIL count %ld at %ld\n", count, i);
					failures++;
					break;
				}
		}
	}
	printf(failures ? "%ld failures\n" : "bitvector loop: all passed\n", failures);
	return failures != 0;
}
