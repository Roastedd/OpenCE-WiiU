/*
SCREENS_SOCIAL_BACKEND.C

The friends layer as the social screens' backend table (see screens_social_backend.h). Every function here runs on the game's thread and reads one copy of the layer's snapshot
(social_snapshot_get: a copy, no wait for the network or the card) that is made again whenever a screen asks whether anything changed (generation). Nothing here blocks.

Rows name what they stand for by its id in the snapshot (the row's key). A request this machine sent, which has no name, is shown by the start of its code; the blocked are only
counted by the layer, so a row says "BLOCKED 2" and unblocking names it by its place.

The failure words are the layer's own (social_*_result_text, social_join_reason_text) in capitals, with a full stop.
*/

#include "screens_social_backend.h"

#include "social.h"
#include "social_join.h"
#include "text_safe.h"

#include <stdio.h>
#include <string.h>

enum
{
	/* the things this file queues for the toast line when something it was asked failed or was done */
	NOTICES_MAXIMUM = 8,
	/* a friend that comes online is told of after this long, if still there (the screens hold it as long, so a flap is not shown), and not oftener than this */
	ONLINE_HOLD_MILLISECONDS = SCREENS_TOAST_ONLINE_HOLD_MILLISECONDS,
	ONLINE_SPACING_MILLISECONDS = 4000,
	ONLINE_PENDING_MAXIMUM = 16,
	/* more friends than this coming online at once are told of in one line */
	ONLINE_BURST = 3,
	/* an invitation's id space is the layer's; a toast the layer did not make has an id from here up */
	NOTICE_ID_BASE = 1000000,
};

static struct
{
	struct social_snapshot snapshot;
	unsigned int version;
	int have;
	/* the serials below are taken from the first snapshot, so that what happened before the screens looked is not told */
	int primed;
	/* moved by every call of ours that changes what the screens show */
	long changes;
	struct screens_social_hooks hooks;
	struct screens_toast notices[NOTICES_MAXIMUM];
	int notice_count;
	long next_id;
	struct
	{
		unsigned int id;
		char name[SCREENS_NAME_SIZE];
		unsigned long since;
	} online[ONLINE_PENDING_MAXIMUM];
	int online_count;
	unsigned long last_online_toast;
	/* what results of the layer were told already */
	unsigned int add_serial, invite_serial, join_serial;
	int join_state_told;
	char invited_name[SCREENS_NAME_SIZE];
	char join_name[SCREENS_NAME_SIZE];
} be;

void screens_social_backend_reset(void)
{
	struct screens_social_hooks hooks = be.hooks;

	memset(&be, 0, sizeof(be));
	be.hooks = hooks;
}

void screens_social_backend_hooks(struct screens_social_hooks const *hooks)
{
	if (hooks)
		be.hooks = *hooks;
	else
		memset(&be.hooks, 0, sizeof(be.hooks));
}

long screens_social_backend_changes(void)
{
	return be.changes;
}

static unsigned long be_now(void)
{
	return be.hooks.milliseconds ? be.hooks.milliseconds() : 0;
}

int screens_social_upper(char *destination, int size, char const *source)
{
	int length = 0;

	if (size <= 0)
		return 0;
	for (; source && *source && length < size - 1; source++, length++)
		destination[length] = (char)(*source >= 'a' && *source <= 'z' ? *source - 32 : *source);
	destination[length] = 0;
	return length;
}

/* a sentence of the layer as a screen says it: capitals, and a full stop */
static void be_sentence(char *destination, int size, char const *source)
{
	int length = screens_social_upper(destination, size, source);

	if (length > 0 && length < size - 1 && destination[length - 1] != '.' && destination[length - 1] != '?')
	{
		destination[length] = '.';
		destination[length + 1] = 0;
	}
}

static void be_answer_set(char *answer, int size, char const *text)
{
	if (answer && size > 0)
		snprintf(answer, (size_t)size, "%s", text ? text : "");
}

static void be_answer_sentence(char *answer, int size, char const *text)
{
	if (answer && size > 0)
		be_sentence(answer, size, text);
}

static void be_name_copy(char *destination, char const *source)
{
	snprintf(destination, SCREENS_NAME_SIZE, "%s", source && source[0] ? source : "PLAYER");
}

/* the snapshot made again; nonzero if the layer has one (it is on and has made its first) */
static int be_refresh(void)
{
	be.version = social_snapshot_get(&be.snapshot);
	be.have = be.version != 0 && be.snapshot.enabled;
	if (be.have && !be.primed)
	{
		struct social_join_status status;

		be.primed = 1;
		be.add_serial = be.snapshot.add_serial;
		be.invite_serial = be.snapshot.invite_serial;
		social_join_status_get(&status);
		be.join_serial = status.serial;
		be.join_state_told = status.state == SOCIAL_JOIN_FAILED;
	}
	return be.have;
}

static int be_have(void)
{
	return be.have ? 1 : be_refresh();
}

/* ---------- finding things in the snapshot */

static struct social_friend_info const *be_friend_by_id(unsigned int id)
{
	int index;

	for (index = 0; index < be.snapshot.friend_count; index++)
	{
		if (be.snapshot.friends[index].id == id)
			return &be.snapshot.friends[index];
	}
	return NULL;
}

static struct social_invite_info const *be_invite_by_id(unsigned int id)
{
	int index;

	for (index = 0; index < be.snapshot.invite_count; index++)
	{
		if (be.snapshot.invites[index].id == id)
			return &be.snapshot.invites[index];
	}
	return NULL;
}

/* the game invitation a friend has sent that is still good */
static struct social_invite_info const *be_invite_from(unsigned int friend_id)
{
	int index;

	for (index = 0; index < be.snapshot.invite_count; index++)
	{
		if (be.snapshot.invites[index].friend_id == friend_id && be.snapshot.invites[index].kind == 1)
			return &be.snapshot.invites[index];
	}
	return NULL;
}

/* ---------- generation */

static unsigned long backend_generation(void)
{
	struct social_join_status status;

	be_refresh();
	social_join_status_get(&status);
	return (unsigned long)be.version * 1000003ul + (unsigned long)be.changes * 7919ul + (unsigned long)status.serial * 31ul + (unsigned long)status.state;
}

/* ---------- friends */

static int be_presence_of(struct social_friend_info const *info)
{
	if (!info->online)
		return SCREENS_PRESENCE_OFFLINE;
	switch (info->activity)
	{
	case SOCIAL_ACTIVITY_LOBBY:
		return SCREENS_PRESENCE_LOBBY;
	case SOCIAL_ACTIVITY_GAME:
		return SCREENS_PRESENCE_PLAYING;
	case SOCIAL_ACTIVITY_SOLO:
	case SOCIAL_ACTIVITY_BUSY:
		return SCREENS_PRESENCE_AWAY;
	default:
		return SCREENS_PRESENCE_ONLINE;
	}
}

/* whether a game can be joined, and if not why, as the join machine would find it (version first, then the map, then the room) */
static int be_join_state(int joinable, int private_game, int players, int maximum, int network_version, char const *map)
{
	int version = be.hooks.network_version ? be.hooks.network_version() : 0;

	if (version && network_version && network_version != version)
		return network_version > version ? SCREENS_JOIN_NEWER_BUILD : SCREENS_JOIN_OLDER_BUILD;
	if (!joinable)
	{
		if (private_game)
			return SCREENS_JOIN_INVITE_ONLY;
		if (maximum > 0 && players >= maximum)
			return SCREENS_JOIN_FULL;
		return SCREENS_JOIN_CLOSED;
	}
	if (map && map[0])
	{
		int state = social_join_hook_map_state(map);

		if (state == 2)
			return SCREENS_JOIN_NEEDS_MAP;
		if (state == 1)
			return SCREENS_JOIN_DOWNLOAD;
	}
	return SCREENS_JOIN_OPEN;
}

static void be_friend_row(struct social_friend_info const *info, struct screens_friend_row *row)
{
	struct social_invite_info const *invite;

	memset(row, 0, sizeof(*row));
	be_name_copy(row->name, info->nickname[0] ? info->nickname : info->name);
	snprintf(row->code, sizeof(row->code), "%s", info->code);
	row->presence = (unsigned char)be_presence_of(info);
	row->party = (unsigned char)(info->in_party != 0);
	row->key = (long)info->id;
	if (info->online && info->hosting && (info->activity == SOCIAL_ACTIVITY_LOBBY || info->activity == SOCIAL_ACTIVITY_GAME))
	{
		row->players = (short)info->players;
		row->maximum = (short)info->maximum_players;
		snprintf(row->map, sizeof(row->map), "%s", info->map);
		snprintf(row->gametype, sizeof(row->gametype), "%s", info->gametype);
		row->join = (unsigned char)be_join_state(info->joinable, info->private_game, info->players, info->maximum_players, info->network_version, info->map);
	}
	/* (a friend that invited this machine to a game that presence does not show as joinable, a private one: the invitation carries the way in) */
	invite = info->online ? be_invite_from(info->id) : NULL;
	if (invite && !info->joinable)
	{
		row->presence = (unsigned char)(info->activity == SOCIAL_ACTIVITY_LOBBY ? SCREENS_PRESENCE_LOBBY : SCREENS_PRESENCE_PLAYING);
		row->players = invite->target.players;
		row->maximum = invite->target.maximum_players;
		snprintf(row->map, sizeof(row->map), "%s", invite->target.map);
		snprintf(row->gametype, sizeof(row->gametype), "%s", invite->target.gametype);
		row->join = (unsigned char)be_join_state(1, 1, invite->target.players, invite->target.maximum_players, invite->target.network_version, invite->target.map);
	}
}

static int be_rows_friends(struct screens_friend_row *rows, int maximum)
{
	int index, count = 0;

	for (index = 0; index < be.snapshot.friend_count && count < maximum; index++)
		be_friend_row(&be.snapshot.friends[index], &rows[count++]);
	return count;
}

/* a code without its dashes, the first 11 characters of it, for a request that has no name yet */
static void be_code_name(char const *code, char *name)
{
	int length = 0;

	for (; *code && length < SCREENS_NAME_LENGTH; code++)
	{
		if (*code != '-')
			name[length++] = *code;
	}
	name[length] = 0;
	if (!length)
		snprintf(name, SCREENS_NAME_SIZE, "NEW FRIEND");
}

static int be_rows_requests(struct screens_friend_row *rows, int maximum)
{
	int index, count = 0;

	for (index = 0; index < be.snapshot.request_count && count < maximum; index++)
	{
		struct social_request_info const *request = &be.snapshot.requests[index];
		struct screens_friend_row *row = &rows[count++];

		memset(row, 0, sizeof(*row));
		be_name_copy(row->name, request->name);
		snprintf(row->code, sizeof(row->code), "%s", request->code);
		row->presence = SCREENS_PRESENCE_REQUEST_GOT;
		row->key = (long)request->id;
	}
	for (index = 0; index < be.snapshot.sent_count && count < maximum; index++)
	{
		struct social_sent_info const *sent = &be.snapshot.sent[index];
		struct screens_friend_row *row = &rows[count++];

		memset(row, 0, sizeof(*row));
		be_code_name(sent->code, row->name);
		snprintf(row->code, sizeof(row->code), "%s", sent->code);
		row->presence = SCREENS_PRESENCE_REQUEST_SENT;
		row->key = (long)sent->id;
	}
	return count;
}

static int be_rows_blocked(struct screens_friend_row *rows, int maximum)
{
	int index, count = 0;

	for (index = 0; index < be.snapshot.blocked_count && count < maximum; index++)
	{
		struct screens_friend_row *row = &rows[count++];

		memset(row, 0, sizeof(*row));
		snprintf(row->name, sizeof(row->name), "BLOCKED %d", index + 1);
		row->presence = SCREENS_PRESENCE_BLOCKED;
		/* (unblocking names a blocked one by its place) */
		row->key = index + 1;
	}
	return count;
}

static int be_service_of(void)
{
	if (!be.have)
		return SCREENS_SERVICE_DISABLED;
	return be.snapshot.brokers > 0 ? SCREENS_SERVICE_CONNECTED : SCREENS_SERVICE_CONNECTING;
}

static int backend_friends(int tab, struct screens_friend_row *rows, int maximum, struct screens_friends_summary *summary)
{
	int count = 0, index, online = 0;

	be_have();
	if (summary)
	{
		memset(summary, 0, sizeof(*summary));
		snprintf(summary->own_code, sizeof(summary->own_code), "%s", be.have ? be.snapshot.code : "");
		summary->service = (unsigned char)be_service_of();
		if (be.have)
		{
			for (index = 0; index < be.snapshot.friend_count; index++)
				online += be.snapshot.friends[index].online != 0;
			summary->counts[SCREENS_TAB_FRIENDS] = (short)be.snapshot.friend_count;
			summary->counts[SCREENS_TAB_REQUESTS] = (short)(be.snapshot.request_count + be.snapshot.sent_count);
			summary->counts[SCREENS_TAB_PARTY] = (short)(be.snapshot.party.active ? be.snapshot.party.member_count : 0);
			summary->counts[SCREENS_TAB_BLOCKED] = (short)be.snapshot.blocked_count;
			summary->online = (short)online;
		}
	}
	if (!be.have || !rows || maximum <= 0)
		return 0;
	switch (tab)
	{
	case SCREENS_TAB_FRIENDS:
		count = be_rows_friends(rows, maximum);
		break;
	case SCREENS_TAB_REQUESTS:
		count = be_rows_requests(rows, maximum);
		break;
	case SCREENS_TAB_BLOCKED:
		count = be_rows_blocked(rows, maximum);
		break;
	default:
		break;
	}
	return count;
}

/* ---------- the party */

static int be_member_state(struct social_party_member_info const *member)
{
	if (!member->online)
		return SCREENS_MEMBER_OFFLINE;
	switch (member->follow_result)
	{
	case SOCIAL_FOLLOW_JOINING:
		return SCREENS_MEMBER_JOINING;
	case SOCIAL_FOLLOW_JOINED:
	case SOCIAL_FOLLOW_BUSY:
		return SCREENS_MEMBER_IN_GAME;
	case SOCIAL_FOLLOW_FULL:
		return SCREENS_MEMBER_FULL_RETRYING;
	case SOCIAL_FOLLOW_MAP_MISSING:
		return SCREENS_MEMBER_NEEDS_MAP;
	case SOCIAL_FOLLOW_VERSION:
		return SCREENS_MEMBER_NEWER_BUILD;
	case SOCIAL_FOLLOW_NO_ROUTE:
	case SOCIAL_FOLLOW_NO_ANSWER:
		return SCREENS_MEMBER_NO_ROUTE;
	case SOCIAL_FOLLOW_OPTED_OUT:
	case SOCIAL_FOLLOW_CLOSED:
	case SOCIAL_FOLLOW_BANNED:
	case SOCIAL_FOLLOW_REPLACED:
		return SCREENS_MEMBER_STAYING;
	default:
		return member->in_game || member->hosting ? SCREENS_MEMBER_IN_GAME : SCREENS_MEMBER_MENUS;
	}
}

static int backend_party(struct screens_party_member *members, int maximum, struct screens_party_summary *summary)
{
	struct social_party_info const *party;
	int index, count = 0;

	be_have();
	if (summary)
		memset(summary, 0, sizeof(*summary));
	if (!be.have)
		return 0;
	party = &be.snapshot.party;
	if (summary)
	{
		summary->present = (unsigned char)(party->active != 0);
		summary->size = (short)(party->active ? party->member_count : 0);
		summary->maximum = SOCIAL_API_MAX_PARTY;
		summary->follow = (unsigned char)(party->auto_follow != 0);
		summary->i_lead = (unsigned char)(party->active && party->is_leader);
		if (party->active && party->game_present)
		{
			char name[SCREENS_NAME_SIZE];

			screens_social_upper(name, sizeof(name), party->game.name);
			snprintf(summary->leader_game, sizeof(summary->leader_game), "%s'S GAME: %s %d OF %d", name[0] ? name : "THE LEADER", party->game.map, party->game.players,
				party->game.maximum_players);
		}
	}
	if (!party->active || !members)
		return 0;
	for (index = 0; index < party->member_count && count < maximum; index++)
	{
		struct social_party_member_info const *member = &party->members[index];
		struct screens_party_member *row = &members[count++];

		memset(row, 0, sizeof(*row));
		be_name_copy(row->name, member->name);
		row->leader = (unsigned char)(member->leader != 0);
		row->me = (unsigned char)(member->self != 0);
		row->state = (unsigned char)be_member_state(member);
		row->key = (long)member->id;
	}
	return count;
}

/* ---------- the toasts */

static void be_notice_push(int kind, char const *text, char const *detail, long id)
{
	struct screens_toast *toast;

	if (be.notice_count >= NOTICES_MAXIMUM)
	{
		/* (the oldest that is no invitation goes) */
		int index;

		for (index = 0; index < be.notice_count && be.notices[index].kind == SCREENS_TOAST_INVITE; index++)
			;
		if (index == be.notice_count)
			return;
		memmove(&be.notices[index], &be.notices[index + 1], (size_t)(be.notice_count - index - 1) * sizeof(be.notices[0]));
		be.notice_count--;
	}
	toast = &be.notices[be.notice_count++];
	memset(toast, 0, sizeof(*toast));
	toast->id = id ? id : NOTICE_ID_BASE + (++be.next_id);
	toast->kind = (unsigned char)kind;
	snprintf(toast->text, sizeof(toast->text), "%s", text);
	snprintf(toast->detail, sizeof(toast->detail), "%s", detail ? detail : "");
}

static void be_notice_named(int kind, char const *name, char const *rest, char const *detail, long id)
{
	char who[SCREENS_NAME_SIZE], text[SCREENS_LINE_SIZE];

	screens_social_upper(who, sizeof(who), name && name[0] ? name : "SOMEONE");
	snprintf(text, sizeof(text), "%s%s", who, rest);
	be_notice_push(kind, text, detail, id);
}

static void be_game_detail(struct social_join_target const *target, char *detail, int size)
{
	if (target->map[0])
		snprintf(detail, (size_t)size, "%s - %s - %d of %d", target->map, target->gametype[0] ? target->gametype : "game", target->players, target->maximum_players);
	else
		detail[0] = 0;
}

static void be_online_forget(unsigned int id)
{
	int index;

	for (index = 0; index < be.online_count; index++)
	{
		if (be.online[index].id == id)
		{
			memmove(&be.online[index], &be.online[index + 1], (size_t)(be.online_count - index - 1) * sizeof(be.online[0]));
			be.online_count--;
			return;
		}
	}
}

/* the friends that came online and have stayed: told of (after the hold; not oftener than the spacing; a crowd of them in one line) */
static void be_online_release(void)
{
	unsigned long clock = be_now();
	int index, ripe = 0;

	if (be.online_count == 0 || (be.last_online_toast && clock - be.last_online_toast < ONLINE_SPACING_MILLISECONDS))
		return;
	/* (one that went offline again was dropped by its event; one removed from the list is dropped here) */
	for (index = 0; index < be.online_count;)
	{
		struct social_friend_info const *info = be_friend_by_id(be.online[index].id);

		if (!info || !info->online)
		{
			be_online_forget(be.online[index].id);
			continue;
		}
		if (clock - be.online[index].since >= ONLINE_HOLD_MILLISECONDS)
			ripe++;
		index++;
	}
	if (!ripe)
		return;
	if (ripe > ONLINE_BURST)
	{
		char text[SCREENS_LINE_SIZE];

		snprintf(text, sizeof(text), "%d FRIENDS ARE ONLINE", ripe);
		be_notice_push(SCREENS_TOAST_FRIEND_ONLINE, text, "Friends", 0);
		for (index = 0; index < be.online_count;)
		{
			if (clock - be.online[index].since >= ONLINE_HOLD_MILLISECONDS)
				be_online_forget(be.online[index].id);
			else
				index++;
		}
	}
	else
	{
		for (index = 0; index < be.online_count; index++)
		{
			if (clock - be.online[index].since >= ONLINE_HOLD_MILLISECONDS)
			{
				be_notice_named(SCREENS_TOAST_FRIEND_ONLINE, be.online[index].name, " IS ONLINE", "", 0);
				be_online_forget(be.online[index].id);
				break;
			}
		}
	}
	be.last_online_toast = clock ? clock : 1;
}

/* what the layer told since the last look, as toasts */
static void be_events_collect(void)
{
	struct social_api_event event;
	char detail[SCREENS_LINE_SIZE];

	while (social_event_next(&event))
	{
		detail[0] = 0;
		switch (event.type)
		{
		case SOCIAL_API_EVENT_REQUEST:
			be_notice_named(SCREENS_TOAST_REQUEST, event.name, " WANTS TO BE YOUR FRIEND", "Friends > Requests", 0);
			break;
		case SOCIAL_API_EVENT_FRIEND:
			be_notice_named(SCREENS_TOAST_NOTICE, event.name, " IS NOW YOUR FRIEND", "", 0);
			break;
		case SOCIAL_API_EVENT_ONLINE:
			be_online_forget(event.id);
			if (be.online_count < ONLINE_PENDING_MAXIMUM)
			{
				be.online[be.online_count].id = event.id;
				be_name_copy(be.online[be.online_count].name, event.name);
				be.online[be.online_count].since = be_now();
				be.online_count++;
			}
			break;
		case SOCIAL_API_EVENT_OFFLINE:
			be_online_forget(event.id);
			break;
		case SOCIAL_API_EVENT_INVITE:
		{
			struct social_invite_info const *invite = be_invite_by_id(event.id);

			if (invite)
				be_game_detail(&invite->target, detail, (int)sizeof(detail));
			be_notice_named(SCREENS_TOAST_INVITE, event.name, " INVITES YOU", detail, (long)event.id);
			break;
		}
		case SOCIAL_API_EVENT_PARTY_INVITE:
		{
			struct social_invite_info const *invite = be_invite_by_id(event.id);

			if (invite)
				snprintf(detail, sizeof(detail), "A party of %d", invite->party_size);
			be_notice_named(SCREENS_TOAST_INVITE, event.name, " INVITES YOU TO A PARTY", detail, (long)event.id);
			break;
		}
		case SOCIAL_API_EVENT_PARTY_JOINED:
			be_notice_push(SCREENS_TOAST_PARTY_JOINED, "YOU ARE IN A PARTY", "", 0);
			break;
		case SOCIAL_API_EVENT_PARTY_LEFT:
			be_sentence(detail, sizeof(detail), event.text);
			be_notice_push(SCREENS_TOAST_PARTY_LEFT, "YOU LEFT THE PARTY", detail, 0);
			break;
		case SOCIAL_API_EVENT_PARTY_KICKED:
			be_notice_named(SCREENS_TOAST_PARTY_LEFT, event.name, " REMOVED YOU FROM THE PARTY", "", 0);
			break;
		case SOCIAL_API_EVENT_PARTY_LEADER:
			be_notice_named(SCREENS_TOAST_NOTICE, event.name, " LEADS THE PARTY", "", 0);
			break;
		default:
			/* (removed by a friend: said nowhere; party chat: the chat's own) */
			break;
		}
	}
}

/* what became of what the player asked (an invite sent, a code added, a join): the failures as toasts, an invite sent as a notice */
static void be_results_collect(void)
{
	struct social_join_status status;
	char text[SCREENS_LINE_SIZE];

	if (be.snapshot.invite_serial != be.invite_serial)
	{
		be.invite_serial = be.snapshot.invite_serial;
		if (be.invited_name[0])
		{
			if (be.snapshot.invite_result == SOCIAL_INVITE_RESULT_SENT)
				be_notice_named(SCREENS_TOAST_NOTICE, be.invited_name, " WAS INVITED", "", 0);
			else
			{
				be_sentence(text, sizeof(text), social_invite_result_text(be.snapshot.invite_result));
				be_notice_push(SCREENS_TOAST_JOIN_FAILED, text, "", 0);
			}
			be.invited_name[0] = 0;
		}
	}
	if (be.snapshot.add_serial != be.add_serial)
	{
		be.add_serial = be.snapshot.add_serial;
		/* (the engine's own answer to a code it took: the typed code was checked and sent already; only a refusal is news) */
		if (be.snapshot.add_result != SOCIAL_ADD_RESULT_SENT && be.add_serial)
		{
			be_sentence(text, sizeof(text), social_add_result_text(be.snapshot.add_result));
			be_notice_push(SCREENS_TOAST_JOIN_FAILED, text, "", 0);
		}
	}
	social_join_status_get(&status);
	if (status.serial != be.join_serial)
	{
		be.join_serial = status.serial;
		be.join_state_told = 0;
	}
	if (status.state == SOCIAL_JOIN_FAILED && !be.join_state_told && status.serial)
	{
		be.join_state_told = 1;
		be_sentence(text, sizeof(text), status.text[0] ? status.text : social_join_reason_text(status.reason, status.target.map));
		be_notice_push(SCREENS_TOAST_JOIN_FAILED, text, "", 0);
	}
}

static int backend_toast_pop(struct screens_toast *toast)
{
	if (!be_refresh())
		return 0;
	be_events_collect();
	be_results_collect();
	be_online_release();
	if (!be.notice_count)
		return 0;
	*toast = be.notices[0];
	memmove(&be.notices[0], &be.notices[1], (size_t)(be.notice_count - 1) * sizeof(be.notices[0]));
	be.notice_count--;
	return 1;
}

static void backend_invite_answer(long id, int accept)
{
	struct social_invite_info const *invite;
	char text[SCREENS_LINE_SIZE];

	be_refresh();
	invite = be.have ? be_invite_by_id((unsigned int)id) : NULL;
	if (!invite)
	{
		be_notice_push(SCREENS_TOAST_NOTICE, "THAT INVITATION HAS LAPSED.", "", 0);
		return;
	}
	if (!accept)
		return;
	be.changes++;
	if (invite->kind == 2)
	{
		social_party_join_invite(invite->id);
		return;
	}
	/* (a game this machine is in is left for the join: the game ends, the menus come up, the join begins and the Direct Link list finishes it. A game that cannot be joined (another version,
	a map that is missing) is not worth leaving this one for: the reason is said and the game goes on) */
	if (social_join_hook_busy())
	{
		int reason = social_flow_leave_and_join_invite(invite->id);

		if (reason != SOCIAL_JOIN_REASON_NONE)
		{
			be_sentence(text, sizeof(text), social_join_reason_text(reason, invite->target.map));
			be_notice_push(SCREENS_TOAST_JOIN_FAILED, text, "", 0);
			return;
		}
		snprintf(be.join_name, sizeof(be.join_name), "%s", invite->name);
		{
			char who[SCREENS_NAME_SIZE];

			screens_social_upper(who, sizeof(who), invite->name[0] ? invite->name : "A FRIEND");
			snprintf(text, sizeof(text), "LEAVING THE GAME TO JOIN %s", who);
			be_notice_push(SCREENS_TOAST_NOTICE, text, "", 0);
		}
		return;
	}
	{
		int reason = social_join_invite(invite->id);

		if (reason != SOCIAL_JOIN_REASON_NONE)
		{
			be_sentence(text, sizeof(text), social_join_reason_text(reason, invite->target.map));
			be_notice_push(SCREENS_TOAST_JOIN_FAILED, text, "", 0);
		}
	}
}

/* ---------- actions */

static int backend_friend_action(int action, struct screens_friend_row const *row, char *answer, int size)
{
	struct social_friend_info const *info;
	unsigned int id = (unsigned int)row->key;

	if (!be_refresh())
	{
		be_answer_set(answer, size, "FRIENDS ARE OFF.");
		return 0;
	}
	be.changes++;
	switch (action)
	{
	case SCREENS_ACTION_VIEW_CODE:
		be_answer_set(answer, size, row->code);
		return 1;
	case SCREENS_ACTION_ACCEPT:
		if (row->presence != SCREENS_PRESENCE_REQUEST_GOT)
			break;
		social_accept(id);
		return 1;
	case SCREENS_ACTION_DECLINE:
		if (row->presence == SCREENS_PRESENCE_REQUEST_SENT)
			social_cancel(id);
		else if (row->presence == SCREENS_PRESENCE_REQUEST_GOT)
			social_ignore(id);
		else
			break;
		return 1;
	case SCREENS_ACTION_UNBLOCK:
		if (row->presence != SCREENS_PRESENCE_BLOCKED)
			break;
		social_unblock((int)row->key - 1);
		return 1;
	case SCREENS_ACTION_BLOCK:
		/* (a request is turned away for good: the layer blocks friends, and a request has no friend id) */
		if (row->presence == SCREENS_PRESENCE_REQUEST_GOT)
			social_ignore(id);
		else if (be_friend_by_id(id) && row->presence != SCREENS_PRESENCE_REQUEST_SENT && row->presence != SCREENS_PRESENCE_BLOCKED)
			social_block(id);
		else
			break;
		return 1;
	case SCREENS_ACTION_REMOVE_FRIEND:
		if (!be_friend_by_id(id) || row->presence == SCREENS_PRESENCE_REQUEST_SENT || row->presence == SCREENS_PRESENCE_REQUEST_GOT || row->presence == SCREENS_PRESENCE_BLOCKED)
			break;
		social_remove(id);
		return 1;
	default:
		break;
	}
	if (action == SCREENS_ACTION_ACCEPT || action == SCREENS_ACTION_DECLINE || action == SCREENS_ACTION_UNBLOCK || action == SCREENS_ACTION_BLOCK || action == SCREENS_ACTION_REMOVE_FRIEND)
	{
		be_answer_set(answer, size, "THAT DOES NOT APPLY TO THIS ROW.");
		return 0;
	}
	info = be_friend_by_id(id);
	if (!info)
	{
		be_answer_set(answer, size, "THAT FRIEND IS NOT ON YOUR LIST.");
		return 0;
	}
	switch (action)
	{
	case SCREENS_ACTION_JOIN_GAME:
	{
		struct social_invite_info const *invite = info->joinable ? NULL : be_invite_from(id);
		int reason = invite ? social_join_invite(invite->id) : social_join_friend(id);

		if (reason != SOCIAL_JOIN_REASON_NONE)
		{
			be_answer_sentence(answer, size, social_join_reason_text(reason, invite ? invite->target.map : info->map));
			return 0;
		}
		snprintf(be.join_name, sizeof(be.join_name), "%s", info->nickname[0] ? info->nickname : info->name);
		be_answer_set(answer, size, "");
		return 1;
	}
	case SCREENS_ACTION_INVITE_GAME:
		/* (the layer answers when its thread has taken the call; results_collect tells how it went) */
		snprintf(be.invited_name, sizeof(be.invited_name), "%s", info->nickname[0] ? info->nickname : info->name);
		social_invite_friend(id);
		be_answer_set(answer, size, "");
		return 1;
	case SCREENS_ACTION_INVITE_PARTY:
		if (!be.snapshot.party.active)
			social_party_make();
		social_party_invite(id);
		be_answer_set(answer, size, "PARTY INVITATION SENT.");
		return 1;
	default:
		be_answer_set(answer, size, "THAT IS NOT AVAILABLE IN THIS BUILD YET.");
		return 0;
	}
}

static int backend_party_action(int action, struct screens_party_member const *member, char *answer, int size)
{
	if (!be_refresh())
	{
		be_answer_set(answer, size, "FRIENDS ARE OFF.");
		return 0;
	}
	be.changes++;
	switch (action)
	{
	case SCREENS_ACTION_FOLLOW_ON:
	case SCREENS_ACTION_FOLLOW_OFF:
		social_party_set_auto_follow(action == SCREENS_ACTION_FOLLOW_ON);
		return 1;
	case SCREENS_ACTION_LEAVE_PARTY:
		social_party_leave();
		return 1;
	case SCREENS_ACTION_MAKE_LEADER:
		social_party_transfer((unsigned int)member->key);
		return 1;
	case SCREENS_ACTION_REMOVE_FROM_PARTY:
		social_party_kick((unsigned int)member->key);
		return 1;
	case SCREENS_ACTION_DISBAND_PARTY:
		/* (the leader ends it for everyone; a member that is not the leader cannot) */
		if (!be.snapshot.party.is_leader)
		{
			be_answer_set(answer, size, "ONLY THE LEADER CAN END THE PARTY. LEAVE IT INSTEAD.");
			return 0;
		}
		social_party_disband();
		return 1;
	default:
		be_answer_set(answer, size, "THAT IS NOT AVAILABLE IN THIS BUILD YET.");
		return 0;
	}
}

/* two codes, the same once read as codes */
static int be_code_same(char const *a, char const *b)
{
	char x[TEXT_SAFE_CODE_LENGTH + 4], y[TEXT_SAFE_CODE_LENGTH + 4];

	return text_safe_code_normalise(x, (short)sizeof(x), a) == TEXT_SAFE_CODE_LENGTH && text_safe_code_normalise(y, (short)sizeof(y), b) == TEXT_SAFE_CODE_LENGTH &&
		!memcmp(x, y, TEXT_SAFE_CODE_LENGTH);
}

static int backend_friend_add(char const *code, char *answer, int size)
{
	char normalised[TEXT_SAFE_CODE_LENGTH + 4], text[SCREENS_CODE_SIZE];
	short length;
	int result, index;

	if (!be_refresh())
	{
		be_answer_set(answer, size, "FRIENDS ARE OFF.");
		return 0;
	}
	length = text_safe_code_normalise(normalised, (short)sizeof(normalised), code);
	if (length < 0)
	{
		be_answer_set(answer, size, "NOT A CODE: CODES HAVE NO I, L, O OR U.");
		return 0;
	}
	if (length != TEXT_SAFE_CODE_LENGTH)
	{
		be_answer_set(answer, size, "CHECK THE CODE: IT IS 13 LETTERS AND NUMBERS.");
		return 0;
	}
	snprintf(text, sizeof(text), "%.4s-%.4s-%.4s-%.1s", normalised, normalised + 4, normalised + 8, normalised + 12);
	/* (what is known here is said at once: the layer answers its own checks later, and a request sent to oneself is no news worth a toast) */
	if (be_code_same(text, be.snapshot.code))
	{
		be_answer_set(answer, size, "THAT IS YOUR OWN CODE.");
		return 0;
	}
	for (index = 0; index < be.snapshot.friend_count; index++)
	{
		if (be_code_same(text, be.snapshot.friends[index].code))
		{
			be_answer_set(answer, size, "YOU ARE FRIENDS ALREADY.");
			return 0;
		}
	}
	for (index = 0; index < be.snapshot.sent_count; index++)
	{
		if (be_code_same(text, be.snapshot.sent[index].code))
		{
			be_answer_set(answer, size, "A REQUEST TO THAT CODE IS WAITING FOR AN ANSWER.");
			return 0;
		}
	}
	be.changes++;
	result = social_add_code(text);
	if (result != SOCIAL_ADD_RESULT_SENT)
	{
		be_answer_sentence(answer, size, social_add_result_text(result));
		return 0;
	}
	be_answer_set(answer, size, "REQUEST SENT. THEY WILL SEE IT WHEN THEY ARE ONLINE.");
	return 1;
}

/* what a friend's row offers: the game's invitation as well as the party's */
static void backend_friend_actions(int tab, struct screens_friend_row const *row, int *actions)
{
	int count = 0;

	if (tab != SCREENS_TAB_FRIENDS)
	{
		screens_default_friend_actions(tab, row, actions);
		return;
	}
	if ((row->presence == SCREENS_PRESENCE_PLAYING || row->presence == SCREENS_PRESENCE_LOBBY) && (row->join == SCREENS_JOIN_OPEN || row->join == SCREENS_JOIN_DOWNLOAD))
		actions[count++] = SCREENS_ACTION_JOIN_GAME;
	if (row->presence != SCREENS_PRESENCE_OFFLINE)
	{
		actions[count++] = SCREENS_ACTION_INVITE_GAME;
		if (!row->party)
			actions[count++] = SCREENS_ACTION_INVITE_PARTY;
	}
	actions[count++] = SCREENS_ACTION_VIEW_CODE;
	actions[count++] = SCREENS_ACTION_REMOVE_FRIEND;
	actions[count++] = SCREENS_ACTION_BLOCK;
	actions[count++] = SCREENS_ACTION_CANCEL;
	actions[count] = SCREENS_ACTION_NONE;
}

/* the party's rows: as the defaults, and the leader's own row also LEAVES (the next member leads), which the defaults do not offer beside DISBAND */
static void backend_party_actions(struct screens_party_member const *member, struct screens_party_summary const *summary, int *actions)
{
	int all[SCREENS_ACTIONS_MAXIMUM + 1], index, count = 0;

	screens_default_party_actions(member, summary, all);
	for (index = 0; all[index] != SCREENS_ACTION_NONE && count < SCREENS_ACTIONS_MAXIMUM - 1; index++)
	{
		actions[count++] = all[index];
		if (all[index] == SCREENS_ACTION_DISBAND_PARTY)
			actions[count++] = SCREENS_ACTION_LEAVE_PARTY;
	}
	actions[count] = SCREENS_ACTION_NONE;
}

/* ---------- the game's roster: who is a friend, who is in the party, who to ask about */

static int be_same_map(char const *a, char const *b)
{
	if (!a || !a[0] || !b || !b[0])
		return 1;
	for (; *a && *b; a++, b++)
	{
		char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a, y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;

		if (x != y)
			return 0;
	}
	return !*a && !*b;
}

unsigned int screens_social_roster_friend(char const *name, char const *map)
{
	int index;

	if (!name || !name[0] || !be_have())
		return 0;
	for (index = 0; index < be.snapshot.friend_count; index++)
	{
		struct social_friend_info const *info = &be.snapshot.friends[index];
		char shown[SCREENS_NAME_SIZE];

		if (!info->online || (info->activity != SOCIAL_ACTIVITY_LOBBY && info->activity != SOCIAL_ACTIVITY_GAME))
			continue;
		be_name_copy(shown, info->name);
		if (!strcmp(shown, name) && be_same_map(info->map, map))
			return info->id;
		if (info->nickname[0])
		{
			be_name_copy(shown, info->nickname);
			if (!strcmp(shown, name) && be_same_map(info->map, map))
				return info->id;
		}
	}
	return 0;
}

unsigned short screens_social_roster_flags(char const *name, char const *map)
{
	unsigned short flags = 0;
	int index;

	(void)map;
	if (!name || !name[0] || !be_have())
		return 0;
	if (screens_social_roster_friend(name, map))
		flags |= SCREENS_ROW_FRIEND;
	if (be.snapshot.party.active)
	{
		for (index = 0; index < be.snapshot.party.member_count; index++)
		{
			struct social_party_member_info const *member = &be.snapshot.party.members[index];
			char shown[SCREENS_NAME_SIZE];

			be_name_copy(shown, member->name);
			if (!member->self && member->online && !strcmp(shown, name))
				flags |= SCREENS_ROW_PARTY;
		}
	}
	return flags;
}

/* A on a player of the roster: what only a friend's row can do (a stranger's code is not known to the game) */
static int backend_roster_action(int action, struct screens_roster_row const *row, char *answer, int size)
{
	unsigned int friend_id;

	if (!be_refresh())
	{
		be_answer_set(answer, size, "FRIENDS ARE OFF.");
		return 0;
	}
	switch (action)
	{
	case SCREENS_ACTION_ADD_FRIEND:
		be_answer_set(answer, size, "THAT PLAYER'S CODE IS NOT KNOWN. ASK FOR IT: FRIENDS > ADD FRIEND.");
		return 0;
	case SCREENS_ACTION_INVITE_PARTY:
		friend_id = screens_social_roster_friend(row->name, "");
		if (!friend_id)
		{
			be_answer_set(answer, size, "ONLY A FRIEND CAN BE INVITED TO A PARTY.");
			return 0;
		}
		be.changes++;
		if (!be.snapshot.party.active)
			social_party_make();
		social_party_invite(friend_id);
		be_answer_set(answer, size, "PARTY INVITATION SENT.");
		return 1;
	default:
		be_answer_set(answer, size, "THAT IS NOT AVAILABLE IN THIS BUILD YET.");
		return 0;
	}
}

/* ---------- the join, for the screen it is shown on */

int screens_social_join_line(char *line, int size)
{
	struct social_join_status status;

	if (size <= 0)
		return 0;
	line[0] = 0;
	social_join_status_get(&status);
	if (status.state == SOCIAL_JOIN_WORKING)
	{
		char name[SCREENS_NAME_SIZE];

		screens_social_upper(name, sizeof(name), status.target.name[0] ? status.target.name : be.join_name);
		snprintf(line, (size_t)size, "JOINING%s%s%s", name[0] ? " " : "", name, status.map_download ? ": THE MAP DOWNLOADS AS YOU JOIN." : "...");
		return 1;
	}
	if (status.state == SOCIAL_JOIN_READY)
	{
		/* (the list presses A for the game; a screen in the way, or the leaving of a game, is said) */
		if (social_flow_waiting_text()[0])
			be_sentence(line, size, social_flow_waiting_text());
		else
			snprintf(line, (size_t)size, "CONNECTED. JOINING THE GAME...");
		return 1;
	}
	if (social_flow_leaving())
	{
		be_sentence(line, size, social_flow_waiting_text()[0] ? social_flow_waiting_text() : "Leaving your game");
		return 1;
	}
	if (status.state == SOCIAL_JOIN_FAILED && status.text[0])
	{
		be_sentence(line, size, status.text);
		return 1;
	}
	return 0;
}

/* ---------- the table */

static struct screens_provider const be_table =
{
	"social",
	backend_generation,
	/* the roster, the bot buttons, the roster's action: the game's own (menu_screens.inl) */
	NULL,
	backend_friends,
	backend_party,
	backend_toast_pop,
	backend_invite_answer,
	backend_roster_action,
	NULL,
	NULL,
	backend_friend_action,
	backend_party_action,
	backend_friend_add,
	NULL,
	backend_friend_actions,
	backend_party_actions,
};

struct screens_provider const *screens_social_backend(void)
{
	return &be_table;
}
