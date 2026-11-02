/*
TICK_RING.H

The bookkeeping of the render view snapshot's ring (tick_snapshot.c; the plan is the tools repository's reports/research/2026-11-01/ticks-stage4.md):
which slot holds which tick, which are committed, and which the drawing thread has pinned. No game data, so that its rules can be tested apart from the
game (port/wiiu/tests/test_tick_ring.c, under the thread sanitizer).

The tick thread captures the world after a tick into a slot; the drawing thread draws a frame from the capture of one tick and the one before it, and
pins those two for the frame. The rules that keep them from meeting in one slot:

- a capture is written to the slot of its own tick if it has one (the serial mode's catch-up), else to an unused slot, else to the oldest slot that is
  not pinned and holds a tick at least two before the newest committed (so the newest two committed captures, the only pair a frame can still ask for,
  are never overwritten), and no capture of the job being run (a tick newer than the committed) is either;
- a job captures at most the last two of its ticks, and a frame pins at most two slots, so with four slots one is always free to write;
- a capture is visible (committed) only when the tick thread says so at the end of its job, with the cameras attached, and the drawing thread asks
  for ticks no newer than the newest committed.

The pins are written by the drawing thread before it hands a job to the tick thread and let go of when its frame is over; the tick thread reads them
when it picks a slot. The committed tick is written by the tick thread and read by the drawing thread when the job is over.
*/

#ifndef __TICK_RING_H
#define __TICK_RING_H

#define TICK_RING_NONE ((long)0x80000000)
#define TICK_RING_MAXIMUM_SLOTS 8

struct tick_ring
{
	int count;
	long tick[TICK_RING_MAXIMUM_SLOTS]; /* TICK_RING_NONE: none */
	int ready[TICK_RING_MAXIMUM_SLOTS];
	long published; /* the newest committed capture's tick */
	long captured; /* the newest capture's tick */
	int pinned[2]; /* the slots a frame is drawn from (-1: none) */
};

static __inline void tick_ring_reset(
	struct tick_ring *ring,
	int count)
{
	int index;

	ring->count = count;
	for (index = 0; index < TICK_RING_MAXIMUM_SLOTS; index++)
	{
		ring->tick[index] = TICK_RING_NONE;
		ring->ready[index] = 0;
	}
	__atomic_store_n(&ring->published, TICK_RING_NONE, __ATOMIC_RELAXED);
	ring->captured = TICK_RING_NONE;
	__atomic_store_n(&ring->pinned[0], -1, __ATOMIC_RELAXED);
	__atomic_store_n(&ring->pinned[1], -1, __ATOMIC_RELAXED);
}

static __inline long tick_ring_published(
	struct tick_ring const *ring)
{
	return __atomic_load_n(&ring->published, __ATOMIC_RELAXED);
}

/* the slot holding the capture of a tick, or -1 */
static __inline int tick_ring_find(
	struct tick_ring const *ring,
	long tick)
{
	int index;

	for (index = 0; index < ring->count; index++)
	{
		if (ring->ready[index] && ring->tick[index] == tick)
			return index;
	}
	return -1;
}

static __inline int tick_ring_pinned(
	struct tick_ring const *ring,
	int index)
{
	return __atomic_load_n(&ring->pinned[0], __ATOMIC_RELAXED) == index || __atomic_load_n(&ring->pinned[1], __ATOMIC_RELAXED) == index;
}

/* the slot a capture of tick is written to, or -1 if there is none */
static __inline int tick_ring_slot_for(
	struct tick_ring const *ring,
	long tick)
{
	const long published = tick_ring_published(ring);
	int found = tick_ring_find(ring, tick);
	int best = -1;
	int index;

	if (found >= 0 && !tick_ring_pinned(ring, found))
		return found;
	for (index = 0; index < ring->count; index++)
	{
		if (tick_ring_pinned(ring, index))
			continue;
		if (!ring->ready[index])
			return index;
		if (published != TICK_RING_NONE && ring->tick[index] <= published - 2 && (best < 0 || ring->tick[index] < ring->tick[best]))
			best = index;
	}
	return best;
}

static __inline void tick_ring_begin_write(
	struct tick_ring *ring,
	int index,
	long tick)
{
	ring->ready[index] = 0;
	ring->tick[index] = tick;
}

static __inline void tick_ring_end_write(
	struct tick_ring *ring,
	int index,
	int commit)
{
	ring->ready[index] = 1;
	ring->captured = ring->tick[index];
	if (commit)
		__atomic_store_n(&ring->published, ring->tick[index], __ATOMIC_RELAXED);
}

/* the captures so far visible to the drawing thread */
static __inline void tick_ring_commit(
	struct tick_ring *ring)
{
	const long published = tick_ring_published(ring);

	if (ring->captured != TICK_RING_NONE && (published == TICK_RING_NONE || ring->captured > published))
		__atomic_store_n(&ring->published, ring->captured, __ATOMIC_RELAXED);
}

/* the capture of tick and the one before it (if it has one) pinned; 0 if there is no committed capture of tick */
static __inline int tick_ring_pin(
	struct tick_ring *ring,
	long tick)
{
	const long published = tick_ring_published(ring);
	int latest = published == TICK_RING_NONE || tick > published ? -1 : tick_ring_find(ring, tick);
	int before;

	__atomic_store_n(&ring->pinned[0], -1, __ATOMIC_RELAXED);
	__atomic_store_n(&ring->pinned[1], -1, __ATOMIC_RELAXED);
	if (latest < 0)
		return 0;
	before = tick_ring_find(ring, tick - 1);
	__atomic_store_n(&ring->pinned[1], before, __ATOMIC_RELAXED);
	__atomic_store_n(&ring->pinned[0], latest, __ATOMIC_RELAXED);
	return 1;
}

static __inline void tick_ring_unpin(
	struct tick_ring *ring)
{
	__atomic_store_n(&ring->pinned[0], -1, __ATOMIC_RELAXED);
	__atomic_store_n(&ring->pinned[1], -1, __ATOMIC_RELAXED);
}

#endif
