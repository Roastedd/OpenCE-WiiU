/*
MATHS_CHECK.H

The game's maths functions (musl's, as halo_sin and so on: port/third_party/musl-math) over a fixed
set of inputs, each function's results folded into one checksum that does not depend on the
machine's byte order. Every machine of a network game must get these to the last bit; the checksums
are the same on x86, ARM64 and big-endian PowerPC (tools/wiiu/probe_tests.sh), and the probe asks
the Wii U's Espresso for them.
*/

#ifndef HALO_WIIU_MATHS_CHECK_H
#define HALO_WIIU_MATHS_CHECK_H

#include <stddef.h>
#include <stdint.h>

#define MATHS_CHECK_COUNT 12
#define MATHS_CHECK_INPUTS 100000

typedef struct maths_check_result
{
	const char *name;
	uint32_t checksum;
	uint64_t nanoseconds;
	int ran;
} maths_check_result;

typedef struct maths_check_hooks
{
	uint64_t (*now_ns)(void); /* a monotonic clock for the timings; NULL for none (they are 0) */
	int (*keep_going)(void);  /* asked between functions; 0 stops the run (the rest have ran 0); may be NULL */
} maths_check_hooks;

/* Runs every function; results has MATHS_CHECK_COUNT entries. hooks may be NULL. */
void maths_check_run(maths_check_result *results, const maths_check_hooks *hooks);

/* what the checksums should be, from tools/wiiu/maths_reference.sh */
typedef struct maths_check_expected
{
	const char *name;
	uint32_t checksum;
} maths_check_expected;

extern const maths_check_expected maths_check_expected_table[MATHS_CHECK_COUNT];

#endif
