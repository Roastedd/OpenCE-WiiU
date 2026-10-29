#include "../include/wiiu_resampler_ps.h"

#include <stdint.h>
#include <string.h>
#ifndef WIIU_RESAMPLER_PS_TEST
#include <coreinit/core.h>
#endif

#define TAPS 48
#define QUALIFIED_MODE 4u
#define GQR_TYPES 0x00070007u

static int initialized, enabled, report_pending;
static struct wiiu_resampler_ps_status state;

#ifdef WIIU_RESAMPLER_PS_TEST
/* Host policy tests inject reads and kernels; no production reset API exists. */
extern unsigned wiiu_resampler_ps_test_core(void);
extern unsigned wiiu_resampler_ps_test_fpscr(void);
extern unsigned wiiu_resampler_ps_test_gqr0(void);
#define read_core wiiu_resampler_ps_test_core
#define read_fpscr wiiu_resampler_ps_test_fpscr
#define read_gqr0 wiiu_resampler_ps_test_gqr0
#else
static unsigned read_core(void)
{
	return OSGetCoreId();
}

static unsigned read_fpscr(void)
{
	union { double real; uint64_t bits; } value;
	__asm__ volatile("mffs %0" : "=f"(value.real));
	return (unsigned)value.bits;
}

static unsigned read_gqr0(void)
{
	unsigned value;
	__asm__ volatile("mfspr %0,896" : "=r"(value));
	return value;
}
#endif

static float from_bits(uint32_t bits)
{
	float value;
	memcpy(&value, &bits, sizeof(value));
	return value;
}

static uint32_t bits_of(float value)
{
	uint32_t bits;
	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

/* The existing mixer sums four independent lanes, then the adjacent pairs.
   Separate statements retain coefficient and product rounding without FMA. */
static float reference(const float *frames, const float *weights, const float *deltas, float blend)
{
	float sum[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	unsigned tap, lane;
	for (tap = 0; tap < TAPS; tap += 4)
		for (lane = 0; lane < 4; lane++)
		{
			float weight = weights[tap + lane];
			float product;
			if (deltas)
			{
				float delta = deltas[tap + lane] * blend;
				weight = weight + delta;
			}
			product = frames[tap + lane] * weight;
			sum[lane] = sum[lane] + product;
		}
	return (sum[0] + sum[1]) + (sum[2] + sum[3]);
}

static int check_case(const float *left, const float *right, const float *weights,
	const float *deltas, float blend)
{
	float prepared[2], blended[2];
	float mono[3] = { 123.0f, 0.0f, 456.0f };
	float stereo[4] = { 123.0f, 0.0f, 0.0f, 456.0f };
	prepared[0] = reference(left, weights, 0, blend);
	prepared[1] = reference(right, weights, 0, blend);
	blended[0] = reference(left, weights, deltas, blend);
	blended[1] = reference(right, weights, deltas, blend);
	wiiu_resampler_ps_dot48(left, weights, mono + 1);
	wiiu_resampler_ps_dot48_stereo(left, right, weights, stereo + 1);
	if (bits_of(prepared[0]) != bits_of(mono[1]) || bits_of(prepared[0]) != bits_of(stereo[1]) ||
		bits_of(prepared[1]) != bits_of(stereo[2]) || mono[0] != 123.0f || mono[2] != 456.0f ||
		stereo[0] != 123.0f || stereo[3] != 456.0f)
		return 0;
	wiiu_resampler_ps_blended48(left, weights, deltas, &blend, mono + 1);
	wiiu_resampler_ps_blended48_stereo(left, right, weights, deltas, &blend, stereo + 1);
	return bits_of(blended[0]) == bits_of(mono[1]) && bits_of(blended[0]) == bits_of(stereo[1]) &&
		bits_of(blended[1]) == bits_of(stereo[2]) && mono[0] == 123.0f && mono[2] == 456.0f &&
		stereo[0] == 123.0f && stereo[3] == 456.0f;
}

static int selftest(void)
{
	float left[TAPS + 4] __attribute__((aligned(16)));
	float right[TAPS + 4] __attribute__((aligned(16)));
	float weights[TAPS + 4] __attribute__((aligned(16)));
	float deltas[TAPS + 4] __attribute__((aligned(16)));
	unsigned n, tap;
	unsigned seed = state.fpscr ^ state.gqr0 ^ 0x6148u;
	for (n = 0; n < 64; n++)
	{
		float blend = (float)((n * 19 + seed) & 1023u) * 0x1p-10f;
		unsigned first = n >= 48 && n < 56 ? n & 3u : 0;
		memset(left, 0, sizeof(left)); memset(right, 0, sizeof(right));
		memset(weights, 0, sizeof(weights)); memset(deltas, 0, sizeof(deltas));
		if (n < 48)
		{
			left[n] = 0.75f; right[n] = -0.25f;
			weights[n] = 0.5f; deltas[n] = -0.125f;
		}
		else if (n < 56 || n == 63)
			for (tap = 0; tap < TAPS + 4; tap++)
			{
				seed = seed * 1664525u + 1013904223u;
				left[tap] = (float)((int)(seed & 511u) - 256) * 0x1p-10f;
				right[tap] = (float)((int)((seed >> 9) & 511u) - 256) * 0x1p-10f;
				weights[tap] = (float)((int)((seed >> 18) & 127u) - 64) * 0x1p-12f;
				deltas[tap] = (float)((int)((seed >> 25) & 63u) - 32) * 0x1p-15f;
			}
		else if (n < 60)
		{
			static const uint32_t small[] = { 0x00800000u, 0x00000001u, 0x007fffffu, 0x80000001u };
			left[1] = right[1] = from_bits(small[n - 56]);
			weights[1] = 0.5f; deltas[1] = 0.25f;
		}
		else if (n == 60)
		{
			left[0] = right[0] = 1.0f + 0x1p-13f;
			weights[0] = 1.0f - 0x1p-13f;
			left[4] = right[4] = -1.0f; weights[4] = 1.0f;
		}
		else if (n == 61)
		{
			left[0] = right[0] = 1.0f; weights[0] = -1.0f;
			deltas[0] = 1.0f + 0x1p-13f; blend = 1.0f - 0x1p-13f;
		}
		else
		{
			left[1] = right[1] = from_bits(0x80000000u);
			weights[1] = 1.0f;
		}
		state.selftest_cases++;
		if (!check_case(left + first, right + ((first * 3) & 3u), weights + first, deltas + first, blend))
		{
			state.failed_case = n + 1;
			return 0;
		}
	}
	return 1;
}

static int validate_environment(void)
{
	state.core = read_core();
	if (state.core != 0)
		state.result = WIIU_RESAMPLER_PS_WRONG_CORE;
	else
	{
		state.fpscr = read_fpscr();
		if ((state.fpscr & 0xffu) != QUALIFIED_MODE)
			state.result = WIIU_RESAMPLER_PS_FPSCR_MODE;
		else
		{
			state.gqr0 = read_gqr0();
			if (state.gqr0 & GQR_TYPES)
				state.result = WIIU_RESAMPLER_PS_GQR_TYPE;
			else
				return 1;
		}
	}
	enabled = 0;
	report_pending = 1;
	return 0;
}

int wiiu_resampler_ps_worker_begin(int requested)
{
	if (initialized) return enabled;
	initialized = report_pending = 1;
	state.result = WIIU_RESAMPLER_PS_DISABLED;
	if (!requested || !validate_environment()) return 0;
	if (!selftest())
	{
		state.result = WIIU_RESAMPLER_PS_SELFTEST;
		return 0;
	}
	state.result = WIIU_RESAMPLER_PS_READY;
	enabled = 1;
	return 1;
}

int wiiu_resampler_ps_check_chunk(void)
{
	return enabled && validate_environment();
}

int wiiu_resampler_ps_ready(void)
{
	return enabled;
}

int wiiu_resampler_ps_take_report(struct wiiu_resampler_ps_status *status)
{
	if (!report_pending || !status) return 0;
	*status = state;
	report_pending = 0;
	return 1;
}

const char *wiiu_resampler_ps_reason(unsigned result)
{
	switch (result)
	{
	case WIIU_RESAMPLER_PS_DISABLED: return "disabled";
	case WIIU_RESAMPLER_PS_READY: return "ready";
	case WIIU_RESAMPLER_PS_WRONG_CORE: return "wrong core";
	case WIIU_RESAMPLER_PS_FPSCR_MODE: return "unqualified FPSCR mode";
	case WIIU_RESAMPLER_PS_GQR_TYPE: return "GQR0 is not float";
	case WIIU_RESAMPLER_PS_SELFTEST: return "numerical selftest failed";
	default: return "unknown";
	}
}
