/* wiiu_sqrtf.c: the same float as a correctly rounded sqrtf for every one of the 2^32 inputs (NaNs: a NaN, and newlib's own payload rule, x * x + x).
Build with -ffp-contract=off, as the game is: cc -O2 -ffp-contract=off -DWIIU_SQRTF_TEST test_sqrtf.c ../src/wiiu_sqrtf.c -lm */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

float wiiu_sqrtf(float x);

int main(int argc, char **argv)
{
	uint64_t index, failures = 0, step = argc > 1 ? 7919 : 1;

	for (index = 0; index <= 0xffffffffu; index += step)
	{
		const uint32_t bits = (uint32_t)index;
		float x, got, want;
		uint32_t got_bits, want_bits;

		memcpy(&x, &bits, 4);
		got = wiiu_sqrtf(x);
		want = x != x ? x * x + x : x < 0.0f ? (x - x) / (x - x) : sqrtf(x);
		memcpy(&got_bits, &got, 4);
		memcpy(&want_bits, &want, 4);
		if (got_bits != want_bits && !(got != got && want != want))
		{
			if (failures++ < 10)
				printf("FAIL %08x: %08x, wanted %08x\n", bits, got_bits, want_bits);
		}
	}
	printf(failures ? "sqrtf: %llu failures\n" : "sqrtf: every input matches\n", (unsigned long long)failures);
	return failures != 0;
}
