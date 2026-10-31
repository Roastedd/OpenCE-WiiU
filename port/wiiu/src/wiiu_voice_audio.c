/*
WIIU_VOICE_AUDIO.C

Voice chat's sound on the Wii U (voice_audio.h; port/linux/src/voice_audio.c is the other builds'): the GamePad's
microphone (mic.rpl), Opus (port/third_party/opus) and the other players' voices mixed into the game's output. A headset
in the GamePad's jack takes the built-in microphone's place on the same stream, so it needs nothing of its own; the Pro
Controller has no jack, and a USB headset has no way in (nsysuhs has no isochronous transfers).

Three threads share it, and none waits for another:

- the game thread (network_voice.c) only copies: the microphone wanted, a frame taken, a frame given to be encoded and the
  oldest packet ready taken back (20 ms late: the one given last time), a packet heard given to be played;
- the voice thread, here, on core 0 at priority 20, below the mixer's 10 (halo/voice_core.txt 2: on core 2 at the GX2
  thread's priority, as DirectSound's packet decoder is, if the mixer misses chunks): every 10 ms it reads what the
  microphone has written, turns it into 48 kHz frames for the game thread (5 at most waiting), encodes the frames it was
  given and decodes the packets, into a ring for each speaker. Opus takes about 2 ms of Espresso a frame to encode and a
  fifth of that to decode, and 42 KB of stack, which the game thread, the console's bottleneck, does not have to spare;
- the mixer (wiiu_audio.c, dsound_sdl.c's mix) adds each speaker's ring to the output.

The system fills a ring in the app's memory and is asked how far (MICGetStatus); what its numbers mean is in dispute
between the published sources, so the reading is either (wiiu_voice_core.h), as the probe (halo/mic_probe.txt) found or
audio.wiiu_microphone_read says. HOME is not documented for the microphone, so it is closed before the app goes to the
background and opened again when it is back.

Not run on a console.
*/

#include "wiiu_voice_core.h"

#include "voice_audio.h"
#include "wiiu_input.h"
#include "wiiu_shell.h"

#include "../../third_party/opus/include/opus.h"

#include <coreinit/cache.h>
#include <coreinit/core.h>
#include <coreinit/mutex.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <mic/mic.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* port_config.c's; musl's (port/include/halo_math.h); wiiu_gx2.c's */
int config_boolean(const char *name);
const char *config_string(const char *name);
double halo_sin(double x);
double halo_cos(double x);
int wiiu_gx2_thread_priority(void);

typedef char voice_frame_size_assert[VOICE_FRAME_SAMPLES == WIIU_VOICE_FRAME_SAMPLES ? 1 : -1];
typedef char voice_packet_size_assert[VOICE_MAXIMUM_PACKET == WIIU_VOICE_MAXIMUM_PACKET ? 1 : -1];

#define VOICE_RATE 48000
/* the least ring mic.rpl takes: 320 ms of its 32 kHz */
#define MIC_RING_SAMPLES 0x2800u
/* the desktop's: speakers decoded at once, and how long one unheard keeps its slot (ms); a speaker plays once 60 ms are
buffered (the network's jitter); frames made up for when packets are lost, at most */
#define VOICE_SLOTS 16
#define VOICE_SLOT_IDLE_MS 5000u
#define VOICE_PREBUFFER_SAMPLES (3u * VOICE_FRAME_SAMPLES)
#define VOICE_MAXIMUM_CONCEALED 3
/* the microphone's frames kept for the game thread (the desktop's VOICE_MAXIMUM_BACKLOG) */
#define VOICE_BACKLOG_FRAMES 5u
/* a packet taken this long after its frame was given is not sent (it is of a pause, or of before the talking stopped). Each
call takes one packet, of a frame given before, so at a slow frame rate one can wait a game frame more: two game frames at
15 frames a second and a voice thread's turn are 143 ms (60 ms lost a fifth of the packets at 30 frames a second) */
#define VOICE_STALE_MS 150u
/* a microphone that would not open is tried again this often (ms) */
#define VOICE_RETRY_MS 10000u
/* the voice thread's turns: every 10 ms while the game asks for voice chat (in the last second), else every 50 ms */
#define VOICE_POLL_MS 10u
#define VOICE_IDLE_POLL_MS 50u
#define VOICE_ACTIVE_MS 1000u
/* how long HOME waits for the voice thread to close the microphone (ms) */
#define VOICE_SUSPEND_WAIT_MS 100u
#define VOICE_REPORT_MS 10000u
/* (Opus's own stack: 41.7 KB at the most, measured under qemu-ppc; with the frames' buffers, a third of this) */
#define VOICE_STACK_BYTES (128u * 1024u)
#define VOICE_PRIORITY 20

/* the queues: capacities (powers of two), and how many each keeps when the oldest are dropped */
#define CAPTURE_RECORDS 8u
#define ENCODE_RECORDS 8u
#define ENCODE_KEEP 4u
#define PACKET_RECORDS 8u
#define PACKET_KEEP 7u
#define PLAY_RECORDS 64u
#define PLAY_KEEP 63u

#define PROBE_FILE WIIU_DATA_DIRECTORY "/mic_probe.txt"
#define PROBE_RESULT_FILE WIIU_DATA_DIRECTORY "/mic_probe_result.txt"
#define PROBE_WAV_FILE WIIU_DATA_DIRECTORY "/mic_probe.wav"
#define CORE_FILE WIIU_DATA_DIRECTORY "/voice_core.txt"

enum
{
	VOICE_NOT_STARTED,
	VOICE_STARTING,
	VOICE_RUNNING,
	/* (the thread could not be made, or has ended with the app) */
	VOICE_DOWN,
};

/* the shell's requests (generation << 2 | one of these) and the voice thread's answer, the request it has carried out */
enum
{
	REQUEST_RUN,
	REQUEST_SUSPEND,
	REQUEST_STOP,
};

/* a frame given to be encoded */
struct encode_job
{
	uint32_t queued_ms;
	int32_t bitrate;
	int32_t maximum;
	float frame[VOICE_FRAME_SAMPLES];
};

/* a packet heard, or a speaker to forget */
enum
{
	PLAY_PACKET,
	PLAY_FORGET,
};

struct play_entry
{
	uint8_t kind;
	uint8_t length;
	uint16_t sequence;
	int32_t speaker;
	float gain, pan;
	unsigned char bytes[VOICE_MAXIMUM_PACKET];
};

/* ---------- what the threads share */

static uint32_t voice_state;
static uint32_t request, acknowledged;
static uint32_t microphone_wanted;
/* the voice thread's: whether the microphone (or debug.voice_test's tone) gives frames */
static uint32_t microphone_open;
/* when the game last asked for anything of voice chat */
static uint32_t session_ms;
/* when the mixer last mixed (no mix lately: the voices sent here would never be heard) */
static uint32_t mixed_ms;
static uint32_t forget_all_requests;
static float voice_volume = 1.0f;
/* debug.voice_test: -1 until the game thread has read it (the settings are not read before the game has started) */
static int voice_test = -1;
/* the game thread's drops, for the voice thread's report */
static uint32_t stale_dropped, play_dropped, encode_dropped;

static float capture_records[CAPTURE_RECORDS][VOICE_FRAME_SAMPLES];
static struct encode_job encode_records[ENCODE_RECORDS];
static struct wiiu_voice_packet packet_records[PACKET_RECORDS];
static struct play_entry play_records[PLAY_RECORDS];
static struct wiiu_voice_queue capture_queue, encode_queue, packet_queue, play_queue;
static struct wiiu_voice_ring rings[VOICE_SLOTS];
/* each slot's speaker, for voice_audio_speaking on the game thread */
static int32_t ring_speakers[VOICE_SLOTS];

static OSThread voice_thread;
static uint8_t voice_stack[VOICE_STACK_BYTES] __attribute__((aligned(16)));
/* held by the voice thread around its microphone calls, so that HOME's last resort (wiiu_voice_suspend) can close the
microphone itself only between them */
static OSMutex microphone_lock;

/* ---------- the voice thread's own */

static struct
{
	int used;
	int speaker;
	OpusDecoder *decoder;
	int sequenced;
	uint16_t next_sequence;
	uint32_t heard_ms, logged_ms;
} slots[VOICE_SLOTS];

/* the system's ring: 0x40-aligned and a whole number of cache lines, flushed before each reading (the system writes it behind
this core's cache, or through another's) and never written by the app but before MICInit */
static unsigned char microphone_ring[MIC_RING_SAMPLES * 2] __attribute__((aligned(64)));
static MICWorkMemory microphone_work;
static int microphone_handle = -1;
static int microphone_failed;
static uint32_t microphone_failed_ms;
static uint32_t microphone_errors;
static struct wiiu_mic_reader reader;
/* the reading, chosen at the first opening (reading_choose) or by the probe's finding */
static int reading_mode = -1;
static uint32_t reading_unit;
static const char *reading_source;
static struct wiiu_voice_capture capture;
static uint32_t tone_started_ms, tone_frames;
static OpusEncoder *encoder;
static int encoder_bitrate;
static uint32_t forget_all_seen;

/* the report, every 10 s of a session */
static struct
{
	uint32_t since_ms;
	uint32_t encoded, decoded, concealed, late, no_slot, ring_full, frames_dropped;
	uint64_t encode_ticks, encode_max, decode_ticks, decode_max, turn_max;
} report;

static uint32_t now_ms(void)
{
	return (uint32_t)OSTicksToMilliseconds(OSGetSystemTime());
}

static double ticks_ms(uint64_t ticks)
{
	return (double)OSTicksToMicroseconds(ticks) / 1000.0;
}

static int running(void)
{
	return __atomic_load_n(&voice_state, __ATOMIC_ACQUIRE) == VOICE_RUNNING;
}

/* ---------- the microphone */

/* audio.wiiu_microphone_read: "auto" (the probe's finding, mic_probe_result.txt, else consume), "consume", "position", or a
mode and a unit as the probe writes them ("consume samples", "position bytes"); read once, at the first opening (an opening
must not wait for the card: HOME may come meanwhile) */
static const char *reading_chosen(int *mode, uint32_t *unit_bytes)
{
	const char *setting = config_string("audio.wiiu_microphone_read");
	const char *source = "audio.wiiu_microphone_read";
	char text[64] = "";

	if (!setting || !setting[0] || !strcmp(setting, "auto"))
	{
		FILE *file = fopen(PROBE_RESULT_FILE, "r");

		source = "the default";
		if (file)
		{
			if (fgets(text, sizeof(text), file))
				source = "the probe's finding (mic_probe_result.txt)";
			else
				text[0] = 0;
			fclose(file);
		}
	}
	else
	{
		snprintf(text, sizeof(text), "%s", setting);
	}
	/* (consume, Cemu's and the examples' reading, unless position is asked for: a game made for the console calls
	MICSetDataConsumed, and an emulator has to behave as the console does for it) */
	*mode = !strncmp(text, "position", 8) ? WIIU_MIC_POSITION : WIIU_MIC_CONSUME;
	*unit_bytes = *mode == WIIU_MIC_POSITION ? 1u : 2u;
	if (strstr(text, "samples"))
		*unit_bytes = 2u;
	else if (strstr(text, "bytes"))
		*unit_bytes = 1u;
	if (strncmp(text, "position", 8) && strncmp(text, "consume", 7))
		source = "the default";
	return source;
}

/* whether the shell asks the voice thread for anything but its turns (HOME, the close) */
static int asked_to_stop(void)
{
	return (__atomic_load_n(&request, __ATOMIC_ACQUIRE) & 3u) != REQUEST_RUN;
}

/* MICInit and MICOpen, under the microphone's lock (so that HOME's last resort cannot find it half open, wiiu_voice_suspend), and
not when HOME or the close has come meanwhile: the handle, or -1 (*error says what failed, MIC_ERROR_OK if it was HOME) */
static int microphone_open_locked(MICError *error, const char **failed)
{
	MICHandle handle;
	MICError opened;

	*error = MIC_ERROR_OK;
	*failed = NULL;
	OSLockMutex(&microphone_lock);
	if (asked_to_stop())
	{
		OSUnlockMutex(&microphone_lock);
		return -1;
	}
	/* (written back now: a dirty line the cache wrote back later would land on the system's samples) */
	memset(microphone_ring, 0, sizeof(microphone_ring));
	DCFlushRange(microphone_ring, sizeof(microphone_ring));
	microphone_work.sampleMaxCount = MIC_RING_SAMPLES;
	microphone_work.sampleBuffer = microphone_ring;
	handle = MICInit(MIC_INSTANCE_0, 0, &microphone_work, error);
	if (*error != MIC_ERROR_OK || handle < 0)
	{
		if (*error == MIC_ERROR_OK)
			*error = MIC_ERROR_INIT;
		*failed = "MICInit";
		OSUnlockMutex(&microphone_lock);
		return -1;
	}
	opened = MICOpen(handle);
	if (opened != MIC_ERROR_OK || asked_to_stop())
	{
		/* (MICOpen failed, or HOME came while it opened: closed again at once, before the release callback looks) */
		if (opened == MIC_ERROR_OK)
			MICClose(handle);
		MICUninit(handle);
		*error = opened;
		*failed = opened != MIC_ERROR_OK ? "MICOpen" : NULL;
		OSUnlockMutex(&microphone_lock);
		return -1;
	}
	microphone_handle = handle;
	OSUnlockMutex(&microphone_lock);
	return handle;
}

static int microphone_open_now(const char *why)
{
	MICError error;
	const char *failed;
	const OSTime started = OSGetSystemTime();

	if (reading_mode < 0)
		reading_source = reading_chosen(&reading_mode, &reading_unit);
	if (microphone_open_locked(&error, &failed) < 0)
	{
		if (!failed)
			return 0;
		wiiu_log("Voice chat: the GamePad's microphone could not be %s (%s: error %d); again in %u s",
			!strcmp(failed, "MICInit") ? "set up" : "opened", failed, (int)error, (unsigned)(VOICE_RETRY_MS / 1000u));
		microphone_failed = 1;
		microphone_failed_ms = now_ms();
		return 0;
	}
	microphone_failed = 0;
	microphone_errors = 0;
	wiiu_mic_reader_init(&reader, reading_mode, reading_unit, MIC_RING_SAMPLES);
	wiiu_voice_capture_reset(&capture);
	__atomic_store_n(&microphone_open, 1u, __ATOMIC_RELEASE);
	wiiu_log("Voice chat: the GamePad's microphone is open (%s; %s; its ring read as %s %s, by %s; %.1f ms)", why,
		wiiu_input_headset() > 0 ? "a headset is in its jack" : wiiu_input_headset() == 0 ? "no headset in its jack" : "its jack not read yet",
		reading_mode == WIIU_MIC_POSITION ? "position" : "consume", reading_unit == 1 ? "bytes" : "samples", reading_source,
		ticks_ms((uint64_t)(OSGetSystemTime() - started)));
	if (wiiu_input_headset() == 0)
	{
		const char *chat = config_string("audio.voice_chat");

		if (chat && !strcmp(chat, "open_mic"))
			wiiu_log("Voice chat: open mic without a headset: the GamePad's microphone hears the GamePad's speaker");
	}
	return 1;
}

static void microphone_close(const char *why)
{
	MICError closed = MIC_ERROR_OK, uninit = MIC_ERROR_OK;
	int handle;

	OSLockMutex(&microphone_lock);
	handle = microphone_handle;
	microphone_handle = -1;
	if (handle >= 0)
	{
		closed = MICClose(handle);
		uninit = MICUninit(handle);
	}
	OSUnlockMutex(&microphone_lock);
	if (handle >= 0)
		wiiu_log("Voice chat: the microphone closed (%s; MICClose %d, MICUninit %d; %u statuses read, %u with the ring full, %u read indexes "
			"not where the last consumption left them)", why, (int)closed, (int)uninit, (unsigned)reader.statuses, (unsigned)reader.full,
			(unsigned)reader.mismatches);
	/* (what it heard before is not sent when it opens again) */
	__atomic_store_n(&microphone_open, 0u, __ATOMIC_RELEASE);
	wiiu_voice_queue_drop_all(&capture_queue);
	wiiu_voice_capture_reset(&capture);
	tone_started_ms = 0;
}

/* what the system has written since the last turn: resampled into the game thread's frames */
static void microphone_poll(void)
{
	MICStatus status;
	MICError error = MIC_ERROR_INVALID_HANDLE;
	uint32_t first, count, consume;

	memset(&status, 0, sizeof(status));
	OSLockMutex(&microphone_lock);
	if (microphone_handle >= 0)
		error = MICGetStatus(microphone_handle, &status);
	OSUnlockMutex(&microphone_lock);
	if (error != MIC_ERROR_OK)
	{
		/* (a second of errors: closed, and tried again later) */
		if (++microphone_errors == 100u)
		{
			wiiu_log("Voice chat: MICGetStatus fails (error %d)", (int)error);
			microphone_close("its status cannot be read");
			microphone_failed = 1;
			microphone_failed_ms = now_ms();
		}
		return;
	}
	microphone_errors = 0;
	wiiu_mic_reader_span(&reader, status.availableData, status.bufferPos, &first, &count, &consume);
	if (count)
	{
		float samples[512];

		/* (flushed, not only thrown away: lines the system wrote through a core's cache are written back first, and the app's own,
		never written since MICInit, are only thrown away; either way the reading comes from memory) */
		DCFlushRange(microphone_ring, sizeof(microphone_ring));
		while (count)
		{
			const uint32_t take = count < 512u ? count : 512u;

			wiiu_mic_ring_samples(microphone_ring, MIC_RING_SAMPLES, first, take, samples);
			wiiu_voice_capture_samples(&capture, samples, take);
			first = (first + take) % MIC_RING_SAMPLES;
			count -= take;
		}
	}
	if (consume)
	{
		OSLockMutex(&microphone_lock);
		if (microphone_handle >= 0)
			MICSetDataConsumed(microphone_handle, (int)consume);
		OSUnlockMutex(&microphone_lock);
	}
}

/* debug.voice_test: a frame of the tone every 20 ms since the microphone was wanted, as voice_audio.c's */
static void tone_poll(uint32_t now)
{
	if (!tone_started_ms)
	{
		tone_started_ms = now ? now : 1u;
		tone_frames = 0;
		__atomic_store_n(&microphone_open, 1u, __ATOMIC_RELEASE);
	}
	while ((now - tone_started_ms) / 20u > tone_frames)
	{
		wiiu_voice_capture_tone(&capture);
		tone_frames++;
	}
}

/* ---------- Opus */

static int encoder_ready(void)
{
	int error = 0;

	if (encoder)
		return 1;
	encoder = opus_encoder_create(VOICE_RATE, 1, OPUS_APPLICATION_VOIP, &error);
	if (!encoder)
	{
		wiiu_log("Voice chat: no Opus encoder (%d)", error);
		return 0;
	}
	/* (as voice_audio.c's: a lost frame made up from the next's redundancy; modest complexity) */
	opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(1));
	opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(10));
	opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(5));
	opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
	encoder_bitrate = 0;
	return 1;
}

static void encode_waiting(void)
{
	static struct encode_job job;

	while (wiiu_voice_queue_take(&encode_queue, &job))
	{
		struct wiiu_voice_packet packet;
		const OSTime started = OSGetSystemTime();
		int length, maximum = job.maximum;
		uint64_t ticks;

		if (!encoder_ready())
			continue;
		if (job.bitrate != encoder_bitrate)
		{
			opus_encoder_ctl(encoder, OPUS_SET_BITRATE(job.bitrate));
			encoder_bitrate = job.bitrate;
		}
		if (maximum > (int)VOICE_MAXIMUM_PACKET)
			maximum = VOICE_MAXIMUM_PACKET;
		length = maximum > 0 ? opus_encode_float(encoder, job.frame, VOICE_FRAME_SAMPLES, packet.bytes, maximum) : 0;
		ticks = (uint64_t)(OSGetSystemTime() - started);
		report.encoded++;
		report.encode_ticks += ticks;
		if (ticks > report.encode_max)
			report.encode_max = ticks;
		if (length <= 0)
			continue;
		packet.queued_ms = job.queued_ms;
		packet.length = (uint32_t)length;
		wiiu_voice_queue_put_newest(&packet_queue, &packet, PACKET_KEEP);
	}
}

/* ---------- the voices */

/* the slot of a speaker, or a free one (or the longest unheard) taken for it, as voice_audio.c's voice_slot_for; -1 if none */
static int slot_for(int speaker, uint32_t now)
{
	int index, oldest = -1;

	for (index = 0; index < VOICE_SLOTS; index++)
		if (slots[index].used && slots[index].speaker == speaker)
			return index;
	for (index = 0; index < VOICE_SLOTS; index++)
	{
		if (!slots[index].used)
		{
			oldest = index;
			break;
		}
		if (now - slots[index].heard_ms >= VOICE_SLOT_IDLE_MS && (oldest < 0 || now - slots[index].heard_ms > now - slots[oldest].heard_ms))
			oldest = index;
	}
	if (oldest < 0)
		return -1;
	if (!slots[oldest].decoder)
	{
		int error = 0;

		slots[oldest].decoder = opus_decoder_create(VOICE_RATE, 1, &error);
		if (!slots[oldest].decoder)
		{
			wiiu_log("Voice chat: no Opus decoder (%d)", error);
			return -1;
		}
	}
	else
	{
		opus_decoder_ctl(slots[oldest].decoder, OPUS_RESET_STATE);
	}
	slots[oldest].used = 1;
	slots[oldest].speaker = speaker;
	slots[oldest].sequenced = 0;
	slots[oldest].heard_ms = now;
	__atomic_store_n(&ring_speakers[oldest], speaker, __ATOMIC_RELAXED);
	/* (the last speaker's samples not played for this one) */
	wiiu_voice_ring_restart(&rings[oldest], 1);
	return oldest;
}

static void slot_forget(int index)
{
	if (!slots[index].used)
		return;
	slots[index].used = 0;
	wiiu_voice_ring_restart(&rings[index], 0);
}

static void decode_packet(const struct play_entry *entry, uint32_t now)
{
	static float samples[(VOICE_MAXIMUM_CONCEALED + 1) * VOICE_FRAME_SAMPLES];
	const int index = slot_for(entry->speaker, now);
	const OSTime started = OSGetSystemTime();
	float gain, pan;
	double angle;
	int lost, total = 0, decoded;
	uint64_t ticks;

	if (index < 0)
	{
		report.no_slot++;
		return;
	}
	slots[index].heard_ms = now;
	/* (equal power, as voice_audio.c's mix: both sides at the gain when centred) */
	gain = entry->gain < 0.0f ? 0.0f : entry->gain > 2.0f ? 2.0f : entry->gain;
	pan = entry->pan < -1.0f ? -1.0f : entry->pan > 1.0f ? 1.0f : entry->pan;
	angle = (pan + 1.0) * 0.25 * 3.14159265358979;
	wiiu_voice_ring_set_gains(&rings[index], (float)(gain * halo_cos(angle) * 1.41421356), (float)(gain * halo_sin(angle) * 1.41421356));
	lost = wiiu_voice_sequence_lost(&slots[index].sequenced, &slots[index].next_sequence, entry->sequence, VOICE_MAXIMUM_CONCEALED);
	if (lost < 0)
	{
		report.late++;
		return;
	}
	while (lost > 0)
	{
		/* (the last lost from this packet's redundancy, those before it concealed) */
		const int redundancy = lost == 1;

		decoded = opus_decode_float(slots[index].decoder, redundancy ? entry->bytes : NULL, redundancy ? entry->length : 0,
			samples + total, VOICE_FRAME_SAMPLES, redundancy);
		if (decoded > 0)
			total += decoded;
		report.concealed++;
		lost--;
	}
	decoded = opus_decode_float(slots[index].decoder, entry->bytes, entry->length, samples + total, VOICE_FRAME_SAMPLES, 0);
	if (decoded > 0)
		total += decoded;
	ticks = (uint64_t)(OSGetSystemTime() - started);
	report.decoded++;
	report.decode_ticks += ticks;
	if (ticks > report.decode_max)
		report.decode_max = ticks;
	/* (the tests: what each speaker sounds like, once a second, in voice_audio.c's words) */
	if (__atomic_load_n(&voice_test, __ATOMIC_ACQUIRE) > 0 && total >= (int)VOICE_FRAME_SAMPLES && now - slots[index].logged_ms >= 1000u)
	{
		slots[index].logged_ms = now;
		wiiu_log("voice test: speaker %d level %.3f gain %.2f pan %.2f bytes %d", entry->speaker,
			wiiu_voice_level(samples + total - VOICE_FRAME_SAMPLES), entry->gain, entry->pan, entry->length);
	}
	if (total > 0 && !wiiu_voice_ring_put(&rings[index], samples, (uint32_t)total))
		report.ring_full++;
}

static void play_waiting(uint32_t now)
{
	static struct play_entry entry;
	const uint32_t forgets = __atomic_load_n(&forget_all_requests, __ATOMIC_ACQUIRE);
	int index;

	if (forgets != forget_all_seen)
	{
		forget_all_seen = forgets;
		for (index = 0; index < VOICE_SLOTS; index++)
			slot_forget(index);
	}
	while (wiiu_voice_queue_take(&play_queue, &entry))
	{
		if (entry.kind == PLAY_FORGET)
		{
			for (index = 0; index < VOICE_SLOTS; index++)
				if (slots[index].used && slots[index].speaker == entry.speaker)
					slot_forget(index);
			continue;
		}
		decode_packet(&entry, now);
	}
}

/* everything waiting dropped and every speaker forgotten (HOME, the app closing) */
static void drop_everything(void)
{
	int index;

	wiiu_voice_queue_drop_all(&capture_queue);
	wiiu_voice_queue_drop_all(&encode_queue);
	wiiu_voice_queue_drop_all(&packet_queue);
	wiiu_voice_queue_drop_all(&play_queue);
	for (index = 0; index < VOICE_SLOTS; index++)
		slot_forget(index);
}

/* ---------- the report */

static void report_poll(uint32_t now, uint64_t turn_ticks)
{
	if (turn_ticks > report.turn_max)
		report.turn_max = turn_ticks;
	if (now - report.since_ms < VOICE_REPORT_MS)
		return;
	report.frames_dropped = capture.dropped;
	capture.dropped = 0;
	/* (only a window that did something: a quiet one is not worth a line) */
	if (report.encoded || report.decoded || report.frames_dropped)
	{
		int speakers = 0, index;

		for (index = 0; index < VOICE_SLOTS; index++)
			speakers += slots[index].used;
		wiiu_log("Voice: %u s: %u frames encoded (mean %.2f, max %.2f ms), %u packets decoded (mean %.2f, max %.2f ms), %u made up, "
			"%d speakers; slowest turn %.2f ms", (unsigned)((now - report.since_ms) / 1000u), (unsigned)report.encoded,
			report.encoded ? ticks_ms(report.encode_ticks) / report.encoded : 0.0, ticks_ms(report.encode_max), (unsigned)report.decoded,
			report.decoded ? ticks_ms(report.decode_ticks) / report.decoded : 0.0, ticks_ms(report.decode_max), (unsigned)report.concealed,
			speakers, ticks_ms(report.turn_max));
		wiiu_log("Voice: dropped %u late, %u for want of a slot, %u to full rings, %u queued packets, %u stale packets, %u frames "
			"given, %u microphone frames; the microphone's ring found full %u times, its read index elsewhere %u times",
			(unsigned)report.late, (unsigned)report.no_slot, (unsigned)report.ring_full,
			(unsigned)__atomic_exchange_n(&play_dropped, 0u, __ATOMIC_RELAXED), (unsigned)__atomic_exchange_n(&stale_dropped, 0u, __ATOMIC_RELAXED),
			(unsigned)__atomic_exchange_n(&encode_dropped, 0u, __ATOMIC_RELAXED), (unsigned)report.frames_dropped, (unsigned)reader.full,
			(unsigned)reader.mismatches);
	}
	memset(&report, 0, sizeof(report));
	report.since_ms = now;
}

/* ---------- the voice thread */

/* one turn: the microphone opened or closed as the game wants it and read, the frames encoded, the packets decoded */
static int voice_turn(void)
{
	const uint32_t now = now_ms();
	const OSTime started = OSGetSystemTime();
	const int active = now - __atomic_load_n(&session_ms, __ATOMIC_RELAXED) < VOICE_ACTIVE_MS;
	const int wanted = active && __atomic_load_n(&microphone_wanted, __ATOMIC_ACQUIRE);

	if (__atomic_load_n(&voice_test, __ATOMIC_ACQUIRE) > 0)
	{
		if (wanted)
			tone_poll(now);
		else if (tone_started_ms)
			microphone_close("not wanted");
	}
	else if (wanted && microphone_handle < 0)
	{
		if (!microphone_failed || now - microphone_failed_ms >= VOICE_RETRY_MS)
			microphone_open_now(microphone_failed ? "tried again" : "voice chat wants it");
	}
	else if (!wanted && microphone_handle >= 0)
	{
		microphone_close(active ? "not wanted" : "no voice chat for a second");
	}
	if (microphone_handle >= 0)
		microphone_poll();
	encode_waiting();
	play_waiting(now);
	report_poll(now, (uint64_t)(OSGetSystemTime() - started));
	return active;
}

static void probe_run(void);

static int voice_main(int argc, const char **argv)
{
	(void)argv;
	wiiu_tag_thread("voice");
	wiiu_log("Voice chat: the voice thread runs on core %u at priority %d", (unsigned)OSGetCoreId(),
		(int)OSGetThreadPriority(OSGetCurrentThread()));
	report.since_ms = now_ms();
	if (argc)
		probe_run();
	for (;;)
	{
		const uint32_t wanted = __atomic_load_n(&request, __ATOMIC_ACQUIRE);
		int active;

		if ((wanted & 3u) == REQUEST_STOP)
		{
			microphone_close("the app closes");
			drop_everything();
			__atomic_store_n(&acknowledged, wanted, __ATOMIC_RELEASE);
			break;
		}
		if ((wanted & 3u) == REQUEST_SUSPEND)
		{
			if (__atomic_load_n(&acknowledged, __ATOMIC_RELAXED) != wanted)
			{
				microphone_close("HOME");
				drop_everything();
				/* (open again at once when the foreground is back) */
				microphone_failed = 0;
				__atomic_store_n(&acknowledged, wanted, __ATOMIC_RELEASE);
			}
			/* (asleep until the foreground is back, without spinning: the system holds the app's threads in the background
			anyway) */
			OSSleepTicks(OSMillisecondsToTicks(20));
			continue;
		}
		active = voice_turn();
		OSSleepTicks(OSMillisecondsToTicks(active ? VOICE_POLL_MS : VOICE_IDLE_POLL_MS));
	}
	wiiu_log("Voice chat: the voice thread ends");
	wiiu_untag_thread();
	return 0;
}

static void voice_start(int probe)
{
	uint32_t expected = VOICE_NOT_STARTED;
	int priority = VOICE_PRIORITY, core = 0;
	OSThreadAttributes affinity = OS_THREAD_ATTRIB_AFFINITY_CPU0;
	FILE *file;

	if (!__atomic_compare_exchange_n(&voice_state, &expected, VOICE_STARTING, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return;
	wiiu_voice_queue_init(&capture_queue, capture_records, CAPTURE_RECORDS, sizeof(capture_records[0]));
	wiiu_voice_queue_init(&encode_queue, encode_records, ENCODE_RECORDS, sizeof(encode_records[0]));
	wiiu_voice_queue_init(&packet_queue, packet_records, PACKET_RECORDS, sizeof(packet_records[0]));
	wiiu_voice_queue_init(&play_queue, play_records, PLAY_RECORDS, sizeof(play_records[0]));
	wiiu_voice_resampler_coefficients();
	wiiu_voice_capture_init(&capture, &capture_queue, VOICE_BACKLOG_FRAMES);
	OSInitMutex(&microphone_lock);
	/* (halo/voice_core.txt 2: the thread beside the GX2 thread, which leaves its core mostly idle, at its priority: that thread waits for
	work yielding, which gives the core only to threads of its priority or above) */
	file = fopen(CORE_FILE, "r");
	if (file)
	{
		if (fscanf(file, "%d", &core) != 1)
			core = 0;
		fclose(file);
	}
	if (core == 2)
	{
		affinity = OS_THREAD_ATTRIB_AFFINITY_CPU2;
		priority = wiiu_gx2_thread_priority();
	}
	if (!OSCreateThread(&voice_thread, voice_main, probe ? 1 : 0, NULL, voice_stack + VOICE_STACK_BYTES, VOICE_STACK_BYTES, priority,
			affinity))
	{
		wiiu_log("Voice chat: no voice thread: voices are neither sent nor heard");
		__atomic_store_n(&voice_state, VOICE_DOWN, __ATOMIC_RELEASE);
		return;
	}
	OSSetThreadName(&voice_thread, "voice");
	__atomic_store_n(&voice_state, VOICE_RUNNING, __ATOMIC_RELEASE);
	OSResumeThread(&voice_thread);
}

/* the game asks for voice chat: noted, and the thread started the first time */
static void voice_touch(void)
{
	__atomic_store_n(&session_ms, now_ms(), __ATOMIC_RELAXED);
	if (__atomic_load_n(&voice_test, __ATOMIC_RELAXED) < 0)
		__atomic_store_n(&voice_test, config_boolean("debug.voice_test") ? 1 : 0, __ATOMIC_RELEASE);
	if (__atomic_load_n(&voice_state, __ATOMIC_ACQUIRE) == VOICE_NOT_STARTED)
		voice_start(0);
}

/* ---------- voice_audio.h, for network_voice.c (the game thread) and dsound_sdl.c (the mixer) */

int voice_audio_microphone(int open)
{
	voice_touch();
	__atomic_store_n(&microphone_wanted, open ? 1u : 0u, __ATOMIC_RELEASE);
	/* (the tone needs no microphone: voice_audio.c's answer) */
	if (voice_test > 0)
		return open;
	return open && __atomic_load_n(&microphone_open, __ATOMIC_ACQUIRE) != 0;
}

int voice_audio_read_frame(float *frame)
{
	voice_touch();
	return running() && wiiu_voice_queue_take(&capture_queue, frame);
}

float voice_audio_level(const float *frame)
{
	return wiiu_voice_level(frame);
}

int voice_audio_encode(const float *frame, int bitrate, unsigned char *packet, int maximum)
{
	static struct encode_job job;
	const uint32_t now = now_ms();
	uint32_t dropped = 0, length;

	voice_touch();
	if (!running())
		return 0;
	job.queued_ms = now;
	job.bitrate = bitrate;
	job.maximum = maximum;
	memcpy(job.frame, frame, sizeof(job.frame));
	__atomic_add_fetch(&encode_dropped, wiiu_voice_queue_put_newest(&encode_queue, &job, ENCODE_KEEP), __ATOMIC_RELAXED);
	/* (the packet of a frame given before: this one's is ready by the next call) */
	length = wiiu_voice_packet_take(&packet_queue, now, VOICE_STALE_MS, maximum > 0 ? (uint32_t)maximum : 0u, packet, &dropped);
	if (dropped)
		__atomic_add_fetch(&stale_dropped, dropped, __ATOMIC_RELAXED);
	return (int)length;
}

void voice_audio_play(int speaker, unsigned short sequence, const unsigned char *packet, int length, float gain, float pan)
{
	static struct play_entry entry;

	if (length <= 0 || length > (int)VOICE_MAXIMUM_PACKET)
		return;
	/* (every build sends a frame of 20 ms: anything else, damaged, is dropped before the decoder sees it, and made up for as
	lost) */
	if (opus_packet_get_nb_samples(packet, length, VOICE_RATE) != (int)VOICE_FRAME_SAMPLES)
		return;
	voice_touch();
	if (!running())
		return;
	entry.kind = PLAY_PACKET;
	entry.length = (uint8_t)length;
	entry.sequence = sequence;
	entry.speaker = speaker;
	entry.gain = gain;
	entry.pan = pan;
	memcpy(entry.bytes, packet, (size_t)length);
	__atomic_add_fetch(&play_dropped, wiiu_voice_queue_put_newest(&play_queue, &entry, PLAY_KEEP), __ATOMIC_RELAXED);
}

void voice_audio_forget(int speaker)
{
	static struct play_entry entry;

	if (!running())
		return;
	entry.kind = PLAY_FORGET;
	entry.speaker = speaker;
	entry.length = 0;
	wiiu_voice_queue_put_newest(&play_queue, &entry, PLAY_KEEP);
}

void voice_audio_forget_all(void)
{
	if (!running())
		return;
	/* (what is queued is of the speakers forgotten; the voice thread forgets the rest at its next turn) */
	wiiu_voice_queue_drop_all(&play_queue);
	__atomic_add_fetch(&forget_all_requests, 1u, __ATOMIC_RELEASE);
}

int voice_audio_speaking(int speaker)
{
	int index;

	for (index = 0; index < VOICE_SLOTS; index++)
	{
		if (wiiu_voice_ring_in_use(&rings[index]) && __atomic_load_n(&ring_speakers[index], __ATOMIC_RELAXED) == speaker)
			return wiiu_voice_ring_playing(&rings[index]) || wiiu_voice_ring_buffered(&rings[index]) >= VOICE_FRAME_SAMPLES;
	}
	return 0;
}

void voice_audio_set_volume(float volume)
{
	volume = volume < 0.0f ? 0.0f : volume > 2.0f ? 2.0f : volume;
	__atomic_store(&voice_volume, &volume, __ATOMIC_RELAXED);
}

int voice_audio_hears(void)
{
	voice_touch();
	/* (the voices are heard only while the voice thread decodes them and the mixer runs: not in the background, nor without
	sound) */
	return running() && (__atomic_load_n(&request, __ATOMIC_ACQUIRE) & 3u) == REQUEST_RUN &&
		now_ms() - __atomic_load_n(&mixed_ms, __ATOMIC_RELAXED) < 500u;
}

void voice_audio_mix(float *output, unsigned long frames)
{
	float volume;
	int index;

	__atomic_store_n(&mixed_ms, now_ms(), __ATOMIC_RELAXED);
	if (!running())
		return;
	__atomic_load(&voice_volume, &volume, __ATOMIC_RELAXED);
	for (index = 0; index < VOICE_SLOTS; index++)
		wiiu_voice_ring_mix(&rings[index], output, (uint32_t)frames, volume, VOICE_PREBUFFER_SAMPLES);
}

/* ---------- the shell's (wiiu_shell.c) */

static uint32_t request_next(uint32_t kind)
{
	/* (only the main thread asks: its ProcUI callbacks and its close) */
	const uint32_t wanted = (((__atomic_load_n(&request, __ATOMIC_RELAXED) >> 2) + 1u) << 2) | kind;

	__atomic_store_n(&request, wanted, __ATOMIC_RELEASE);
	return wanted;
}

void wiiu_voice_boot(void);
void wiiu_voice_suspend(void);
void wiiu_voice_resume(void);
int wiiu_voice_stop(uint32_t timeout_ms);

/* at the start: the microphone probe, if halo/mic_probe.txt says 1 (the thread is otherwise started by the first network
game's voice chat) */
void wiiu_voice_boot(void)
{
	FILE *file = fopen(PROBE_FILE, "r");
	int probe = 0;

	if (file)
	{
		if (fscanf(file, "%d", &probe) != 1)
			probe = 0;
		fclose(file);
	}
	if (probe == 1)
		voice_start(1);
}

/* HOME: the microphone closed and everything waiting dropped by the voice thread, and the thread asleep, before the sound is
given up; waited for 100 ms at the most. A thread that does not answer (one the system is not running) has the microphone
closed for it here, if it is not in the middle of a microphone call. */
void wiiu_voice_suspend(void)
{
	uint32_t wanted, waited = 0;

	if (!running())
		return;
	wanted = request_next(REQUEST_SUSPEND);
	while (__atomic_load_n(&acknowledged, __ATOMIC_ACQUIRE) != wanted && waited < VOICE_SUSPEND_WAIT_MS)
	{
		OSSleepTicks(OSMillisecondsToTicks(2));
		waited += 2;
	}
	if (__atomic_load_n(&acknowledged, __ATOMIC_ACQUIRE) == wanted)
	{
		wiiu_log("Voice chat: given up for the background after %u ms", (unsigned)waited);
		return;
	}
	if (OSTryLockMutex(&microphone_lock))
	{
		const int handle = microphone_handle;
		MICError closed = MIC_ERROR_OK, uninit = MIC_ERROR_OK;

		microphone_handle = -1;
		if (handle >= 0)
		{
			closed = MICClose(handle);
			uninit = MICUninit(handle);
		}
		OSUnlockMutex(&microphone_lock);
		wiiu_log("Voice chat: the voice thread did not answer in %u ms; %s (MICClose %d, MICUninit %d)", (unsigned)waited,
			handle >= 0 ? "the microphone closed here" : "the microphone was not open", (int)closed, (int)uninit);
	}
	else
	{
		wiiu_log("Voice chat: the voice thread did not answer in %u ms, and is in a microphone call: the microphone is left to the system",
			(unsigned)waited);
	}
}

/* back in the foreground: the thread wakes, and opens the microphone again if it is wanted (every 10 s while it fails) */
void wiiu_voice_resume(void)
{
	if (!running())
		return;
	request_next(REQUEST_RUN);
}

/* the app closes: the microphone closed and the thread ended and joined. 1 if it ended in time, 0 if not (it is left as it is), 2 if
there was no thread */
int wiiu_voice_stop(uint32_t timeout_ms)
{
	uint32_t waited = 0;

	if (!running())
		return 2;
	request_next(REQUEST_STOP);
	while (!OSIsThreadTerminated(&voice_thread) && waited < timeout_ms)
	{
		OSSleepTicks(OSMillisecondsToTicks(5));
		waited += 5;
	}
	if (!OSIsThreadTerminated(&voice_thread))
		return 0;
	OSJoinThread(&voice_thread, NULL);
	__atomic_store_n(&voice_state, VOICE_DOWN, __ATOMIC_RELEASE);
	return 1;
}

/* ---------- the probe (halo/mic_probe.txt 1; port/wiiu/README.md says how to read what it logs) */

/* What the microphone does on this console, logged as "mic probe:" lines, from the voice thread: the states mic.rpl reports,
bufferPos and availableData as the system moves them with nothing consumed and then with each reading consumed, which of the
two readings that is (written to mic_probe_result.txt, which audio.wiiu_microphone_read "auto" follows), ten seconds recorded
by it to mic_probe.wav, and what HOME during the recording does to the microphone. It starts 10 s after the app, for the
menus to be up and the GamePad read; the TV button is taken for its length, and each press logged by the input layer. */
#define PROBE_DELAY_MS 10000u
#define PROBE_POLLS 100
#define PROBE_LOGGED 20
#define PROBE_SECONDS 10u

static struct
{
	int32_t available[PROBE_POLLS], position[PROBE_POLLS];
} probe;

static MICError probe_status(MICStatus *status)
{
	MICError error = MIC_ERROR_INVALID_HANDLE;

	memset(status, 0, sizeof(*status));
	OSLockMutex(&microphone_lock);
	if (microphone_handle >= 0)
		error = MICGetStatus(microphone_handle, status);
	OSUnlockMutex(&microphone_lock);
	return error;
}

/* the microphone opened as voice chat opens it (microphone_open_locked); 1 if it opened, 0 if it failed, -1 if HOME or the close
came meanwhile (it is closed again then) */
static int probe_open(const char *why)
{
	MICError error;
	const char *failed;
	const OSTime started = OSGetSystemTime();
	const int handle = microphone_open_locked(&error, &failed);
	const double took = ticks_ms((uint64_t)(OSGetSystemTime() - started));

	if (handle >= 0)
	{
		wiiu_log("mic probe: %s: MICInit (instance 0, a ring of %u samples at %08x) and MICOpen: handle %d (%.2f ms)", why,
			(unsigned)MIC_RING_SAMPLES, (unsigned)(uintptr_t)microphone_ring, handle, took);
		return 1;
	}
	if (failed)
	{
		wiiu_log("mic probe: %s: %s failed: error %d (%.2f ms)", why, failed, (int)error, took);
		return 0;
	}
	wiiu_log("mic probe: %s: HOME or the close came while it opened: closed again (%.2f ms)", why, took);
	return -1;
}

static void probe_close(const char *why)
{
	const OSTime started = OSGetSystemTime();
	MICError closed = MIC_ERROR_OK, uninit = MIC_ERROR_OK;
	int handle;

	OSLockMutex(&microphone_lock);
	handle = microphone_handle;
	microphone_handle = -1;
	if (handle >= 0)
	{
		closed = MICClose(handle);
		uninit = MICUninit(handle);
	}
	OSUnlockMutex(&microphone_lock);
	if (handle >= 0)
		wiiu_log("mic probe: closed (%s): MICClose %d, MICUninit %d (%.2f ms)", why, (int)closed, (int)uninit,
			ticks_ms((uint64_t)(OSGetSystemTime() - started)));
}

/* a sample of the ring by its index (as the system's bufferPos would be in samples) */
static int probe_sample(uint32_t index)
{
	const unsigned char *at = microphone_ring + 2u * (index % MIC_RING_SAMPLES);
	int value = (at[0] << 8) | at[1];

	return value >= 32768 ? value - 65536 : value;
}

/* HOME or the close during the probe: what the microphone said just before it closed, how the close went, how it opened again
when the foreground was back (if it was open). 0: go on as before; 1: opened again (a gap); -1: the app closes, or the microphone
would not open again */
static int probe_requests(void)
{
	uint32_t wanted = __atomic_load_n(&request, __ATOMIC_ACQUIRE);
	const int was_open = microphone_handle >= 0;
	MICStatus status;
	MICError error;
	uint32_t asleep_ms;

	if ((wanted & 3u) == REQUEST_RUN)
		return 0;
	if (was_open)
	{
		error = probe_status(&status);
		wiiu_log("mic probe: %s during the probe: the last status (error %d): state %d, availableData %d, bufferPos %d",
			(wanted & 3u) == REQUEST_STOP ? "the close" : "HOME", (int)error, status.state, status.availableData, status.bufferPos);
		probe_close((wanted & 3u) == REQUEST_STOP ? "the app closes" : "HOME");
	}
	else
	{
		wiiu_log("mic probe: %s with the microphone closed", (wanted & 3u) == REQUEST_STOP ? "the close" : "HOME");
	}
	__atomic_store_n(&acknowledged, wanted, __ATOMIC_RELEASE);
	asleep_ms = now_ms();
	while ((__atomic_load_n(&request, __ATOMIC_ACQUIRE) & 3u) == REQUEST_SUSPEND)
		OSSleepTicks(OSMillisecondsToTicks(20));
	wanted = __atomic_load_n(&request, __ATOMIC_ACQUIRE);
	if ((wanted & 3u) == REQUEST_STOP)
	{
		/* (the voice thread's loop answers the stop) */
		wiiu_log("mic probe: given up: the app closes");
		return -1;
	}
	wiiu_log("mic probe: back in the foreground after %u ms", (unsigned)(now_ms() - asleep_ms));
	if (!was_open)
		return 0;
	if (probe_open("again after HOME") <= 0)
		return -1;
	error = probe_status(&status);
	wiiu_log("mic probe: the first status after HOME (error %d): state %d, availableData %d, bufferPos %d", (int)error, status.state,
		status.availableData, status.bufferPos);
	return 1;
}

/* the probe's wait before it starts, HOME and the close answered meanwhile; -1 if the app closes */
static int probe_wait(uint32_t milliseconds)
{
	const uint32_t started = now_ms();

	while (now_ms() - started < milliseconds)
	{
		if (probe_requests() < 0)
			return -1;
		OSSleepTicks(OSMillisecondsToTicks(20));
	}
	return 0;
}

static void probe_sleep(void)
{
	OSSleepTicks(OSMillisecondsToTicks(VOICE_POLL_MS));
}

/* the number of polls that saw bufferPos move, of those from first to last */
static int probe_moves(const int32_t *positions, int first, int last)
{
	int index, moves = 0;

	for (index = first + 1; index <= last; index++)
		moves += positions[index] != positions[index - 1];
	return moves;
}

static void probe_wav_write(const unsigned char *samples, uint32_t count)
{
	unsigned char header[44];
	const uint32_t bytes = count * 2u, rate = 32000u;
	const OSTime started = OSGetSystemTime();
	uint32_t written, piece = 0;
	FILE *file;
	int index;
	/* (RIFF's little-endian fields) */
	const uint32_t fields[] = { 36u + bytes, 16u, 0x00010001u /* PCM, mono */, rate, rate * 2u, 0x00100002u /* 2 bytes a frame, 16 bits */,
		bytes };

	memcpy(header, "RIFF", 4);
	memcpy(header + 8, "WAVEfmt ", 8);
	memcpy(header + 36, "data", 4);
	for (index = 0; index < 4; index++)
	{
		header[4 + index] = (unsigned char)(fields[0] >> (8 * index));
		header[16 + index] = (unsigned char)(fields[1] >> (8 * index));
		header[20 + index] = (unsigned char)(fields[2] >> (8 * index));
		header[24 + index] = (unsigned char)(fields[3] >> (8 * index));
		header[28 + index] = (unsigned char)(fields[4] >> (8 * index));
		header[32 + index] = (unsigned char)(fields[5] >> (8 * index));
		header[40 + index] = (unsigned char)(fields[6] >> (8 * index));
	}
	file = fopen(PROBE_WAV_FILE, "wb");
	if (!file || fwrite(header, 1, sizeof(header), file) != sizeof(header))
	{
		wiiu_log("mic probe: %s could not be written", PROBE_WAV_FILE);
		if (file)
			fclose(file);
		return;
	}
	/* (32 KB at a time, HOME and the close answered between: the card is not written in the background, nor once the close has
	moved on to taking it away) */
	for (written = 0; written < bytes; written += piece)
	{
		piece = bytes - written < 32768u ? bytes - written : 32768u;
		if (probe_requests() < 0)
		{
			wiiu_log("mic probe: %s left at %u of %u bytes: the app closes", PROBE_WAV_FILE, (unsigned)written, (unsigned)bytes);
			fclose(file);
			return;
		}
		if (fwrite(samples + written, 1, piece, file) != piece)
		{
			wiiu_log("mic probe: %s could not be written", PROBE_WAV_FILE);
			fclose(file);
			return;
		}
	}
	fclose(file);
	wiiu_log("mic probe: %u samples (%.2f s at 32 kHz) written to %s in %.0f ms", (unsigned)count, count / 32000.0, PROBE_WAV_FILE,
		ticks_ms((uint64_t)(OSGetSystemTime() - started)));
}

static void probe_run(void)
{
	static const char *const state_names[9] = { "rate", "gain", "gain range", "gain range", "gain range", "mute", "?", "echo cancellation",
		"auto selection" };
	const uint32_t ring = MIC_RING_SAMPLES;
	uint32_t index, moves, early_moves, moved_as_consumed = 0, polls_consumed = 0;
	int32_t position_highest = 0, consumed_highest = 0;
	MICStatus status;
	MICError error;
	int mode, gap;
	uint32_t unit_bytes;
	const char *verdict;
	unsigned char *recording;
	uint32_t recorded = 0, second_count = 0, second_started, peak = 0, recording_started;
	double second_sum = 0.0;
	FILE *file;

	wiiu_log("mic probe: starts in %u s (halo/mic_probe.txt): speak into the GamePad, or its headset, during the %u s recording; press TV to "
		"see it logged; HOME during the recording tests the background", (unsigned)(PROBE_DELAY_MS / 1000u), (unsigned)PROBE_SECONDS);
	wiiu_input_take_tv_button(1);
	if (probe_wait(PROBE_DELAY_MS) < 0)
	{
		wiiu_input_take_tv_button(0);
		return;
	}
	wiiu_log("mic probe: the voice thread on core %u at priority %d; the GamePad's jack: %s (VPADStatus.usingHeadphones as the game "
		"thread last read it), microphone status 0x%02x", (unsigned)OSGetCoreId(), (int)OSGetThreadPriority(OSGetCurrentThread()),
		wiiu_input_headset() > 0 ? "a headset" : wiiu_input_headset() == 0 ? "empty" : "not read yet", wiiu_input_mic_status());
	for (;;)
	{
		const int opened = probe_open("first");

		if (opened > 0)
			break;
		if (!opened)
		{
			wiiu_log("mic probe: the microphone does not open: nothing more to learn");
			wiiu_input_take_tv_button(0);
			return;
		}
		if (probe_requests() < 0)
		{
			wiiu_input_take_tv_button(0);
			return;
		}
	}
	/* the states (Cemu's names, partly guessed) */
	for (index = 0; index < 9; index++)
	{
		uint32_t value = 0;

		OSLockMutex(&microphone_lock);
		error = MICGetState(microphone_handle, (int)index, &value);
		OSUnlockMutex(&microphone_lock);
		wiiu_log("mic probe: state %u (%s): error %d, value %u (0x%08x)", (unsigned)index, state_names[index], (int)error, (unsigned)value,
			(unsigned)value);
	}
	/* a second with nothing consumed: does bufferPos move by itself (a write position), or does availableData grow (a read index)? */
	for (index = 0; index < PROBE_POLLS; index++)
	{
		if (probe_requests() < 0)
		{
			probe_close("given up");
			wiiu_input_take_tv_button(0);
			return;
		}
		error = probe_status(&status);
		probe.available[index] = status.availableData;
		probe.position[index] = status.bufferPos;
		if (status.bufferPos > position_highest)
			position_highest = status.bufferPos;
		if (index < PROBE_LOGGED || index == PROBE_POLLS - 1)
		{
			DCFlushRange(microphone_ring, sizeof(microphone_ring));
			wiiu_log("mic probe: unconsumed %u: error %d, state %d, availableData %d, bufferPos %d; samples at bufferPos %d %d %d %d, at "
				"bufferPos/2 %d %d %d %d", (unsigned)index, (int)error, status.state, status.availableData, status.bufferPos,
				probe_sample((uint32_t)status.bufferPos), probe_sample((uint32_t)status.bufferPos + 1), probe_sample((uint32_t)status.bufferPos + 2),
				probe_sample((uint32_t)status.bufferPos + 3), probe_sample((uint32_t)status.bufferPos / 2), probe_sample((uint32_t)status.bufferPos / 2 + 1),
				probe_sample((uint32_t)status.bufferPos / 2 + 2), probe_sample((uint32_t)status.bufferPos / 2 + 3));
		}
		probe_sleep();
	}
	moves = (uint32_t)probe_moves(probe.position, 0, PROBE_POLLS - 1);
	early_moves = (uint32_t)probe_moves(probe.position, 0, 24);
	wiiu_log("mic probe: with nothing consumed, bufferPos moved in %u of %u polls (%u of the first 24, before a ring's 320 ms), went up to %d; "
		"availableData went from %d to %d (%d after 250 ms)", (unsigned)moves, (unsigned)(PROBE_POLLS - 1), (unsigned)early_moves,
		(int)position_highest, (int)probe.available[0], (int)probe.available[PROBE_POLLS - 1], (int)probe.available[24]);
	/* a second consuming each reading at once (as Cemu and the examples do), with the status read again after each */
	for (index = 0; index < PROBE_POLLS; index++)
	{
		MICStatus after;
		MICError consumed_error = MIC_ERROR_OK;
		uint32_t waiting;

		if (probe_requests() < 0)
		{
			probe_close("given up");
			wiiu_input_take_tv_button(0);
			return;
		}
		probe_status(&status);
		waiting = status.availableData <= 0 ? 0u : (uint32_t)status.availableData;
		if (waiting > ring)
			waiting = ring;
		OSLockMutex(&microphone_lock);
		if (microphone_handle >= 0 && waiting)
			consumed_error = MICSetDataConsumed(microphone_handle, (int)waiting);
		OSUnlockMutex(&microphone_lock);
		probe_status(&after);
		if (status.bufferPos > consumed_highest)
			consumed_highest = status.bufferPos;
		if (after.bufferPos > consumed_highest)
			consumed_highest = after.bufferPos;
		/* (a read index moves on by what was consumed, in its own unit; round the ring, in either: twice the samples is a
		whole number of rings of samples) */
		if (waiting)
		{
			polls_consumed++;
			moved_as_consumed += ((uint32_t)after.bufferPos % ring + ring - (uint32_t)status.bufferPos % ring) % ring == waiting % ring;
		}
		if (index < PROBE_LOGGED || index == PROBE_POLLS - 1)
			wiiu_log("mic probe: consumed %u: before availableData %d, bufferPos %d; MICSetDataConsumed(%u) %d; after availableData %d, "
				"bufferPos %d", (unsigned)index, status.availableData, status.bufferPos, (unsigned)waiting, (int)consumed_error, after.availableData,
				after.bufferPos);
		probe_sleep();
	}
	wiiu_log("mic probe: consuming, bufferPos moved on by what was consumed after %u of %u readings, and went up to %d",
		(unsigned)moved_as_consumed, (unsigned)polls_consumed, (int)consumed_highest);
	verdict = wiiu_mic_probe_finding(probe.position, probe.available, PROBE_POLLS, ring, consumed_highest, &mode, &unit_bytes);
	wiiu_log("mic probe: the reading is \"%s\" (%s)", verdict, !strcmp(verdict, "unknown") ? "nothing moved: no sound came, the "
		"recording reads by consuming" : "written to mic_probe_result.txt, which audio.wiiu_microphone_read \"auto\" follows");
	file = asked_to_stop() ? NULL : fopen(PROBE_RESULT_FILE, "w");
	if (file)
	{
		fprintf(file, "%s\n", verdict);
		fprintf(file, "# the microphone probe's finding (wiiu_voice_audio.c), %u-sample ring\n", (unsigned)ring);
		fprintf(file, "# unconsumed: bufferPos moved in %u of %u polls (%u of the first 24), up to %d; availableData %d, %d after 250 ms, %d after 1 s\n",
			(unsigned)moves, (unsigned)(PROBE_POLLS - 1), (unsigned)early_moves, (int)position_highest, (int)probe.available[0],
			(int)probe.available[24], (int)probe.available[PROBE_POLLS - 1]);
		fprintf(file, "# consuming: bufferPos moved on by what was consumed %u of %u times, up to %d\n", (unsigned)moved_as_consumed,
			(unsigned)polls_consumed, (int)consumed_highest);
		fclose(file);
	}
	/* ten seconds as the finding reads them, raw (32 kHz, big-endian from the system, little-endian in the file), with each second's level
	and how many samples it brought (32000 if the rate is 32 kHz and the reading is right) */
	recording = malloc(PROBE_SECONDS * 32000u * 2u);
	if (!recording)
	{
		wiiu_log("mic probe: no memory for the recording");
		probe_close("done");
		wiiu_input_take_tv_button(0);
		return;
	}
	wiiu_mic_reader_init(&reader, mode, unit_bytes, ring);
	wiiu_log("mic probe: recording %u s by %s %s", (unsigned)PROBE_SECONDS, mode == WIIU_MIC_POSITION ? "position" : "consume",
		unit_bytes == 1 ? "bytes" : "samples");
	recording_started = second_started = now_ms();
	while (recorded < PROBE_SECONDS * 32000u)
	{
		uint32_t first, count, consume;

		gap = probe_requests();
		if (gap < 0)
		{
			probe_close("given up");
			wiiu_input_take_tv_button(0);
			free(recording);
			return;
		}
		if (gap > 0)
		{
			wiiu_mic_reader_init(&reader, mode, unit_bytes, ring);
			wiiu_log("mic probe: the recording goes on after HOME at %.2f s", recorded / 32000.0);
		}
		if (probe_status(&status) == MIC_ERROR_OK)
		{
			wiiu_mic_reader_span(&reader, status.availableData, status.bufferPos, &first, &count, &consume);
			DCFlushRange(microphone_ring, sizeof(microphone_ring));
			for (index = 0; index < count && recorded < PROBE_SECONDS * 32000u; index++)
			{
				const int value = probe_sample(first + index);
				const uint32_t magnitude = (uint32_t)(value < 0 ? -value : value);

				recording[2u * recorded] = (unsigned char)(value & 0xff);
				recording[2u * recorded + 1u] = (unsigned char)((value >> 8) & 0xff);
				recorded++;
				second_count++;
				second_sum += (double)value * value;
				if (magnitude > peak)
					peak = magnitude;
			}
			if (consume)
			{
				OSLockMutex(&microphone_lock);
				if (microphone_handle >= 0)
					MICSetDataConsumed(microphone_handle, (int)consume);
				OSUnlockMutex(&microphone_lock);
			}
		}
		if (now_ms() - second_started >= 1000u)
		{
			wiiu_log("mic probe: %u samples in %u ms, level %.4f of full scale, peak %u; %u ring full, %u read indexes elsewhere",
				(unsigned)second_count, (unsigned)(now_ms() - second_started), second_count ? sqrt(second_sum / second_count) / 32768.0 : 0.0,
				(unsigned)peak, (unsigned)reader.full, (unsigned)reader.mismatches);
			second_started = now_ms();
			second_count = 0;
			second_sum = 0.0;
			peak = 0;
		}
		/* (a recording that brings nothing for twice its length stops) */
		if (now_ms() - recording_started > 2u * PROBE_SECONDS * 1000u)
		{
			wiiu_log("mic probe: the recording brought %u samples in %u s: stopped", (unsigned)recorded, (unsigned)(2u * PROBE_SECONDS));
			break;
		}
		probe_sleep();
	}
	probe_close("done");
	wiiu_input_take_tv_button(0);
	probe_wav_write(recording, recorded);
	free(recording);
	wiiu_log("mic probe: done");
}
