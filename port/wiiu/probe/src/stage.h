/*
STAGE.H

The probe's stages. Each takes the context, reports into it and returns; none of them stops the
run for what it finds (a failed check is a line in the report), and each asks keep_going() now and
then, so that the HOME button is answered while one is under way.

abi, numeric and maths are the same code on the console and on the desktop (tests/test_stages.c
runs them little-endian and big-endian); system, memory and cpu are the Wii U's own.
*/

#ifndef HALO_WIIU_STAGE_H
#define HALO_WIIU_STAGE_H

#include "report.h"

#include <stdint.h>

typedef struct stage_context
{
	report *report;
	uint64_t (*now_ns)(void); /* a monotonic clock in nanoseconds */
	int (*keep_going)(void);  /* 0 once the user has asked to leave */
	void (*flush)(void);      /* writes the report so far out (may be NULL): before what might not return */
} stage_context;

/* what the port's code assumes of the machine it is built for */
typedef struct stage_abi_expectation
{
	int big_endian;
	unsigned pointer_size;
	unsigned long_size;
	unsigned wchar_size;
	unsigned double_alignment;
	int char_signed;
} stage_abi_expectation;

/* The compiled-in ABI against what the port assumes (byte order, sizes, alignments, bit-field order). */
void stage_abi(stage_context *context, const stage_abi_expectation *expected);

/* The floating-point unit: FPSCR (PowerPC), fused multiply-adds, denormals, conversions. */
void stage_numeric(stage_context *context);

/* A floating-point unit for the numeric stage to ask, instead of the machine's own (tests only). */
void stage_numeric_use_fpu(uint32_t (*read)(void), void (*clear)(void));

/* The FPSCR of the calling thread (0 where there is none: not PowerPC). */
uint32_t stage_read_fpscr(void);

/* The game's maths against the checksums every other machine gets. */
void stage_maths(stage_context *context);

/* Wii U only */
void stage_system(stage_context *context);
void stage_memory(stage_context *context);
void stage_cpu(stage_context *context);
void stage_unaligned(stage_context *context);

#endif
