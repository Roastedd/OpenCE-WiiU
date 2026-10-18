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
static uint32_t real_read_fpscr(void)
{
	union
	{
		double d;
		uint64_t u;
	} value;

	__asm__ volatile("mffs %0" : "=f"(value.d));
	return (uint32_t)value.u;
}

/* FPSCR[NI], bit 29: with it set a PowerPC flushes denormals to zero, where x86 keeps them */
static void real_clear_non_ieee(void)
{
	__asm__ volatile("mtfsb0 29");
}

static uint32_t (*read_fpscr)(void) = real_read_fpscr;
static void (*clear_non_ieee)(void) = real_clear_non_ieee;

uint32_t stage_read_fpscr(void)
{
	return real_read_fpscr();
}
#else
static uint32_t (*read_fpscr)(void);
static void (*clear_non_ieee)(void);

uint32_t stage_read_fpscr(void)
{
	return 0;
}
#endif

/* for the tests: a floating-point unit that says what they want it to (the stage cannot otherwise be run with the bit set on a
machine that does not start with it, which the emulator does not) */
void stage_numeric_use_fpu(uint32_t (*read)(void), void (*clear)(void))
{
	read_fpscr = read;
	clear_non_ieee = clear;
}

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

typedef struct denormal_results
{
	uint64_t half_double;
	uint32_t half_float;
	uint32_t narrowed;
} denormal_results;

static void measure_denormals(denormal_results *results)
{
	volatile double d = DBL_MIN;
	volatile float f = FLT_MIN;
	volatile double wide = 1e-40;
	double half = d / 2.0;
	float half_float = f / 2.0f;
	float narrowed = (float)wide;

	results->half_double = probe_double_bits(half);
	results->half_float = probe_float_bits(half_float);
	results->narrowed = probe_float_bits(narrowed);
}

static void check_denormals(report *r, const denormal_results *results)
{
	report_check(r, results->half_double == 0x0008000000000000ull, "double denormal", "DBL_MIN/2 is %016llX, expected 0008000000000000",
		(unsigned long long)results->half_double);
	report_check(r, results->half_float == 0x00400000u, "float denormal", "FLT_MIN/2 is %08X, expected 00400000", results->half_float);
	report_check(r, results->narrowed == 0x000116C2u, "double to float denormal", "1e-40 narrowed is %08X, expected 000116C2",
		results->narrowed);
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
	denormal_results results;

	if (stopping(context))
		return;
	report_section(r, "numeric");
	if (read_fpscr)
	{
		const uint32_t fpscr = read_fpscr();

		report_value(r, "FPSCR at start", "%08X", (unsigned)fpscr);
		report_check(r, (fpscr & 0x3u) == 0, "rounding mode", "RN %u, expected 0 (to nearest)", (unsigned)(fpscr & 0x3u));
		report_check(r, (fpscr & 0xF8u) == 0, "trapping exceptions", "enable bits %02X, expected 0", (unsigned)(fpscr & 0xF8u));
		if (fpscr & 0x4u)
		{
			uint32_t after;

			/* the first console run found it set: say what it does, then clear it and test again */
			measure_denormals(&results);
			report_note(r, "non-IEEE mode is on at start: denormals are flushed to zero (DBL_MIN/2 is %016llX, FLT_MIN/2 is %08X, 1e-40 narrowed is %08X)",
				(unsigned long long)results.half_double, results.half_float, results.narrowed);
			clear_non_ieee();
			after = read_fpscr();
			report_value(r, "FPSCR after clearing NI", "%08X", (unsigned)after);
			report_check(r, (after & 0x4u) == 0, "non-IEEE mode cleared", "NI %u after mtfsb0 29, expected 0", (unsigned)((after >> 2) & 1u));
		}
		else
		{
			report_check(r, 1, "non-IEEE mode", "NI 0 at start (denormals kept)");
		}
	}
	else
	{
		report_note(r, "FPSCR: not PowerPC, not read");
	}
	check_fused(r);
	measure_denormals(&results);
	check_denormals(r, &results);
	report_conversions(r);
}
