/* periodic_function_evaluate (source/math/periodic_functions.c) as the Wii U has it (HALO_WIIU: for a time of 0 to 2^31 the fraction and the index come from the
integer part, without fmod and the rounding call) against the original, each extracted from the source by tools/test_wiiu_periodic_exact.py into periodic.inc and
compiled twice. Every function type, with tables of random bytes, on times of every kind: ordinary, negative, zeros of both signs, subnormal, around 2^23, 2^24 and the
2^31 limit of the fast path, infinities and NaNs; the same bits every time. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef float real;
typedef unsigned char byte;
typedef unsigned char boolean;
#define TRUE 1
#define FALSE 0
#define FLAG(b) (1 << (b))
#define CEILING(n, ceiling) ((n) > (ceiling) ? (ceiling) : (n))
#define PIN(n, floor, ceiling) ((n) < (floor) ? (floor) : CEILING((n), (ceiling)))
#define TEST_FLAG(flags, bit) (((flags) & (unsigned)FLAG(bit)) != 0)
#define VALID_INDEX(index, count) (index >= 0 && index < count)
#define match_assert(file, line, condition) ((void)0)

enum
{
	_periodic_function_one = 0, _periodic_function_zero, _periodic_function_cosine, _periodic_function_cosine_variable_period, _periodic_function_diagonal_wave,
	_periodic_function_diagonal_wave_variable_period, _periodic_function_slide, _periodic_function_slide_variable_period, _periodic_function_noise,
	_periodic_function_jitter, _periodic_function_wander, _periodic_function_spark, NUMBER_OF_PERIODIC_FUNCTIONS,
};
enum
{
	PERIODIC_FUNCTION_TABLE_SIZE = 1024,
	PERIODIC_FUNCTION_TABLE_MASK = PERIODIC_FUNCTION_TABLE_SIZE - 1,
};
enum
{
	SLIDE_PERIODIC_FUNCTION_FLAGS = FLAG(_periodic_function_slide) | FLAG(_periodic_function_slide_variable_period),
};

static boolean function_tables_initialized = TRUE;
static byte *periodic_function_tables[NUMBER_OF_PERIODIC_FUNCTIONS];
static void function_type_error(char const *kind, short type)
{
	(void)kind;
	(void)type;
}

/* cseries.h's, as the PowerPC build has it: FISTP rounds to nearest */
static __inline long fast_ftol(real value)
{
	return (long)__builtin_rint((double)value);
}

#define HALO_WIIU 1
#define periodic_function_evaluate fast_evaluate
#include "periodic.inc"
#undef periodic_function_evaluate
#undef HALO_WIIU
#define periodic_function_evaluate ref_evaluate
#include "periodic.inc"
#undef periodic_function_evaluate

static uint64_t rng = 0x9e3779b97f4a7c15ull;
static uint32_t next32(void)
{
	rng ^= rng << 13;
	rng ^= rng >> 7;
	rng ^= rng << 17;
	return (uint32_t)(rng >> 16);
}
static float from_bits(uint32_t bits)
{
	float f;

	memcpy(&f, &bits, sizeof(f));
	return f;
}
static uint32_t bits_of(float f)
{
	uint32_t bits;

	memcpy(&bits, &f, sizeof(bits));
	return bits;
}

/* a time of one kind: the picked kind decides how it is made */
static float make_time(void)
{
	const float special[] = { 0.0f, -0.0f, 1.0f, -1.0f, 0.5f, 1e-45f, -1e-45f, 1e-38f, 8388607.5f, 8388608.0f, 8388609.0f, 16777215.0f, 16777216.0f, 83886.07f, 2147483520.0f,
		2147483648.0f, 2147483904.0f, -2147483648.0f, 3.4e38f, -3.4e38f, INFINITY, -INFINITY, NAN, 0.99999994f, 1.00000012f, 39.0625f, 0.0390625f };
	const uint32_t kind = next32() % 8;

	switch (kind)
	{
	case 0: return special[next32() % (sizeof(special) / sizeof(special[0]))];
	case 1: return (float)(next32() % 100000) / 64.0f; /* quarter seconds, ordinary game times over 25.6 */
	case 2: return (float)(next32() % 4000000) * 0.0001f;
	case 3: return -(float)(next32() % 100000) / 64.0f;
	case 4: return from_bits(next32()); /* any bit pattern: every exponent, NaNs */
	case 5: return from_bits(0x4b000000u + (next32() % 64) - 32); /* around 2^23 */
	case 6: return from_bits(0x4f000000u + (next32() % 64) - 32); /* around 2^31 */
	default: return (float)(next32() % 1000000) / 25.6f + (float)(next32() % 1000) / 1000.0f;
	}
}

int main(void)
{
	long cases = 0, differences = 0, fast_path = 0;
	int type, i;
	long n;

	for (type = 0; type < NUMBER_OF_PERIODIC_FUNCTIONS; type++)
	{
		static byte tables[NUMBER_OF_PERIODIC_FUNCTIONS][PERIODIC_FUNCTION_TABLE_SIZE];

		for (i = 0; i < PERIODIC_FUNCTION_TABLE_SIZE; i++)
			tables[type][i] = (byte)next32();
		periodic_function_tables[type] = tables[type];
	}
	for (n = 0; n < 60000000L; n++)
	{
		const float time = make_time();
		const short type = (short)(next32() % (NUMBER_OF_PERIODIC_FUNCTIONS + 4)) - 2; /* two past each end of the types as well */
		const float a = fast_evaluate(type, time);
		const float b = ref_evaluate(type, time);

		cases++;
		fast_path += (time * 25.6f >= 0.0f && time * 25.6f < 2147483648.0f);
		if (bits_of(a) != bits_of(b))
		{
			if (differences++ < 10)
				printf("DIFFERENT: type %d time %a (%g): %a against %a\n", type, time, time, a, b);
		}
	}
	printf("%ld cases (%ld on the fast path), %ld differences\n", cases, fast_path, differences);
	if (differences)
		return 1;
	printf("every case matches\n");
	return 0;
}
