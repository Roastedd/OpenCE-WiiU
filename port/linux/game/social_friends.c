/*
SOCIAL_FRIENDS.C

Requests, answers, hello and remove between friends (social_core.h; the design is reports/research/2026-11-07/design/social.md 3.4 and 3.5).

A request is an ADD sent to the other's inbox, a topic anyone who knows the other's friend code can work out. Accepting is sending an ADD back. Each side then sends
the other a HELLO, which carries its circle key (the key its presence is sealed with, held by its friends only), until the other's HELLO says the key arrived. Removing
a friend sends it a REMOVE and changes the circle key, so that it hears nothing more.
*/

#include "../src/p2p_internal.h"
#include "social_core.h"
#include "text_safe.h"

#include <stdio.h>
#include <string.h>

/* ---------- small things */

static unsigned int id_of_bytes(const unsigned char *bytes)
{
	unsigned int id = (unsigned int)bytes[0] << 24 | (unsigned int)bytes[1] << 16 | (unsigned int)bytes[2] << 8 | bytes[3];

	return id ? id : 1;
}

static void store_changed(struct social_engine *engine)
{
	if (!engine->store_dirty)
		engine->store_dirty_time = social_now(engine);
	engine->store_dirty = 1;
	engine->version++;
}

static unsigned long long utc_now(struct social_engine *engine)
{
	int anchored = 0;
	unsigned long long seconds = engine->env.utc(engine->env.user, &anchored);

	/* (0 when the clock is not anchored: a wrong date is worse than none) */
	return anchored ? seconds : 0;
}

static int listed(const unsigned char (*list)[SOCIAL_BLOCK_SIZE], int count, const unsigned char *key_hash)
{
	int index;

	for (index = 0; index < count; index++)
	{
		if (!memcmp(list[index], key_hash, SOCIAL_BLOCK_SIZE))
			return 1;
	}
	return 0;
}

static void hash_of_key(const unsigned char *key, unsigned char *hash)
{
	unsigned char digest[P2P_SHA256_SIZE];

	p2p_sha256(key, SOCIAL_KEY_SIZE, digest);
	memcpy(hash, digest, SOCIAL_BLOCK_SIZE);
}

static struct social_friend *friend_by_id_writable(struct social_engine *engine, unsigned int id)
{
	return (struct social_friend *)social_friend_by_id(engine, id);
}

static void event_for(struct social_engine *engine, int type, const struct social_friend *friend, const char *name, const char *code)
{
	struct social_event event;

	memset(&event, 0, sizeof(event));
	event.type = type;
	event.id = friend ? friend->id : 0;
	snprintf(event.name, sizeof(event.name), "%s", name ? name : friend ? friend->presence.name : "");
	snprintf(event.code, sizeof(event.code), "%s", code ? code : friend ? friend->code : "");
	social_event_push(engine, &event);
}

/* ---------- sending */

static int send_request(struct social_engine *engine, const unsigned char *code_bytes)
{
	unsigned char plain[SOCIAL_MAX_PLAIN];
	unsigned char seal[SOCIAL_KEY_SIZE];
	char topic[SOCIAL_TOPIC_SIZE];
	int size;

	if (!engine->generation_ready)
		return 0;
	social_inbox_for(code_bytes, topic, seal);
	size = social_add_encode(&engine->identity, engine->generation, social_next_sequence(engine), engine->name, engine->accept_requests ? 0 : 1, code_bytes,
		plain, (int)sizeof(plain));
	return size > 0 && social_send(engine, topic, SOCIAL_KIND_REQUEST, seal, NULL, plain, size);
}

static int send_hello(struct social_engine *engine, struct social_friend *friend, int on_old_topic)
{
	struct social_hello hello;
	unsigned char plain[SOCIAL_MAX_PLAIN];
	unsigned char seal[SOCIAL_KEY_SIZE];
	char topic[SOCIAL_TOPIC_SIZE];
	int size;

	if (!engine->generation_ready || !social_friend_pair(engine, friend))
		return 0;
	memset(&hello, 0, sizeof(hello));
	hello.circle_generation = engine->circle_generation;
	memcpy(hello.circle_key, engine->circle_key, SOCIAL_KEY_SIZE);
	hello.have_generation = friend->has_circle ? friend->circle_generation : 0;
	size = social_hello_encode(engine->generation, social_next_sequence(engine), &hello, plain, (int)sizeof(plain));
	if (on_old_topic && engine->rotated)
		snprintf(topic, sizeof(topic), "%s", engine->old_circle_topic);
	else
		social_inbox_for(friend->code_bytes, topic, seal);
	return size > 0 && social_send(engine, topic, SOCIAL_KIND_DIRECT, friend->pair_key, friend->tag_out, plain, size);
}

/* the other is told it is no longer a friend: on the inbox and on the circle it listens to */
static void send_remove(struct social_engine *engine, struct social_friend *friend, int reason)
{
	unsigned char plain[SOCIAL_MAX_PLAIN];
	unsigned char seal[SOCIAL_KEY_SIZE];
	char topic[SOCIAL_TOPIC_SIZE];
	int size;

	if (!engine->generation_ready || !social_friend_pair(engine, friend))
		return;
	size = social_remove_encode(engine->generation, social_next_sequence(engine), reason, plain, (int)sizeof(plain));
	if (size <= 0)
		return;
	social_inbox_for(friend->code_bytes, topic, seal);
	social_send(engine, topic, SOCIAL_KIND_DIRECT, friend->pair_key, friend->tag_out, plain, size);
	size = social_remove_encode(engine->generation, social_next_sequence(engine), reason, plain, (int)sizeof(plain));
	social_send(engine, engine->circle_topic, SOCIAL_KIND_DIRECT, friend->pair_key, friend->tag_out, plain, size);
}

/* ---------- the circle */

static void rotate_circle(struct social_engine *engine)
{
	unsigned int delay = 0;
	int index;

	snprintf(engine->old_circle_topic, sizeof(engine->old_circle_topic), "%s", engine->circle_topic);
	engine->rotated = 1;
	engine->rotated_time = social_now(engine);
	engine->env.random(engine->env.user, engine->circle_key, SOCIAL_KEY_SIZE);
	engine->circle_generation = (unsigned char)(engine->circle_generation % 255 + 1);
	social_circle_for(engine->circle_key, engine->circle_topic, engine->circle_seal);
	for (index = 0; index < SOCIAL_MAX_FRIENDS; index++)
	{
		struct social_friend *friend = &engine->friends[index];

		if (!friend->used)
			continue;
		/* (one hello a second, the first on the old circle, which the friends still hear) */
		friend->acknowledged = 0;
		friend->hello_count = 0;
		friend->hello_next = social_now(engine) + delay;
		friend->reply_valid = 0;
		delay += SOCIAL_HELLO_SPACING;
	}
	store_changed(engine);
	social_log(engine, "circle key changed, generation %d", engine->circle_generation);
}

/* ---------- the calls */

static int add_code_checked(struct social_engine *engine, const char *text)
{
	unsigned char code_bytes[SOCIAL_CODE_BYTES];
	char clean[SOCIAL_CODE_TEXT_SIZE];
	struct social_sent *slot = NULL;
	int result, index, recent;

	if (!engine->enabled || !engine->has_identity)
		return SOCIAL_ADD_OFF;
	result = social_code_parse(text, code_bytes, clean);
	if (result == SOCIAL_CODE_BAD_TEXT)
		return SOCIAL_ADD_BAD_TEXT;
	if (result == SOCIAL_CODE_BAD_CHECK)
		return SOCIAL_ADD_BAD_CHECK;
	if (!memcmp(code_bytes, engine->identity.code_bytes, SOCIAL_CODE_BYTES))
		return SOCIAL_ADD_OWN;
	for (index = 0; index < SOCIAL_MAX_FRIENDS; index++)
	{
		if (engine->friends[index].used && !memcmp(engine->friends[index].code_bytes, code_bytes, SOCIAL_CODE_BYTES))
			return SOCIAL_ADD_ALREADY;
	}
	for (index = 0; index < SOCIAL_MAX_SENT; index++)
	{
		if (engine->sent[index].used && !memcmp(engine->sent[index].code_bytes, code_bytes, SOCIAL_CODE_BYTES))
		{
			/* (asked again: sent again at once, if the last was a while ago) */
			if (social_elapsed(engine, engine->sent[index].sent_time, 10000))
				engine->sent[index].sent_valid = 0;
			return SOCIAL_ADD_OK;
		}
	}
	if (social_friend_count(engine) >= SOCIAL_MAX_FRIENDS)
		return SOCIAL_ADD_FULL;
	for (index = 0; index < SOCIAL_MAX_SENT && !slot; index++)
	{
		if (!engine->sent[index].used)
			slot = &engine->sent[index];
	}
	if (!slot)
		return SOCIAL_ADD_PENDING_FULL;
	/* four new codes a minute at most */
	for (index = 0, recent = 0; index < engine->add_times_count; index++)
		recent += !social_elapsed(engine, engine->add_times[index], SOCIAL_ADD_WINDOW);
	if (recent >= SOCIAL_ADD_PER_WINDOW)
		return SOCIAL_ADD_TOO_FAST;
	if (engine->add_times_count < SOCIAL_ADD_PER_WINDOW)
		engine->add_times[engine->add_times_count++] = social_now(engine);
	else
	{
		memmove(engine->add_times, engine->add_times + 1, (SOCIAL_ADD_PER_WINDOW - 1) * sizeof(engine->add_times[0]));
		engine->add_times[SOCIAL_ADD_PER_WINDOW - 1] = social_now(engine);
	}
	memset(slot, 0, sizeof(*slot));
	slot->used = 1;
	slot->id = id_of_bytes(code_bytes);
	memcpy(slot->code_bytes, code_bytes, SOCIAL_CODE_BYTES);
	snprintf(slot->code, sizeof(slot->code), "%s", clean);
	slot->added = utc_now(engine);
	store_changed(engine);
	social_topics_changed(engine);
	social_log(engine, "request to %s", clean);
	return SOCIAL_ADD_OK;
}

int social_engine_add_code(struct social_engine *engine, const char *text)
{
	int result = add_code_checked(engine, text);

	engine->add_result = result;
	engine->add_serial++;
	engine->version++;
	if (result != SOCIAL_ADD_OK)
		social_log(engine, "request refused: %d", result);
	return result;
}

static struct social_request *request_by_id(struct social_engine *engine, unsigned int id)
{
	int index;

	for (index = 0; index < SOCIAL_MAX_REQUESTS; index++)
	{
		if (engine->requests[index].used && engine->requests[index].id == id)
			return &engine->requests[index];
	}
	return NULL;
}

int social_engine_accept(struct social_engine *engine, unsigned int id)
{
	struct social_request *request = request_by_id(engine, id);
	struct social_friend *friend;

	if (!request || !engine->generation_ready)
		return 0;
	friend = social_friend_add(engine, request->key, 0, NULL, 0);
	if (!friend)
		return 0;
	friend->added = utc_now(engine);
	friend->accept_sent = 1;
	friend->hello_next = social_now(engine);
	send_request(engine, friend->code_bytes);
	event_for(engine, SOCIAL_EVENT_FRIEND, friend, request->name, request->code);
	social_log(engine, "accepted %s", request->code);
	memset(request, 0, sizeof(*request));
	store_changed(engine);
	return 1;
}

int social_engine_ignore(struct social_engine *engine, unsigned int id)
{
	struct social_request *request = request_by_id(engine, id);
	unsigned char hash[SOCIAL_BLOCK_SIZE];

	if (!request)
		return 0;
	hash_of_key(request->key, hash);
	if (!listed((const unsigned char (*)[SOCIAL_BLOCK_SIZE])engine->ignored, engine->ignored_count, hash))
	{
		memcpy(engine->ignored[engine->ignored_next], hash, SOCIAL_BLOCK_SIZE);
		engine->ignored_next = (engine->ignored_next + 1) % SOCIAL_MAX_IGNORED;
		if (engine->ignored_count < SOCIAL_MAX_IGNORED)
			engine->ignored_count++;
	}
	memset(request, 0, sizeof(*request));
	store_changed(engine);
	return 1;
}

int social_engine_cancel(struct social_engine *engine, unsigned int id)
{
	int index;

	for (index = 0; index < SOCIAL_MAX_SENT; index++)
	{
		if (engine->sent[index].used && engine->sent[index].id == id)
		{
			memset(&engine->sent[index], 0, sizeof(engine->sent[index]));
			store_changed(engine);
			social_topics_changed(engine);
			return 1;
		}
	}
	return 0;
}

int social_engine_remove(struct social_engine *engine, unsigned int id)
{
	struct social_friend *friend = friend_by_id_writable(engine, id);

	if (!friend)
		return 0;
	send_remove(engine, friend, 0);
	social_log(engine, "removed %s", friend->code);
	memset(friend, 0, sizeof(*friend));
	social_topics_changed(engine);
	rotate_circle(engine);
	return 1;
}

int social_engine_block(struct social_engine *engine, unsigned int id)
{
	struct social_friend *friend = friend_by_id_writable(engine, id);
	unsigned char hash[SOCIAL_BLOCK_SIZE];

	if (!friend)
		return 0;
	hash_of_key(friend->key, hash);
	if (!listed((const unsigned char (*)[SOCIAL_BLOCK_SIZE])engine->blocked, engine->blocked_count, hash))
	{
		if (engine->blocked_count == SOCIAL_MAX_BLOCKED)
		{
			/* (the oldest is let go) */
			memmove(engine->blocked, engine->blocked + 1, (SOCIAL_MAX_BLOCKED - 1) * SOCIAL_BLOCK_SIZE);
			engine->blocked_count--;
		}
		memcpy(engine->blocked[engine->blocked_count++], hash, SOCIAL_BLOCK_SIZE);
	}
	send_remove(engine, friend, 1);
	social_log(engine, "blocked %s", friend->code);
	memset(friend, 0, sizeof(*friend));
	social_topics_changed(engine);
	rotate_circle(engine);
	return 1;
}

int social_engine_unblock(struct social_engine *engine, int index)
{
	if (index < 0 || index >= engine->blocked_count)
		return 0;
	memmove(engine->blocked[index], engine->blocked[index + 1], (size_t)(engine->blocked_count - index - 1) * SOCIAL_BLOCK_SIZE);
	engine->blocked_count--;
	store_changed(engine);
	return 1;
}

int social_engine_set_nickname(struct social_engine *engine, unsigned int id, const char *nickname)
{
	struct social_friend *friend = friend_by_id_writable(engine, id);
	char clean[SOCIAL_NAME_SIZE + 1];

	if (!friend)
		return 0;
	text_safe_text(clean, (short)sizeof(clean), nickname ? nickname : "", (short)(nickname ? strlen(nickname) : 0), SOCIAL_NAME_SIZE);
	if (strcmp(clean, friend->nickname))
	{
		strcpy(friend->nickname, clean);
		store_changed(engine);
	}
	return 1;
}

int social_requests_pending(const struct social_engine *engine)
{
	int index;

	for (index = 0; index < SOCIAL_MAX_SENT; index++)
	{
		if (engine->sent[index].used)
			return 1;
	}
	return 0;
}

/* ---------- receiving */

struct social_friend *social_direct_sender(struct social_engine *engine, struct social_friend *friend, const unsigned char *tag, const unsigned char **key)
{
	int index;

	if (friend)
	{
		if (!friend->pair_ready || !friend->has_circle || memcmp(tag, friend->tag_in, SOCIAL_TAG_SIZE))
			return NULL;
		*key = friend->pair_key;
		return friend;
	}
	for (index = 0; index < SOCIAL_MAX_FRIENDS; index++)
	{
		struct social_friend *candidate = &engine->friends[index];

		if (candidate->used && candidate->pair_ready && !memcmp(tag, candidate->tag_in, SOCIAL_TAG_SIZE))
		{
			*key = candidate->pair_key;
			return candidate;
		}
	}
	return NULL;
}

void social_request_heard(struct social_engine *engine, const unsigned char *plain, int size, const struct social_inner *inner)
{
	struct social_add add;
	unsigned char hash[SOCIAL_BLOCK_SIZE];
	unsigned char code_bytes[SOCIAL_CODE_BYTES];
	char code[SOCIAL_CODE_TEXT_SIZE];
	unsigned char scratch[SOCIAL_KEY_SIZE];
	struct social_friend *friend;
	struct social_request *request = NULL;
	int index, matched = -1;

	(void)inner;
	if (!social_add_decode(plain, size, &add))
	{
		engine->statistics.dropped_malformed++;
		return;
	}
	if (!memcmp(add.key, engine->identity.key, SOCIAL_KEY_SIZE))
		return;
	hash_of_key(add.key, hash);
	if (listed((const unsigned char (*)[SOCIAL_BLOCK_SIZE])engine->blocked, engine->blocked_count, hash) ||
		listed((const unsigned char (*)[SOCIAL_BLOCK_SIZE])engine->ignored, engine->ignored_count, hash))
	{
		engine->statistics.dropped_blocked++;
		return;
	}
	social_code_of_key(add.key, code_bytes, code);
	for (index = 0; index < SOCIAL_MAX_SENT; index++)
	{
		if (engine->sent[index].used && !memcmp(engine->sent[index].code_bytes, code_bytes, SOCIAL_CODE_BYTES))
			matched = index;
	}
	friend = social_friend_by_key(engine, add.key);
	if (!engine->accept_requests && matched < 0 && !friend)
	{
		engine->statistics.dropped_blocked++;
		return;
	}
	if (!social_verify_budget(engine))
	{
		engine->statistics.dropped_verify_budget++;
		return;
	}
	/* a key of small order is no one's, and the signature must be the key's, for this machine's inbox */
	engine->statistics.verifications++;
	if (!p2p_ed25519_to_x25519(add.key, scratch) || !social_add_verify(&add, plain, engine->identity.code_bytes))
	{
		engine->statistics.dropped_signature++;
		return;
	}
	engine->statistics.accepted++;
	if (friend)
	{
		/* (the other sends its acceptance again: it has not our hello) */
		friend->hello_next = social_now(engine);
		return;
	}
	if (matched >= 0)
	{
		/* the answer to a request of ours; only the key whose code was typed gets here, as a stranger's code is another's */
		friend = social_friend_add(engine, add.key, 0, NULL, 0);
		if (!friend)
			return;
		friend->added = utc_now(engine);
		friend->hello_next = social_now(engine);
		event_for(engine, SOCIAL_EVENT_FRIEND, friend, add.name, friend->code);
		social_log(engine, "%s answered the request: friends", friend->code);
		memset(&engine->sent[matched], 0, sizeof(engine->sent[matched]));
		store_changed(engine);
		social_topics_changed(engine);
		return;
	}
	for (index = 0; index < SOCIAL_MAX_REQUESTS; index++)
	{
		if (engine->requests[index].used && !memcmp(engine->requests[index].key, add.key, SOCIAL_KEY_SIZE))
		{
			snprintf(engine->requests[index].name, sizeof(engine->requests[index].name), "%s", add.name);
			return;
		}
	}
	for (index = 0; index < SOCIAL_MAX_REQUESTS && !request; index++)
	{
		if (!engine->requests[index].used)
			request = &engine->requests[index];
	}
	if (!request)
	{
		engine->statistics.dropped_blocked++;
		return;
	}
	memset(request, 0, sizeof(*request));
	request->used = 1;
	{
		unsigned char digest[P2P_SHA256_SIZE];

		p2p_sha256(add.key, SOCIAL_KEY_SIZE, digest);
		request->id = id_of_bytes(digest);
	}
	memcpy(request->key, add.key, SOCIAL_KEY_SIZE);
	snprintf(request->name, sizeof(request->name), "%s", add.name);
	snprintf(request->code, sizeof(request->code), "%s", code);
	request->received = social_now(engine);
	request->utc = utc_now(engine);
	{
		struct social_event event;

		memset(&event, 0, sizeof(event));
		event.type = SOCIAL_EVENT_REQUEST;
		event.id = request->id;
		snprintf(event.name, sizeof(event.name), "%s", request->name);
		snprintf(event.code, sizeof(event.code), "%s", request->code);
		social_event_push(engine, &event);
	}
	social_log(engine, "request from %s (%s)", request->code, request->name);
}

/* a friend's hello: its circle key, and which of this machine's it holds */
static void hello_heard(struct social_engine *engine, struct social_friend *friend, const unsigned char *plain, int size, const struct social_inner *inner)
{
	struct social_hello hello;
	int new_circle;

	if (!social_hello_decode(plain, size, &hello))
	{
		engine->statistics.dropped_malformed++;
		return;
	}
	social_window_mark(&friend->window, inner->generation, inner->sequence);
	engine->statistics.accepted++;
	new_circle = !friend->has_circle || friend->circle_generation != hello.circle_generation || memcmp(friend->circle_key, hello.circle_key, SOCIAL_KEY_SIZE);
	if (new_circle)
	{
		social_friend_add(engine, friend->key, hello.circle_generation, hello.circle_key, 0);
		/* (what it said before belonged to the old circle) */
		friend->heard = 0;
		/* its presence is on a topic this machine listens to only now: this machine says it is here, and the friend answers with its own */
		engine->presence_wanted = 1;
	}
	friend->acknowledged = hello.have_generation == engine->circle_generation;
	/* told something new, or it has not our key: answered, within a few seconds */
	if (new_circle || !friend->acknowledged)
		friend->reply_valid = 1;
	if (friend->acknowledged)
		friend->hello_count = 0;
	social_log(engine, "hello from %s: circle %d, it holds mine: %s", friend->code, hello.circle_generation, friend->acknowledged ? "yes" : "no");
	store_changed(engine);
}

static void remove_heard(struct social_engine *engine, struct social_friend *friend, const unsigned char *plain, int size, const struct social_inner *inner)
{
	int reason;
	struct social_event event;

	if (!social_remove_decode(plain, size, &reason))
	{
		engine->statistics.dropped_malformed++;
		return;
	}
	social_window_mark(&friend->window, inner->generation, inner->sequence);
	engine->statistics.accepted++;
	memset(&event, 0, sizeof(event));
	event.type = SOCIAL_EVENT_REMOVED;
	event.id = friend->id;
	snprintf(event.name, sizeof(event.name), "%s", friend->presence.name);
	snprintf(event.code, sizeof(event.code), "%s", friend->code);
	social_event_push(engine, &event);
	social_log(engine, "%s removed this machine from its friends", friend->code);
	memset(friend, 0, sizeof(*friend));
	social_topics_changed(engine);
	/* (it holds this machine's circle key still) */
	rotate_circle(engine);
}

void social_direct_heard(struct social_engine *engine, struct social_friend *friend, int on_inbox, const unsigned char *plain, int size,
	const struct social_inner *inner)
{
	(void)on_inbox;
	switch (inner->type)
	{
	case SOCIAL_TYPE_HELLO:
		hello_heard(engine, friend, plain, size, inner);
		break;
	case SOCIAL_TYPE_REMOVE:
		remove_heard(engine, friend, plain, size, inner);
		break;
	case SOCIAL_TYPE_INVITE:
		social_invite_heard(engine, friend, plain, size, inner);
		break;
	default:
		engine->statistics.dropped_unknown++;
		break;
	}
}

/* ---------- the passes */

void social_friends_update(struct social_engine *engine)
{
	int index;
	unsigned int now = social_now(engine);

	if (!engine->generation_ready)
		return;
	if (engine->rotated && social_elapsed(engine, engine->rotated_time, SOCIAL_ROTATION_LIFE))
		engine->rotated = 0;
	/* requests received and not answered go after half an hour */
	for (index = 0; index < SOCIAL_MAX_REQUESTS; index++)
	{
		if (engine->requests[index].used && social_elapsed(engine, engine->requests[index].received, SOCIAL_REQUEST_LIFE))
		{
			memset(&engine->requests[index], 0, sizeof(engine->requests[index]));
			engine->version++;
		}
	}
	if (engine->env.ready(engine->env.user) <= 0)
		return;
	/* requests sent are sent again every five minutes (one in a pass: the queue is short) */
	for (index = 0; index < SOCIAL_MAX_SENT; index++)
	{
		struct social_sent *sent = &engine->sent[index];

		if (sent->used && (!sent->sent_valid || social_elapsed(engine, sent->sent_time, SOCIAL_REQUEST_REPEAT)))
		{
			if (send_request(engine, sent->code_bytes))
			{
				sent->sent_time = now;
				sent->sent_valid = 1;
			}
			break;
		}
	}
	/* hello, one a second at most, until the friend has the key */
	if (engine->hello_valid && !social_elapsed(engine, engine->hello_last, SOCIAL_HELLO_SPACING))
		return;
	for (index = 0; index < SOCIAL_MAX_FRIENDS; index++)
	{
		struct social_friend *friend = &engine->friends[index];
		int due;

		if (!friend->used || !friend->pair_ready)
			continue;
		due = (int)(now - friend->hello_next) >= 0;
		/* (an answer to a hello is sent at once, but not to the same friend more often than every few seconds: two machines unsure of each other's key would
		otherwise answer each other for ever) */
		if (friend->reply_valid)
		{
			if (friend->hello_count && !social_elapsed(engine, friend->reply_time, SOCIAL_REPLY_SPACING))
				continue;
		}
		else if (friend->acknowledged || !due)
			continue;
		/* the acceptance before the hello, while the friend has not shown it knows this machine */
		if (friend->accept_sent && !friend->has_circle)
			send_request(engine, friend->code_bytes);
		if (send_hello(engine, friend, engine->rotated && friend->hello_count == 0 && !friend->reply_valid))
		{
			friend->hello_count++;
			friend->reply_valid = 0;
			friend->reply_time = now;
			friend->hello_next = now + (friend->hello_count < SOCIAL_HELLO_TRIES ? SOCIAL_HELLO_FIRST : SOCIAL_HELLO_LATER);
			engine->hello_last = now;
			engine->hello_valid = 1;
		}
		break;
	}
}
