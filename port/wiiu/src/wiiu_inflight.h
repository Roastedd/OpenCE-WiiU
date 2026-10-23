/* The memory the streams handed to the GX2 thread will read, and which stream (submission number) reads it, until that stream is retired: what a write the game announces into such memory
has to wait for (reports/research/2026-10-23/two-core-render-plan.md, section 3.3). wiiu_pending_reads.c is the same for a stream that is consumed at once. */
#ifndef WIIU_INFLIGHT_H
#define WIIU_INFLIGHT_H

#include <stddef.h>
#include <stdint.h>

#define INFLIGHT_LIMIT 128

struct inflight
{
	struct { uintptr_t start, end; uint32_t submission; } range[INFLIGHT_LIMIT];
	unsigned count;
};

void inflight_clear(struct inflight *inflight);
/* [address, address + size) is read by the stream numbered `submission`; `retired` is the last stream the GX2 thread has finished (their ranges are dropped when room is wanted). A range that touches
one recorded joins it, with the later submission; when the table is full the nearest ranges are joined (covering more than was read, never less). Never fails. */
void inflight_add(struct inflight *inflight, const void *address, unsigned long size, uint32_t submission, uint32_t retired);
/* the latest stream not yet retired that reads any of [address, address + size), 0 if none: the writer waits until that one is retired (after handing it over, if it is the stream still being written) */
uint32_t inflight_hazard(const struct inflight *inflight, const void *address, unsigned long size, uint32_t retired);

#endif
