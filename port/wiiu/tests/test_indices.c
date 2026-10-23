/* wiiu_index_range: the same as the plain loop, for every length and alignment. */

#include "../src/wiiu_indices.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

int main(void)
{
	static uint16_t data[3000 + 8];
	size_t count, offset;
	unsigned seed;
	uint32_t lo, hi;

	for (seed = 0; seed < 6; seed++)
	{
		srand(seed + 1);
		for (count = 0; count < 3000; count++)
			data[count] = (uint16_t)(seed == 0 ? 500 : seed == 1 ? count : (unsigned)rand() % (seed == 2 ? 70000 : seed == 3 ? 100 : 65536));
		for (offset = 0; offset < 4; offset++)
			for (count = 0; count < 70; count++)
			{
				uint32_t want_lo = 0xffff, want_hi = 0;
				size_t i;

				for (i = 0; i < count; i++)
				{
					if (data[offset + i] < want_lo)
						want_lo = data[offset + i];
					if (data[offset + i] > want_hi)
						want_hi = data[offset + i];
				}
				wiiu_index_range(data + offset, count, &lo, &hi);
				if (lo != want_lo || hi != want_hi)
				{
					printf("FAIL seed %u offset %zu count %zu: %u %u, wanted %u %u\n", seed, offset, count, lo, hi, want_lo, want_hi);
					failures++;
				}
			}
		/* long ranges, with the extremes at the ends */
		data[1] = 0;
		data[2999] = 65535;
		wiiu_index_range(data + 1, 2999, &lo, &hi);
		if (lo != 0 || hi != 65535)
		{
			printf("FAIL extremes\n");
			failures++;
		}
	}
	/* copy and range: the same range, the same bytes, for every pair of alignments and lengths, and nothing written past the end */
	{
		static uint16_t source[3000 + 8], destination[3000 + 16];
		size_t so, d_o, n;

		for (n = 0; n < 3000; n++)
			source[n] = (uint16_t)rand();
		for (so = 0; so < 4; so++)
			for (d_o = 0; d_o < 4; d_o++)
				for (n = 0; n < 40; n += (n < 20 ? 1 : 7))
				{
					uint32_t want_lo = 0xffff, want_hi = 0;
					size_t i;

					memset(destination, 0xee, sizeof(destination));
					for (i = 0; i < n; i++)
					{
						if (source[so + i] < want_lo)
							want_lo = source[so + i];
						if (source[so + i] > want_hi)
							want_hi = source[so + i];
					}
					wiiu_index_copy_range(destination + d_o, source + so, n, &lo, &hi);
					if (lo != want_lo || hi != want_hi || memcmp(destination + d_o, source + so, n * 2) || destination[d_o + n] != 0xeeee || (d_o && destination[d_o - 1] != 0xeeee))
					{
						printf("FAIL copy source %zu destination %zu count %zu\n", so, d_o, n);
						failures++;
					}
				}
		wiiu_index_copy_range(destination, source, 3000, &lo, &hi);
		if (memcmp(destination, source, 6000))
		{
			printf("FAIL long copy\n");
			failures++;
		}
	}
	printf(failures ? "%d failures\n" : "index range: all passed\n", failures);
	return failures != 0;
}
