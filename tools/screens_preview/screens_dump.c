/* screens_dump: what the social screens show for a state of the mock provider, as JSON, for tools/screens_preview.py to draw (cc -I port/linux/game ... screens_dump.c).
   It calls the same functions menu_screens.inl does (the cells, the details, the heads), so the picture has the texts the menus would have.

   screens_dump SCENARIO [FOCUS [FIRST]]
   players | players-client | players-empty | actions-person | actions-bot | actions-me | friends N | party | requests | blocked | friends-empty | friends-nofriendservice
   | add-friend TEXT | lobby | toasts */
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

		if (!strcmp(scenario, "players-client"))
			summary.host = 0;
		count = !strcmp(scenario, "players-empty") ? 0 : model.roster_count;
		if (!strcmp(scenario, "players-empty"))
			summary.players = summary.bots = 0;
		screens_player_head(&summary, text, sizeof(text));
		printf(", \"head\": ");
		json_string(text);
		printf(", \"banner\": ");
		json_string(summary.running ? "THE GAME KEEPS RUNNING WHILE THIS IS OPEN." : "");
		printf(", \"host\": %d, \"count\": %d, \"rows\": [", summary.host, count);
		for (index = 0; index < count - first && index < SCREENS_PLAYER_ROWS; index++)
		{
			struct screens_roster_row const *row = &model.roster[first + index];

			screens_player_cells(row, &summary, cells);
			printf("%s{\"cells\": ", index ? ", " : "");
			json_cells(cells, SCREENS_PLAYER_CELLS);
			printf(", \"icon\": %d}", (row->flags & SCREENS_ROW_MUTED) ? 2 : (row->flags & SCREENS_ROW_SPEAKING) ? 1 : 0);
		}
		printf("]");
		if (count)
		{
			screens_player_detail(&model.roster[first + focus], &summary, text, sizeof(text));
		}
		else
		{
			screens_empty_text(0, 0, NULL, 1, text, sizeof(text));
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
	else if (!strncmp(scenario, "friends", 7) || !strcmp(scenario, "party") || !strcmp(scenario, "requests") || !strcmp(scenario, "blocked") || !strcmp(scenario, "actions-friend"))
	{
		int tab = !strcmp(scenario, "party") ? SCREENS_TAB_PARTY : !strcmp(scenario, "requests") ? SCREENS_TAB_REQUESTS : !strcmp(scenario, "blocked") ? SCREENS_TAB_BLOCKED : SCREENS_TAB_FRIENDS;
		int have = strcmp(scenario, "friends-nofriendservice") != 0;
		struct screens_friends_summary summary = model.friends_summary;

		if (!strcmp(scenario, "friends-empty") || !strcmp(scenario, "friends-nofriendservice"))
			memset(model.friends_count, 0, sizeof(model.friends_count));
		if (!strcmp(scenario, "friends-offline"))
		{
			summary.service = SCREENS_SERVICE_OFF;
			memset(model.friends_count, 0, sizeof(model.friends_count));
		}
		count = tab == SCREENS_TAB_PARTY ? model.party_count : model.friends_count[tab];
		if (!strncmp(scenario, "friends-", 8))
			count = 0;
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
		printf("], \"count\": %d, \"rows\": [", count);
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
		if (!count || !have)
			screens_empty_text(1, tab, &summary, have, text, sizeof(text));
		else if (tab == SCREENS_TAB_PARTY)
			screens_party_head(&model.party_summary, text, sizeof(text));
		else
			screens_friend_detail(tab, &model.friends[tab][first + focus], &summary, text, sizeof(text));
		printf(", \"detail\": ");
		json_string(text);
		if (!strcmp(scenario, "actions-friend") || 1)
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
