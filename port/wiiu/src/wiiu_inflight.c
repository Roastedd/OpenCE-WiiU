#include "wiiu_inflight.h"

void inflight_clear(struct inflight *inflight)
{
	inflight->count = 0;
}

static void drop_retired(struct inflight *inflight, uint32_t retired)
{
	unsigned i, out = 0;

	for (i = 0; i < inflight->count; i++)
		if (inflight->range[i].submission > retired)
			inflight->range[out++] = inflight->range[i];
	inflight->count = out;
}

/* sorted by start, ranges closer than a growing gap are joined (the later submission kept) until half the table is free; at the last resort everything is one range */
static void coalesce(struct inflight *inflight)
{
	unsigned i, j;
	uintptr_t gap = 256;

	for (i = 1; i < inflight->count; i++)
	{
		const typeof(inflight->range[0]) moving = inflight->range[i];

		for (j = i; j > 0 && inflight->range[j - 1].start > moving.start; j--)
			inflight->range[j] = inflight->range[j - 1];
		inflight->range[j] = moving;
	}
	while (inflight->count > INFLIGHT_LIMIT / 2 && gap)
	{
		unsigned out = 0;

		for (i = 0; i < inflight->count; i++)
		{
			if (out && inflight->range[i].start <= inflight->range[out - 1].end + gap)
			{
				if (inflight->range[i].end > inflight->range[out - 1].end)
					inflight->range[out - 1].end = inflight->range[i].end;
				if (inflight->range[i].submission > inflight->range[out - 1].submission)
					inflight->range[out - 1].submission = inflight->range[i].submission;
			}
			else
				inflight->range[out++] = inflight->range[i];
		}
		inflight->count = out;
		gap *= 4;
	}
	if (inflight->count > INFLIGHT_LIMIT / 2)
	{
		/* (the gap overflowed: one range for all) */
		for (i = 1; i < inflight->count; i++)
		{
			if (inflight->range[i].end > inflight->range[0].end)
				inflight->range[0].end = inflight->range[i].end;
			if (inflight->range[i].submission > inflight->range[0].submission)
				inflight->range[0].submission = inflight->range[i].submission;
		}
		inflight->count = 1;
	}
}

void inflight_add(struct inflight *inflight, const void *address, unsigned long size, uint32_t submission, uint32_t retired)
{
	const uintptr_t start = (uintptr_t)address, end = start + size;
	unsigned index;

	if (!size)
		return;
	/* (the most recent first: a draw's streams follow the last draw's) */
	for (index = inflight->count; index-- > 0;)
	{
		if (start <= inflight->range[index].end && end >= inflight->range[index].start)
		{
			if (start < inflight->range[index].start)
				inflight->range[index].start = start;
			if (end > inflight->range[index].end)
				inflight->range[index].end = end;
			if (submission > inflight->range[index].submission)
				inflight->range[index].submission = submission;
			return;
		}
	}
	if (inflight->count == INFLIGHT_LIMIT)
	{
		drop_retired(inflight, retired);
		if (inflight->count == INFLIGHT_LIMIT)
			coalesce(inflight);
	}
	inflight->range[inflight->count].start = start;
	inflight->range[inflight->count].end = end;
	inflight->range[inflight->count].submission = submission;
	inflight->count++;
}

uint32_t inflight_hazard(const struct inflight *inflight, const void *address, unsigned long size, uint32_t retired)
{
	const uintptr_t start = (uintptr_t)address, end = start + size;
	uint32_t latest = 0;
	unsigned index;

	for (index = 0; index < inflight->count; index++)
		if (inflight->range[index].submission > retired && start < inflight->range[index].end && end > inflight->range[index].start &&
			inflight->range[index].submission > latest)
			latest = inflight->range[index].submission;
	return latest;
}
