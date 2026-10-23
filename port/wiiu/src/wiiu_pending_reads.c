#include "wiiu_pending_reads.h"

void pending_reads_clear(struct pending_reads *reads)
{
	reads->count = 0;
}

int pending_reads_add(struct pending_reads *reads, const void *address, unsigned long size)
{
	const uintptr_t start = (uintptr_t)address, end = start + size;
	unsigned index;

	if (!size)
		return 1;
	/* (the most recent first: a draw's streams follow the last draw's) */
	for (index = reads->count; index-- > 0;)
	{
		if (start <= reads->range[index].end && end >= reads->range[index].start)
		{
			if (start < reads->range[index].start)
				reads->range[index].start = start;
			if (end > reads->range[index].end)
				reads->range[index].end = end;
			return 1;
		}
	}
	if (reads->count == PENDING_READ_LIMIT)
		return 0;
	reads->range[reads->count].start = start;
	reads->range[reads->count].end = end;
	reads->count++;
	return 1;
}

int pending_reads_overlap(const struct pending_reads *reads, const void *address, unsigned long size)
{
	const uintptr_t start = (uintptr_t)address, end = start + size;
	unsigned index;

	for (index = 0; index < reads->count; index++)
		if (start < reads->range[index].end && end > reads->range[index].start)
			return 1;
	return 0;
}
