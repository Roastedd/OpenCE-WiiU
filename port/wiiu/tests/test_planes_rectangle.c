/* structure_visibility.c's planes_intersect_rectangle on the Wii U (two corners a plane) against the original eight-corner test it keeps as
planes_intersect_rectangle_corners: the same answer for random boxes and planes, ordinary and degenerate, with zeros of both signs, subnormals,
huge values, infinities and NaNs. tools/test_wiiu_planes_rectangle.py extracts the two from the source into planes_rectangle.inc. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef float real;
typedef unsigned char boolean;
#define TRUE 1
#define FALSE 0
#define FLAG(b) (1 << (b))
#define UNSIGNED_CHAR_MAX 255
typedef struct { real i, j, k; } real_vector3d;
typedef struct { real_vector3d n; real d; } real_plane3d;
typedef struct { real x0, x1, y0, y1, z0, z1; } real_rectangle3d;
enum { _intersection_out, _intersection_in, _intersection_spanning };

#include "planes_rectangle.inc"

static uint32_t state = 12345;

static uint32_t next(void)
{
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return state;
}

static real value(int wild)
{
	static const uint32_t special[] = { 0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u, 0x007fffffu, 0x7f7fffffu, 0xff7fffffu,
		0x7f800000u, 0xff800000u, 0x7fc00000u, 0x3f800000u, 0xbf800000u };
	uint32_t bits;
	real result;

	if (wild && next() % 8 == 0)
	{
		bits = special[next() % (sizeof(special) / sizeof(special[0]))];
		memcpy(&result, &bits, sizeof(result));
		return result;
	}
	if (wild && next() % 4 == 0)
	{
		bits = next();
		memcpy(&result, &bits, sizeof(result));
		return result;
	}
	/* (world coordinates and unit normals: a box and planes near each other, so that every answer comes up) */
	return ((real)(int)(next() % 20001u) - 10000.0f) / (next() % 2 ? 100.0f : 1000.0f);
}

int main(void)
{
	unsigned long trial, failures = 0, counts[3] = { 0, 0, 0 };

	for (trial = 0; trial < 20000000UL; trial++)
	{
		const int wild = trial % 5 == 0;
		real_rectangle3d box;
		real_plane3d planes[8];
		short count = (short)(next() % 8 + 1), index;
		long got, want;

		box.x0 = value(wild); box.x1 = next() % 16 ? box.x0 + fabsf(value(0)) : value(wild);
		box.y0 = value(wild); box.y1 = next() % 16 ? box.y0 + fabsf(value(0)) : value(wild);
		box.z0 = value(wild); box.z1 = next() % 16 ? box.z0 + fabsf(value(0)) : value(wild);
		for (index = 0; index < count; index++)
		{
			planes[index].n.i = value(wild) / 100.0f;
			planes[index].n.j = value(wild) / 100.0f;
			planes[index].n.k = value(wild) / 100.0f;
			planes[index].d = value(wild);
			if (next() % 64 == 0)
				planes[index].n.j = 0.0f;
		}
		got = planes_intersect_rectangle(&box, count, planes);
		want = planes_intersect_rectangle_corners(&box, count, planes);
		if (want >= 0 && want < 3)
			counts[want]++;
		if (got != want && failures++ < 10)
			printf("FAIL trial %lu: %ld, wanted %ld\n", trial, got, want);
	}
	printf(failures ? "planes/rectangle: %lu failures\n" : "planes/rectangle: every case matches (out %lu, in %lu, spanning %lu)\n",
		failures ? failures : counts[0], counts[1], counts[2]);
	return failures != 0;
}
