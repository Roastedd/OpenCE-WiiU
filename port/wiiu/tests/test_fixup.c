/*
The misaligned float access emulation (src/wiiu_align_fixup.c) on known answers: all the forms, at every misalignment, against
what the instruction means, on 64-bit ARM, 32-bit x86 and 32-bit big-endian PowerPC (tools/wiiu/shell_tests.sh). On PowerPC the
same instructions are also run for real, at aligned addresses where the hardware does them, and the emulation must agree with it
to the bit. (Where the hardware cannot do them an emulator is what is wanted; on the console the probe checks the emulation at
the addresses that raise the exception.)
*/

#include "../src/wiiu_align_fixup.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("check failed, line %d: %s\n", __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

/* instruction words */
#define D_FORM(opcode, t, a) (((uint32_t)(opcode) << 26) | ((uint32_t)(t) << 21) | ((uint32_t)(a) << 16))
#define X_FORM(xo, t, a, b) ((31u << 26) | ((uint32_t)(t) << 21) | ((uint32_t)(a) << 16) | ((uint32_t)(b) << 11) | ((uint32_t)(xo) << 1))

static uint64_t bits_of(double value)
{
	uint64_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static double bits_double_from(uint64_t bits)
{
	double value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

static uint32_t single_bits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static void test_loads_and_stores_at_every_misalignment(void)
{
	/* memory holds big-endian values: 1.5f, then -2.25 as a double, from a given offset */
	static uint8_t memory[64];
	uint32_t gpr[32];
	double fpr[32];
	unsigned offset;

	for (offset = 0; offset < 8; offset++)
	{
		const uint32_t one_and_a_half = 0x3FC00000u;
		const uint64_t minus_two_and_a_quarter = 0xC002000000000000ull;
		uint32_t word;

		memset(memory, 0xAA, sizeof(memory));
		memory[offset + 0] = (uint8_t)(one_and_a_half >> 24);
		memory[offset + 1] = (uint8_t)(one_and_a_half >> 16);
		memory[offset + 2] = (uint8_t)(one_and_a_half >> 8);
		memory[offset + 3] = (uint8_t)one_and_a_half;
		for (word = 0; word < 8; word++)
			memory[offset + 8 + word] = (uint8_t)(minus_two_and_a_quarter >> (56 - 8 * word));
		memset(gpr, 0, sizeof(gpr));
		memset(fpr, 0, sizeof(fpr));

		/* lfs f3, 0(r4) */
		gpr[4] = (uint32_t)(uintptr_t)(memory + offset);
		CHECK(wiiu_align_fixup(D_FORM(48, 3, 4), (uintptr_t)(memory + offset), gpr, fpr) == 1);
		CHECK(fpr[3] == 1.5 && gpr[4] == (uint32_t)(uintptr_t)(memory + offset));
		/* lfd f5, 0(r4) */
		CHECK(wiiu_align_fixup(D_FORM(50, 5, 4), (uintptr_t)(memory + offset + 8), gpr, fpr) == 1);
		CHECK(bits_of(fpr[5]) == minus_two_and_a_quarter);
		/* the update forms set rA to the address; the indexed ones too */
		gpr[6] = 0;
		CHECK(wiiu_align_fixup(D_FORM(49, 7, 6), (uintptr_t)(memory + offset), gpr, fpr) == 1 && gpr[6] == (uint32_t)(uintptr_t)(memory + offset));
		gpr[6] = 0;
		CHECK(wiiu_align_fixup(D_FORM(51, 7, 6), (uintptr_t)(memory + offset + 8), gpr, fpr) == 1 && gpr[6] == (uint32_t)(uintptr_t)(memory + offset + 8));
		CHECK(wiiu_align_fixup(X_FORM(535, 8, 4, 9), (uintptr_t)(memory + offset), gpr, fpr) == 1 && fpr[8] == 1.5);
		CHECK(wiiu_align_fixup(X_FORM(599, 8, 4, 9), (uintptr_t)(memory + offset + 8), gpr, fpr) == 1 && bits_of(fpr[8]) == minus_two_and_a_quarter);
		gpr[6] = 0;
		CHECK(wiiu_align_fixup(X_FORM(567, 8, 6, 9), (uintptr_t)(memory + offset), gpr, fpr) == 1 && gpr[6] == (uint32_t)(uintptr_t)(memory + offset));
		gpr[6] = 0;
		CHECK(wiiu_align_fixup(X_FORM(631, 8, 6, 9), (uintptr_t)(memory + offset + 8), gpr, fpr) == 1 && gpr[6] == (uint32_t)(uintptr_t)(memory + offset + 8));

		/* stores write big-endian bytes at the address and nothing around it */
		memset(memory, 0x55, sizeof(memory));
		fpr[10] = 0.1;
		CHECK(wiiu_align_fixup(D_FORM(52, 10, 4), (uintptr_t)(memory + offset), gpr, fpr) == 1);
		{
			const uint32_t bits = single_bits((float)0.1);

			CHECK(memory[offset] == (uint8_t)(bits >> 24) && memory[offset + 1] == (uint8_t)(bits >> 16) &&
				memory[offset + 2] == (uint8_t)(bits >> 8) && memory[offset + 3] == (uint8_t)bits);
			CHECK((offset == 0 || memory[offset - 1] == 0x55) && memory[offset + 4] == 0x55);
		}
		memset(memory, 0x55, sizeof(memory));
		fpr[11] = -2.25;
		CHECK(wiiu_align_fixup(D_FORM(54, 11, 4), (uintptr_t)(memory + offset), gpr, fpr) == 1);
		CHECK(memory[offset] == 0xC0 && memory[offset + 1] == 0x02 && memory[offset + 2] == 0 && memory[offset + 7] == 0);
		CHECK((offset == 0 || memory[offset - 1] == 0x55) && memory[offset + 8] == 0x55);
		/* stfiwx: the low word of the register, whatever it holds as a double */
		memset(memory, 0x55, sizeof(memory));
		fpr[12] = bits_double_from(0x1122334455667788ull);
		CHECK(wiiu_align_fixup(X_FORM(983, 12, 4, 9), (uintptr_t)(memory + offset), gpr, fpr) == 1);
		CHECK(memory[offset] == 0x55 && memory[offset + 1] == 0x66 && memory[offset + 2] == 0x77 && memory[offset + 3] == 0x88);
		/* an update store sets rA */
		gpr[13] = 0;
		CHECK(wiiu_align_fixup(D_FORM(55, 11, 13), (uintptr_t)(memory + offset), gpr, fpr) == 1 && gpr[13] == (uint32_t)(uintptr_t)(memory + offset));
	}
}

static void test_conversions(void)
{
	static uint8_t memory[16];
	uint32_t gpr[32] = {0};
	double fpr[32] = {0};

	/* a single that is a denormal widens exactly (the hardware does not flush it: the non-IEEE bit is cleared in game threads) */
	memory[3] = 1;
	CHECK(wiiu_align_fixup(D_FORM(48, 1, 4), (uintptr_t)memory, gpr, fpr) == 1);
	CHECK(bits_of(fpr[1]) == 0x36A0000000000000ull);
	/* infinity and a NaN keep their kind */
	memory[0] = 0x7F; memory[1] = 0x80; memory[2] = 0; memory[3] = 0;
	CHECK(wiiu_align_fixup(D_FORM(48, 1, 4), (uintptr_t)memory, gpr, fpr) == 1 && fpr[1] > 1e308);
	memory[0] = 0x7F; memory[1] = 0xC0;
	CHECK(wiiu_align_fixup(D_FORM(48, 1, 4), (uintptr_t)memory, gpr, fpr) == 1 && fpr[1] != fpr[1]);

	/* a store narrows to nearest even, overflows to infinity, and keeps a NaN a NaN */
	fpr[2] = 16777217.0; /* 2^24 + 1: halfway between two singles, rounds to even */
	CHECK(wiiu_align_fixup(D_FORM(52, 2, 4), (uintptr_t)(memory + 1), gpr, fpr) == 1);
	CHECK(memory[1] == 0x4B && memory[2] == 0x80 && memory[3] == 0 && memory[4] == 0);
	fpr[2] = bits_double_from(0x7FF0000000000000ull);
	CHECK(wiiu_align_fixup(D_FORM(52, 2, 4), (uintptr_t)(memory + 1), gpr, fpr) == 1);
	CHECK(memory[1] == 0x7F && memory[2] == 0x80 && memory[3] == 0 && memory[4] == 0);
	/* a finite double too large for a single is not an infinity: the architecture's truncated form (checked against the hardware
	on PowerPC below) */
	fpr[2] = 1e300;
	CHECK(wiiu_align_fixup(D_FORM(52, 2, 4), (uintptr_t)(memory + 1), gpr, fpr) == 1);
	CHECK(memory[1] == 0x71 && memory[2] == 0xBF && memory[3] == 0x21 && memory[4] == 0xE4);
	fpr[2] = bits_double_from(0x7FF8000000000000ull);
	CHECK(wiiu_align_fixup(D_FORM(52, 2, 4), (uintptr_t)(memory + 1), gpr, fpr) == 1);
	CHECK(memory[1] == 0x7F && (memory[2] & 0xC0) == 0xC0);
}

static void test_what_it_does_not_do(void)
{
	static uint8_t memory[16];
	uint32_t gpr[32] = {0};
	double fpr[32] = {0};
	const uintptr_t address = (uintptr_t)memory;

	CHECK(wiiu_align_fixup(D_FORM(32, 3, 4), address, gpr, fpr) == 0);         /* lwz: integer, not this */
	CHECK(wiiu_align_fixup(D_FORM(56, 3, 4), address, gpr, fpr) == 0);         /* psq_l: paired singles */
	CHECK(wiiu_align_fixup(D_FORM(46, 3, 4), address, gpr, fpr) == 0);         /* lmw */
	CHECK(wiiu_align_fixup(X_FORM(20, 3, 4, 5), address, gpr, fpr) == 0);      /* lwarx */
	CHECK(wiiu_align_fixup(X_FORM(535, 3, 4, 5) | 1, address, gpr, fpr) == 0); /* an indexed form with Rc set is not one */
	CHECK(wiiu_align_fixup(D_FORM(49, 3, 0), address, gpr, fpr) == 0);         /* an update form with rA = 0 is invalid */
	CHECK(wiiu_align_fixup(X_FORM(631, 3, 0, 5), address, gpr, fpr) == 0);
	CHECK(wiiu_align_fixup(0, address, gpr, fpr) == 0);
	/* and a refused instruction touched nothing */
	CHECK(memory[0] == 0 && gpr[3] == 0 && fpr[3] == 0.0);
}

#if defined(__PPC__) || defined(__powerpc__)
/* the real instructions, at an address where the hardware does them: the emulation must give the same bits */
static void test_against_the_hardware(void)
{
	static uint8_t memory[64] __attribute__((aligned(16)));
	static const uint8_t patterns[][8] = {
		{0x3F, 0xC0, 0, 0, 0xC0, 0x02, 0, 0}, {0x00, 0, 0, 1, 0x80, 0, 0, 0}, {0x7F, 0x80, 0, 0, 0x7F, 0xF0, 0, 0},
		{0x7F, 0xC0, 0, 1, 0x7F, 0xF8, 0, 1}, {0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x10, 0, 0}, {0x80, 0, 0, 0, 0x80, 0, 0, 0},
		{0x33, 0x33, 0x33, 0x33, 0x3F, 0xB9, 0x99, 0x9A}, {0x7E, 0xFF, 0xFF, 0xFF, 0x47, 0xEF, 0xFF, 0xFF},
	};
	unsigned p;

	for (p = 0; p < sizeof(patterns) / sizeof(patterns[0]); p++)
	{
		uint32_t gpr[32] = {0};
		double fpr[32] = {0};
		double real_single = 0.0, real_double = 0.0;
		uint8_t *at = memory + 8;

		memcpy(at, patterns[p], 8);
		__asm__ volatile("lfs %0, 0(%1)" : "=f"(real_single) : "b"(at) : "memory");
		__asm__ volatile("lfd %0, 0(%1)" : "=f"(real_double) : "b"(at) : "memory");
		CHECK(wiiu_align_fixup(D_FORM(48, 1, 4), (uintptr_t)at, gpr, fpr) == 1);
		CHECK(bits_of(fpr[1]) == bits_of(real_single) || (fpr[1] != fpr[1] && real_single != real_single));
		CHECK(wiiu_align_fixup(D_FORM(50, 2, 4), (uintptr_t)at, gpr, fpr) == 1);
		CHECK(bits_of(fpr[2]) == bits_of(real_double));
		{
			/* the stores: the same register through the hardware's store and ours, into two places */
			uint8_t *by_hardware = memory + 24, *by_us = memory + 40;

			memset(memory + 16, 0, 40);
			__asm__ volatile("stfs %0, 0(%1)" : : "f"(real_double), "b"(by_hardware) : "memory");
			fpr[3] = real_double;
			CHECK(wiiu_align_fixup(D_FORM(52, 3, 4), (uintptr_t)by_us, gpr, fpr) == 1);
			if (memcmp(by_hardware, by_us, 4))
				printf("stfs differs for pattern %u: double %016llX, hardware %02X%02X%02X%02X, ours %02X%02X%02X%02X\n", p,
					(unsigned long long)bits_of(real_double), by_hardware[0], by_hardware[1], by_hardware[2], by_hardware[3],
					by_us[0], by_us[1], by_us[2], by_us[3]);
			CHECK(!memcmp(by_hardware, by_us, 4));
			memset(memory + 16, 0, 40);
			__asm__ volatile("stfd %0, 0(%1)" : : "f"(real_double), "b"(by_hardware) : "memory");
			CHECK(wiiu_align_fixup(D_FORM(54, 3, 4), (uintptr_t)by_us, gpr, fpr) == 1);
			CHECK(!memcmp(by_hardware, by_us, 8));
			memset(memory + 16, 0, 40);
			__asm__ volatile("stfiwx %0, 0, %1" : : "f"(real_double), "r"(by_hardware) : "memory");
			CHECK(wiiu_align_fixup(X_FORM(983, 3, 0, 4), (uintptr_t)by_us, gpr, fpr) == 1);
			CHECK(!memcmp(by_hardware, by_us, 4));
		}
	}
}
#endif

int main(void)
{
	test_loads_and_stores_at_every_misalignment();
	test_conversions();
	test_what_it_does_not_do();
#if defined(__PPC__) || defined(__powerpc__)
	test_against_the_hardware();
#endif
	if (failures)
	{
		printf("%d checks failed\n", failures);
		return 1;
	}
	printf("fix-up tests passed\n");
	return 0;
}
