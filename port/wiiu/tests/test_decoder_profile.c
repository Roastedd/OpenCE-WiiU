#include "../src/wiiu_decoder_profile.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
	struct wiiu_decoder_profile profile = { 0 };
	const uint64_t thresholds[3] = { 2, 10, 16 };

	/* Queue, decoding and publication time remain separate. */
	wiiu_decoder_profile_add(&profile, 100, 102, 107, 118, thresholds);
	wiiu_decoder_profile_add(&profile, 100, 116, 117, 119, thresholds);
	assert(profile.jobs == 2);
	assert(profile.total[0] == 18 && profile.maximum[0] == 16);
	assert(profile.total[1] == 6 && profile.maximum[1] == 5);
	assert(profile.total[2] == 13 && profile.maximum[2] == 11);
	assert(profile.queue_at_least[0] == 2 && profile.queue_at_least[1] == 1 && profile.queue_at_least[2] == 1);
	/* Tick rollover uses unsigned subtraction. */
	wiiu_decoder_profile_add(&profile, UINT64_MAX - 1, 1, 3, 4, thresholds);
	assert(profile.jobs == 3 && profile.total[0] == 21);
	assert(profile.queue_at_least[0] == 3);
	wiiu_decoder_profile_add(&profile, 0, 9, 9, 9, thresholds);
	wiiu_decoder_profile_add(&profile, 0, 10, 10, 10, thresholds);
	wiiu_decoder_profile_add(&profile, 0, 15, 15, 15, thresholds);
	assert(profile.queue_at_least[0] == 6 && profile.queue_at_least[1] == 3 && profile.queue_at_least[2] == 1);
	memset(&profile, 0, sizeof(profile));
	assert(profile.jobs == 0 && profile.total[0] == 0 && profile.maximum[2] == 0);
	puts("decoder profile: all passed");
	return 0;
}
