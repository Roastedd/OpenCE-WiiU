/*
WIIU_SQRTF.C

sqrtf for the Espresso, which has no square root instruction: newlib's takes the root a bit at a time (24 passes of integer work), and the game asks
for thousands a frame (2% of the game thread in a fight). This one is correctly rounded as newlib's is - the same float for every input, which network
play, saved films and the deterministic tick need - by a different road: an estimate of 1/sqrt from the float's bits, three Newton steps in double
precision (from the estimate's 3.5% to 4e-11 of the root, 0.0006 of a float's last place, where a fourth only adds digits a double keeps and a float
throws away), the root as the nearest float, then a check of the two halfway points either side
of it (the rounding of an estimate that is 0.0006 of a place out is wrong only when the root is that close to a halfway point, and then it is one float away), squared exactly in double precision (a 25-bit number squared is exact there; neither can equal the input, a 24-bit float), which moves it to the
correctly rounded float when the estimate's rounding landed one away. Zeros, infinities, NaNs and negatives give what newlib's gives.
port/wiiu/tests/test_sqrtf.c checks every float against the host's correctly rounded sqrtf.
*/

#include <stdint.h>
#include <string.h>

static uint32_t float_bits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static float bits_float(uint32_t bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

float wiiu_sqrtf(float x);

float wiiu_sqrtf(float x)
{
	const uint32_t bits = float_bits(x);
	double d, scaled, y, s;
	float r;
	uint32_t estimate_bits;
	double unscale = 1.0;

	/* (zeros, negatives, infinities and NaNs: as newlib's __ieee754_sqrtf and its wrapper) */
	if (bits - 1u >= 0x7f7fffffu)
	{
		if (x == 0.0f)
			return x;
		if (x != x)
			return x * x + x;
		if (x < 0.0f)
			return (x - x) / (x - x);
		return x;
	}
	d = (double)x;
	scaled = d;
	estimate_bits = bits;
	if (bits < 0x00800000u)
	{
		/* (a subnormal: the estimate from its bits is useless, so from 2^24 times it, exactly; the root is then 2^12 too big) */
		scaled = d * 16777216.0;
		estimate_bits = float_bits((float)scaled);
		unscale = 1.0 / 4096.0;
	}
	y = (double)bits_float(0x5f3759dfu - (estimate_bits >> 1));
	y = y * (1.5 - 0.5 * scaled * y * y);
	y = y * (1.5 - 0.5 * scaled * y * y);
	y = y * (1.5 - 0.5 * scaled * y * y);
	s = scaled * y * unscale;
	r = (float)s;
	{
		const uint32_t root = float_bits(r);
		const double above = ((double)r + (double)bits_float(root + 1u)) * 0.5;
		const double below = ((double)bits_float(root - 1u) + (double)r) * 0.5;

		if (above * above < d)
			r = bits_float(root + 1u);
		else if (below * below > d)
			r = bits_float(root - 1u);
	}
	return r;
}

#ifndef WIIU_SQRTF_TEST
/* (in place of newlib's: the link takes this object's before the library's) */
float sqrtf(float x)
{
	return wiiu_sqrtf(x);
}
#endif
