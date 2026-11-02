/*
WIIU_BW_REFERENCE.H

The C each Broadway kernel (port/wiiu/src/bw/ (the .S files), port/linux/include/halo_bw_kernels.h) has to equal, bit for bit.

matrix4x3_multiply, fast_vector_intersects_sphere and quaternion_decompress_8byte are the game's own functions, their bodies word for
word (tools/test_bw_reference_sync.py fails if a body here and the game's differ); the shim types are the game's, reduced to the
fields they use. The two BSP walks and the reference search are what the game's functions do at the nodes and references that the
kernels do: the game's expressions in the game's order (dot_product3d, plane3d_distance_to_point, collision_bsp3d_enter_node,
datum_get), and a stop, with nothing about the node taken, at the cases the kernel gives back to the C.

Used by the differential tests (port/wiiu/tests/test_bw_model.c, on the host) and by the console's boot-time self-test
(port/wiiu/src/wiiu_bw_kernels.c). Compile with -ffp-contract=off.
*/

#ifndef WIIU_BW_REFERENCE_H
#define WIIU_BW_REFERENCE_H

#include "halo_bw_kernels.h"

typedef float real;
typedef unsigned char boolean;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
typedef struct { real x, y, z; } real_point3d;
typedef struct { real i, j, k; } real_vector3d;
typedef struct { real_vector3d v; real w; } real_quaternion;
typedef struct { real scale; real n[4][3]; } real_matrix4x3;
struct compressed_quaternion_8byte { short i, j, k, w; };
enum { COMPRESSED_QUATERNION_COMPONENT_MAXIMUM = 32767 };

__attribute__((unused)) static void halo_bw_ref_matrix4x3_multiply(
	real_matrix4x3 const *a,
	real_matrix4x3 const *b,
	real_matrix4x3 *result)
{
	real const *a_elements = a->n[0];
	real const *b_elements = b->n[0];
	real *result_elements = result->n[0];
	real const *a_scale = &a->scale;

	{
		/* the SSE block below: rows of b combine the rows of a, and the
		position is scaled by a's scale before a's own is added */
		real product[12];
		long row, column;

		for (row = 0; row < 4; row++)
		{
			for (column = 0; column < 3; column++)
			{
				real value = b_elements[row * 3 + 0] * a_elements[column] +
					b_elements[row * 3 + 1] * a_elements[3 + column] +
					b_elements[row * 3 + 2] * a_elements[6 + column];

				if (row == 3)
					value = value * *a_scale + a_elements[9 + column];
				product[row * 3 + column] = value;
			}
		}
		for (row = 0; row < 12; row++)
			result_elements[row] = product[row];
	}

	result->scale = a->scale * b->scale;

	return;
}

__attribute__((unused)) static boolean halo_bw_ref_fast_vector_intersects_sphere(
	real_point3d const *point,
	real_vector3d const *vector,
	real_point3d const *center,
	real radius)
{
	real_point3d p = {point->x-center->x, point->y-center->y, point->z - center->z};
	real c = (p.x*p.x) + (p.y*p.y) + (p.z*p.z) - (radius*radius);
	real_vector3d direction;

	if (c < 0.f)
	{
		return TRUE;
	}
	else
	{
		real b;

		direction = *vector;
		b = direction.i*p.x + direction.j*p.y + direction.k*p.z;
		
		if (b >= 0.f)
		{
			return FALSE;
		}
		else
		{
			real a =
				direction.i*direction.i +
				direction.j*direction.j +
				direction.k*direction.k;
			real disc = b * b - a * c;
			
			if (disc <= 0.f)
			{
				return FALSE;
			}
			else
			{
				real neg_a_minus_b = -a - b;

				if (neg_a_minus_b < 0.f)
				{
					return TRUE;
				}
				else
				{
					return neg_a_minus_b * neg_a_minus_b < disc;
				}
			}
		}
	}
}

__attribute__((unused)) static void halo_bw_ref_quaternion_decompress_8byte(
	struct compressed_quaternion_8byte const *compressed,
	real_quaternion *quaternion)
{
	real const scale = 1.f / COMPRESSED_QUATERNION_COMPONENT_MAXIMUM;

	quaternion->v.i = compressed->i * scale;
	quaternion->v.j = compressed->j * scale;
	quaternion->v.k = compressed->k * scale;
	quaternion->w = compressed->w * scale;

	return;
}

/* collision_bsp_test_vector_recursive's nodes that send the ray one way (the node count, the depth and the budget of
collision_bsp3d_enter_node; the plane of the designator; the two distances; reaches_back and reaches_front), as many in a row as there
are; it stops, taking nothing about the node, at a leaf or an index out of range, the depth or the budget reached, a designator out of
range, a crossing, and a distance that is not a number */
__attribute__((unused)) static int halo_bw_ref_bsp_ray_walk(struct halo_bw_bsp_walk *walk, int node, real t0, real t1)
{
	const int *nodes = (const int *)walk->nodes;
	const real *planes = (const real *)walk->planes;
	const real_point3d *point = (const real_point3d *)walk->point;
	const real_vector3d *vector = (const real_vector3d *)walk->vector;

	for (;;)
	{
		const int *entry;
		const real *plane;
		real distance, dot, distance0, distance1;
		boolean reaches_back, reaches_front;

		if (node < 0 || (unsigned)node >= walk->node_count || walk->depth >= 128 || walk->nodes_left <= 0)
			return node;
		entry = nodes + 3 * node;
		if (entry[0] < 0 || (unsigned)entry[0] >= walk->plane_count)
			return node;
		plane = planes + 4 * entry[0];
		distance = (point->x*plane[0] + point->y*plane[1] + point->z*plane[2]) - plane[3];
		dot = vector->i*plane[0] + vector->j*plane[1] + vector->k*plane[2];
		distance0 = dot*t0 + distance;
		distance1 = dot*t1 + distance;
		if (distance0 != distance0 || distance1 != distance1)
			return node;
		reaches_back = distance0 < 0.f || distance1 < 0.f;
		reaches_front = distance0 >= 0.f || distance1 >= 0.f;
		if (reaches_back && reaches_front)
			return node;
		walk->nodes_left--;
		walk->depth++;
		node = entry[1 + reaches_front];
	}
}

/* bsp3d_test_sphere_recursive's, likewise (child_index and reaches_second_child) */
__attribute__((unused)) static int halo_bw_ref_bsp_sphere_walk(struct halo_bw_bsp_walk *walk, int node, real radius)
{
	const int *nodes = (const int *)walk->nodes;
	const real *planes = (const real *)walk->planes;
	const real_point3d *center = (const real_point3d *)walk->point;

	for (;;)
	{
		const int *entry;
		const real *plane;
		real distance;
		boolean reaches_second_child, child_index;

		if (node < 0 || (unsigned)node >= walk->node_count || walk->depth >= 128 || walk->nodes_left <= 0)
			return node;
		entry = nodes + 3 * node;
		if (entry[0] < 0 || (unsigned)entry[0] >= walk->plane_count)
			return node;
		plane = planes + 4 * entry[0];
		distance = (center->x*plane[0] + center->y*plane[1] + center->z*plane[2]) - plane[3];
		if (distance != distance || radius != radius)
			return node;
		reaches_second_child = distance < radius;
		child_index = distance > -radius;
		if (child_index && reaches_second_child)
			return node;
		walk->nodes_left--;
		walk->depth++;
		node = entry[1 + child_index];
	}
}

/* reference_list_remove's search: datum_get's checks of each reference on the list, and the compare of its datum index; NULL where
datum_get would not return the reference (or asserts), or the datum is not on the list. The reference is 12 bytes: a short
identifier, a short of padding, the datum index and the next reference's index. */
struct halo_bw_ref_reference
{
	short identifier;
	unsigned short pad;
	int datum_index;
	int next_reference_index;
};

__attribute__((unused)) static int *halo_bw_ref_reference_find(const void *references, unsigned count, unsigned zero_invalid, int *first, int datum)
{
	int *link = first;

	while (*link != -1)
	{
		short identifier = (short)(*link >> 16);
		short absolute_index = (short)*link;
		const struct halo_bw_ref_reference *reference;

		if (!identifier && zero_invalid)
			return 0;
		if (!(absolute_index >= 0 && absolute_index < (int)count))
			return 0;
		reference = (const struct halo_bw_ref_reference *)((const char *)references + 12 * absolute_index);
		if (!(reference->identifier && (!identifier || identifier == reference->identifier)))
			return 0;
		if (reference->datum_index == datum)
			return link;
		link = (int *)&reference->next_reference_index;
	}
	return 0;
}

#endif
