/*
WIIU_MEMCPY.C

memcpy and memset for the Espresso. The libc's (newlib) copies a word at a time and waits for every cache line it reads or writes: the 750 core has no prefetching of its own, a read
of a line that is not in the cache stops the copy for as long as the memory takes, and a write to a line first reads it. The profiler (wiiu_profile.c) showed copies of the renderer's
and the game's data at a third of the game thread's time at about 100 MB a second. These ask for the source's lines ahead (dcbt) and make the destination's lines without reading
them (dcbz) when they are whole lines of the application's memory, which more than doubles the speed of copies of a few hundred bytes or more.

Every access is a word or a byte, whatever the alignment (the Espresso does misaligned integer accesses in hardware). The loops that copy bytes are volatile so that the compiler does not
recognise them as memcpy and call it. dcbz is used only on addresses of the application's memory (0x10000000 to 0x50000000), where lines are cached.
*/

#include <stddef.h>
#include <stdint.h>

#define CACHE_LINE 32u

static int zeroable(const void *address)
{
	return (uintptr_t)address >= 0x10000000u && (uintptr_t)address < 0x50000000u;
}

void *memcpy(void *destination, const void *source, size_t size)
{
	volatile uint8_t *d = (volatile uint8_t *)destination;
	const volatile uint8_t *s = (const volatile uint8_t *)source;

	if (size >= 96 && zeroable(destination))
	{
		size_t head = (size_t)((0u - (uintptr_t)destination) & (CACHE_LINE - 1));

		size -= head;
		while (head--)
			*d++ = *s++;
		while (size >= CACHE_LINE)
		{
			uint32_t a, b, c, e, f, g, h, i;

			__asm__ volatile("dcbt 0,%0" : : "r"((const uint8_t *)s + 3 * CACHE_LINE));
			__asm__ volatile("dcbz 0,%0" : : "r"((uint8_t *)d) : "memory");
			__asm__ volatile(
				"lwz %0,0(%8)\n\tlwz %1,4(%8)\n\tlwz %2,8(%8)\n\tlwz %3,12(%8)\n\t"
				"lwz %4,16(%8)\n\tlwz %5,20(%8)\n\tlwz %6,24(%8)\n\tlwz %7,28(%8)\n\t"
				"stw %0,0(%9)\n\tstw %1,4(%9)\n\tstw %2,8(%9)\n\tstw %3,12(%9)\n\t"
				"stw %4,16(%9)\n\tstw %5,20(%9)\n\tstw %6,24(%9)\n\tstw %7,28(%9)"
				: "=&r"(a), "=&r"(b), "=&r"(c), "=&r"(e), "=&r"(f), "=&r"(g), "=&r"(h), "=&r"(i)
				: "b"((const uint8_t *)s), "b"((uint8_t *)d)
				: "memory");
			s += CACHE_LINE;
			d += CACHE_LINE;
			size -= CACHE_LINE;
		}
	}
	else if (size >= 16 && ((((uintptr_t)destination) | (uintptr_t)source) & 3u) == 0)
	{
		while (size >= 4)
		{
			*(volatile uint32_t *)d = *(const volatile uint32_t *)s;
			d += 4;
			s += 4;
			size -= 4;
		}
	}
	while (size--)
		*d++ = *s++;
	return destination;
}

void *memset(void *destination, int value, size_t size)
{
	volatile uint8_t *d = (volatile uint8_t *)destination;
	const uint8_t byte = (uint8_t)value;

	if (size >= 96 && zeroable(destination) && byte == 0)
	{
		size_t head = (size_t)((0u - (uintptr_t)destination) & (CACHE_LINE - 1));

		size -= head;
		while (head--)
			*d++ = 0;
		while (size >= CACHE_LINE)
		{
			__asm__ volatile("dcbz 0,%0" : : "r"((uint8_t *)d) : "memory");
			d += CACHE_LINE;
			size -= CACHE_LINE;
		}
	}
	else if (size >= 16)
	{
		const uint32_t word = (uint32_t)byte * 0x01010101u;

		while (size && ((uintptr_t)d & 3u))
		{
			*d++ = byte;
			size--;
		}
		while (size >= 4)
		{
			*(volatile uint32_t *)d = word;
			d += 4;
			size -= 4;
		}
	}
	while (size--)
		*d++ = byte;
	return destination;
}

/* every size to 300 at every misalignment of both addresses, against a copy a byte at a time, and the memset's the same; returns the number of wrong results */
int wiiu_memcpy_selftest(void);

int wiiu_memcpy_selftest(void)
{
	static uint8_t source[512] __attribute__((aligned(32))), target[512] __attribute__((aligned(32))), reference[512] __attribute__((aligned(32)));
	unsigned size, from, to, i;
	int wrong = 0;

	for (i = 0; i < sizeof(source); i++)
		source[i] = (uint8_t)(i * 31u + 7u);
	for (size = 0; size <= 300; size++)
		for (from = 0; from < 8; from++)
			for (to = 0; to < 8; to++)
			{
				for (i = 0; i < sizeof(target); i++)
					target[i] = reference[i] = 0xA5;
				for (i = 0; i < size; i++)
					((volatile uint8_t *)reference)[to + i] = source[from + i];
				memcpy(target + to, source + from, size);
				for (i = 0; i < sizeof(target); i++)
					if (target[i] != reference[i])
					{
						wrong++;
						break;
					}
			}
	for (size = 0; size <= 300; size += 3)
		for (to = 0; to < 8; to++)
		{
			for (i = 0; i < sizeof(target); i++)
				target[i] = reference[i] = 0xA5;
			for (i = 0; i < size; i++)
				((volatile uint8_t *)reference)[to + i] = 0;
			memset(target + to, 0, size);
			for (i = 0; i < sizeof(target); i++)
				if (target[i] != reference[i])
				{
					wrong++;
					break;
				}
			for (i = 0; i < sizeof(target); i++)
				target[i] = reference[i] = 0xA5;
			for (i = 0; i < size; i++)
				((volatile uint8_t *)reference)[to + i] = 0x5C;
			memset(target + to, 0x5C, size);
			for (i = 0; i < sizeof(target); i++)
				if (target[i] != reference[i])
				{
					wrong++;
					break;
				}
		}
	return wrong;
}
