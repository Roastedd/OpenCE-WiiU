/*
GAME_DIGEST_PADDING.C

The game digest's poisoned build (build.py --poison) links this first, so that every other unit's data, and the heap after them, are at other
addresses than in the plain build: a pointer left in what the game hands the GPU then shows as a frame whose digest differs (game_digest_glue.c's
"what the game hands the GPU that it never set").
*/

#ifndef GAME_DIGEST_POISON
#define GAME_DIGEST_POISON 40000
#endif

/* (one in the initialized data, one in the zeroed: both sections moved) */
volatile unsigned char game_digest_padding_data[GAME_DIGEST_POISON] = { 1 };
volatile unsigned char game_digest_padding_zero[GAME_DIGEST_POISON + 4096];
