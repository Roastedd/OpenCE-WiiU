/* The memory the commands of a stream read when the stream is handed over (d3d8_gx2.c), so that a write the game makes into it can hand the stream over first. */
#ifndef WIIU_PENDING_READS_H
#define WIIU_PENDING_READS_H

#include <stddef.h>
#include <stdint.h>

#define PENDING_READ_LIMIT 32

struct pending_reads
{
	struct { uintptr_t start, end; } range[PENDING_READ_LIMIT];
	unsigned count;
};

void pending_reads_clear(struct pending_reads *reads);
/* records [address, address + size); a range that touches one recorded joins it. Returns 0 if the table is full (nothing recorded: hand the stream over, clear, record again) */
int pending_reads_add(struct pending_reads *reads, const void *address, unsigned long size);
/* whether a write to [address, address + size) changes something recorded */
int pending_reads_overlap(const struct pending_reads *reads, const void *address, unsigned long size);

#endif
