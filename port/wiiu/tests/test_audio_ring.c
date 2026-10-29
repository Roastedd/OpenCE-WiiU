#include "../src/wiiu_audio_ring.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
	struct wiiu_audio_ring_observations s = { 0 };

	/* Ordinary lead, including wrap and the last allowed chunk. */
	wiiu_audio_ring_observe(&s, 1500, 500, 0);
	wiiu_audio_ring_observe(&s, 200, 3900, 0);
	wiiu_audio_ring_observe(&s, 1599, 0, 0);
	assert(!s.apparent_overtakes && !s.at_deadline && !s.long_gaps);
	/* A cursor that appears behind persists for many polls: count one event. */
	wiiu_audio_ring_observe(&s, 990, 1000, 0);
	wiiu_audio_ring_observe(&s, 990, 1100, 0);
	assert(s.apparent_overtakes == 1);
	wiiu_audio_ring_observe(&s, 2000, 1000, 0);
	wiiu_audio_ring_observe(&s, 990, 1000, 0);
	assert(s.apparent_overtakes == 2);
	/* At equality we cannot tell a just-empty buffer from a full lap. */
	wiiu_audio_ring_observe(&s, 1000, 1000, 0);
	wiiu_audio_ring_observe(&s, 1000, 1000, 0);
	assert(s.at_deadline == 1);
	/* A full-lap polling gap is ambiguous even if the cursors look normal. */
	wiiu_audio_ring_observe(&s, 1500, 500, 1);
	assert(s.long_gaps == 1 && s.apparent_overtakes == 2);
	wiiu_audio_ring_observe(&s, 1000, 1000, 0);
	assert(s.at_deadline == 2);
	puts("audio ring observations: all passed");
	return 0;
}
