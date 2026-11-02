/*
WIIU_BW_TESTCASES.H

The cases the Broadway kernels are tested on, one function for each kernel that takes the kernel and runs it, with the C reference
(wiiu_bw_reference.h), on random and edge inputs, and compares every output word bit for bit. The host's differential test
(port/wiiu/tests/test_bw_model.c) hands it the scalar model of the kernel; the console's boot-time self-test
(port/wiiu/src/wiiu_bw_kernels.c) hands it the assembly. Compile with -ffp-contract=off.

Inputs come in classes, because the way a kernel can differ from the C is by what it does with a particular kind of number:
  normal    ordinary values of the sizes the game has (about 2^-7 to 2^7, either sign)
  ties      the same with many exact zeros, +-1, +-0.5 and small integers: distances exactly zero, equal to a radius, cancelling sums
  wide      every exponent: overflow to infinity, underflow to a subnormal, cancellation of huge numbers
  denormal  subnormal numbers and zeros among normal ones (the Espresso's FPSCR non-IEEE mode, which the console runs in, may treat a
            subnormal differently in a paired-single operation than in a scalar one: this class finds it)
  inf       infinities among normal ones
  nan       quiet NaNs of one payload among normal ones (a case with a second payload compares a NaN with a NaN: which operand's payload
            a NaN-producing operation passes on depends on the operand order, which the assembly and the compiler may differ in)
Outputs are compared as bits: a zero's sign, a NaN's payload (with the one exception above) and every other bit.
*/

#ifndef WIIU_BW_TESTCASES_H
#define WIIU_BW_TESTCASES_H

#include "wiiu_bw_reference.h"

#include <stdio.h>
#include <string.h>

enum bw_class
{
	BW_NORMAL,
	BW_TIES,
	BW_WIDE,
	BW_DENORMAL,
	BW_INF,
	BW_NAN,
	BW_CLASSES
};

static const char *const bw_class_names[BW_CLASSES] = { "normal", "ties", "wide", "denormal", "inf", "nan" };

struct bw_stats
{
	unsigned long cases;
	unsigned long failures;
	unsigned long lenient;	/* cases whose NaNs were compared as NaNs (two payloads) */
	char first[240];		/* the first failure: what differed */
};

struct bw_rng
{
	unsigned state;
};

static unsigned bw_next(struct bw_rng *rng)
{
	unsigned x = rng->state;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	rng->state = x;
	return x;
}

static unsigned bw_bits(float value)
{
	unsigned bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static float bw_float_from_bits(unsigned bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

static int bw_is_nan(float value)
{
	return (bw_bits(value) & 0x7fffffffu) > 0x7f800000u;
}

/* a random float of the class; payload is the NaN of this case (its low 22 bits, and its sign in the top bit) */
static float bw_float(struct bw_rng *rng, int cls, unsigned payload)
{
	static const float ties[] = { 0.0f, -0.0f, 1.0f, -1.0f, 0.5f, -0.5f, 2.0f, -2.0f, 0.25f, 3.0f, -3.0f, 4.0f, 0.125f, -0.125f };
	const unsigned pick = bw_next(rng) % 100;
	const unsigned mantissa = bw_next(rng) & 0x7fffffu;
	const unsigned sign = (bw_next(rng) & 1u) << 31;
	unsigned exponent = 120 + bw_next(rng) % 15;	/* 2^-7 to 2^7 */

	switch (cls)
	{
	case BW_TIES:
		if (pick < 45)
			return ties[bw_next(rng) % (sizeof(ties) / sizeof(ties[0]))];
		break;
	case BW_WIDE:
		exponent = 1 + bw_next(rng) % 254;
		break;
	case BW_DENORMAL:
		if (pick < 25)
			return bw_float_from_bits(sign | mantissa);
		if (pick < 35)
			return bw_float_from_bits(sign);
		if (pick < 45)
			exponent = 1 + bw_next(rng) % 20;	/* small normals, whose products and sums go subnormal */
		break;
	case BW_INF:
		if (pick < 12)
			return bw_float_from_bits(sign | 0x7f800000u);
		if (pick < 20)
			exponent = 240 + bw_next(rng) % 15;
		break;
	case BW_NAN:
		if (pick < 12)
			return bw_float_from_bits(0x7fc00000u | payload);	/* (the payload has the NaN's sign in it: one NaN, bit for bit, in a case) */
		break;
	default:
		break;
	}
	return bw_float_from_bits(sign | (exponent << 23) | mantissa);
}

static void bw_fail(struct bw_stats *stats, const char *what, int cls, unsigned long index, const char *field, unsigned kernel, unsigned reference)
{
	if (!stats->failures++)
		snprintf(stats->first, sizeof(stats->first), "%s case %lu (%s): %s is %08x, the C gives %08x", what, index, bw_class_names[cls], field,
			kernel, reference);
}

static int bw_same(unsigned kernel, unsigned reference, int lenient)
{
	if (kernel == reference)
		return 1;
	return lenient && bw_is_nan(bw_float_from_bits(kernel)) && bw_is_nan(bw_float_from_bits(reference));
}

/* ---------- matrix4x3_multiply */

typedef void (*bw_matrix_fn)(const float *a, const float *b, float *out);

static void bw_test_matrix(bw_matrix_fn kernel, int cls, unsigned long cases, unsigned seed, struct bw_stats *stats)
{
	struct bw_rng rng = { seed * 2654435761u + 1u };
	unsigned long index;

	for (index = 0; index < cases; index++)
	{
		float a[13] __attribute__((aligned(16))), b[13] __attribute__((aligned(16))), expect[13];
		float guard[13 + 8] __attribute__((aligned(16)));
		float t[13] __attribute__((aligned(16)));
		const unsigned payload = (bw_next(&rng) & 0x3fffffu) | (bw_next(&rng) & 0x80000000u);
		const int lenient = cls == BW_NAN && (bw_next(&rng) & 3u) == 0;
		int k, variant;

		for (k = 0; k < 13; k++)
		{
			a[k] = bw_float(&rng, cls, payload);
			b[k] = bw_float(&rng, cls, lenient && (bw_next(&rng) & 1u) ? payload ^ 0x155u : payload);
		}
		if (lenient)
			stats->lenient++;
		stats->cases++;
		halo_bw_ref_matrix4x3_multiply((const real_matrix4x3 *)a, (const real_matrix4x3 *)b, (real_matrix4x3 *)expect);
		for (variant = 0; variant < 4; variant++)
		{
			/* 0: a, b and out apart; 1: out is a; 2: out is b; 3: all three are one matrix (a times itself) */
			const float *use_a = a, *use_b = b;
			float *out = guard + 4;
			float want[13];

			for (k = 0; k < 21; k++)
				guard[k] = 12345.0f;
			memcpy(want, expect, sizeof(want));
			if (variant == 1)
			{
				memcpy(t, a, sizeof(t));
				use_a = out = t;
			}
			else if (variant == 2)
			{
				memcpy(t, b, sizeof(t));
				use_b = out = t;
			}
			else if (variant == 3)
			{
				memcpy(t, a, sizeof(t));
				halo_bw_ref_matrix4x3_multiply((const real_matrix4x3 *)t, (const real_matrix4x3 *)t, (real_matrix4x3 *)want);
				use_a = use_b = out = t;
			}
			kernel(use_a, use_b, out);
			for (k = 0; k < 13; k++)
				if (!bw_same(bw_bits(out[k]), bw_bits(want[k]), lenient))
				{
					bw_fail(stats, variant ? "matrix in place" : "matrix", cls, index, "element", bw_bits(out[k]), bw_bits(want[k]));
					break;
				}
			if (variant == 0)
				for (k = 0; k < 21; k++)
					if ((k < 4 || k >= 17) && bw_bits(guard[k]) != bw_bits(12345.0f))
					{
						bw_fail(stats, "matrix wrote outside its output", cls, index, "guard", bw_bits(guard[k]), bw_bits(12345.0f));
						break;
					}
		}
	}
}

/* ---------- fast_vector_intersects_sphere */

typedef int (*bw_sphere_fn)(const float *point, const float *vector, const float *center, float radius);

static void bw_test_sphere(bw_sphere_fn kernel, int cls, unsigned long cases, unsigned seed, struct bw_stats *stats)
{
	struct bw_rng rng = { seed * 2654435761u + 7u };
	unsigned long index;

	for (index = 0; index < cases; index++)
	{
		float point[3] __attribute__((aligned(16))), vector[3] __attribute__((aligned(16))), center[3] __attribute__((aligned(16)));
		float radius;
		const unsigned payload = (bw_next(&rng) & 0x3fffffu) | (bw_next(&rng) & 0x80000000u);
		const int lenient = cls == BW_NAN && (bw_next(&rng) & 3u) == 0;
		const unsigned shape = bw_next(&rng) % 4;
		int k, expect, got;

		for (k = 0; k < 3; k++)
		{
			point[k] = bw_float(&rng, cls, payload);
			vector[k] = bw_float(&rng, cls, payload);
			center[k] = bw_float(&rng, cls, lenient ? payload ^ 0x155u : payload);
		}
		radius = bw_float(&rng, cls, payload);
		if (shape == 1 && cls != BW_NAN)
		{
			/* a center near the segment: the hit and the miss near the boundary */
			float s = (float)(bw_next(&rng) % 1025) * 0.0009765625f;

			for (k = 0; k < 3; k++)
			{
				float step = vector[k] * s;

				center[k] = point[k] + step;
			}
			radius = radius < 0.0f ? -radius : radius;
		}
		else if (shape == 2)
			radius = radius < 0.0f ? -radius : radius;
		stats->cases++;
		if (lenient)
			stats->lenient++;
		expect = halo_bw_ref_fast_vector_intersects_sphere((const real_point3d *)point, (const real_vector3d *)vector, (const real_point3d *)center, radius);
		got = kernel(point, vector, center, radius) != 0;
		if (got != expect)
			bw_fail(stats, "sphere", cls, index, "answer", (unsigned)got, (unsigned)expect);
	}
}

/* ---------- the BSP walks */

typedef int (*bw_ray_walk_fn)(struct halo_bw_bsp_walk *walk, int node, float t0, float t1);
typedef int (*bw_sphere_walk_fn)(struct halo_bw_bsp_walk *walk, int node, float radius);

#define BW_NODES 48
#define BW_PLANES 12

struct bw_tree
{
	int nodes[BW_NODES * 3] __attribute__((aligned(16)));
	float planes[BW_PLANES * 4] __attribute__((aligned(16)));
	float point[3] __attribute__((aligned(16)));
	float vector[3] __attribute__((aligned(16)));
	unsigned node_count, plane_count;
};

/* a random BSP of the class: mostly nodes that lead on, some leaves (the high bit), some designators and children out of range */
static void bw_make_tree(struct bw_rng *rng, int cls, unsigned payload, struct bw_tree *tree)
{
	unsigned i;
	const unsigned shape = bw_next(rng) % 8;

	tree->node_count = 1 + bw_next(rng) % BW_NODES;
	tree->plane_count = 1 + bw_next(rng) % BW_PLANES;
	for (i = 0; i < tree->node_count; i++)
	{
		unsigned pick = bw_next(rng) % 100;

		tree->nodes[3 * i] = (int)(bw_next(rng) % (tree->plane_count + (pick < 4 ? 3 : 0)));
		if (pick >= 96)
			tree->nodes[3 * i] = (int)(0x80000000u | bw_next(rng));
		else if (pick >= 94)
			tree->nodes[3 * i] = -1;
		for (unsigned c = 1; c < 3; c++)
		{
			const unsigned which = bw_next(rng) % 100;

			if (which < 55)
				tree->nodes[3 * i + c] = (int)(bw_next(rng) % tree->node_count);
			else if (which < 62 && i + 1 < tree->node_count)
				tree->nodes[3 * i + c] = (int)(i + 1);
			else if (which < 85)
				tree->nodes[3 * i + c] = (int)(0x80000000u | (bw_next(rng) % 40));
			else if (which < 90)
				tree->nodes[3 * i + c] = -1;
			else if (which < 95)
				tree->nodes[3 * i + c] = (int)(tree->node_count + bw_next(rng) % 5);
			else
				tree->nodes[3 * i + c] = (int)i;	/* itself: the depth ends it */
		}
	}
	for (i = 0; i < BW_PLANES; i++)
	{
		unsigned k;

		for (k = 0; k < 4; k++)
			tree->planes[4 * i + k] = bw_float(rng, cls, payload);
		if (shape < 3 && cls != BW_NAN)
		{
			/* axis planes, as the maps mostly have, at ties */
			tree->planes[4 * i] = tree->planes[4 * i + 1] = tree->planes[4 * i + 2] = 0.0f;
			tree->planes[4 * i + bw_next(rng) % 3] = (bw_next(rng) & 1u) ? 1.0f : -1.0f;
			if (cls != BW_TIES)
				tree->planes[4 * i + 3] = (float)((int)(bw_next(rng) % 9) - 4) * 0.5f;
		}
	}
	for (i = 0; i < 3; i++)
	{
		tree->point[i] = bw_float(rng, cls, payload);
		tree->vector[i] = bw_float(rng, cls, payload);
		if (shape < 3 && cls != BW_NAN)
			tree->point[i] = (float)((int)(bw_next(rng) % 9) - 4) * 0.5f;
	}
}

static void bw_test_ray_walk(bw_ray_walk_fn kernel, int cls, unsigned long cases, unsigned seed, struct bw_stats *stats)
{
	struct bw_rng rng = { seed * 2654435761u + 13u };
	unsigned long index;

	for (index = 0; index < cases; index++)
	{
		struct bw_tree tree;
		struct halo_bw_bsp_walk reference_walk, kernel_walk;
		const unsigned payload = (bw_next(&rng) & 0x3fffffu) | (bw_next(&rng) & 0x80000000u);
		const int lenient = cls == BW_NAN && (bw_next(&rng) & 3u) == 0;
		float t0, t1;
		int start, expect, got;

		bw_make_tree(&rng, cls, lenient ? payload ^ 0x155u : payload, &tree);
		t0 = (float)(bw_next(&rng) % 1025) * 0.0009765625f;
		t1 = t0 + (float)(bw_next(&rng) % 1025) * 0.0009765625f;
		if (cls != BW_NORMAL && cls != BW_TIES && (bw_next(&rng) & 3u) == 0)
		{
			t0 = bw_float(&rng, cls, payload);
			t1 = bw_float(&rng, cls, payload);
		}
		start = (bw_next(&rng) % 10) ? (int)(bw_next(&rng) % tree.node_count) : (int)(bw_next(&rng) % (tree.node_count + 3));
		reference_walk.nodes = tree.nodes;
		reference_walk.planes = tree.planes;
		reference_walk.point = tree.point;
		reference_walk.vector = tree.vector;
		reference_walk.node_count = tree.node_count;
		reference_walk.plane_count = tree.plane_count;
		reference_walk.nodes_left = (bw_next(&rng) % 5) ? 300 : (int)(bw_next(&rng) % 8) - 1;
		reference_walk.depth = (bw_next(&rng) % 6) ? (int)(bw_next(&rng) % 5) : (int)(bw_next(&rng) % 132);
		kernel_walk = reference_walk;
		stats->cases++;
		if (lenient)
			stats->lenient++;
		expect = halo_bw_ref_bsp_ray_walk(&reference_walk, start, t0, t1);
		got = kernel(&kernel_walk, start, t0, t1);
		if (got != expect)
			bw_fail(stats, "ray walk", cls, index, "node", (unsigned)got, (unsigned)expect);
		else if (kernel_walk.nodes_left != reference_walk.nodes_left)
			bw_fail(stats, "ray walk", cls, index, "nodes left", (unsigned)kernel_walk.nodes_left, (unsigned)reference_walk.nodes_left);
		else if (kernel_walk.depth != reference_walk.depth)
			bw_fail(stats, "ray walk", cls, index, "depth", (unsigned)kernel_walk.depth, (unsigned)reference_walk.depth);
		else if (kernel_walk.nodes != reference_walk.nodes || kernel_walk.planes != reference_walk.planes || kernel_walk.point != reference_walk.point ||
			kernel_walk.vector != reference_walk.vector || kernel_walk.node_count != reference_walk.node_count || kernel_walk.plane_count != reference_walk.plane_count)
			bw_fail(stats, "ray walk", cls, index, "walk input changed", 0, 0);
	}
}

static void bw_test_sphere_walk(bw_sphere_walk_fn kernel, int cls, unsigned long cases, unsigned seed, struct bw_stats *stats)
{
	struct bw_rng rng = { seed * 2654435761u + 29u };
	unsigned long index;

	for (index = 0; index < cases; index++)
	{
		struct bw_tree tree;
		struct halo_bw_bsp_walk reference_walk, kernel_walk;
		const unsigned payload = (bw_next(&rng) & 0x3fffffu) | (bw_next(&rng) & 0x80000000u);
		const int lenient = cls == BW_NAN && (bw_next(&rng) & 3u) == 0;
		float radius;
		int start, expect, got;

		bw_make_tree(&rng, cls, lenient ? payload ^ 0x155u : payload, &tree);
		radius = (bw_next(&rng) & 1u) ? (float)(bw_next(&rng) % 9) * 0.25f : bw_float(&rng, cls, payload);
		start = (bw_next(&rng) % 10) ? (int)(bw_next(&rng) % tree.node_count) : (int)(bw_next(&rng) % (tree.node_count + 3));
		reference_walk.nodes = tree.nodes;
		reference_walk.planes = tree.planes;
		reference_walk.point = tree.point;
		reference_walk.vector = 0;
		reference_walk.node_count = tree.node_count;
		reference_walk.plane_count = tree.plane_count;
		reference_walk.nodes_left = (bw_next(&rng) % 5) ? 300 : (int)(bw_next(&rng) % 8) - 1;
		reference_walk.depth = (bw_next(&rng) % 6) ? (int)(bw_next(&rng) % 5) : (int)(bw_next(&rng) % 132);
		kernel_walk = reference_walk;
		stats->cases++;
		if (lenient)
			stats->lenient++;
		expect = halo_bw_ref_bsp_sphere_walk(&reference_walk, start, radius);
		got = kernel(&kernel_walk, start, radius);
		if (got != expect)
			bw_fail(stats, "sphere walk", cls, index, "node", (unsigned)got, (unsigned)expect);
		else if (kernel_walk.nodes_left != reference_walk.nodes_left)
			bw_fail(stats, "sphere walk", cls, index, "nodes left", (unsigned)kernel_walk.nodes_left, (unsigned)reference_walk.nodes_left);
		else if (kernel_walk.depth != reference_walk.depth)
			bw_fail(stats, "sphere walk", cls, index, "depth", (unsigned)kernel_walk.depth, (unsigned)reference_walk.depth);
	}
}

/* ---------- the reference search */

typedef int *(*bw_find_fn)(const void *references, unsigned count, unsigned zero_invalid, int *first, int datum);

#define BW_REFERENCES 32

static void bw_test_find(bw_find_fn kernel, unsigned long cases, unsigned seed, struct bw_stats *stats)
{
	struct bw_rng rng = { seed * 2654435761u + 41u };
	unsigned long index;

	for (index = 0; index < cases; index++)
	{
		struct halo_bw_ref_reference references[BW_REFERENCES] __attribute__((aligned(16)));
		unsigned count = 1 + bw_next(&rng) % BW_REFERENCES, zero_invalid = bw_next(&rng) & 1u, i;
		int first, datum, link_for_reference, link_for_kernel;
		int *expect, *got;

		memset(references, 0, sizeof(references));
		for (i = 0; i < count; i++)
		{
			const unsigned pick = bw_next(&rng) % 100;

			references[i].identifier = pick < 8 ? 0 : (short)(1 + bw_next(&rng) % 5);
			references[i].pad = (unsigned short)bw_next(&rng);
			references[i].datum_index = (int)(bw_next(&rng) % 6);
			/* the next: a later reference, with its identifier or not (a list that goes on, or goes wrong), or a link that ends the walk
			(none, a negative index, one past the count): never an earlier reference, so no loops */
			if (pick < 15 || i + 1 >= count)
			{
				const unsigned ending = bw_next(&rng) % 3;
				const unsigned identifier = bw_next(&rng) & 0xffffu;

				references[i].next_reference_index = ending == 0 ? -1
					: ending == 1 ? (int)((identifier << 16) | 0x8000u | (bw_next(&rng) & 0x7fffu))
					: (int)((identifier << 16) | (count + bw_next(&rng) % 50));
			}
			else
			{
				const unsigned later = i + 1 + bw_next(&rng) % (count - i - 1);
				const unsigned identifier = (bw_next(&rng) % 20) ? (unsigned)(unsigned short)references[later].identifier : bw_next(&rng) & 0xffffu;
				const unsigned form = bw_next(&rng) % 20;

				references[i].next_reference_index = form == 0 ? (int)later /* no identifier */
					: (int)(later | (identifier << 16));
			}
		}
		if (bw_next(&rng) % 12)
		{
			const unsigned start = bw_next(&rng) % count;
			const unsigned form = bw_next(&rng) % 10;
			const unsigned identifier = form == 0 ? 0 : form == 1 ? (bw_next(&rng) & 0xffffu) : (unsigned)(unsigned short)references[start].identifier;

			first = (int)(start | (identifier << 16));
		}
		else
			first = (bw_next(&rng) & 1u) ? -1 : (int)bw_next(&rng);
		if (first != -1 && (unsigned short)first >= count && (bw_next(&rng) % 4))
			first = (int)(((unsigned)first & 0xffff0000u) | (bw_next(&rng) % count));
		datum = (int)(bw_next(&rng) % 7);
		link_for_reference = link_for_kernel = first;
		stats->cases++;
		expect = halo_bw_ref_reference_find(references, count, zero_invalid, &link_for_reference, datum);
		got = kernel(references, count, zero_invalid, &link_for_kernel, datum);
		/* (the first link is each one's own variable: the same answer is its own address in both) */
		if (expect == &link_for_reference)
			expect = &link_for_kernel;
		if (expect != got)
			bw_fail(stats, "reference find", BW_NORMAL, index, "link", (unsigned)(size_t)got, (unsigned)(size_t)expect);
	}
}

/* ---------- quaternion_decompress_8byte */

typedef int (*bw_qload_fn)(const void *compressed, float *quaternion);

/* every value of each of the four samples (the samples are independent: the other three are random), then random */
static void bw_test_qload(bw_qload_fn kernel, unsigned long random_cases, unsigned seed, struct bw_stats *stats, int *unavailable)
{
	struct bw_rng rng = { seed * 2654435761u + 53u };
	unsigned long index, total = 4ul * 65536ul + random_cases;

	*unavailable = 0;
	for (index = 0; index < total; index++)
	{
		struct compressed_quaternion_8byte compressed __attribute__((aligned(8)));
		float expect[4] __attribute__((aligned(16))), got[5] __attribute__((aligned(16)));
		short *samples = (short *)&compressed;
		int k;

		for (k = 0; k < 4; k++)
			samples[k] = (short)bw_next(&rng);
		if (index < 4ul * 65536ul)
			samples[index >> 16] = (short)(index & 0xffffu);
		got[4] = 777.0f;
		memset(got, 0xA5, 4 * sizeof(float));
		stats->cases++;
		halo_bw_ref_quaternion_decompress_8byte(&compressed, (real_quaternion *)expect);
		if (!kernel(&compressed, got))
		{
			*unavailable = 1;
			return;
		}
		for (k = 0; k < 4; k++)
			if (bw_bits(got[k]) != bw_bits(expect[k]))
			{
				bw_fail(stats, "quaternion load", BW_NORMAL, index, "component", bw_bits(got[k]), bw_bits(expect[k]));
				break;
			}
		if (got[4] != 777.0f)
			bw_fail(stats, "quaternion load wrote outside its output", BW_NORMAL, index, "guard", bw_bits(got[4]), bw_bits(777.0f));
	}
}

#endif
