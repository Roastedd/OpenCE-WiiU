/*
The 64-bit atomic operations (port/wiiu/src/wiiu_atomic.c, built with WIIU_ATOMIC_HOST_TEST: no interrupts to hold off) on known
answers, on the three machines: tools/wiiu/shell_tests.sh. The values have bits in both words, because the operations are on
64-bit pairs of 32-bit registers.
*/

#include "../src/wiiu_atomic.h"

#include <stdio.h>

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

int main(void)
{
	volatile uint64_t cell = 0x0123456789ABCDEFull;
	uint64_t expected;

	CHECK(wiiu_atomic_load_8(&cell) == 0x0123456789ABCDEFull);
	wiiu_atomic_store_8(&cell, 0xFFFFFFFF00000001ull);
	CHECK(cell == 0xFFFFFFFF00000001ull);
	CHECK(wiiu_atomic_exchange_8(&cell, 0x00000001FFFFFFFFull) == 0xFFFFFFFF00000001ull && cell == 0x00000001FFFFFFFFull);

	/* a compare that holds stores; one that does not leaves the cell and tells what was there */
	expected = 0x00000001FFFFFFFFull;
	CHECK(wiiu_atomic_compare_exchange_8(&cell, &expected, 7) && cell == 7 && expected == 0x00000001FFFFFFFFull);
	expected = 8;
	CHECK(!wiiu_atomic_compare_exchange_8(&cell, &expected, 9) && cell == 7 && expected == 7);

	/* each returns what was there; the carry crosses the words */
	cell = 0x00000000FFFFFFFFull;
	CHECK(wiiu_atomic_fetch_add_8(&cell, 1) == 0x00000000FFFFFFFFull && cell == 0x0000000100000000ull);
	CHECK(wiiu_atomic_fetch_sub_8(&cell, 1) == 0x0000000100000000ull && cell == 0x00000000FFFFFFFFull);
	cell = 0xF0F0F0F00F0F0F0Full;
	CHECK(wiiu_atomic_fetch_and_8(&cell, 0xFF00FF00FF00FF00ull) == 0xF0F0F0F00F0F0F0Full && cell == 0xF000F0000F000F00ull);
	CHECK(wiiu_atomic_fetch_or_8(&cell, 0x000F000F000F000Full) == 0xF000F0000F000F00ull && cell == 0xF00FF00F0F0F0F0Full);
	CHECK(wiiu_atomic_fetch_xor_8(&cell, 0xFFFFFFFFFFFFFFFFull) == 0xF00FF00F0F0F0F0Full && cell == 0x0FF00FF0F0F0F0F0ull);
	CHECK(wiiu_atomic_fetch_nand_8(&cell, 0x00000000FFFFFFFFull) == 0x0FF00FF0F0F0F0F0ull && cell == 0xFFFFFFFF0F0F0F0Full);

	if (failures)
	{
		printf("%d checks failed\n", failures);
		return 1;
	}
	printf("atomic tests passed\n");
	return 0;
}
