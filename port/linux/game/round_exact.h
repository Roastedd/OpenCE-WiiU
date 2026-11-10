/*
ROUND_EXACT.H

halo_rint_long: (long)__builtin_rint(value), the game's way to round a float to an integer (fast_ftol, the colours' conversion to pixels), without the call of the
C library's rint, which the Wii U's compiler makes (the 750 has no instruction that rounds a double to a whole double: devkitPPC calls newlib). Under HALO_WIIU, for a
value from -2^51 to 2^51 (not a NaN: the comparisons fail for it), adding and taking away 1.5 * 2^52 in double precision rounds to the nearest whole number, halves to
the even, as rint does in the default rounding mode, and gives the same double, so the same long after the cast, for every such value. The game is built with
-ffp-contract=off and without -ffast-math, so the two operations stay two operations. Any other value, and every other build, takes (long)__builtin_rint(value).
port/wiiu/tests/test_round_exact.c compares the two on hundreds of millions of values of every kind.
*/

#ifndef __ROUND_EXACT_H
#define __ROUND_EXACT_H

static __inline long halo_rint_long(double value)
{
#ifdef HALO_WIIU
	if (value > -2251799813685248.0 && value < 2251799813685248.0)
	{
		return (long)((value + 6755399441055744.0) - 6755399441055744.0);
	}
#endif
	return (long)__builtin_rint(value);
}

#endif /* __ROUND_EXACT_H */
