/* the pieces of port/linux/game/tick_overlap.h that port/linux/game/tick_owners.c uses, for test_tick_owners.c: the fixture defines them and sets what they
answer (the tags of the pushes, the limit of a drain, a deferred drain, the owner's state) */
#ifndef __TICK_OVERLAP_H
#define __TICK_OVERLAP_H

#include "tick_owners.h"

boolean tick_overlap_requested(void);
unsigned long tick_overlap_push_tag(void);
unsigned long tick_overlap_drain_limit(void);
boolean tick_overlap_defer_drain(struct tick_queue const *queue);
boolean tick_overlap_owner_blocked(long owner);
boolean tick_overlap_owner_is_caller(long owner);
void tick_overlap_note_push_wait(void);

#endif
