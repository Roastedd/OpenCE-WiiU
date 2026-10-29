/* Mixer diagnostics use monotonic ticks, including scheduling and lock waits.
Only the audio worker updates the accumulator. No counters change playback. */
#ifndef WIIU_MIXER_PROFILE_H
#define WIIU_MIXER_PROFILE_H

#include <stdint.h>
#include <string.h>

enum wiiu_mixer_profile_stage
{
	WIIU_MIXER_VOICES, WIIU_MIXER_REVERB, WIIU_MIXER_CHAT, WIIU_MIXER_LIMITER,
	WIIU_MIXER_STAGES
};

struct wiiu_audio_mix_pressure
{
	uint64_t lock_wait, stage[WIIU_MIXER_STAGES];
	unsigned streams, queued;
};

struct wiiu_mixer_profile
{
	uint64_t render_total, render_max, lock_total, lock_max, poll_max;
	uint64_t stage_total[WIIU_MIXER_STAGES], stage_max[WIIU_MIXER_STAGES];
	uint64_t streams_total, queued_total;
	uint32_t chunks, over_budget, polls, lead_min, streams_max, queued_max;
};

static inline void wiiu_mixer_profile_reset(struct wiiu_mixer_profile *profile)
{
	memset(profile, 0, sizeof(*profile));
	profile->lead_min = UINT32_MAX;
}

static inline void wiiu_mixer_profile_poll(struct wiiu_mixer_profile *profile,
	uint32_t lead, uint64_t gap)
{
	profile->polls++;
	if (lead < profile->lead_min)
		profile->lead_min = lead;
	if (gap > profile->poll_max)
		profile->poll_max = gap;
}

static inline void wiiu_mixer_profile_chunk(struct wiiu_mixer_profile *profile,
	uint64_t elapsed, uint64_t budget, const struct wiiu_audio_mix_pressure *pressure)
{
	unsigned i;

	profile->chunks++;
	profile->render_total += elapsed;
	profile->lock_total += pressure->lock_wait;
	profile->streams_total += pressure->streams;
	profile->queued_total += pressure->queued;
	if (elapsed > profile->render_max)
		profile->render_max = elapsed;
	if (pressure->lock_wait > profile->lock_max)
		profile->lock_max = pressure->lock_wait;
	if (elapsed > budget)
		profile->over_budget++;
	if (pressure->streams > profile->streams_max)
		profile->streams_max = pressure->streams;
	if (pressure->queued > profile->queued_max)
		profile->queued_max = pressure->queued;
	for (i = 0; i < WIIU_MIXER_STAGES; i++)
	{
		profile->stage_total[i] += pressure->stage[i];
		if (pressure->stage[i] > profile->stage_max[i])
			profile->stage_max[i] = pressure->stage[i];
	}
}

/* The dsound callback runs on the output worker. These hooks are diagnostics,
not cross-thread polling: the worker reads the pressure after render returns. */
int wiiu_audio_profile_is_enabled(void);
void wiiu_audio_profile_mix_pressure(const struct wiiu_audio_mix_pressure *pressure);

#endif
