/*
WIIU_ATOMIC.H

The 750-class core has no 64-bit atomic instructions (lwarx and stwcx. are 32 bits), so a 64-bit atomic access compiles to a call
of the libatomic function of that size, and devkitPPC has no libatomic. These are those, for 8 bytes: a lock around plain
accesses, held with interrupts off on the core that takes it (a thread preempted in the middle could never otherwise be told from
one that is taking its time) and spun for, for the few instructions it takes, by a thread on another core.

The functions are in the libatomic ABI (the memory-order arguments are ignored: the lock is a full barrier). Everything else
(1, 2 and 4 byte atomics) is inline.
*/

#ifndef HALO_WIIU_ATOMIC_H
#define HALO_WIIU_ATOMIC_H

#include <stdbool.h>
#include <stdint.h>

/* the operations by the names they have where they can be tested (wiiu_atomic.c gives them libatomic's names on the console) */
uint64_t wiiu_atomic_load_8(const volatile void *pointer);
void wiiu_atomic_store_8(volatile void *pointer, uint64_t value);
uint64_t wiiu_atomic_exchange_8(volatile void *pointer, uint64_t value);
bool wiiu_atomic_compare_exchange_8(volatile void *pointer, uint64_t *expected, uint64_t desired);
uint64_t wiiu_atomic_fetch_add_8(volatile void *pointer, uint64_t value);
uint64_t wiiu_atomic_fetch_sub_8(volatile void *pointer, uint64_t value);
uint64_t wiiu_atomic_fetch_and_8(volatile void *pointer, uint64_t value);
uint64_t wiiu_atomic_fetch_or_8(volatile void *pointer, uint64_t value);
uint64_t wiiu_atomic_fetch_xor_8(volatile void *pointer, uint64_t value);
uint64_t wiiu_atomic_fetch_nand_8(volatile void *pointer, uint64_t value);

#endif
