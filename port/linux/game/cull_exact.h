/*
CULL_EXACT.H

A box's least and greatest distance from a plane, from two corners and not eight (the Wii U's box tests: structure_culling.c, render_cameras.c). The same
trick as structure_visibility.c's planes_intersect_rectangle: the distance of a corner is ((n.i * x + n.j * y) + n.k * z) - d in floating point, and each
product, sum and difference rounds monotonically, so the corner whose three products are each the greatest has the greatest distance AS THE FLOATING POINT
COMPUTES IT, and the corner of the three least products has the least. "A corner is in front" is then "the greatest is in front" and "every corner is in
front" is "the least is". Not one answer differs for any finite input (port/wiiu/tests/test_cull_exact.c compares them on millions of cases with zeros of both
signs, subnormals and huge values); a box or plane with an infinity or a NaN, or products that overflow, is left to the caller's original loop (the function
returns FALSE). The game's objects are built with -ffp-contract=off, which the equality of the sums needs.
*/

#ifndef __CULL_EXACT_H
#define __CULL_EXACT_H

/* (real, boolean, real_plane3d and real_rectangle3d are the game's) */
static __inline boolean cull_box_plane_range(
	real_plane3d const *plane,
	real_rectangle3d const *box,
	real *least,
	real *most)
{
	real ix0 = plane->n.i * box->x0, ix1 = plane->n.i * box->x1;
	real jy0 = plane->n.j * box->y0, jy1 = plane->n.j * box->y1;
	real kz0 = plane->n.k * box->z0, kz1 = plane->n.k * box->z1;
	real most_i, least_i, most_j, least_j, most_k, least_k;

	/* (x - x is 0 for every finite x, NaN for an infinity or a NaN) */
	if ((ix0 - ix0) != 0.0f || (ix1 - ix1) != 0.0f || (jy0 - jy0) != 0.0f || (jy1 - jy1) != 0.0f ||
		(kz0 - kz0) != 0.0f || (kz1 - kz1) != 0.0f || (plane->d - plane->d) != 0.0f)
	{
		return FALSE;
	}
	if (ix0 > ix1) { most_i = ix0; least_i = ix1; } else { most_i = ix1; least_i = ix0; }
	if (jy0 > jy1) { most_j = jy0; least_j = jy1; } else { most_j = jy1; least_j = jy0; }
	if (kz0 > kz1) { most_k = kz0; least_k = kz1; } else { most_k = kz1; least_k = kz0; }
	*most = most_i + most_j + most_k - plane->d;
	*least = least_i + least_j + least_k - plane->d;

	return TRUE;
}

#endif // __CULL_EXACT_H
