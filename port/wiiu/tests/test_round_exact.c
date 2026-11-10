/* round_exact.h's halo_rint_long (port/linux/game/round_exact.h) as the Wii U has it (HALO_WIIU: adding and taking away 1.5 * 2^52 for a value of -2^51 to 2^51) against
(long)__builtin_rint(value) as the game had it, and the colours' conversions to pixels of source/bitmaps/bitmaps_inlines.h (each channel rounded once, the check kept)
against the same functions rounding each channel as often as they did. The values: ordinary, halves, a hair either side of a half, negative, zeros of both signs, around
2^31, 2^51, 2^52, infinities, NaNs, every bit pattern; the colours' channels from -2 to 3 (255 times that is a whole range of results and of failed checks). The same
long every time, the same pixel, the same number of checks that fail. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef float real;
typedef uint32_t pixel32;
typedef struct { real alpha, red, green, blue; } real_argb_color;
typedef struct { real red, green, blue; } real_rgb_color;
#define UNSIGNED_CHAR_MAX 255

static long failed_checks;
#define match_assert(file, line, expr) if (!(expr)) { failed_checks++; }
#define match_vassert(file, line, expr, string) ((void)0)
#define csprintf(...) 0
#define match_assert_valid_real_rgb_color(file, line, rgb) ((void)0)
#define valid_real_argb_color(color) 1
#define temporary 0

/* round_exact.h twice: as the Wii U has it (wiiu_rint) and as every other build does (ref_rint_long) */
#define HALO_WIIU 1
#define halo_rint_long wiiu_rint
#include "round_exact.h"
#undef __ROUND_EXACT_H
#undef halo_rint_long
#undef HALO_WIIU
#define halo_rint_long ref_rint_long
#include "round_exact.h"
#undef halo_rint_long

/* the Wii U's conversions: the header's own text, extracted by tools/test_wiiu_round_exact.py into pixels_fast.inc, with halo_rint_long the Wii U's */
#define halo_rint_long wiiu_rint
#define real_argb_color_to_pixel32 fast_argb
#define real_rgb_color_to_pixel32 fast_rgb
#define real_alpha_to_pixel32 fast_alpha
#define __inline static __inline
#include "pixels_fast.inc"
#undef real_argb_color_to_pixel32
#undef real_rgb_color_to_pixel32
#undef real_alpha_to_pixel32
#undef halo_rint_long

/* the original's, as they were before the Wii U's changes: each channel rounded as often as it was written, by the library's rint */
static pixel32 ref_argb(real_argb_color const *color)
{
	real scale = 255.0f;
	pixel32 result;

	match_vassert("", 0, valid_real_argb_color(color), 0);
	result = (pixel32)(
		(long)__builtin_rint((double)color->blue * scale) |
		((long)__builtin_rint((double)color->green * scale) << 8) |
		((long)__builtin_rint((double)color->red * scale) << 16) |
		((long)__builtin_rint((double)color->alpha * scale) << 24));
	{
		pixel32 verify;

		verify = (pixel32)(
			((long)__builtin_rint((double)color->blue * scale) & 0xff) |
			(((long)__builtin_rint((double)color->green * scale) & 0xff) << 8) |
			(((long)__builtin_rint((double)color->red * scale) & 0xff) << 16) |
			((long)__builtin_rint((double)color->alpha * scale) << 24));

		match_assert("", 188, verify == result);
	}
	return result;
}

static pixel32 ref_rgb(real_rgb_color const *color)
{
	pixel32 result;
	real scale = (real)UNSIGNED_CHAR_MAX;

	result = (pixel32)(
		((long)__builtin_rint((double)color->blue * scale) & 0xff) |
		(((long)__builtin_rint((double)color->green * scale) & 0xff) << 8) |
		(((long)__builtin_rint((double)color->red * scale) & 0xff) << 16));
	return result;
}

static pixel32 ref_alpha(real alpha)
{
	real scale = 255.0f;

	match_assert("", 291, alpha>=0.0f && alpha<=1.0f);
	return (pixel32)((long)__builtin_rint((double)alpha * scale) << 24);
}

static uint64_t rng = 0x9e3779b97f4a7c15ull;
static uint32_t next32(void)
{
	rng ^= rng << 13;
	rng ^= rng >> 7;
	rng ^= rng << 17;
	return (uint32_t)(rng >> 16);
}
static double from_bits64(uint64_t bits)
{
	double d;

	memcpy(&d, &bits, sizeof(d));
	return d;
}
static double make_value(void)
{
	const double special[] = { 0.0, -0.0, 0.5, -0.5, 1.5, -1.5, 2.5, -2.5, 0.49999999999999994, -0.49999999999999994, 0.5000000000000001, 254.5, 255.0, 127.5, 2147483647.5,
		2147483648.0, -2147483648.5, 2251799813685247.5, 2251799813685248.0, -2251799813685248.0, -2251799813685247.5, 4503599627370496.0, 4503599627370497.0, 1e300, -1e300,
		INFINITY, -INFINITY, NAN, 1e-300, 4.9e-324 };

	switch (next32() % 8)
	{
	case 0: return special[next32() % (sizeof(special) / sizeof(special[0]))];
	case 1: return ((double)(int32_t)next32()) / 256.0; /* quarters of 2^-8: halves and near halves */
	case 2: return (double)(int32_t)next32() + 0.5;
	case 3: return (double)(next32() % 100000) * 0.01;
	case 4: return from_bits64(((uint64_t)next32() << 32) | next32());
	case 5: return from_bits64(0x4320000000000000ull + (next32() % 512) - 256); /* around 2^51 */
	case 6: return (double)(int32_t)next32() * 1048576.0;
	default: return ((double)next32() / 4294967296.0) * 255.0;
	}
}
static float make_channel(void)
{
	switch (next32() % 5)
	{
	case 0: return (float)(next32() % 256) / 255.0f; /* a byte's own value over 255: a half cannot be it, but products come near */
	case 1: return (float)(next32() % 511) / 510.0f; /* halves of a step */
	case 2: return ((float)next32() / 4294967296.0f) * 5.0f - 2.0f;
	case 3: return (float)(next32() % 2) ? 0.0f : -0.0f;
	default: return ((float)(next32() % 1021) - 255) / 255.0f;
	}
}

int main(void)
{
	long n, differences = 0, cases = 0;
	long checks_fast = 0, checks_ref = 0;

	for (n = 0; n < 300000000L; n++)
	{
		const double value = make_value();
		const long a = wiiu_rint(value), b = ref_rint_long(value);

		cases++;
		if (a != b && differences++ < 10)
			printf("DIFFERENT: %a (%g): %ld against %ld\n", value, value, a, b);
	}
	for (n = 0; n < 40000000L; n++)
	{
		real_argb_color argb = { make_channel(), make_channel(), make_channel(), make_channel() };
		real_rgb_color rgb = { argb.red, argb.green, argb.blue };
		real alpha = argb.alpha;
		pixel32 fa, ra, fr, rr, fl, rl;

		failed_checks = 0;
		fa = fast_argb(&argb);
		fr = fast_rgb(&rgb);
		fl = fast_alpha(alpha);
		checks_fast += failed_checks;
		failed_checks = 0;
		ra = ref_argb(&argb);
		rr = ref_rgb(&rgb);
		rl = ref_alpha(alpha);
		checks_ref += failed_checks;
		cases++;
		if ((fa != ra || fr != rr || fl != rl) && differences++ < 10)
			printf("DIFFERENT pixel: %g %g %g %g: %08x %08x %08x against %08x %08x %08x\n", argb.alpha, argb.red, argb.green, argb.blue, fa, fr, fl, ra, rr, rl);
	}
	if (checks_fast != checks_ref)
	{
		differences++;
		printf("DIFFERENT: %ld failed checks against %ld\n", checks_fast, checks_ref);
	}
	printf("%ld cases, %ld failed checks of the pixel conversions (the same each way), %ld differences\n", cases, checks_ref, differences);
	if (differences)
		return 1;
	printf("every case matches\n");
	return 0;
}
