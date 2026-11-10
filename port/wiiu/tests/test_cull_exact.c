/* cull_exact.h's two-corner box tests against the original eight-corner ones, as the game has them: structure_culling_box_test (port/linux/game/structure_culling.c) and
render_frustum_cube_visible (source/render/render_cameras.c), each extracted from its source by tools/test_wiiu_cull_exact.py into box_test.inc and cube_visible.inc and
compiled twice, with HALO_WIIU defined (the Wii U's) and without (the original). Random boxes and planes, ordinary and degenerate, with zeros of both signs, subnormals,
huge values (whose products overflow), infinities and NaNs: the same answer every time. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef float real;
typedef unsigned char boolean;
typedef unsigned short word;
#define TRUE 1
#define FALSE 0
#define FLAG(b) (1 << (b))
#define NUMBEROF(a) (sizeof(a) / sizeof((a)[0]))
typedef struct { real i, j, k; } real_vector3d;
typedef struct { real x, y, z; } real_point3d;
typedef struct { real_vector3d n; real d; } real_plane3d;
typedef struct { real x0, x1, y0, y1, z0, z1; } real_rectangle3d;
struct render_frustum
{
	real_plane3d world_planes[6];
	real_point3d world_vertices[5];
	real_rectangle3d world_bounds;
};

static __inline real dot_product3d(real_vector3d const *a, real_vector3d const *b)
{
	return a->i*b->i + a->j*b->j + a->k*b->k;
}
static __inline real plane3d_distance_to_point(real_plane3d const *plane, real_point3d const *point)
{
	return dot_product3d((real_vector3d *)point, &plane->n) - plane->d;
}

enum render_frustum_point_flag_bits
{
	_render_frustum_point_flags_left_bit,
	_render_frustum_point_flags_right_bit,
	_render_frustum_point_flags_top_bit,
	_render_frustum_point_flags_bottom_bit,
	_render_frustum_point_flags_near_bit,
	_render_frustum_point_flags_far_bit,
	NUMBER_OF_RENDER_FRUSTUM_POINT_FLAGS,
	RENDER_FRUSTUM_POINT_FLAGS_PLANE_MASK = FLAG(NUMBER_OF_RENDER_FRUSTUM_POINT_FLAGS) - 1,
};

#include "cull_exact.h"

#define HALO_WIIU 1
#define structure_culling_box_test fast_box_test
#define render_frustum_cube_visible fast_cube_visible
#include "box_test.inc"
#include "cube_visible.inc"
#undef structure_culling_box_test
#undef render_frustum_cube_visible
#undef HALO_WIIU
#define structure_culling_box_test ref_box_test
#define render_frustum_cube_visible ref_cube_visible
#include "box_test.inc"
#include "cube_visible.inc"

static uint32_t state = 424242;

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
		0x7f800000u, 0xff800000u, 0x7fc00000u, 0x3f800000u, 0xbf800000u, 0x7e800000u, 0xfe800000u };
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

static real_rectangle3d make_box(int wild)
{
	real_rectangle3d box;

	box.x0 = value(wild); box.x1 = next() % 16 ? box.x0 + fabsf(value(0)) : value(wild);
	box.y0 = value(wild); box.y1 = next() % 16 ? box.y0 + fabsf(value(0)) : value(wild);
	box.z0 = value(wild); box.z1 = next() % 16 ? box.z0 + fabsf(value(0)) : value(wild);
	return box;
}

static real_plane3d make_plane(int wild)
{
	real_plane3d plane;

	plane.n.i = value(wild) / 100.0f;
	plane.n.j = value(wild) / 100.0f;
	plane.n.k = value(wild) / 100.0f;
	plane.d = value(wild);
	if (next() % 64 == 0)
		plane.n.j = 0.0f;
	if (next() % 64 == 0)
		plane.n.i = -0.0f;
	if (next() % 96 == 0)
		memset(&plane, 0, sizeof(plane));
	return plane;
}

int main(void)
{
	unsigned long trial, failures = 0, box_counts[3] = { 0, 0, 0 }, cube_counts[3] = { 0, 0, 0 };

	for (trial = 0; trial < 12000000UL; trial++)
	{
		const int wild = trial % 5 == 0;
		real_rectangle3d box = make_box(wild);
		real_plane3d planes[6];
		struct render_frustum frustum;
		int index;
		short got, want;

		for (index = 0; index < 6; index++)
		{
			planes[index] = make_plane(wild);
			if (next() % 3 == 0)
			{
				/* (a plane through one of the box's corners, or a step either side of it: the answers that hang on the last bit) */
				real_point3d corner = { next() % 2 ? box.x0 : box.x1, next() % 2 ? box.y0 : box.y1, next() % 2 ? box.z0 : box.z1 };
				real d = dot_product3d((real_vector3d *)&corner, &planes[index].n);

				if (next() % 2)
					d = nextafterf(d, next() % 2 ? INFINITY : -INFINITY);
				if (d - d == 0.0f)
					planes[index].d = d;
			}
			frustum.world_planes[index] = planes[index];
		}
		got = fast_box_test(planes, &box);
		want = ref_box_test(planes, &box);
		if (want >= 0 && want < 3)
			box_counts[want]++;
		if (got != want && failures++ < 10)
			printf("FAIL box trial %lu: %d, wanted %d\n", trial, got, want);

		/* (a frustum whose bounds mostly take in the box, so that the planes are reached; its vertices near the box for the frustum-in-cube test) */
		frustum.world_bounds.x0 = -1.0e6f; frustum.world_bounds.x1 = 1.0e6f;
		frustum.world_bounds.y0 = -1.0e6f; frustum.world_bounds.y1 = 1.0e6f;
		frustum.world_bounds.z0 = -1.0e6f; frustum.world_bounds.z1 = 1.0e6f;
		if (next() % 8 == 0)
		{
			frustum.world_bounds.x0 = value(wild); frustum.world_bounds.y1 = value(wild);
		}
		for (index = 0; index < 5; index++)
		{
			frustum.world_vertices[index].x = value(wild);
			frustum.world_vertices[index].y = value(wild);
			frustum.world_vertices[index].z = value(wild);
		}
		for (index = 0; index < 2; index++)
		{
			boolean test = (boolean)index;
			short cube_got = fast_cube_visible(&frustum, &box, test);
			short cube_want = ref_cube_visible(&frustum, &box, test);

			if (cube_want >= 0 && cube_want < 3)
				cube_counts[cube_want]++;
			if (cube_got != cube_want && failures++ < 10)
				printf("FAIL cube trial %lu test %d: %d, wanted %d\n", trial, index, cube_got, cube_want);
		}
	}
	if (failures)
	{
		printf("cull exact: %lu failures\n", failures);
		return 1;
	}
	printf("cull exact: every case matches (box test: out %lu, spanning %lu, in %lu; cube: out %lu, in %lu, spanning %lu)\n",
		box_counts[0], box_counts[1], box_counts[2], cube_counts[0], cube_counts[2], cube_counts[1]);
	return 0;
}
