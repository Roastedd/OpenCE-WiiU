/* screens_dump: what the social screens show for a state of the mock provider, as JSON, for tools/screens_preview.py to draw (cc -I port/linux/game ... screens_dump.c).
   It calls the same functions menu_screens.inl does (the cells, the details, the heads), so the picture has the texts the menus would have.

   screens_dump SCENARIO [FOCUS [FIRST]]
   players | players-client | players-empty | players16 | actions-person | actions-bot | actions-me | friends | party | requests | blocked | friends50 | friends-empty | friends-offline
   | friends-disabled | friends-nofriendservice | requests-empty | party-empty | blocked-empty | add-friend TEXT | lobby | toasts

   players16 and friends50 are made up here (the widest names, every flag and every state of a friend's game, the longest maps), not the mock's: the layout checks (tools/test_screens_layout.py) run
   them at both sizes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "text_safe.h"
#include "screens.h"

long config_integer(const char *name)
{
	(void)name;
	return 1;
}

static void json_string(char const *text)
{
	putchar('"');
	for (; *text; text++)
	{
		if (*text == '\r')
		{
			fputs("\\n", stdout);
			continue;
		}
		if (*text == '"' || *text == '\\')
			putchar('\\');
		putchar(*text);
	}
	putchar('"');
}

static void json_cell(struct screens_cell const *cell)
{
	printf("{\"text\": ");
	json_string(cell->text);
	printf(", \"rgb\": %lu}", screens_color_rgb(cell->color));
}

static void json_cells(struct screens_cell const *cells, int count)
{
	int index;

	printf("[");
	for (index = 0; index < count; index++)
	{
		if (index)
			printf(", ");
		json_cell(&cells[index]);
	}
	printf("]");
}

static void json_actions(int const *actions)
{
	int index;

	printf("\"actions\": [");
	for (index = 0; actions[index] != SCREENS_ACTION_NONE; index++)
	{
		if (index)
			printf(", ");
		json_string(screens_action_word(actions[index]));
	}
	printf("]");
}

/* sixteen players with the widest names and every flag a row can have */
static int make_players16(struct screens_roster_row *rows)
{
	static char const *const names[16] = { "WWWWWWWWWWW", "%@%@%@%@%@%", "MMMMMMMMMMM", "iiiiiiiiiii", "Alice", "BOB", "Carl Jr", "DAN", "ERIN", "WWWWWWWWWW", "gjpqy_gjpqy", "Hayes",
		"JOHNSON", "STERLING", "MIRANDA", "DUBBO" };
	int index;

	for (index = 0; index < 16; index++)
	{
		memset(&rows[index], 0, sizeof(rows[index]));
		snprintf(rows[index].name, sizeof(rows[index].name), "%s", names[index]);
		rows[index].team = (signed char)(index % 2);
		rows[index].skill = -1;
		rows[index].ping = (short)(index % 3 == 0 ? -1 : 20 + index * 17);
		rows[index].score = (short)(index * 3);
		rows[index].machine = index;
		if (index == 0)
			rows[index].flags = SCREENS_ROW_ME | SCREENS_ROW_HOST;
		else if (index % 5 == 1)
		{
			rows[index].flags = SCREENS_ROW_BOT;
			rows[index].skill = (signed char)(index % SCREENS_SKILLS);
			rows[index].ping = -1;
		}
		else if (index % 5 == 2)
			rows[index].flags = SCREENS_ROW_FRIEND | ((index & 1) ? SCREENS_ROW_MUTED : SCREENS_ROW_SPEAKING);
		else if (index % 5 == 3)
			rows[index].flags = SCREENS_ROW_PARTY;
	}
	/* (the longest tag: a legendary bot; the longest ping) */
	rows[11].flags = SCREENS_ROW_BOT;
	rows[11].skill = SCREENS_SKILLS - 1;
	rows[11].ping = -1;
	rows[12].ping = 1500;
	return 16;
}

/* fifty friends: names of the widest letters, maps of 31 characters and of every length, every presence and every state of a game */
static int make_friends50(struct screens_friend_row *rows)
{
	static char const *const names[8] = { "WWWWWWWWWWW", "ALICE", "%@%@%@%@%@%", "Bob", "MMMMMMMMMMM", "Carl Jr", "gjpqy_gjpqy", "DAN" };
	static char const *const maps[8] = { "bloodgulch", "Danger_Canyon_Remastered_XL_2000", "hangemhigh", "Prisoner_Of_The_Longest_Name__", "a", "boardingaction", "gephyrophobia",
		"Sidewinder_Extended" };
	int index;

	for (index = 0; index < 50; index++)
	{
		struct screens_friend_row *row = &rows[index];

		memset(row, 0, sizeof(*row));
		snprintf(row->name, sizeof(row->name), "%s", index % 7 == 0 ? names[0] : names[index % 8]);
		snprintf(row->code, sizeof(row->code), "7F3K-9QXA-2M4D-%c", 'A' + index % 26);
		row->key = index + 1;
		row->presence = (unsigned char)(index % 5 == 4 ? SCREENS_PRESENCE_OFFLINE : index % 5 == 3 ? SCREENS_PRESENCE_AWAY : index % 5 == 2 ? SCREENS_PRESENCE_ONLINE :
			index % 5 == 1 ? SCREENS_PRESENCE_LOBBY : SCREENS_PRESENCE_PLAYING);
		if (row->presence == SCREENS_PRESENCE_PLAYING || row->presence == SCREENS_PRESENCE_LOBBY)
		{
			static unsigned char const joins[8] = { SCREENS_JOIN_OPEN, SCREENS_JOIN_DOWNLOAD, SCREENS_JOIN_FULL, SCREENS_JOIN_CLOSED, SCREENS_JOIN_PASSWORD, SCREENS_JOIN_NEWER_BUILD,
				SCREENS_JOIN_OLDER_BUILD, SCREENS_JOIN_NEEDS_MAP };

			row->join = joins[(index / 2) % 8];
			row->maximum = (short)(index % 3 ? 16 : 128);
			row->players = row->maximum;
			if (index % 4)
				row->players = (short)(index % row->maximum);
			snprintf(row->map, sizeof(row->map), "%s", maps[index % 8]);
			snprintf(row->gametype, sizeof(row->gametype), "Slayer");
		}
		row->party = (unsigned char)(index % 9 == 0);
	}
	rows[3].presence = SCREENS_PRESENCE_ONLINE;
	rows[3].party = 1;
	return 50;
}

static void json_scroll(char const *prefix, int focus, int first, int shown, int count)
{
	char text[SCREENS_LINE_SIZE];

	screens_scroll_text(focus, first, shown, count, text, sizeof(text));
	printf(", \"%s_count\": ", prefix);
	json_string(text);
	printf(", \"%s_up\": %d, \"%s_down\": %d", prefix, first > 0, prefix, first + shown < count);
}

int main(int argc, char **argv)
{
	char const *scenario = argc > 1 ? argv[1] : "players";
	int focus = argc > 2 && isdigit((unsigned char)argv[2][0]) ? atoi(argv[2]) : 0;
	int first = argc > 3 ? atoi(argv[3]) : 0;
	static struct screens_model model;
	struct screens_provider const *provider = screens_mock_provider();
	char text[SCREENS_LINE_SIZE];
	int index, count;
	struct screens_cell cells[8];

	screens_mock_reset();
	screens_model_update(&model, 1);
	printf("{\"scenario\": ");
	json_string(scenario);
	printf(", \"focus\": %d, \"first\": %d", focus, first);
	if (!strncmp(scenario, "players", 7) || !strncmp(scenario, "actions-", 8))
	{
		struct screens_roster_summary summary = model.roster_summary;
		struct screens_roster_row *roster = model.roster;

		if (!strcmp(scenario, "players-client"))
			summary.host = 0;
		if (!strcmp(scenario, "players16"))
		{
			model.roster_count = make_players16(model.roster);
			summary.players = 16;
			summary.maximum = 16;
			summary.bots = 6;
			summary.host = 1;
			summary.teams = 1;
			summary.running = 1;
		}
		count = !strcmp(scenario, "players-empty") ? 0 : model.roster_count;
		if (!strcmp(scenario, "players-empty"))
			summary.players = summary.bots = 0;
		screens_player_head(&summary, text, sizeof(text));
		printf(", \"head\": ");
		json_string(text);
		printf(", \"banner\": ");
		json_string(summary.running ? "THE GAME KEEPS RUNNING WHILE THIS IS OPEN." : "");
		printf(", \"host\": %d, \"count\": %d, \"shown\": %d, \"rows\": [", summary.host, count, count - first < SCREENS_PLAYER_ROWS ? count - first : SCREENS_PLAYER_ROWS);
		for (index = 0; index < count - first && index < SCREENS_PLAYER_ROWS; index++)
		{
			struct screens_roster_row const *row = &roster[first + index];

			screens_player_cells(row, &summary, cells);
			printf("%s{\"cells\": ", index ? ", " : "");
			json_cells(cells, SCREENS_PLAYER_CELLS);
			printf(", \"icon\": %d}", (row->flags & SCREENS_ROW_MUTED) ? 2 : (row->flags & SCREENS_ROW_SPEAKING) ? 1 : 0);
		}
		printf("]");
		json_scroll("players", count ? focus : -1, first, count - first < SCREENS_PLAYER_ROWS ? count - first : SCREENS_PLAYER_ROWS, count);
		if (count)
		{
			screens_player_detail(&roster[first + focus], &summary, text, sizeof(text));
			printf(", \"empty\": \"\"");
		}
		else
		{
			screens_empty_block(0, 0, NULL, 1, text, sizeof(text));
			printf(", \"empty\": ");
			json_string(text);
			text[0] = 0;
		}
		printf(", \"detail\": ");
		json_string(text);
		if (!strncmp(scenario, "actions-", 8))
		{
			int row_index = !strcmp(scenario, "actions-bot") ? 2 : !strcmp(scenario, "actions-me") ? 0 : 5;
			int actions[SCREENS_ACTIONS_MAXIMUM + 1];

			provider->roster_actions(&model.roster[row_index], &summary, actions);
			printf(", \"title\": ");
			json_string(model.roster[row_index].name);
			printf(", ");
			json_actions(actions);
		}
	}
	else if (!strncmp(scenario, "friends", 7) || !strncmp(scenario, "party", 5) || !strncmp(scenario, "requests", 8) || !strncmp(scenario, "blocked", 7))
	{
		int tab = !strncmp(scenario, "party", 5) ? SCREENS_TAB_PARTY : !strncmp(scenario, "requests", 8) ? SCREENS_TAB_REQUESTS : !strncmp(scenario, "blocked", 7) ? SCREENS_TAB_BLOCKED :
			SCREENS_TAB_FRIENDS;
		int have = strcmp(scenario, "friends-nofriendservice") != 0;
		int empty = strstr(scenario, "-empty") != NULL || strstr(scenario, "-offline") != NULL || strstr(scenario, "-disabled") != NULL || strstr(scenario, "-nofriendservice") != NULL;
		struct screens_friends_summary summary = model.friends_summary;

		if (!strcmp(scenario, "friends50"))
		{
			model.friends_count[SCREENS_TAB_FRIENDS] = make_friends50(model.friends[SCREENS_TAB_FRIENDS]);
			summary.online = 40;
		}
		if (empty)
		{
			memset(model.friends_count, 0, sizeof(model.friends_count));
			model.party_count = 0;
			model.party_summary.present = 0;
		}
		if (strstr(scenario, "-offline"))
			summary.service = SCREENS_SERVICE_OFF;
		if (strstr(scenario, "-disabled"))
			summary.service = SCREENS_SERVICE_DISABLED;
		count = tab == SCREENS_TAB_PARTY ? model.party_count : model.friends_count[tab];
		for (index = 0; index < SCREENS_TABS; index++)
			summary.counts[index] = (short)(index == SCREENS_TAB_PARTY ? model.party_count : model.friends_count[index]);
		printf(", \"tab\": %d, \"service\": ", tab);
		json_string(have ? screens_service_word(summary.service) : "NO FRIEND SERVICE");
		printf(", \"code\": ");
		snprintf(text, sizeof(text), "YOUR CODE: %s", summary.own_code);
		json_string(have ? text : "");
		printf(", \"tabs\": [");
		for (index = 0; index < SCREENS_TABS; index++)
		{
			screens_tab_label(index, index == tab, &summary, &model.party_summary, text, sizeof(text));
			printf("%s", index ? ", " : "");
			json_string(text);
		}
		printf("], \"count\": %d, \"shown\": %d, \"rows\": [", count, count - first < SCREENS_FRIEND_ROWS ? count - first : SCREENS_FRIEND_ROWS);
		for (index = 0; index < count - first && index < SCREENS_FRIEND_ROWS; index++)
		{
			if (tab == SCREENS_TAB_PARTY)
				screens_member_cells(&model.party[first + index], cells);
			else
				screens_friend_cells(tab, &model.friends[tab][first + index], cells);
			printf("%s{\"cells\": ", index ? ", " : "");
			json_cells(cells, SCREENS_FRIEND_CELLS);
			printf("}");
		}
		printf("]");
		json_scroll("friends", count ? focus : -1, first, count - first < SCREENS_FRIEND_ROWS ? count - first : SCREENS_FRIEND_ROWS, count);
		if (!count || !have)
		{
			screens_empty_block(1, tab, &summary, have, text, sizeof(text));
			printf(", \"empty\": ");
			json_string(text);
			text[0] = 0;
		}
		else
		{
			printf(", \"empty\": \"\"");
			if (tab == SCREENS_TAB_PARTY)
				screens_party_head(&model.party_summary, text, sizeof(text));
			else
				screens_friend_detail(tab, &model.friends[tab][first + focus], &summary, text, sizeof(text));
		}
		printf(", \"detail\": ");
		json_string(text);
		{
			int actions[SCREENS_ACTIONS_MAXIMUM + 1];

			if (tab != SCREENS_TAB_PARTY && model.friends_count[tab])
			{
				provider->friend_actions(tab, &model.friends[tab][first + focus], actions);
				printf(", \"title\": ");
				json_string(model.friends[tab][first + focus].name);
				printf(", ");
				json_actions(actions);
			}
		}
		{
			struct screens_friend_row const *row = tab != SCREENS_TAB_PARTY && count ? &model.friends[tab][first + focus] : NULL;
			int joinable = row && tab == SCREENS_TAB_FRIENDS && (row->presence == SCREENS_PRESENCE_PLAYING || row->presence == SCREENS_PRESENCE_LOBBY) &&
				(row->join == SCREENS_JOIN_OPEN || row->join == SCREENS_JOIN_DOWNLOAD);
			int online = row && tab == SCREENS_TAB_FRIENDS && row->presence != SCREENS_PRESENCE_OFFLINE;

			printf(", \"join\": %d, \"invite\": %d, \"add\": %d", joinable, online, have);
		}
	}
	else if (!strcmp(scenario, "add-friend"))
	{
		printf(", \"own\": ");
		json_string(model.friends_summary.own_code);
		printf(", \"typed\": ");
		json_string(argc > 4 ? argv[4] : "");
		/* (the answers a request gets, as the screen puts them in its two lines) */
		{
			char wrapped[SCREENS_LINE_SIZE * 2];

			screens_wrap(SCREENS_FONT_LARGE, (float)SCREENS_LINE_BOX_WIDTH, SCREENS_LINE_BOX_LINES, "REQUEST SENT. THEY WILL SEE IT WHEN THEY ARE ONLINE.", wrapped, sizeof(wrapped));
			printf(", \"sent\": ");
			json_string(wrapped);
			/* (and any sentence given as the fifth argument, as the screen would break it) */
			screens_wrap(SCREENS_FONT_LARGE, (float)SCREENS_LINE_BOX_WIDTH, SCREENS_LINE_BOX_LINES, argc > 5 ? argv[5] : "", wrapped, sizeof(wrapped));
			printf(", \"wrapped\": ");
			json_string(wrapped);
		}
	}
	else if (!strcmp(scenario, "lobby"))
	{
		printf(", \"rows\": [");
		for (index = 0; index < model.roster_count && index < 11; index++)
		{
			screens_lobby_cells(&model.roster[index], 1, cells);
			printf("%s{\"cells\": ", index ? ", " : "");
			json_cells(cells, SCREENS_LOBBY_CELLS);
			printf("}");
		}
		printf("]");
	}
	else if (!strcmp(scenario, "toasts"))
	{
		struct screens_toast toast;

		printf(", \"toasts\": [");
		index = 0;
		while (provider->toast_pop(&toast))
		{
			printf("%s{\"kind\": %d, \"text\": ", index++ ? ", " : "", toast.kind);
			json_string(toast.text);
			printf(", \"detail\": ");
			json_string(toast.detail);
			printf("}");
		}
		printf("]");
	}
	printf("}\n");
	return 0;
}
