#include "maths_check.h"

#include "probe_logic.h"

#include <string.h>

double halo_sin(double x);
double halo_cos(double x);
double halo_tan(double x);
double halo_asin(double x);
double halo_acos(double x);
double halo_atan(double x);
double halo_atan2(double y, double x);
double halo_exp(double x);
double halo_log(double x);
double halo_log2(double x);
double halo_log10(double x);
double halo_pow(double x, double y);

/* a result's bits, most significant byte first, so that the checksum is the same on every byte order */
static uint32_t fold(uint32_t hash, double value)
{
	uint64_t bits = probe_double_bits(value);
	uint8_t bytes[8];
	int i;

	for (i = 0; i < 8; i++)
		bytes[i] = (uint8_t)(bits >> (56 - 8 * i));
	return probe_fnv(hash, bytes, sizeof(bytes));
}

typedef double (*function1)(double);
typedef double (*function2)(double, double);

static uint32_t run_one(function1 f, double low, double high, uint32_t seed)
{
	uint32_t hash = PROBE_FNV_SEED;
	uint32_t state = seed;
	int i;

	for (i = 0; i < MATHS_CHECK_INPUTS; i++)
		hash = fold(hash, f(probe_random_double(&state, low, high)));
	return hash;
}

static uint32_t run_two(function2 f, double low_x, double high_x, double low_y, double high_y, uint32_t seed)
{
	uint32_t hash = PROBE_FNV_SEED;
	uint32_t state = seed;
	int i;

	for (i = 0; i < MATHS_CHECK_INPUTS; i++)
	{
		double x = probe_random_double(&state, low_x, high_x);
		double y = probe_random_double(&state, low_y, high_y);

		hash = fold(hash, f(x, y));
	}
	return hash;
}

const maths_check_expected maths_check_expected_table[MATHS_CHECK_COUNT] = {
	{"sin", 0x488FB497u},
	{"cos", 0x044D739Du},
	{"tan", 0xA263E434u},
	{"asin", 0x8414AAE4u},
	{"acos", 0x845A3EC9u},
	{"atan", 0x0D7DEA87u},
	{"atan2", 0x19EAD003u},
	{"exp", 0x7C3207E9u},
	{"log", 0x55BAED09u},
	{"log2", 0x2F59677Eu},
	{"log10", 0x18E340D1u},
	{"pow", 0x3A6DA4B8u},
};

void maths_check_run(maths_check_result *results, const maths_check_hooks *hooks)
{
	int n = 0;
	uint64_t started = 0;
	int more = 1;

	memset(results, 0, sizeof(maths_check_result) * MATHS_CHECK_COUNT);

#define BEGIN(label) \
	results[n].name = label; \
	if (more) \
	{ \
		started = hooks && hooks->now_ns ? hooks->now_ns() : 0;
#define END \
		results[n].nanoseconds = hooks && hooks->now_ns ? hooks->now_ns() - started : 0; \
		results[n].ran = 1; \
		if (hooks && hooks->keep_going && !hooks->keep_going()) \
			more = 0; \
	} \
	n++;
#define ONE(label, f, low, high) BEGIN(label) results[n].checksum = run_one(f, low, high, 0x1000u + (uint32_t)n); END
#define TWO(label, f, lx, hx, ly, hy) BEGIN(label) results[n].checksum = run_two(f, lx, hx, ly, hy, 0x1000u + (uint32_t)n); END

	ONE("sin", halo_sin, -1000.0, 1000.0)
	ONE("cos", halo_cos, -1000.0, 1000.0)
	ONE("tan", halo_tan, -100.0, 100.0)
	ONE("asin", halo_asin, -1.0, 1.0)
	ONE("acos", halo_acos, -1.0, 1.0)
	ONE("atan", halo_atan, -1000.0, 1000.0)
	TWO("atan2", halo_atan2, -100.0, 100.0, -100.0, 100.0)
	ONE("exp", halo_exp, -50.0, 50.0)
	ONE("log", halo_log, 0.000001, 1000000.0)
	ONE("log2", halo_log2, 0.000001, 1000000.0)
	ONE("log10", halo_log10, 0.000001, 1000000.0)
	TWO("pow", halo_pow, 0.001, 100.0, -10.0, 10.0)
#undef BEGIN
#undef END
#undef ONE
#undef TWO
}
