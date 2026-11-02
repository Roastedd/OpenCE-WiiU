/*
HALO_BW_MODEL.H

A scalar C model of each Broadway kernel (port/wiiu/src/bw/ (the .S files)): the same operations in the same order, each paired-single
operation as the two single-precision operations it is, each branch the assembly's (its compare and its condition, NaNs included).
QEMU does not emulate paired singles, so the host cannot run the assembly; it runs this, which the differential tests
(port/wiiu/tests/test_bw_model.c) prove equal to the C functions the kernels replace, and which the host test builds of the game
(HALO_BW_MODEL) call in the kernels' place, to prove the call sites' glue. The console's self-test then proves the assembly equal to
the same C.

Compile it with -ffp-contract=off and with floats computed as floats (SSE on x86, not the x87), as the game is built; each
operation below is a statement of its own for that reason.
*/

#ifndef HALO_BW_MODEL_H
#define HALO_BW_MODEL_H

#include <string.h>

struct halo_bw_model_reference
{
	unsigned short identifier;
	unsigned short pad;
	int datum_index;
	int next_reference_index;
};

/* halo_bw_asm_matrix4x3_multiply: the 12 elements after the scale as pairs (0,1) (3,4) (6,7) and singles 2 5 8, in the assembly's
order of reads and writes (so that out may be a or b) */
static __inline__ void halo_bw_model_matrix4x3_multiply(const float *a, const float *b, float *out)
{
	float a_scale, b_scale, scale;
	float a0, a1, a2, a3, a4, a5, a6, a7, a8;
	int row;

	a_scale = a[0];
	b_scale = b[0];
	scale = a_scale * b_scale;
	out[0] = scale;
	a0 = a[1]; a1 = a[2]; a2 = a[3];
	a3 = a[4]; a4 = a[5]; a5 = a[6];
	a6 = a[7]; a7 = a[8]; a8 = a[9];
	for (row = 0; row < 4; row++)
	{
		float bx = b[1 + 3 * row], by = b[2 + 3 * row], bz = b[3 + 3 * row];
		float x, y, z, t;

		/* lane 0 and 1 of f10: (a0, a1) * bx, then (a3, a4) * by, then (a6, a7) * bz, added in that order */
		x = a0 * bx; t = a3 * by; x = x + t; t = a6 * bz; x = x + t;
		y = a1 * bx; t = a4 * by; y = y + t; t = a7 * bz; y = y + t;
		/* f11 */
		z = a2 * bx; t = a5 * by; z = z + t; t = a8 * bz; z = z + t;
		if (row == 3)
		{
			float p0 = a[10], p1 = a[11], p2 = a[12];

			x = x * a_scale; y = y * a_scale; z = z * a_scale;
			x = x + p0; y = y + p1; z = z + p2;
		}
		out[1 + 3 * row] = x;
		out[2 + 3 * row] = y;
		out[3 + 3 * row] = z;
	}
}

/* halo_bw_asm_ray_sphere */
static __inline__ int halo_bw_model_ray_sphere(const float *point, const float *vector, const float *center, float radius)
{
	float px, py, pz, c, t, b, a, disc, n;

	px = point[0] - center[0];
	py = point[1] - center[1];
	pz = point[2] - center[2];
	c = px * px;
	t = py * py;
	c = c + t;
	t = pz * pz;
	c = c + t;
	t = radius * radius;
	c = c - t;
	if (c < 0.0f)
		return 1;
	b = px * vector[0];
	t = py * vector[1];
	b = b + t;
	t = pz * vector[2];
	b = b + t;
	/* (cror EQ = GT or EQ: false for a NaN) */
	if (b > 0.0f || b == 0.0f)
		return 0;
	a = vector[0] * vector[0];
	t = vector[1] * vector[1];
	a = a + t;
	t = vector[2] * vector[2];
	a = a + t;
	disc = b * b;
	t = a * c;
	disc = disc - t;
	if (disc < 0.0f || disc == 0.0f)
		return 0;
	n = -a;
	n = n - b;
	if (n < 0.0f)
		return 1;
	n = n * n;
	return n < disc;
}

/* halo_bw_asm_bsp_ray_walk */
static __inline__ int halo_bw_model_bsp_ray_walk(struct halo_bw_bsp_walk *walk, int node, float t0, float t1)
{
	const int *nodes = (const int *)walk->nodes;
	const float *planes = (const float *)walk->planes;
	float px = walk->point[0], py = walk->point[1], pz = walk->point[2];
	float vx = walk->vector[0], vy = walk->vector[1], vz = walk->vector[2];
	int nodes_left = walk->nodes_left, depth = walk->depth;

	for (;;)
	{
		const int *entry;
		const float *plane;
		unsigned designator;
		float point_dot, vector_dot, t, distance, distance0, distance1;
		int next;

		if ((unsigned)node >= walk->node_count || depth >= 128 || nodes_left <= 0)
			break;
		entry = nodes + 3 * (unsigned)node;
		designator = (unsigned)entry[0];
		if (designator >= walk->plane_count)
			break;
		plane = planes + 4 * designator;
		point_dot = px * plane[0]; t = py * plane[1]; point_dot = point_dot + t; t = pz * plane[2]; point_dot = point_dot + t;
		vector_dot = vx * plane[0]; t = vy * plane[1]; vector_dot = vector_dot + t; t = vz * plane[2]; vector_dot = vector_dot + t;
		distance = point_dot - plane[3];
		distance0 = vector_dot * t0;
		distance0 = distance0 + distance;
		distance1 = vector_dot * t1;
		distance1 = distance1 + distance;
		if (distance0 != distance0 || distance1 != distance1)
			break;
		if (distance0 < 0.0f)
		{
			if (!(distance1 < 0.0f))
				break;
			next = entry[1];
		}
		else
		{
			if (distance1 < 0.0f)
				break;
			next = entry[2];
		}
		nodes_left--;
		depth++;
		node = next;
	}
	walk->nodes_left = nodes_left;
	walk->depth = depth;
	return node;
}

/* halo_bw_asm_bsp_sphere_walk */
static __inline__ int halo_bw_model_bsp_sphere_walk(struct halo_bw_bsp_walk *walk, int node, float radius)
{
	const int *nodes = (const int *)walk->nodes;
	const float *planes = (const float *)walk->planes;
	float cx = walk->point[0], cy = walk->point[1], cz = walk->point[2];
	float negative = -radius;
	int nodes_left = walk->nodes_left, depth = walk->depth;

	for (;;)
	{
		const int *entry;
		const float *plane;
		unsigned designator;
		float sum, t, distance;
		int next;

		if ((unsigned)node >= walk->node_count || depth >= 128 || nodes_left <= 0)
			break;
		entry = nodes + 3 * (unsigned)node;
		designator = (unsigned)entry[0];
		if (designator >= walk->plane_count)
			break;
		plane = planes + 4 * designator;
		sum = cx * plane[0]; t = cy * plane[1]; sum = sum + t; t = cz * plane[2]; sum = sum + t;
		distance = sum - plane[3];
		if (distance != distance || negative != negative || radius != radius)
			break;
		if (distance <= negative)
			next = entry[1];
		else
		{
			if (distance < radius)
				break;
			next = entry[2];
		}
		nodes_left--;
		depth++;
		node = next;
	}
	walk->nodes_left = nodes_left;
	walk->depth = depth;
	return node;
}

/* halo_bw_asm_reference_find */
static __inline__ int *halo_bw_model_reference_find(const void *references, unsigned count, unsigned zero_invalid, int *link, int datum)
{
	for (;;)
	{
		int index = *link;
		int absolute;
		unsigned identifier;
		const struct halo_bw_model_reference *reference;

		if (index == -1)
			return 0;
		absolute = (short)index;
		if ((unsigned)absolute >= count)
			return 0;
		identifier = (unsigned)index >> 16;
		if (identifier == 0 && zero_invalid)
			return 0;
		reference = (const struct halo_bw_model_reference *)((const char *)references + 12 * absolute);
		if (reference->identifier == 0)
			return 0;
		if (identifier != 0 && reference->identifier != identifier)
			return 0;
		if (reference->datum_index == datum)
			return link;
		link = (int *)&reference->next_reference_index;
	}
}

/* halo_bw_asm_quaternion_load (the model assumes GQR5 is the signed 16-bit load, as the assembly checks) */
static __inline__ int halo_bw_model_quaternion_load(const void *compressed, float *quaternion)
{
	static const unsigned scale_bits = 0x38000100u;
	const short *samples = (const short *)compressed;
	float scale;
	float i, j, k, w;

	memcpy(&scale, &scale_bits, sizeof(scale));
	i = (float)samples[0];
	j = (float)samples[1];
	k = (float)samples[2];
	w = (float)samples[3];
	quaternion[0] = i * scale;
	quaternion[1] = j * scale;
	quaternion[2] = k * scale;
	quaternion[3] = w * scale;
	return 1;
}

#endif
