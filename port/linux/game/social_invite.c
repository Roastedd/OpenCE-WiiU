/*
SOCIAL_INVITE.C

Inviting a friend to the game this machine hosts, and the invites that arrive (social_core.h). An invite is a message to one friend, sealed with the key the two
share, on this machine's presence topic: the host's key hash and the invite's token (the two halves of halo://join/...), and what is needed to say whether the game
can be joined before trying: its version, its map, its type and its players. It lasts a minute at the receiver, and a friend has three at most.
*/

#include "../src/p2p_internal.h"
#include "social_core.h"

#include <stdio.h>
#include <string.h>

int social_engine_invite(struct social_engine *engine, unsigned int friend_id)
{
	struct social_friend *friend = (struct social_friend *)social_friend_by_id(engine, friend_id);
	const struct social_place *place = &engine->place;
	struct social_invite invite;
	unsigned char plain[SOCIAL_MAX_PLAIN];
	int size, result;

	if (!engine->enabled || !engine->has_identity)
		result = SOCIAL_INVITE_OFF;
	else if (!friend)
		result = SOCIAL_INVITE_NO_FRIEND;
	else if (!place->hosting || !place->has_invite)
		result = SOCIAL_INVITE_NOTHING;
	else if (!social_friend_online(engine, friend))
		result = SOCIAL_INVITE_OFFLINE;
	else if (friend->invite_valid && !social_elapsed(engine, friend->invite_time, SOCIAL_INVITE_SPACING))
		result = SOCIAL_INVITE_TOO_FAST;
	else if (!engine->generation_ready || !social_friend_pair(engine, friend))
		result = SOCIAL_INVITE_BUSY;
	else
	{
		memset(&invite, 0, sizeof(invite));
		invite.kind = 1;
		memcpy(invite.target.host_hash, place->invite_hash, SOCIAL_HASH_SIZE);
		memcpy(invite.target.token, place->invite_token, SOCIAL_TOKEN_SIZE);
		invite.target.network_version = (unsigned short)engine->env.network_version;
		invite.target.players = place->players;
		invite.target.maximum_players = place->maximum_players;
		invite.target.engine_type = place->engine_type;
		snprintf(invite.target.map, sizeof(invite.target.map), "%s", place->map);
		snprintf(invite.target.gametype, sizeof(invite.target.gametype), "%s", place->gametype);
		size = social_invite_encode(engine->generation, social_next_sequence(engine), &invite, plain, (int)sizeof(plain));
		if (size > 0 && social_send(engine, engine->circle_topic, SOCIAL_KIND_DIRECT, friend->pair_key, friend->tag_out, plain, size))
		{
			friend->invite_time = social_now(engine);
			friend->invite_valid = 1;
			social_log(engine, "invited %s", friend->code);
			result = SOCIAL_INVITE_OK;
		}
		else
			result = SOCIAL_INVITE_BUSY;
	}
	engine->invite_result = result;
	engine->invite_serial++;
	engine->version++;
	return result == SOCIAL_INVITE_OK;
}

int social_engine_invite_dismiss(struct social_engine *engine, unsigned int id)
{
	int index;

	for (index = 0; index < SOCIAL_MAX_INVITES_TOTAL; index++)
	{
		if (engine->invites[index].used && engine->invites[index].id == id)
		{
			memset(&engine->invites[index], 0, sizeof(engine->invites[index]));
			engine->version++;
			return 1;
		}
	}
	return 0;
}

void social_invite_heard(struct social_engine *engine, struct social_friend *friend, const unsigned char *plain, int size, const struct social_inner *inner)
{
	struct social_invite invite;
	struct social_pending_invite *slot = NULL;
	struct social_event event;
	int index, per_friend = 0;

	if (!social_invite_decode(plain, size, &invite))
	{
		engine->statistics.dropped_malformed++;
		return;
	}
	social_window_mark(&friend->window, inner->generation, inner->sequence);
	engine->statistics.accepted++;
	/* the same game again: its time is renewed */
	for (index = 0; index < SOCIAL_MAX_INVITES_TOTAL; index++)
	{
		struct social_pending_invite *held = &engine->invites[index];

		if (!held->used || held->friend_id != friend->id)
			continue;
		per_friend++;
		if (held->kind == invite.kind && ((invite.kind == 1 && !memcmp(held->target.host_hash, invite.target.host_hash, SOCIAL_HASH_SIZE) &&
			!memcmp(held->target.token, invite.target.token, SOCIAL_TOKEN_SIZE)) || (invite.kind == 2 && !memcmp(held->party_token, invite.party_token, SOCIAL_TOKEN_SIZE))))
		{
			held->received = social_now(engine);
			return;
		}
	}
	/* three from one friend: its oldest goes. Twelve in all: the oldest of all */
	if (per_friend >= SOCIAL_MAX_INVITES)
	{
		for (index = 0; index < SOCIAL_MAX_INVITES_TOTAL; index++)
		{
			struct social_pending_invite *held = &engine->invites[index];

			if (held->used && held->friend_id == friend->id && (!slot || (int)(slot->received - held->received) > 0))
				slot = held;
		}
	}
	if (!slot)
	{
		for (index = 0; index < SOCIAL_MAX_INVITES_TOTAL && !slot; index++)
		{
			if (!engine->invites[index].used)
				slot = &engine->invites[index];
		}
	}
	if (!slot)
	{
		for (index = 0; index < SOCIAL_MAX_INVITES_TOTAL; index++)
		{
			if (!slot || (int)(slot->received - engine->invites[index].received) > 0)
				slot = &engine->invites[index];
		}
	}
	memset(slot, 0, sizeof(*slot));
	slot->used = 1;
	slot->id = ++engine->invite_serial_in ? engine->invite_serial_in : (engine->invite_serial_in = 1);
	slot->friend_id = friend->id;
	slot->kind = invite.kind;
	slot->target = invite.target;
	slot->target.friend_id = friend->id;
	slot->target.invited = 1;
	snprintf(slot->target.name, sizeof(slot->target.name), "%s", friend->presence.name[0] ? friend->presence.name : friend->code);
	memcpy(slot->party_token, invite.party_token, SOCIAL_TOKEN_SIZE);
	slot->party_size = invite.party_size;
	slot->received = social_now(engine);
	memset(&event, 0, sizeof(event));
	event.type = invite.kind == 1 ? SOCIAL_EVENT_INVITE : SOCIAL_EVENT_PARTY_INVITE;
	event.id = slot->id;
	snprintf(event.name, sizeof(event.name), "%s", slot->target.name);
	snprintf(event.code, sizeof(event.code), "%s", friend->code);
	event.target = slot->target;
	memcpy(event.party_token, slot->party_token, SOCIAL_TOKEN_SIZE);
	event.party_size = slot->party_size;
	social_event_push(engine, &event);
	social_log(engine, "%s invites to a %s", friend->code, invite.kind == 1 ? "game" : "party");
}

void social_invites_update(struct social_engine *engine)
{
	int index;

	for (index = 0; index < SOCIAL_MAX_INVITES_TOTAL; index++)
	{
		if (engine->invites[index].used && social_elapsed(engine, engine->invites[index].received, SOCIAL_INVITE_LIFE))
		{
			memset(&engine->invites[index], 0, sizeof(engine->invites[index]));
			engine->version++;
		}
	}
}
