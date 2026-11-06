/* render_interpolation_fast.h against the arithmetic of render_interpolation.c (basis_is_rotation, quaternion_from_basis and the blend's normalising, copied here
as the oracle), and wiiu_gpu_offload.c's parser of halo/gpu_offload.txt.
cc -O2 -ffp-contract=off -DWIIU_GPU_OFFLOAD_TEST -I../../linux/game test_interpolation_fast.c ../src/wiiu_gpu_offload.c -lm */
#include "render_interpolation_fast.h"
#include "../src/wiiu_gpu_offload.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t random_state = 12345u;

static float unit_random(void)
{
	random_state = random_state * 1664525u + 1013904223u;
	return (float)(random_state >> 8) / 16777216.0f;
}

/* ---- the original's arithmetic (render_interpolation.c at c2b8d0da) */
static float length3(const float v[3])
{
	return (float)sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static int original_is_rotation(const float f[3], const float l[3], const float u[3])
{
	float cross[3], determinant;

	if (fabs(length3(f) - 1.0f) > 1e-2f || fabs(length3(l) - 1.0f) > 1e-2f || fabs(length3(u) - 1.0f) > 1e-2f)
		return 0;
	cross[0] = f[1] * l[2] - f[2] * l[1];
	cross[1] = f[2] * l[0] - f[0] * l[2];
	cross[2] = f[0] * l[1] - f[1] * l[0];
	determinant = cross[0] * u[0] + cross[1] * u[1] + cross[2] * u[2];
	return determinant > 0.5f;
}

static void original_quaternion(float m00, float m10, float m20, float m01, float m11, float m21, float m02, float m12, float m22, float q[4])
{
	float trace = m00 + m11 + m22, s;

	if (trace > 0.0f)
	{
		s = 0.5f / (float)sqrt(trace + 1.0f);
		q[3] = 0.25f / s;
		q[0] = (m21 - m12) * s;
		q[1] = (m02 - m20) * s;
		q[2] = (m10 - m01) * s;
	}
	else if (m00 > m11 && m00 > m22)
	{
		s = 2.0f * (float)sqrt(1.0f + m00 - m11 - m22);
		q[3] = (m21 - m12) / s;
		q[0] = 0.25f * s;
		q[1] = (m01 + m10) / s;
		q[2] = (m02 + m20) / s;
	}
	else if (m11 > m22)
	{
		s = 2.0f * (float)sqrt(1.0f + m11 - m00 - m22);
		q[3] = (m02 - m20) / s;
		q[0] = (m01 + m10) / s;
		q[1] = 0.25f * s;
		q[2] = (m12 + m21) / s;
	}
	else
	{
		s = 2.0f * (float)sqrt(1.0f + m22 - m00 - m11);
		q[3] = (m10 - m01) / s;
		q[0] = (m02 + m20) / s;
		q[1] = (m12 + m21) / s;
		q[2] = 0.25f * s;
	}
}

/* a rotation matrix from a random unit quaternion (columns forward, left, up) */
static void random_rotation(float f[3], float l[3], float u[3])
{
	float q[4], n;
	int i;

	for (i = 0; i < 4; i++)
		q[i] = unit_random() * 2.0f - 1.0f;
	n = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
	for (i = 0; i < 4; i++)
		q[i] /= n;
	{
		const float x = q[0], y = q[1], z = q[2], w = q[3];

		f[0] = 1 - 2 * (y * y + z * z); f[1] = 2 * (x * y + w * z); f[2] = 2 * (x * z - w * y);
		l[0] = 2 * (x * y - w * z); l[1] = 1 - 2 * (x * x + z * z); l[2] = 2 * (y * z + w * x);
		u[0] = 2 * (x * z + w * y); u[1] = 2 * (y * z - w * x); u[2] = 1 - 2 * (x * x + y * y);
	}
}

int main(void)
{
	unsigned failures = 0, i, samples = 0, decisions_differ = 0, boundary = 0;
	double worst_quaternion = 0.0, worst_rsqrt = 0.0, worst_normalised = 0.0;

	/* the parser */
	if (wiiu_gpu_offload_parse(NULL) != 0 || wiiu_gpu_offload_parse("") != 0 || wiiu_gpu_offload_parse("0\n") != 0 || wiiu_gpu_offload_parse("-1") != 0 ||
		wiiu_gpu_offload_parse("1\n") != 1 || wiiu_gpu_offload_parse(" 3 ") != 3 || wiiu_gpu_offload_parse("0x2") != 2 || wiiu_gpu_offload_parse("255") != 7 ||
		wiiu_gpu_offload_parse("8") != 0 || wiiu_gpu_offload_parse("abc") != 0)
	{
		printf("FAIL: the card file's parser\n");
		failures++;
	}
	/* 1 / sqrt over the range the blends use */
	for (i = 0; i < 200000; i++)
	{
		const float x = powf(10.0f, unit_random() * 8.0f - 3.0f);
		const double relative = fabs((double)interpolation_rsqrt(x) * sqrt((double)x) - 1.0);

		if (relative > worst_rsqrt)
			worst_rsqrt = relative;
	}
	if (worst_rsqrt > 2e-7)
	{
		printf("FAIL: 1/sqrt is off by %g (relative)\n", worst_rsqrt);
		failures++;
	}
	for (i = 0; i < 400000; i++)
	{
		float f[3], l[3], u[3], expected[4], got[4];
		int want, have, k;

		random_rotation(f, l, u);
		/* some are not rotations: scaled by up to 3 percent, one axis flipped (a mirror), or sheared */
		switch (i % 5)
		{
		case 1: for (k = 0; k < 3; k++) { const float s = 0.97f + unit_random() * 0.06f; f[k] *= s; } break;
		case 2: for (k = 0; k < 3; k++) u[k] = -u[k]; break;
		case 3: for (k = 0; k < 3; k++) l[k] += (unit_random() - 0.5f) * 0.6f; break;
		case 4: { const float s = 0.99f + (unit_random() - 0.5f) * 2e-5f; for (k = 0; k < 3; k++) f[k] *= s; } break;
		}
		want = original_is_rotation(f, l, u);
		have = interpolation_basis_is_rotation_fast(f, l, u);
		if (want != have)
		{
			/* only a basis whose length is within a hair of a limit may be judged otherwise */
			const double lengths[3] = { length3(f), length3(l), length3(u) };
			int near = 0;

			for (k = 0; k < 3; k++)
				if (fabs(fabs(lengths[k] - 1.0) - 0.01) < 2e-6)
					near = 1;
			if (near)
				boundary++;
			else
			{
				decisions_differ++;
				if (decisions_differ < 5)
					printf("FAIL: rotation test %d against %d\n", have, want);
			}
		}
		if (!want)
			continue;
		samples++;
		original_quaternion(f[0], f[1], f[2], l[0], l[1], l[2], u[0], u[1], u[2], expected);
		interpolation_quaternion_from_basis_fast(f[0], f[1], f[2], l[0], l[1], l[2], u[0], u[1], u[2], &got[0], &got[1], &got[2], &got[3]);
		for (k = 0; k < 4; k++)
			if (fabs((double)got[k] - expected[k]) > worst_quaternion)
				worst_quaternion = fabs((double)got[k] - expected[k]);
		/* the blend's normalising */
		{
			float q[4] = { expected[0] + 0.01f * unit_random(), expected[1] + 0.01f * unit_random(), expected[2], expected[3] }, r[4];
			const float squared = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
			const float length = (float)sqrt(squared), inverse = interpolation_rsqrt(squared);

			for (k = 0; k < 4; k++)
			{
				r[k] = q[k] * inverse;
				if (fabs((double)r[k] - q[k] / length) > worst_normalised)
					worst_normalised = fabs((double)r[k] - q[k] / length);
			}
		}
	}
	if (decisions_differ || worst_quaternion > 3e-6 || worst_normalised > 3e-7)
	{
		printf("FAIL: %u decisions differ, quaternion off by %g, normalised off by %g\n", decisions_differ, worst_quaternion, worst_normalised);
		failures++;
	}
	printf("%u rotations: worst quaternion difference %g, worst normalised difference %g, worst 1/sqrt %g, %u boundary decisions left out\n", samples, worst_quaternion,
		worst_normalised, worst_rsqrt, boundary);
	if (!failures)
		printf("all match\n");
	return failures != 0;
}
