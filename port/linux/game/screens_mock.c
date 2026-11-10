/*
SCREENS_MOCK.C

The provider the screens use while no backend exists, and the one the tests and the pictures use: made-up players (with bots at every skill, and the longest names the screens must
fit), friends in every state a friend can be in, requests, blocked people, a party of five whose members are each in a different state, and toasts. Its actions change the
made-up data (so a screen's navigation can be seen to act), and each change moves the generation, as a real backend's would. No game types; screens.h has the interface and
what a real backend must fill.
*/

#include "screens.h"
#include "text_safe.h"

#include <stdio.h>
#include <string.h>

static struct
{
	unsigned long generation;
	struct screens_roster_row roster[SCREENS_ROSTER_MAXIMUM];
	int roster_count;
	int skill;
	int bots_wanted;
	struct screens_friend_row friends[SCREENS_TABS][SCREENS_FRIENDS_MAXIMUM];
	int friends_count[SCREENS_TABS];
	struct screens_party_member party[SCREENS_PARTY_MAXIMUM];
	int party_count;
	int follow;
	struct screens_toast toasts[SCREENS_TOASTS_MAXIMUM];
	int toast_count, toast_next;
	int next_machine;
	int next_key;
	char last_answer[SCREENS_LINE_SIZE];
} mock;

static void roster_add(char const *name, int team, int skill, unsigned short flags, int ping, int score)
{
	struct screens_roster_row *row = &mock.roster[mock.roster_count++];

	memset(row, 0, sizeof(*row));
	snprintf(row->name, sizeof(row->name), "%s", name);
	row->team = (signed char)team;
	row->skill = (signed char)skill;
	row->flags = (unsigned short)(flags | (skill >= 0 ? SCREENS_ROW_BOT : 0));
	row->ping = (short)ping;
	row->score = (short)score;
	row->machine = skill >= 0 ? 113 + mock.next_machine++ : mock.next_key++;
}

static void friend_add_row(int tab, char const *name, char const *code, int presence, int join, int players, int maximum, char const *map, char const *gametype, int party)
{
	struct screens_friend_row *row = &mock.friends[tab][mock.friends_count[tab]++];

	memset(row, 0, sizeof(*row));
	snprintf(row->name, sizeof(row->name), "%s", name);
	snprintf(row->code, sizeof(row->code), "%s", code);
	row->presence = (unsigned char)presence;
	row->join = (unsigned char)join;
	row->players = (short)players;
	row->maximum = (short)maximum;
	snprintf(row->map, sizeof(row->map), "%s", map);
	snprintf(row->gametype, sizeof(row->gametype), "%s", gametype);
	row->party = (unsigned char)party;
	row->key = mock.next_key++;
}

static void party_add(char const *name, int leader, int me, int state, int amount)
{
	struct screens_party_member *member = &mock.party[mock.party_count++];

	memset(member, 0, sizeof(*member));
	snprintf(member->name, sizeof(member->name), "%s", name);
	member->leader = (unsigned char)leader;
	member->me = (unsigned char)me;
	member->state = (unsigned char)state;
	member->amount = (short)amount;
	member->key = mock.next_key++;
}

static void toast_add(int kind, char const *text, char const *detail)
{
	struct screens_toast *toast = &mock.toasts[mock.toast_count++];

	memset(toast, 0, sizeof(*toast));
	toast->id = 1000 + mock.toast_count;
	toast->kind = (unsigned char)kind;
	snprintf(toast->text, sizeof(toast->text), "%s", text);
	snprintf(toast->detail, sizeof(toast->detail), "%s", detail);
}

void screens_mock_reset(void)
{
	unsigned long generation = mock.generation + 1;

	memset(&mock, 0, sizeof(mock));
	mock.generation = generation;
	mock.next_key = 1;
	mock.skill = 3;
	mock.bots_wanted = 6;
	mock.follow = 1;
	/* the players of a Blood Gulch Slayer game with teams: this machine is the host (BOB), friends and a party member among them, bots at every skill, and the two longest names a
	11 character ASCII name can be (the widest letters, and the widest signs) */
	roster_add("BOB", SCREENS_TEAM_RED, -1, SCREENS_ROW_ME | SCREENS_ROW_HOST, -1, 12);
	roster_add("ALICE", SCREENS_TEAM_RED, -1, SCREENS_ROW_FRIEND | SCREENS_ROW_SPEAKING | SCREENS_ROW_PARTY, 32, 9);
	roster_add("JOHNSON", SCREENS_TEAM_BLUE, 3, 0, -1, 8);
	roster_add("JENKINS", SCREENS_TEAM_BLUE, 1, 0, -1, 2);
	roster_add("CARL", SCREENS_TEAM_BLUE, -1, SCREENS_ROW_FRIEND, 61, 7);
	roster_add("WWWWWWWWWWW", SCREENS_TEAM_RED, -1, 0, 118, 3);
	roster_add("DAN", SCREENS_TEAM_BLUE, -1, SCREENS_ROW_PARTY | SCREENS_ROW_MUTED, 204, 5);
	roster_add("MIRANDA", SCREENS_TEAM_RED, 0, 0, -1, 0);
	roster_add("%@%@%@%@%@%", SCREENS_TEAM_BLUE, -1, 0, 48, 4);
	roster_add("STERLING", SCREENS_TEAM_RED, 5, 0, -1, 14);
	roster_add("HAYES", SCREENS_TEAM_BLUE, 4, 0, -1, 11);
	roster_add("DUBBO", SCREENS_TEAM_RED, 2, 0, -1, 6);
	/* friends: in every state; the tail has the long map names, a full game and a name of the widest letters */
	friend_add_row(SCREENS_TAB_FRIENDS, "ALICE", "7F3K-9QXA-2M4D-R", SCREENS_PRESENCE_PLAYING, SCREENS_JOIN_OPEN, 6, 16, "bloodgulch", "Slayer", 1);
	friend_add_row(SCREENS_TAB_FRIENDS, "CARL", "9FQ2-KD4A-7HNE-3", SCREENS_PRESENCE_LOBBY, SCREENS_JOIN_OPEN, 3, 8, "Infinity", "CTF", 0);
	friend_add_row(SCREENS_TAB_FRIENDS, "DAN", "3M7P-XK2V-9QHA-8", SCREENS_PRESENCE_ONLINE, SCREENS_JOIN_NONE, 0, 0, "", "", 1);
	friend_add_row(SCREENS_TAB_FRIENDS, "ERIN", "H2D8-5TQW-KJ4N-6", SCREENS_PRESENCE_AWAY, SCREENS_JOIN_NONE, 0, 0, "", "", 0);
	friend_add_row(SCREENS_TAB_FRIENDS, "FRAN", "Q9X3-7ZRB-2HDM-1", SCREENS_PRESENCE_OFFLINE, SCREENS_JOIN_NONE, 0, 0, "", "", 0);
	friend_add_row(SCREENS_TAB_FRIENDS, "GRETA", "5KJ8-QW3T-9N2P-4", SCREENS_PRESENCE_PLAYING, SCREENS_JOIN_FULL, 16, 16, "chillout", "Slayer", 0);
	friend_add_row(SCREENS_TAB_FRIENDS, "HUGO", "8TQ2-3ZXN-5KD7-M", SCREENS_PRESENCE_PLAYING, SCREENS_JOIN_NEEDS_MAP, 4, 12, "Prisoner_Of_Hyrule_Remastered", "King of the Hill", 0);
	friend_add_row(SCREENS_TAB_FRIENDS, "IVAN", "2D5H-KQ8W-4XNT-9", SCREENS_PRESENCE_PLAYING, SCREENS_JOIN_PASSWORD, 7, 16, "wizard", "Oddball", 0);
	friend_add_row(SCREENS_TAB_FRIENDS, "JUDY", "6QH4-DM9K-3TXW-2", SCREENS_PRESENCE_PLAYING, SCREENS_JOIN_NEWER_BUILD, 5, 16, "sidewinder", "Slayer", 0);
	friend_add_row(SCREENS_TAB_FRIENDS, "KARL", "4N7T-2QKD-8XHP-5", SCREENS_PRESENCE_PLAYING, SCREENS_JOIN_DOWNLOAD, 8, 16, "Danger_Canyon_Reborn", "Team Slayer", 0);
	friend_add_row(SCREENS_TAB_FRIENDS, "LENA", "J8K2-4DQH-5XWT-7", SCREENS_PRESENCE_PLAYING, SCREENS_JOIN_CLOSED, 14, 16, "carousel", "Race", 0);
	friend_add_row(SCREENS_TAB_FRIENDS, "WWWWWWWWWWW", "Z3M9-7PQK-2TXD-V", SCREENS_PRESENCE_PLAYING, SCREENS_JOIN_DOWNLOAD, 16, 16, "Hang_Em_High_Extended_Edition", "Capture the Flag", 0);
	/* requests: two that were got, one that was sent (waiting for the other to be online) */
	friend_add_row(SCREENS_TAB_REQUESTS, "MALLORY", "9FQ2-KD4A-3TXW-8", SCREENS_PRESENCE_REQUEST_GOT, SCREENS_JOIN_NONE, 0, 0, "", "", 0);
	friend_add_row(SCREENS_TAB_REQUESTS, "NIA", "K4D7-3QHX-9TWP-2", SCREENS_PRESENCE_REQUEST_GOT, SCREENS_JOIN_NONE, 0, 0, "", "", 0);
	friend_add_row(SCREENS_TAB_REQUESTS, "OSCAR", "7WX2-Q9KD-4HTN-5", SCREENS_PRESENCE_REQUEST_SENT, SCREENS_JOIN_NONE, 0, 0, "", "", 0);
	friend_add_row(SCREENS_TAB_BLOCKED, "TROLL1", "2XQ9-KD38-7HTW-4", SCREENS_PRESENCE_BLOCKED, SCREENS_JOIN_NONE, 0, 0, "", "", 0);
	friend_add_row(SCREENS_TAB_BLOCKED, "SPAMMER", "8HD4-3QKX-2TWN-9", SCREENS_PRESENCE_BLOCKED, SCREENS_JOIN_NONE, 0, 0, "", "", 0);
	/* a party of five, each member in another state (the failed stay with their reason) */
	party_add("BOB", 1, 1, SCREENS_MEMBER_READY, 0);
	party_add("ALICE", 0, 0, SCREENS_MEMBER_JOINING, 40);
	party_add("CARL", 0, 0, SCREENS_MEMBER_DOWNLOADING, 12);
	party_add("DAN", 0, 0, SCREENS_MEMBER_NEEDS_MAP, 0);
	party_add("ERIN", 0, 0, SCREENS_MEMBER_FULL_RETRYING, 14);
	/* toasts, in the order they come */
	toast_add(SCREENS_TOAST_FRIEND_ONLINE, "ALICE IS ONLINE", "");
	toast_add(SCREENS_TOAST_INVITE, "ALICE INVITES YOU", "Blood Gulch - Slayer - 6 of 16");
	toast_add(SCREENS_TOAST_PARTY_JOINED, "DAN JOINED YOUR PARTY", "");
	toast_add(SCREENS_TOAST_JOIN_FAILED, "ALICE'S GAME IS FULL.", "16 of 16 players. A retry in 20 s.");
	toast_add(SCREENS_TOAST_REQUEST, "MALLORY WANTS TO BE YOUR FRIEND", "Friends > Requests");
}

int screens_mock_toasts_left(void)
{
	return mock.toast_count - mock.toast_next;
}

static void ensure(void)
{
	if (!mock.next_key)
		screens_mock_reset();
}

static unsigned long mock_generation(void)
{
	ensure();
	return mock.generation;
}

static int mock_roster(struct screens_roster_row *rows, int maximum, struct screens_roster_summary *summary)
{
	int count, index, bots = 0;

	ensure();
	count = mock.roster_count < maximum ? mock.roster_count : maximum;
	memcpy(rows, mock.roster, (size_t)count * sizeof(rows[0]));
	for (index = 0; index < count; index++)
		bots += (rows[index].flags & SCREENS_ROW_BOT) != 0;
	if (summary)
	{
		memset(summary, 0, sizeof(*summary));
		summary->players = (short)count;
		summary->bots = (short)bots;
		summary->maximum = 16;
		summary->skill = (short)mock.skill;
		summary->bots_wanted = (short)mock.bots_wanted;
		summary->host = 1;
		summary->teams = 1;
		summary->running = 1;
		snprintf(summary->map, sizeof(summary->map), "bloodgulch");
		snprintf(summary->gametype, sizeof(summary->gametype), "Slayer");
	}
	return count;
}

static int mock_friends(int tab, struct screens_friend_row *rows, int maximum, struct screens_friends_summary *summary)
{
	int count, index, online = 0;

	ensure();
	if (tab < 0 || tab >= SCREENS_TABS)
		return 0;
	count = mock.friends_count[tab] < maximum ? mock.friends_count[tab] : maximum;
	memcpy(rows, mock.friends[tab], (size_t)count * sizeof(rows[0]));
	if (summary)
	{
		memset(summary, 0, sizeof(*summary));
		for (index = 0; index < count; index++)
			online += rows[index].presence != SCREENS_PRESENCE_OFFLINE;
		snprintf(summary->own_code, sizeof(summary->own_code), "7F3K-9QXA-2M4D-R");
		summary->service = SCREENS_SERVICE_CONNECTED;
		summary->online = (short)online;
	}
	return count;
}

static int mock_party(struct screens_party_member *members, int maximum, struct screens_party_summary *summary)
{
	int count;

	ensure();
	count = mock.party_count < maximum ? mock.party_count : maximum;
	memcpy(members, mock.party, (size_t)count * sizeof(members[0]));
	if (summary)
	{
		memset(summary, 0, sizeof(*summary));
		summary->present = count > 0;
		summary->size = (short)count;
		summary->maximum = SCREENS_PARTY_MAXIMUM;
		summary->follow = (unsigned char)mock.follow;
		summary->room = 1;
		summary->i_lead = 1;
		snprintf(summary->leader_game, sizeof(summary->leader_game), "bloodgulch, 6 of 16");
	}
	return count;
}

static int mock_toast_pop(struct screens_toast *toast)
{
	ensure();
	if (mock.toast_next >= mock.toast_count)
		return 0;
	*toast = mock.toasts[mock.toast_next++];
	return 1;
}

static void mock_invite_answer(long id, int accept)
{
	(void)id;
	(void)accept;
	mock.generation++;
}

static void answer_set(char *answer, int size, char const *text)
{
	if (answer && size > 0)
		snprintf(answer, (size_t)size, "%s", text);
}

static int roster_find(long machine)
{
	int index;

	for (index = 0; index < mock.roster_count; index++)
	{
		if (mock.roster[index].machine == machine)
			return index;
	}
	return -1;
}

static void roster_remove(int index)
{
	memmove(&mock.roster[index], &mock.roster[index + 1], (size_t)(mock.roster_count - index - 1) * sizeof(mock.roster[0]));
	mock.roster_count--;
}

static int mock_roster_action(int action, struct screens_roster_row const *row, char *answer, int size)
{
	int index;

	ensure();
	index = roster_find(row->machine);
	if (index < 0)
	{
		answer_set(answer, size, "THAT PLAYER LEFT.");
		return 0;
	}
	switch (action)
	{
	case SCREENS_ACTION_MUTE:
		mock.roster[index].flags |= SCREENS_ROW_MUTED;
		break;
	case SCREENS_ACTION_UNMUTE:
		mock.roster[index].flags &= (unsigned short)~SCREENS_ROW_MUTED;
		break;
	case SCREENS_ACTION_KICK:
	case SCREENS_ACTION_BAN:
	case SCREENS_ACTION_DROP_BOT:
		roster_remove(index);
		break;
	case SCREENS_ACTION_BOT_SKILL_UP:
		if (mock.roster[index].skill < SCREENS_SKILLS - 1)
			mock.roster[index].skill++;
		break;
	case SCREENS_ACTION_BOT_SKILL_DOWN:
		if (mock.roster[index].skill > 0)
			mock.roster[index].skill--;
		break;
	case SCREENS_ACTION_CHANGE_TEAM:
		mock.roster[index].team = (signed char)(mock.roster[index].team == SCREENS_TEAM_RED ? SCREENS_TEAM_BLUE : SCREENS_TEAM_RED);
		break;
	case SCREENS_ACTION_ADD_FRIEND:
		mock.roster[index].flags |= SCREENS_ROW_FRIEND;
		answer_set(answer, size, "FRIEND REQUEST SENT.");
		break;
	case SCREENS_ACTION_INVITE_PARTY:
		mock.roster[index].flags |= SCREENS_ROW_PARTY;
		break;
	case SCREENS_ACTION_VOTE_KICK:
		answer_set(answer, size, "A VOTE TO KICK HAS BEGUN.");
		return 1;
	default:
		return 0;
	}
	mock.generation++;
	return 1;
}

static int mock_bot_add(int team, int skill, char *answer, int size)
{
	static char const *const names[] = { "JOHNSON", "JENKINS", "MIRANDA", "STERLING", "HAYES", "DUBBO", "LOCKE", "ARMSTRONG", "CORTANA", "BISHOP", "WALKER", "DARE", "KEYES",
		"SILVA", "REYES" };
	int bots = 0, index;

	ensure();
	for (index = 0; index < mock.roster_count; index++)
		bots += (mock.roster[index].flags & SCREENS_ROW_BOT) != 0;
	if (mock.roster_count >= 16)
	{
		answer_set(answer, size, "THE GAME IS FULL.");
		return 0;
	}
	if (bots >= 15)
	{
		answer_set(answer, size, "THAT IS THE MOST BOTS A GAME TAKES.");
		return 0;
	}
	if (team == SCREENS_TEAM_NONE)
	{
		int red = 0, blue = 0;

		for (index = 0; index < mock.roster_count; index++)
		{
			red += mock.roster[index].team == SCREENS_TEAM_RED;
			blue += mock.roster[index].team == SCREENS_TEAM_BLUE;
		}
		team = red <= blue ? SCREENS_TEAM_RED : SCREENS_TEAM_BLUE;
	}
	roster_add(names[bots % 15], team, skill < 0 ? mock.skill : skill, 0, -1, 0);
	mock.generation++;
	return 1;
}

static int mock_bot_drop(long machine, char *answer, int size)
{
	int index = -1, found;

	ensure();
	if (machine >= 0)
		index = roster_find(machine);
	else
	{
		for (found = mock.roster_count - 1; found >= 0 && index < 0; found--)
		{
			if (mock.roster[found].flags & SCREENS_ROW_BOT)
				index = found;
		}
	}
	if (index < 0 || !(mock.roster[index].flags & SCREENS_ROW_BOT))
	{
		answer_set(answer, size, "THERE IS NO BOT TO DROP.");
		return 0;
	}
	roster_remove(index);
	mock.generation++;
	return 1;
}

static int friend_find(int tab, long key)
{
	int index;

	for (index = 0; index < mock.friends_count[tab]; index++)
	{
		if (mock.friends[tab][index].key == key)
			return index;
	}
	return -1;
}

static void friend_move(int from, int index, int to, int presence)
{
	struct screens_friend_row row = mock.friends[from][index];

	memmove(&mock.friends[from][index], &mock.friends[from][index + 1], (size_t)(mock.friends_count[from] - index - 1) * sizeof(row));
	mock.friends_count[from]--;
	if (to >= 0 && mock.friends_count[to] < SCREENS_FRIENDS_MAXIMUM)
	{
		row.presence = (unsigned char)presence;
		mock.friends[to][mock.friends_count[to]++] = row;
	}
}

static int mock_friend_action(int action, struct screens_friend_row const *row, char *answer, int size)
{
	int tab, index = -1;

	ensure();
	for (tab = 0; tab < SCREENS_TABS; tab++)
	{
		if (tab == SCREENS_TAB_PARTY)
			continue;
		index = friend_find(tab, row->key);
		if (index >= 0)
			break;
	}
	if (tab == SCREENS_TABS)
	{
		answer_set(answer, size, "THAT FRIEND IS NOT ON YOUR LIST.");
		return 0;
	}
	switch (action)
	{
	case SCREENS_ACTION_REMOVE_FRIEND:
		friend_move(tab, index, -1, 0);
		break;
	case SCREENS_ACTION_BLOCK:
		friend_move(tab, index, SCREENS_TAB_BLOCKED, SCREENS_PRESENCE_BLOCKED);
		break;
	case SCREENS_ACTION_UNBLOCK:
	case SCREENS_ACTION_DECLINE:
		friend_move(tab, index, -1, 0);
		break;
	case SCREENS_ACTION_ACCEPT:
		friend_move(tab, index, SCREENS_TAB_FRIENDS, SCREENS_PRESENCE_ONLINE);
		break;
	case SCREENS_ACTION_JOIN_GAME:
		if (row->join == SCREENS_JOIN_FULL)
		{
			answer_set(answer, size, "THE GAME IS FULL.");
			return 0;
		}
		answer_set(answer, size, "JOINING...");
		return 1;
	case SCREENS_ACTION_INVITE_GAME:
	case SCREENS_ACTION_INVITE_PARTY:
		answer_set(answer, size, "INVITATION SENT.");
		return 1;
	case SCREENS_ACTION_VIEW_CODE:
		answer_set(answer, size, row->code);
		return 1;
	default:
		return 0;
	}
	mock.generation++;
	return 1;
}

static int mock_party_action(int action, struct screens_party_member const *member, char *answer, int size)
{
	int index;

	ensure();
	(void)answer;
	(void)size;
	switch (action)
	{
	case SCREENS_ACTION_FOLLOW_ON:
	case SCREENS_ACTION_FOLLOW_OFF:
		mock.follow = action == SCREENS_ACTION_FOLLOW_ON;
		break;
	case SCREENS_ACTION_LEAVE_PARTY:
	case SCREENS_ACTION_DISBAND_PARTY:
		mock.party_count = 0;
		break;
	case SCREENS_ACTION_REMOVE_FROM_PARTY:
	case SCREENS_ACTION_MAKE_LEADER:
		for (index = 0; index < mock.party_count && mock.party[index].key != member->key; index++)
			;
		if (index == mock.party_count)
			return 0;
		if (action == SCREENS_ACTION_MAKE_LEADER)
		{
			int other;

			for (other = 0; other < mock.party_count; other++)
				mock.party[other].leader = (unsigned char)(other == index);
		}
		else
		{
			memmove(&mock.party[index], &mock.party[index + 1], (size_t)(mock.party_count - index - 1) * sizeof(mock.party[0]));
			mock.party_count--;
		}
		break;
	default:
		return 0;
	}
	mock.generation++;
	return 1;
}

static int mock_friend_add(char const *code, char *answer, int size)
{
	char normalised[TEXT_SAFE_CODE_LENGTH + 2];
	short length;
	int tab, index;

	ensure();
	length = text_safe_code_normalise(normalised, (short)sizeof(normalised), code);
	if (length != TEXT_SAFE_CODE_LENGTH)
	{
		answer_set(answer, size, length < 0 ? "NOT A CODE." : "CHECK THE CODE: IT IS 13 CHARACTERS.");
		return 0;
	}
	if (!memcmp(normalised, "7F3K9QXA2M4DR", TEXT_SAFE_CODE_LENGTH))
	{
		answer_set(answer, size, "THAT IS YOUR OWN CODE.");
		return 0;
	}
	for (tab = 0; tab < SCREENS_TABS; tab++)
	{
		for (index = 0; tab != SCREENS_TAB_PARTY && index < mock.friends_count[tab]; index++)
		{
			char other[SCREENS_CODE_SIZE];
			short other_length = text_safe_code_normalise(other, (short)sizeof(other), mock.friends[tab][index].code);

			if (other_length == TEXT_SAFE_CODE_LENGTH && !memcmp(other, normalised, TEXT_SAFE_CODE_LENGTH))
			{
				answer_set(answer, size, tab == SCREENS_TAB_FRIENDS ? "ALREADY A FRIEND." : tab == SCREENS_TAB_BLOCKED ? "THAT PLAYER IS BLOCKED." : "THERE IS A REQUEST WITH THAT CODE ALREADY.");
				return 0;
			}
		}
	}
	if (mock.friends_count[SCREENS_TAB_REQUESTS] < SCREENS_FRIENDS_MAXIMUM)
	{
		char text[SCREENS_CODE_SIZE];

		snprintf(text, sizeof(text), "%.4s-%.4s-%.4s-%.1s", normalised, normalised + 4, normalised + 8, normalised + 12);
		friend_add_row(SCREENS_TAB_REQUESTS, "NEW FRIEND", text, SCREENS_PRESENCE_REQUEST_SENT, SCREENS_JOIN_NONE, 0, 0, "", "", 0);
	}
	answer_set(answer, size, "REQUEST SENT. THEY WILL SEE IT WHEN THEY ARE ONLINE.");
	mock.generation++;
	return 1;
}

static struct screens_provider const mock_table =
{
	"mock", mock_generation, mock_roster, mock_friends, mock_party, mock_toast_pop, mock_invite_answer, mock_roster_action, mock_bot_add, mock_bot_drop, mock_friend_action,
	mock_party_action, mock_friend_add, screens_default_roster_actions, screens_default_friend_actions, screens_default_party_actions,
};

struct screens_provider const *screens_mock_provider(void)
{
	return &mock_table;
}
