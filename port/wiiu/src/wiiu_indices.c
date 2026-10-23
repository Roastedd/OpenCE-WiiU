#include "wiiu_indices.h"

void wiiu_index_range(const uint16_t *indices, size_t count, uint32_t *minimum, uint32_t *maximum)
{
	uint32_t lo = 0xffff, hi = 0, lo2 = 0xffff, hi2 = 0;
	size_t pairs;

	/* (the word loads are aligned: an odd first index is taken alone) */
	if (count && ((uintptr_t)indices & 2))
	{
		lo = hi = indices[0];
		indices++;
		count--;
	}
	/* (a word holds two indices, and which is which does not matter to a minimum and a maximum; four words a pass) */
	for (pairs = count / 2; pairs >= 4; pairs -= 4, indices += 8)
	{
		const uint32_t *words = (const uint32_t *)(const void *)indices;
		unsigned i;

		for (i = 0; i < 4; i++)
		{
			const uint32_t a = words[i] >> 16, b = words[i] & 0xffff;

			if (a < lo)
				lo = a;
			if (b < lo2)
				lo2 = b;
			if (a > hi)
				hi = a;
			if (b > hi2)
				hi2 = b;
		}
	}
	for (; pairs; pairs--, indices += 2)
	{
		const uint32_t a = indices[0], b = indices[1];

		if (a < lo)
			lo = a;
		if (b < lo2)
			lo2 = b;
		if (a > hi)
			hi = a;
		if (b > hi2)
			hi2 = b;
	}
	if (count & 1)
	{
		const uint32_t a = indices[0];

		if (a < lo)
			lo = a;
		if (a > hi)
			hi = a;
	}
	*minimum = lo < lo2 ? lo : lo2;
	*maximum = hi > hi2 ? hi : hi2;
}
