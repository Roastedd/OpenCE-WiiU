/*
SOCIAL_FRONT.H

The friends layer as the game's threads see it (social.h): a queue of what they ask for, a snapshot of what they may read, and a thread that writes the file. The
engine (social_core.h) is touched by one thread only, the one that calls social_front_tick and social_front_heard (the p2p thread); the game's threads take a lock
for as long as it takes to copy a few bytes, and never wait for a pass of the engine, the network or the card.

The harness makes one of these for each simulated machine; social_p2p.c makes the real one.
*/

#ifndef __HALO_LINUX_SOCIAL_FRONT_H
#define __HALO_LINUX_SOCIAL_FRONT_H

#include "social.h"
#include "social_store.h"

struct social_front;

/* the one friends layer of the process (social.c), set by social_p2p.inc before any of social.h's calls can reach it; NULL while it is off */
extern struct social_front *social_global_front;
/* what the game last said it is doing (social_set_activity), 0 if nothing yet */
int social_activity_get(void);

struct social_front_options
{
	/* whether the game has the screen (the card is written only then); NULL: always */
	int (*foreground)(void *user);
	void *user;
	/* the saver is a thread of its own (the game's); off: social_front_save_now does it, for tests */
	int saver_thread;
};

/* The engine lives in the front. env is copied. files may be NULL (nothing saved). */
struct social_front *social_front_create(const struct social_env *env, const struct social_files *files, const struct social_front_options *options);
/* stops the saver after it writes what is waiting */
void social_front_destroy(struct social_front *front);
struct social_engine *social_front_engine(struct social_front *front);

/* the engine's thread: commands taken, the engine's pass, the snapshot, the save handed to the saver */
void social_front_tick(struct social_front *front);
void social_front_heard(struct social_front *front, const char *topic, const unsigned char *payload, int size);

/* the identity: read from the files (friends.key, made if it is not there), the list read; the name; call once before the first tick, from any thread that is
allowed to wait for the card. Returns 0 if ready, 1 if friends.key could not be written (the identity then lasts this run only). */
int social_front_start(struct social_front *front, const char *name, const unsigned char *fresh_seed);
/* the identity's seed is made by the caller (random bytes) and written to friends.key if the file is not there; files must be set */

/* what the game's threads call (social.h's functions are these on the real front) */
int social_front_add_code(struct social_front *front, const char *text);
void social_front_accept(struct social_front *front, unsigned int id);
void social_front_ignore(struct social_front *front, unsigned int id);
void social_front_cancel(struct social_front *front, unsigned int id);
void social_front_remove(struct social_front *front, unsigned int id);
void social_front_block(struct social_front *front, unsigned int id);
void social_front_unblock(struct social_front *front, int index);
void social_front_set_nickname(struct social_front *front, unsigned int id, const char *nickname);
void social_front_set_name(struct social_front *front, const char *name);
void social_front_set_share(struct social_front *front, int show_status, int details, int accept_requests);
void social_front_invite(struct social_front *front, unsigned int friend_id);
void social_front_invite_dismiss(struct social_front *front, unsigned int invite_id);
/* a party call: what is 1 make, 2 invite (id a friend's), 4 join a friend's open party, 5 leave, 6 kick (a member's), 7 transfer (a member's), 8 open (value), 9 auto follow
(value), 10 chat (text). The result is in the snapshot */
void social_front_party(struct social_front *front, int what, unsigned int id, int value, const char *text);
void social_front_party_token(struct social_front *front, const unsigned char *token);
/* whether the game thread is to join the party leader's game now (cheap: no snapshot copied), the target and its order; and what became of it */
int social_front_follow(struct social_front *front, struct social_join_target *target, unsigned int *order);
void social_front_follow_report(struct social_front *front, unsigned int order, int result);
void social_front_set_share_part(struct social_front *front, int which, int value);
void social_front_set_enabled(struct social_front *front, int enabled);
void social_front_set_place(struct social_front *front, const struct social_place *place);
/* whether it is on, and this machine's code: a few bytes under the lock, without copying the snapshot */
int social_front_enabled(struct social_front *front);
void social_front_code(struct social_front *front, char *code, int size);
unsigned int social_front_snapshot(struct social_front *front, struct social_snapshot *snapshot);
int social_front_event(struct social_front *front, struct social_api_event *event);

/* the save, now, on this thread (for tests and for the game quitting); 0 if saved or nothing was due, else social_store_save's failure */
int social_front_save_now(struct social_front *front);
/* the saver's counts, for the log and the tests */
void social_front_saver(struct social_front *front, unsigned int *saves, unsigned int *failures);

#endif
