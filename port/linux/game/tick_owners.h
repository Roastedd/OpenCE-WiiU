/*
TICK_OWNERS.H

One owner for each structure the game's ticks and its drawing share, for the
Wii U (the tools repository's reports/research/2026-11-01/ticks-off-thread.md
section 9, stage 2; reports/research/2026-11-01/ticks-stage2.md has what was
done). A structure that two threads touch gets one owner thread, and the other
thread's changes to it are requests in a queue that the owner applies at its own
points: the sound manager's list (sound_render), the texture and sound caches
(the game thread), the hs runtime (the tick thread), the particles, particle
systems, contrails and decals (the render thread), and the game clock's pause
(the tick thread).

Nothing here runs a second thread yet. The mode says how the queues are used
(HALO_TICK_OWNERS in the environment; on the console halo/tick_owners.txt, which
wiiu_shell.c turns into it):

- 0 (the default): none. Every call is made directly, as it always was.
- 1: every request goes through its queue and is applied at once, where the
  direct call was. The ticks cannot differ, and a picture does not: this
  checks the queues' plumbing (what a request carries, what it returns).
- 2: every request goes through its queue and waits for its owner's own point
  (the start of sound_render, of the frame's particles_update, ...). This is the
  order a second thread would give, run on one: the ticks must still not differ.

A queue that fills before its owner drains it is drained by the producer while
one thread does both (tick_owners_threaded() is FALSE), and waited on once
there are two.

Everything here is for the Wii U (HALO_WIIU) and is not compiled otherwise.
*/

#ifndef __TICK_OWNERS_H
#define __TICK_OWNERS_H

#ifdef HALO_WIIU

#include <pthread.h>

/* ---------- the mode */

/* 0, 1 or 2 (above); read once, logged once */
long tick_owners_mode(void);
/* (what it read, -1 before: the checks on the game's hot paths are a load and a compare) */
extern long tick_owners_mode_value;

#define TICK_OWNERS_MODE() (tick_owners_mode_value >= 0 ? tick_owners_mode_value : tick_owners_mode())
#define TICK_OWNERS_ON() (TICK_OWNERS_MODE() != 0)

/* The parts the switch turns on, each of which can be left out (HALO_TICK_OWNERS_OFF: a number, the parts' bits added, 0x for
hexadecimal; on the console the second number of halo/tick_owners.txt), to find which part of a run's difference is whose. The
queues' own plumbing (the library, the drains) is not a part. */
enum tick_feature
{
	TICK_FEATURE_SOUND,
	TICK_FEATURE_RESOURCES,
	TICK_FEATURE_HS,
	TICK_FEATURE_PAUSE,
	TICK_FEATURE_FIRST_PERSON,
	TICK_FEATURE_WIDGET_COUNTS,
	TICK_FEATURE_LIGHTS,
	TICK_FEATURE_PARTICLES,
	TICK_FEATURE_PARTICLE_SYSTEMS,
	TICK_FEATURE_CONTRAILS,
	TICK_FEATURE_DECALS,
	TICK_FEATURE_PARTICLE_EFFECTS,
	TICK_FEATURE_INPUT,
	TICK_FEATURE_OBSERVER,
	TICK_FEATURE_FACING,
	TICK_FEATURE_POOL,
	NUMBER_OF_TICK_FEATURES
};
extern unsigned long tick_owners_off_mask;
#define TICK_OWNERS_FEATURE(feature) (TICK_OWNERS_ON() && !(tick_owners_off_mask & (1UL << (feature))))
/* mode 1: a request is applied as it is made */
#define TICK_OWNERS_APPLY_AT_ONCE() (TICK_OWNERS_MODE() == 1)

/* whether the ticks run on a thread of their own (stage 4: not yet). Setting it also registers the thread's gate with the shell
(wiiu_shell.c): HOME, a close and a halt wait for it as for the game thread's. */
boolean tick_owners_threaded(void);
void tick_owners_set_threaded(boolean threaded);
/* the tick thread's safe point, between two ticks: it is held there while the app is in the background, closing or halted */
void tick_owners_safe_point(void);

/* ---------- owners */

/* the thread that applies a queue's requests, and so which of its points drains them (tick_owners_drain) */
enum tick_owner
{
	/* the game thread: the texture and sound caches (before a frame is drawn) */
	TICK_OWNER_GAME,
	/* sound_render: the sound manager's list (it drains its own queue at its start) */
	TICK_OWNER_SOUND,
	/* the tick thread: the hs runtime, the effects and the game clock's pause (before the tick pass of a frame) */
	TICK_OWNER_TICK,
	/* the render thread: the particles, particle systems, contrails and decals (at the start of the frame's particles_update) */
	TICK_OWNER_RENDER,
	NUMBER_OF_TICK_OWNERS
};

/* ---------- queues */

/* A first-in first-out queue of fixed-size records with any number of producers and one owner. */
struct tick_queue
{
	char const *name;
	long owner;
	byte *records;
	long record_size;
	long capacity;
	/* the owner's step for one record */
	void (*apply)(void const *record);
	/* the tick overlap's tag of each record (tick_overlap.h): which jobs the game thread had posted when it pushed it */
	unsigned long *tags;
	pthread_mutex_t lock;
	/* counts that only ever go up (head: the next record to take, tail: the next place to put one) */
	unsigned long head;
	unsigned long tail;
	boolean draining;
	/* what the log says at a map's end */
	unsigned long pushed_total;
	unsigned long high_water;
	unsigned long forced_drains;
	unsigned long pushed_at_report;
	unsigned long forced_at_report;
	struct tick_queue *next;
	/* producers waiting for room (the overlap's owner serves a queue that has any, even while it defers the others) */
	volatile long waiters;
};

#define TICK_QUEUE_DEFINE(variable, queue_name, queue_owner, record_type, queue_capacity, apply_function) \
	static record_type variable##_records[queue_capacity]; \
	static unsigned long variable##_tags[queue_capacity]; \
	static struct tick_queue variable = \
	{ \
		queue_name, \
		queue_owner, \
		(byte *)variable##_records, \
		sizeof(record_type), \
		queue_capacity, \
		apply_function, \
		variable##_tags, \
		PTHREAD_MUTEX_INITIALIZER \
	}

/* The record copied to the end of the queue. If the queue is full and one thread does both jobs, the queue is drained
first (counted in the log); in mode 1 the owner's drain follows at once. */
void tick_queue_push(struct tick_queue *queue, void const *record);
/* The owner applies every record there is, oldest first, including those its own steps add; returns how many.
A drain inside a drain does nothing: the outer one takes what was added. */
long tick_queue_drain(struct tick_queue *queue);
/* The queues that producers are waiting for room in, drained whoever owns them (the game thread's wait for the tick thread: nothing else would) */
long tick_owners_drain_waited(void);
/* Every queue of an owner drained, at that owner's point (modes 1 and 2: in mode 1 nothing waits). Returns the requests applied. */
long tick_owners_drain(long owner);
/* Records dropped unapplied (a map's end: nothing they name is there any more) */
long tick_queue_discard(struct tick_queue *queue);
/* and for every queue */
void tick_owners_discard_all(void);
long tick_queue_count(struct tick_queue *queue);

/* One line to the game's log for each queue that was used since the last time, at a map's end */
void tick_owners_report(void);

/* ---------- the object memory pool's hand-off

The objects' memory pool (objects.c) is compacted by moving the objects' blocks, and the renderer holds pointers into them
for the whole frame it draws. So the pool is compacted only at the hand-off, when the renderer holds none: it brackets
its frame with the reader calls, and a compaction (the tick thread's, from the garbage collector or a script) waits
until the frame is over. The calls nest, and one thread may do both (the pool is then compacted at once between the
frames; inside one it is the same thread's pointers that move, as ever). With the queues off, none of this is done. */
void tick_owners_object_pool_reader_begin(void);
void tick_owners_object_pool_reader_end(void);
/* runs compact with no reader inside a frame (the calling thread's own frame excepted); counted in the log */
void tick_owners_object_pool_compact(void (*compact)(void));

#endif /* HALO_WIIU */

#endif /* __TICK_OWNERS_H */
