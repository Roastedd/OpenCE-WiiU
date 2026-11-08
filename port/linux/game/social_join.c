/*
SOCIAL_JOIN.C

Joining a friend's game (social_join.h).
*/

#include "social_join.h"
#include "../src/p2p.h"

#include <stdio.h>
#include <string.h>

/* how long a join may take before it is given up (p2p.c has its own, shorter, for the host's answer) */
#define JOIN_TIMEOUT 90000
/* how often the friend is looked at again while joining */
#define JOIN_CHECK_INTERVAL 1000

const char *social_join_reason_word(int reason)
{
	static const char *const words[NUMBER_OF_SOCIAL_JOIN_REASONS] = { "none", "offline", "not_joinable", "left", "version", "wrong_map", "full", "closed", "banned",
		"password", "no_answer", "no_route", "strict_nat", "busy", "together", "bad_invite", "refused", "timeout", "cancelled", "off" };

	return reason >= 0 && reason < NUMBER_OF_SOCIAL_JOIN_REASONS ? words[reason] : "unknown";
}

const char *social_join_reason_text(int reason, const char *map)
{
	static char text[96];

	switch (reason)
	{
	case SOCIAL_JOIN_REASON_NONE:
		return "Joining";
	case SOCIAL_JOIN_REASON_OFFLINE:
		return "Your friend is offline";
	case SOCIAL_JOIN_REASON_NOT_JOINABLE:
		return "Your friend is not in a game you can join";
	case SOCIAL_JOIN_REASON_LEFT:
		return "Your friend has left that game";
	case SOCIAL_JOIN_REASON_VERSION:
		return "Your friend plays another version of the game";
	case SOCIAL_JOIN_REASON_MAP_MISSING:
		if (map && map[0])
		{
			snprintf(text, sizeof(text), "You do not have the map %s", map);
			return text;
		}
		return "You do not have the map";
	case SOCIAL_JOIN_REASON_FULL:
		return "The game is full";
	case SOCIAL_JOIN_REASON_CLOSED:
		return "The game is closed to new players";
	case SOCIAL_JOIN_REASON_BANNED:
		return "You are banned from that game";
	case SOCIAL_JOIN_REASON_PASSWORD:
		return "That game needs a password";
	case SOCIAL_JOIN_REASON_NO_ANSWER:
		return "The game did not answer; it may have ended";
	case SOCIAL_JOIN_REASON_NO_ROUTE:
		return "Could not connect to the game";
	case SOCIAL_JOIN_REASON_STRICT_NAT:
		return "Your network is too strict to connect to that game";
	case SOCIAL_JOIN_REASON_BUSY:
		return "Leave your current game first";
	case SOCIAL_JOIN_REASON_TOGETHER:
		return "You are in that game already";
	case SOCIAL_JOIN_REASON_BAD_INVITE:
		return "That invite is no good";
	case SOCIAL_JOIN_REASON_REFUSED:
		return "The game would not take you";
	case SOCIAL_JOIN_REASON_TIMEOUT:
		return "Joining took too long";
	case SOCIAL_JOIN_REASON_CANCELLED:
		return "Cancelled";
	case SOCIAL_JOIN_REASON_OFF:
		return "Friends are turned off";
	}
	return "Could not join";
}

void social_join_machine_init(struct social_join_machine *machine, const struct social_join_backend *backend)
{
	memset(machine, 0, sizeof(*machine));
	machine->backend = *backend;
}

static unsigned int now(struct social_join_machine *machine)
{
	return machine->backend.now(machine->backend.user);
}

static int fail(struct social_join_machine *machine, int reason)
{
	machine->state = SOCIAL_JOIN_FAILED;
	machine->reason = reason;
	return reason;
}

int social_join_machine_target(struct social_join_machine *machine, const struct social_join_target *target)
{
	const struct social_join_backend *backend = &machine->backend;
	int map_state;
	static const unsigned char none[SOCIAL_TOKEN_SIZE];

	if (machine->state == SOCIAL_JOIN_WORKING)
		social_join_machine_cancel(machine);
	machine->state = SOCIAL_JOIN_IDLE;
	machine->reason = SOCIAL_JOIN_REASON_NONE;
	machine->target = *target;
	machine->map_download = 0;
	machine->retried = 0;
	machine->serial++;
	machine->started = now(machine);
	machine->checked = machine->started;
	/* an invite with no token is none */
	if (!memcmp(target->token, none, sizeof(none)))
		return fail(machine, SOCIAL_JOIN_REASON_BAD_INVITE);
	if (target->network_version != backend->version(backend->user))
		return fail(machine, SOCIAL_JOIN_REASON_VERSION);
	if (backend->in_game_of(backend->user, target->host_hash))
		return fail(machine, SOCIAL_JOIN_REASON_TOGETHER);
	if (backend->busy(backend->user))
		return fail(machine, SOCIAL_JOIN_REASON_BUSY);
	map_state = target->map[0] ? backend->map_state(backend->user, target->map) : 0;
	if (map_state == 2)
		return fail(machine, SOCIAL_JOIN_REASON_MAP_MISSING);
	machine->map_download = map_state == 1;
	if (!backend->begin(backend->user, target->host_hash, target->token))
		return fail(machine, SOCIAL_JOIN_REASON_BAD_INVITE);
	machine->state = SOCIAL_JOIN_WORKING;
	return SOCIAL_JOIN_REASON_NONE;
}

int social_join_machine_friend(struct social_join_machine *machine, unsigned int friend_id)
{
	struct social_join_target target;
	int online = 0;

	memset(&target, 0, sizeof(target));
	if (!machine->backend.friend_now(machine->backend.user, friend_id, &online, &target))
	{
		/* (the friend is online, or it is not) */
		if (machine->state == SOCIAL_JOIN_WORKING)
			social_join_machine_cancel(machine);
		machine->serial++;
		machine->target = target;
		return fail(machine, online ? SOCIAL_JOIN_REASON_NOT_JOINABLE : SOCIAL_JOIN_REASON_OFFLINE);
	}
	return social_join_machine_target(machine, &target);
}

static int reason_of_word(struct social_join_machine *machine, const char *word)
{
	if (!strcmp(word, "no_answer"))
		return SOCIAL_JOIN_REASON_NO_ANSWER;
	if (!strcmp(word, "no_route"))
		return machine->backend.strict_nat && machine->backend.strict_nat(machine->backend.user) ? SOCIAL_JOIN_REASON_STRICT_NAT : SOCIAL_JOIN_REASON_NO_ROUTE;
	if (!strcmp(word, "full") || !strcmp(word, "address_full"))
		return SOCIAL_JOIN_REASON_FULL;
	if (!strcmp(word, "closed"))
		return SOCIAL_JOIN_REASON_CLOSED;
	if (!strcmp(word, "banned"))
		return SOCIAL_JOIN_REASON_BANNED;
	if (!strcmp(word, "password"))
		return SOCIAL_JOIN_REASON_PASSWORD;
	if (!strcmp(word, "version"))
		return SOCIAL_JOIN_REASON_VERSION;
	/* the host made a new invite (it went private or started afresh) */
	if (!strcmp(word, "token"))
		return SOCIAL_JOIN_REASON_LEFT;
	return SOCIAL_JOIN_REASON_REFUSED;
}

void social_join_machine_update(struct social_join_machine *machine)
{
	const struct social_join_backend *backend = &machine->backend;
	char word[32];
	int stage = 0;

	if (machine->state != SOCIAL_JOIN_WORKING)
		return;
	backend->progress(backend->user, &stage, word, (int)sizeof(word));
	if (stage == P2P_JOIN_FAILED)
	{
		int reason = reason_of_word(machine, word);

		/* the invite was replaced: if the friend shows a new one, once more */
		if (reason == SOCIAL_JOIN_REASON_LEFT && machine->target.friend_id && !machine->retried)
		{
			struct social_join_target again;
			int online = 0;

			machine->retried = 1;
			memset(&again, 0, sizeof(again));
			if (backend->friend_now(backend->user, machine->target.friend_id, &online, &again) && memcmp(again.token, machine->target.token, SOCIAL_TOKEN_SIZE))
			{
				unsigned int serial = machine->serial;

				social_join_machine_target(machine, &again);
				machine->serial = serial;
				machine->retried = 1;
				return;
			}
		}
		fail(machine, reason);
		return;
	}
	if (stage == P2P_JOIN_CONNECTED)
	{
		if (!backend->game_ready || backend->game_ready(backend->user, machine->target.host_hash))
			machine->state = SOCIAL_JOIN_READY;
		return;
	}
	if ((unsigned int)(now(machine) - machine->started) >= JOIN_TIMEOUT)
	{
		backend->cancel(backend->user);
		fail(machine, SOCIAL_JOIN_REASON_TIMEOUT);
		return;
	}
	/* a friend's game: the friend gone from it while the host is asked is the game gone; a new invite in its place (it went private, or started afresh) is tried once */
	if (!machine->target.invited && machine->target.friend_id && (unsigned int)(now(machine) - machine->checked) >= JOIN_CHECK_INTERVAL)
	{
		struct social_join_target again;
		int online = 0;
		int shown;

		machine->checked = now(machine);
		memset(&again, 0, sizeof(again));
		shown = backend->friend_now(backend->user, machine->target.friend_id, &online, &again);
		if (shown && !memcmp(again.token, machine->target.token, SOCIAL_TOKEN_SIZE))
			return;
		if (shown && !machine->retried)
		{
			unsigned int serial = machine->serial;

			backend->cancel(backend->user);
			social_join_machine_target(machine, &again);
			machine->serial = serial;
			machine->retried = 1;
			return;
		}
		backend->cancel(backend->user);
		fail(machine, SOCIAL_JOIN_REASON_LEFT);
	}
}

void social_join_machine_cancel(struct social_join_machine *machine)
{
	if (machine->state == SOCIAL_JOIN_WORKING)
		machine->backend.cancel(machine->backend.user);
	if (machine->state != SOCIAL_JOIN_IDLE)
	{
		machine->state = SOCIAL_JOIN_FAILED;
		machine->reason = SOCIAL_JOIN_REASON_CANCELLED;
	}
}

void social_join_machine_done(struct social_join_machine *machine)
{
	machine->state = SOCIAL_JOIN_IDLE;
	machine->reason = SOCIAL_JOIN_REASON_NONE;
}

void social_join_machine_status(const struct social_join_machine *machine, struct social_join_status *status)
{
	memset(status, 0, sizeof(*status));
	status->state = machine->state;
	status->reason = machine->reason;
	status->target = machine->target;
	status->serial = machine->serial;
	status->map_download = machine->map_download;
	if (machine->state == SOCIAL_JOIN_FAILED)
		snprintf(status->text, sizeof(status->text), "%s", social_join_reason_text(machine->reason, machine->target.map));
	else if (machine->state == SOCIAL_JOIN_WORKING)
	{
		if (machine->map_download)
			snprintf(status->text, sizeof(status->text), "Joining; the map %s downloads as you join", machine->target.map);
		else
			snprintf(status->text, sizeof(status->text), "Joining");
	}
	else if (machine->state == SOCIAL_JOIN_READY)
		snprintf(status->text, sizeof(status->text), "Connected");
}

/* ---------- the machine of the process */

static struct social_join_machine global_machine;
static int global_ready;
static int (*hook_map_state)(const char *map);
static int (*hook_game_ready)(const unsigned char *host_hash);
static int (*hook_busy)(void);
/* the snapshot the lookups read (the game thread's alone) */
static struct social_snapshot global_snapshot;

void social_join_set_backend(const struct social_join_backend *backend)
{
	social_join_machine_init(&global_machine, backend);
	global_ready = 1;
}

void social_join_set_game_hooks(int (*map_state)(const char *map), int (*game_ready)(const unsigned char *host_hash), int (*busy)(void))
{
	hook_map_state = map_state;
	hook_game_ready = game_ready;
	hook_busy = busy;
}

int social_join_hook_map_state(const char *map)
{
	return hook_map_state ? hook_map_state(map) : 0;
}

int social_join_hook_game_ready(const unsigned char *host_hash)
{
	return hook_game_ready ? hook_game_ready(host_hash) : 1;
}

int social_join_hook_busy(void)
{
	return hook_busy ? hook_busy() : 0;
}

int social_join_friend(unsigned int friend_id)
{
	if (!global_ready || !social_is_enabled())
		return SOCIAL_JOIN_REASON_OFF;
	return social_join_machine_friend(&global_machine, friend_id);
}

int social_join_invite(unsigned int invite_id)
{
	int index;

	if (!global_ready || !social_is_enabled())
		return SOCIAL_JOIN_REASON_OFF;
	social_snapshot_get(&global_snapshot);
	for (index = 0; index < global_snapshot.invite_count; index++)
	{
		const struct social_invite_info *invite = &global_snapshot.invites[index];

		if (invite->id == invite_id && invite->kind == 1)
		{
			int reason = social_join_machine_target(&global_machine, &invite->target);

			social_invite_dismiss(invite_id);
			return reason;
		}
	}
	return SOCIAL_JOIN_REASON_BAD_INVITE;
}

void social_join_update(void)
{
	if (global_ready && global_machine.state == SOCIAL_JOIN_WORKING)
		social_join_machine_update(&global_machine);
}

void social_join_cancel(void)
{
	if (global_ready)
		social_join_machine_cancel(&global_machine);
}

void social_join_done(void)
{
	if (global_ready)
		social_join_machine_done(&global_machine);
}

void social_join_status_get(struct social_join_status *status)
{
	if (global_ready)
		social_join_machine_status(&global_machine, status);
	else
		memset(status, 0, sizeof(*status));
}
