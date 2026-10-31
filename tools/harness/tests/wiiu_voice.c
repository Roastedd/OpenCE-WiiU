/*
WIIU_VOICE.C (test)

The Wii U's voice chat without the console (port/wiiu/src/wiiu_voice_core.c): the queues between its threads (one
producer and one consumer, counters that wrap, the backlog kept, a producer dropping the record a consumer is copying, two
threads at once), a speaker's ring for the mixer, the 32 to 48 kHz resampler (its response, and chunks that come out as
the whole did), the microphone's frames, both readings of the system's microphone ring over simulated ones, the stale
packets and a speaker's sequence. test_wiiu_voice.py takes the code (under_test.inc) and voice_audio.h's constants
(config.inc), and runs it big-endian too.
*/

#include "harness.h"

#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>

/* (musl's on the console: the C library's here, the same to the last bit or near it) */
double halo_sin(double x)
{
	return sin(x);
}

/* the queue's copies out, to put a producer's drops between the halves of a consumer's copy (queue-race) */
static const unsigned char *hooked_records;
static size_t hooked_size;
static void (*copy_hook)(void);

static void *fixture_memcpy(void *destination, const void *source, size_t size)
{
	const unsigned char *from = source;

	if (copy_hook && from >= hooked_records && from < hooked_records + hooked_size)
	{
		void (*hook)(void) = copy_hook;
		const size_t half = size / 2;

		copy_hook = NULL;
		memcpy(destination, source, half);
		hook();
		memcpy((unsigned char *)destination + half, from + half, size - half);
		return destination;
	}
	return memcpy(destination, source, size);
}

/* (a C library may have a macro of its own for it) */
#undef memcpy
#define memcpy(destination, source, size) fixture_memcpy(destination, source, size)
#include "config.inc"
#include "under_test.inc"
#undef memcpy

/* ---------- records that say whether they were copied whole */

struct record
{
	uint32_t words[16];
};

static struct record numbered(uint32_t number)
{
	struct record record;
	int index;

	for (index = 0; index < 16; index++)
		record.words[index] = number;
	return record;
}

static int whole(const struct record *record)
{
	int index;

	for (index = 1; index < 16; index++)
		if (record->words[index] != record->words[0])
			return FALSE;
	return TRUE;
}

/* a pseudo-random number generator, the same everywhere */
static uint32_t random_state = 12345;

static uint32_t random_next(void)
{
	random_state = random_state * 1103515245u + 12345u;
	return random_state >> 8;
}

static float random_sample(void)
{
	return (float)((double)(random_next() & 0xffff) / 32768.0 - 1.0);
}

/* ---------- the queue */

static struct wiiu_voice_queue race_queue;
static uint32_t race_next;

/* (the producer's turn, in the middle of the consumer's copy: four put, each dropping the oldest, one of them over the record
being copied) */
static void race_producer(void)
{
	int index;

	for (index = 0; index < 4; index++)
	{
		struct record record = numbered(race_next++);

		wiiu_voice_queue_put_newest(&race_queue, &record, 3);
	}
}

#define THREAD_RECORDS 100000u

static struct wiiu_voice_queue thread_queue;
static struct record thread_storage[8];
static uint32_t thread_keep;
static uint32_t thread_done, thread_dropped;

static void *thread_producer(void *unused)
{
	uint32_t number;

	(void)unused;
	for (number = 1; number <= THREAD_RECORDS; number++)
	{
		struct record record = numbered(number);

		if (thread_keep)
			thread_dropped += wiiu_voice_queue_put_newest(&thread_queue, &record, thread_keep);
		else
			while (!wiiu_voice_queue_put(&thread_queue, &record))
				sched_yield();
	}
	__atomic_store_n(&thread_done, 1u, __ATOMIC_RELEASE);
	return NULL;
}

/* both sides at once on two threads: every record taken is whole and newer than the last; none is lost but those dropped */
static void queue_threads(uint32_t keep)
{
	pthread_t producer;
	struct record record;
	uint32_t last = 0, taken = 0;

	wiiu_voice_queue_init(&thread_queue, thread_storage, 8, sizeof(struct record));
	thread_keep = keep;
	thread_done = thread_dropped = 0;
	CHECK(!pthread_create(&producer, NULL, thread_producer, NULL), "no producer thread");
	for (;;)
	{
		const int done = __atomic_load_n(&thread_done, __ATOMIC_ACQUIRE);

		if (wiiu_voice_queue_take(&thread_queue, &record))
		{
			CHECK(whole(&record), "a record taken half overwritten (%u and %u)", record.words[0], record.words[15]);
			CHECK(record.words[0] > last, "record %u taken after %u", record.words[0], last);
			last = record.words[0];
			taken++;
			continue;
		}
		if (done)
			break;
	}
	pthread_join(producer, NULL);
	CHECK(last == THREAD_RECORDS, "the newest record (%u) was not taken: the last was %u", THREAD_RECORDS, last);
	CHECK(taken + thread_dropped == THREAD_RECORDS, "%u taken and %u dropped of %u", taken, thread_dropped, THREAD_RECORDS);
	if (!keep)
		CHECK(taken == THREAD_RECORDS, "a queue that refuses when full lost records: %u of %u", taken, THREAD_RECORDS);
}

/* ---------- the microphone's ring, simulated */

#define RING 10240u

/* the samples the system makes, in order (they say where they are in it) */
static int16_t sample_value(uint32_t index)
{
	return (int16_t)(index * 7919u + 13u);
}

static void ring_store(unsigned char *ring, uint32_t at, int16_t value)
{
	ring[2 * at] = (unsigned char)((uint16_t)value >> 8);
	ring[2 * at + 1] = (unsigned char)value;
}

/* Cemu's: availableData samples wait from bufferPos, the read index, until consumed; a full ring takes no more (or, overwriting,
moves the read index on over its oldest) */
struct consume_system
{
	unsigned char ring[2 * RING];
	uint32_t read, waiting;
	uint32_t unit_bytes;
	int ignores_consumption;
	int overwrites;
	/* what it keeps, in order, for the reader to be checked against */
	int16_t kept[2 * RING];
	uint32_t kept_first, kept_count;
	uint32_t made;
};

static void consume_system_hear(struct consume_system *system, uint32_t count)
{
	while (count--)
	{
		const int16_t value = sample_value(system->made++);

		if (system->waiting == RING && system->overwrites)
		{
			system->read = (system->read + 1) % RING;
			system->waiting--;
			system->kept_first = (system->kept_first + 1) % (2 * RING);
			system->kept_count--;
		}
		if (system->waiting == RING)
			continue;
		ring_store(system->ring, (system->read + system->waiting) % RING, value);
		system->waiting++;
		system->kept[(system->kept_first + system->kept_count++) % (2 * RING)] = value;
	}
}

static void consume_system_status(const struct consume_system *system, int32_t *available, int32_t *position)
{
	const uint32_t per_sample = 2u / system->unit_bytes;

	*available = (int32_t)(system->waiting * per_sample);
	*position = (int32_t)(system->read * per_sample);
}

static void consume_system_consumed(struct consume_system *system, uint32_t amount)
{
	const uint32_t samples = amount / (2u / system->unit_bytes);

	if (system->ignores_consumption)
		return;
	CHECK(samples <= system->waiting, "%u consumed of %u waiting", samples, system->waiting);
	system->read = (system->read + samples) % RING;
	system->waiting -= samples;
}

/* WiiUCamera's: bufferPos is where the system writes, availableData means nothing the reader may use */
struct position_system
{
	unsigned char ring[2 * RING];
	uint32_t write;
	uint32_t unit_bytes;
	uint32_t made;
};

static void position_system_hear(struct position_system *system, uint32_t count)
{
	while (count--)
	{
		ring_store(system->ring, system->write, sample_value(system->made++));
		system->write = (system->write + 1) % RING;
	}
}

/* a reading of either: its samples, as floats from the ring, checked against those the system made, from `from` on; whether
they all matched */
static int read_matches(struct wiiu_mic_reader *reader, const unsigned char *ring, int32_t available, int32_t position,
	uint32_t *expected, uint32_t *consume)
{
	static float samples[RING];
	uint32_t first, count, index;
	int matches = TRUE;

	wiiu_mic_reader_span(reader, available, position, &first, &count, consume);
	CHECK(count <= RING, "a span of %u samples from a ring of %u", count, RING);
	wiiu_mic_ring_samples(ring, RING, first, count, samples);
	for (index = 0; index < count; index++)
	{
		if (samples[index] != (float)sample_value((*expected)++) / 32768.0f)
			matches = FALSE;
	}
	return matches;
}

/* the probe's two seconds (wiiu_voice_audio.c's probe_run) over a simulated system, a poll every 10 ms: one with nothing
consumed, one consuming each reading at once; its finding */
enum
{
	SYSTEM_CONSUME,
	SYSTEM_CONSUME_OVERWRITING,
	SYSTEM_POSITION,
	SYSTEM_SILENT,
};

static const char *probe_simulated(int kind, uint32_t unit, int *mode, uint32_t *unit_bytes)
{
	static struct consume_system consume;
	static struct position_system position;
	int32_t positions[100], availables[100], consumed_highest = 0;
	uint32_t poll;

	memset(&consume, 0, sizeof(consume));
	memset(&position, 0, sizeof(position));
	consume.unit_bytes = position.unit_bytes = unit;
	consume.overwrites = kind == SYSTEM_CONSUME_OVERWRITING;
	for (poll = 0; poll < 100; poll++)
	{
		if (kind == SYSTEM_POSITION)
		{
			position_system_hear(&position, 320);
			positions[poll] = (int32_t)(position.write * (2u / unit));
			availables[poll] = 4096;
		}
		else if (kind == SYSTEM_SILENT)
		{
			positions[poll] = availables[poll] = 0;
		}
		else
		{
			consume_system_hear(&consume, 320);
			consume_system_status(&consume, &availables[poll], &positions[poll]);
		}
	}
	for (poll = 0; poll < 100; poll++)
	{
		int32_t available = 0, at = 0, after_available, after_at = 0;

		if (kind == SYSTEM_POSITION)
		{
			position_system_hear(&position, 320);
			at = after_at = (int32_t)(position.write * (2u / unit));
		}
		else if (kind != SYSTEM_SILENT)
		{
			consume_system_hear(&consume, 320);
			consume_system_status(&consume, &available, &at);
			consume_system_consumed(&consume, (uint32_t)available);
			consume_system_status(&consume, &after_available, &after_at);
		}
		if (at > consumed_highest)
			consumed_highest = at;
		if (after_at > consumed_highest)
			consumed_highest = after_at;
	}
	return wiiu_mic_probe_finding(positions, availables, 100, RING, consumed_highest, mode, unit_bytes);
}

/* ---------- the resampler */

/* the gain at a frequency (input 0.5 at 32 kHz), and how far below the tone everything else is (dB), over a second of output
long after the start (a whole number of the tone's periods: an exact fit) */
static void response(double frequency, double *gain, double *rest_db)
{
	enum { INPUTS = 32000 * 5 / 4, START = 4000, LENGTH = 48000 };
	static float input[INPUTS], output[INPUTS * 3 / 2 + 2];
	struct wiiu_voice_resampler resampler;
	double a = 0.0, b = 0.0, signal = 0.0, rest = 0.0;
	uint32_t index, made;

	for (index = 0; index < INPUTS; index++)
		input[index] = (float)(0.5 * sin(2.0 * 3.14159265358979323846 * frequency * index / 32000.0));
	wiiu_voice_resampler_reset(&resampler);
	made = wiiu_voice_resample(&resampler, input, INPUTS, output);
	CHECK(made >= START + LENGTH, "%u outputs", made);
	for (index = START; index < START + LENGTH; index++)
	{
		const double t = 2.0 * 3.14159265358979323846 * frequency * index / 48000.0;

		a += output[index] * sin(t);
		b += output[index] * cos(t);
	}
	a *= 2.0 / LENGTH;
	b *= 2.0 / LENGTH;
	for (index = START; index < START + LENGTH; index++)
	{
		const double t = 2.0 * 3.14159265358979323846 * frequency * index / 48000.0;
		const double fit = a * sin(t) + b * cos(t);

		signal += fit * fit;
		rest += (output[index] - fit) * (output[index] - fit);
	}
	*gain = sqrt(a * a + b * b) / 0.5;
	*rest_db = 10.0 * log10(rest / signal + 1e-30);
}

/* ---------- the cases */

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	wiiu_voice_resampler_coefficients();

	CASE("constants")
	{
		CHECK(WIIU_VOICE_FRAME_SAMPLES == VOICE_FRAME_SAMPLES, "a frame: %u here, %d in voice_audio.h", WIIU_VOICE_FRAME_SAMPLES,
			VOICE_FRAME_SAMPLES);
		CHECK(WIIU_VOICE_MAXIMUM_PACKET == VOICE_MAXIMUM_PACKET, "the longest packet: %u here, %d in voice_audio.h",
			WIIU_VOICE_MAXIMUM_PACKET, VOICE_MAXIMUM_PACKET);
		return 0;
	}
	CASE("queue-order")
	{
		static struct record storage[8];
		struct wiiu_voice_queue queue;
		struct record record;
		uint32_t put = 1, taken = 1, round, index;

		wiiu_voice_queue_init(&queue, storage, 8, sizeof(struct record));
		CHECK(!wiiu_voice_queue_take(&queue, &record), "an empty queue gave a record");
		/* (the counters about to wrap) */
		queue.head = queue.tail = 0xfffffff3u;
		for (round = 0; round < 40; round++)
		{
			for (index = 0; index < 8; index++)
			{
				record = numbered(put++);
				CHECK(wiiu_voice_queue_put(&queue, &record), "round %u: record %u refused by a queue of %u", round, index,
					wiiu_voice_queue_count(&queue));
			}
			record = numbered(999999);
			CHECK(!wiiu_voice_queue_put(&queue, &record), "round %u: a full queue of 8 took a ninth", round);
			CHECK(wiiu_voice_queue_count(&queue) == 8, "round %u: %u queued", round, wiiu_voice_queue_count(&queue));
			for (index = 0; index < 8; index++)
			{
				CHECK(wiiu_voice_queue_take(&queue, &record), "round %u: record %u missing", round, index);
				CHECK(whole(&record) && record.words[0] == taken, "round %u: record %u taken where %u was due", round, record.words[0],
					taken);
				taken++;
			}
			CHECK(!wiiu_voice_queue_take(&queue, &record), "round %u: an emptied queue gave a record", round);
		}
		/* (three put and two taken at a time, for many laps of the eight and across the wrap) */
		queue.head = queue.tail = 0xffffff00u;
		for (round = 0; round < 1000; round++)
		{
			for (index = 0; index < 3 && wiiu_voice_queue_count(&queue) < 8; index++)
			{
				record = numbered(put++);
				CHECK(wiiu_voice_queue_put(&queue, &record), "lap %u: refused with room", round);
			}
			for (index = 0; index < 2; index++)
			{
				CHECK(wiiu_voice_queue_take(&queue, &record), "lap %u: nothing to take", round);
				CHECK(whole(&record) && record.words[0] == taken, "lap %u: record %u where %u was due", round, record.words[0], taken);
				taken++;
			}
		}
		CHECK(queue.head < 0x1000u, "the counters did not wrap (%08x)", queue.head);
		return 0;
	}
	CASE("queue-backlog")
	{
		static struct record storage[8];
		struct wiiu_voice_queue queue;
		struct record record;
		uint32_t index, dropped = 0;

		wiiu_voice_queue_init(&queue, storage, 8, sizeof(struct record));
		queue.head = queue.tail = 0xfffffffeu;
		/* twelve put with nobody taking: the newest five kept, the seven oldest dropped */
		for (index = 0; index < 12; index++)
		{
			record = numbered(index);
			dropped += wiiu_voice_queue_put_newest(&queue, &record, 5);
			CHECK(wiiu_voice_queue_count(&queue) <= 5, "%u queued when 5 are kept", wiiu_voice_queue_count(&queue));
		}
		CHECK(dropped == 7, "%u dropped of 12, keeping 5", dropped);
		for (index = 7; index < 12; index++)
		{
			CHECK(wiiu_voice_queue_take(&queue, &record), "record %u missing", index);
			CHECK(whole(&record) && record.words[0] == index, "record %u where %u was due", record.words[0], index);
		}
		CHECK(!wiiu_voice_queue_take(&queue, &record), "more than the five newest");
		/* (as many as the capacity is never kept: one less, so the consumer's record is never written over) */
		for (index = 0; index < 20; index++)
		{
			record = numbered(100 + index);
			wiiu_voice_queue_put_newest(&queue, &record, 8);
		}
		CHECK(wiiu_voice_queue_count(&queue) == 7, "%u kept of a capacity of 8", wiiu_voice_queue_count(&queue));
		CHECK(wiiu_voice_queue_drop_all(&queue) == 7, "not all dropped");
		CHECK(!wiiu_voice_queue_take(&queue, &record) && wiiu_voice_queue_drop_all(&queue) == 0, "a dropped queue not empty");
		record = numbered(5);
		CHECK(wiiu_voice_queue_put_newest(&queue, &record, 0) == 0 && wiiu_voice_queue_count(&queue) == 1, "keep 0 is keep 1");
		record = numbered(6);
		CHECK(wiiu_voice_queue_put_newest(&queue, &record, 0) == 1, "keep 1 kept two");
		CHECK(wiiu_voice_queue_take(&queue, &record) && record.words[0] == 6, "not the newest kept");
		return 0;
	}
	CASE("queue-race")
	{
		static struct record storage[4];
		struct record record;
		uint32_t number;

		wiiu_voice_queue_init(&race_queue, storage, 4, sizeof(struct record));
		for (number = 1; number <= 3; number++)
		{
			record = numbered(number);
			wiiu_voice_queue_put_newest(&race_queue, &record, 3);
		}
		race_next = 4;
		hooked_records = (const unsigned char *)storage;
		hooked_size = sizeof(storage);
		copy_hook = race_producer;
		/* (the consumer copies record 1 while the producer drops it and 2, 3 and 4, and writes record 5 where 1 was) */
		CHECK(wiiu_voice_queue_take(&race_queue, &record), "nothing taken");
		CHECK(!copy_hook, "the producer did not run in the middle of the copy");
		CHECK(whole(&record), "a record half written over was taken (%u, %u)", record.words[0], record.words[15]);
		CHECK(record.words[0] == 5, "record %u taken, where 5 is the oldest left", record.words[0]);
		for (number = 6; number <= 7; number++)
		{
			CHECK(wiiu_voice_queue_take(&race_queue, &record) && whole(&record) && record.words[0] == number,
				"record %u where %u was due", record.words[0], number);
		}
		CHECK(!wiiu_voice_queue_take(&race_queue, &record), "a dropped record came back (%u)", record.words[0]);
		return 0;
	}
	CASE("queue-threads")
	{
		/* the backlog the microphone's frames have, then a queue that refuses when full */
		queue_threads(5);
		queue_threads(0);
		return 0;
	}
	CASE("ring")
	{
		static struct wiiu_voice_ring ring;
		static float samples[WIIU_VOICE_RING_SAMPLES];
		float output[2 * 64];
		uint32_t index, mixed, total;

		wiiu_voice_ring_init(&ring);
		/* (the counters about to wrap) */
		ring.write = ring.read = ring.flush_at = 0xfffff000u;
		wiiu_voice_ring_restart(&ring, 1);
		wiiu_voice_ring_set_gains(&ring, 1.0f, 0.5f);
		CHECK(wiiu_voice_ring_in_use(&ring), "a speaker's slot not in use");
		for (index = 0; index < 960; index++)
			samples[index] = 0.5f;
		/* buffered: nothing played until three frames are there */
		CHECK(wiiu_voice_ring_put(&ring, samples, 960), "a frame refused by an empty ring");
		memset(output, 0, sizeof(output));
		CHECK(wiiu_voice_ring_mix(&ring, output, 64, 1.0f, 2880) == 0 && output[0] == 0.0f, "played before 60 ms were buffered");
		CHECK(wiiu_voice_ring_put(&ring, samples, 960) && wiiu_voice_ring_put(&ring, samples, 960), "frames refused");
		mixed = wiiu_voice_ring_mix(&ring, output, 64, 1.0f, 2880);
		CHECK(mixed == 64, "%u frames mixed of 64", mixed);
		/* (the gains glide from nothing over the first chunk) */
		CHECK(fabs(output[0] - 0.5 / 64) < 1e-6 && fabs(output[1] - 0.25 / 64) < 1e-6, "the first frame %g %g, not gliding", output[0],
			output[1]);
		CHECK(fabs(output[126] - 0.5) < 1e-5 && fabs(output[127] - 0.25) < 1e-5, "the chunk's end %g %g, not at the gains", output[126],
			output[127]);
		CHECK(wiiu_voice_ring_playing(&ring), "not playing");
		/* the volume, then played out: it stops, and waits for three frames again */
		total = 64;
		while ((memset(output, 0, sizeof(output)), mixed = wiiu_voice_ring_mix(&ring, output, 64, 2.0f, 2880)) > 0)
		{
			if (total >= 128)
				CHECK(fabs(output[0] - 1.0) < 1e-5 && fabs(output[1] - 0.5) < 1e-5, "at volume 2: %g %g", output[0], output[1]);
			total += mixed;
		}
		CHECK(total == 2880, "%u played of 2880", total);
		CHECK(!wiiu_voice_ring_playing(&ring) && wiiu_voice_ring_buffered(&ring) == 0, "still playing when run out");
		CHECK(wiiu_voice_ring_put(&ring, samples, 960) && wiiu_voice_ring_mix(&ring, output, 64, 1.0f, 2880) == 0,
			"played again without buffering");
		/* a new speaker in the slot: the last one's samples never played */
		for (index = 0; index < 1920; index++)
			samples[index] = 0.25f;
		CHECK(wiiu_voice_ring_put(&ring, samples, 1920), "refused");
		wiiu_voice_ring_restart(&ring, 1);
		for (index = 0; index < 2880; index++)
			samples[index] = -0.5f;
		CHECK(wiiu_voice_ring_put(&ring, samples, 2880), "the new speaker's refused");
		total = 0;
		while ((memset(output, 0, sizeof(output)), mixed = wiiu_voice_ring_mix(&ring, output, 64, 1.0f, 2880)) > 0)
		{
			for (index = 0; index < mixed; index++)
				CHECK(output[2 * index] <= 0.0f, "the last speaker's sample (%g) played for the new one", output[2 * index]);
			total += mixed;
		}
		CHECK(total == 2880, "%u of the new speaker's 2880 played", total);
		/* a full ring refuses a frame, and keeps what it has */
		for (index = 0; index < WIIU_VOICE_RING_SAMPLES; index++)
			samples[index] = (float)(index % 1000) / 1000.0f;
		CHECK(wiiu_voice_ring_put(&ring, samples, WIIU_VOICE_RING_SAMPLES), "a whole ring refused by an empty one");
		CHECK(!wiiu_voice_ring_put(&ring, samples, 960), "a full ring took a frame");
		CHECK(wiiu_voice_ring_buffered(&ring) == WIIU_VOICE_RING_SAMPLES, "%u buffered", wiiu_voice_ring_buffered(&ring));
		wiiu_voice_ring_set_gains(&ring, 1.0f, 1.0f);
		total = 0;
		while ((memset(output, 0, sizeof(output)), mixed = wiiu_voice_ring_mix(&ring, output, 64, 1.0f, 2880)) > 0)
		{
			/* (past the glide's chunk, the samples as they were put) */
			if (total >= 64)
				for (index = 0; index < mixed; index++)
					CHECK(fabs(output[2 * index] - (float)((total + index) % 1000) / 1000.0f) < 1e-5, "sample %u is %g", total + index,
						output[2 * index]);
			total += mixed;
		}
		CHECK(total == WIIU_VOICE_RING_SAMPLES, "%u played of a full ring", total);
		/* forgotten: nothing played */
		CHECK(wiiu_voice_ring_put(&ring, samples, 2880), "refused");
		wiiu_voice_ring_restart(&ring, 0);
		CHECK(!wiiu_voice_ring_in_use(&ring) && wiiu_voice_ring_mix(&ring, output, 64, 1.0f, 2880) == 0, "a forgotten speaker played");
		CHECK(ring.read == ring.write, "the forgotten speaker's samples not skipped");
		CHECK(ring.write < 0xfffff000u, "the counters did not wrap (%08x)", ring.write);
		return 0;
	}
	CASE("resampler-dc")
	{
		static float input[2000], output[3001];
		struct wiiu_voice_resampler resampler;
		uint32_t index, count, made;

		for (index = 0; index < 2000; index++)
			input[index] = 0.7f;
		wiiu_voice_resampler_reset(&resampler);
		made = wiiu_voice_resample(&resampler, input, 2000, output);
		CHECK(made == 3000, "%u outputs of 2000 inputs", made);
		for (index = 100; index < made; index++)
			CHECK(fabs(output[index] - 0.7) < 2e-6, "output %u of a constant 0.7 is %.9f", index, output[index]);
		/* (three outputs for two inputs: two for an even one, one for an odd one) */
		for (count = 0; count <= 11; count++)
		{
			wiiu_voice_resampler_reset(&resampler);
			made = wiiu_voice_resample(&resampler, input, count, output);
			CHECK(made == (3 * count + 1) / 2, "%u outputs of %u inputs", made, count);
		}
		return 0;
	}
	CASE("resampler-response")
	{
		static const double passband[] = { 100.0, 300.0, 1000.0, 2000.0, 3000.0, 4000.0, 6000.0, 8000.0, 9000.0, 10000.0 };
		double gain, rest;
		unsigned index;

		/* the voice's band through as it was, and nothing else (the images the putting up makes) above -70 dB */
		for (index = 0; index < sizeof(passband) / sizeof(passband[0]); index++)
		{
			response(passband[index], &gain, &rest);
			CHECK(fabs(gain - 1.0) < 0.006, "%.0f Hz: a gain of %.5f (%.3f dB)", passband[index], gain, 20.0 * log10(gain));
			CHECK(rest < -70.0, "%.0f Hz: the rest only %.1f dB down", passband[index], rest);
		}
		/* the band's end: most of 15 kHz gone, 15.9 kHz (whose image is at 16.1 kHz) all but */
		response(15000.0, &gain, &rest);
		CHECK(gain < 0.05, "15 kHz: a gain of %.5f", gain);
		response(15900.0, &gain, &rest);
		CHECK(gain < 0.001, "15.9 kHz: a gain of %.5f", gain);
		return 0;
	}
	CASE("resampler-chunks")
	{
		enum { INPUTS = 40000 };
		static float input[INPUTS], whole_output[INPUTS * 3 / 2 + 2], pieces[INPUTS * 3 / 2 + 2];
		struct wiiu_voice_resampler resampler;
		uint32_t index, made, done = 0, made_pieces = 0;

		for (index = 0; index < INPUTS; index++)
			input[index] = random_sample();
		wiiu_voice_resampler_reset(&resampler);
		made = wiiu_voice_resample(&resampler, input, INPUTS, whole_output);
		/* the same in pieces of 0 to 700 samples (a 10 ms poll is 320): every output the same to the last bit, so no piece's
		edge can click */
		wiiu_voice_resampler_reset(&resampler);
		while (done < INPUTS)
		{
			uint32_t take = random_next() % 701u;

			if (take > INPUTS - done)
				take = INPUTS - done;
			made_pieces += wiiu_voice_resample(&resampler, input + done, take, pieces + made_pieces);
			done += take;
		}
		CHECK(made_pieces == made, "%u outputs in pieces, %u at once", made_pieces, made);
		for (index = 0; index < made; index++)
			CHECK(pieces[index] == whole_output[index], "output %u is %.9g in pieces, %.9g at once", index, pieces[index], whole_output[index]);
		return 0;
	}
	CASE("capture")
	{
		enum { INPUTS = 16000 };
		static float input[INPUTS], reference[INPUTS * 3 / 2 + 2], frames[8][WIIU_VOICE_FRAME_SAMPLES], frame[WIIU_VOICE_FRAME_SAMPLES];
		struct wiiu_voice_resampler resampler;
		struct wiiu_voice_queue queue;
		struct wiiu_voice_capture capture;
		uint32_t index, made, first;

		for (index = 0; index < INPUTS; index++)
			input[index] = random_sample();
		wiiu_voice_resampler_reset(&resampler);
		made = wiiu_voice_resample(&resampler, input, INPUTS, reference);
		CHECK(made == 24000, "%u outputs", made);
		/* half a second heard in polls of 10 ms with nobody taking: 25 frames made, the newest five kept */
		wiiu_voice_queue_init(&queue, frames, 8, sizeof(frames[0]));
		wiiu_voice_capture_init(&capture, &queue, 5);
		for (index = 0; index < INPUTS; index += 320)
			wiiu_voice_capture_samples(&capture, input + index, 320);
		CHECK(capture.made == 25 && capture.dropped == 20, "%u frames made, %u dropped", capture.made, capture.dropped);
		CHECK(wiiu_voice_queue_count(&queue) == 5, "%u frames waiting", wiiu_voice_queue_count(&queue));
		for (first = 20; first < 25; first++)
		{
			CHECK(wiiu_voice_queue_take(&queue, frame), "frame %u missing", first);
			CHECK(!memcmp(frame, reference + first * WIIU_VOICE_FRAME_SAMPLES, sizeof(frame)), "frame %u is not the resampler's", first);
		}
		/* opened again: what was half a frame is not carried over */
		wiiu_voice_capture_samples(&capture, input, 100);
		wiiu_voice_capture_reset(&capture);
		CHECK(capture.filled == 0, "a part frame carried over");
		wiiu_voice_capture_samples(&capture, input, 640);
		CHECK(wiiu_voice_queue_take(&queue, frame) && !memcmp(frame, reference, sizeof(frame)), "the first frame after reopening is not the "
			"resampler's from the start");
		return 0;
	}
	CASE("tone")
	{
		static float frames[8][WIIU_VOICE_FRAME_SAMPLES];
		float frame[WIIU_VOICE_FRAME_SAMPLES], ones[WIIU_VOICE_FRAME_SAMPLES];
		struct wiiu_voice_queue queue;
		struct wiiu_voice_capture capture;
		uint32_t index;

		wiiu_voice_queue_init(&queue, frames, 8, sizeof(frames[0]));
		wiiu_voice_capture_init(&capture, &queue, 5);
		wiiu_voice_capture_tone(&capture);
		wiiu_voice_capture_tone(&capture);
		CHECK(wiiu_voice_queue_take(&queue, frame), "no tone");
		CHECK(fabs(wiiu_voice_level(frame) - 0.3 / sqrt(2.0)) < 0.002, "the tone's level %.4f", wiiu_voice_level(frame));
		CHECK(wiiu_voice_queue_take(&queue, frame), "no second frame");
		/* (the second frame goes on where the first ended: 8.8 periods of 440 Hz later) */
		CHECK(fabs(frame[0] - 0.3 * sin(2.0 * 3.14159265358979323846 * 0.8)) < 1e-4, "the second frame starts at %.5f", frame[0]);
		for (index = 0; index < WIIU_VOICE_FRAME_SAMPLES; index++)
			ones[index] = index & 1 ? -1.0f : 1.0f;
		CHECK(fabs(wiiu_voice_level(ones) - 1.0) < 1e-6, "full scale's level %.6f", wiiu_voice_level(ones));
		memset(ones, 0, sizeof(ones));
		CHECK(wiiu_voice_level(ones) == 0.0f, "silence's level");
		return 0;
	}
	CASE("mic-consume")
	{
		static struct consume_system system;
		struct wiiu_mic_reader reader;
		uint32_t unit, poll, expected, consume;
		int32_t available, position;

		/* in samples (Cemu's) and in bytes */
		for (unit = 2; unit >= 1; unit--)
		{
			memset(&system, 0, sizeof(system));
			system.unit_bytes = unit;
			wiiu_mic_reader_init(&reader, WIIU_MIC_CONSUME, unit, RING);
			expected = 0;
			/* ten seconds of polls every 10 ms, the system's 32 kHz in uneven bursts: thirty laps of the ring */
			for (poll = 0; poll < 1000; poll++)
			{
				consume_system_hear(&system, 300u + random_next() % 41u);
				consume_system_status(&system, &available, &position);
				CHECK(read_matches(&reader, system.ring, available, position, &expected, &consume), "unit %u, poll %u: not the samples "
					"made", unit, poll);
				consume_system_consumed(&system, consume);
			}
			CHECK(expected == system.made, "unit %u: %u read of %u made", unit, expected, system.made);
			CHECK(reader.mismatches == 0 && reader.full == 0, "unit %u: %u read indexes elsewhere, %u full", unit, reader.mismatches, reader.full);
			/* a stall of 400 ms: the ring fills, the system takes no more, the reader takes all it kept, in order */
			consume_system_hear(&system, 12800);
			consume_system_status(&system, &available, &position);
			{
				uint32_t kept = expected;

				CHECK(read_matches(&reader, system.ring, available, position, &kept, &consume), "unit %u: a full ring not read in order",
					unit);
				CHECK(kept - expected == RING && reader.full == 1, "unit %u: %u read of a full ring, %u full", unit, kept - expected, reader.full);
			}
			consume_system_consumed(&system, consume);
		}
		/* a system that does not move its read index as consumed is seen (the log says so) */
		memset(&system, 0, sizeof(system));
		system.unit_bytes = 2;
		system.ignores_consumption = TRUE;
		wiiu_mic_reader_init(&reader, WIIU_MIC_CONSUME, 2, RING);
		expected = 0;
		for (poll = 0; poll < 3; poll++)
		{
			consume_system_hear(&system, 320);
			consume_system_status(&system, &available, &position);
			read_matches(&reader, system.ring, available, position, &expected, &consume);
			consume_system_consumed(&system, consume);
		}
		CHECK(reader.mismatches == 2, "%u read indexes elsewhere", reader.mismatches);
		return 0;
	}
	CASE("mic-position")
	{
		static struct position_system system;
		struct wiiu_mic_reader reader;
		uint32_t unit, poll, expected, consume, before;

		/* in bytes (WiiUCamera's) and in samples */
		for (unit = 1; unit <= 2; unit++)
		{
			memset(&system, 0, sizeof(system));
			system.unit_bytes = unit;
			/* (what the system wrote before the first status is not this opening's) */
			position_system_hear(&system, 5000);
			before = system.made;
			wiiu_mic_reader_init(&reader, WIIU_MIC_POSITION, unit, RING);
			expected = before;
			for (poll = 0; poll < 1000; poll++)
			{
				const int32_t position = (int32_t)(system.write * (2u / unit));

				/* (availableData is not used: here it means nothing) */
				CHECK(read_matches(&reader, system.ring, 4096, position, &expected, &consume), "unit %u, poll %u: not the samples made", unit,
					poll);
				CHECK(consume == 0, "unit %u: a position reading consumed %u", unit, consume);
				position_system_hear(&system, 300u + random_next() % 41u);
			}
			{
				const int32_t position = (int32_t)(system.write * (2u / unit));

				CHECK(read_matches(&reader, system.ring, 4096, position, &expected, &consume), "unit %u: the last poll", unit);
			}
			CHECK(expected == system.made, "unit %u: %u read of %u made", unit, expected - before, system.made - before);
		}
		/* the readings are told apart: this system read by consuming is not what it made */
		memset(&system, 0, sizeof(system));
		system.unit_bytes = 1;
		wiiu_mic_reader_init(&reader, WIIU_MIC_CONSUME, 2, RING);
		expected = 0;
		position_system_hear(&system, 320);
		CHECK(!read_matches(&reader, system.ring, 4096, (int32_t)(system.write * 2u), &expected, &consume),
			"a write position read as a read index gave the samples made");
		return 0;
	}
	CASE("probe-finding")
	{
		static const struct
		{
			int kind;
			uint32_t unit;
			const char *finding;
			int mode;
			uint32_t unit_bytes;
		} systems[] =
		{
			{ SYSTEM_CONSUME, 2, "consume samples", WIIU_MIC_CONSUME, 2 },
			{ SYSTEM_CONSUME, 1, "consume bytes", WIIU_MIC_CONSUME, 1 },
			/* (a read index the system moves on once the ring is full is still a read index) */
			{ SYSTEM_CONSUME_OVERWRITING, 2, "consume samples", WIIU_MIC_CONSUME, 2 },
			{ SYSTEM_POSITION, 1, "position bytes", WIIU_MIC_POSITION, 1 },
			{ SYSTEM_POSITION, 2, "position samples", WIIU_MIC_POSITION, 2 },
			/* (nothing moved: consume's reading, the default) */
			{ SYSTEM_SILENT, 2, "unknown", WIIU_MIC_CONSUME, 2 },
		};
		unsigned index;

		for (index = 0; index < sizeof(systems) / sizeof(systems[0]); index++)
		{
			int mode = -1;
			uint32_t unit_bytes = 0;
			const char *finding = probe_simulated(systems[index].kind, systems[index].unit, &mode, &unit_bytes);

			CHECK(!strcmp(finding, systems[index].finding) && mode == systems[index].mode && unit_bytes == systems[index].unit_bytes,
				"system %u: found \"%s\" (mode %d, %u bytes a unit), not \"%s\"", index, finding, mode, unit_bytes, systems[index].finding);
		}
		return 0;
	}
	CASE("stale")
	{
		static struct wiiu_voice_packet storage[8];
		struct wiiu_voice_queue queue;
		struct wiiu_voice_packet packet;
		unsigned char out[WIIU_VOICE_MAXIMUM_PACKET];
		uint32_t dropped = 0, length, index;

		wiiu_voice_queue_init(&queue, storage, 8, sizeof(storage[0]));
		for (index = 0; index < 4; index++)
		{
			memset(&packet, 0, sizeof(packet));
			packet.queued_ms = 1000 + 10 * index;
			packet.length = 10 * (index + 1);
			memset(packet.bytes, (int)packet.length, packet.length);
			CHECK(wiiu_voice_queue_put(&queue, &packet), "refused");
		}
		/* at 1070: the frame of 1000 is 70 ms old and dropped; that of 1010, 60 ms, is sent */
		length = wiiu_voice_packet_take(&queue, 1070, 60, 255, out, &dropped);
		CHECK(length == 20 && out[0] == 20 && out[19] == 20 && dropped == 1, "%u bytes (%u), %u dropped", length, out[0], dropped);
		CHECK(wiiu_voice_packet_take(&queue, 1075, 60, 255, out, &dropped) == 30, "not the oldest fresh");
		/* a packet longer than the quality now allows is dropped; so is an empty one */
		memset(&packet, 0, sizeof(packet));
		packet.queued_ms = 1075;
		packet.length = 200;
		CHECK(wiiu_voice_queue_put(&queue, &packet), "refused");
		packet.length = 0;
		CHECK(wiiu_voice_queue_put(&queue, &packet), "refused");
		CHECK(wiiu_voice_packet_take(&queue, 1080, 60, 100, out, &dropped) == 40, "the packet of 1030");
		CHECK(wiiu_voice_packet_take(&queue, 1080, 60, 100, out, &dropped) == 0 && dropped == 3, "%u dropped", dropped);
		/* the clock's wrap: 32 ms old is fresh */
		packet.queued_ms = 0xfffffff0u;
		packet.length = 5;
		CHECK(wiiu_voice_queue_put(&queue, &packet), "refused");
		CHECK(wiiu_voice_packet_take(&queue, 0x10u, 60, 255, out, &dropped) == 5, "a packet across the clock's wrap dropped");
		/* the tail of the last time the player talked, seconds ago: never sent */
		for (index = 0; index < 3; index++)
		{
			packet.queued_ms = 5000 + 20 * index;
			CHECK(wiiu_voice_queue_put(&queue, &packet), "refused");
		}
		CHECK(wiiu_voice_packet_take(&queue, 9000, 60, 255, out, &dropped) == 0 && dropped == 6 && !wiiu_voice_queue_count(&queue),
			"an old talk's packets kept");
		return 0;
	}
	CASE("sequence")
	{
		int sequenced = FALSE;
		uint16_t next = 0;

		CHECK(wiiu_voice_sequence_lost(&sequenced, &next, 100, 3) == 0, "the first packet");
		CHECK(wiiu_voice_sequence_lost(&sequenced, &next, 101, 3) == 0, "the next");
		CHECK(wiiu_voice_sequence_lost(&sequenced, &next, 103, 3) == 1, "one lost");
		CHECK(wiiu_voice_sequence_lost(&sequenced, &next, 107, 3) == 3, "three lost");
		CHECK(wiiu_voice_sequence_lost(&sequenced, &next, 112, 3) == 0, "four lost: not starting over");
		CHECK(wiiu_voice_sequence_lost(&sequenced, &next, 111, 3) == -1, "a late packet played");
		CHECK(wiiu_voice_sequence_lost(&sequenced, &next, 112, 3) == -1, "a repeated packet played");
		CHECK(wiiu_voice_sequence_lost(&sequenced, &next, 113, 3) == 0, "after the late one");
		sequenced = FALSE;
		CHECK(wiiu_voice_sequence_lost(&sequenced, &next, 0xffffu, 3) == 0, "a new speaker");
		CHECK(wiiu_voice_sequence_lost(&sequenced, &next, 0, 3) == 0, "the sequence's wrap");
		CHECK(wiiu_voice_sequence_lost(&sequenced, &next, 2, 3) == 1, "one lost after the wrap");
		return 0;
	}
	/* (not a check: the response's figures, for a person: test_wiiu_voice.py metrics) */
	CASE("metrics")
	{
		static const double frequencies[] = { 100.0, 1000.0, 4000.0, 8000.0, 10000.0, 11000.0, 12000.0, 13000.0, 14000.0, 15000.0, 15900.0 };
		double gain, rest;
		unsigned index;

		for (index = 0; index < sizeof(frequencies) / sizeof(frequencies[0]); index++)
		{
			response(frequencies[index], &gain, &rest);
			printf("%7.0f Hz: gain %+8.3f dB, everything else %6.1f dB below the tone\n", frequencies[index], 20.0 * log10(gain), -rest);
		}
		return 0;
	}
	fprintf(stderr, "no case %s\n", case_name);
	return 2;
}
