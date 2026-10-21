#include "wiiu_align_fixup.h"

#include <string.h>

static uint32_t load_word(const uint8_t *memory)
{
	return ((uint32_t)memory[0] << 24) | ((uint32_t)memory[1] << 16) | ((uint32_t)memory[2] << 8) | (uint32_t)memory[3];
}

static void store_word(uint8_t *memory, uint32_t value)
{
	memory[0] = (uint8_t)(value >> 24);
	memory[1] = (uint8_t)(value >> 16);
	memory[2] = (uint8_t)(value >> 8);
	memory[3] = (uint8_t)value;
}

static uint64_t double_bits(double value)
{
	uint64_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static double bits_double(uint64_t bits)
{
	double value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

/* What stfs stores for a double. In the range a single has (the exponent field of the double 897 to 1150 and below, down to the
denormals) it is the value narrowed to nearest, FPSCR RN being 0. Above it, and for infinities and NaNs, the architecture gives the
bits of the double as they come: the sign, the exponent's top bit, its low seven, and the top 23 of the fraction (an infinity and the
usual NaNs come out right by this; a finite value too large for a single does not become an infinity, which the hardware confirmed
in the probe's test and an emulator copies). */
static uint32_t double_to_single_bits(double value)
{
	const uint64_t bits = double_bits(value);
	const uint32_t exponent = (uint32_t)((bits >> 52) & 0x7FF);

	if (exponent > 1150)
		return (uint32_t)((bits >> 63) << 31) | (((exponent >> 10) & 1u) << 30) | ((exponent & 0x7Fu) << 23) |
			(uint32_t)((bits & 0xFFFFFFFFFFFFFull) >> 29);
	{
		const float single = (float)value;
		uint32_t result;

		memcpy(&result, &single, sizeof(result));
		return result;
	}
}

typedef enum kind
{
	NONE,
	LOAD_SINGLE,
	LOAD_DOUBLE,
	STORE_SINGLE,
	STORE_DOUBLE,
	STORE_INTEGER_WORD
} kind;

int wiiu_align_fixup(uint32_t instruction, uintptr_t address, uint32_t *gpr, double *fpr)
{
	const uint32_t primary = instruction >> 26;
	const uint32_t register_t = (instruction >> 21) & 31;
	const uint32_t register_a = (instruction >> 16) & 31;
	uint8_t *memory = (uint8_t *)address;
	kind what = NONE;
	int update = 0;

	switch (primary)
	{
	case 48: what = LOAD_SINGLE; break;                    /* lfs */
	case 49: what = LOAD_SINGLE; update = 1; break;        /* lfsu */
	case 50: what = LOAD_DOUBLE; break;                    /* lfd */
	case 51: what = LOAD_DOUBLE; update = 1; break;        /* lfdu */
	case 52: what = STORE_SINGLE; break;                   /* stfs */
	case 53: what = STORE_SINGLE; update = 1; break;       /* stfsu */
	case 54: what = STORE_DOUBLE; break;                   /* stfd */
	case 55: what = STORE_DOUBLE; update = 1; break;       /* stfdu */
	case 31:
		switch ((instruction >> 1) & 0x3FF)
		{
		case 535: what = LOAD_SINGLE; break;               /* lfsx */
		case 567: what = LOAD_SINGLE; update = 1; break;   /* lfsux */
		case 599: what = LOAD_DOUBLE; break;               /* lfdx */
		case 631: what = LOAD_DOUBLE; update = 1; break;   /* lfdux */
		case 663: what = STORE_SINGLE; break;              /* stfsx */
		case 695: what = STORE_SINGLE; update = 1; break;  /* stfsux */
		case 727: what = STORE_DOUBLE; break;              /* stfdx */
		case 759: what = STORE_DOUBLE; update = 1; break;  /* stfdux */
		case 983: what = STORE_INTEGER_WORD; break;        /* stfiwx */
		default: return 0;
		}
		/* (the indexed forms are opcode 31 with the Rc bit clear) */
		if (instruction & 1)
			return 0;
		break;
	default:
		return 0;
	}
	/* an update form with rA = 0 is an invalid form, and a load with it the same; the hardware would not have done it */
	if (update && register_a == 0)
		return 0;

	switch (what)
	{
	case LOAD_SINGLE:
	{
		/* a single widened to a double is exact, denormals included */
		const uint32_t bits = load_word(memory);
		float single;

		memcpy(&single, &bits, sizeof(single));
		fpr[register_t] = (double)single;
		break;
	}
	case LOAD_DOUBLE:
		fpr[register_t] = bits_double(((uint64_t)load_word(memory) << 32) | load_word(memory + 4));
		break;
	case STORE_SINGLE:
		store_word(memory, double_to_single_bits(fpr[register_t]));
		break;
	case STORE_DOUBLE:
	{
		const uint64_t bits = double_bits(fpr[register_t]);

		store_word(memory, (uint32_t)(bits >> 32));
		store_word(memory + 4, (uint32_t)bits);
		break;
	}
	case STORE_INTEGER_WORD:
		store_word(memory, (uint32_t)double_bits(fpr[register_t]));
		break;
	default:
		return 0;
	}
	if (update)
		gpr[register_a] = (uint32_t)address;
	return 1;
}
