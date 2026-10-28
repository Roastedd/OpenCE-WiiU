/* wiiu_immediate.c: the same draw data as copying all sixteen registers for every vertex, for random sequences of register writes, with the vertices in a place lent (of
random size, given back at random) or on the heap. */

#include "../src/wiiu_immediate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static unsigned ended_lent;

int main(void)
{
	unsigned trial;

	srand(7);
	for (trial = 0; trial < 3000; trial++)
	{
		struct wiiu_immediate fast;
		float attributes[16][4], naive[300][16][4];
		unsigned long naive_mask, naive_count = 0, vertex;
		unsigned long writes = 1 + (unsigned)rand() % 200, w;
		float out[300 * 16 * 4];
		static float lent[300 * 16 * 4];
		unsigned registers[16], touched, i, reg;
		const int lending = trial % 3 != 0;

		memset(&fast, 0, sizeof(fast));
		for (i = 0; i < 64; i++)
			((float *)attributes)[i] = (float)(rand() % 1000);
		/* a Begin */
		wiiu_immediate_begin(&fast);
		naive_mask = 1;
		for (w = 0; w < writes && naive_count < 299; w++)
		{
			/* the Direct3D layer lends its stream when the first vertex is made */
			if (lending && !fast.count && !fast.lent_floats && rand() % 2)
				wiiu_immediate_lend(&fast, lent, (unsigned long)(rand() % 8000));
			if (fast.lent_floats && rand() % 50 == 0)
				wiiu_immediate_unlend(&fast);
			/* mostly the registers a draw uses (0 completes a vertex), sometimes any */
			reg = rand() % 4 ? (unsigned)rand() % 4 : (unsigned)rand() % 16;
			wiiu_immediate_write(&fast, reg, attributes[reg]);
			naive_mask |= 1UL << reg;
			for (i = 0; i < 4; i++)
				attributes[reg][i] = (float)(rand() % 100000);
			if (reg == 0)
			{
				if (rand() % 4 == 0)
				{
					/* a vertex written by the caller in the room given (as the decals' are) */
					float *vertex = wiiu_immediate_room(&fast, 1);

					if (!vertex)
						return 2;
					for (i = 0; i < fast.layout_count; i++)
						memcpy(vertex + i * 4, attributes[fast.layout[i]], 4 * sizeof(float));
					fast.count++;
				}
				else if (!wiiu_immediate_emit(&fast, (const float (*)[4])attributes))
					return 2;
				memcpy(naive[naive_count++], attributes, sizeof(attributes));
			}
		}
		ended_lent += fast.lent_floats != 0;
		touched = wiiu_immediate_pack(&fast, fast.count, out, registers);
		if (fast.count != naive_count || fast.mask != naive_mask)
		{
			printf("FAIL trial %u: count or mask\n", trial);
			failures++;
			continue;
		}
		/* what the full copies would give: every vertex's registers of the final mask */
		{
			unsigned expected_touched = 0;

			for (reg = 0; reg < 16; reg++)
				if (naive_mask & (1UL << reg))
					expected_touched++;
			if (touched != expected_touched)
			{
				printf("FAIL trial %u: touched\n", trial);
				failures++;
				continue;
			}
		}
		for (vertex = 0; vertex < naive_count; vertex++)
			for (i = 0; i < touched; i++)
				if (memcmp(out + (vertex * touched + i) * 4, naive[vertex][registers[i]], 4 * sizeof(float)))
				{
					printf("FAIL trial %u vertex %lu register %u\n", trial, vertex, registers[i]);
					failures++;
					vertex = naive_count;
					break;
				}
		free(fast.heap);
	}
	printf(failures ? "%d failures\n" : "immediate: all passed (%u of the draws were still in the place lent at their end)\n", failures ? failures : (int)ended_lent);
	return failures != 0;
}
