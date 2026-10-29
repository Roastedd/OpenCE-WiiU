/* Decoder timings use monotonic ticks. Only the decoder thread updates this
   accumulator; elapsed values include scheduling and lock waits. */
#ifndef WIIU_DECODER_PROFILE_H
#define WIIU_DECODER_PROFILE_H

#include <stdint.h>

struct wiiu_decoder_profile
{
	uint64_t total[3], maximum[3]; /* queue, decode, publish */
	uint32_t jobs, queue_at_least[3]; /* 2, 10 and 16 ms */
};

static inline void wiiu_decoder_profile_add(struct wiiu_decoder_profile *profile,
	uint64_t posted, uint64_t started, uint64_t decoded, uint64_t published,
	const uint64_t thresholds[3])
{
	const uint64_t durations[3] = { started - posted, decoded - started, published - decoded };
	unsigned i;

	for (i = 0; i < 3; i++)
	{
		profile->total[i] += durations[i];
		if (durations[i] > profile->maximum[i])
			profile->maximum[i] = durations[i];
		if (durations[0] >= thresholds[i])
			profile->queue_at_least[i]++;
	}
	profile->jobs++;
}

#endif
