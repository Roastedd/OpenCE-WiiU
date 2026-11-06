/*
WIIU_ENTROPY.H

Random bytes for the Wii U build: Mbed TLS's entropy source (wiiu_mbedtls.c) and the port's posix_random_bytes (the nonces, the peer-to-peer keys
and the identity key of network.key, wiiu_posix_net.c).

Be clear about what this is: the Espresso has no hardware random number generator that this port can reach (Cafe OS has a random number service
behind IOSU, which nothing here calls), so the bytes are made of what a program can see of itself: several reads of the 62 MHz time base, taken
either side of a yield to the other threads (the scheduler's jitter), the system clock, the addresses of a stack variable, a heap block and the
pool, which thread is asking, and a seed file kept on the SD card (halo/entropy.seed). Everything is mixed into a pool with BLAKE2b (Monocypher),
and every output is a keyed hash of the pool, after which the pool is ratcheted, so that earlier outputs cannot be had from a later state. The
seed file is read at the first use, mixed in, and written again at once with a new value derived from the pool, so that two starts never begin
from the same file. It makes a start that has little jitter (all threads still, a fixed boot) no worse than the start before it: it adds nothing
secret to the very first start on a card, whose only unpredictability is the clocks', and anyone who reads the SD card reads the seed. A key that
must hold against an attacker who can guess the console's time to the millisecond should not come from here alone.

The pool (wiiu_entropy_init, _mix, _extract, _seed) is plain C over Monocypher and is tested against a model written from its description
(tools/test_wiiu_https.py); the part that gathers (wiiu_entropy_collect, the global instance) takes its clocks from hooks, which the tests replace.
*/

#ifndef HALO_WIIU_ENTROPY_H
#define HALO_WIIU_ENTROPY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
	WIIU_ENTROPY_STATE_SIZE = 64,
	WIIU_ENTROPY_SEED_SIZE = 64,
	/* the times the clock is read either side of a yield when the pool is gathered */
	WIIU_ENTROPY_ROUNDS = 48,
};

typedef struct wiiu_entropy
{
	uint8_t state[WIIU_ENTROPY_STATE_SIZE];
	/* counts every mix, extract and ratchet: it is in each hash */
	uint64_t counter;
} wiiu_entropy;

/* The state at the start: BLAKE2b-512 of a fixed text. */
void wiiu_entropy_init(wiiu_entropy *pool);
/* state = BLAKE2b-512("mix", state, counter, size, data); counter + 1. */
void wiiu_entropy_mix(wiiu_entropy *pool, const void *data, size_t size);
/* size bytes, each 32 of them BLAKE2b-256 keyed by the state of ("out", counter, block); then the state is ratcheted:
state = BLAKE2b-512 keyed by the state of ("ratchet", counter), and counter + 1. */
void wiiu_entropy_extract(wiiu_entropy *pool, void *output, size_t size);
/* a seed for the next start: BLAKE2b-512 keyed by the state of ("seed", counter); then ratcheted as after extract. */
void wiiu_entropy_seed(wiiu_entropy *pool, uint8_t seed[WIIU_ENTROPY_SEED_SIZE]);

/* What the gathering reads of the machine (the Wii U's by default, a desktop's on a desktop; the tests' own). A NULL hook is not used. */
typedef struct wiiu_entropy_hooks
{
	/* a fast counter that does not stop (the time base) */
	uint64_t (*ticks)(void);
	/* the system clock (the console's own: OSGetTime) */
	uint64_t (*wall)(void);
	/* gives the processor to other threads for a moment */
	void (*yield)(void);
	/* something of the calling thread: its address, its core */
	uint32_t (*thread)(void);
	/* nonzero while the card may be read and written (the app in the foreground); NULL: always */
	int (*card_may_be_used)(void);
	/* one line of what happened with the seed file */
	void (*log)(const char *text);
} wiiu_entropy_hooks;

/* Mixes the clocks, a jitter of WIIU_ENTROPY_ROUNDS yields, the thread and the addresses of a stack variable, a heap block and the pool. */
void wiiu_entropy_collect(wiiu_entropy *pool, const wiiu_entropy_hooks *hooks);

/* The port's one pool, made at the first call (its seed file read and written, its clocks gathered), under a lock; any thread. */
void wiiu_entropy_bytes(void *output, size_t size);
/* A new seed file now (the TLS stack asks for entropy rarely; the nonces' callers are not made to wait for the card). */
void wiiu_entropy_save_seed(void);

/* For the tests: the hooks and the seed file's path (NULL: none) used from now on, and the pool made again at the next call. */
void wiiu_entropy_configure(const wiiu_entropy_hooks *hooks, const char *seed_path);

#ifdef __cplusplus
}
#endif

#endif
