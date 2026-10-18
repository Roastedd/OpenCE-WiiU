#include "stage.h"

#include "probe_logic.h"

#include <float.h>
#include <math.h>
#include <stdint.h>

#if defined(__PPC__) || defined(__powerpc__)
#define HAVE_FPSCR 1
#endif

static int stopping(const stage_context *context)
{
	return context->keep_going && !context->keep_going();
}

#ifdef HAVE_FPSCR
static uint32_t read_fpscr(void)
{
	union
	{
		double d;
		uint64_t u;
	} value;

	__asm__ volatile("mffs %0" : "=f"(value.d));
	return (uint32_t)value.u;
}
#endif

/* the compiler may not fold these: every operand is read from a volatile */
static void check_fused(report *r)
{
	volatile double a = 1.0 + 0x1p-27;
	volatile double b = 1.0 - 0x1p-27;
	volatile double c = -1.0;
	volatile float fa = 1.0f + 0x1p-13f;
	volatile float fb = 1.0f - 0x1p-13f;
	volatile float fc = -1.0f;
	double product_then_sum = a * b;
	float float_product_then_sum = fa * fb;

	product_then_sum += c;
	float_product_then_sum += fc;
	/* the inputs are the kind a fused multiply-add gives a different answer to: fma() is exact */
	report_check(r, fma(a, b, c) != 0.0, "fusing is detectable (double)", "fma gives %.3e, a*b+c rounded twice gives 0",
		fma(a, b, c));
	report_check(r, product_then_sum == 0.0, "no fused multiply-add (double)", "a*b+c = %.3e, expected 0", product_then_sum);
	report_check(r, fmaf(fa, fb, fc) != 0.0f, "fusing is detectable (float)", "fmaf gives %.3e", (double)fmaf(fa, fb, fc));
	report_check(r, float_product_then_sum == 0.0f, "no fused multiply-add (float)", "a*b+c = %.3e, expected 0",
		(double)float_product_then_sum);
}

static void check_denormals(report *r)
{
	volatile double d = DBL_MIN;
	volatile float f = FLT_MIN;
	volatile double wide = 1e-40;
	double half = d / 2.0;
	float half_float = f / 2.0f;
	float narrowed = (float)wide;
	const uint64_t half_bits = probe_double_bits(half);
	const uint32_t half_float_bits = probe_float_bits(half_float);
	const uint32_t narrowed_bits = probe_float_bits(narrowed);

	report_check(r, half_bits == 0x0008000000000000ull, "double denormal", "DBL_MIN/2 is %016llX, expected 0008000000000000",
		(unsigned long long)half_bits);
	report_check(r, half_float_bits == 0x00400000u, "float denormal", "FLT_MIN/2 is %08X, expected 00400000", half_float_bits);
	report_check(r, narrowed_bits == 0x000116C2u, "double to float denormal", "1e-40 narrowed is %08X, expected 000116C2",
		narrowed_bits);
}

/* what out-of-range and NaN conversions give is C undefined behaviour and differs by CPU: reported, not judged */
static void report_conversions(report *r)
{
	volatile float big = 3.0e9f;
	volatile float small = -3.0e9f;
	volatile float negative = -1.5f;
	volatile double not_a_number = 0.0;
	volatile double huge = 1.0e30;
	double nan;

	not_a_number = not_a_number / not_a_number;
	nan = not_a_number;
	report_note(r, "x86 gives 80000000 for a float that does not fit an int32 and for NaN; ARM64 and PowerPC saturate (PowerPC gives 80000000 for NaN)");
	report_value(r, "int32 of 3e9f", "%08X", (unsigned)(int32_t)big);
	report_value(r, "int32 of -3e9f", "%08X", (unsigned)(int32_t)small);
	report_value(r, "int32 of NaN", "%08X", (unsigned)(int32_t)nan);
	report_value(r, "uint32 of -1.5f", "%08X", (unsigned)(uint32_t)negative);
	report_value(r, "int32 of 1e30", "%08X", (unsigned)(int32_t)huge);
	report_value(r, "0/0 bits", "%016llX", (unsigned long long)probe_double_bits(nan));
}

void stage_numeric(stage_context *context)
{
	report *r = context->report;

	if (stopping(context))
		return;
	report_section(r, "numeric");
#ifdef HAVE_FPSCR
	{
		const uint32_t fpscr = read_fpscr();

		report_value(r, "FPSCR", "%08X", (unsigned)fpscr);
		report_check(r, (fpscr & 0x3u) == 0, "rounding mode", "RN %u, expected 0 (to nearest)", (unsigned)(fpscr & 0x3u));
		report_check(r, (fpscr & 0x4u) == 0, "non-IEEE mode", "NI %u, expected 0 (denormals kept)", (unsigned)((fpscr >> 2) & 1u));
		report_check(r, (fpscr & 0xF8u) == 0, "trapping exceptions", "enable bits %02X, expected 0", (unsigned)(fpscr & 0xF8u));
	}
#else
	report_note(r, "FPSCR: not PowerPC, not read");
#endif
	check_fused(r);
	check_denormals(r);
	report_conversions(r);
}
