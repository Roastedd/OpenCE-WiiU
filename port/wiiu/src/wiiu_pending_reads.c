#include "wiiu_pending_reads.h"

void pending_reads_clear(struct pending_reads *reads)
{
	reads->count = 0;
}

/* half of the table's ranges are joined to their neighbours: sorted by start, those closer than a gap are made one, the gap growing until half are gone (each range stays inside one of the
results: no read is forgotten) */
static void coalesce(struct pending_reads *reads)
{
	unsigned i, j;
	uintptr_t gap = 256;

	for (i = 1; i < reads->count; i++)
	{
		const uintptr_t start = reads->range[i].start, end = reads->range[i].end;

		for (j = i; j > 0 && reads->range[j - 1].start > start; j--)
			reads->range[j] = reads->range[j - 1];
		reads->range[j].start = start;
		reads->range[j].end = end;
	}
	while (reads->count > PENDING_READ_LIMIT / 2 && gap)
	{
		unsigned out = 0;

		for (i = 0; i < reads->count; i++)
		{
			if (out && reads->range[i].start <= reads->range[out - 1].end + gap)
			{
				if (reads->range[i].end > reads->range[out - 1].end)
					reads->range[out - 1].end = reads->range[i].end;
			}
			else
				reads->range[out++] = reads->range[i];
		}
		reads->count = out;
		gap *= 4;
	}
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
	{
		coalesce(reads);
		if (reads->count == PENDING_READ_LIMIT)
			return 0;
	}
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
