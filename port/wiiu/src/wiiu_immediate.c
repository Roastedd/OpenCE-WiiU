#include "wiiu_immediate.h"

#include <stdlib.h>
#include <string.h>

static void layout_set(struct wiiu_immediate *immediate, unsigned long mask)
{
	unsigned reg;

	immediate->layout_mask = mask;
	immediate->layout_count = 0;
	for (reg = 0; reg < WIIU_IMMEDIATE_REGISTERS; reg++)
		if (mask & (1UL << reg))
			immediate->layout[immediate->layout_count++] = reg;
}

void wiiu_immediate_begin(struct wiiu_immediate *immediate)
{
	immediate->mask = 1;
	immediate->count = 0;
	layout_set(immediate, 1);
}

static int room_for(struct wiiu_immediate *immediate, unsigned long vertices, unsigned registers)
{
	if (vertices > immediate->capacity)
	{
		unsigned long capacity = immediate->capacity ? immediate->capacity : 256;
		float *grown;

		while (capacity < vertices)
			capacity *= 2;
		/* (capacity is in vertices of the widest layout, so that a re-layout never needs more) */
		grown = realloc(immediate->vertices, capacity * WIIU_IMMEDIATE_REGISTERS * 4 * sizeof(float));
		if (!grown)
			return 0;
		immediate->vertices = grown;
		immediate->capacity = capacity;
	}
	(void)registers;
	return 1;
}

void wiiu_immediate_write(struct wiiu_immediate *immediate, unsigned reg, const float *old_value)
{
	unsigned long mask = immediate->mask | (1UL << reg);

	if (immediate->mask & (1UL << reg))
		return;
	immediate->mask = mask;
	if (immediate->count)
	{
		/* the vertices made so far have this register at the value it held until now: lay them out again, last to first so that no vertex is overwritten before it is moved */
		const unsigned old_count = immediate->layout_count;
		unsigned new_layout[WIIU_IMMEDIATE_REGISTERS], new_count = 0, index, old_index;
		unsigned long vertex;

		for (index = 0; index < WIIU_IMMEDIATE_REGISTERS; index++)
			if (mask & (1UL << index))
				new_layout[new_count++] = index;
		if (!room_for(immediate, immediate->count, new_count))
		{
			immediate->count = 0; /* (out of memory: the draw is lost) */
			layout_set(immediate, mask);
			return;
		}
		for (vertex = immediate->count; vertex-- > 0;)
		{
			const float *from = immediate->vertices + vertex * old_count * 4;
			float *to = immediate->vertices + vertex * new_count * 4;

			/* (to >= from: the new layout is wider; copy from the end so that overlap is safe) */
			for (index = new_count, old_index = old_count; index-- > 0;)
			{
				if (new_layout[index] == reg)
					memcpy(to + index * 4, old_value, 4 * sizeof(float));
				else
				{
					old_index--;
					memmove(to + index * 4, from + old_index * 4, 4 * sizeof(float));
				}
			}
		}
	}
	layout_set(immediate, mask);
}

int wiiu_immediate_emit(struct wiiu_immediate *immediate, const float (*registers)[4])
{
	float *vertex;
	unsigned i;

	if (!room_for(immediate, immediate->count + 1, immediate->layout_count))
		return 0;
	vertex = immediate->vertices + immediate->count * immediate->layout_count * 4;
	/* (four stores, not a call of memcpy for 16 bytes: a decal's vertex is three registers, and the decals are thousands of vertices a frame) */
	for (i = 0; i < immediate->layout_count; i++)
	{
		const float *from = registers[immediate->layout[i]];

		vertex[i * 4] = from[0];
		vertex[i * 4 + 1] = from[1];
		vertex[i * 4 + 2] = from[2];
		vertex[i * 4 + 3] = from[3];
	}
	immediate->count++;
	return 1;
}

unsigned wiiu_immediate_pack(const struct wiiu_immediate *immediate, unsigned long count, float *out, unsigned *registers)
{
	unsigned i;

	for (i = 0; i < immediate->layout_count; i++)
		registers[i] = immediate->layout[i];
	if (count)
		memcpy(out, immediate->vertices, count * immediate->layout_count * 4 * sizeof(float));
	return immediate->layout_count;
}
