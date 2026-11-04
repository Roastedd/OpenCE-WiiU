/*
RENDER_INTERPOLATION_FAST.H

The arithmetic of render_interpolation.c's blends with the square roots taken out (halo/gpu_offload.txt bit 1 on the Wii U; port/wiiu/src/wiiu_gpu_offload.h).

The Espresso has no square root instruction. The port's sqrtf (wiiu_sqrtf.c) is correctly rounded, as the ticks and the network need, by four Newton steps in double
precision and a check: about 250 cycles. A node of a blended pose took nine of them (a rotation found for each of the two poses: three lengths for the test that
the basis is a rotation and one for the quaternion, and one to normalise the blend): about 2,300 cycles of the 3,200 a node takes, and the blends are 2.3 ms of a
fight's 21 ms of drawing (runs/wiiu/session-20261031/codex-ab/b30-M-*, "blends"). The drawing can do without a correctly rounded root: a pose is shown, not
simulated, and the ticks never read these results.

  * the test that a basis is a rotation compares the squared lengths with the squares of the limits (0.99 and 1.01), no root;
  * the quaternion of a basis and the normalised blend use 1 / sqrt, from the estimate the processor gives (frsqrte) and three Newton steps in single precision,
    which are as exact as a float (the relative error is under 1e-6 on the host's reference, test_interpolation_fast.c checks it against the original arithmetic).

The results differ from render_interpolation.c's own by a few units in the last place of a float; the decision "a rotation or not" differs only for a basis whose
length is within a millionth of 0.99 or 1.01. With the bit off nothing here is called.
*/
#ifndef RENDER_INTERPOLATION_FAST_H
#define RENDER_INTERPOLATION_FAST_H

#include <math.h>

/* 1 / sqrt(x) for a normal positive x */
static inline float interpolation_rsqrt(float x)
{
#if defined(__powerpc__) || defined(__ppc__)
	double estimate;
	float y;

	__asm__("frsqrte %0,%1" : "=f"(estimate) : "f"((double)x));
	y = (float)estimate;
	/* (the estimate is good to about 5 bits; each step doubles them: 10, 20, 40) */
	y = y * (1.5f - 0.5f * x * y * y);
	y = y * (1.5f - 0.5f * x * y * y);
	y = y * (1.5f - 0.5f * x * y * y);
	return y;
#else
	return 1.0f / sqrtf(x);
#endif
}

/* an orthonormal right-handed basis: the lengths of its three vectors within 1 percent of 1 (squares within 0.9801 and 1.0201) and the determinant over 0.5.
f, l and u are the vectors forward, left and up */
static inline int interpolation_basis_is_rotation_fast(const float f[3], const float l[3], const float u[3])
{
	const float fl = f[0] * f[0] + f[1] * f[1] + f[2] * f[2];
	const float ll = l[0] * l[0] + l[1] * l[1] + l[2] * l[2];
	const float ul = u[0] * u[0] + u[1] * u[1] + u[2] * u[2];
	float cross[3], determinant;

	if (!(fl >= 0.9801f && fl <= 1.0201f && ll >= 0.9801f && ll <= 1.0201f && ul >= 0.9801f && ul <= 1.0201f))
		return 0;
	cross[0] = f[1] * l[2] - f[2] * l[1];
	cross[1] = f[2] * l[0] - f[0] * l[2];
	cross[2] = f[0] * l[1] - f[1] * l[0];
	determinant = cross[0] * u[0] + cross[1] * u[1] + cross[2] * u[2];
	return determinant > 0.5f;
}

/* the quaternion (i, j, k, w) of the rotation whose columns are forward (m00 m10 m20), left (m01 m11 m21), up (m02 m12 m22): render_interpolation.c's
quaternion_from_basis with its four roots and its divisions turned into one 1 / sqrt and products */
static inline void interpolation_quaternion_from_basis_fast(float m00, float m10, float m20, float m01, float m11, float m21, float m02, float m12, float m22,
	float *i, float *j, float *k, float *w)
{
	const float trace = m00 + m11 + m22;
	float x, r, inverse_s, quarter_s;

	if (trace > 0.0f)
	{
		x = trace + 1.0f;
		r = interpolation_rsqrt(x);
		inverse_s = 0.5f * r; /* (the original's s is 0.5 / sqrt(x)) */
		*w = 0.5f * x * r; /* (0.25 / s) */
		*i = (m21 - m12) * inverse_s;
		*j = (m02 - m20) * inverse_s;
		*k = (m10 - m01) * inverse_s;
	}
	else if (m00 > m11 && m00 > m22)
	{
		x = 1.0f + m00 - m11 - m22;
		r = interpolation_rsqrt(x);
		inverse_s = 0.5f * r; /* (the original's s is 2 sqrt(x)) */
		quarter_s = 0.5f * x * r; /* (0.25 s) */
		*w = (m21 - m12) * inverse_s;
		*i = quarter_s;
		*j = (m01 + m10) * inverse_s;
		*k = (m02 + m20) * inverse_s;
	}
	else if (m11 > m22)
	{
		x = 1.0f + m11 - m00 - m22;
		r = interpolation_rsqrt(x);
		inverse_s = 0.5f * r;
		quarter_s = 0.5f * x * r;
		*w = (m02 - m20) * inverse_s;
		*i = (m01 + m10) * inverse_s;
		*j = quarter_s;
		*k = (m12 + m21) * inverse_s;
	}
	else
	{
		x = 1.0f + m22 - m00 - m11;
		r = interpolation_rsqrt(x);
		inverse_s = 0.5f * r;
		quarter_s = 0.5f * x * r;
		*w = (m10 - m01) * inverse_s;
		*i = (m02 + m20) * inverse_s;
		*j = (m12 + m21) * inverse_s;
		*k = quarter_s;
	}
}

#endif
