/*
TEST_VOICE_AUDIO.C

port/wiiu/src/wiiu_voice_audio.c as the console would run it, over stand-ins for Cafe OS (voice_mock/): threads on
pthreads, and a clock that moves only when every thread sleeps, so that the voice thread's 10 ms turns come out the same
however slow the machine (qemu-ppc); mic.rpl as Cemu has it (a read index and a count, consumed) or as WiiUCamera reads it
(a write position in bytes), playing a 440 Hz tone; and the real Opus. The cases: the microphone's frames encoded, sent and
played back through the mix; the packet of the frame before, and none stale; HOME (the voice thread closing the microphone,
and the release callback closing it for a thread that does not answer); the close; a microphone that does not open; the
ring read by its write position, as the setting or the probe's finding says; a speaker forgotten; debug.voice_test; what the
host is told; and the probe, HOME during its recording.

tools/harness/tests/test_wiiu_voice_audio.py builds it (VOICE_AUDIO_SOURCE: wiiu_voice_audio.c, or a copy with a fault)
and runs each case on its own (HALO_TEST_DIRECTORY: where the card's files go).
*/

#include "voice_mock.h"

/* (the port's own declarations of what this file stands in for) */
#include "wiiu_input.h"
#include "wiiu_shell.h"

#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition, ...) \
	do \
	{ \
		if (!(condition)) \
		{ \
			fprintf(stderr, "%s:%d: check failed: %s: ", __FILE__, __LINE__, #condition); \
			fprintf(stderr, __VA_ARGS__); \
			fputc('\n', stderr); \
			exit(1); \
		} \
	} while (0)

/* ---------- the clock: it moves on only when every thread sleeps, to the nearest wake */

static pthread_mutex_t clock_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t clock_moved = PTHREAD_COND_INITIALIZER;
/* (a second in: a time of 0 is the voice code's "never") */
static OSTime clock_now = 1000000;
static int threads_alive = 1, threads_asleep;
#define SLEEPERS 8
static struct
{
	int used;
	pthread_t thread;
	OSTime until;
} sleepers[SLEEPERS];
/* a thread held asleep however late it gets (the voice thread not answering HOME) */
static int held;
static pthread_t held_thread;

static int is_held(pthread_t thread)
{
	return held && pthread_equal(thread, held_thread);
}

OSTime OSGetSystemTime(void)
{
	OSTime now;

	pthread_mutex_lock(&clock_lock);
	now = clock_now;
	pthread_mutex_unlock(&clock_lock);
	return now;
}

void OSSleepTicks(OSTime ticks)
{
	int slot;

	pthread_mutex_lock(&clock_lock);
	for (slot = 0; slot < SLEEPERS && sleepers[slot].used; slot++)
		;
	CHECK(slot < SLEEPERS, "too many sleepers");
	sleepers[slot].used = 1;
	sleepers[slot].thread = pthread_self();
	sleepers[slot].until = clock_now + (ticks > 0 ? ticks : 0);
	threads_asleep++;
	for (;;)
	{
		if (clock_now >= sleepers[slot].until && !is_held(pthread_self()))
			break;
		if (threads_asleep == threads_alive)
		{
			OSTime next = INT64_MAX;
			int index;

			for (index = 0; index < SLEEPERS; index++)
				if (sleepers[index].used && !is_held(sleepers[index].thread) && sleepers[index].until < next)
					next = sleepers[index].until;
			CHECK(next != INT64_MAX, "every thread is held asleep");
			if (next > clock_now)
			{
				clock_now = next;
				pthread_cond_broadcast(&clock_moved);
				continue;
			}
		}
		pthread_cond_wait(&clock_moved, &clock_lock);
	}
	sleepers[slot].used = 0;
	threads_asleep--;
	pthread_mutex_unlock(&clock_lock);
}

static void hold(pthread_t thread, int on)
{
	pthread_mutex_lock(&clock_lock);
	held = on;
	held_thread = thread;
	pthread_cond_broadcast(&clock_moved);
	pthread_mutex_unlock(&clock_lock);
}

/* the test's own time going by (the game's frames between) */
static void advance(uint32_t milliseconds)
{
	OSSleepTicks(OSMillisecondsToTicks(milliseconds));
}

/* ---------- threads */

static void *thread_start(void *parameter)
{
	OSThread *thread = parameter;
	int result = thread->entry(thread->argc, NULL);

	(void)result;
	pthread_mutex_lock(&clock_lock);
	thread->terminated = 1;
	threads_alive--;
	pthread_cond_broadcast(&clock_moved);
	pthread_mutex_unlock(&clock_lock);
	return NULL;
}

static int fail_thread;

BOOL OSCreateThread(OSThread *thread, OSThreadEntryPointFn entry, int32_t argc, char *argv, void *stack, uint32_t stack_size,
	int32_t priority, OSThreadAttributes attributes)
{
	(void)argv;
	(void)stack;
	(void)stack_size;
	if (fail_thread)
		return FALSE;
	memset(thread, 0, sizeof(*thread));
	thread->entry = entry;
	thread->argc = argc;
	thread->priority = priority;
	thread->attributes = attributes;
	thread->created = 1;
	return TRUE;
}

int32_t OSResumeThread(OSThread *thread)
{
	pthread_mutex_lock(&clock_lock);
	threads_alive++;
	pthread_mutex_unlock(&clock_lock);
	thread->resumed = 1;
	CHECK(!pthread_create(&thread->host, NULL, thread_start, thread), "no thread");
	return 1;
}

void OSSetThreadName(OSThread *thread, const char *name)
{
	thread->name = name;
}

static OSThread current_thread;

OSThread *OSGetCurrentThread(void)
{
	return &current_thread;
}

int32_t OSGetThreadPriority(OSThread *thread)
{
	(void)thread;
	return 20;
}

BOOL OSIsThreadTerminated(OSThread *thread)
{
	int terminated;

	pthread_mutex_lock(&clock_lock);
	terminated = thread->terminated;
	pthread_mutex_unlock(&clock_lock);
	return terminated;
}

BOOL OSJoinThread(OSThread *thread, int *result)
{
	pthread_join(thread->host, NULL);
	thread->joined = 1;
	if (result)
		*result = 0;
	return TRUE;
}

uint32_t OSGetCoreId(void)
{
	return 0;
}

void OSInitMutex(OSMutex *mutex)
{
	pthread_mutex_init(&mutex->lock, NULL);
}

void OSLockMutex(OSMutex *mutex)
{
	pthread_mutex_lock(&mutex->lock);
}

void OSUnlockMutex(OSMutex *mutex)
{
	pthread_mutex_unlock(&mutex->lock);
}

BOOL OSTryLockMutex(OSMutex *mutex)
{
	return pthread_mutex_trylock(&mutex->lock) == 0;
}

static int flushes;

void DCFlushRange(void *address, uint32_t size)
{
	(void)address;
	(void)size;
	__atomic_add_fetch(&flushes, 1, __ATOMIC_RELAXED);
}

void DCInvalidateRange(void *address, uint32_t size)
{
	(void)address;
	(void)size;
}

/* ---------- mic.rpl: a 440 Hz tone at a quarter of full scale, 32 kHz, big-endian, into the app's ring */

enum
{
	MODEL_CONSUME,
	MODEL_POSITION,
};

#define MIC_HANDLE 3

static pthread_mutex_t mic_lock = PTHREAD_MUTEX_INITIALIZER;
static struct
{
	int model;
	int initialized, open;
	unsigned char *ring;
	uint32_t ring_samples;
	uint32_t read, waiting, write;
	OSTime opened_at;
	uint64_t made_since_open, made;
	int init_failures;
	/* (MICInit taking this long: HOME while it runs) */
	uint32_t init_delay_ms;
	int inits, opens, closes, uninits, consumptions;
	OSTime init_times[16];
	OSTime opened_last, closed_last;
} mic;

static int16_t mic_tone(uint64_t index)
{
	return (int16_t)lrint(0.25 * 32767.0 * sin(2.0 * 3.14159265358979323846 * 440.0 * (double)index / 32000.0));
}

/* (the samples the clock says have come since the opening) */
static void mic_hear(void)
{
	const uint64_t due = (uint64_t)(OSGetSystemTime() - mic.opened_at) * 32000u / 1000000u;

	while (mic.made_since_open < due)
	{
		const int16_t value = mic_tone(mic.made++);
		uint32_t at;

		mic.made_since_open++;
		if (mic.model == MODEL_CONSUME)
		{
			if (mic.waiting == mic.ring_samples)
				continue;
			at = (mic.read + mic.waiting) % mic.ring_samples;
			mic.waiting++;
		}
		else
		{
			at = mic.write;
			mic.write = (mic.write + 1) % mic.ring_samples;
		}
		mic.ring[2 * at] = (unsigned char)((uint16_t)value >> 8);
		mic.ring[2 * at + 1] = (unsigned char)value;
	}
}

MICHandle MICInit(MICInstance instance, int unused, MICWorkMemory *work_memory, MICError *error)
{
	(void)unused;
	if (mic.init_delay_ms)
		OSSleepTicks(OSMillisecondsToTicks(mic.init_delay_ms));
	pthread_mutex_lock(&mic_lock);
	if (mic.inits < 16)
		mic.init_times[mic.inits] = OSGetSystemTime();
	mic.inits++;
	if (mic.init_failures > 0 || instance != MIC_INSTANCE_0 || work_memory->sampleMaxCount < 0x2800 ||
		((uintptr_t)work_memory->sampleBuffer & 0x3f))
	{
		if (mic.init_failures > 0)
			mic.init_failures--;
		*error = MIC_ERROR_INIT;
		pthread_mutex_unlock(&mic_lock);
		return -1;
	}
	mic.initialized = 1;
	mic.ring = work_memory->sampleBuffer;
	mic.ring_samples = (uint32_t)work_memory->sampleMaxCount;
	*error = MIC_ERROR_OK;
	pthread_mutex_unlock(&mic_lock);
	return MIC_HANDLE;
}

MICError MICOpen(MICHandle handle)
{
	pthread_mutex_lock(&mic_lock);
	if (handle != MIC_HANDLE || !mic.initialized || mic.open)
	{
		pthread_mutex_unlock(&mic_lock);
		return MIC_ERROR_INVALID_HANDLE;
	}
	mic.open = 1;
	mic.opens++;
	mic.opened_at = mic.opened_last = OSGetSystemTime();
	mic.made_since_open = 0;
	mic.read = mic.waiting = mic.write = 0;
	pthread_mutex_unlock(&mic_lock);
	return MIC_ERROR_OK;
}

MICError MICGetState(MICHandle handle, int state, uint32_t *value)
{
	if (handle != MIC_HANDLE || !mic.initialized)
		return MIC_ERROR_INVALID_HANDLE;
	*value = state == 0 ? 32000u : (uint32_t)state;
	return MIC_ERROR_OK;
}

MICError MICSetState(MICHandle handle, int state, uint32_t value)
{
	(void)handle;
	(void)state;
	(void)value;
	return MIC_ERROR_OK;
}

MICError MICGetStatus(MICHandle handle, MICStatus *status)
{
	pthread_mutex_lock(&mic_lock);
	if (handle != MIC_HANDLE || !mic.open)
	{
		pthread_mutex_unlock(&mic_lock);
		return MIC_ERROR_NOT_OPENED;
	}
	mic_hear();
	status->state = 1 << 1;
	if (mic.model == MODEL_CONSUME)
	{
		status->availableData = (int)mic.waiting;
		status->bufferPos = (int)mic.read;
	}
	else
	{
		/* (availableData means nothing a reader may use) */
		status->availableData = 4096;
		status->bufferPos = (int)(mic.write * 2u);
	}
	pthread_mutex_unlock(&mic_lock);
	return MIC_ERROR_OK;
}

MICError MICSetDataConsumed(MICHandle handle, int consumed)
{
	pthread_mutex_lock(&mic_lock);
	if (handle != MIC_HANDLE || !mic.open)
	{
		pthread_mutex_unlock(&mic_lock);
		return MIC_ERROR_NOT_OPENED;
	}
	mic.consumptions++;
	if (mic.model == MODEL_CONSUME)
	{
		CHECK(consumed >= 0 && (uint32_t)consumed <= mic.waiting, "%d consumed of %u waiting", consumed, mic.waiting);
		mic.read = (mic.read + (uint32_t)consumed) % mic.ring_samples;
		mic.waiting -= (uint32_t)consumed;
	}
	pthread_mutex_unlock(&mic_lock);
	return MIC_ERROR_OK;
}

MICError MICClose(MICHandle handle)
{
	pthread_mutex_lock(&mic_lock);
	if (handle != MIC_HANDLE || !mic.open)
	{
		pthread_mutex_unlock(&mic_lock);
		return MIC_ERROR_ALREADY_CLOSED;
	}
	mic.open = 0;
	mic.closes++;
	mic.closed_last = OSGetSystemTime();
	pthread_mutex_unlock(&mic_lock);
	return MIC_ERROR_OK;
}

MICError MICUninit(MICHandle handle)
{
	pthread_mutex_lock(&mic_lock);
	if (handle != MIC_HANDLE || !mic.initialized)
	{
		pthread_mutex_unlock(&mic_lock);
		return MIC_ERROR_INVALID_HANDLE;
	}
	mic.initialized = 0;
	mic.uninits++;
	pthread_mutex_unlock(&mic_lock);
	return MIC_ERROR_OK;
}

/* ---------- the rest of the port, as the voice code calls it */

#define LOG_LINES 8192
static char log_lines[LOG_LINES][240];
static int log_count;
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

void wiiu_log(const char *format, ...)
{
	va_list arguments;

	pthread_mutex_lock(&log_lock);
	if (log_count < LOG_LINES)
	{
		va_start(arguments, format);
		vsnprintf(log_lines[log_count], sizeof(log_lines[0]), format, arguments);
		va_end(arguments);
		if (getenv("HALO_TEST_VERBOSE"))
			fprintf(stderr, "%8.3f %s\n", (double)OSGetSystemTime() / 1e6, log_lines[log_count]);
		log_count++;
	}
	pthread_mutex_unlock(&log_lock);
}

/* how many lines of the log have text in them */
static int logged(const char *text)
{
	int index, count = 0;

	pthread_mutex_lock(&log_lock);
	for (index = 0; index < log_count; index++)
		count += strstr(log_lines[index], text) != NULL;
	pthread_mutex_unlock(&log_lock);
	return count;
}

void wiiu_tag_thread(const char *name)
{
	(void)name;
}

void wiiu_untag_thread(void)
{
}

static int headset;
static int tv_taken, tv_takings;

int wiiu_input_headset(void)
{
	return headset;
}

unsigned int wiiu_input_mic_status(void)
{
	return 0;
}

void wiiu_input_take_tv_button(int take)
{
	tv_taken = take;
	tv_takings++;
}

static int config_voice_test;
static const char *config_reading = "auto";
static const char *config_chat = "push_to_talk";

int config_boolean(const char *name)
{
	return !strcmp(name, "debug.voice_test") ? config_voice_test : 0;
}

const char *config_string(const char *name)
{
	if (!strcmp(name, "audio.wiiu_microphone_read"))
		return config_reading;
	if (!strcmp(name, "audio.voice_chat"))
		return config_chat;
	return "";
}

double halo_sin(double x)
{
	return sin(x);
}

double halo_cos(double x)
{
	return cos(x);
}

int wiiu_gx2_thread_priority(void)
{
	return 16;
}

/* the card's folder, in the test's own (HALO_TEST_DIRECTORY) */
static char test_directory[512] = ".";

static FILE *card_fopen(const char *path, const char *mode)
{
	static const char prefix[] = "/vol/external01/wiiu/apps/halo";
	char mapped[1024];

	if (!strncmp(path, prefix, sizeof(prefix) - 1))
	{
		snprintf(mapped, sizeof(mapped), "%s%s", test_directory, path + sizeof(prefix) - 1);
		path = mapped;
	}
	return fopen(path, mode);
}

#define fopen(path, mode) card_fopen(path, mode)
#ifndef VOICE_AUDIO_SOURCE
#define VOICE_AUDIO_SOURCE "../src/wiiu_voice_audio.c"
#endif
#include VOICE_AUDIO_SOURCE
#undef fopen

/* ---------- the game's side (network_voice.c's every frame) and the mixer's */

static struct
{
	uint32_t frames, calls, packets, first_packet_call;
	double level_sum;
	uint32_t levels;
	uint16_t sequence;
	double mixed_sum;
	uint32_t mixed;
} game;

/* whether the game takes the packets no later frame took back (network_voice.c's, HALO_WIIU) */
static int game_takes_tail = 1;

/* the game thread's frame: the microphone wanted; each frame heard, and talking, encoded, and the packet it gives back
played as the speakers' (none: not played) */
static void game_frame(int wanted, int talking, int speaker, int other_speaker)
{
	float frame[VOICE_FRAME_SAMPLES];
	unsigned char packet[VOICE_MAXIMUM_PACKET];

	voice_audio_set_volume(1.0f);
	voice_audio_microphone(wanted);
	while (voice_audio_read_frame(frame))
	{
		int length;

		game.frames++;
		/* (past the resampler's and the microphone's start) */
		if (game.frames > 2)
		{
			game.level_sum += voice_audio_level(frame);
			game.levels++;
		}
		if (!talking)
			continue;
		game.calls++;
		length = voice_audio_encode(frame, 24000, packet, 136);
		if (length <= 0)
			continue;
		if (!game.packets)
			game.first_packet_call = game.calls;
		game.packets++;
		if (speaker >= 0)
			voice_audio_play(speaker, game.sequence, packet, length, 1.0f, 0.0f);
		if (other_speaker >= 0)
			voice_audio_play(other_speaker, game.sequence, packet, length, 1.0f, 0.0f);
		game.sequence++;
	}
	/* (network_voice.c's loop after that one: the packets of frames given before that no later frame took back) */
	if (wanted && game_takes_tail)
	{
		int length;

		while ((length = voice_audio_packet_ready(packet, 136)) > 0)
		{
			game.packets++;
			if (speaker >= 0)
				voice_audio_play(speaker, game.sequence, packet, length, 1.0f, 0.0f);
			game.sequence++;
		}
	}
}

/* the mixer's chunks of 64 frames, for so many milliseconds; the power of what it added, while it added something */
static double mix(uint32_t milliseconds)
{
	float output[64 * 2];
	uint32_t chunk, index;
	double sum = 0.0;
	uint32_t count = 0;

	for (chunk = 0; chunk < milliseconds * 48u / 64u; chunk++)
	{
		memset(output, 0, sizeof(output));
		voice_audio_mix(output, 64);
		for (index = 0; index < 64; index++)
		{
			sum += (double)output[2 * index] * output[2 * index];
			count++;
		}
	}
	if (sum > 0.0)
	{
		game.mixed_sum += sum;
		game.mixed += count;
	}
	return count ? sum / count : 0.0;
}

/* frames of the game, frame_ms apart, with the mixer's as long after each */
static void play_at(uint32_t milliseconds, uint32_t frame_ms, int wanted, int talking, int speaker, int other_speaker)
{
	uint32_t at;

	for (at = 0; at < milliseconds; at += frame_ms)
	{
		game_frame(wanted, talking, speaker, other_speaker);
		mix(frame_ms);
		advance(frame_ms);
	}
}

/* at 50 frames a second, the microphone's own rate */
static void play_for(uint32_t milliseconds, int wanted, int talking, int speaker, int other_speaker)
{
	play_at(milliseconds, 20, wanted, talking, speaker, other_speaker);
}

static double mean_level(void)
{
	return game.levels ? game.level_sum / game.levels : 0.0;
}

/* the tone's root mean square: a quarter of full scale over the square root of 2 */
#define TONE_LEVEL (0.25 / 1.41421356)

static long file_size(const char *name)
{
	char path[1024];
	FILE *file;
	long size;

	snprintf(path, sizeof(path), "%s/%s", test_directory, name);
	file = fopen(path, "rb");
	if (!file)
		return -1;
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fclose(file);
	return size;
}

static void card_write(const char *name, const char *text)
{
	char path[1024];
	FILE *file;

	snprintf(path, sizeof(path), "%s/%s", test_directory, name);
	file = fopen(path, "w");
	CHECK(file, "%s cannot be written", path);
	fputs(text, file);
	fclose(file);
}

static void card_remove(const char *name)
{
	char path[1024];

	snprintf(path, sizeof(path), "%s/%s", test_directory, name);
	remove(path);
}

static void card_first_line(const char *name, char *line, size_t size)
{
	char path[1024];
	FILE *file;

	snprintf(path, sizeof(path), "%s/%s", test_directory, name);
	line[0] = 0;
	file = fopen(path, "r");
	if (!file)
		return;
	if (!fgets(line, (int)size, file))
		line[0] = 0;
	fclose(file);
	line[strcspn(line, "\r\n")] = 0;
}

/* the probe's whole run against the microphone's model, HOME during the recording */
static void probe_case(int model, const char *finding)
{
	char line[128];
	long size;

	mic.model = model;
	card_remove("mic_probe_result.txt");
	card_remove("mic_probe.wav");
	card_write("mic_probe.txt", "1\n");
	wiiu_voice_boot();
	CHECK(running(), "the probe did not start the voice thread");
	/* (10 s of waiting, 2 s of polls, and 2 s of the recording) */
	advance(15000);
	CHECK(mic.open, "the microphone not open during the recording");
	wiiu_voice_suspend();
	CHECK(logged("mic probe: HOME during the probe: the last status"), "the probe did not see HOME");
	CHECK(!mic.open && mic.closes >= 1, "the microphone left open at HOME");
	advance(1000);
	wiiu_voice_resume();
	advance(12000);
	CHECK(logged("mic probe: back in the foreground after"), "the probe did not come back");
	CHECK(logged("mic probe: done") == 1, "the probe did not finish");
	CHECK(!mic.open, "the microphone left open after the probe");
	card_first_line("mic_probe_result.txt", line, sizeof(line));
	CHECK(!strcmp(line, finding), "the probe found \"%s\", not \"%s\"", line, finding);
	CHECK(logged("samples in") >= 9, "%d seconds of the recording logged", logged("samples in"));
	CHECK(logged("mic probe: 32000 samples in 1000 ms") >= 5, "the seconds did not bring 32000 samples each");
	size = file_size("mic_probe.wav");
	CHECK(size == 44 + 10 * 32000 * 2, "the recording is %ld bytes", size);
	CHECK(tv_takings == 2 && !tv_taken, "the TV button taken %d times, and %s at the end", tv_takings, tv_taken ? "held" : "given back");
	card_remove("mic_probe.txt");
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	const char *directory = getenv("HALO_TEST_DIRECTORY");

	if (directory && *directory)
		snprintf(test_directory, sizeof(test_directory), "%s", directory);
	mic.model = MODEL_CONSUME;
	config_reading = "consume";

	if (!strcmp(case_name, "round-trip"))
	{
		/* two seconds talking: the frames heard as they were made, each packet the frame before's, played back as it was */
		play_for(2000, TRUE, TRUE, 7, -1);
		CHECK(logged("Voice chat: the GamePad's microphone is open (voice chat wants it; no headset in its jack; its ring read as consume samples"),
			"no line of the microphone opening");
		CHECK(voice_audio_microphone(TRUE), "the microphone not said to be open");
		CHECK(game.frames >= 95 && game.frames <= 101, "%u frames in 2 s", game.frames);
		CHECK(fabs(mean_level() - TONE_LEVEL) < 0.02 * TONE_LEVEL, "the frames' level %.4f, the tone's %.4f", mean_level(), TONE_LEVEL);
		/* (a frame's packet comes back at the next call, not its own: the second, or the third when two frames came at once) */
		CHECK(game.first_packet_call == 2 || game.first_packet_call == 3, "the first packet came back at call %u", game.first_packet_call);
		CHECK(game.packets + 2 >= game.calls, "%u packets for %u frames", game.packets, game.calls);
		CHECK(game.mixed > 0, "nothing played");
		CHECK(fabs(sqrt(game.mixed_sum / game.mixed) - TONE_LEVEL) < 0.2 * TONE_LEVEL, "played back at %.4f, the tone at %.4f",
			sqrt(game.mixed_sum / game.mixed), TONE_LEVEL);
		CHECK(voice_audio_speaking(7) && !voice_audio_speaking(8), "speaking: 7 %d, 8 %d", voice_audio_speaking(7), voice_audio_speaking(8));
		CHECK(flushes > 1 && mic.consumptions > 0, "the ring flushed %d times, consumed %d", flushes, mic.consumptions);
		/* (the game stopped taking frames: five wait, the newest; counted with the voice thread held asleep, which would otherwise
		add one while they are taken) */
		advance(500);
		hold(voice_thread.host, 1);
		advance(15);
		{
			float frame[VOICE_FRAME_SAMPLES];
			uint32_t waiting = 0;

			voice_audio_microphone(TRUE);
			while (voice_audio_read_frame(frame))
				waiting++;
			CHECK(waiting == 5, "%u frames waited after half a second", waiting);
		}
		hold(voice_thread.host, 0);
		return 0;
	}
	if (!strcmp(case_name, "stale"))
	{
		float frame[VOICE_FRAME_SAMPLES];
		unsigned char packet[VOICE_MAXIMUM_PACKET];
		int length;

		play_for(200, TRUE, TRUE, 7, -1);
		advance(20);
		voice_audio_microphone(TRUE);
		CHECK(voice_audio_read_frame(frame), "no frame");
		voice_audio_encode(frame, 24000, packet, 136);
		/* the game stops for 200 ms: that frame's packet, ready meanwhile, is not sent when it comes back */
		advance(200);
		voice_audio_microphone(TRUE);
		CHECK(voice_audio_read_frame(frame), "no frame");
		length = voice_audio_encode(frame, 24000, packet, 136);
		CHECK(length == 0 && stale_dropped >= 1, "a packet of %d bytes, %u dropped as stale", length, stale_dropped);
		advance(20);
		voice_audio_microphone(TRUE);
		CHECK(voice_audio_read_frame(frame), "no frame");
		CHECK(voice_audio_encode(frame, 24000, packet, 136) > 0, "no packet after the stall's");
		return 0;
	}
	if (!strcmp(case_name, "slow-frames"))
	{
		/* the game at 30 frames a second (two microphone frames in some of its frames, one in others), then at 15: every
		packet sent, none waiting long enough to be dropped as stale */
		uint32_t calls, packets;

		play_at(3000, 33, TRUE, TRUE, 7, -1);
		CHECK(stale_dropped == 0 && game.packets + 2 >= game.calls, "at 30 frames a second: %u packets for %u frames, %u stale",
			game.packets, game.calls, stale_dropped);
		calls = game.calls;
		packets = game.packets;
		play_at(3000, 66, TRUE, TRUE, 7, -1);
		CHECK(stale_dropped == 0 && game.packets - packets + 3 >= game.calls - calls, "at 15 frames a second: %u packets for %u frames, %u stale",
			game.packets - packets, game.calls - calls, stale_dropped);
		CHECK(game.mixed > 0 && fabs(sqrt(game.mixed_sum / game.mixed) - TONE_LEVEL) < 0.25 * TONE_LEVEL, "played back at %.4f",
			game.mixed ? sqrt(game.mixed_sum / game.mixed) : 0.0);
		return 0;
	}
	if (!strcmp(case_name, "home-while-opening"))
	{
		/* HOME while MICInit takes 150 ms: the release callback cannot close a microphone that is being opened (it is under the
		lock), and the voice thread closes it the moment MICOpen returns */
		mic.init_delay_ms = 150;
		game_frame(TRUE, FALSE, -1, -1);
		advance(20);
		wiiu_voice_suspend();
		CHECK(logged("is in a microphone call: the microphone is left to the system"), "the callback did not find it in its call");
		advance(100);
		CHECK(mic.opens == 1 && !mic.open && mic.closes == 1 && mic.uninits == 1, "opened %d, open %d, closed %d, uninitialised %d",
			mic.opens, mic.open, mic.closes, mic.uninits);
		CHECK(mic.closed_last == mic.opened_last, "closed %lld ms after it opened, not at once",
			(long long)OSTicksToMilliseconds(mic.closed_last - mic.opened_last));
		CHECK(__atomic_load_n(&acknowledged, __ATOMIC_ACQUIRE) == __atomic_load_n(&request, __ATOMIC_ACQUIRE), "HOME not answered after");
		CHECK(!logged("Voice chat: the GamePad's microphone is open"), "said to be open");
		mic.init_delay_ms = 0;
		wiiu_voice_resume();
		play_for(100, TRUE, FALSE, -1, -1);
		CHECK(mic.open && mic.opens == 2, "not opened after HOME (opened %d times)", mic.opens);
		return 0;
	}
	if (!strcmp(case_name, "home"))
	{
		uint32_t frames;

		play_for(200, TRUE, FALSE, -1, -1);
		CHECK(mic.open && mic.opens == 1, "the microphone not open");
		wiiu_voice_suspend();
		CHECK(logged("Voice chat: given up for the background after"), "the voice thread did not answer HOME");
		CHECK(!mic.open && mic.closes == 1 && mic.uninits == 1, "at HOME: open %d, closed %d, uninitialised %d", mic.open, mic.closes, mic.uninits);
		CHECK(wiiu_voice_queue_count(&capture_queue) == 0, "frames left waiting in the background");
		CHECK(!voice_audio_hears(), "voices heard in the background");
		/* (in the background, nothing) */
		advance(2000);
		CHECK(mic.inits == 1, "the microphone set up again in the background");
		wiiu_voice_resume();
		frames = game.frames;
		play_for(100, TRUE, FALSE, -1, -1);
		CHECK(mic.open && mic.opens == 2, "the microphone not open again 100 ms after HOME (opened %d times)", mic.opens);
		CHECK(game.frames > frames, "no frames after HOME");
		mix(20);
		CHECK(voice_audio_hears(), "voices not heard after HOME");
		return 0;
	}
	if (!strcmp(case_name, "home-unanswered"))
	{
		play_for(200, TRUE, FALSE, -1, -1);
		CHECK(mic.open, "the microphone not open");
		/* the voice thread asleep and not woken: the release callback closes the microphone itself, after 100 ms */
		hold(voice_thread.host, 1);
		advance(30);
		wiiu_voice_suspend();
		CHECK(logged("Voice chat: the voice thread did not answer in 100 ms; the microphone closed here"), "the microphone not closed for it");
		CHECK(!mic.open && mic.closes == 1 && mic.uninits == 1, "open %d, closed %d, uninitialised %d", mic.open, mic.closes, mic.uninits);
		hold(voice_thread.host, 0);
		advance(50);
		CHECK(__atomic_load_n(&acknowledged, __ATOMIC_ACQUIRE) == __atomic_load_n(&request, __ATOMIC_ACQUIRE),
			"the voice thread did not answer once woken");
		CHECK(mic.closes == 1, "closed twice");
		wiiu_voice_resume();
		play_for(100, TRUE, FALSE, -1, -1);
		CHECK(mic.open && mic.opens == 2, "the microphone not open again (opened %d times)", mic.opens);
		return 0;
	}
	if (!strcmp(case_name, "tail"))
	{
		/* push to talk let go: the frames given up to then all have their packets sent, the last ones too (a second of talking at 50
		game frames a second, then 200 ms of silence with the microphone still open) */
		play_for(1000, TRUE, TRUE, 7, -1);
		play_for(200, TRUE, FALSE, 7, -1);
		CHECK(game.calls >= 45, "%u frames given", game.calls);
		CHECK(game.packets == game.calls, "%u packets sent for %u frames given: the last frames' packets were never taken back", game.packets,
			game.calls);
		CHECK(stale_dropped == 0, "%u packets dropped as stale", stale_dropped);
		/* (and at 15 game frames a second, three frames at a time) */
		play_at(1000, 66, TRUE, TRUE, 7, -1);
		play_at(300, 66, TRUE, FALSE, 7, -1);
		CHECK(game.packets == game.calls, "at 15 frames a second: %u packets for %u frames", game.packets, game.calls);
		return 0;
	}
	if (!strcmp(case_name, "home-closed"))
	{
		/* HOME with the microphone closed (no one has asked for it, or it was not wanted a second): the release callback does not wait for
		the voice thread, which is held asleep here and never answers; and the microphone does not open in the background */
		uint32_t before;

		voice_audio_microphone(FALSE);
		advance(100);
		hold(voice_thread.host, 1);
		before = (uint32_t)OSTicksToMilliseconds(OSGetSystemTime());
		wiiu_voice_suspend();
		CHECK((uint32_t)OSTicksToMilliseconds(OSGetSystemTime()) - before < 20u, "the release callback waited %u ms for nothing",
			(unsigned)((uint32_t)OSTicksToMilliseconds(OSGetSystemTime()) - before));
		CHECK(logged("Voice chat: HOME with no microphone open"), "no line of it");
		CHECK(!logged("did not answer"), "waited all the same");
		/* (held for 400 ms more, as if the system had stopped it: it says so when it runs again) */
		advance(400);
		hold(voice_thread.host, 0);
		/* (woken, it answers, and says how late: and asked for the microphone in the background it opens none) */
		voice_audio_microphone(TRUE);
		advance(300);
		CHECK(mic.inits == 0, "the microphone set up in the background");
		CHECK(logged("Voice chat: the voice thread's loop did not come round for"), "no line of the thread's gap");
		CHECK(__atomic_load_n(&acknowledged, __ATOMIC_ACQUIRE) == __atomic_load_n(&request, __ATOMIC_ACQUIRE), "the voice thread did not answer once woken");
		wiiu_voice_resume();
		play_for(200, TRUE, FALSE, -1, -1);
		CHECK(mic.open && mic.opens == 1, "the microphone not open after HOME (opened %d times)", mic.opens);
		return 0;
	}
	if (!strcmp(case_name, "complexity"))
	{
		/* halo/voice_complexity.txt: Opus's complexity, 5 without it; nonsense in it is ignored */
		card_write("voice_complexity.txt", "2\n");
		play_for(200, TRUE, TRUE, 7, -1);
		CHECK(logged("Voice chat: Opus complexity 2 (halo/voice_complexity.txt)"), "no line of the complexity");
		CHECK(game.packets > 0, "no packets at complexity 2");
		return 0;
	}
	if (!strcmp(case_name, "complexity-ignored"))
	{
		card_write("voice_complexity.txt", "99\n");
		play_for(200, TRUE, TRUE, 7, -1);
		CHECK(!logged("Opus complexity"), "a complexity of 99 taken");
		CHECK(game.packets > 0, "no packets");
		return 0;
	}
	if (!strcmp(case_name, "close"))
	{
		CHECK(wiiu_voice_stop(300) == 2, "a thread stopped that never started");
		play_for(200, TRUE, TRUE, 7, -1);
		CHECK(mic.open, "the microphone not open");
		CHECK(wiiu_voice_stop(300) == 1, "the voice thread did not end");
		CHECK(voice_thread.joined && !mic.open && mic.closes == 1 && mic.uninits == 1, "joined %d, open %d, closed %d", voice_thread.joined,
			mic.open, mic.closes);
		CHECK(logged("Voice chat: the voice thread ends"), "no line of the end");
		/* (after the close, voice chat is nothing: no thread again, no microphone) */
		play_for(100, TRUE, TRUE, 7, -1);
		CHECK(mic.inits == 1 && !voice_audio_hears(), "voice chat started again after the close");
		return 0;
	}
	if (!strcmp(case_name, "retry"))
	{
		mic.init_failures = 2;
		play_for(25000, TRUE, FALSE, -1, -1);
		CHECK(mic.inits == 3 && mic.open, "%d set-ups, open %d", mic.inits, mic.open);
		CHECK(mic.init_times[1] - mic.init_times[0] >= OSMillisecondsToTicks(10000) &&
			mic.init_times[1] - mic.init_times[0] < OSMillisecondsToTicks(10100), "tried again after %lld ms",
			(long long)OSTicksToMilliseconds(mic.init_times[1] - mic.init_times[0]));
		CHECK(mic.init_times[2] - mic.init_times[1] >= OSMillisecondsToTicks(10000), "tried a third time after %lld ms",
			(long long)OSTicksToMilliseconds(mic.init_times[2] - mic.init_times[1]));
		CHECK(logged("could not be set up (MICInit: error -5); again in 10 s") == 2, "the failures not logged");
		CHECK(logged("Voice chat: the GamePad's microphone is open (tried again"), "the opening not logged");
		CHECK(game.frames > 0, "no frames once open");
		return 0;
	}
	if (!strcmp(case_name, "position"))
	{
		/* the ring read by its write position, as the setting says, and as the probe's finding says ("auto") */
		mic.model = MODEL_POSITION;
		config_reading = "position";
		play_for(1000, TRUE, TRUE, 7, -1);
		CHECK(logged("its ring read as position bytes, by audio.wiiu_microphone_read"), "not read by position");
		CHECK(game.frames >= 45 && fabs(mean_level() - TONE_LEVEL) < 0.02 * TONE_LEVEL, "%u frames at %.4f", game.frames, mean_level());
		CHECK(mic.consumptions == 0, "a write position consumed");
		CHECK(game.mixed > 0, "nothing played");
		CHECK(wiiu_voice_stop(300) == 1, "not stopped");
		return 0;
	}
	if (!strcmp(case_name, "position-auto"))
	{
		mic.model = MODEL_POSITION;
		config_reading = "auto";
		card_write("mic_probe_result.txt", "position bytes\n# the probe's\n");
		play_for(1000, TRUE, FALSE, -1, -1);
		CHECK(logged("its ring read as position bytes, by the probe's finding"), "the probe's finding not followed");
		CHECK(game.frames >= 45 && fabs(mean_level() - TONE_LEVEL) < 0.02 * TONE_LEVEL, "%u frames at %.4f", game.frames, mean_level());
		card_remove("mic_probe_result.txt");
		return 0;
	}
	if (!strcmp(case_name, "forget"))
	{
		play_for(1000, TRUE, TRUE, 7, -1);
		CHECK(voice_audio_speaking(7), "not speaking");
		voice_audio_forget(7);
		advance(20);
		CHECK(mix(20) == 0.0 && !voice_audio_speaking(7), "a forgotten speaker played");
		/* two speakers, both forgotten at once (a session's end) */
		play_for(1000, TRUE, TRUE, 8, 9);
		CHECK(voice_audio_speaking(8) && voice_audio_speaking(9), "not both speaking");
		voice_audio_forget_all();
		advance(20);
		CHECK(mix(20) == 0.0 && !voice_audio_speaking(8) && !voice_audio_speaking(9), "forgotten speakers played");
		return 0;
	}
	if (!strcmp(case_name, "voice-test"))
	{
		config_voice_test = 1;
		CHECK(voice_audio_microphone(TRUE), "the tone's microphone not open at once");
		play_for(2000, TRUE, TRUE, 7, -1);
		CHECK(mic.inits == 0, "a microphone set up for the tone");
		CHECK(game.frames >= 95 && fabs(mean_level() - 0.3 / 1.41421356) < 0.003, "%u frames at %.4f", game.frames, mean_level());
		CHECK(logged("voice test: speaker 7 level 0.2") >= 1, "no voice test line");
		return 0;
	}
	if (!strcmp(case_name, "hears"))
	{
		CHECK(!voice_audio_hears(), "voices heard before the mixer ran");
		mix(20);
		CHECK(voice_audio_hears(), "voices not heard with the mixer running");
		wiiu_voice_suspend();
		CHECK(!voice_audio_hears(), "voices heard in the background");
		wiiu_voice_resume();
		mix(20);
		CHECK(voice_audio_hears(), "voices not heard after HOME");
		advance(1000);
		CHECK(!voice_audio_hears(), "voices heard with the mixer stopped");
		return 0;
	}
	if (!strcmp(case_name, "no-thread"))
	{
		/* a voice thread that cannot be made: nothing sent or heard, nothing waits */
		fail_thread = 1;
		play_for(200, TRUE, TRUE, 7, -1);
		mix(20);
		CHECK(!voice_audio_hears() && game.frames == 0 && mic.inits == 0, "voice chat without its thread");
		CHECK(logged("Voice chat: no voice thread"), "not logged");
		return 0;
	}
	if (!strcmp(case_name, "probe"))
	{
		probe_case(MODEL_CONSUME, "consume samples");
		return 0;
	}
	if (!strcmp(case_name, "probe-position"))
	{
		probe_case(MODEL_POSITION, "position bytes");
		return 0;
	}
	fprintf(stderr, "no case %s\n", case_name);
	return 2;
}
