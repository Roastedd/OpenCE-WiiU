/*
WIIU_AUDIO.C

Sound output for the game's software mixer (dsound_sdl.c's mix): two AX voices, one for each ear, play 16-bit samples round and round a ring buffer, and
a thread of ours keeps the ring full a little ahead of where they are, by calling the mixer for a chunk of frames at a time, as SDL's audio callback would.
The same samples go to the TV and to the GamePad.

48 kHz, stereo. The ring is 4096 frames (85 ms); the thread keeps the write cursor between 768 and 1536 frames ahead of the voices'.

Not run on a console.
*/

#include "wiiu_shell.h"

#include <coreinit/cache.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <sndcore2/core.h>
#include <sndcore2/device.h>
#include <sndcore2/voice.h>

#include <stdint.h>
#include <string.h>

#define RING_FRAMES 4096u
#define CHUNK_FRAMES 256u
#define AHEAD_LOW 768u
#define AHEAD_HIGH 1536u
#define STACK_BYTES (64 * 1024)

static void (*render)(float *output, unsigned long frames);
static AXVoice *voices[2];
static int16_t *rings[2];
static uint32_t written; /* frames made so far, counted without wrapping */
static OSThread thread;
static uint8_t stack[STACK_BYTES] __attribute__((aligned(16)));
static volatile int started;
static volatile int suspended;

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

static int audio_thread(int argc, const char **argv)
{
	float mixed[CHUNK_FRAMES * 2];

	(void)argc;
	(void)argv;
	for (;;)
	{
		uint32_t play;

		if (suspended)
		{
			OSSleepTicks(OSMillisecondsToTicks(10));
			continue;
		}
		play = played_offset();
		const uint32_t write = written % RING_FRAMES;
		/* how far ahead of the voices the cursor is, around the ring */
		const uint32_t ahead = (write + RING_FRAMES - play) % RING_FRAMES;

		if (ahead < AHEAD_HIGH)
		{
			uint32_t at = write, i;

			render(mixed, CHUNK_FRAMES);
			for (i = 0; i < CHUNK_FRAMES; i++)
			{
				int left = (int)(mixed[i * 2] * 32767.0f), right = (int)(mixed[i * 2 + 1] * 32767.0f);

				rings[0][at] = (int16_t)(left < -32768 ? -32768 : left > 32767 ? 32767 : left);
				rings[1][at] = (int16_t)(right < -32768 ? -32768 : right > 32767 ? 32767 : right);
				at = (at + 1) % RING_FRAMES;
			}
			written += CHUNK_FRAMES;
			/* (the part of the ring written, flushed for the voices) */
			DCFlushRange(rings[0], RING_FRAMES * sizeof(int16_t));
			DCFlushRange(rings[1], RING_FRAMES * sizeof(int16_t));
			if (ahead >= AHEAD_LOW)
				OSSleepTicks(OSMillisecondsToTicks(2));
		}
		else
		{
			OSSleepTicks(OSMillisecondsToTicks(2));
		}
	}
	return 0;
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
		AXInitWithParams(&parameters);
	for (ear = 0; ear < 2; ear++)
	{
		if (!rings[ear])
			rings[ear] = MEMAllocFromDefaultHeapEx(RING_FRAMES * sizeof(int16_t), 64);
		voices[ear] = AXAcquireVoice(25, NULL, NULL);
		if (!rings[ear] || !voices[ear])
		{
			wiiu_log("Audio: no memory or no voice for the %s ear", ear ? "right" : "left");
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
	if (started)
		return 1;
	render = mixer;
	if (!voices_start())
		return 0;
	if (!OSCreateThread(&thread, audio_thread, 0, NULL, stack + STACK_BYTES, STACK_BYTES, 10,
			(OSThreadAttributes)(OS_THREAD_ATTRIB_AFFINITY_CPU0 | OS_THREAD_ATTRIB_DETACHED)))
	{
		wiiu_log("Audio: no thread");
		return 0;
	}
	OSSetThreadName(&thread, "audio mixer");
	OSResumeThread(&thread);
	started = 1;
	wiiu_log("Audio: AX at 48 kHz, two voices on a ring of %u frames", (unsigned)RING_FRAMES);
	return 1;
}

/* The app goes to the background (HOME): the system takes the sound engine. The mixer thread is parked first, then the voices let go and AX quit. */
void wiiu_audio_suspend(void);
void wiiu_audio_resume(void);

void wiiu_audio_suspend(void)
{
	unsigned int ear;

	if (!started || suspended)
		return;
	suspended = 1;
	/* (the thread notices at its next turn: a few milliseconds) */
	OSSleepTicks(OSMillisecondsToTicks(20));
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
	}
	AXQuit();
	wiiu_log("Audio: given up for the background");
}

void wiiu_audio_resume(void)
{
	if (!started || !suspended)
		return;
	if (voices_start())
		wiiu_log("Audio: back");
	else
		wiiu_log("Audio: could not be started again");
	suspended = 0;
}
