/*
WIIU_VOICE_CORE.C

The Wii U's voice chat without the console (wiiu_voice_core.h): the queues, the speakers' rings, the resampler, the
microphone ring's two readings, the stale packets. Plain C and atomics, so that a desktop tests every line of it.
*/

#include "wiiu_voice_core.h"

#include <math.h>
#include <string.h>

/* musl's, the same on every machine (port/include/halo_math.h) */
double halo_sin(double x);

/* ---------- a queue of records between two threads */

void wiiu_voice_queue_init(struct wiiu_voice_queue *queue, void *records, uint32_t capacity, uint32_t record_size)
{
	queue->head = queue->tail = 0;
	queue->capacity = capacity;
	queue->record_size = record_size;
	queue->records = records;
}

static unsigned char *queue_record(const struct wiiu_voice_queue *queue, uint32_t index)
{
	return queue->records + (index & (queue->capacity - 1)) * queue->record_size;
}

int wiiu_voice_queue_put(struct wiiu_voice_queue *queue, const void *record)
{
	const uint32_t head = __atomic_load_n(&queue->head, __ATOMIC_RELAXED);
	/* (acquire: the consumer's copy of the record this one overwrites is over before the overwriting starts) */
	const uint32_t tail = __atomic_load_n(&queue->tail, __ATOMIC_ACQUIRE);

	if (head - tail >= queue->capacity)
		return 0;
	memcpy(queue_record(queue, head), record, queue->record_size);
	__atomic_store_n(&queue->head, head + 1, __ATOMIC_RELEASE);
	return 1;
}

uint32_t wiiu_voice_queue_put_newest(struct wiiu_voice_queue *queue, const void *record, uint32_t keep)
{
	const uint32_t head = __atomic_load_n(&queue->head, __ATOMIC_RELAXED);
	uint32_t tail = __atomic_load_n(&queue->tail, __ATOMIC_ACQUIRE);
	uint32_t dropped = 0;

	/* (fewer than the capacity kept, so that the record written is never the one a consumer is copying and may still take:
	that one is at the tail, and the head stays less than keep past it) */
	if (keep < 1)
		keep = 1;
	if (keep > queue->capacity - 1)
		keep = queue->capacity - 1;
	while (head - tail >= keep)
	{
		/* (the oldest dropped; when the consumer takes it first, the failed exchange gives the tail it left) */
		if (__atomic_compare_exchange_n(&queue->tail, &tail, tail + 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		{
			tail++;
			dropped++;
		}
	}
	memcpy(queue_record(queue, head), record, queue->record_size);
	__atomic_store_n(&queue->head, head + 1, __ATOMIC_RELEASE);
	return dropped;
}

int wiiu_voice_queue_take(struct wiiu_voice_queue *queue, void *record)
{
	uint32_t tail = __atomic_load_n(&queue->tail, __ATOMIC_ACQUIRE);

	for (;;)
	{
		const uint32_t head = __atomic_load_n(&queue->head, __ATOMIC_ACQUIRE);

		if (head == tail)
			return 0;
		memcpy(record, queue_record(queue, tail), queue->record_size);
		/* (taken only if it is still the oldest: had the producer dropped it, the copy may be of a newer record half written
		over it, and is thrown away) */
		if (__atomic_compare_exchange_n(&queue->tail, &tail, tail + 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			return 1;
	}
}

uint32_t wiiu_voice_queue_drop_all(struct wiiu_voice_queue *queue)
{
	uint32_t tail = __atomic_load_n(&queue->tail, __ATOMIC_ACQUIRE);

	for (;;)
	{
		const uint32_t head = __atomic_load_n(&queue->head, __ATOMIC_ACQUIRE);

		if (head == tail)
			return 0;
		if (__atomic_compare_exchange_n(&queue->tail, &tail, head, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			return head - tail;
	}
}

uint32_t wiiu_voice_queue_count(const struct wiiu_voice_queue *queue)
{
	/* (the tail first: the head read after it is never behind it) */
	const uint32_t tail = __atomic_load_n(&queue->tail, __ATOMIC_ACQUIRE);
	const uint32_t head = __atomic_load_n(&queue->head, __ATOMIC_ACQUIRE);

	return head - tail;
}

/* ---------- a speaker's samples, from the voice thread to the mixer */

#define RING_MASK (WIIU_VOICE_RING_SAMPLES - 1u)

void wiiu_voice_ring_init(struct wiiu_voice_ring *ring)
{
	memset(ring, 0, sizeof(*ring));
}

void wiiu_voice_ring_restart(struct wiiu_voice_ring *ring, int in_use)
{
	const uint32_t state = __atomic_load_n(&ring->state, __ATOMIC_RELAXED);

	/* (where the new generation's samples begin, stored before the generation that tells the mixer to look) */
	__atomic_store_n(&ring->flush_at, __atomic_load_n(&ring->write, __ATOMIC_RELAXED), __ATOMIC_RELAXED);
	__atomic_store_n(&ring->state, (((state >> 1) + 1u) << 1) | (in_use ? 1u : 0u), __ATOMIC_RELEASE);
}

int wiiu_voice_ring_put(struct wiiu_voice_ring *ring, const float *samples, uint32_t count)
{
	const uint32_t write = __atomic_load_n(&ring->write, __ATOMIC_RELAXED);
	/* (acquire: what the mixer has read it is done with) */
	const uint32_t read = __atomic_load_n(&ring->read, __ATOMIC_ACQUIRE);
	uint32_t index;

	/* (a ring that cannot take the frame drops it: the speaker is a third of a second ahead of the mix already, and
	only the mixer moves the reading on) */
	if (count > WIIU_VOICE_RING_SAMPLES - (write - read))
		return 0;
	for (index = 0; index < count; index++)
		ring->samples[(write + index) & RING_MASK] = samples[index];
	__atomic_store_n(&ring->write, write + count, __ATOMIC_RELEASE);
	return 1;
}

void wiiu_voice_ring_set_gains(struct wiiu_voice_ring *ring, float left, float right)
{
	/* (each a 32-bit word stored whole: a mix that reads one new and one old glides to both all the same) */
	__atomic_store(&ring->gain_left, &left, __ATOMIC_RELAXED);
	__atomic_store(&ring->gain_right, &right, __ATOMIC_RELAXED);
}

uint32_t wiiu_voice_ring_mix(struct wiiu_voice_ring *ring, float *output, uint32_t frames, float volume, uint32_t prebuffer)
{
	const uint32_t state = __atomic_load_n(&ring->state, __ATOMIC_ACQUIRE);
	uint32_t write, read, count, frame;
	float left, right, step_left, step_right;

	if ((state >> 1) != ring->generation)
	{
		const uint32_t flush_at = __atomic_load_n(&ring->flush_at, __ATOMIC_RELAXED);

		/* (a new speaker, or none: the last one's samples skipped, never the new one's, so the reading only moves on) */
		ring->generation = state >> 1;
		if ((int32_t)(flush_at - ring->read) > 0)
			__atomic_store_n(&ring->read, flush_at, __ATOMIC_RELEASE);
		__atomic_store_n(&ring->playing, 0u, __ATOMIC_RELAXED);
		ring->left = ring->right = 0.0f;
	}
	if (!(state & 1u))
		return 0;
	write = __atomic_load_n(&ring->write, __ATOMIC_ACQUIRE);
	read = ring->read;
	count = write - read;
	/* (the network's jitter: played once prebuffer samples wait, and waiting again once they have run out) */
	if (!ring->playing && count >= prebuffer)
		__atomic_store_n(&ring->playing, 1u, __ATOMIC_RELAXED);
	if (!ring->playing)
		return 0;
	__atomic_load(&ring->gain_left, &left, __ATOMIC_RELAXED);
	__atomic_load(&ring->gain_right, &right, __ATOMIC_RELAXED);
	left *= volume;
	right *= volume;
	if (count > frames)
		count = frames;
	/* (the gains glide to their new values over the chunk, as voice_audio.c's do) */
	step_left = count ? (left - ring->left) / (float)count : 0.0f;
	step_right = count ? (right - ring->right) / (float)count : 0.0f;
	for (frame = 0; frame < count; frame++)
	{
		const float sample = ring->samples[(read + frame) & RING_MASK];

		ring->left += step_left;
		ring->right += step_right;
		output[frame * 2] += sample * ring->left;
		output[frame * 2 + 1] += sample * ring->right;
	}
	/* (release: the samples read may be written over now) */
	__atomic_store_n(&ring->read, read + count, __ATOMIC_RELEASE);
	if (write == read + count)
		__atomic_store_n(&ring->playing, 0u, __ATOMIC_RELAXED);
	return count;
}

uint32_t wiiu_voice_ring_buffered(const struct wiiu_voice_ring *ring)
{
	/* (the reading first: the writing read after it is never behind it) */
	const uint32_t read = __atomic_load_n(&ring->read, __ATOMIC_ACQUIRE);
	const uint32_t write = __atomic_load_n(&ring->write, __ATOMIC_ACQUIRE);

	return write - read;
}

int wiiu_voice_ring_in_use(const struct wiiu_voice_ring *ring)
{
	return (__atomic_load_n(&ring->state, __ATOMIC_ACQUIRE) & 1u) != 0;
}

int wiiu_voice_ring_playing(const struct wiiu_voice_ring *ring)
{
	return __atomic_load_n(&ring->playing, __ATOMIC_RELAXED) != 0;
}

/* ---------- the microphone's 32 kHz to 48 kHz */

/* Three outputs for every two inputs: the input put up to 96 kHz (two zeros after each sample), a low pass there, and every
second sample of that kept; worked as three phases of the low pass, so that no zero is multiplied. The microphone's band ends
at 16 kHz, and the images of it that the putting up makes begin there: the low pass (a Kaiser window, beta 7, of a sinc
cut at 13.5 kHz) keeps up to about 11 kHz as it is and takes the images 70 dB down, which is more than Opus at a voice
chat's rates keeps of either (it codes up to 8 kHz at 24 kbps, 12 kHz at the most). */
#define RESAMPLER_LENGTH (3u * WIIU_VOICE_RESAMPLER_TAPS)
#define RESAMPLER_RATE 96000.0
#define RESAMPLER_CUTOFF 13500.0
#define RESAMPLER_BETA 7.0
#define PI 3.14159265358979323846

/* each phase's coefficients, newest input last (so that each output is a dot product with the history in a row) */
static float resampler_phases[3][WIIU_VOICE_RESAMPLER_TAPS];

/* the modified Bessel function of order 0, from its series (the Kaiser window's) */
static double bessel_i0(double x)
{
	const double half = x / 2.0;
	double sum = 1.0, term = 1.0;
	int k;

	for (k = 1; k < 100; k++)
	{
		term *= (half / k) * (half / k);
		sum += term;
		if (term < sum * 1e-17)
			break;
	}
	return sum;
}

void wiiu_voice_resampler_coefficients(void)
{
	const double center = (RESAMPLER_LENGTH - 1u) / 2.0;
	const double scale = 2.0 * RESAMPLER_CUTOFF / RESAMPLER_RATE;
	const double window_scale = bessel_i0(RESAMPLER_BETA);
	double taps[RESAMPLER_LENGTH];
	uint32_t n, phase, j;

	for (n = 0; n < RESAMPLER_LENGTH; n++)
	{
		const double x = (double)n - center;
		const double ratio = x / center;
		const double argument = PI * scale * x;

		taps[n] = (argument == 0.0 ? 1.0 : halo_sin(argument) / argument) *
			bessel_i0(RESAMPLER_BETA * sqrt(1.0 - ratio * ratio)) / window_scale;
	}
	/* (each phase made to add up to 1, so that a constant comes out as it went in, with no ripple at the phases' rate) */
	for (phase = 0; phase < 3; phase++)
	{
		double sum = 0.0;

		for (j = 0; j < WIIU_VOICE_RESAMPLER_TAPS; j++)
			sum += taps[phase + 3 * j];
		for (j = 0; j < WIIU_VOICE_RESAMPLER_TAPS; j++)
			resampler_phases[phase][WIIU_VOICE_RESAMPLER_TAPS - 1 - j] = (float)(taps[phase + 3 * j] / sum);
	}
}

void wiiu_voice_resampler_reset(struct wiiu_voice_resampler *resampler)
{
	memset(resampler->history, 0, sizeof(resampler->history));
	/* (the first input goes to 0) */
	resampler->at = WIIU_VOICE_RESAMPLER_TAPS - 1;
	resampler->inputs = 0;
}

static float resampler_dot(const float *coefficients, const float *history)
{
	float sum = 0.0f;
	uint32_t j;

	for (j = 0; j < WIIU_VOICE_RESAMPLER_TAPS; j++)
		sum += coefficients[j] * history[j];
	return sum;
}

uint32_t wiiu_voice_resample(struct wiiu_voice_resampler *resampler, const float *input, uint32_t count, float *output)
{
	uint32_t made = 0, index;

	for (index = 0; index < count; index++)
	{
		const float *window;

		resampler->at = resampler->at + 1 == WIIU_VOICE_RESAMPLER_TAPS ? 0 : resampler->at + 1;
		resampler->history[resampler->at] = resampler->history[resampler->at + WIIU_VOICE_RESAMPLER_TAPS] = input[index];
		/* (the newest TAPS inputs, oldest first) */
		window = resampler->history + resampler->at + 1;
		/* (output m is the 96 kHz sample 2m: phase 2m mod 3 of the low pass, at input 2m / 3. An even input is where
		phases 0 and 2 fall, an odd one phase 1) */
		if (!(resampler->inputs & 1u))
		{
			output[made++] = resampler_dot(resampler_phases[0], window);
			output[made++] = resampler_dot(resampler_phases[2], window);
		}
		else
		{
			output[made++] = resampler_dot(resampler_phases[1], window);
		}
		resampler->inputs++;
	}
	return made;
}

/* ---------- the microphone's frames, from the voice thread to the game thread */

void wiiu_voice_capture_init(struct wiiu_voice_capture *capture, struct wiiu_voice_queue *frames, uint32_t keep)
{
	memset(capture, 0, sizeof(*capture));
	capture->frames = frames;
	capture->keep = keep;
	wiiu_voice_resampler_reset(&capture->resampler);
}

void wiiu_voice_capture_reset(struct wiiu_voice_capture *capture)
{
	wiiu_voice_resampler_reset(&capture->resampler);
	capture->filled = 0;
}

static void capture_frame_done(struct wiiu_voice_capture *capture, const float *frame)
{
	capture->dropped += wiiu_voice_queue_put_newest(capture->frames, frame, capture->keep);
	capture->made++;
}

void wiiu_voice_capture_samples(struct wiiu_voice_capture *capture, const float *samples, uint32_t count)
{
	/* (a few at a time, so that the outputs fit here) */
	float resampled[64 * 3 / 2 + 1];

	while (count)
	{
		const uint32_t take = count < 64 ? count : 64;
		const uint32_t made = wiiu_voice_resample(&capture->resampler, samples, take, resampled);
		uint32_t index;

		for (index = 0; index < made; index++)
		{
			capture->frame[capture->filled++] = resampled[index];
			if (capture->filled == WIIU_VOICE_FRAME_SAMPLES)
			{
				capture_frame_done(capture, capture->frame);
				capture->filled = 0;
			}
		}
		samples += take;
		count -= take;
	}
}

void wiiu_voice_capture_tone(struct wiiu_voice_capture *capture)
{
	float frame[WIIU_VOICE_FRAME_SAMPLES];
	uint32_t index;

	for (index = 0; index < WIIU_VOICE_FRAME_SAMPLES; index++)
	{
		frame[index] = 0.3f * (float)halo_sin(capture->tone_phase);
		capture->tone_phase += 2.0 * PI * 440.0 / 48000.0;
	}
	capture->tone_phase = fmod(capture->tone_phase, 2.0 * PI);
	capture_frame_done(capture, frame);
}

float wiiu_voice_level(const float *frame)
{
	double sum = 0.0;
	uint32_t index;

	for (index = 0; index < WIIU_VOICE_FRAME_SAMPLES; index++)
		sum += (double)frame[index] * frame[index];
	return (float)sqrt(sum / WIIU_VOICE_FRAME_SAMPLES);
}

/* ---------- the system's microphone ring */

void wiiu_mic_reader_init(struct wiiu_mic_reader *reader, int mode, uint32_t unit_bytes, uint32_t ring_samples)
{
	memset(reader, 0, sizeof(*reader));
	reader->mode = mode;
	reader->unit_bytes = unit_bytes == 1 ? 1 : 2;
	reader->ring_samples = ring_samples;
}

void wiiu_mic_reader_span(struct wiiu_mic_reader *reader, int32_t available, int32_t position, uint32_t *first, uint32_t *count,
	uint32_t *consume)
{
	const uint32_t ring = reader->ring_samples;
	/* (the status's units in a sample) */
	const uint32_t per_sample = 2u / reader->unit_bytes;
	const uint32_t at = (position < 0 ? 0u : (uint32_t)position / per_sample) % ring;

	reader->statuses++;
	*first = at;
	*count = 0;
	*consume = 0;
	if (reader->mode == WIIU_MIC_CONSUME)
	{
		uint32_t waiting = available <= 0 ? 0u : (uint32_t)available / per_sample;

		/* (the read index should be where the last consumption left it: one that is not says the reading is not the system's) */
		if (reader->expecting && at != reader->expected)
			reader->mismatches++;
		if (waiting >= ring)
		{
			reader->full++;
			waiting = ring;
		}
		*count = waiting;
		*consume = waiting * per_sample;
		reader->expected = (at + waiting) % ring;
		reader->expecting = 1;
	}
	else
	{
		/* (the first status says where the system is: what it wrote before is not this opening's) */
		if (!reader->started)
		{
			reader->read = at;
			reader->started = 1;
		}
		*first = reader->read;
		*count = (at + ring - reader->read) % ring;
		reader->read = at;
	}
}

uint32_t wiiu_mic_ring_samples(const unsigned char *ring, uint32_t ring_samples, uint32_t first, uint32_t count, float *output)
{
	uint32_t at = first % ring_samples;
	uint32_t index;

	for (index = 0; index < count; index++)
	{
		/* (big-endian whatever this machine is: the tests run on little-endian ones too) */
		int value = (ring[2 * at] << 8) | ring[2 * at + 1];

		if (value >= 32768)
			value -= 65536;
		output[index] = (float)value * (1.0f / 32768.0f);
		at = at + 1 == ring_samples ? 0 : at + 1;
	}
	return count;
}

/* ---------- Opus packets, from the voice thread back to the game thread */

uint32_t wiiu_voice_packet_take(struct wiiu_voice_queue *queue, uint32_t now_ms, uint32_t maximum_age_ms, uint32_t maximum,
	unsigned char *packet, uint32_t *dropped)
{
	struct wiiu_voice_packet record;

	if (queue->record_size != sizeof(record))
		return 0;
	while (wiiu_voice_queue_take(queue, &record))
	{
		/* (a packet from before a pause, or of a frame the game gave before it stalled, is not sent late; nor one longer
		than the quality now allows) */
		if (now_ms - record.queued_ms > maximum_age_ms || !record.length || record.length > maximum ||
			record.length > WIIU_VOICE_MAXIMUM_PACKET)
		{
			(*dropped)++;
			continue;
		}
		memcpy(packet, record.bytes, record.length);
		return record.length;
	}
	return 0;
}

/* ---------- a speaker's sequence */

int wiiu_voice_sequence_lost(int *sequenced, uint16_t *next, uint16_t sequence, int concealed)
{
	int lost = 0;

	if (*sequenced)
	{
		const uint16_t ahead = (uint16_t)(sequence - *next);

		/* (late, or repeated: dropped) */
		if (ahead >= 0x8000u)
			return -1;
		/* (a few lost are made up for; more, it starts over) */
		if (ahead <= concealed)
			lost = ahead;
	}
	*sequenced = 1;
	*next = (uint16_t)(sequence + 1);
	return lost;
}
