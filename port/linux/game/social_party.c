/*
SOCIAL_PARTY.C

Parties of up to eight over the brokers (social_core.h; the design is reports/research/2026-11-07/design/social.md 4).

A party is a topic and a key made from a token that its members hold, and one leader. Everyone in it publishes a STATE every few seconds (who it is, whether it follows
the leader, where it is, and, for the leader, the party's list and the game it is hosting); a member is anyone the leader lists, and the leader lists the keys it invited, and
friends of its own while the party is open. Every message is signed with its sender's key, so a member cannot speak for another.

The leader is the one with the highest term. A leader silent for twelve seconds is replaced by the first member after it in the list that is still heard, with the term
one higher; a leader that leaves names its successor. Two claims of one term are settled by the place in the list. A member in the menus follows the leader's game when it
shows one: this file says when the game's thread is to join it (social_party_follow_wanted), and hears what came of it (social_engine_party_follow_report).
*/

#include "../src/p2p_internal.h"
#include "social_core.h"
#include "text_safe.h"

#include <stdio.h>
#include <string.h>

/* the flags of a STATE */
enum
{
	_state_leader = 1,
	_state_auto_follow = 2,
	_state_hosting = 4,
	_state_in_game = 8,
	_state_can_accept = 16,
};

/* where a member is (STATE's place) */
enum
{
	_place_menus,
	_place_joining,
	_place_internet_game,
	_place_local_game,
};

struct state_message
{
	unsigned char key[SOCIAL_KEY_SIZE];
	unsigned int term;
	unsigned int roster_index;
	unsigned int flags, place, follow_result, network_version;
	char name[SOCIAL_NAME_SIZE + 1];
	unsigned char tunnel_hash[SOCIAL_HASH_SIZE];
	unsigned int game_order;
	int game_present;
	struct social_join_target game;
	int roster_count;
	unsigned char roster[SOCIAL_MAX_PARTY_MEMBERS][SOCIAL_BLOCK_SIZE];
	int signed_size;
	const unsigned char *signature;
};

/* ---------- small things */

static void hash8(const unsigned char *key, unsigned char *out)
{
	unsigned char digest[P2P_SHA256_SIZE];

	p2p_sha256(key, SOCIAL_KEY_SIZE, digest);
	memcpy(out, digest, SOCIAL_BLOCK_SIZE);
}

static unsigned int member_id_of(const unsigned char *hash)
{
	unsigned int id = (unsigned int)hash[0] << 24 | (unsigned int)hash[1] << 16 | (unsigned int)hash[2] << 8 | hash[3];

	return id ? id : 1;
}

static void event_push(struct social_engine *engine, int type, const char *name, const char *text)
{
	struct social_event event;

	memset(&event, 0, sizeof(event));
	event.type = type;
	snprintf(event.name, sizeof(event.name), "%s", name ? name : "");
	snprintf(event.text, sizeof(event.text), "%s", text ? text : "");
	social_event_push(engine, &event);
}

static int member_index_by_key(const struct social_party *party, const unsigned char *key)
{
	int index;

	for (index = 0; index < SOCIAL_MAX_PARTY_MEMBERS; index++)
	{
		if (party->members[index].used && !memcmp(party->members[index].key, key, SOCIAL_KEY_SIZE))
			return index;
	}
	return -1;
}

static int member_index_by_hash8(const struct social_party *party, const unsigned char *hash)
{
	int index;

	for (index = 0; index < SOCIAL_MAX_PARTY_MEMBERS; index++)
	{
		if (party->members[index].used && !memcmp(party->members[index].hash, hash, SOCIAL_BLOCK_SIZE))
			return index;
	}
	return -1;
}

static int roster_position(const struct social_party *party, const unsigned char *hash)
{
	int index;

	for (index = 0; index < party->roster_count; index++)
	{
		if (!memcmp(party->roster[index], hash, SOCIAL_BLOCK_SIZE))
			return index;
	}
	return -1;
}

static int member_count(const struct social_party *party)
{
	int index, count = 0;

	for (index = 0; index < SOCIAL_MAX_PARTY_MEMBERS; index++)
		count += party->members[index].used;
	return count;
}

static int is_leader(const struct social_engine *engine)
{
	return engine->party.active && engine->party.leader >= 0 && engine->party.leader == engine->party.self;
}

static struct social_member *member_add(struct social_engine *engine, const unsigned char *key, const char *name)
{
	struct social_party *party = &engine->party;
	int index;

	for (index = 0; index < SOCIAL_MAX_PARTY_MEMBERS && party->members[index].used; index++)
		;
	if (index == SOCIAL_MAX_PARTY_MEMBERS)
		return NULL;
	memset(&party->members[index], 0, sizeof(party->members[index]));
	party->members[index].used = 1;
	memcpy(party->members[index].key, key, SOCIAL_KEY_SIZE);
	p2p_sha256(key, SOCIAL_KEY_SIZE, party->members[index].hash);
	party->members[index].id = member_id_of(party->members[index].hash);
	party->members[index].roster_index = 0xFF;
	snprintf(party->members[index].name, sizeof(party->members[index].name), "%s", name && name[0] ? name : "?");
	return &party->members[index];
}

/* a member that fell silent or was passed over is remembered, so that it is taken back if it speaks again (the two halves of a partition) */
static void remember_dropped(struct social_engine *engine, const struct social_member *member)
{
	struct social_party *party = &engine->party;
	int index, slot = 0;

	for (index = 0; index < SOCIAL_MAX_PARTY_MEMBERS; index++)
	{
		if (party->dropped_time[index] && !memcmp(party->dropped[index], member->key, SOCIAL_KEY_SIZE))
		{
			slot = index;
			break;
		}
		if (!party->dropped_time[index] || social_elapsed(engine, party->dropped_time[index], SOCIAL_PARTY_DROPPED_LIFE))
			slot = index;
	}
	memcpy(party->dropped[slot], member->key, SOCIAL_KEY_SIZE);
	party->dropped_time[slot] = social_now(engine) ? social_now(engine) : 1;
}

static int dropped_key(struct social_engine *engine, const unsigned char *key)
{
	int index;

	for (index = 0; index < SOCIAL_MAX_PARTY_MEMBERS; index++)
	{
		if (engine->party.dropped_time[index] && !social_elapsed(engine, engine->party.dropped_time[index], SOCIAL_PARTY_DROPPED_LIFE) &&
			!memcmp(engine->party.dropped[index], key, SOCIAL_KEY_SIZE))
		{
			return 1;
		}
	}
	return 0;
}

static int member_alive(struct social_engine *engine, const struct social_member *member)
{
	return member->used && member->heard && !social_elapsed(engine, member->heard_time, SOCIAL_PARTY_LEADER_SILENCE);
}

static void roster_remove(struct social_party *party, const unsigned char *hash)
{
	int position = roster_position(party, hash);

	if (position < 0)
		return;
	memmove(party->roster[position], party->roster[position + 1], (size_t)(party->roster_count - position - 1) * SOCIAL_BLOCK_SIZE);
	party->roster_count--;
}

static void party_clear(struct social_engine *engine)
{
	memset(&engine->party, 0, sizeof(engine->party));
	engine->party.leader = -1;
	engine->party.self = -1;
	engine->party.auto_follow = 1;
	social_topics_changed(engine);
	engine->version++;
}

static void party_start(struct social_engine *engine, const unsigned char *token)
{
	struct social_party *party = &engine->party;
	struct social_member *me;

	party_clear(engine);
	party->active = 1;
	memcpy(party->token, token, SOCIAL_TOKEN_SIZE);
	social_party_for(party->token, party->topic, party->seal);
	party->auto_follow = 1;
	me = member_add(engine, engine->identity.key, engine->name);
	party->self = (int)(me - party->members);
	me->heard = 1;
	me->heard_time = social_now(engine);
	social_topics_changed(engine);
	engine->version++;
}

/* ---------- messages */

static void put_signature(struct social_engine *engine, struct social_writer *writer)
{
	unsigned char signature[SOCIAL_SIGNATURE_SIZE];

	if (writer->bad)
		return;
	social_sign(&engine->identity, "hceu-f/party", engine->party.token, SOCIAL_TOKEN_SIZE, writer->bytes, writer->size, signature);
	social_put_bytes(writer, signature, SOCIAL_SIGNATURE_SIZE);
}

static int party_send(struct social_engine *engine, const unsigned char *plain, int size)
{
	return !engine->party.active ? 0 : social_send(engine, engine->party.topic, SOCIAL_KIND_PARTY, engine->party.seal, NULL, plain, size);
}

/* this machine's STATE */
static void state_send(struct social_engine *engine)
{
	struct social_party *party = &engine->party;
	struct social_member *me = &party->members[party->self];
	unsigned char plain[SOCIAL_MAX_PLAIN];
	struct social_writer writer;
	int leader = is_leader(engine);
	int flags = 0, index;

	if (!engine->generation_ready)
		return;
	if (leader)
		flags |= _state_leader;
	if (party->auto_follow)
		flags |= _state_auto_follow;
	if (engine->place.hosting)
		flags |= _state_hosting;
	if (engine->place.activity == SOCIAL_GAME)
		flags |= _state_in_game;
	if (engine->place.activity == SOCIAL_MENUS || engine->place.activity == SOCIAL_LOBBY)
		flags |= _state_can_accept;
	social_writer_start(&writer, plain, (int)sizeof(plain));
	social_put8(&writer, SOCIAL_TYPE_PARTY_STATE);
	social_put32(&writer, engine->generation);
	social_put32(&writer, social_next_sequence(engine));
	social_put_bytes(&writer, engine->identity.key, SOCIAL_KEY_SIZE);
	social_put16(&writer, party->term);
	if (leader)
		social_put8(&writer, party->claim_index);
	else
	{
		int position = roster_position(party, me->hash);

		social_put8(&writer, position < 0 ? 0xFF : (unsigned int)position);
	}
	social_put8(&writer, (unsigned int)flags);
	social_put8(&writer, engine->place.activity == SOCIAL_GAME ? (engine->place.hosting ? _place_internet_game : _place_local_game) : party->follow_running ? _place_joining :
		_place_menus);
	social_put8(&writer, me->follow_result);
	social_put16(&writer, (unsigned int)engine->env.network_version);
	social_put_text(&writer, engine->name, SOCIAL_NAME_SIZE);
	/* (a hold room's key hashes, later: zeros) */
	for (index = 0; index < SOCIAL_HASH_SIZE; index++)
		social_put8(&writer, 0);
	if (leader)
	{
		social_put16(&writer, party->game_order);
		social_put8(&writer, (unsigned int)party->game_present);
		if (party->game_present)
		{
			social_put_bytes(&writer, party->game.host_hash, SOCIAL_HASH_SIZE);
			social_put_bytes(&writer, party->game.token, SOCIAL_TOKEN_SIZE);
			social_put_text(&writer, party->game.map, SOCIAL_MAP_SIZE);
			social_put_text(&writer, party->game.gametype, SOCIAL_GAMETYPE_SIZE);
			social_put8(&writer, party->game.players);
			social_put8(&writer, party->game.maximum_players);
		}
		social_put8(&writer, (unsigned int)party->roster_count);
		for (index = 0; index < party->roster_count; index++)
			social_put_bytes(&writer, party->roster[index], SOCIAL_BLOCK_SIZE);
	}
	put_signature(engine, &writer);
	if (!writer.bad && party_send(engine, plain, writer.size))
	{
		party->state_time = social_now(engine);
		party->state_dirty = 0;
	}
}

static int state_decode(const unsigned char *plain, int size, struct state_message *state)
{
	struct social_reader reader;
	int index;

	memset(state, 0, sizeof(*state));
	social_reader_start(&reader, plain, size);
	reader.offset = SOCIAL_INNER_HEADER;
	social_get_bytes(&reader, state->key, SOCIAL_KEY_SIZE);
	state->term = social_get16(&reader);
	state->roster_index = social_get8(&reader);
	state->flags = social_get8(&reader);
	state->place = social_get8(&reader);
	state->follow_result = social_get8(&reader);
	state->network_version = social_get16(&reader);
	social_get_text(&reader, state->name, (int)sizeof(state->name), SOCIAL_NAME_SIZE);
	social_get_bytes(&reader, state->tunnel_hash, SOCIAL_HASH_SIZE);
	if (state->flags & _state_leader)
	{
		state->game_order = social_get16(&reader);
		state->game_present = (int)social_get8(&reader);
		if (state->game_present > 1)
			return 0;
		if (state->game_present)
		{
			social_get_bytes(&reader, state->game.host_hash, SOCIAL_HASH_SIZE);
			social_get_bytes(&reader, state->game.token, SOCIAL_TOKEN_SIZE);
			social_get_text(&reader, state->game.map, (int)sizeof(state->game.map), SOCIAL_MAP_SIZE);
			social_get_text(&reader, state->game.gametype, (int)sizeof(state->game.gametype), SOCIAL_GAMETYPE_SIZE);
			state->game.players = (unsigned char)social_get8(&reader);
			state->game.maximum_players = (unsigned char)social_get8(&reader);
		}
		state->roster_count = (int)social_get8(&reader);
		if (state->roster_count > SOCIAL_MAX_PARTY_MEMBERS)
			return 0;
		for (index = 0; index < state->roster_count; index++)
			social_get_bytes(&reader, state->roster[index], SOCIAL_BLOCK_SIZE);
	}
	state->signed_size = reader.offset;
	state->signature = plain + reader.offset;
	reader.offset += SOCIAL_SIGNATURE_SIZE;
	if (reader.offset > reader.size)
		reader.bad = 1;
	if (reader.bad || reader.offset != reader.size)
		return 0;
	if (!state->name[0])
		strcpy(state->name, "?");
	return 1;
}

/* a message with a key, a byte or two and the signature: LEAVE (key, successor), KICK (key), DISBAND (term), TRANSFER (hash, term) */
static void simple_send(struct social_engine *engine, int type, const unsigned char *key, int byte, int term, const unsigned char *hash)
{
	unsigned char plain[SOCIAL_MAX_PLAIN];
	struct social_writer writer;

	if (!engine->generation_ready)
		return;
	social_writer_start(&writer, plain, (int)sizeof(plain));
	social_put8(&writer, (unsigned int)type);
	social_put32(&writer, engine->generation);
	social_put32(&writer, social_next_sequence(engine));
	switch (type)
	{
	case SOCIAL_TYPE_PARTY_LEAVE:
		social_put_bytes(&writer, key, SOCIAL_KEY_SIZE);
		social_put8(&writer, (unsigned int)byte);
		break;
	case SOCIAL_TYPE_PARTY_KICK:
		social_put_bytes(&writer, key, SOCIAL_KEY_SIZE);
		break;
	case SOCIAL_TYPE_PARTY_DISBAND:
		social_put16(&writer, (unsigned int)term);
		break;
	case SOCIAL_TYPE_PARTY_TRANSFER:
		social_put_bytes(&writer, hash, SOCIAL_BLOCK_SIZE);
		social_put16(&writer, (unsigned int)term);
		break;
	}
	put_signature(engine, &writer);
	if (!writer.bad)
		party_send(engine, plain, writer.size);
}

/* ---------- the calls */

void social_engine_party_result(struct social_engine *engine, int result)
{
	engine->party_result = result;
	engine->party_serial++;
	engine->version++;
}


int social_engine_party_make(struct social_engine *engine)
{
	unsigned char token[SOCIAL_TOKEN_SIZE];

	if (!engine->enabled || !engine->has_identity)
		return SOCIAL_PARTY_OFF;
	if (engine->party.active)
		return SOCIAL_PARTY_ALREADY_IN_PARTY;
	engine->env.random(engine->env.user, token, SOCIAL_TOKEN_SIZE);
	party_start(engine, token);
	engine->party.leader = engine->party.self;
	engine->party.term = 1;
	memcpy(engine->party.roster[0], engine->party.members[engine->party.self].hash, SOCIAL_BLOCK_SIZE);
	engine->party.roster_count = 1;
	engine->party.state_dirty = 1;
	social_log(engine, "party made");
	event_push(engine, SOCIAL_EVENT_PARTY_JOINED, engine->name, "");
	return SOCIAL_PARTY_OK;
}

int social_engine_party_invite(struct social_engine *engine, unsigned int friend_id)
{
	struct social_party *party = &engine->party;
	struct social_friend *friend = (struct social_friend *)social_friend_by_id(engine, friend_id);
	struct social_invite invite;
	unsigned char plain[SOCIAL_MAX_PLAIN];
	int size, slot = -1, index;

	if (!engine->enabled || !engine->has_identity)
		return SOCIAL_PARTY_OFF;
	if (!party->active)
		return SOCIAL_PARTY_NOT_IN_PARTY;
	if (!is_leader(engine))
		return SOCIAL_PARTY_NOT_LEADER;
	if (!friend)
		return SOCIAL_PARTY_NO_FRIEND;
	if (!social_friend_online(engine, friend))
		return SOCIAL_PARTY_OFFLINE;
	if (member_count(party) >= SOCIAL_MAX_PARTY_MEMBERS)
		return SOCIAL_PARTY_FULL;
	if (friend->invite_valid && !social_elapsed(engine, friend->invite_time, SOCIAL_INVITE_SPACING))
		return SOCIAL_PARTY_TOO_FAST;
	if (!engine->generation_ready || !social_friend_pair(engine, friend))
		return SOCIAL_PARTY_TOO_FAST;
	memset(&invite, 0, sizeof(invite));
	invite.kind = 2;
	memcpy(invite.party_token, party->token, SOCIAL_TOKEN_SIZE);
	invite.party_size = (unsigned char)member_count(party);
	size = social_invite_encode(engine->generation, social_next_sequence(engine), &invite, plain, (int)sizeof(plain));
	if (size <= 0 || !social_send(engine, engine->circle_topic, SOCIAL_KIND_DIRECT, friend->pair_key, friend->tag_out, plain, size))
		return SOCIAL_PARTY_TOO_FAST;
	friend->invite_time = social_now(engine);
	friend->invite_valid = 1;
	/* its key is let in when it asks (two minutes) */
	for (index = 0; index < SOCIAL_MAX_PARTY_MEMBERS; index++)
	{
		if (!party->invited_time[index] || social_elapsed(engine, party->invited_time[index], SOCIAL_PARTY_INVITE_LIFE) ||
			!memcmp(party->invited[index], friend->key, SOCIAL_KEY_SIZE))
		{
			slot = index;
			if (!memcmp(party->invited[index], friend->key, SOCIAL_KEY_SIZE))
				break;
		}
	}
	if (slot < 0)
		slot = 0;
	memcpy(party->invited[slot], friend->key, SOCIAL_KEY_SIZE);
	party->invited_time[slot] = social_now(engine) ? social_now(engine) : 1;
	social_log(engine, "invited %s to the party", friend->code);
	return SOCIAL_PARTY_OK;
}

int social_engine_party_join(struct social_engine *engine, const unsigned char *token)
{
	struct social_party *party = &engine->party;

	if (!engine->enabled || !engine->has_identity)
		return SOCIAL_PARTY_OFF;
	if (party->active)
		return SOCIAL_PARTY_ALREADY_IN_PARTY;
	party_start(engine, token);
	party->joining = 1;
	party->join_time = social_now(engine);
	party->state_dirty = 1;
	social_log(engine, "asking to join a party");
	return SOCIAL_PARTY_OK;
}

int social_engine_party_join_friend(struct social_engine *engine, unsigned int friend_id)
{
	const struct social_friend *friend = social_friend_by_id(engine, friend_id);

	if (!engine->enabled || !engine->has_identity)
		return SOCIAL_PARTY_OFF;
	if (!friend)
		return SOCIAL_PARTY_NO_FRIEND;
	if (!social_friend_online(engine, friend) || !(friend->presence.flags & SOCIAL_PRESENCE_PARTY_OPEN))
		return SOCIAL_PARTY_OFFLINE;
	return social_engine_party_join(engine, friend->presence.party_token);
}

static void left(struct social_engine *engine, const char *why)
{
	event_push(engine, SOCIAL_EVENT_PARTY_LEFT, engine->name, why);
	social_log(engine, "left the party: %s", why);
	party_clear(engine);
}

int social_engine_party_leave(struct social_engine *engine)
{
	struct social_party *party = &engine->party;
	int successor = 0xFF;

	if (!party->active)
		return SOCIAL_PARTY_NOT_IN_PARTY;
	if (party->joining)
	{
		left(engine, "left");
		return SOCIAL_PARTY_OK;
	}
	if (is_leader(engine))
	{
		int index;

		/* the next in the list after this machine, that is heard */
		for (index = 0; index < party->roster_count; index++)
		{
			int member = member_index_by_hash8(party, party->roster[index]);

			if (member >= 0 && member != party->self && party->members[member].heard)
			{
				successor = index;
				break;
			}
		}
	}
	simple_send(engine, SOCIAL_TYPE_PARTY_LEAVE, engine->identity.key, successor, 0, NULL);
	/* (the message is in the queue; the party is dropped at once) */
	left(engine, "left");
	return SOCIAL_PARTY_OK;
}

/* the leader ends the party: a signed DISBAND that every member hears (at the term the leader holds), and the party is dropped here at once */
int social_engine_party_disband(struct social_engine *engine)
{
	struct social_party *party = &engine->party;

	if (!party->active)
		return SOCIAL_PARTY_NOT_IN_PARTY;
	if (party->joining || !is_leader(engine))
		return SOCIAL_PARTY_NOT_LEADER;
	simple_send(engine, SOCIAL_TYPE_PARTY_DISBAND, NULL, 0, party->term, NULL);
	left(engine, "disbanded");
	return SOCIAL_PARTY_OK;
}

int social_engine_party_kick(struct social_engine *engine, unsigned int member_id)
{
	struct social_party *party = &engine->party;
	int index;

	if (!party->active)
		return SOCIAL_PARTY_NOT_IN_PARTY;
	if (!is_leader(engine))
		return SOCIAL_PARTY_NOT_LEADER;
	for (index = 0; index < SOCIAL_MAX_PARTY_MEMBERS; index++)
	{
		struct social_member *member = &party->members[index];

		if (member->used && member->id == member_id && index != party->self)
		{
			simple_send(engine, SOCIAL_TYPE_PARTY_KICK, member->key, 0, 0, NULL);
			if (party->banned_count < SOCIAL_MAX_PARTY_MEMBERS)
				memcpy(party->banned[party->banned_count++], member->hash, SOCIAL_BLOCK_SIZE);
			roster_remove(party, member->hash);
			social_log(engine, "kicked %s", member->name);
			memset(member, 0, sizeof(*member));
			party->state_dirty = 1;
			engine->version++;
			return SOCIAL_PARTY_OK;
		}
	}
	return SOCIAL_PARTY_NO_SUCH_MEMBER;
}

int social_engine_party_transfer(struct social_engine *engine, unsigned int member_id)
{
	struct social_party *party = &engine->party;
	int index;

	if (!party->active)
		return SOCIAL_PARTY_NOT_IN_PARTY;
	if (!is_leader(engine))
		return SOCIAL_PARTY_NOT_LEADER;
	for (index = 0; index < SOCIAL_MAX_PARTY_MEMBERS; index++)
	{
		struct social_member *member = &party->members[index];

		if (member->used && member->id == member_id && index != party->self && member->heard)
		{
			party->term++;
			simple_send(engine, SOCIAL_TYPE_PARTY_TRANSFER, NULL, 0, party->term, member->hash);
			party->leader = index;
			party->leader_heard = social_now(engine);
			party->state_dirty = 1;
			engine->version++;
			social_log(engine, "handed the lead to %s", member->name);
			return SOCIAL_PARTY_OK;
		}
	}
	return SOCIAL_PARTY_NO_SUCH_MEMBER;
}

int social_engine_party_set_open(struct social_engine *engine, int open)
{
	if (!engine->party.active)
		return SOCIAL_PARTY_NOT_IN_PARTY;
	if (!is_leader(engine))
		return SOCIAL_PARTY_NOT_LEADER;
	engine->party.open = !!open;
	engine->version++;
	return SOCIAL_PARTY_OK;
}

int social_engine_party_set_auto_follow(struct social_engine *engine, int on)
{
	if (!engine->party.active)
		return SOCIAL_PARTY_NOT_IN_PARTY;
	engine->party.auto_follow = !!on;
	engine->party.state_dirty = 1;
	if (!on)
		engine->party.members[engine->party.self].follow_result = SOCIAL_FOLLOW_OPTED_OUT;
	else if (engine->party.members[engine->party.self].follow_result == SOCIAL_FOLLOW_OPTED_OUT)
		engine->party.members[engine->party.self].follow_result = SOCIAL_FOLLOW_NONE;
	engine->version++;
	return SOCIAL_PARTY_OK;
}

static void chat_add(struct social_engine *engine, const char *name, const char *text)
{
	struct social_party *party = &engine->party;
	int index = (party->chat_first + party->chat_count) % 8;

	if (party->chat_count == 8)
	{
		party->chat_first = (party->chat_first + 1) % 8;
		index = (party->chat_first + 7) % 8;
		party->chat_count--;
	}
	snprintf(party->chat[index].name, sizeof(party->chat[index].name), "%s", name);
	snprintf(party->chat[index].text, sizeof(party->chat[index].text), "%s", text);
	party->chat[index].time = social_now(engine);
	party->chat_count++;
	engine->version++;
}

int social_engine_party_chat(struct social_engine *engine, const char *text)
{
	struct social_party *party = &engine->party;
	unsigned char plain[SOCIAL_MAX_PLAIN];
	struct social_writer writer;
	char clean[SOCIAL_CHAT_SIZE + 1];
	unsigned int now = social_now(engine);
	int index, recent = 0;

	if (!party->active || party->joining)
		return SOCIAL_PARTY_NOT_IN_PARTY;
	text_safe_text(clean, (short)sizeof(clean), text ? text : "", (short)(text ? strlen(text) : 0), SOCIAL_CHAT_SIZE);
	if (!clean[0])
		return SOCIAL_PARTY_BAD_TEXT;
	/* one a second, three at once */
	for (index = 0; index < party->chat_sent_count; index++)
		recent += (unsigned int)(now - party->chat_sent[index]) < 3000;
	if (recent >= 3 || (party->chat_sent_count && (unsigned int)(now - party->chat_sent[party->chat_sent_count - 1]) < 300))
		return SOCIAL_PARTY_TOO_FAST;
	if (!engine->generation_ready)
		return SOCIAL_PARTY_TOO_FAST;
	social_writer_start(&writer, plain, (int)sizeof(plain));
	social_put8(&writer, SOCIAL_TYPE_PARTY_CHAT);
	social_put32(&writer, engine->generation);
	social_put32(&writer, social_next_sequence(engine));
	social_put_bytes(&writer, engine->identity.key, SOCIAL_KEY_SIZE);
	social_put_text(&writer, clean, SOCIAL_CHAT_SIZE);
	put_signature(engine, &writer);
	if (writer.bad || !party_send(engine, plain, writer.size))
		return SOCIAL_PARTY_TOO_FAST;
	if (party->chat_sent_count == 3)
	{
		party->chat_sent[0] = party->chat_sent[1];
		party->chat_sent[1] = party->chat_sent[2];
		party->chat_sent_count--;
	}
	party->chat_sent[party->chat_sent_count++] = now;
	chat_add(engine, engine->name, clean);
	return SOCIAL_PARTY_OK;
}

/* ---------- receiving */

/* the signer of a message: a member (or, for a STATE, perhaps a new one); its signature checked within the budget */
static int check_signature(struct social_engine *engine, const unsigned char *key, const unsigned char *plain, int signed_size, const unsigned char *signature)
{
	if (!social_verify_budget(engine))
	{
		engine->statistics.dropped_verify_budget++;
		return 0;
	}
	engine->statistics.verifications++;
	if (!social_verify("hceu-f/party", engine->party.token, SOCIAL_TOKEN_SIZE, key, plain, signed_size, signature))
	{
		engine->statistics.dropped_signature++;
		return 0;
	}
	return 1;
}

/* this machine leads now, from the term (the successor of a leader that left or fell silent) */
static void claim(struct social_engine *engine, unsigned int term)
{
	struct social_party *party = &engine->party;
	int index;

	party->term = (unsigned short)term;
	party->leader = party->self;
	party->leader_heard = social_now(engine);
	{
		int position = roster_position(party, party->members[party->self].hash);

		party->claim_index = position < 0 ? 0xFE : (unsigned int)position;
	}
	/* (the list as this machine has it: those it hears, in the order the old leader gave) */
	{
		unsigned char kept[SOCIAL_MAX_PARTY_MEMBERS][SOCIAL_BLOCK_SIZE];
		int count = 0;

		for (index = 0; index < party->roster_count; index++)
		{
			int member = member_index_by_hash8(party, party->roster[index]);

			if (member >= 0 && (member == party->self || member_alive(engine, &party->members[member])))
				memcpy(kept[count++], party->roster[index], SOCIAL_BLOCK_SIZE);
		}
		memcpy(party->roster, kept, sizeof(kept));
		party->roster_count = count;
	}
	if (roster_position(party, party->members[party->self].hash) < 0 && party->roster_count < SOCIAL_MAX_PARTY_MEMBERS)
		memcpy(party->roster[party->roster_count++], party->members[party->self].hash, SOCIAL_BLOCK_SIZE);
	for (index = 0; index < SOCIAL_MAX_PARTY_MEMBERS; index++)
	{
		if (party->members[index].used && index != party->self && roster_position(party, party->members[index].hash) < 0)
		{
			remember_dropped(engine, &party->members[index]);
			memset(&party->members[index], 0, sizeof(party->members[index]));
		}
	}
	party->state_dirty = 1;
	party->state_time = social_now(engine) - SOCIAL_PARTY_CHANGE_SPACING;
	event_push(engine, SOCIAL_EVENT_PARTY_LEADER, engine->name, "");
	social_log(engine, "this machine leads the party now (term %u)", term);
	engine->version++;
}

static int invited_key(struct social_engine *engine, const unsigned char *key)
{
	int index;

	for (index = 0; index < SOCIAL_MAX_PARTY_MEMBERS; index++)
	{
		if (engine->party.invited_time[index] && !social_elapsed(engine, engine->party.invited_time[index], SOCIAL_PARTY_INVITE_LIFE) &&
			!memcmp(engine->party.invited[index], key, SOCIAL_KEY_SIZE))
		{
			return 1;
		}
	}
	return 0;
}

static void state_heard(struct social_engine *engine, const unsigned char *plain, int size, const struct social_inner *inner)
{
	struct social_party *party = &engine->party;
	struct state_message state;
	struct social_member *member;
	int index, known;
	unsigned char hash[SOCIAL_BLOCK_SIZE];

	if (!state_decode(plain, size, &state))
	{
		engine->statistics.dropped_malformed++;
		return;
	}
	if (!memcmp(state.key, engine->identity.key, SOCIAL_KEY_SIZE))
		return;
	hash8(state.key, hash);
	index = member_index_by_key(party, state.key);
	known = index >= 0;
	if (known && !social_window_check(&party->members[index].window, inner->generation, inner->sequence))
	{
		engine->statistics.dropped_window++;
		return;
	}
	for (known = 0; known < party->banned_count; known++)
	{
		if (!memcmp(party->banned[known], hash, SOCIAL_BLOCK_SIZE))
		{
			engine->statistics.dropped_blocked++;
			return;
		}
	}
	known = index >= 0;
	/* a stranger to this machine: the leader lists a key it invited, or a friend's while the party is open; a member lists those the leader does */
	if (!known)
	{
		int admit = 0;

		if (is_leader(engine))
			admit = invited_key(engine, state.key) || (party->open && social_friend_by_key(engine, state.key)) || dropped_key(engine, state.key);
		else if (party->leader >= 0 && roster_position(party, hash) >= 0)
			admit = 1;
		else if (party->joining && (state.flags & _state_leader))
			admit = 1;
		/* a member passed over that speaks as the leader: the other half of a partition, which healed */
		if (!admit && (state.flags & _state_leader) && dropped_key(engine, state.key))
			admit = 1;
		if (!admit)
			return;
		if (member_count(party) >= SOCIAL_MAX_PARTY_MEMBERS)
		{
			engine->statistics.dropped_blocked++;
			return;
		}
	}
	if (!check_signature(engine, state.key, plain, state.signed_size, state.signature))
		return;
	if (!known)
	{
		member = member_add(engine, state.key, state.name);
		if (!member)
			return;
		index = (int)(member - party->members);
		if (is_leader(engine) && roster_position(party, member->hash) < 0 && party->roster_count < SOCIAL_MAX_PARTY_MEMBERS)
		{
			memcpy(party->roster[party->roster_count++], member->hash, SOCIAL_BLOCK_SIZE);
			party->state_dirty = 1;
			social_log(engine, "%s joined the party", member->name);
		}
	}
	member = &party->members[index];
	social_window_mark(&member->window, inner->generation, inner->sequence);
	engine->statistics.accepted++;
	snprintf(member->name, sizeof(member->name), "%s", state.name);
	member->heard = 1;
	member->heard_time = social_now(engine);
	member->flags = (unsigned char)state.flags;
	member->place = (unsigned char)state.place;
	member->follow_result = (unsigned char)state.follow_result;
	member->net_version = (unsigned short)state.network_version;
	member->term = (unsigned short)state.term;
	member->roster_index = (unsigned char)state.roster_index;
	engine->version++;
	if (!(state.flags & _state_leader))
		return;
	/* a leader's word: the highest term leads; of two claims of one term, the lower place in the list */
	if (state.term < party->term)
		return;
	if (state.term == party->term && party->leader >= 0 && party->leader != index)
	{
		unsigned int current = party->leader == party->self ? party->claim_index : party->members[party->leader].roster_index;

		if (state.roster_index > current || (state.roster_index == current && memcmp(state.key, party->members[party->leader].key, SOCIAL_KEY_SIZE) > 0))
			return;
	}
	if (party->leader != index || state.term > party->term)
	{
		if (party->leader == party->self)
			social_log(engine, "%s leads the party (term %u); this machine steps down", member->name, state.term);
		party->leader = index;
		event_push(engine, SOCIAL_EVENT_PARTY_LEADER, member->name, "");
	}
	party->term = (unsigned short)state.term;
	party->leader_heard = social_now(engine);
	/* the leader's list is the party's: a member it does not list is gone, one it does list that this machine has not heard is waited for */
	party->roster_count = state.roster_count;
	memcpy(party->roster, state.roster, sizeof(party->roster));
	for (index = 0; index < SOCIAL_MAX_PARTY_MEMBERS; index++)
	{
		struct social_member *other = &party->members[index];

		if (other->used && index != party->self && index != party->leader && roster_position(party, other->hash) < 0)
		{
			remember_dropped(engine, other);
			memset(other, 0, sizeof(*other));
		}
	}
	if (party->game_present != state.game_present || party->game_order != state.game_order ||
		(state.game_present && memcmp(party->game.token, state.game.token, SOCIAL_TOKEN_SIZE)))
	{
		party->game_order = (unsigned short)state.game_order;
		party->game_present = state.game_present;
		party->game = state.game;
		party->game.network_version = (unsigned short)state.network_version;
		party->game.friend_id = member->id;
		snprintf(party->game.name, sizeof(party->game.name), "%s", member->name);
		party->game.invited = 1;
		party->follow_done = 0;
		party->follow_running = 0;
		party->follow_tries = 0;
		party->follow_final_retry = 0;
		party->follow_retry_wait = 0;
		if (party->members[party->self].follow_result != SOCIAL_FOLLOW_OPTED_OUT)
			party->members[party->self].follow_result = SOCIAL_FOLLOW_NONE;
	}
	/* the leader's list leaves this machine out (it led, fell silent, and the party went on without it): it asks to be listed again, and is if the new leader lets it
	in; otherwise it gives up */
	if (!party->joining && roster_position(party, party->members[party->self].hash) < 0)
	{
		party->joining = 1;
		party->join_time = social_now(engine);
		party->state_dirty = 1;
		social_log(engine, "the party's list leaves this machine out: asking to be listed");
	}
	if (party->joining && roster_position(party, party->members[party->self].hash) >= 0)
	{
		party->joining = 0;
		event_push(engine, SOCIAL_EVENT_PARTY_JOINED, engine->name, "");
		social_log(engine, "joined the party led by %s", member->name);
		}
}

static void leave_heard(struct social_engine *engine, const unsigned char *plain, int size, const struct social_inner *inner)
{
	struct social_party *party = &engine->party;
	struct social_reader reader;
	unsigned char key[SOCIAL_KEY_SIZE];
	unsigned int successor;
	const unsigned char *signature;
	int index, signed_size;
	int was_leader;

	social_reader_start(&reader, plain, size);
	reader.offset = SOCIAL_INNER_HEADER;
	social_get_bytes(&reader, key, SOCIAL_KEY_SIZE);
	successor = social_get8(&reader);
	signed_size = reader.offset;
	signature = plain + reader.offset;
	reader.offset += SOCIAL_SIGNATURE_SIZE;
	if (reader.bad || reader.offset != size)
	{
		engine->statistics.dropped_malformed++;
		return;
	}
	index = member_index_by_key(party, key);
	if (index < 0 || index == party->self || !social_window_check(&party->members[index].window, inner->generation, inner->sequence) ||
		!check_signature(engine, key, plain, signed_size, signature))
	{
		return;
	}
	social_window_mark(&party->members[index].window, inner->generation, inner->sequence);
	engine->statistics.accepted++;
	was_leader = index == party->leader;
	social_log(engine, "%s left the party", party->members[index].name);
	roster_remove(party, party->members[index].hash);
	memset(&party->members[index], 0, sizeof(party->members[index]));
	if (was_leader)
	{
		party->leader = -1;
		/* the one it named leads at once */
		if (successor < (unsigned int)party->roster_count && !memcmp(party->roster[successor], party->members[party->self].hash, SOCIAL_BLOCK_SIZE))
			claim(engine, (unsigned int)party->term + 1);
	}
	else if (is_leader(engine))
		party->state_dirty = 1;
	engine->version++;
}

static void kick_heard(struct social_engine *engine, const unsigned char *plain, int size, const struct social_inner *inner)
{
	struct social_party *party = &engine->party;
	struct social_reader reader;
	unsigned char key[SOCIAL_KEY_SIZE];
	const unsigned char *signature;
	int index, signed_size;

	social_reader_start(&reader, plain, size);
	reader.offset = SOCIAL_INNER_HEADER;
	social_get_bytes(&reader, key, SOCIAL_KEY_SIZE);
	signed_size = reader.offset;
	signature = plain + reader.offset;
	reader.offset += SOCIAL_SIGNATURE_SIZE;
	if (reader.bad || reader.offset != size)
	{
		engine->statistics.dropped_malformed++;
		return;
	}
	/* only the leader kicks */
	if (party->leader < 0 || party->leader == party->self || !social_window_check(&party->members[party->leader].window, inner->generation, inner->sequence) ||
		!check_signature(engine, party->members[party->leader].key, plain, signed_size, signature))
	{
		return;
	}
	social_window_mark(&party->members[party->leader].window, inner->generation, inner->sequence);
	engine->statistics.accepted++;
	if (!memcmp(key, engine->identity.key, SOCIAL_KEY_SIZE))
	{
		event_push(engine, SOCIAL_EVENT_PARTY_KICKED, party->members[party->leader].name, "");
		social_log(engine, "kicked from the party");
		party_clear(engine);
		return;
	}
	index = member_index_by_key(party, key);
	if (index >= 0)
	{
		roster_remove(party, party->members[index].hash);
		memset(&party->members[index], 0, sizeof(party->members[index]));
		engine->version++;
	}
}

static void disband_heard(struct social_engine *engine, const unsigned char *plain, int size, const struct social_inner *inner)
{
	struct social_party *party = &engine->party;
	struct social_reader reader;
	const unsigned char *signature;
	unsigned int term;
	int signed_size;

	social_reader_start(&reader, plain, size);
	reader.offset = SOCIAL_INNER_HEADER;
	term = social_get16(&reader);
	signed_size = reader.offset;
	signature = plain + reader.offset;
	reader.offset += SOCIAL_SIGNATURE_SIZE;
	if (reader.bad || reader.offset != size)
	{
		engine->statistics.dropped_malformed++;
		return;
	}
	if (party->leader < 0 || party->leader == party->self || term < party->term ||
		!social_window_check(&party->members[party->leader].window, inner->generation, inner->sequence) ||
		!check_signature(engine, party->members[party->leader].key, plain, signed_size, signature))
	{
		return;
	}
	engine->statistics.accepted++;
	left(engine, "disbanded");
}

static void transfer_heard(struct social_engine *engine, const unsigned char *plain, int size, const struct social_inner *inner)
{
	struct social_party *party = &engine->party;
	struct social_reader reader;
	unsigned char hash[SOCIAL_BLOCK_SIZE];
	const unsigned char *signature;
	unsigned int term;
	int signed_size, target;

	social_reader_start(&reader, plain, size);
	reader.offset = SOCIAL_INNER_HEADER;
	social_get_bytes(&reader, hash, SOCIAL_BLOCK_SIZE);
	term = social_get16(&reader);
	signed_size = reader.offset;
	signature = plain + reader.offset;
	reader.offset += SOCIAL_SIGNATURE_SIZE;
	if (reader.bad || reader.offset != size)
	{
		engine->statistics.dropped_malformed++;
		return;
	}
	if (party->leader < 0 || party->leader == party->self || term <= party->term ||
		!social_window_check(&party->members[party->leader].window, inner->generation, inner->sequence) ||
		!check_signature(engine, party->members[party->leader].key, plain, signed_size, signature))
	{
		return;
	}
	social_window_mark(&party->members[party->leader].window, inner->generation, inner->sequence);
	engine->statistics.accepted++;
	target = member_index_by_hash8(party, hash);
	if (target < 0)
		return;
	party->term = (unsigned short)term;
	party->leader_heard = social_now(engine);
	if (target == party->self)
		claim(engine, term);
	else
	{
		party->leader = target;
		event_push(engine, SOCIAL_EVENT_PARTY_LEADER, party->members[target].name, "");
	}
	engine->version++;
}

static void chat_heard(struct social_engine *engine, const unsigned char *plain, int size, const struct social_inner *inner)
{
	struct social_party *party = &engine->party;
	struct social_reader reader;
	unsigned char key[SOCIAL_KEY_SIZE];
	char text[SOCIAL_CHAT_SIZE + 1];
	const unsigned char *signature;
	struct social_member *member;
	int index, signed_size;

	social_reader_start(&reader, plain, size);
	reader.offset = SOCIAL_INNER_HEADER;
	social_get_bytes(&reader, key, SOCIAL_KEY_SIZE);
	social_get_text(&reader, text, (int)sizeof(text), SOCIAL_CHAT_SIZE);
	signed_size = reader.offset;
	signature = plain + reader.offset;
	reader.offset += SOCIAL_SIGNATURE_SIZE;
	if (reader.bad || reader.offset != size || !text[0])
	{
		engine->statistics.dropped_malformed++;
		return;
	}
	index = member_index_by_key(party, key);
	if (index < 0 || index == party->self)
		return;
	member = &party->members[index];
	if (!social_window_check(&member->window, inner->generation, inner->sequence))
	{
		engine->statistics.dropped_window++;
		return;
	}
	/* a member's lines: four a second */
	if (social_elapsed(engine, member->chat_start, 1000))
	{
		member->chat_start = social_now(engine);
		member->chat_count = 0;
	}
	if (++member->chat_count > SOCIAL_PARTY_CHAT_RATE)
	{
		engine->statistics.dropped_rate++;
		return;
	}
	if (!check_signature(engine, key, plain, signed_size, signature))
		return;
	social_window_mark(&member->window, inner->generation, inner->sequence);
	engine->statistics.accepted++;
	chat_add(engine, member->name, text);
	{
		struct social_event event;

		memset(&event, 0, sizeof(event));
		event.type = SOCIAL_EVENT_PARTY_CHAT;
		event.id = member->id;
		snprintf(event.name, sizeof(event.name), "%s", member->name);
		snprintf(event.text, sizeof(event.text), "%s", text);
		social_event_push(engine, &event);
	}
}

void social_party_heard(struct social_engine *engine, const char *topic, const unsigned char *plain, int size, const struct social_inner *inner)
{
	(void)topic;
	if (!engine->party.active)
		return;
	switch (inner->type)
	{
	case SOCIAL_TYPE_PARTY_STATE:
		state_heard(engine, plain, size, inner);
		break;
	case SOCIAL_TYPE_PARTY_LEAVE:
		leave_heard(engine, plain, size, inner);
		break;
	case SOCIAL_TYPE_PARTY_KICK:
		kick_heard(engine, plain, size, inner);
		break;
	case SOCIAL_TYPE_PARTY_DISBAND:
		disband_heard(engine, plain, size, inner);
		break;
	case SOCIAL_TYPE_PARTY_TRANSFER:
		transfer_heard(engine, plain, size, inner);
		break;
	case SOCIAL_TYPE_PARTY_CHAT:
		chat_heard(engine, plain, size, inner);
		break;
	default:
		engine->statistics.dropped_unknown++;
		break;
	}
}

/* ---------- following the leader's game */

int social_party_follow_wanted(const struct social_engine *engine, struct social_join_target *target, unsigned int *order)
{
	const struct social_party *party = &engine->party;
	struct social_engine *writable = (struct social_engine *)engine;

	if (!party->active || party->joining || is_leader(engine) || !party->auto_follow || !party->game_present || party->follow_done || party->follow_running)
		return 0;
	/* only from the menus: a machine in a game is not moved out of it */
	if (engine->place.activity != SOCIAL_MENUS && engine->place.activity != SOCIAL_BUSY && engine->place.activity != SOCIAL_LOBBY)
		return 0;
	if (party->follow_retry_wait && !social_elapsed(writable, party->follow_time, party->follow_final_retry ? SOCIAL_PARTY_FOLLOW_LAST : SOCIAL_PARTY_FOLLOW_RETRY))
		return 0;
	*target = party->game;
	*order = party->game_order;
	return 1;
}

int social_engine_party_follow_report(struct social_engine *engine, unsigned int order, int result)
{
	struct social_party *party = &engine->party;
	struct social_member *me;

	if (!party->active || party->joining || order != party->game_order)
		return SOCIAL_PARTY_NOT_IN_PARTY;
	me = &party->members[party->self];
	if (result < 0 || result >= NUMBER_OF_SOCIAL_FOLLOWS)
		result = SOCIAL_FOLLOW_NO_ANSWER;
	if (me->follow_result != result)
	{
		me->follow_result = (unsigned char)result;
		party->state_dirty = 1;
	}
	party->follow_time = social_now(engine);
	switch (result)
	{
	case SOCIAL_FOLLOW_JOINING:
		party->follow_running = 1;
		party->follow_retry_wait = 0;
		break;
	case SOCIAL_FOLLOW_JOINED:
		party->follow_running = 0;
		party->follow_done = 1;
		party->follow_done_order = (unsigned short)order;
		break;
	case SOCIAL_FOLLOW_FULL:
		/* members arrive at different times and the room is shared: again every few seconds, then once more a minute later while the leader's game is the same */
		party->follow_running = 0;
		if (!party->follow_final_retry && party->follow_tries + 1 < SOCIAL_PARTY_FOLLOW_TRIES)
		{
			party->follow_tries++;
			party->follow_retry_wait = 1;
		}
		else if (!party->follow_final_retry)
		{
			party->follow_final_retry = 1;
			party->follow_retry_wait = 1;
		}
		else
		{
			party->follow_done = 1;
			party->follow_done_order = (unsigned short)order;
		}
		break;
	default:
		party->follow_running = 0;
		party->follow_done = 1;
		party->follow_done_order = (unsigned short)order;
		break;
	}
	engine->version++;
	return SOCIAL_PARTY_OK;
}

/* ---------- the passes */

void social_party_update(struct social_engine *engine)
{
	struct social_party *party = &engine->party;
	unsigned int now = social_now(engine);
	int index;

	if (!party->active)
		return;
	if (party->joining)
	{
		/* asking the leader to list this machine: every few seconds, and given up after a while */
		if (social_elapsed(engine, party->join_time, SOCIAL_PARTY_JOIN_WAIT))
		{
			left(engine, "not admitted");
			return;
		}
		if (engine->env.ready(engine->env.user) > 0 && social_elapsed(engine, party->state_time, SOCIAL_PARTY_CHANGE_SPACING * 3))
			state_send(engine);
		return;
	}
	party->members[party->self].heard = 1;
	party->members[party->self].heard_time = now;
	if (is_leader(engine))
	{
		party->leader_heard = now;
		/* the game it hosts: a new one counts */
		const struct social_place *place = &engine->place;
		int present = place->hosting && place->has_invite && place->joinable && place->activity != SOCIAL_SOLO;

		if (present)
		{
			if (!party->game_present || memcmp(party->game.token, place->invite_token, SOCIAL_TOKEN_SIZE) || memcmp(party->game.host_hash, place->invite_hash, SOCIAL_HASH_SIZE))
			{
				party->game_order++;
				party->state_dirty = 1;
			}
			if (!party->game_present || strcmp(party->game.map, place->map) || party->game.players != place->players || strcmp(party->game.gametype, place->gametype))
				party->state_dirty = 1;
			party->game_present = 1;
			memcpy(party->game.host_hash, place->invite_hash, SOCIAL_HASH_SIZE);
			memcpy(party->game.token, place->invite_token, SOCIAL_TOKEN_SIZE);
			snprintf(party->game.map, sizeof(party->game.map), "%s", place->map);
			snprintf(party->game.gametype, sizeof(party->game.gametype), "%s", place->gametype);
			party->game.players = place->players;
			party->game.maximum_players = place->maximum_players;
			party->game.engine_type = place->engine_type;
			party->game.network_version = (unsigned short)engine->env.network_version;
		}
		else if (party->game_present)
		{
			party->game_present = 0;
			party->state_dirty = 1;
		}
		/* members silent for thirty seconds are dropped from the list */
		for (index = 0; index < SOCIAL_MAX_PARTY_MEMBERS; index++)
		{
			struct social_member *member = &party->members[index];

			if (member->used && index != party->self && social_elapsed(engine, member->heard_time, SOCIAL_PARTY_MEMBER_SILENCE))
			{
				social_log(engine, "%s fell silent: dropped from the party", member->name);
				remember_dropped(engine, member);
				roster_remove(party, member->hash);
				memset(member, 0, sizeof(*member));
				party->state_dirty = 1;
				engine->version++;
			}
		}
	}
	else
	{
		/* the leader silent for twelve seconds: the first member after it in the list that is still heard takes over, with the next term */
		if (party->leader < 0 || social_elapsed(engine, party->leader_heard, SOCIAL_PARTY_LEADER_SILENCE))
		{
			int candidate = -1;

			for (index = 0; index < party->roster_count && candidate < 0; index++)
			{
				int member = member_index_by_hash8(party, party->roster[index]);

				if (member < 0 || member == party->leader)
					continue;
				if (member == party->self || !social_elapsed(engine, party->members[member].heard_time, SOCIAL_PARTY_LEADER_SILENCE))
					candidate = member;
			}
			if (candidate == party->self)
				claim(engine, (unsigned int)party->term + 1);
			else if (party->leader >= 0)
			{
				/* (the leader is dropped from view, so that the list shows who was heard; it comes back with its next word) */
				party->members[party->leader].heard = 0;
			}
		}
	}
	if (engine->env.ready(engine->env.user) <= 0)
		return;
	if ((party->state_dirty && social_elapsed(engine, party->state_time, SOCIAL_PARTY_CHANGE_SPACING)) ||
		social_elapsed(engine, party->state_time, is_leader(engine) ? SOCIAL_PARTY_HEARTBEAT_LEADER : SOCIAL_PARTY_HEARTBEAT))
	{
		state_send(engine);
	}
}

void social_party_shutdown(struct social_engine *engine)
{
	if (engine->party.active && !engine->party.joining)
		social_engine_party_leave(engine);
}

int social_party_topic(const struct social_engine *engine, const char *topic)
{
	return engine->party.active && !strcmp(engine->party.topic, topic);
}

const unsigned char *social_party_key(const struct social_engine *engine, const char *topic)
{
	return social_party_topic(engine, topic) ? engine->party.seal : NULL;
}

int social_party_topic_wanted(const struct social_engine *engine, char *topic)
{
	if (!engine->party.active)
		return 0;
	snprintf(topic, SOCIAL_TOPIC_SIZE, "%s", engine->party.topic);
	return 1;
}

void social_presence_extra(struct social_engine *engine, struct social_presence *presence)
{
	const struct social_party *party = &engine->party;

	if (!party->active || party->joining)
		return;
	presence->flags |= SOCIAL_PRESENCE_IN_PARTY;
	if (is_leader(engine) && party->open && engine->details != SOCIAL_DETAILS_NONE)
	{
		presence->flags |= SOCIAL_PRESENCE_PARTY_OPEN;
		memcpy(presence->party_token, party->token, SOCIAL_TOKEN_SIZE);
	}
}

const struct social_member *social_party_member_by_id(const struct social_engine *engine, unsigned int id)
{
	int index;

	for (index = 0; index < SOCIAL_MAX_PARTY_MEMBERS; index++)
	{
		if (engine->party.members[index].used && engine->party.members[index].id == id)
			return &engine->party.members[index];
	}
	return NULL;
}
