/*
WIIU_VOICE_CORE.H

The parts of the Wii U's voice chat (wiiu_voice_audio.c) that need no console, so that a desktop can test them
(tools/harness/tests/test_wiiu_voice.py, also big-endian under qemu-ppc): the queues between its three threads, each
speaker's ring from the voice thread to the mixer, the microphone's 32 kHz resampled to the 48 kHz Opus is given, and
the two ways the system's microphone ring may have to be read.

The threads: the game thread copies frames and packets in and out and never waits; the voice thread (core 0, below the
mixer) reads the microphone and runs Opus; the mixer (wiiu_audio.c, core 0) reads a ring for each speaker. Each queue and
ring has one producer and one consumer, and neither ever waits for the other.
*/

#ifndef HALO_WIIU_VOICE_CORE_H
#define HALO_WIIU_VOICE_CORE_H

#include <stdint.h>

/* 20 ms at 48 kHz, mono: voice_audio.h's VOICE_FRAME_SAMPLES (the test checks they agree) */
#define WIIU_VOICE_FRAME_SAMPLES 960u
/* the longest Opus packet: voice_audio.h's VOICE_MAXIMUM_PACKET */
#define WIIU_VOICE_MAXIMUM_PACKET 255u

/* ---------- a queue of records between two threads */

/* head counts the records ever put (the producer's) and tail those ever taken or dropped. Both run on past 2^32 and wrap,
which the unsigned differences allow for. The consumer takes a record by copying it and then moving the tail on from where
it found it; the producer may move the tail on too, to drop the oldest record when the queue holds as many as it should
keep, and a copy whose record was dropped meanwhile (its move then fails) is thrown away, never used. */
struct wiiu_voice_queue
{
	uint32_t head;
	uint32_t tail;
	uint32_t capacity; /* a power of two */
	uint32_t record_size;
	unsigned char *records;
};

void wiiu_voice_queue_init(struct wiiu_voice_queue *queue, void *records, uint32_t capacity, uint32_t record_size);
/* the producer: the record put if there is room; whether it was */
int wiiu_voice_queue_put(struct wiiu_voice_queue *queue, const void *record);
/* the producer: the record put, the oldest dropped so that at most keep (1 to capacity - 1) are queued; how many were dropped */
uint32_t wiiu_voice_queue_put_newest(struct wiiu_voice_queue *queue, const void *record, uint32_t keep);
/* the consumer: the oldest record copied out and taken; whether there was one */
int wiiu_voice_queue_take(struct wiiu_voice_queue *queue, void *record);
/* either side: every record queued dropped; how many */
uint32_t wiiu_voice_queue_drop_all(struct wiiu_voice_queue *queue);
uint32_t wiiu_voice_queue_count(const struct wiiu_voice_queue *queue);

/* ---------- a speaker's samples, from the voice thread to the mixer */

/* 341 ms at 48 kHz (a power of two, for the counters' wrap) */
#define WIIU_VOICE_RING_SAMPLES 16384u

/* write and the gains are the voice thread's, read and the rest below them the mixer's. A new speaker in the slot, or one
forgotten, is a new generation: the mixer starts again from flush_at, where the voice thread's samples for the new one begin,
so that it never plays the last one's. */
struct wiiu_voice_ring
{
	uint32_t write;
	/* generation << 1, and 1 while a speaker has the slot */
	uint32_t state;
	uint32_t flush_at;
	/* the speaker's gain and pan for each side (the voice thread works them out); the mixer adds the volume */
	float gain_left, gain_right;
	/* the mixer's */
	uint32_t read;
	uint32_t generation;
	uint32_t playing;
	float left, right;
	float samples[WIIU_VOICE_RING_SAMPLES];
};

void wiiu_voice_ring_init(struct wiiu_voice_ring *ring);
/* the voice thread: the slot given to a speaker (in_use 1) or freed (0), a new generation either way */
void wiiu_voice_ring_restart(struct wiiu_voice_ring *ring, int in_use);
/* the voice thread: the samples after those it put, if they all fit before the mixer's reading; whether they did */
int wiiu_voice_ring_put(struct wiiu_voice_ring *ring, const float *samples, uint32_t count);
void wiiu_voice_ring_set_gains(struct wiiu_voice_ring *ring, float left, float right);
/* the mixer: the speaker added to the output (stereo, frames of it) at the volume, once prebuffer samples have been
buffered, until they run out; how many frames it added */
uint32_t wiiu_voice_ring_mix(struct wiiu_voice_ring *ring, float *output, uint32_t frames, float volume, uint32_t prebuffer);
/* any thread: samples waiting, and whether a speaker has the slot and is being played (as near now as can be known) */
uint32_t wiiu_voice_ring_buffered(const struct wiiu_voice_ring *ring);
int wiiu_voice_ring_in_use(const struct wiiu_voice_ring *ring);
int wiiu_voice_ring_playing(const struct wiiu_voice_ring *ring);

/* ---------- the microphone's 32 kHz to 48 kHz */

/* the coefficients for each of the three output phases (the low pass is 96 samples of 96 kHz long) */
#define WIIU_VOICE_RESAMPLER_TAPS 32u

struct wiiu_voice_resampler
{
	/* the last TAPS inputs, written twice (at at and at + TAPS) so that the newest TAPS are always in a row */
	float history[2 * WIIU_VOICE_RESAMPLER_TAPS];
	uint32_t at;
	/* inputs taken, ever: an even one makes two outputs, an odd one one */
	uint32_t inputs;
};

/* the low pass, worked out once (before any resampling, by any thread) */
void wiiu_voice_resampler_coefficients(void);
void wiiu_voice_resampler_reset(struct wiiu_voice_resampler *resampler);
/* count inputs resampled: the outputs written (output has room for count * 3 / 2 + 1) */
uint32_t wiiu_voice_resample(struct wiiu_voice_resampler *resampler, const float *input, uint32_t count, float *output);

/* ---------- the microphone's frames, from the voice thread to the game thread */

struct wiiu_voice_capture
{
	struct wiiu_voice_resampler resampler;
	float frame[WIIU_VOICE_FRAME_SAMPLES];
	uint32_t filled;
	/* frames of float samples, at most keep of them waiting: a game that stalled must not talk late */
	struct wiiu_voice_queue *frames;
	uint32_t keep;
	uint32_t made, dropped;
	/* debug.voice_test's tone */
	double tone_phase;
};

void wiiu_voice_capture_init(struct wiiu_voice_capture *capture, struct wiiu_voice_queue *frames, uint32_t keep);
/* the microphone opened again: nothing of before carried over */
void wiiu_voice_capture_reset(struct wiiu_voice_capture *capture);
/* 32 kHz samples heard: resampled, and each frame they fill queued */
void wiiu_voice_capture_samples(struct wiiu_voice_capture *capture, const float *samples, uint32_t count);
/* debug.voice_test: a frame of 440 Hz at 0.3 in place of the microphone's, as voice_audio.c makes it */
void wiiu_voice_capture_tone(struct wiiu_voice_capture *capture);

/* the root mean square of a frame (1 at full scale), as voice_audio.c's voice_audio_level */
float wiiu_voice_level(const float *frame);

/* ---------- the system's microphone ring (mic.rpl writes it in the app's memory: signed 16-bit, big-endian) */

/* The two ways the system's status may be meant, from the published sources: consume (Cemu, and the example of
capitalistspz's wiiu_audio_examples): availableData samples wait from bufferPos, the read index, and MICSetDataConsumed
moves it on; position (WiiUCamera): bufferPos is where the system writes, in bytes, and the app follows it with a cursor of
its own and never consumes. The probe (wiiu_voice_audio.c) finds which on a console. */
enum
{
	WIIU_MIC_CONSUME,
	WIIU_MIC_POSITION,
};

struct wiiu_mic_reader
{
	int mode;
	/* bytes in each unit of bufferPos, availableData and what is consumed: 1 if they count bytes, 2 if samples */
	uint32_t unit_bytes;
	uint32_t ring_samples;
	/* the position mode's cursor (samples into the ring), from the first status on */
	uint32_t read;
	int started;
	/* what was seen: statuses read, a ring found full (the consume mode), and a read index that did not move as consumed */
	uint32_t statuses, full, mismatches;
	uint32_t expected;
	int expecting;
};

void wiiu_mic_reader_init(struct wiiu_mic_reader *reader, int mode, uint32_t unit_bytes, uint32_t ring_samples);
/* from a status: the run of the ring to read now (its first sample and how many, at most the ring), and how much to tell
MICSetDataConsumed afterwards (0: nothing) */
void wiiu_mic_reader_span(struct wiiu_mic_reader *reader, int32_t available, int32_t position, uint32_t *first, uint32_t *count,
	uint32_t *consume);
/* count samples of the ring from first (round its end) as floats (1 at full scale); count */
uint32_t wiiu_mic_ring_samples(const unsigned char *ring, uint32_t ring_samples, uint32_t first, uint32_t count, float *output);

/* The microphone probe's finding (wiiu_voice_audio.c), from a second of statuses with nothing consumed (bufferPos and
availableData at a poll every 10 ms) and the highest bufferPos while every reading was consumed at once: bufferPos moving by
itself before the ring could have filled is a write position; availableData growing while it stands is a read index. Either
counts bytes if bufferPos went past the ring's samples. The mode and unit found, and the words the probe writes for them
("unknown": nothing moved, the mode and unit then consume's) */
const char *wiiu_mic_probe_finding(const int32_t *positions, const int32_t *availables, uint32_t polls, uint32_t ring_samples,
	int32_t consumed_highest, int *mode, uint32_t *unit_bytes);

/* ---------- Opus packets, from the voice thread back to the game thread */

struct wiiu_voice_packet
{
	/* when the game thread gave the frame (milliseconds, wrapping) */
	uint32_t queued_ms;
	uint32_t length;
	unsigned char bytes[WIIU_VOICE_MAXIMUM_PACKET];
};

/* the consumer: the oldest packet not older than maximum_age_ms and no longer than maximum, copied out; those older or
longer are dropped (counted in dropped). Its length, 0 for none */
uint32_t wiiu_voice_packet_take(struct wiiu_voice_queue *queue, uint32_t now_ms, uint32_t maximum_age_ms, uint32_t maximum,
	unsigned char *packet, uint32_t *dropped);

/* ---------- a speaker's sequence (voice_audio.c's voice_audio_play) */

/* how many frames were lost just before a packet (up to `concealed`, made up for), or -1 for a late or repeated packet,
dropped; a larger gap starts over. sequenced and next are the speaker's. */
int wiiu_voice_sequence_lost(int *sequenced, uint16_t *next, uint16_t sequence, int concealed);

#endif
