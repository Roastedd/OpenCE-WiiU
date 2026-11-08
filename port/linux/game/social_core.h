/*
SOCIAL_CORE.H

Friends, join and invite, and parties over the public MQTT brokers internet play already uses (p2p_signal.c): the engine, which touches nothing but what its
environment (struct social_env) gives it, so that the harness runs several of them in one process over a fake broker (tools/harness/tests/social_fake.h). It is
single-threaded: the p2p thread calls it; the game's threads reach it through social_front.h's queue and snapshot, never directly.

Every payload is sealed (ChaCha20-Poly1305) with keys only the people concerned hold, on topics that are hashes of those keys (hceu/3/f/...), so the brokers carry
bytes they cannot read. Nothing depends on a wall clock: freshness is a (generation, sequence) pair per sender and the receiver's monotonic clock. All integers on the
wire are big-endian, written byte by byte; nothing here is copied to the wire from memory, so the Wii U (big-endian) and every other build put the same bytes (the
harness holds both to one golden file).

Text that arrives from outside is cleaned once, here (text_safe.h) before anything keeps it: printable ASCII, never "|".
*/

#ifndef __HALO_LINUX_SOCIAL_CORE_H
#define __HALO_LINUX_SOCIAL_CORE_H

#include "social_codec.h"

enum
{
	SOCIAL_MAX_FRIENDS = 50,
	/* a friend request sent and waiting for an answer */
	SOCIAL_MAX_SENT = 16,
	/* requests received and waiting to be answered */
	SOCIAL_MAX_REQUESTS = 16,
	SOCIAL_MAX_IGNORED = 32,
	SOCIAL_MAX_BLOCKED = 64,
	/* events waiting for the game (a request, an invite, a friend coming online ...) */
	SOCIAL_MAX_EVENTS = 24,
	/* messages waiting for the bucket */
	SOCIAL_MAX_QUEUE = 8,
	/* the ids of messages seen (the same message comes through every broker) */
	SOCIAL_MAX_SEEN = 128,
	/* topics wanted at a broker: the inbox, every friend's presence, the party */
	SOCIAL_MAX_TOPICS = SOCIAL_MAX_FRIENDS + 4,
	/* invites waiting for an answer, per friend */
	SOCIAL_MAX_INVITES = 3,
	/* invites waiting, in all */
	SOCIAL_MAX_INVITES_TOTAL = 12,
	/* what is kept of a key to refuse it: the first bytes of its SHA-256 */
	SOCIAL_BLOCK_SIZE = 8,
	/* presences waiting for their signature check */
	SOCIAL_MAX_PENDING = 64,
};

/* milliseconds */
enum
{
	/* a friend not heard in this long is offline */
	SOCIAL_OFFLINE_TIME = 150000,
	SOCIAL_HEARTBEAT = 60000,
	SOCIAL_PRESENCE_INTERVAL = 3000,
	SOCIAL_PRESENCE_GAME_INTERVAL = 15000,
	SOCIAL_ANSWER_MINIMUM = 300,
	SOCIAL_ANSWER_MAXIMUM = 2500,
	SOCIAL_ANSWER_REPEAT = 10000,
	/* the clock's anchor is waited for this long before generation is chosen */
	SOCIAL_ANCHOR_WAIT = 15000,
	/* the bucket of publishes (all brokers): a burst, and one more each interval */
	SOCIAL_SEND_BURST = 6,
	SOCIAL_SEND_INTERVAL = 400,
	SOCIAL_QUEUE_LIFE = 2000,
	/* what is received: messages opened at most this many a second; signature checks, a burst and one more each interval; a friend's messages a second (opening a message is cheap, a signature is not) */
	SOCIAL_OPEN_RATE = 200,
	SOCIAL_VERIFY_BURST = 8,
	SOCIAL_VERIFY_INTERVAL = 50,
	SOCIAL_FRIEND_RATE = 20,
	SOCIAL_REQUEST_NOTICE = 600000,
	SOCIAL_PENDING_LIFE = 10000,
	/* a request sent again; hello sent again; an add to a new code; the least between two sent to one */
	SOCIAL_INVITE_LIFE = 60000,
	SOCIAL_INVITE_SPACING = 10000,
	SOCIAL_REQUEST_REPEAT = 300000,
	SOCIAL_HELLO_FIRST = 20000,
	SOCIAL_HELLO_LATER = 300000,
	SOCIAL_HELLO_TRIES = 10,
	SOCIAL_HELLO_SPACING = 1000,
	SOCIAL_ADD_WINDOW = 60000,
	SOCIAL_ADD_PER_WINDOW = 4,
	SOCIAL_REPLY_SPACING = 5000,
	SOCIAL_ROTATION_LIFE = 120000,
	SOCIAL_STORE_DEBOUNCE = 2000,
	SOCIAL_STORE_INTERVAL = 30000,
	SOCIAL_STORE_SOFT_INTERVAL = 300000,
};

/* where a person is, as presence says (and as the game tells social_set_place) */
enum
{
	SOCIAL_OFFLINE,
	SOCIAL_MENUS,
	SOCIAL_LOBBY,
	SOCIAL_GAME,
	/* a campaign or solo game: never joinable */
	SOCIAL_SOLO,
	SOCIAL_BUSY,
};

enum
{
	SOCIAL_PLATFORM_UNKNOWN,
	SOCIAL_PLATFORM_WIIU,
	SOCIAL_PLATFORM_WINDOWS,
	SOCIAL_PLATFORM_LINUX,
	SOCIAL_PLATFORM_MACOS,
	SOCIAL_PLATFORM_ANDROID,
	SOCIAL_PLATFORM_WEB,
};

/* social.share_details */
enum
{
	SOCIAL_DETAILS_NONE,
	SOCIAL_DETAILS_BASIC,
	SOCIAL_DETAILS_FULL,
};

/* what the machine is doing and could be joined at (the game tells; social_set_place) */
struct social_place
{
	unsigned char activity;
	/* this machine hosts the game, and friends may join it (an internet game with an invite) */
	unsigned char hosting;
	unsigned char joinable;
	/* a password, or a private game */
	unsigned char private_game;
	char map[SOCIAL_MAP_SIZE + 1];
	char gametype[SOCIAL_GAMETYPE_SIZE + 1];
	unsigned char players, maximum_players, engine_type;
	/* the invite's two halves (halo://join/<host key hash><token>), when the game has one: it is shown to friends if joinable, and sent to one by an invite */
	unsigned char has_invite;
	unsigned char invite_hash[SOCIAL_HASH_SIZE];
	unsigned char invite_token[SOCIAL_TOKEN_SIZE];
};

/* the machine's way of reaching the world; every call is made by the thread that calls the engine */
struct social_env
{
	void *user;
	/* the monotonic clock, milliseconds (wraps at 2^32: only differences are used) */
	unsigned int (*now)(void *user);
	/* Unix seconds in UTC, and whether the clock is anchored to a server's word (halo_utc_now); 0 seconds says there is no usable time */
	unsigned long long (*utc)(void *user, int *anchored);
	void (*random)(void *user, unsigned char *bytes, int size);
	/* one message on a topic, to every broker that is ready; 0 if none took it */
	int (*publish)(void *user, const char *topic, const unsigned char *payload, int size);
	/* the topics wanted now (the whole set; the transport diffs it) */
	void (*subscribe)(void *user, const char (*topics)[SOCIAL_TOPIC_SIZE], int count);
	/* how many brokers are ready: nothing is sent while there is none */
	int (*ready)(void *user);
	/* a line for the log (no "social: " prefix, no newline) */
	void (*log)(void *user, const char *line);
	int platform;
	/* HALO_PORT_NETWORK_VERSION */
	int network_version;
};

/* what the engine counts, for the log and the tests */
struct social_statistics
{
	unsigned int received, accepted, opened, attempted, verifications;
	unsigned int dropped_size, dropped_kind, dropped_topic, dropped_rate, dropped_seen, dropped_open, dropped_header, dropped_window, dropped_signature,
		dropped_verify_budget, dropped_unknown, dropped_blocked, dropped_malformed;
	unsigned int sent, queued, send_dropped, send_expired, deferred;
};

/* the presence a friend last told */
struct social_presence
{
	unsigned char circle_generation;
	unsigned char flags;
	unsigned char activity;
	unsigned char platform;
	unsigned short network_version;
	char name[SOCIAL_NAME_SIZE + 1];
	char map[SOCIAL_MAP_SIZE + 1];
	char gametype[SOCIAL_GAMETYPE_SIZE + 1];
	unsigned char players, maximum_players, engine_type;
	unsigned char invite_hash[SOCIAL_HASH_SIZE];
	unsigned char invite_token[SOCIAL_TOKEN_SIZE];
	unsigned char party_token[SOCIAL_TOKEN_SIZE];
};

struct social_friend
{
	int used;
	/* the first four bytes of the key's SHA-256, never 0: what the game's side names a friend by */
	unsigned int id;
	unsigned char key[SOCIAL_KEY_SIZE];
	unsigned char hash[SOCIAL_HASH_SIZE_FULL];
	unsigned char code_bytes[SOCIAL_CODE_BYTES];
	char code[SOCIAL_CODE_TEXT_SIZE];
	/* the key the two share, and the tags that say who a direct message is from and for (worked out when first needed) */
	int pair_ready;
	unsigned char pair_key[SOCIAL_KEY_SIZE];
	unsigned char tag_in[SOCIAL_TAG_SIZE];
	unsigned char tag_out[SOCIAL_TAG_SIZE];
	/* the friend's circle: the key it seals its presence with, and the topic it publishes it on */
	int has_circle;
	unsigned char circle_generation;
	unsigned char circle_key[SOCIAL_KEY_SIZE];
	unsigned char circle_seal[SOCIAL_KEY_SIZE];
	char topic[SOCIAL_TOPIC_SIZE];
	/* the friend holds this machine's circle key of the generation held */
	int acknowledged;
	/* hello (and the acceptance before it) sent again until acknowledged */
	unsigned int hello_next;
	int hello_count;
	int accept_sent;
	unsigned int reply_time;
	int reply_valid;
	struct social_window window;
	char nickname[SOCIAL_NAME_SIZE + 1];
	unsigned long long added;
	/* presence */
	int heard;
	unsigned int heard_time;
	struct social_presence presence;
	/* messages from it in the current second */
	unsigned int rate_start;
	int rate_count;
	unsigned int invite_time;
	int invite_valid;
};

enum
{
	SOCIAL_EVENT_NONE,
	/* a friend request arrived (index: its id; name and code) */
	SOCIAL_EVENT_REQUEST,
	/* a friendship began (request accepted, or ours was) */
	SOCIAL_EVENT_FRIEND,
	/* a friend was removed by the other side */
	SOCIAL_EVENT_REMOVED,
	SOCIAL_EVENT_ONLINE,
	SOCIAL_EVENT_OFFLINE,
	/* an invite to a game (target: the join target) */
	SOCIAL_EVENT_INVITE,
	/* an invite to a party */
	SOCIAL_EVENT_PARTY_INVITE,
	SOCIAL_EVENT_PARTY_CHAT,
	SOCIAL_EVENT_PARTY_JOINED,
	SOCIAL_EVENT_PARTY_LEFT,
	SOCIAL_EVENT_PARTY_KICKED,
	SOCIAL_EVENT_PARTY_LEADER,
};

struct social_event
{
	int type;
	unsigned int id;
	char name[SOCIAL_NAME_SIZE + 1];
	char code[SOCIAL_CODE_TEXT_SIZE];
	/* chat text */
	char text[SOCIAL_CHAT_SIZE + 1];
	/* an invite to a game */
	struct social_join_target target;
	unsigned char party_token[SOCIAL_TOKEN_SIZE];
	unsigned char party_size;
};


struct social_queued
{
	unsigned int time;
	int size;
	char topic[SOCIAL_TOPIC_SIZE];
	unsigned char payload[SOCIAL_MAX_WIRE];
};

/* a request this machine sent, waiting for the other to answer */
struct social_sent
{
	int used;
	unsigned int id;
	unsigned char code_bytes[SOCIAL_CODE_BYTES];
	char code[SOCIAL_CODE_TEXT_SIZE];
	unsigned int sent_time;
	int sent_valid;
	unsigned long long added;
};

/* a request received, waiting for an answer */
struct social_request
{
	int used;
	unsigned int id;
	unsigned char key[SOCIAL_KEY_SIZE];
	char name[SOCIAL_NAME_SIZE + 1];
	char code[SOCIAL_CODE_TEXT_SIZE];
	unsigned int received;
	unsigned long long utc;
};

/* an invite received, waiting to be accepted or to lapse */
struct social_pending_invite
{
	int used;
	unsigned int id;
	unsigned int friend_id;
	int kind;
	struct social_join_target target;
	unsigned char party_token[SOCIAL_TOKEN_SIZE];
	unsigned char party_size;
	unsigned int received;
};

/* a presence opened, awaiting the budget for its signature check */
struct social_pending
{
	unsigned int friend_id;
	unsigned int time;
	int size;
	struct social_inner inner;
	unsigned char plain[SOCIAL_MAX_PLAIN];
};

struct social_engine
{
	struct social_env env;
	struct social_identity identity;
	int has_identity;
	int enabled;
	/* the privacy settings (social_set_share) */
	int share_status;
	int details;
	int accept_requests;
	char name[SOCIAL_NAME_SIZE + 1];
	/* this boot's generation (chosen once the clock is anchored, or after SOCIAL_ANCHOR_WAIT) and the last sequence sent */
	unsigned int stored_generation;
	unsigned int generation;
	unsigned int sequence;
	int generation_ready;
	unsigned int start_time;
	/* where requests to this machine come, and the key under which they are sealed */
	char inbox_topic[SOCIAL_TOPIC_SIZE];
	unsigned char inbox_seal[SOCIAL_KEY_SIZE];
	/* this machine's circle: its key (friends hold it), generation, and where presence is published */
	unsigned char circle_key[SOCIAL_KEY_SIZE];
	unsigned char circle_seal[SOCIAL_KEY_SIZE];
	unsigned char circle_generation;
	char circle_topic[SOCIAL_TOPIC_SIZE];
	struct social_friend friends[SOCIAL_MAX_FRIENDS];
	/* sending: a bucket and a small queue */
	struct social_queued queue[SOCIAL_MAX_QUEUE];
	int queue_first, queue_count;
	int send_tokens;
	unsigned int send_time;
	/* receiving: the messages seen lately (a message comes through every broker), and the budgets */
	unsigned char seen[SOCIAL_MAX_SEEN][8];
	int seen_count, seen_next;
	unsigned int open_start;
	int open_count;
	int verify_tokens;
	unsigned int verify_time;
	/* presences opened and waiting for a signature check (the budget) */
	struct social_pending pending[SOCIAL_MAX_PENDING];
	int pending_first, pending_count;
	/* invites received, and the last one sent: a count, and how it ended (SOCIAL_INVITE_*) */
	struct social_pending_invite invites[SOCIAL_MAX_INVITES_TOTAL];
	unsigned int invite_serial_in;
	unsigned int invite_serial;
	int invite_result;
	/* requests: sent (waiting for an answer), received, and the keys (the first 8 bytes of their hashes) refused for good or for now */
	struct social_sent sent[SOCIAL_MAX_SENT];
	struct social_request requests[SOCIAL_MAX_REQUESTS];
	unsigned char ignored[SOCIAL_MAX_IGNORED][SOCIAL_BLOCK_SIZE];
	int ignored_count, ignored_next;
	unsigned char blocked[SOCIAL_MAX_BLOCKED][SOCIAL_BLOCK_SIZE];
	int blocked_count;
	/* the times of the last new codes this machine sent a request to */
	unsigned int add_times[4];
	int add_times_count;
	/* a hello a second at most, and the circle before the last rotation (friends still listen to it for the hello that gives them the new key) */
	unsigned int hello_last;
	int hello_valid;
	char old_circle_topic[SOCIAL_TOPIC_SIZE];
	unsigned int rotated_time;
	int rotated;
	/* the store: changed since the last save (hard: a friend, a list, the circle or a setting; soft: only the windows and sequence), when, and the last save */
	int store_dirty, store_soft;
	unsigned int store_dirty_time, store_soft_time, store_saved_time;
	int store_saved;
	/* the file was written by a newer version and is left alone */
	int store_locked;
	/* the circle generation a file named (read before its key) */
	int store_circle_generation;
	/* the last request by code: a count, and how it ended (SOCIAL_ADD_*) */
	unsigned int add_serial;
	int add_result;
	/* the topics asked of the brokers */
	char wanted[SOCIAL_MAX_TOPICS][SOCIAL_TOPIC_SIZE];
	int wanted_count;
	int topics_dirty;
	/* presence: what was last sent, and when */
	struct social_place place;
	struct social_presence sent_presence;
	int presence_sent;
	int presence_offline_sent;
	unsigned int presence_time;
	int presence_wanted;
	int ready_before;
	int answer_pending;
	unsigned int answer_due;
	int answer_valid;
	unsigned int answer_last;
	struct social_event events[SOCIAL_MAX_EVENTS];
	int event_first, event_count;
	struct social_statistics statistics;
	/* changed since the snapshot was made / the store was written */
	unsigned int version;
	/* the generation was chosen: the store writes it at once */
	int generation_changed;
	/* a friend's pair key was worked out in this pass */
	int pair_worked;
	/* the game is quitting: nothing more is done */
	int shut_down;
};

/* ---------- the engine (social_core.c) */

/* the engine, set up with no identity: social_set_identity next */
void social_engine_init(struct social_engine *engine, const struct social_env *env);
/* the identity: the Ed25519 seed (kept by the store), the name shown to friends (cleaned here), and the generation stored last time. A circle key is made */
void social_set_identity(struct social_engine *engine, const unsigned char *seed, const char *name, unsigned int stored_generation);
/* a circle (the store's) in place of the one made */
void social_set_circle(struct social_engine *engine, int generation, const unsigned char *key);
/* off: nothing is subscribed, received or sent */
void social_set_enabled(struct social_engine *engine, int enabled);
/* the privacy settings: whether presence is shown to friends at all (appear offline), what detail it carries (SOCIAL_DETAILS_*), and whether requests are accepted */
void social_engine_set_share(struct social_engine *engine, int share_status, int details, int accept_requests);
void social_engine_set_name(struct social_engine *engine, const char *name);
/* what the machine is doing (cheap to call again with the same) */
void social_engine_set_place(struct social_engine *engine, const struct social_place *place);
/* the passes of the p2p thread: timers, resends, publishes */
void social_update(struct social_engine *engine);
/* a message on a topic, from a broker */
void social_heard(struct social_engine *engine, const char *topic, const unsigned char *payload, int size);
/* the game is quitting: offline said at once */
void social_shutdown(struct social_engine *engine);
const char *social_code(const struct social_engine *engine);
const struct social_statistics *social_statistics(const struct social_engine *engine);
const struct social_friend *social_friend_by_id(const struct social_engine *engine, unsigned int id);
int social_friend_count(const struct social_engine *engine);
/* whether the friend is online now (heard from within SOCIAL_OFFLINE_TIME) */
int social_friend_online(const struct social_engine *engine, const struct social_friend *friend);
/* a friend whose circle key is known (a request accepted, the store's list): returns it, or NULL if the list is full, the key is this machine's own or unusable.
acknowledged: the friend holds this machine's circle key already */
struct social_friend *social_friend_add(struct social_engine *engine, const unsigned char *key, int circle_generation, const unsigned char *circle_key,
	int acknowledged);
/* the oldest event, removed; 0 if none */
int social_event_take(struct social_engine *engine, struct social_event *event);
/* the log's line, with "social: " in front by the environment's log */
void social_log(struct social_engine *engine, const char *format, ...);

/* ---------- friends (social_friends.c) */

/* the results of adding a friend by a typed code (in the order of social.h's SOCIAL_ADD_RESULT_*) */
enum
{
	SOCIAL_ADD_OK,
	SOCIAL_ADD_BAD_TEXT,
	SOCIAL_ADD_BAD_CHECK,
	SOCIAL_ADD_OWN,
	SOCIAL_ADD_ALREADY,
	SOCIAL_ADD_FULL,
	SOCIAL_ADD_PENDING_FULL,
	SOCIAL_ADD_TOO_FAST,
	SOCIAL_ADD_OFF,
	SOCIAL_ADD_BUSY,
};

/* sends a request to the owner of a typed code; one of SOCIAL_ADD_*. A request is sent again every five minutes while it waits */
int social_engine_add_code(struct social_engine *engine, const char *text);
/* a request received (by its id): becomes a friend, and the other is told. 0 if there is no such request or the list is full */
int social_engine_accept(struct social_engine *engine, unsigned int id);
/* ... is turned away, and asked about no more (until the list of those turned away forgets it) */
int social_engine_ignore(struct social_engine *engine, unsigned int id);
/* a request sent (by its id) is forgotten */
int social_engine_cancel(struct social_engine *engine, unsigned int id);
/* the friend is told it is not one, deleted, and this machine's circle key is changed so that it hears nothing more */
int social_engine_remove(struct social_engine *engine, unsigned int id);
/* ... and its key refused for good */
int social_engine_block(struct social_engine *engine, unsigned int id);
/* one of the blocked (by its place in the list) can ask again */
int social_engine_unblock(struct social_engine *engine, int index);
int social_engine_set_nickname(struct social_engine *engine, unsigned int id, const char *nickname);

/* ---------- invites (social_invite.c) */

/* the results of inviting a friend to the game this machine hosts (in the order of social.h's SOCIAL_INVITE_RESULT_*) */
enum
{
	SOCIAL_INVITE_OK,
	SOCIAL_INVITE_NO_FRIEND,
	SOCIAL_INVITE_OFFLINE,
	/* this machine hosts no game a friend could join */
	SOCIAL_INVITE_NOTHING,
	SOCIAL_INVITE_TOO_FAST,
	SOCIAL_INVITE_BUSY,
	SOCIAL_INVITE_OFF,
};

/* the friend (by id) is asked to join the game this machine hosts */
int social_engine_invite(struct social_engine *engine, unsigned int friend_id);
/* an invite received (by its id) is let go */
int social_engine_invite_dismiss(struct social_engine *engine, unsigned int id);
void social_invite_heard(struct social_engine *engine, struct social_friend *friend, const unsigned char *plain, int size, const struct social_inner *inner);
void social_invites_update(struct social_engine *engine);

/* ---------- the file (social_store.c) */

/* whether a save is wanted now: something changed, and the debounce and the interval are over; foreground says the game has the screen (the card is not
written while it is away) */
int social_store_due(struct social_engine *engine, int foreground);
/* the text of the file, and the change marked as saved; the length, or 0 if the engine is locked out of its file */
int social_store_take(struct social_engine *engine, char *text, int capacity);

/* ---------- for the engine's own files */

unsigned int social_now(struct social_engine *engine);
int social_elapsed(struct social_engine *engine, unsigned int since, unsigned int time);
void social_event_push(struct social_engine *engine, const struct social_event *event);
/* a message sealed and queued for the topic; 0 if the queue was full or it did not fit */
int social_send(struct social_engine *engine, const char *topic, int kind, const unsigned char *key, const unsigned char *tag, const unsigned char *plain,
	int plain_size);
/* the next sequence number */
unsigned int social_next_sequence(struct social_engine *engine);
struct social_friend *social_friend_by_key(struct social_engine *engine, const unsigned char *key);
/* works out the pair key and tags if not yet; 0 if the friend's key is unusable */
int social_friend_pair(struct social_engine *engine, struct social_friend *friend);
/* topics wanted changed */
void social_topics_changed(struct social_engine *engine);
/* a signature check may be made now (the budget is spent) */
int social_verify_budget(struct social_engine *engine);

/* social_friends.c: requests, answers, hello, remove; direct messages */
void social_friends_update(struct social_engine *engine);
int social_requests_pending(const struct social_engine *engine);
/* which friend a direct message is from, and the key it is sealed with; NULL if none. friend is the owner of the topic it came on (NULL on the inbox) */
struct social_friend *social_direct_sender(struct social_engine *engine, struct social_friend *friend, const unsigned char *tag, const unsigned char **key);
void social_request_heard(struct social_engine *engine, const unsigned char *plain, int size, const struct social_inner *inner);
void social_direct_heard(struct social_engine *engine, struct social_friend *friend, int on_inbox, const unsigned char *plain, int size,
	const struct social_inner *inner);

/* social_party.c */
void social_party_update(struct social_engine *engine);
void social_party_shutdown(struct social_engine *engine);
/* whether the topic is the party's, the key its messages are sealed with, and the topic to listen to (0: none) */
int social_party_topic(const struct social_engine *engine, const char *topic);
const unsigned char *social_party_key(const struct social_engine *engine, const char *topic);
int social_party_topic_wanted(const struct social_engine *engine, char *topic);
void social_party_heard(struct social_engine *engine, const char *topic, const unsigned char *plain, int size, const struct social_inner *inner);
/* the party's part of this machine's presence (its token, if it lets friends in) */
void social_presence_extra(struct social_engine *engine, struct social_presence *presence);

#endif
