#include "wiiu_atomic.h"

#ifdef WIIU_ATOMIC_HOST_TEST
/* the desktop test: one thread, no interrupts to hold off */
#define ENTER() 0
#define LEAVE(state) ((void)(state))
#else
#include <coreinit/interrupts.h>

static volatile uint32_t lock_word;

static int enter(void)
{
	int state = OSDisableInterrupts();

	while (!__sync_bool_compare_and_swap(&lock_word, 0, 1))
	{
	}
	return state;
}

static void leave(int state)
{
	__sync_lock_release(&lock_word);
	OSRestoreInterrupts(state);
}

#define ENTER() enter()
#define LEAVE(state) leave(state)
#endif

uint64_t wiiu_atomic_load_8(const volatile void *pointer)
{
	const int state = ENTER();
	const uint64_t value = *(const volatile uint64_t *)pointer;

	LEAVE(state);
	return value;
}

void wiiu_atomic_store_8(volatile void *pointer, uint64_t value)
{
	const int state = ENTER();

	*(volatile uint64_t *)pointer = value;
	LEAVE(state);
}

uint64_t wiiu_atomic_exchange_8(volatile void *pointer, uint64_t value)
{
	const int state = ENTER();
	const uint64_t old = *(volatile uint64_t *)pointer;

	*(volatile uint64_t *)pointer = value;
	LEAVE(state);
	return old;
}

bool wiiu_atomic_compare_exchange_8(volatile void *pointer, uint64_t *expected, uint64_t desired)
{
	const int state = ENTER();
	const uint64_t old = *(volatile uint64_t *)pointer;
	const bool equal = old == *expected;

	if (equal)
		*(volatile uint64_t *)pointer = desired;
	else
		*expected = old;
	LEAVE(state);
	return equal;
}

#define FETCH(name, expression) \
	uint64_t wiiu_atomic_fetch_##name##_8(volatile void *pointer, uint64_t value) \
	{ \
		const int state = ENTER(); \
		const uint64_t old = *(volatile uint64_t *)pointer; \
		\
		*(volatile uint64_t *)pointer = (expression); \
		LEAVE(state); \
		return old; \
	}

FETCH(add, old + value)
FETCH(sub, old - value)
FETCH(and, old & value)
FETCH(or, old | value)
FETCH(xor, old ^ value)
FETCH(nand, ~(old & value))

#ifndef WIIU_ATOMIC_HOST_TEST
/* libatomic's names and signatures (the memory orders are ignored) */
uint64_t __atomic_load_8(const volatile void *pointer, int order)
{
	(void)order;
	return wiiu_atomic_load_8(pointer);
}

void __atomic_store_8(volatile void *pointer, uint64_t value, int order)
{
	(void)order;
	wiiu_atomic_store_8(pointer, value);
}

uint64_t __atomic_exchange_8(volatile void *pointer, uint64_t value, int order)
{
	(void)order;
	return wiiu_atomic_exchange_8(pointer, value);
}

bool __atomic_compare_exchange_8(volatile void *pointer, uint64_t *expected, uint64_t desired, int success, int failure)
{
	(void)success;
	(void)failure;
	return wiiu_atomic_compare_exchange_8(pointer, expected, desired);
}

uint64_t __atomic_fetch_add_8(volatile void *pointer, uint64_t value, int order)
{
	(void)order;
	return wiiu_atomic_fetch_add_8(pointer, value);
}

uint64_t __atomic_fetch_sub_8(volatile void *pointer, uint64_t value, int order)
{
	(void)order;
	return wiiu_atomic_fetch_sub_8(pointer, value);
}

uint64_t __atomic_fetch_and_8(volatile void *pointer, uint64_t value, int order)
{
	(void)order;
	return wiiu_atomic_fetch_and_8(pointer, value);
}

uint64_t __atomic_fetch_or_8(volatile void *pointer, uint64_t value, int order)
{
	(void)order;
	return wiiu_atomic_fetch_or_8(pointer, value);
}

uint64_t __atomic_fetch_xor_8(volatile void *pointer, uint64_t value, int order)
{
	(void)order;
	return wiiu_atomic_fetch_xor_8(pointer, value);
}

uint64_t __atomic_fetch_nand_8(volatile void *pointer, uint64_t value, int order)
{
	(void)order;
	return wiiu_atomic_fetch_nand_8(pointer, value);
}
#endif
