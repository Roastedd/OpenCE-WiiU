/* internet play's inboxes in the process (port/linux/src/p2p.c): the ring of datagrams a peer sent one of the game's
sockets, taken in order with their peers' addresses, round the ring's end, refusing what does not fit as a full socket
does, and cut short to the reader's buffer as Winsock does */
#include "harness.h"

/* (a ring this small comes round its end every few datagrams) */
#define INBOX_SIZE 64
#include "under_test.inc"

static unsigned char ring[INBOX_SIZE];
static struct inbox inbox;

static void reset(void)
{
	memset(&inbox, 0, sizeof(inbox));
	memset(ring, 0xEE, sizeof(ring));
	inbox.bytes = ring;
}

/* a datagram of size bytes, each its own (seed + index) */
static void fill(unsigned char *data, int size, int seed)
{
	int index;

	for (index = 0; index < size; index++)
		data[index] = (unsigned char)(seed + index * 7);
}

static void check_taken(int size, int seed, unsigned long address, unsigned short port)
{
	unsigned char buffer[INBOX_SIZE], expected[INBOX_SIZE];
	unsigned long taken_address = 0;
	unsigned short taken_port = 0;
	int taken;

	fill(expected, size, seed);
	taken = inbox_take(&inbox, buffer, (int)sizeof(buffer), &taken_address, &taken_port);
	CHECK(taken == size, "took %d bytes, not %d", taken, size);
	CHECK(!memcmp(buffer, expected, (size_t)size), "a datagram of %d bytes (seed %d) came out changed", size, seed);
	CHECK(taken_address == address && taken_port == port, "from %lx:%x, not %lx:%x", taken_address, taken_port, address,
		port);
}

static void put(int size, int seed, unsigned long address, unsigned short port)
{
	unsigned char data[INBOX_SIZE];

	fill(data, size, seed);
	CHECK(inbox_put(&inbox, address, port, data, size), "a datagram of %d bytes was refused with %d used", size,
		inbox.used);
}

int main(int argc, char **argv)
{
	char const *case_name = argc == 2 ? argv[1] : "";
	unsigned char buffer[INBOX_SIZE];
	unsigned long address;
	unsigned short port;
	int round;

	reset();
	CASE("order")
	{
		put(5, 1, 0x0A400064UL, 0x1E14);
		put(12, 2, 0x0B400064UL, 0x1F14);
		put(0, 3, 0x0C400064UL, 0x2014);
		CHECK(inbox.count == 3 && inbox.data_bytes == 17, "%d datagrams of %d bytes", inbox.count, inbox.data_bytes);
		check_taken(5, 1, 0x0A400064UL, 0x1E14);
		check_taken(12, 2, 0x0B400064UL, 0x1F14);
		/* (an empty datagram is one, to be taken) */
		check_taken(0, 3, 0x0C400064UL, 0x2014);
		CHECK(inbox.count == 0 && inbox.used == 0 && inbox.data_bytes == 0, "left %d datagrams, %d bytes", inbox.count,
			inbox.used);
		CHECK(inbox_take(&inbox, buffer, (int)sizeof(buffer), &address, &port) == -1, "an empty inbox gave a datagram");
	}
	else CASE("wrap")
	{
		/* (every offset of the ring's start, headers and data cut by its end) */
		for (round = 0; round < 200; round++)
		{
			int size = round % 23;

			put(size, round, 0x64400000UL + (unsigned long)round, (unsigned short)(round * 3));
			if (round % 3 == 2)
				put(7, round + 1000, 0x64410000UL + (unsigned long)round, (unsigned short)round);
			check_taken(size, round, 0x64400000UL + (unsigned long)round, (unsigned short)(round * 3));
			if (round % 3 == 2)
				check_taken(7, round + 1000, 0x64410000UL + (unsigned long)round, (unsigned short)round);
		}
		CHECK(inbox.used == 0, "%d bytes left", inbox.used);
	}
	else CASE("full")
	{
		/* (8 + 20 twice fits 64; a third does not, nor anything after, until one is taken) */
		put(20, 1, 1, 1);
		put(20, 2, 2, 2);
		fill(buffer, 20, 3);
		CHECK(!inbox_put(&inbox, 3, 3, buffer, 20), "a datagram past the ring's room was taken");
		CHECK(!inbox_put(&inbox, 3, 3, buffer, INBOX_SIZE), "a datagram as large as the ring was taken");
		CHECK(inbox_put(&inbox, 4, 4, buffer, 0), "an empty datagram's header did not fit the room left");
		CHECK(inbox.used <= INBOX_SIZE, "%d bytes used of %d", inbox.used, INBOX_SIZE);
		check_taken(20, 1, 1, 1);
		put(20, 5, 5, 5);
		check_taken(20, 2, 2, 2);
		check_taken(0, 3, 4, 4);
		check_taken(20, 5, 5, 5);
	}
	else CASE("truncate")
	{
		put(30, 9, 9, 9);
		put(4, 10, 10, 10);
		memset(buffer, 0, sizeof(buffer));
		/* (its whole size, its start copied: the reader's WSAEMSGSIZE; the rest of it goes) */
		CHECK(inbox_take(&inbox, buffer, 10, &address, &port) == 30, "a cut datagram's size was not its own");
		{
			unsigned char expected[30];

			fill(expected, 30, 9);
			CHECK(!memcmp(buffer, expected, 10) && buffer[10] == 0, "a cut datagram was not cut at the buffer's length");
		}
		check_taken(4, 10, 10, 10);
	}
	else
		return 2;
	return 0;
}
