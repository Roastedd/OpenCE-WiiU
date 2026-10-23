/* wiiu_pending_reads.c: the ranges of memory a stream's commands read, and the hazard they guard against: draw A, the game rewrites the storage, draw B, one present. */

#include "../src/wiiu_pending_reads.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

/* A toy of d3d8_gx2.c's stream: a draw records a pointer and a size; the host reads the bytes when the stream is handed over (flush). */
static struct pending_reads reads;
static struct { const unsigned char *data; unsigned size; } stream[64];
static unsigned stream_count;
static unsigned char seen[8][16];
static unsigned seen_count;

static void flush(void)
{
	unsigned i;

	for (i = 0; i < stream_count; i++)
		memcpy(seen[seen_count++], stream[i].data, stream[i].size);
	stream_count = 0;
	pending_reads_clear(&reads);
}

static void draw(const unsigned char *data, unsigned size)
{
	if (!pending_reads_add(&reads, data, size))
	{
		flush();
		pending_reads_add(&reads, data, size);
	}
	stream[stream_count].data = data;
	stream[stream_count].size = size;
	stream_count++;
}

/* the game's write, announced first as memory_watch_prepare_write does */
static void write_buffer(unsigned char *where, unsigned size, unsigned char value, int announce)
{
	if (announce && pending_reads_overlap(&reads, where, size))
		flush();
	memset(where, value, size);
}

int main(void)
{
	static unsigned char buffer[256];
	unsigned i;

	/* the hazard, unguarded: both draws read the second fill */
	seen_count = stream_count = 0;
	pending_reads_clear(&reads);
	write_buffer(buffer, 16, 0xaa, 0);
	draw(buffer, 16);
	write_buffer(buffer, 16, 0xbb, 0);
	draw(buffer, 16);
	flush();
	CHECK(seen[0][0] == 0xbb && seen[1][0] == 0xbb);

	/* guarded: A reads what A was */
	seen_count = stream_count = 0;
	pending_reads_clear(&reads);
	write_buffer(buffer, 16, 0xaa, 1);
	draw(buffer, 16);
	write_buffer(buffer, 16, 0xbb, 1);
	draw(buffer, 16);
	flush();
	CHECK(seen_count == 2 && seen[0][0] == 0xaa && seen[1][0] == 0xbb);

	/* a write to other storage hands nothing over; a write beside the range (touching, not overlapping) neither */
	seen_count = stream_count = 0;
	pending_reads_clear(&reads);
	write_buffer(buffer, 16, 0x11, 1);
	draw(buffer, 16);
	write_buffer(buffer + 16, 16, 0x22, 1);
	write_buffer(buffer + 100, 16, 0x33, 1);
	CHECK(stream_count == 1 && seen_count == 0);
	write_buffer(buffer + 15, 1, 0x44, 1);
	CHECK(stream_count == 0 && seen_count == 1 && seen[0][0] == 0x11);

	/* a zero-size range records nothing and overlaps nothing */
	pending_reads_clear(&reads);
	CHECK(pending_reads_add(&reads, buffer, 0) && reads.count == 0);
	pending_reads_add(&reads, buffer, 16);
	CHECK(!pending_reads_overlap(&reads, buffer, 0));

	/* ranges that touch or overlap join: draws that follow each other cost one entry */
	pending_reads_clear(&reads);
	for (i = 0; i < 10; i++)
		pending_reads_add(&reads, buffer + i * 8, 8);
	CHECK(reads.count == 1 && pending_reads_overlap(&reads, buffer + 79, 1) && !pending_reads_overlap(&reads, buffer + 80, 1));

	/* more separate ranges than the table holds: the nearest are joined, and nothing is forgotten: every range added is inside some entry afterwards */
	{
		static unsigned char big[100000];
		unsigned long k;

		pending_reads_clear(&reads);
		for (k = 0; k < 5000; k++)
		{
			const unsigned long where = (k * 7919ul) % 99000ul;

			CHECK(pending_reads_add(&reads, big + where, 16));
			CHECK(reads.count <= PENDING_READ_LIMIT);
			CHECK(pending_reads_overlap(&reads, big + where, 16));
			CHECK(pending_reads_overlap(&reads, big + where + 15, 1));
		}
		/* (and every earlier one is still covered) */
		for (k = 0; k < 5000; k++)
		{
			const unsigned long where = (k * 7919ul) % 99000ul;

			if (!pending_reads_overlap(&reads, big + where, 16))
			{
				CHECK(0);
				break;
			}
		}
		/* a place nothing was added near stays clear when the table is spread out */
		pending_reads_clear(&reads);
		for (k = 0; k < 40; k++)
			CHECK(pending_reads_add(&reads, big + 100 + k * 1000, 8));
		CHECK(!pending_reads_overlap(&reads, big + 60000, 8));
	}

	printf(failures ? "%d failures\n" : "pending reads: all passed\n", failures);
	return failures != 0;
}
