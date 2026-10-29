#include "../src/wiiu_mixer_profile.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
	struct wiiu_mixer_profile p;
	struct wiiu_audio_mix_pressure pressure = { 0 };

	wiiu_mixer_profile_reset(&p);
	assert(!p.chunks && !p.polls && p.lead_min == UINT32_MAX);
	wiiu_mixer_profile_poll(&p, 1500, 30);
	wiiu_mixer_profile_poll(&p, 720, 200);
	wiiu_mixer_profile_poll(&p, 4090, 80);
	assert(p.polls == 3 && p.lead_min == 720 && p.poll_max == 200);
	pressure.lock_wait = 10; pressure.streams = 8; pressure.queued = 2;
	pressure.stage[WIIU_MIXER_VOICES] = 50; pressure.stage[WIIU_MIXER_REVERB] = 20;
	pressure.stage[WIIU_MIXER_CHAT] = 2; pressure.stage[WIIU_MIXER_LIMITER] = 5;
	wiiu_mixer_profile_chunk(&p, 80, 100, &pressure);
	pressure.lock_wait = 20; pressure.streams = 9; pressure.queued = 4;
	pressure.stage[WIIU_MIXER_REVERB] = 30;
	wiiu_mixer_profile_chunk(&p, 100, 100, &pressure);
	pressure.lock_wait = 150; pressure.streams = 7; pressure.queued = 3;
	pressure.stage[WIIU_MIXER_VOICES] = 175; pressure.stage[WIIU_MIXER_REVERB] = 15;
	wiiu_mixer_profile_chunk(&p, 200, 100, &pressure);
	assert(p.chunks == 3 && p.over_budget == 1);
	assert(p.render_total == 380 && p.render_max == 200);
	assert(p.lock_total == 180 && p.lock_max == 150);
	assert(p.streams_total == 24 && p.streams_max == 9);
	assert(p.queued_total == 9 && p.queued_max == 4);
	assert(p.stage_total[WIIU_MIXER_VOICES] == 275 && p.stage_max[WIIU_MIXER_VOICES] == 175);
	assert(p.stage_total[WIIU_MIXER_REVERB] == 65 && p.stage_max[WIIU_MIXER_REVERB] == 30);
	assert(p.stage_total[WIIU_MIXER_CHAT] == 6 && p.stage_max[WIIU_MIXER_CHAT] == 2);
	assert(p.stage_total[WIIU_MIXER_LIMITER] == 15 && p.stage_max[WIIU_MIXER_LIMITER] == 5);
	/* Real timer deltas must stay 64-bit on the console's 32-bit ABI. */
	pressure.lock_wait = UINT64_C(0x100000000);
	wiiu_mixer_profile_chunk(&p, UINT64_C(0x100000001), 100, &pressure);
	assert(p.render_max == UINT64_C(0x100000001));
	assert(p.lock_max == UINT64_C(0x100000000));
	assert(p.over_budget == 2);
	wiiu_mixer_profile_reset(&p);
	assert(!p.chunks && !p.render_total && !p.render_max && !p.lock_total && !p.lock_max);
	assert(!p.over_budget && !p.polls && !p.poll_max && p.lead_min == UINT32_MAX);
	assert(!p.streams_total && !p.queued_total && !p.streams_max && !p.queued_max);
	assert(!p.stage_total[WIIU_MIXER_VOICES] && !p.stage_max[WIIU_MIXER_REVERB]);
	puts("mixer profile: all passed");
	return 0;
}
