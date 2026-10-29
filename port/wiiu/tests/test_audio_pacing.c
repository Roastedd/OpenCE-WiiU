/* Run the production mixer loop against a deterministic 48 kHz AX clock. */
#include "mock.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/wiiu_audio.c"

static jmp_buf finished;
static uint64_t now_us, end_us, mix_us, stall_at_us, stall_us;
static uint32_t minimum_lead_after_warmup;
static int stalled, underrun;
static unsigned mix_calls, sleep_calls;
static unsigned readiness_begin_calls, readiness_chunk_calls;
static AXVoice fake_voice;
static int16_t fake_ring[2][RING_FRAMES];

/* Each simulated microsecond consumes 48/1000 source frames. */
static uint64_t played(void) { return now_us * 48 / 1000; }
static void advance(uint64_t delta)
{
	uint64_t target = now_us + delta;

	if (target > end_us)
		target = end_us;
	if (target * 48 / 1000 >= written)
	{
		underrun = 1;
		longjmp(finished, 1);
	}
	now_us = target;
	if (now_us >= end_us)
		longjmp(finished, 1);
}

void AXGetVoiceOffsets(AXVoice *voice, AXVoiceOffsets *offsets)
{
	uint32_t lead = written - (uint32_t)played();
	(void)voice;
	if (now_us >= 100000 && lead < minimum_lead_after_warmup)
		minimum_lead_after_warmup = lead;
	offsets->currentOffset = (uint32_t)(played() % RING_FRAMES);
}

OSTime OSGetSystemTime(void) { return now_us; }
OSTime OSMillisecondsToTicks(unsigned ms) { return (OSTime)ms * 1000; }
void OSSleepTicks(OSTime ticks) { sleep_calls++; advance(ticks); }
void DCFlushRange(void *ptr, size_t length) { (void)ptr; (void)length; }
void wiiu_log(const char *fmt, ...) { (void)fmt; }

/* This fixture measures pacing with a scalar policy. Native paired-single
   readiness and numerical equivalence have their own policy/kernel tests. */
int wiiu_resampler_ps_worker_begin(int requested)
{
	assert(requested == 1);
	readiness_begin_calls++;
	return 0;
}
int wiiu_resampler_ps_check_chunk(void)
{
	readiness_chunk_calls++;
	return 0;
}
int wiiu_resampler_ps_take_report(struct wiiu_resampler_ps_status *status)
{
	(void)status;
	return 0;
}
const char *wiiu_resampler_ps_reason(unsigned result)
{
	(void)result;
	return "scalar pacing fixture";
}

static void simulate_mix(float *output, unsigned long frames)
{
	uint64_t duration = mix_us;

	assert(frames == CHUNK_FRAMES);
	if (!stalled && stall_at_us && now_us >= stall_at_us)
	{
		stalled = 1;
		duration = stall_us;
	}
	mix_calls++;
	advance(duration);
	memset(output, 0, frames * 2 * sizeof(*output));
}

/* The rest of the included Wii U output unit is not exercised by this test. */
void *MEMAllocFromDefaultHeapEx(size_t n, int align) { (void)align; return malloc(n); }
void MEMFreeToDefaultHeap(void *p) { free(p); }
int AXIsInit(void) { return 1; }
void AXInitWithParams(AXInitParams *p) { (void)p; }
void AXQuit(void) { }
AXVoice *AXAcquireVoice(int p, void *a, void *b) { (void)p; (void)a; (void)b; return &fake_voice; }
void AXFreeVoice(AXVoice *v) { (void)v; }
void AXVoiceBegin(AXVoice *v) { (void)v; }
void AXVoiceEnd(AXVoice *v) { (void)v; }
void AXSetVoiceDeviceMix(AXVoice *v, int d, int x, AXVoiceDeviceMixData *m) { (void)v; (void)d; (void)x; (void)m; }
void AXSetVoiceType(AXVoice *v, int x) { (void)v; (void)x; }
void AXSetVoiceOffsets(AXVoice *v, AXVoiceOffsets *o) { (void)v; (void)o; }
void AXSetVoiceVe(AXVoice *v, AXVoiceVeData *d) { (void)v; (void)d; }
void AXSetVoiceSrc(AXVoice *v, AXVoiceSrc *d) { (void)v; (void)d; }
void AXSetVoiceSrcType(AXVoice *v, int t) { (void)v; (void)t; }
void AXSetVoiceState(AXVoice *v, int s) { (void)v; (void)s; }
int OSCreateThread(OSThread *t, int (*f)(int,const char **), int a, void *b, void *c, int d, int e, OSThreadAttributes g)
{ (void)t; (void)f; (void)a; (void)b; (void)c; (void)d; (void)e; (void)g; return 1; }
void OSSetThreadName(OSThread *t, const char *n) { (void)t; (void)n; }
void OSResumeThread(OSThread *t) { (void)t; }

static void run(uint64_t per_chunk_us, uint64_t injected_stall_us)
{
	now_us = 0;
	end_us = 1200000;
	mix_us = per_chunk_us;
	stall_at_us = injected_stall_us ? 200000 : 0;
	stall_us = injected_stall_us;
	stalled = underrun = 0;
	mix_calls = sleep_calls = 0;
	readiness_begin_calls = readiness_chunk_calls = 0;
	minimum_lead_after_warmup = UINT32_MAX;
	written = AHEAD_LOW;
	voices[0] = &fake_voice;
	rings[0] = fake_ring[0];
	rings[1] = fake_ring[1];
	render = simulate_mix;
	suspended = 0;
	if (!setjmp(finished))
		audio_thread(0, NULL);
	assert(readiness_begin_calls == 1 && readiness_chunk_calls == mix_calls);
}

int main(void)
{
	/* The mixer fills ahead and still sleeps when healthy. */
	run(500, 0);
	assert(!underrun && mix_calls && sleep_calls);
	assert(minimum_lead_after_warmup >= 1400);

	/* An 18 ms scheduling stall used to run past the roughly 16 ms lead. */
	run(300, 18000);
	assert(stalled && !underrun);

	/* Headroom cannot cure a mixer that is slower than real time. */
	run(1500, 0);
	assert(underrun);
	puts("audio pacing: all passed");
	return 0;
}
