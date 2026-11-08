/*
SOCIAL_CORE.C

The engine of the friends layer (social_core.h): identity, the sealed messages' passage in and out, the budgets that keep a flood from doing work, and presence.
Friends' requests and answers are social_friends.c, joining and inviting social_invite.c, parties social_party.c, and the file social_store.c.
*/

#include "p2p_internal.h"
#include "social_core.h"
#include "../game/text_safe.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* 2026-01-01 in Unix seconds: the generation counts minutes from here */
#define GENERATION_EPOCH 1767225600ULL

unsigned int social_now(struct social_engine *engine)
{
	return engine->env.now(engine->env.user);
}

int social_elapsed(struct social_engine *engine, unsigned int since, unsigned int time)
{
	/* (unsigned, as the clock wraps) */
	return (unsigned int)(social_now(engine) - since) >= time;
}

void social_log(struct social_engine *engine, const char *format, ...)
{
	char line[256];
	va_list arguments;

	if (!engine->env.log)
		return;
	va_start(arguments, format);
	vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	engine->env.log(engine->env.user, line);
}

static void random_bytes(struct social_engine *engine, unsigned char *bytes, int size)
{
	engine->env.random(engine->env.user, bytes, size);
}

void social_engine_init(struct social_engine *engine, const struct social_env *env)
{
	memset(engine, 0, sizeof(*engine));
	engine->env = *env;
	engine->share_status = 1;
	engine->details = SOCIAL_DETAILS_BASIC;
	engine->accept_requests = 1;
	engine->send_tokens = SOCIAL_SEND_BURST;
	engine->send_time = social_now(engine);
	engine->verify_tokens = SOCIAL_VERIFY_BURST;
	engine->verify_time = engine->send_time;
	engine->open_start = engine->send_time;
	engine->start_time = engine->send_time;
	engine->place.activity = SOCIAL_MENUS;
	engine->topics_dirty = 1;
	strcpy(engine->name, "PLAYER");
}

static void derive_circle(struct social_engine *engine)
{
	social_circle_for(engine->circle_key, engine->circle_topic, engine->circle_seal);
}

void social_set_name(struct social_engine *engine, const char *name)
{
	char clean[SOCIAL_NAME_SIZE + 1];

	text_safe_name_utf8(clean, (short)sizeof(clean), name ? name : "", (long)(name ? strlen(name) : 0), "PLAYER");
	if (strcmp(clean, engine->name))
	{
		strcpy(engine->name, clean);
		engine->version++;
	}
}

void social_set_identity(struct social_engine *engine, const unsigned char *seed, const char *name, unsigned int stored_generation)
{
	social_identity_make(&engine->identity, seed);
	engine->has_identity = 1;
	engine->stored_generation = stored_generation;
	social_set_name(engine, name);
	social_inbox_for(engine->identity.code_bytes, engine->inbox_topic, engine->inbox_seal);
	random_bytes(engine, engine->circle_key, SOCIAL_KEY_SIZE);
	engine->circle_generation = 1;
	derive_circle(engine);
	engine->topics_dirty = 1;
	engine->version++;
	social_log(engine, "identity ready, code %s, name %s", engine->identity.code, engine->name);
}

void social_set_circle(struct social_engine *engine, int generation, const unsigned char *key)
{
	if (generation < 1 || generation > 255)
		return;
	memcpy(engine->circle_key, key, SOCIAL_KEY_SIZE);
	engine->circle_generation = (unsigned char)generation;
	derive_circle(engine);
	engine->version++;
}

void social_set_enabled(struct social_engine *engine, int enabled)
{
	if (engine->enabled == !!enabled)
		return;
	engine->enabled = !!enabled;
	engine->shut_down = 0;
	engine->topics_dirty = 1;
	engine->ready_before = 0;
	social_log(engine, "%s", enabled ? "on" : "off");
}

void social_set_share(struct social_engine *engine, int share_status, int details, int accept_requests)
{
	if (details < SOCIAL_DETAILS_NONE || details > SOCIAL_DETAILS_FULL)
		details = SOCIAL_DETAILS_BASIC;
	if (engine->accept_requests != !!accept_requests)
		engine->topics_dirty = 1;
	engine->share_status = !!share_status;
	engine->details = details;
	engine->accept_requests = !!accept_requests;
	engine->version++;
}

void social_set_place(struct social_engine *engine, const struct social_place *place)
{
	struct social_place clean = *place;

	if (clean.activity > SOCIAL_BUSY)
		clean.activity = SOCIAL_MENUS;
	text_safe_text(clean.map, (short)sizeof(clean.map), place->map, (short)sizeof(place->map), SOCIAL_MAP_SIZE);
	text_safe_text(clean.gametype, (short)sizeof(clean.gametype), place->gametype, (short)sizeof(place->gametype), SOCIAL_GAMETYPE_SIZE);
	/* a solo game cannot be joined, and nor can one that is not a game at all */
	if (clean.activity != SOCIAL_LOBBY && clean.activity != SOCIAL_GAME)
		clean.joinable = 0;
	if (!clean.joinable)
		memset(clean.invite_token, 0, sizeof(clean.invite_token));
	if (memcmp(&clean, &engine->place, sizeof(clean)))
	{
		engine->place = clean;
		engine->version++;
	}
}

const char *social_code(const struct social_engine *engine)
{
	return engine->identity.code;
}

const struct social_statistics *social_statistics(const struct social_engine *engine)
{
	return &engine->statistics;
}

unsigned int social_next_sequence(struct social_engine *engine)
{
	return ++engine->sequence;
}

/* ---------- events */

void social_event_push(struct social_engine *engine, const struct social_event *event)
{
	int index;

	if (engine->event_count == SOCIAL_MAX_EVENTS)
	{
		/* (the oldest goes: a game that does not read them must not stop the rest) */
		engine->event_first = (engine->event_first + 1) % SOCIAL_MAX_EVENTS;
		engine->event_count--;
	}
	index = (engine->event_first + engine->event_count) % SOCIAL_MAX_EVENTS;
	engine->events[index] = *event;
	engine->event_count++;
	engine->version++;
}

int social_event_take(struct social_engine *engine, struct social_event *event)
{
	if (!engine->event_count)
		return 0;
	*event = engine->events[engine->event_first];
	engine->event_first = (engine->event_first + 1) % SOCIAL_MAX_EVENTS;
	engine->event_count--;
	return 1;
}

/* ---------- friends */

struct social_friend *social_friend_by_key(struct social_engine *engine, const unsigned char *key)
{
	int index;

	for (index = 0; index < SOCIAL_MAX_FRIENDS; index++)
	{
		if (engine->friends[index].used && !memcmp(engine->friends[index].key, key, SOCIAL_KEY_SIZE))
			return &engine->friends[index];
	}
	return NULL;
}

const struct social_friend *social_friend_by_id(const struct social_engine *engine, unsigned int id)
{
	int index;

	for (index = 0; index < SOCIAL_MAX_FRIENDS; index++)
	{
		if (engine->friends[index].used && engine->friends[index].id == id)
			return &engine->friends[index];
	}
	return NULL;
}

int social_friend_count(const struct social_engine *engine)
{
	int index, count = 0;

	for (index = 0; index < SOCIAL_MAX_FRIENDS; index++)
		count += engine->friends[index].used;
	return count;
}

int social_friend_online(const struct social_engine *engine, const struct social_friend *friend)
{
	struct social_engine *writable = (struct social_engine *)engine;

	return friend->used && friend->heard && !social_elapsed(writable, friend->heard_time, SOCIAL_OFFLINE_TIME);
}

static void friend_circle(struct social_friend *friend, int generation, const unsigned char *key)
{
	friend->has_circle = 1;
	friend->circle_generation = (unsigned char)generation;
	memcpy(friend->circle_key, key, SOCIAL_KEY_SIZE);
	social_circle_for(key, friend->topic, friend->circle_seal);
}

struct social_friend *social_friend_add(struct social_engine *engine, const unsigned char *key, int circle_generation, const unsigned char *circle_key,
	int acknowledged)
{
	struct social_friend *friend = social_friend_by_key(engine, key);
	unsigned char scratch[SOCIAL_KEY_SIZE];
	int index;

	if (!engine->has_identity || !memcmp(key, engine->identity.key, SOCIAL_KEY_SIZE))
		return NULL;
	if (!friend)
	{
		unsigned int id;

		/* (a key of small order, which anyone can sign for, is no one's) */
		if (!p2p_ed25519_to_x25519(key, scratch))
			return NULL;
		for (index = 0; index < SOCIAL_MAX_FRIENDS && engine->friends[index].used; index++)
			;
		if (index == SOCIAL_MAX_FRIENDS)
			return NULL;
		friend = &engine->friends[index];
		memset(friend, 0, sizeof(*friend));
		friend->used = 1;
		memcpy(friend->key, key, SOCIAL_KEY_SIZE);
		p2p_sha256(key, SOCIAL_KEY_SIZE, friend->hash);
		id = (unsigned int)friend->hash[0] << 24 | (unsigned int)friend->hash[1] << 16 | (unsigned int)friend->hash[2] << 8 | friend->hash[3];
		/* (never 0, and not another friend's) */
		while (!id || (social_friend_by_id(engine, id) && social_friend_by_id(engine, id) != friend))
			id++;
		friend->id = id;
		social_code_of_key(key, friend->code_bytes, friend->code);
	}
	if (circle_key && circle_generation >= 1 && circle_generation <= 255 && (!friend->has_circle || circle_generation != friend->circle_generation ||
		memcmp(circle_key, friend->circle_key, SOCIAL_KEY_SIZE)))
	{
		friend_circle(friend, circle_generation, circle_key);
		engine->topics_dirty = 1;
	}
	if (acknowledged)
		friend->acknowledged = 1;
	engine->version++;
	return friend;
}

int social_friend_pair(struct social_engine *engine, struct social_friend *friend)
{
	if (friend->pair_ready)
		return 1;
	if (!social_pair_for(&engine->identity, friend->key, friend->pair_key, friend->tag_in, friend->tag_out))
		return 0;
	friend->pair_ready = 1;
	return 1;
}

/* ---------- sending */

static void bucket_refill(struct social_engine *engine)
{
	unsigned int now = social_now(engine);
	unsigned int gained = (now - engine->send_time) / SOCIAL_SEND_INTERVAL;

	if (gained > 0)
	{
		engine->send_tokens = gained + (unsigned int)engine->send_tokens > SOCIAL_SEND_BURST ? SOCIAL_SEND_BURST :
			engine->send_tokens + (int)gained;
		engine->send_time += gained * SOCIAL_SEND_INTERVAL;
		if (engine->send_tokens == SOCIAL_SEND_BURST)
			engine->send_time = now;
	}
}

int social_send(struct social_engine *engine, const char *topic, int kind, const unsigned char *key, const unsigned char *tag, const unsigned char *plain,
	int plain_size)
{
	struct social_queued *queued;
	unsigned char nonce[SOCIAL_NONCE_SIZE];
	int index, size;

	if (engine->queue_count == SOCIAL_MAX_QUEUE)
	{
		engine->statistics.send_dropped++;
		return 0;
	}
	index = (engine->queue_first + engine->queue_count) % SOCIAL_MAX_QUEUE;
	queued = &engine->queue[index];
	random_bytes(engine, nonce, SOCIAL_NONCE_SIZE);
	size = social_seal(key, kind, tag, topic, plain, plain_size, nonce, queued->payload, (int)sizeof(queued->payload));
	if (size < 0)
	{
		engine->statistics.send_dropped++;
		return 0;
	}
	queued->size = size;
	queued->time = social_now(engine);
	snprintf(queued->topic, sizeof(queued->topic), "%s", topic);
	engine->queue_count++;
	engine->statistics.queued++;
	return 1;
}

static void flush(struct social_engine *engine)
{
	if (!engine->queue_count)
		return;
	if (engine->env.ready(engine->env.user) <= 0)
		return;
	bucket_refill(engine);
	while (engine->queue_count)
	{
		struct social_queued *queued = &engine->queue[engine->queue_first];

		if (social_elapsed(engine, queued->time, SOCIAL_QUEUE_LIFE))
		{
			engine->statistics.send_expired++;
		}
		else if (engine->send_tokens <= 0 || !engine->env.publish(engine->env.user, queued->topic, queued->payload, queued->size))
		{
			return;
		}
		else
		{
			engine->send_tokens--;
			engine->statistics.sent++;
		}
		engine->queue_first = (engine->queue_first + 1) % SOCIAL_MAX_QUEUE;
		engine->queue_count--;
	}
}

/* ---------- presence */

static void presence_build(struct social_engine *engine, struct social_presence *presence)
{
	const struct social_place *place = &engine->place;
	int full = engine->details == SOCIAL_DETAILS_FULL;
	int joinable = place->joinable && engine->details != SOCIAL_DETAILS_NONE;

	memset(presence, 0, sizeof(*presence));
	presence->circle_generation = engine->circle_generation;
	presence->activity = engine->details == SOCIAL_DETAILS_NONE ? SOCIAL_BUSY : place->activity;
	presence->platform = (unsigned char)engine->env.platform;
	presence->network_version = (unsigned short)engine->env.network_version;
	strcpy(presence->name, engine->name);
	if (place->hosting && engine->details != SOCIAL_DETAILS_NONE)
		presence->flags |= SOCIAL_PRESENCE_HOSTING;
	if (joinable)
		presence->flags |= SOCIAL_PRESENCE_JOINABLE;
	if (place->private_game && engine->details != SOCIAL_DETAILS_NONE)
		presence->flags |= SOCIAL_PRESENCE_PRIVATE;
	if (full || joinable)
	{
		presence->flags |= SOCIAL_PRESENCE_DETAILS;
		strcpy(presence->map, place->map);
		strcpy(presence->gametype, place->gametype);
		presence->players = place->players;
		presence->maximum_players = place->maximum_players;
		presence->engine_type = place->engine_type;
	}
	if (joinable)
	{
		memcpy(presence->invite_hash, place->invite_hash, SOCIAL_HASH_SIZE);
		memcpy(presence->invite_token, place->invite_token, SOCIAL_TOKEN_SIZE);
	}
	social_presence_extra(engine, presence);
}

/* publishes this presence (with flags added); 0 if it could not be queued */
static int presence_publish(struct social_engine *engine, const struct social_presence *presence, int extra_flags, int forced)
{
	struct social_presence copy = *presence;
	unsigned char plain[SOCIAL_MAX_PLAIN];
	int size;

	copy.flags = (unsigned char)(copy.flags | extra_flags);
	size = social_presence_encode(&engine->identity, engine->generation, social_next_sequence(engine), &copy, plain, (int)sizeof(plain));
	if (size < 0)
		return 0;
	if (forced)
	{
		/* (the game is quitting: past the bucket and the queue) */
		unsigned char nonce[SOCIAL_NONCE_SIZE];
		unsigned char wire[SOCIAL_MAX_WIRE];
		int wire_size;

		random_bytes(engine, nonce, SOCIAL_NONCE_SIZE);
		wire_size = social_seal(engine->circle_seal, SOCIAL_KIND_PRESENCE, NULL, engine->circle_topic, plain, size, nonce, wire, (int)sizeof(wire));
		if (wire_size > 0 && engine->env.ready(engine->env.user) > 0)
			engine->env.publish(engine->env.user, engine->circle_topic, wire, wire_size);
		return 1;
	}
	return social_send(engine, engine->circle_topic, SOCIAL_KIND_PRESENCE, engine->circle_seal, NULL, plain, size);
}

/* how different two presences are: 0 the same, 1 a count or a name, 2 where the machine is or whether it can be joined */
static int presence_difference(const struct social_presence *old, const struct social_presence *current)
{
	if (old->activity != current->activity || (old->flags & ~(SOCIAL_PRESENCE_FIRST | SOCIAL_PRESENCE_OFFLINE)) !=
		(current->flags & ~(SOCIAL_PRESENCE_FIRST | SOCIAL_PRESENCE_OFFLINE)) || old->circle_generation != current->circle_generation ||
		memcmp(old->invite_hash, current->invite_hash, SOCIAL_HASH_SIZE) || memcmp(old->invite_token, current->invite_token, SOCIAL_TOKEN_SIZE) ||
		memcmp(old->party_token, current->party_token, SOCIAL_TOKEN_SIZE))
	{
		return 2;
	}
	return memcmp(old, current, sizeof(*old)) ? 1 : 0;
}

static void presence_update(struct social_engine *engine)
{
	struct social_presence presence;
	int ready = engine->env.ready(engine->env.user) > 0;
	int difference;
	int send_first = 0;

	if (!engine->generation_ready)
		return;
	if (!ready)
	{
		engine->ready_before = 0;
		return;
	}
	if (!engine->ready_before)
	{
		/* (a broker came up: the friends are told, and answer) */
		engine->ready_before = 1;
		engine->presence_wanted = 1;
	}
	if (!engine->share_status)
	{
		/* appear offline: one last word, then nothing */
		if (engine->presence_sent && !engine->presence_offline_sent)
		{
			presence_build(engine, &presence);
			if (presence_publish(engine, &presence, SOCIAL_PRESENCE_OFFLINE, 0))
			{
				engine->presence_offline_sent = 1;
				social_log(engine, "presence: appearing offline");
			}
		}
		return;
	}
	if (engine->presence_offline_sent)
	{
		engine->presence_offline_sent = 0;
		engine->presence_wanted = 1;
	}
	presence_build(engine, &presence);
	send_first = engine->presence_wanted || !engine->presence_sent;
	difference = engine->presence_sent ? presence_difference(&engine->sent_presence, &presence) : 2;
	if (send_first)
	{
		if (!presence_publish(engine, &presence, SOCIAL_PRESENCE_FIRST, 0))
			return;
		engine->presence_wanted = 0;
	}
	else if (engine->answer_pending && (int)(social_now(engine) - engine->answer_due) >= 0)
	{
		if (!presence_publish(engine, &presence, 0, 0))
			return;
	}
	else if (difference)
	{
		unsigned int interval = difference == 2 || engine->sent_presence.activity != SOCIAL_GAME ? SOCIAL_PRESENCE_INTERVAL :
			SOCIAL_PRESENCE_GAME_INTERVAL;

		if (!social_elapsed(engine, engine->presence_time, interval) || !presence_publish(engine, &presence, 0, 0))
			return;
	}
	else if (!social_elapsed(engine, engine->presence_time, SOCIAL_HEARTBEAT) || !presence_publish(engine, &presence, 0, 0))
	{
		return;
	}
	engine->sent_presence = presence;
	engine->presence_sent = 1;
	engine->presence_time = social_now(engine);
	/* (an answer to a FIRST is at most one in SOCIAL_ANSWER_REPEAT: a word sent for any other reason may have come before the friend listened) */
	if (engine->answer_pending)
	{
		engine->answer_last = engine->presence_time;
		engine->answer_valid = 1;
	}
	engine->answer_pending = 0;
}

static void presence_answer_wanted(struct social_engine *engine)
{
	unsigned char random[2];
	unsigned int span = SOCIAL_ANSWER_MAXIMUM - SOCIAL_ANSWER_MINIMUM;

	if (engine->answer_pending || !engine->share_status || !engine->presence_sent)
		return;
	if (engine->answer_valid && !social_elapsed(engine, engine->answer_last, SOCIAL_ANSWER_REPEAT))
		return;
	random_bytes(engine, random, 2);
	engine->answer_pending = 1;
	engine->answer_due = social_now(engine) + SOCIAL_ANSWER_MINIMUM + ((unsigned int)random[0] << 8 | random[1]) % span;
}

/* a friend's presence, checked and kept */
static void presence_apply(struct social_engine *engine, struct social_friend *friend, const unsigned char *plain, int size, const struct social_inner *inner)
{
	struct social_presence presence;
	struct social_event event;
	const unsigned char *signature;
	int signed_size;
	int was_online;

	if (!social_presence_decode(plain, size, &presence, &signed_size, &signature))
	{
		engine->statistics.dropped_malformed++;
		return;
	}
	if (!social_presence_verify(friend->key, plain, signed_size, signature))
	{
		engine->statistics.dropped_signature++;
		return;
	}
	social_window_mark(&friend->window, inner->generation, inner->sequence);
	engine->statistics.accepted++;
	was_online = social_friend_online(engine, friend);
	friend->presence = presence;
	if (presence.flags & SOCIAL_PRESENCE_OFFLINE)
	{
		friend->heard = 0;
		if (was_online)
		{
			memset(&event, 0, sizeof(event));
			event.type = SOCIAL_EVENT_OFFLINE;
			event.id = friend->id;
			snprintf(event.name, sizeof(event.name), "%s", presence.name);
			social_event_push(engine, &event);
			social_log(engine, "friend %s is offline", friend->code);
		}
	}
	else
	{
		friend->heard = 1;
		friend->heard_time = social_now(engine);
		if (!was_online)
		{
			memset(&event, 0, sizeof(event));
			event.type = SOCIAL_EVENT_ONLINE;
			event.id = friend->id;
			snprintf(event.name, sizeof(event.name), "%s", presence.name);
			social_event_push(engine, &event);
			social_log(engine, "friend %s (%s) is online", friend->code, presence.name);
		}
		if (presence.flags & SOCIAL_PRESENCE_FIRST)
			presence_answer_wanted(engine);
	}
	engine->version++;
}

/* a friend's presence, opened: checked now if the budget allows, else when it does (a friend's coming online is a handful of them at once) */
static void presence_heard(struct social_engine *engine, struct social_friend *friend, const unsigned char *plain, int size, const struct social_inner *inner)
{
	struct social_pending *pending;

	if (social_verify_budget(engine))
	{
		presence_apply(engine, friend, plain, size, inner);
		return;
	}
	if (engine->pending_count == SOCIAL_MAX_PENDING)
	{
		engine->statistics.dropped_verify_budget++;
		return;
	}
	pending = &engine->pending[(engine->pending_first + engine->pending_count) % SOCIAL_MAX_PENDING];
	pending->friend_id = friend->id;
	pending->time = social_now(engine);
	pending->size = size;
	pending->inner = *inner;
	memcpy(pending->plain, plain, (size_t)size);
	engine->pending_count++;
	engine->statistics.deferred++;
}

static void pending_update(struct social_engine *engine)
{
	while (engine->pending_count)
	{
		struct social_pending *pending = &engine->pending[engine->pending_first];
		const struct social_friend *found;

		if (social_elapsed(engine, pending->time, SOCIAL_PENDING_LIFE))
		{
			engine->statistics.dropped_verify_budget++;
		}
		else
		{
			if (!social_verify_budget(engine))
				return;
			found = social_friend_by_id(engine, pending->friend_id);
			/* (the friend may have gone, or a newer word of it been taken meanwhile) */
			if (found && social_window_check(&found->window, pending->inner.generation, pending->inner.sequence))
				presence_apply(engine, (struct social_friend *)found, pending->plain, pending->size, &pending->inner);
		}
		engine->pending_first = (engine->pending_first + 1) % SOCIAL_MAX_PENDING;
		engine->pending_count--;
	}
}

/* ---------- receiving */

int social_verify_budget(struct social_engine *engine)
{
	unsigned int now = social_now(engine);
	unsigned int gained = (now - engine->verify_time) / SOCIAL_VERIFY_INTERVAL;

	if (gained > 0)
	{
		engine->verify_tokens = gained + (unsigned int)engine->verify_tokens > SOCIAL_VERIFY_BURST ? SOCIAL_VERIFY_BURST :
			engine->verify_tokens + (int)gained;
		engine->verify_time += gained * SOCIAL_VERIFY_INTERVAL;
		if (engine->verify_tokens == SOCIAL_VERIFY_BURST)
			engine->verify_time = now;
	}
	if (engine->verify_tokens <= 0)
		return 0;
	engine->verify_tokens--;
	return 1;
}

static int open_budget(struct social_engine *engine)
{
	if (social_elapsed(engine, engine->open_start, 1000))
	{
		engine->open_start = social_now(engine);
		engine->open_count = 0;
	}
	return ++engine->open_count <= SOCIAL_OPEN_RATE;
}

static int friend_rate_ok(struct social_engine *engine, struct social_friend *friend)
{
	if (social_elapsed(engine, friend->rate_start, 1000))
	{
		friend->rate_start = social_now(engine);
		friend->rate_count = 0;
	}
	return ++friend->rate_count <= SOCIAL_FRIEND_RATE;
}

static int seen_before(struct social_engine *engine, const unsigned char *wire, int size, unsigned char *id)
{
	unsigned char digest[P2P_SHA256_SIZE];
	int index;

	p2p_sha256(wire, size, digest);
	memcpy(id, digest, 8);
	for (index = 0; index < engine->seen_count; index++)
	{
		if (!memcmp(engine->seen[index], id, 8))
			return 1;
	}
	return 0;
}

static void seen_add(struct social_engine *engine, const unsigned char *id)
{
	memcpy(engine->seen[engine->seen_next], id, 8);
	engine->seen_next = (engine->seen_next + 1) % SOCIAL_MAX_SEEN;
	if (engine->seen_count < SOCIAL_MAX_SEEN)
		engine->seen_count++;
}

static struct social_friend *friend_by_topic(struct social_engine *engine, const char *topic)
{
	int index;

	for (index = 0; index < SOCIAL_MAX_FRIENDS; index++)
	{
		if (engine->friends[index].used && engine->friends[index].has_circle && !strcmp(engine->friends[index].topic, topic))
			return &engine->friends[index];
	}
	return NULL;
}

void social_heard(struct social_engine *engine, const char *topic, const unsigned char *payload, int size)
{
	unsigned char plain[SOCIAL_MAX_PLAIN];
	unsigned char id[8];
	struct social_inner inner;
	struct social_friend *friend = NULL;
	const unsigned char *tag;
	const unsigned char *key = NULL;
	int kind, plain_size;
	int on_inbox = 0;
	int on_party = 0;

	if (!engine->enabled || !engine->has_identity)
		return;
	engine->statistics.received++;
	if (!payload || size < 2 || size > SOCIAL_MAX_WIRE)
	{
		engine->statistics.dropped_size++;
		return;
	}
	if (!social_wire_header(payload, size, &kind, &tag))
	{
		engine->statistics.dropped_kind++;
		return;
	}
	if (!topic || strlen(topic) >= SOCIAL_TOPIC_SIZE)
	{
		engine->statistics.dropped_topic++;
		return;
	}
	if (!strcmp(topic, engine->inbox_topic))
		on_inbox = 1;
	else if (!(friend = friend_by_topic(engine, topic)))
	{
		on_party = social_party_topic(engine, topic);
		if (!on_party)
		{
			engine->statistics.dropped_topic++;
			return;
		}
	}
	/* (a message that came already through another broker costs a hash and no budget: coming online is a message from every friend, four times) */
	if (seen_before(engine, payload, size, id))
	{
		engine->statistics.dropped_seen++;
		return;
	}
	if (!open_budget(engine))
	{
		engine->statistics.dropped_rate++;
		return;
	}
	if (friend && !friend_rate_ok(engine, friend))
	{
		engine->statistics.dropped_rate++;
		return;
	}
	/* which key opens it */
	if (on_inbox)
		key = kind == SOCIAL_KIND_REQUEST ? engine->inbox_seal : NULL;
	else if (friend)
		key = kind == SOCIAL_KIND_PRESENCE ? friend->circle_seal : NULL;
	if (kind == SOCIAL_KIND_DIRECT)
		friend = social_direct_sender(engine, friend, tag, &key);
	else if (kind == SOCIAL_KIND_PARTY)
		key = social_party_key(engine, topic);
	if (!key || (kind == SOCIAL_KIND_DIRECT && (!friend || on_party)))
	{
		engine->statistics.dropped_unknown++;
		return;
	}
	plain_size = social_open(key, topic, payload, size, plain, (int)sizeof(plain));
	engine->statistics.attempted++;
	if (plain_size < 0)
	{
		engine->statistics.dropped_open++;
		return;
	}
	seen_add(engine, id);
	engine->statistics.opened++;
	if (!social_inner_header(plain, plain_size, &inner))
	{
		engine->statistics.dropped_header++;
		return;
	}
	/* a friend's messages are in order of a window; strangers' (requests) and the party's are dealt with by their own */
	if (friend && !social_window_check(&friend->window, inner.generation, inner.sequence))
	{
		engine->statistics.dropped_window++;
		return;
	}
	if (kind == SOCIAL_KIND_PRESENCE && friend && inner.type == SOCIAL_TYPE_PRESENCE)
		presence_heard(engine, friend, plain, plain_size, &inner);
	else if (kind == SOCIAL_KIND_REQUEST && on_inbox)
		social_request_heard(engine, plain, plain_size, &inner);
	else if (kind == SOCIAL_KIND_DIRECT)
		social_direct_heard(engine, friend, on_inbox, plain, plain_size, &inner);
	else if (kind == SOCIAL_KIND_PARTY)
		social_party_heard(engine, topic, plain, plain_size, &inner);
	else
		engine->statistics.dropped_unknown++;
}

/* ---------- the passes */

static void generation_update(struct social_engine *engine)
{
	unsigned long long utc;
	int anchored = 0;
	unsigned int minutes = 0;

	if (engine->generation_ready || !engine->has_identity)
		return;
	utc = engine->env.utc(engine->env.user, &anchored);
	if (!anchored && !social_elapsed(engine, engine->start_time, SOCIAL_ANCHOR_WAIT))
		return;
	/* (the clock is a floor, so that a card restored from before, or a reinstall, never sends a lower generation than one it sent) */
	if (anchored && utc > GENERATION_EPOCH)
		minutes = (unsigned int)((utc - GENERATION_EPOCH) / 60);
	engine->generation = engine->stored_generation + 1 > minutes ? engine->stored_generation + 1 : minutes;
	engine->stored_generation = engine->generation;
	engine->generation_ready = 1;
	engine->generation_changed = 1;
	engine->version++;
	social_log(engine, "generation %u (%s)", engine->generation, anchored ? "clock anchored" : "clock not anchored");
}

static void topics_update(struct social_engine *engine)
{
	char wanted[SOCIAL_MAX_TOPICS][SOCIAL_TOPIC_SIZE];
	int count = 0;
	int index;

	if (!engine->topics_dirty)
		return;
	engine->topics_dirty = 0;
	memset(wanted, 0, sizeof(wanted));
	if (engine->enabled && engine->has_identity)
	{
		if (engine->accept_requests || social_requests_pending(engine))
			snprintf(wanted[count++], SOCIAL_TOPIC_SIZE, "%s", engine->inbox_topic);
		for (index = 0; index < SOCIAL_MAX_FRIENDS && count < SOCIAL_MAX_TOPICS - 1; index++)
		{
			if (engine->friends[index].used && engine->friends[index].has_circle)
				snprintf(wanted[count++], SOCIAL_TOPIC_SIZE, "%s", engine->friends[index].topic);
		}
		if (count < SOCIAL_MAX_TOPICS && social_party_topic_wanted(engine, wanted[count]))
			count++;
	}
	if (count == engine->wanted_count && !memcmp(wanted, engine->wanted, sizeof(wanted)))
		return;
	memcpy(engine->wanted, wanted, sizeof(wanted));
	engine->wanted_count = count;
	engine->env.subscribe(engine->env.user, (const char (*)[SOCIAL_TOPIC_SIZE])engine->wanted, count);
	social_log(engine, "listening to %d topics", count);
}

void social_topics_changed(struct social_engine *engine)
{
	engine->topics_dirty = 1;
}

static void friends_update(struct social_engine *engine)
{
	struct social_event event;
	int index;

	for (index = 0; index < SOCIAL_MAX_FRIENDS; index++)
	{
		struct social_friend *friend = &engine->friends[index];

		if (!friend->used)
			continue;
		if (friend->heard && social_elapsed(engine, friend->heard_time, SOCIAL_OFFLINE_TIME))
		{
			friend->heard = 0;
			memset(&event, 0, sizeof(event));
			event.type = SOCIAL_EVENT_OFFLINE;
			event.id = friend->id;
			snprintf(event.name, sizeof(event.name), "%s", friend->presence.name);
			social_event_push(engine, &event);
			social_log(engine, "friend %s timed out, offline", friend->code);
		}
		/* (one pair key a pass, so that fifty friends are not fifty X25519s in one) */
		if (!friend->pair_ready && !engine->pair_worked)
		{
			social_friend_pair(engine, friend);
			engine->pair_worked = 1;
		}
	}
	engine->pair_worked = 0;
}

void social_update(struct social_engine *engine)
{
	if (engine->shut_down)
		return;
	if (!engine->enabled || !engine->has_identity)
	{
		/* (turned off: the topics asked for go) */
		if (engine->has_identity)
			topics_update(engine);
		return;
	}
	generation_update(engine);
	topics_update(engine);
	pending_update(engine);
	friends_update(engine);
	social_friends_update(engine);
	social_party_update(engine);
	presence_update(engine);
	flush(engine);
}

void social_shutdown(struct social_engine *engine)
{
	struct social_presence presence;

	if (!engine->enabled || !engine->has_identity || !engine->generation_ready)
		return;
	social_party_shutdown(engine);
	if (engine->share_status && engine->presence_sent && !engine->presence_offline_sent)
	{
		presence_build(engine, &presence);
		presence_publish(engine, &presence, SOCIAL_PRESENCE_OFFLINE, 1);
		engine->presence_offline_sent = 1;
		social_log(engine, "shutdown, offline said");
	}
	engine->shut_down = 1;
}
