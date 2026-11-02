/*
The tick owners' queues and the object pool's hand-off (port/linux/game/tick_owners.c), apart from the game: the modes, the order
of requests, what a full queue does, a request made by an apply, the owners' drains, the discard, many producers and one owner,
and the pool's compaction waiting for a frame to end. Built for the desktop, for 32-bit x86 and for 32-bit big-endian PowerPC
(run under qemu-ppc), with the sanitizers where there are any: tools/test_wiiu_tick_owners.py.

    test_tick_owners mode0 | mode1 | mode2 | full | nested | owners | discard | threads | pool
Exit status 0 when everything holds; a failing check says which and exits 1.
*/

#define HALO_WIIU 1
#include "tick_owners.c"

#include <stdarg.h>
#include <stdio.h>

static int failures;
static int errors_logged;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("check failed, line %d: %s\n", __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

void platform_log(char const *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	va_end(arguments);
	errors_logged++;
}

/* the shell's gate (port/wiiu/src/wiiu_shell.c): counts */
static int gate_registered, gate_calls;

void wiiu_tick_gate_register(int registered)
{
	gate_registered = registered;
}

void wiiu_tick_gate(void)
{
	gate_calls++;
}

/* ---------- a queue of numbers */

struct number
{
	long producer;
	long value;
};

#define SMALL 4
static struct number applied[100000];
static long applied_count;

static void apply_number(void const *record)
{
	applied[applied_count++] = *(struct number const *)record;
}

TICK_QUEUE_DEFINE(small_queue, "small", TICK_OWNER_GAME, struct number, SMALL, apply_number);
TICK_QUEUE_DEFINE(sound_queue, "other", TICK_OWNER_SOUND, struct number, SMALL, apply_number);

static void push(struct tick_queue *queue, long producer, long value)
{
	struct number number = { producer, value };

	tick_queue_push(queue, &number);
}

static void test_mode(long expected)
{
	CHECK(tick_owners_mode() == expected);
	push(&small_queue, 0, 1);
	push(&small_queue, 0, 2);
	if (expected == 0)
		return;
	if (expected == 1)
	{
		/* applied where it was made */
		CHECK(applied_count == 2);
		CHECK(tick_queue_count(&small_queue) == 0);
	}
	else
	{
		CHECK(applied_count == 0);
		CHECK(tick_queue_count(&small_queue) == 2);
		CHECK(tick_owners_drain(TICK_OWNER_GAME) == 2);
		CHECK(applied_count == 2);
	}
	CHECK(applied[0].value == 1 && applied[1].value == 2);
	CHECK(tick_owners_drain(TICK_OWNER_GAME) == 0);
}

static void test_full(void)
{
	long index;

	/* more than the queue holds, one thread for both jobs: it makes room itself, and nothing is lost or out of order */
	for (index = 0; index < 10; index++)
		push(&small_queue, 0, index);
	tick_owners_drain(TICK_OWNER_GAME);
	CHECK(applied_count == 10);
	for (index = 0; index < 10; index++)
		CHECK(applied[index].value == index);
	CHECK(small_queue.forced_drains >= 1);
	CHECK(small_queue.high_water == SMALL);
	CHECK(errors_logged == 0);
}

/* ---------- a request made by an apply */

TICK_QUEUE_DEFINE(nested_queue, "nested", TICK_OWNER_TICK, struct number, SMALL, NULL);
static int nested_overflow;

static void apply_nested(void const *record)
{
	struct number const *number = (struct number const *)record;

	applied[applied_count++] = *number;
	/* each of the first records asks for two more, until there are seven; the later ones fill the queue inside its own drain */
	if (number->value < 3)
	{
		push(&nested_queue, 0, number->value * 2 + 10);
		push(&nested_queue, 0, number->value * 2 + 11);
	}
	if (number->value == 100)
	{
		long index;

		for (index = 0; index < SMALL + 2; index++)
			push(&nested_queue, 0, 200 + index);
	}
}

static void test_nested(void)
{
	long index;

	nested_queue.apply = apply_nested;
	push(&nested_queue, 0, 1);
	tick_owners_drain(TICK_OWNER_TICK);
	/* 1 asks for 12 and 13; 12 and 13 ask for nothing */
	CHECK(applied_count == 3);
	CHECK(applied[0].value == 1 && applied[1].value == 12 && applied[2].value == 13);
	CHECK(errors_logged == 0);

	/* a request that asks for more than fits, inside its own drain: the ones that do not fit are dropped, said in the log */
	applied_count = 0;
	push(&nested_queue, 0, 100);
	tick_owners_drain(TICK_OWNER_TICK);
	CHECK(errors_logged >= 1);
	for (index = 0; index < applied_count; index++)
		CHECK(applied[index].value == 100 || applied[index].value >= 200);
	CHECK(applied_count >= 1 + 1);
	CHECK(tick_queue_count(&nested_queue) == 0);
	nested_overflow = errors_logged;
}

static void test_owners(void)
{
	push(&small_queue, 0, 1);
	push(&sound_queue, 0, 2);
	CHECK(applied_count == 0);
	CHECK(tick_owners_drain(TICK_OWNER_SOUND) == 1);
	CHECK(applied_count == 1 && applied[0].value == 2);
	CHECK(tick_queue_count(&small_queue) == 1);
	CHECK(tick_owners_drain(TICK_OWNER_GAME) == 1);
	CHECK(applied_count == 2 && applied[1].value == 1);
}

static void test_discard(void)
{
	push(&small_queue, 0, 1);
	push(&small_queue, 0, 2);
	tick_owners_discard_all();
	CHECK(tick_queue_count(&small_queue) == 0);
	CHECK(tick_owners_drain(TICK_OWNER_GAME) == 0);
	CHECK(applied_count == 0);
	push(&small_queue, 0, 3);
	tick_owners_drain(TICK_OWNER_GAME);
	CHECK(applied_count == 1 && applied[0].value == 3);
	tick_owners_report();
	CHECK(errors_logged >= 1); /* (the report says what the queue carried) */
}

/* ---------- many producers, one owner */

#define PRODUCERS 4
#define PER_PRODUCER 5000
static volatile int producing;

static void *producer_main(void *argument)
{
	long producer = (long)argument;
	long index;

	for (index = 0; index < PER_PRODUCER; index++)
		push(&small_queue, producer, index);
	__atomic_sub_fetch(&producing, 1, __ATOMIC_SEQ_CST);
	return NULL;
}

static void test_threads(void)
{
	pthread_t threads[PRODUCERS];
	long next[PRODUCERS] = { 0 };
	long producer, index;

	tick_owners_set_threaded(TRUE);
	CHECK(gate_registered == 1);
	tick_owners_safe_point();
	CHECK(gate_calls == 1);
	producing = PRODUCERS;
	for (producer = 0; producer < PRODUCERS; producer++)
		pthread_create(&threads[producer], NULL, producer_main, (void *)producer);
	/* (this thread is the owner: it drains while they produce, and once more when they are done) */
	while (producing || tick_queue_count(&small_queue))
		tick_owners_drain(TICK_OWNER_GAME);
	for (producer = 0; producer < PRODUCERS; producer++)
		pthread_join(threads[producer], NULL);
	tick_owners_drain(TICK_OWNER_GAME);
	CHECK(applied_count == PRODUCERS * PER_PRODUCER);
	/* each producer's own requests in the order it made them, none lost, none twice */
	for (index = 0; index < applied_count; index++)
	{
		struct number *number = &applied[index];

		if (number->producer < 0 || number->producer >= PRODUCERS || number->value != next[number->producer]++)
		{
			CHECK(0);
			break;
		}
	}
	for (producer = 0; producer < PRODUCERS; producer++)
		CHECK(next[producer] == PER_PRODUCER);
	CHECK(small_queue.forced_drains == 0); /* (two threads: a full queue is waited on, not drained by the producer) */
	tick_owners_set_threaded(FALSE);
	CHECK(gate_registered == 0);
	tick_owners_safe_point();
	CHECK(gate_calls == 1);
}

/* ---------- the object pool's hand-off */

static volatile int compacted;
static volatile int frame_over;

static void compact_now(void)
{
	/* (never while the frame is on) */
	if (!frame_over)
		compacted = -1000;
	compacted++;
}

static void *compact_main(void *unused)
{
	(void)unused;
	tick_owners_object_pool_compact(compact_now);
	return NULL;
}

static void test_pool(void)
{
	pthread_t thread;
	struct timespec pause = { 0, 50 * 1000 * 1000 };

	/* one thread both draws and compacts: at once, between the frames */
	frame_over = 1;
	tick_owners_object_pool_compact(compact_now);
	CHECK(compacted == 1);
	/* inside its own frame: the same thread's pointers move, as ever */
	tick_owners_object_pool_reader_begin();
	frame_over = 1;
	tick_owners_object_pool_compact(compact_now);
	CHECK(compacted == 2);
	tick_owners_object_pool_reader_end();

	/* another thread's frame is on: the compaction waits for its end */
	tick_owners_object_pool_reader_begin();
	frame_over = 0;
	pthread_create(&thread, NULL, compact_main, NULL);
	nanosleep(&pause, NULL);
	CHECK(compacted == 2);
	frame_over = 1;
	tick_owners_object_pool_reader_end();
	pthread_join(thread, NULL);
	CHECK(compacted == 3);
	tick_owners_report();
}

int main(int count, char **arguments)
{
	char const *which = count > 1 ? arguments[1] : "mode0";

	if (!strcmp(which, "mode0"))
	{
		setenv("HALO_TICK_OWNERS", "0", 1);
		tick_owners_mode();
		errors_logged = 0;
		test_mode(0);
		/* (and a pool that is not held: compacted at once) */
		frame_over = 1;
		tick_owners_object_pool_reader_begin();
		tick_owners_object_pool_compact(compact_now);
		tick_owners_object_pool_reader_end();
		CHECK(compacted == 1);
	}
	else if (!strcmp(which, "mode1"))
	{
		setenv("HALO_TICK_OWNERS", "1", 1);
		tick_owners_mode();
		errors_logged = 0;
		test_mode(1);
	}
	else if (!strcmp(which, "mode2"))
	{
		setenv("HALO_TICK_OWNERS", "2", 1);
		tick_owners_mode();
		errors_logged = 0;
		test_mode(2);
	}
	else if (!strcmp(which, "full"))
	{
		setenv("HALO_TICK_OWNERS", "2", 1);
		tick_owners_mode();
		errors_logged = 0;
		test_full();
	}
	else if (!strcmp(which, "nested"))
	{
		setenv("HALO_TICK_OWNERS", "1", 1);
		tick_owners_mode();
		errors_logged = 0;
		test_nested();
	}
	else if (!strcmp(which, "owners"))
	{
		setenv("HALO_TICK_OWNERS", "2", 1);
		tick_owners_mode();
		errors_logged = 0;
		test_owners();
	}
	else if (!strcmp(which, "discard"))
	{
		setenv("HALO_TICK_OWNERS", "2", 1);
		tick_owners_mode();
		errors_logged = 0;
		test_discard();
	}
	else if (!strcmp(which, "threads"))
	{
		setenv("HALO_TICK_OWNERS", "2", 1);
		tick_owners_mode();
		errors_logged = 0;
		test_threads();
	}
	else if (!strcmp(which, "pool"))
	{
		setenv("HALO_TICK_OWNERS", "2", 1);
		tick_owners_mode();
		errors_logged = 0;
		test_pool();
	}
	else
	{
		printf("unknown case %s\n", which);
		return 2;
	}
	if (failures)
	{
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("%s: ok\n", which);
	return 0;
}
