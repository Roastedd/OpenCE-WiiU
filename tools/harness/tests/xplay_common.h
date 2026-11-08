/* xplay_common.h: what the cross-architecture wire drivers (xplay_*.c, tools/harness/xplay.py) share. They are compiled as the
game's own units are (C89, the port's flags), for 32-bit x86 and for 32-bit PowerPC, and print one line a message; the lines are
what the committed golden files hold. */

#ifndef XPLAY_COMMON_H
#define XPLAY_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

/* (the port's stdio.h names its own printf and the others, which the platform layer defines and these drivers do not link:
the C library's) */
#undef printf
#undef fprintf
#undef sprintf
#undef snprintf
#undef vsnprintf
#undef vprintf
#undef vfprintf
#undef vsprintf
#undef fopen

/* a scalar, or a run of them, of a structure: tools/harness/xplay_layouts.py's tables */
struct xplay_leaf
{
	long offset;
	long width;
	long count;
};

struct xplay_structure
{
	const char *name;
	long size;
	const struct xplay_leaf *leaves;
	long leaf_count;
};

#define XPLAY_FAIL_STATUS 1

/* ---------- values */

/* a value that is the same on every machine and has no two equal bytes where it has two or more, so that a swap, or a
swap that was not made, is seen, and that is no floating point NaN (a copy through the floating point unit would quiet it) */
static unsigned long xplay_mix(unsigned long n)
{
	n = n * 2654435761UL + 0x9E3779B9UL;
	n ^= n >> 15;
	n *= 0x85EBCA6BUL;
	n ^= n >> 13;
	n *= 0xC2B2AE35UL;
	n ^= n >> 16;
	return n;
}

/* the value of a width of 1, 2, 4 or 8 bytes for a seed: its bytes (least significant first) in value[0..width-1] */
static void xplay_value(unsigned char *value, long width, unsigned long seed)
{
	unsigned long low = xplay_mix(seed), high = xplay_mix(seed + 0x51ED27UL);
	long index;

	for (index = 0; index < 4; index++)
	{
		value[index] = (unsigned char)(low >> (8 * index));
		value[4 + index] = (unsigned char)(high >> (8 * index));
	}
	if (width >= 2)
	{
		/* (neither half of a pair alike, and a value that is not its own swap) */
		if (value[0] == value[width - 1])
			value[width - 1] ^= 0x5A;
		if (width >= 4 && value[1] == value[2])
			value[2] ^= 0xA5;
		if (width >= 4)
			value[width - 1] = (unsigned char)(value[width - 1] & 0x3F);
		if (width == 8)
		{
			if (value[4] == value[5])
				value[5] ^= 0x3C;
		}
	}
}

/* the bytes of value[0..width-1], least significant first, as a number in this machine's order at place */
static void xplay_store_native(unsigned char *place, const unsigned char *value, long width)
{
	long index;

	switch (width)
	{
	case 1:
		place[0] = value[0];
		break;
	case 2:
	{
		unsigned short number = (unsigned short)(value[0] | value[1] << 8);

		memcpy(place, &number, 2);
		break;
	}
	case 4:
	{
		unsigned long number = (unsigned long)value[0] | (unsigned long)value[1] << 8 | (unsigned long)value[2] << 16 |
			(unsigned long)value[3] << 24;

		memcpy(place, &number, 4);
		break;
	}
	case 8:
	{
		unsigned long long number = 0;

		for (index = 7; index >= 0; index--)
			number = number << 8 | value[index];
		memcpy(place, &number, 8);
		break;
	}
	default:
		memcpy(place, value, (size_t)width);
		break;
	}
}

/* ---------- text */

static void xplay_hex(const char *name, const void *data, long size)
{
	const unsigned char *bytes = data;
	long index;

	printf("%s %ld ", name, size);
	if (size > 1024)
	{
		/* (a long record: its first 64 bytes, and a 64-bit FNV-1a of all of it) */
		unsigned long long hash = 0xCBF29CE484222325ULL;

		for (index = 0; index < size; index++)
			hash = (hash ^ bytes[index]) * 0x100000001B3ULL;
		for (index = 0; index < 64; index++)
			printf("%02x", bytes[index]);
		printf(" fnv1a64=%08lx%08lx", (unsigned long)(hash >> 32), (unsigned long)(hash & 0xFFFFFFFFUL));
	}
	else
		for (index = 0; index < size; index++)
			printf("%02x", bytes[index]);
	printf("\n");
}

static int xplay_failures;

#define XPLAY_CHECK(condition, ...) \
	do \
	{ \
		if (!(condition)) \
		{ \
			xplay_failures++; \
			fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
			fprintf(stderr, __VA_ARGS__); \
			fputc('\n', stderr); \
		} \
	} while (0)

/* ---------- the structures of the layout tables */

/* the bytes of a structure of the table, with each scalar of the table's the value for its seed in this machine's order
(native, the bytes between them a pattern of their own); expected: the same with each scalar's bytes little-endian,
the wire's order, which is what a send must turn native into */
static void xplay_fill(const struct xplay_structure *structure, unsigned char *native, unsigned char *expected,
	unsigned long seed)
{
	long index, run, element;

	for (index = 0; index < structure->size; index++)
		native[index] = expected[index] = (unsigned char)(xplay_mix(seed * 7919UL + (unsigned long)index) >> 8);
	for (run = 0; run < structure->leaf_count; run++)
	{
		const struct xplay_leaf *leaf = &structure->leaves[run];

		for (element = 0; element < leaf->count; element++)
		{
			unsigned char value[8];
			long at = leaf->offset + element * leaf->width;

			xplay_value(value, leaf->width, seed * 131UL + (unsigned long)(run * 17 + element));
			xplay_store_native(native + at, value, leaf->width);
			memcpy(expected + at, value, (size_t)leaf->width);
		}
	}
}

#endif
