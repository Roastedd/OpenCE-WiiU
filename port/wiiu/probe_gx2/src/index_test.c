#include "index_test.h"

/* triangle 3, then 0, then 2: corners 1 is left out */
const unsigned idx_values[IDX_COUNT] = { 9, 10, 11, 0, 1, 2, 6, 7, 8 };

const char *idx_type_name(idx_type type)
{
	static const char *const names[IDX_TYPE_COUNT] = { "U16_LE", "U32_LE", "U16", "U32" };

	return (unsigned)type < IDX_TYPE_COUNT ? names[type] : "?";
}

unsigned idx_type_width(idx_type type)
{
	return type == IDX_U32_LE || type == IDX_U32 ? 4 : 2;
}

int idx_type_swaps(idx_type type)
{
	return type == IDX_U16 || type == IDX_U32;
}

void idx_pack(unsigned width, int big_endian, uint8_t *out)
{
	unsigned i, b;

	for (i = 0; i < IDX_COUNT; i++)
		for (b = 0; b < width; b++)
			out[i * width + (big_endian ? width - 1 - b : b)] = (uint8_t)((idx_values[i] >> (8 * b)) & 0xFF);
}

void idx_read(idx_type type, const uint8_t *data, unsigned out[IDX_COUNT])
{
	unsigned width = idx_type_width(type);
	unsigned i, b;

	for (i = 0; i < IDX_COUNT; i++)
	{
		unsigned value = 0;

		for (b = 0; b < width; b++)
			value |= (unsigned)data[i * width + (idx_type_swaps(type) ? width - 1 - b : b)] << (8 * b);
		out[i] = value;
	}
}

int idx_reads_correctly(idx_type type, const uint8_t *data)
{
	unsigned read[IDX_COUNT], i;

	idx_read(type, data, read);
	for (i = 0; i < IDX_COUNT; i++)
		if (read[i] != idx_values[i])
			return 0;
	return 1;
}

unsigned idx_corners(const unsigned indices[IDX_COUNT])
{
	unsigned mask = 0, t;

	for (t = 0; t < IDX_COUNT / 3; t++)
	{
		unsigned a = indices[t * 3], b = indices[t * 3 + 1], c = indices[t * 3 + 2];

		/* a triangle whole: its three vertices, in order */
		if (a < IDX_VERTICES && a % 3 == 0 && b == a + 1 && c == a + 2)
			mask |= 1u << (a / 3);
	}
	return mask;
}
