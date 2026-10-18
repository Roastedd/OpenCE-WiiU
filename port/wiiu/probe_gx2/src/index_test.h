/*
INDEX_TEST.H

Index buffers on a big-endian console. GX2 has four index types: GX2_INDEX_TYPE_U16_LE and U32_LE read little-endian data
as it is (what the Xbox's index buffers hold), U16 and U32 swap the bytes of each index first (data the big-endian CPU
wrote). The test draws the same triangles from each type with data in each order, and the picture says which of them
read the indices they were given.

The indices pick triangles out of twelve vertices, three to a triangle, one triangle to each corner of the target, with
a triangle left out; an index read wrongly picks other vertices, or outside the buffer where there are zeros, and the
corner it belonged to stays empty.
*/

#ifndef HALO_WIIU_GX2_INDEX_TEST_H
#define HALO_WIIU_GX2_INDEX_TEST_H

#include <stdint.h>

typedef enum idx_type
{
	IDX_U16_LE,
	IDX_U32_LE,
	IDX_U16,
	IDX_U32,
	IDX_TYPE_COUNT
} idx_type;

#define IDX_COUNT 9
#define IDX_VERTICES 12
#define IDX_MAX_BYTES (IDX_COUNT * 4)

extern const unsigned idx_values[IDX_COUNT];

const char *idx_type_name(idx_type type);
unsigned idx_type_width(idx_type type);
/* 1 for the types that swap (the data they expect is big-endian) */
int idx_type_swaps(idx_type type);

/* the data of an index buffer: width 2 or 4, in the byte order given */
void idx_pack(unsigned width, int big_endian, uint8_t *out);

/* the indices a type reads from data */
void idx_read(idx_type type, const uint8_t *data, unsigned out[IDX_COUNT]);

/* 1 if the type reads every index as it was written */
int idx_reads_correctly(idx_type type, const uint8_t *data);

/* The corners (0 top left, 1 top right, 2 bottom left, 3 bottom right) whose triangle the indices pick: bit k set. */
unsigned idx_corners(const unsigned indices[IDX_COUNT]);

#endif
