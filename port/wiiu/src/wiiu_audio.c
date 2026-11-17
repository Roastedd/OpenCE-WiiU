/*
WIIU_AUDIO.C

Sound output for the game's software mixer (dsound_sdl.c's mix): two AX voices, one for each ear, play 16-bit samples round and round a ring buffer, and
a thread of ours keeps the ring full a little ahead of where they are, by calling the mixer for a chunk of frames at a time, as SDL's audio callback would.
The same samples go to the TV and to the GamePad.

48 kHz, stereo. The ring is 4096 frames (85 ms); the thread keeps the write cursor between 768 and 1536 frames ahead of the voices'.

The pacing and resampler paths have native console measurements under reports/reviews.
*/

#ifndef JOIN_BEAT
#ifdef HALO_WIIU
/* the join record's thread counters (port/wiiu/src/wiiu_join_trace.c) */
extern volatile unsigned wiiu_thread_beats[5];
#define JOIN_BEAT(index) (wiiu_thread_beats[index]++)
#else
#define JOIN_BEAT(index) ((void)0)
#endif
#endif

#include "wiiu_shell.h"
#include "wiiu_audio_ring.h"
#include "wiiu_mixer_profile.h"
#include "../include/wiiu_resampler_ps.h"

#include <coreinit/cache.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <sndcore2/core.h>
#include <sndcore2/device.h>
#include <sndcore2/voice.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RING_FRAMES WIIU_AUDIO_RING_FRAMES
/* (64 frames, 1.3 ms, a mix at a time: the mixer holds the sound's lock while it mixes every voice, and the game thread waits for it to queue or change a sound -
10% of its time while the assault rifle fired with chunks of 256; a quarter of the chunk is about a quarter of each wait) */
#define CHUNK_FRAMES WIIU_AUDIO_CHUNK_FRAMES
#define AHEAD_LOW 768u
#define AHEAD_HIGH WIIU_AUDIO_AHEAD_HIGH
#define STACK_BYTES (64 * 1024)

static void (*render)(float *output, unsigned long frames);
static AXVoice *voices[2];
static int16_t *rings[2];
static uint32_t written; /* frames made so far, counted without wrapping */
static OSThread thread;
static uint8_t stack[STACK_BYTES] __attribute__((aligned(16)));
static volatile int started;
static volatile int suspended;
static int ax_owned; /* this output initialized AX and must shut it down */
static int mixer_profile_enabled;
static struct wiiu_audio_mix_pressure mix_pressure;

/* the mixer's time, for the detailed overlay (wiiu_perf.h): monotonic counts, written by the audio thread only, 32 bits so that another thread reads them whole */
static volatile uint32_t perf_mixer_ticks, perf_mixer_chunks;

void wiiu_audio_perf(uint32_t *mixer_ticks, uint32_t *mixer_chunks);

void wiiu_audio_perf(uint32_t *mixer_ticks, uint32_t *mixer_chunks)
{
	*mixer_ticks = perf_mixer_ticks;
	*mixer_chunks = perf_mixer_chunks;
}

int wiiu_audio_profile_is_enabled(void)
{
	return mixer_profile_enabled;
}

void wiiu_audio_profile_mix_pressure(const struct wiiu_audio_mix_pressure *pressure)
{
	mix_pressure = *pressure;
}

static void set_mix(AXVoice *voice, int ear)
{
	AXVoiceDeviceMixData mix[6];
	AXDeviceType devices[2] = { AX_DEVICE_TYPE_TV, AX_DEVICE_TYPE_DRC };
	unsigned int device;

	for (device = 0; device < 2; device++)
	{
		memset(mix, 0, sizeof(mix));
		mix[ear].bus[0].volume = 0x8000;
		AXSetVoiceDeviceMix(voice, devices[device], 0, mix);
	}
}

/* frames the voices have played, as an offset into the ring */
static uint32_t played_offset(void)
{
	AXVoiceOffsets offsets;

	AXGetVoiceOffsets(voices[0], &offsets);
	return offsets.currentOffset;
}

/* port: log a readiness change outside the render measurement and hot dots. */
static void resampler_report(void)
{
	struct wiiu_resampler_ps_status status;

	if (wiiu_resampler_ps_take_report(&status))
		wiiu_log("Audio resampler: %s, core %u, FPSCR %08x, GQR0 %08x, selftest %u cases, failed case %u",
			wiiu_resampler_ps_reason(status.result), status.core, status.fpscr, status.gqr0,
			status.selftest_cases, status.failed_case);
}

static int audio_thread(int argc, const char **argv)
{
	float mixed[CHUNK_FRAMES * 2];
	struct wiiu_audio_ring_observations observations = { 0 };
	struct wiiu_mixer_profile profile;
	OSTime last_poll = 0, last_report = 0;
	const uint64_t budget = (uint64_t)OSMillisecondsToTicks(1000) * CHUNK_FRAMES / 48000u;
	int have_poll = 0;
	/* port: how long this thread has run without a sleep, and the times it has been made to sleep (see the end of a chunk) */
	OSTime burst_started = OSGetSystemTime();
	unsigned overload_sleeps = 0;

	(void)argc;
	(void)argv;
	/* port: native full-mixer NI-mode checks qualify these audio-only dots. */
	wiiu_resampler_ps_worker_begin(1);
	resampler_report();
	wiiu_mixer_profile_reset(&profile);
	for (;;)
	{
		uint32_t play;

		JOIN_BEAT(1);
		if (suspended)
		{
			/* New AX voices start with a fresh ring after HOME. */
			have_poll = 0;
			memset(&observations, 0, sizeof(observations));
			wiiu_mixer_profile_reset(&profile);
			OSSleepTicks(OSMillisecondsToTicks(10));
			continue;
		}
		play = played_offset();
		const uint32_t write = written % RING_FRAMES;
		/* how far ahead of the voices the cursor is, around the ring */
		const uint32_t ahead = (write + RING_FRAMES - play) % RING_FRAMES;
		const OSTime now = OSGetSystemTime();

		if (have_poll)
		{
			if (mixer_profile_enabled)
				wiiu_mixer_profile_poll(&profile, ahead, (uint64_t)(now - last_poll));
			/* 85 ms is a conservative threshold for the 85.33 ms ring. */
			wiiu_audio_ring_observe(&observations, write, play,
				now - last_poll >= (OSTime)OSMillisecondsToTicks(85));
			if (now - last_report >= (OSTime)OSMillisecondsToTicks(10000))
			{
				wiiu_log("Audio ring: %u apparent overtakes, %u at deadline, %u long polling gaps, %u overload sleeps (10 s)",
					(unsigned)observations.apparent_overtakes, (unsigned)observations.at_deadline,
					(unsigned)observations.long_gaps, overload_sleeps);
				overload_sleeps = 0;
				observations.apparent_overtakes = observations.at_deadline = observations.long_gaps = 0;
				if (mixer_profile_enabled)
				{
					const double ticks_per_ms = (double)OSMillisecondsToTicks(1);
					const double chunks = (double)profile.chunks;

					wiiu_log("Audio mix profile: %u chunks, render mean/max %.3f/%.3f ms, %u over %.3f ms; lock/handoff mean/max %.3f/%.3f ms",
						(unsigned)profile.chunks, chunks ? (double)profile.render_total / chunks / ticks_per_ms : 0.0,
						(double)profile.render_max / ticks_per_ms, (unsigned)profile.over_budget,
						(double)budget / ticks_per_ms, chunks ? (double)profile.lock_total / chunks / ticks_per_ms : 0.0,
						(double)profile.lock_max / ticks_per_ms);
					wiiu_log("Audio mix profile: considered streams mean/max %.1f/%u, queued-unpaused mean/max %.1f/%u, min apparent lead %u frames, max poll gap %.3f ms",
						chunks ? (double)profile.streams_total / chunks : 0.0, (unsigned)profile.streams_max,
						chunks ? (double)profile.queued_total / chunks : 0.0, (unsigned)profile.queued_max,
						(unsigned)(profile.polls ? profile.lead_min : 0), (double)profile.poll_max / ticks_per_ms);
					wiiu_log("Audio mix profile: voices+lock mean/max %.3f/%.3f ms, reverb %.3f/%.3f ms, chat %.3f/%.3f ms, limiter %.3f/%.3f ms",
						chunks ? (double)profile.stage_total[WIIU_MIXER_VOICES] / chunks / ticks_per_ms : 0.0,
						(double)profile.stage_max[WIIU_MIXER_VOICES] / ticks_per_ms,
						chunks ? (double)profile.stage_total[WIIU_MIXER_REVERB] / chunks / ticks_per_ms : 0.0,
						(double)profile.stage_max[WIIU_MIXER_REVERB] / ticks_per_ms,
						chunks ? (double)profile.stage_total[WIIU_MIXER_CHAT] / chunks / ticks_per_ms : 0.0,
						(double)profile.stage_max[WIIU_MIXER_CHAT] / ticks_per_ms,
						chunks ? (double)profile.stage_total[WIIU_MIXER_LIMITER] / chunks / ticks_per_ms : 0.0,
						(double)profile.stage_max[WIIU_MIXER_LIMITER] / ticks_per_ms);
					wiiu_mixer_profile_reset(&profile);
				}
				last_report = now;
			}
		}
		else
		{
			have_poll = 1;
			last_report = now;
		}
		last_poll = now;

		if (ahead < AHEAD_HIGH)
		{
			uint32_t at = write, i;
			OSTime render_started = 0;

			/* port: an unexpected worker mode permanently selects the C path. */
			wiiu_resampler_ps_check_chunk();
			resampler_report();
			if (mixer_profile_enabled)
			{
				memset(&mix_pressure, 0, sizeof(mix_pressure));
				render_started = OSGetSystemTime();
			}
			{
				/* (two reads of the time base a chunk, 750 chunks a second: the detailed overlay's AUDIO) */
				const OSTime mix_started = OSGetSystemTime();

				render(mixed, CHUNK_FRAMES);
				perf_mixer_ticks += (uint32_t)(OSGetSystemTime() - mix_started);
				perf_mixer_chunks++;
			}
			if (mixer_profile_enabled)
				wiiu_mixer_profile_chunk(&profile, (uint64_t)(OSGetSystemTime() - render_started), budget, &mix_pressure);
			for (i = 0; i < CHUNK_FRAMES; i++)
			{
				int left = (int)(mixed[i * 2] * 32767.0f), right = (int)(mixed[i * 2 + 1] * 32767.0f);

				rings[0][at] = (int16_t)(left < -32768 ? -32768 : left > 32767 ? 32767 : left);
				rings[1][at] = (int16_t)(right < -32768 ? -32768 : right > 32767 ? 32767 : right);
				at = (at + 1) % RING_FRAMES;
			}
			written += CHUNK_FRAMES;
			/* (the part of the ring written, flushed for the voices: a chunk never wraps, the ring being a whole number of them) */
			DCFlushRange(rings[0] + write, CHUNK_FRAMES * sizeof(int16_t));
			DCFlushRange(rings[1] + write, CHUNK_FRAMES * sizeof(int16_t));
			/* Keep filling up to the high-water mark before sleeping. A 2 ms
			   sleep after every 64-frame chunk consumes more than a chunk. */
			/* port: but not for ever. This thread is above everything else on its core (priority 10: the watchdog, the black box, the log, the card's callbacks), and a mixer
			   that cannot keep up (the merged mixer, with the dozens of sounds that a joined game starts at once) never finds the ring ahead, so it never slept: the core
			   was taken whole and the console stopped answering. After 4 ms of work it sleeps 1 ms whatever the ring says: a sound that stutters for a moment, not a hung console. */
			if (OSGetSystemTime() - burst_started > (OSTime)OSMillisecondsToTicks(4))
			{
				OSSleepTicks(OSMillisecondsToTicks(1));
				burst_started = OSGetSystemTime();
				overload_sleeps++;
			}
		}
		else
		{
			OSSleepTicks(OSMillisecondsToTicks(2));
			burst_started = OSGetSystemTime();
		}
	}
	return 0;
}

/* Release everything acquired so far, including a partly initialized pair.
The rings may be kept across HOME; a failed start releases them as well. */
static void voices_stop(int free_rings)
{
	unsigned int ear;

	for (ear = 0; ear < 2; ear++)
	{
		if (voices[ear])
		{
			AXVoiceBegin(voices[ear]);
			AXSetVoiceState(voices[ear], AX_VOICE_STATE_STOPPED);
			AXVoiceEnd(voices[ear]);
			AXFreeVoice(voices[ear]);
			voices[ear] = NULL;
		}
		if (free_rings && rings[ear])
		{
			MEMFreeToDefaultHeap(rings[ear]);
			rings[ear] = NULL;
		}
	}
	if (ax_owned)
	{
		AXQuit();
		ax_owned = 0;
	}
}

/* the AX side: the engine started and the two voices set to play their rings round and round. 0 if something was not to be had. */
static int voices_start(void)
{
	AXInitParams parameters;
	unsigned int ear;

	memset(&parameters, 0, sizeof(parameters));
	parameters.renderer = AX_INIT_RENDERER_48KHZ;
	parameters.pipeline = AX_INIT_PIPELINE_SINGLE;
	if (!AXIsInit())
	{
		AXInitWithParams(&parameters);
		if (!AXIsInit())
		{
			wiiu_log("Audio: AX could not be initialized");
			voices_stop(1);
			return 0;
		}
		ax_owned = 1;
	}
	for (ear = 0; ear < 2; ear++)
	{
		if (!rings[ear])
			rings[ear] = MEMAllocFromDefaultHeapEx(RING_FRAMES * sizeof(int16_t), 64);
		if (rings[ear])
			voices[ear] = AXAcquireVoice(25, NULL, NULL);
		if (!rings[ear] || !voices[ear])
		{
			wiiu_log("Audio: no memory or no voice for the %s ear", ear ? "right" : "left");
			voices_stop(1);
			return 0;
		}
		memset(rings[ear], 0, RING_FRAMES * sizeof(int16_t));
		DCFlushRange(rings[ear], RING_FRAMES * sizeof(int16_t));
	}
	AXVoiceBegin(voices[0]);
	AXVoiceBegin(voices[1]);
	for (ear = 0; ear < 2; ear++)
	{
		AXVoiceVeData volume = { 0x8000, 0 };
		AXVoiceOffsets offsets;
		AXVoiceSrc source;

		memset(&offsets, 0, sizeof(offsets));
		offsets.dataType = AX_VOICE_FORMAT_LPCM16;
		offsets.loopingEnabled = AX_VOICE_LOOP_ENABLED;
		offsets.loopOffset = 0;
		offsets.endOffset = RING_FRAMES - 1;
		offsets.currentOffset = 0;
		offsets.data = rings[ear];
		AXSetVoiceType(voices[ear], 0);
		AXSetVoiceOffsets(voices[ear], &offsets);
		AXSetVoiceVe(voices[ear], &volume);
		set_mix(voices[ear], (int)ear);
		memset(&source, 0, sizeof(source));
		source.ratio = 0x00010000;
		AXSetVoiceSrc(voices[ear], &source);
		AXSetVoiceSrcType(voices[ear], AX_VOICE_SRC_TYPE_NONE);
		AXSetVoiceState(voices[ear], AX_VOICE_STATE_PLAYING);
	}
	AXVoiceEnd(voices[0]);
	AXVoiceEnd(voices[1]);
	/* the ring starts AHEAD_LOW frames ahead of where the voices are */
	written = AHEAD_LOW;
	return 1;
}

int wiiu_audio_open(void (*mixer)(float *output, unsigned long frames));

int wiiu_audio_open(void (*mixer)(float *output, unsigned long frames))
{
	FILE *profile_file;
	int profile_value = 0;

	if (started)
		return 1;
	profile_file = fopen(WIIU_DATA_DIRECTORY "/audio_profile.txt", "r");
	if (profile_file)
	{
		if (fscanf(profile_file, "%d", &profile_value) != 1)
			profile_value = 0;
		fclose(profile_file);
	}
	mixer_profile_enabled = profile_value == 1;
	if (mixer_profile_enabled)
		wiiu_log("Audio mix profile: timing enabled (audio_profile.txt)");
	render = mixer;
	if (!voices_start())
		return 0;
	if (!OSCreateThread(&thread, audio_thread, 0, NULL, stack + STACK_BYTES, STACK_BYTES, 10,
			(OSThreadAttributes)(OS_THREAD_ATTRIB_AFFINITY_CPU0 | OS_THREAD_ATTRIB_DETACHED)))
	{
		wiiu_log("Audio: no thread");
		voices_stop(1);
		return 0;
	}
	OSSetThreadName(&thread, "audio mixer");
	OSResumeThread(&thread);
	started = 1;
	wiiu_log("Audio: AX at 48 kHz, two voices on a ring of %u frames", (unsigned)RING_FRAMES);
	return 1;
}

/* The app goes to the background (HOME): the system takes the sound engine. The mixer thread is parked first, then the voices let go and AX quit. */
/* the mixer thread's core time (the shell's log of the threads' shares of their cores): nanoseconds it has been given, and how many times it was woken; 0 if there is none */
uint64_t wiiu_audio_thread_cpu_ns(uint64_t *wakes);

uint64_t wiiu_audio_thread_cpu_ns(uint64_t *wakes)
{
	*wakes = started ? thread.wakeCount : 0;
	return started ? thread.coreTimeConsumedNs : 0;
}

void wiiu_audio_suspend(void);
void wiiu_audio_resume(void);

void wiiu_audio_suspend(void)
{
	if (!started || suspended)
		return;
	suspended = 1;
	/* (the thread notices at its next turn: a few milliseconds) */
	OSSleepTicks(OSMillisecondsToTicks(20));
	voices_stop(0);
	wiiu_log("Audio: given up for the background");
}

void wiiu_audio_resume(void)
{
	if (!started || !suspended)
		return;
	if (voices_start())
	{
		wiiu_log("Audio: back");
		suspended = 0;
	}
	else
		wiiu_log("Audio: could not be started again");
}
