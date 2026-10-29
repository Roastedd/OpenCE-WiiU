/* The ring's cursor observations. A modulo cursor alone cannot prove how
many full laps were played between observations. */
#ifndef WIIU_AUDIO_RING_H
#define WIIU_AUDIO_RING_H

#include <stdint.h>

#define WIIU_AUDIO_RING_FRAMES 4096u
#define WIIU_AUDIO_CHUNK_FRAMES 64u
#define WIIU_AUDIO_AHEAD_HIGH 1536u

struct wiiu_audio_ring_observations
{
	uint32_t apparent_overtakes;
	uint32_t at_deadline;
	uint32_t long_gaps;
	int outside_previous;
	int deadline_previous;
};

/* Count transitions, not mixer iterations. A long gap makes the number of
laps ambiguous, so report it separately and do not classify its cursors. */
static inline void wiiu_audio_ring_observe(struct wiiu_audio_ring_observations *state,
	uint32_t write, uint32_t play, int long_gap)
{
	uint32_t ahead = (write + WIIU_AUDIO_RING_FRAMES - play) % WIIU_AUDIO_RING_FRAMES;
	int outside, deadline;

	if (long_gap)
	{
		state->long_gaps++;
		state->outside_previous = 0;
		state->deadline_previous = 0;
		return;
	}
	/* The writer only starts a chunk below AHEAD_HIGH. A larger apparent
	lead suggests the player passed it, but is not proof of an underrun. */
	outside = ahead >= WIIU_AUDIO_AHEAD_HIGH + WIIU_AUDIO_CHUNK_FRAMES;
	deadline = ahead == 0;
	if (outside && !state->outside_previous)
		state->apparent_overtakes++;
	if (deadline && !state->deadline_previous)
		state->at_deadline++;
	state->outside_previous = outside;
	state->deadline_previous = deadline;
}

#endif
