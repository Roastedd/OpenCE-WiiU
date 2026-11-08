#include "harness.h"

/* The social screens' model (port/linux/game/screens.c, screens_mock.c): words and colours, the text cut to a column's width (every 11 character ASCII name, the widest letters
and signs, fits its column), the cells of every row in every state, the order the focus moves in, the toast queue and its timing, the model's rebuilding by generation, and the
mock provider's actions. */
#include "text_safe.h"
#include "screens.h"
#include "screens_view.h"

static long enable_setting = 1;

long config_integer(const char *name)
{
	(void)name;
	return enable_setting;
}

#include "text_safe_code.inc"
#include "screens_code.inc"
#include "screens_mock_code.inc"
#include "screens_view_code.inc"

static int ascii_clean(char const *text)
{
	for (; *text; text++)
	{
		if ((unsigned char)*text < 0x20 || (unsigned char)*text > 0x7E || *text == '|')
			return 0;
	}
	return 1;
}

static void words(void)
{
	int index;

	for (index = 0; index < SCREENS_SKILLS; index++)
	{
		CHECK(screens_skill_word(index)[0] && ascii_clean(screens_skill_word(index)), "skill %d", index);
	}
	CHECK(!strcmp(screens_skill_word(0), "RECRUIT") && !strcmp(screens_skill_word(5), "LEGENDARY") && !strcmp(screens_skill_word(3), "HEROIC"), "the skills in bots.c's order");
	CHECK(!screens_skill_word(6)[0] && !screens_skill_word(-1)[0], "no skill out of range");
	for (index = 1; index < SCREENS_ACTIONS; index++)
	{
		CHECK(screens_action_word(index)[0] && ascii_clean(screens_action_word(index)), "action %d", index);
	}
	for (index = 0; index < 8; index++)
	{
		CHECK(ascii_clean(screens_presence_word(index)) && ascii_clean(screens_join_word(index)) && ascii_clean(screens_service_word(index)), "presence, join, service %d", index);
	}
	for (index = 0; index < 11; index++)
	{
		CHECK(screens_member_word(index)[0] && ascii_clean(screens_member_word(index)), "member %d", index);
	}
	CHECK(!screens_member_word(11)[0], "no member state past the last");
	for (index = 0; index < SCREENS_TABS; index++)
	{
		CHECK(screens_tab_word(index)[0], "tab %d", index);
	}
	for (index = 0; index < SCREENS_COLORS; index++)
	{
		CHECK(screens_color_rgb(index) <= 0xFFFFFF, "colour %d", index);
	}
	CHECK(screens_color_rgb(SCREENS_COLOR_AZURE) == 0x2896FF && screens_color_rgb(SCREENS_COLOR_AMBER) == 0xFFB020 && screens_color_rgb(SCREENS_COLOR_GREEN) == 0x40D050,
		"the menus' colours");
}

/* the widest character of a font among those a name can have */
static char widest(int font)
{
	char best = 'W';
	int code;

	for (code = 0x20; code <= 0x7E; code++)
	{
		char one[2] = { (char)code, 0 }, other[2] = { best, 0 };

		if (code != '|' && screens_text_width(font, one) > screens_text_width(font, other))
			best = (char)code;
	}
	return best;
}

static void fit_names(void)
{
	char name[SCREENS_NAME_SIZE];
	struct screens_cell cells[8];
	struct screens_roster_row row;
	struct screens_friend_row friend_row;
	struct screens_party_member member;
	int code;

	for (code = 0x20; code <= 0x7E; code++)
	{
		int count;

		if (code == '|')
			continue;
		for (count = 0; count < SCREENS_NAME_LENGTH; count++)
			name[count] = (char)code;
		name[SCREENS_NAME_LENGTH] = 0;
		memset(&row, 0, sizeof(row));
		memcpy(row.name, name, sizeof(name));
		row.team = SCREENS_TEAM_BLUE;
		row.ping = 999;
		row.score = 9999;
		screens_player_cells(&row, NULL, cells);
		CHECK(!strcmp(cells[SCREENS_PLAYER_NAME].text, name), "players page, the name of 11 '%c' cut to '%s'", code, cells[SCREENS_PLAYER_NAME].text);
		screens_lobby_cells(&row, 1, cells);
		CHECK(!strcmp(cells[SCREENS_LOBBY_NAME].text, name), "lobby, the name of 11 '%c' cut to '%s'", code, cells[SCREENS_LOBBY_NAME].text);
		memset(&friend_row, 0, sizeof(friend_row));
		memcpy(friend_row.name, name, sizeof(name));
		screens_friend_cells(SCREENS_TAB_FRIENDS, &friend_row, cells);
		CHECK(!strcmp(cells[SCREENS_FRIEND_NAME].text, name), "friends, the name of 11 '%c' cut to '%s'", code, cells[SCREENS_FRIEND_NAME].text);
		memset(&member, 0, sizeof(member));
		memcpy(member.name, name, sizeof(name));
		screens_member_cells(&member, cells);
		CHECK(!strcmp(cells[SCREENS_MEMBER_NAME].text, name), "party, the name of 11 '%c' cut to '%s'", code, cells[SCREENS_MEMBER_NAME].text);
	}
	/* the widest letter really is the one that sets the column widths, and the columns have room for it */
	CHECK(11 * screens_text_width(SCREENS_FONT_LARGE, (char[2]){ widest(SCREENS_FONT_LARGE), 0 }) <= (float)SCREENS_LOBBY_NAME_WIDTH, "the widest name fits the lobby's column");
}

static float cell_limit(int kind, int index)
{
	static const float player[SCREENS_PLAYER_CELLS] = { SCREENS_PLAYER_NAME_WIDTH, SCREENS_PLAYER_TAG_WIDTH, SCREENS_PLAYER_TEAM_WIDTH, SCREENS_PLAYER_PING_WIDTH };
	static const float lobby[SCREENS_LOBBY_CELLS] = { SCREENS_LOBBY_NAME_WIDTH, SCREENS_LOBBY_TAG_WIDTH };
	static const float friend_limit[SCREENS_FRIEND_CELLS] = { 24, SCREENS_FRIEND_NAME_WIDTH, SCREENS_FRIEND_STATUS_WIDTH };
	static const float party[SCREENS_MEMBER_CELLS] = { 24, SCREENS_FRIEND_NAME_WIDTH, SCREENS_MEMBER_STATE_WIDTH };

	return kind == 0 ? player[index] : kind == 1 ? lobby[index] : kind == 2 ? friend_limit[index] : party[index];
}

static int cell_font(int kind, int index)
{
	/* the name, mark cells are large; every other is small */
	return (kind == 0 && index == 0) || (kind == 1 && index == 0) || (kind == 2 && index < 2) || (kind == 3 && index < 2) ? SCREENS_FONT_LARGE : SCREENS_FONT_SMALL;
}

static void cells_fit_check(struct screens_cell const *cells, int kind, int count, char const *what)
{
	int index;

	for (index = 0; index < count; index++)
	{
		CHECK(ascii_clean(cells[index].text), "%s: cell %d is not clean: '%s'", what, index, cells[index].text);
		CHECK(screens_text_width(cell_font(kind, index), cells[index].text) <= cell_limit(kind, index), "%s: cell %d '%s' is %.1f wide, its column %.0f", what, index, cells[index].text,
			screens_text_width(cell_font(kind, index), cells[index].text), cell_limit(kind, index));
		CHECK(cells[index].color < SCREENS_COLORS, "%s: cell %d has a colour", what, index);
	}
}

static void fit_columns(void)
{
	static char const *const maps[] = { "", "ctf", "bloodgulch", "Prisoner_Of_Hyrule_Remastered", "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW", "mmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmm" };
	struct screens_cell cells[8];
	struct screens_roster_row row;
	struct screens_friend_row friend_row;
	struct screens_party_member member;
	int presence, join, map, flags, skill, team, state;

	for (flags = 0; flags < 256; flags++)
	{
		for (skill = -1; skill < SCREENS_SKILLS; skill++)
		{
			for (team = -1; team <= 1; team++)
			{
				memset(&row, 0, sizeof(row));
				snprintf(row.name, sizeof(row.name), "mmmmmmmmmmm");
				row.flags = (unsigned short)(flags | (skill >= 0 ? SCREENS_ROW_BOT : 0));
				row.skill = (signed char)skill;
				row.team = (signed char)team;
				row.ping = (short)(flags % 3 ? 12345 : -1);
				row.score = (short)(flags % 5 ? 32000 : -1);
				screens_player_cells(&row, NULL, cells);
				cells_fit_check(cells, 0, SCREENS_PLAYER_CELLS, "players");
				screens_lobby_cells(&row, team >= 0, cells);
				cells_fit_check(cells, 1, SCREENS_LOBBY_CELLS, "lobby");
			}
		}
	}
	for (presence = 0; presence < 8; presence++)
	{
		for (join = 0; join < 9; join++)
		{
			for (map = 0; map < (int)(sizeof(maps) / sizeof(maps[0])); map++)
			{
				int party, tab;

				for (party = 0; party < 2; party++)
				{
					for (tab = 0; tab < SCREENS_TABS; tab += 2)
					{
						memset(&friend_row, 0, sizeof(friend_row));
						snprintf(friend_row.name, sizeof(friend_row.name), "mmmmmmmmmmm");
						friend_row.presence = (unsigned char)presence;
						friend_row.join = (unsigned char)join;
						friend_row.party = (unsigned char)party;
						friend_row.players = 128;
						friend_row.maximum = 128;
						snprintf(friend_row.map, sizeof(friend_row.map), "%s", maps[map]);
						screens_friend_cells(tab, &friend_row, cells);
						cells_fit_check(cells, 2, SCREENS_FRIEND_CELLS, "friends");
						/* a join word and the player count are kept whole, whatever the map */
						if ((presence == SCREENS_PRESENCE_PLAYING || presence == SCREENS_PRESENCE_LOBBY) && screens_join_word(join)[0])
						{
							CHECK(strstr(cells[SCREENS_FRIEND_STATUS].text, screens_join_word(join)), "the join word '%s' cut from '%s'", screens_join_word(join),
								cells[SCREENS_FRIEND_STATUS].text);
							CHECK(strstr(cells[SCREENS_FRIEND_STATUS].text, "128/128"), "the count cut from '%s'", cells[SCREENS_FRIEND_STATUS].text);
						}
					}
				}
			}
		}
	}
	for (state = 0; state < 11; state++)
	{
		memset(&member, 0, sizeof(member));
		snprintf(member.name, sizeof(member.name), "mmmmmmmmmmm");
		member.state = (unsigned char)state;
		member.amount = 9999;
		member.leader = (unsigned char)(state & 1);
		screens_member_cells(&member, cells);
		cells_fit_check(cells, 3, SCREENS_MEMBER_CELLS, "party");
	}
}

static void fit_cut(void)
{
	char out[SCREENS_LINE_SIZE];
	float exact = screens_text_width(SCREENS_FONT_SMALL, "HELLO WORLD");
	int length;

	length = screens_fit(SCREENS_FONT_SMALL, exact, "HELLO WORLD", out, (int)sizeof(out));
	CHECK(!strcmp(out, "HELLO WORLD") && length == 11, "a text that fits exactly is not cut: '%s'", out);
	length = screens_fit(SCREENS_FONT_SMALL, exact - 0.1f, "HELLO WORLD", out, (int)sizeof(out));
	CHECK(length >= 3 && !strcmp(out + length - 3, "..."), "a text one tenth too wide is cut with dots: '%s'", out);
	CHECK(screens_text_width(SCREENS_FONT_SMALL, out) <= exact - 0.1f, "the cut text fits: %.1f", screens_text_width(SCREENS_FONT_SMALL, out));
	length = screens_fit(SCREENS_FONT_SMALL, 5.0f, "HELLO", out, (int)sizeof(out));
	CHECK(!strcmp(out, "..."), "a column too narrow for the dots has only the dots: '%s'", out);
	length = screens_fit(SCREENS_FONT_LARGE, 1000.0f, "HELLO", out, 3);
	CHECK(length == 2 && out[2] == 0, "a small buffer is respected: '%s' %d", out, length);
	length = screens_fit(SCREENS_FONT_LARGE, 10.0f, "HELLO WORLD", out, 4);
	CHECK(strlen(out) <= 3, "a small buffer with a cut is respected: '%s'", out);
	length = screens_fit(SCREENS_FONT_LARGE, 100.0f, NULL, out, (int)sizeof(out));
	CHECK(length == 0 && !out[0], "no text");
	CHECK(screens_text_width(SCREENS_FONT_LARGE, "A") > screens_text_width(SCREENS_FONT_SMALL, "A"), "the large font is wider than the small");
	/* (a space before the dots is dropped) */
	length = screens_fit(SCREENS_FONT_SMALL, screens_text_width(SCREENS_FONT_SMALL, "AB ...") + 0.01f, "AB CDEFGHIJ", out, (int)sizeof(out));
	CHECK(!strcmp(out, "AB..."), "no space before the dots: '%s'", out);
}

static void player_cells(void)
{
	struct screens_roster_row rows[SCREENS_ROSTER_MAXIMUM];
	struct screens_roster_summary summary;
	struct screens_cell cells[8];
	int count;

	screens_mock_reset();
	count = screens_mock_provider()->roster(rows, SCREENS_ROSTER_MAXIMUM, &summary);
	CHECK(count == 12 && summary.players == 12 && summary.bots == 6 && summary.maximum == 16, "the mock roster: %d, %d bots", count, summary.bots);
	screens_player_cells(&rows[0], &summary, cells);
	CHECK(!strcmp(cells[SCREENS_PLAYER_NAME].text, "BOB") && !strcmp(cells[SCREENS_PLAYER_TAG].text, "YOU - HOST") && cells[SCREENS_PLAYER_TAG].color == SCREENS_COLOR_WHITE,
		"this machine's own row: %s / %s", cells[0].text, cells[1].text);
	CHECK(!strcmp(cells[SCREENS_PLAYER_TEAM].text, "RED") && cells[SCREENS_PLAYER_TEAM].color == SCREENS_COLOR_RED_TEAM, "red team in the team's colour");
	CHECK(!strcmp(cells[SCREENS_PLAYER_PING].text, "--") && cells[SCREENS_PLAYER_PING].color == SCREENS_COLOR_GREY, "this machine's players have no ping");
	screens_player_cells(&rows[2], &summary, cells);
	CHECK(!strcmp(cells[SCREENS_PLAYER_TAG].text, "BOT HEROIC") && cells[SCREENS_PLAYER_TAG].color == SCREENS_COLOR_AMBER, "a bot is tagged BOT with its skill, in amber: '%s'",
		cells[SCREENS_PLAYER_TAG].text);
	CHECK(!strcmp(cells[SCREENS_PLAYER_TEAM].text, "BLUE") && cells[SCREENS_PLAYER_TEAM].color == SCREENS_COLOR_BLUE_TEAM, "blue team");
	CHECK(!strcmp(cells[SCREENS_PLAYER_PING].text, "--"), "a bot has no ping");
	screens_player_cells(&rows[1], &summary, cells);
	CHECK(!strcmp(cells[SCREENS_PLAYER_TAG].text, "PARTY") && cells[SCREENS_PLAYER_TAG].color == SCREENS_COLOR_AZURE, "a party member: '%s'", cells[SCREENS_PLAYER_TAG].text);
	CHECK(!strcmp(cells[SCREENS_PLAYER_PING].text, "32 MS") && cells[SCREENS_PLAYER_PING].color == SCREENS_COLOR_GREEN, "a good ping in green");
	screens_player_cells(&rows[4], &summary, cells);
	CHECK(!strcmp(cells[SCREENS_PLAYER_TAG].text, "FRIEND") && cells[SCREENS_PLAYER_TAG].color == SCREENS_COLOR_GREEN, "a friend");
	CHECK(cells[SCREENS_PLAYER_PING].color == SCREENS_COLOR_GREEN, "61 ms is green");
	screens_player_cells(&rows[5], &summary, cells);
	CHECK(cells[SCREENS_PLAYER_PING].color == SCREENS_COLOR_AMBER && !cells[SCREENS_PLAYER_TAG].text[0], "118 ms is amber, no tag: '%s'", cells[SCREENS_PLAYER_TAG].text);
	screens_player_cells(&rows[6], &summary, cells);
	CHECK(cells[SCREENS_PLAYER_PING].color == SCREENS_COLOR_ALERT && cells[SCREENS_PLAYER_NAME].color == SCREENS_COLOR_GREY, "204 ms is red; a muted player's name is grey");
	/* the lobby's cell: the team word and a bot's tag */
	screens_lobby_cells(&rows[2], 1, cells);
	CHECK(!strcmp(cells[SCREENS_LOBBY_TAG].text, "BLUE  BOT HEROIC") && cells[SCREENS_LOBBY_TAG].color == SCREENS_COLOR_AMBER, "lobby: team and bot: '%s'", cells[SCREENS_LOBBY_TAG].text);
	screens_lobby_cells(&rows[1], 1, cells);
	CHECK(!strcmp(cells[SCREENS_LOBBY_TAG].text, "RED") && cells[SCREENS_LOBBY_TAG].color == SCREENS_COLOR_RED_TEAM, "lobby: a person in a team game: '%s'", cells[SCREENS_LOBBY_TAG].text);
	screens_lobby_cells(&rows[1], 0, cells);
	CHECK(!strcmp(cells[SCREENS_LOBBY_TAG].text, "PARTY"), "lobby: no team game, the party tag: '%s'", cells[SCREENS_LOBBY_TAG].text);
	screens_lobby_cells(&rows[2], 0, cells);
	CHECK(!strcmp(cells[SCREENS_LOBBY_TAG].text, "BOT HEROIC"), "lobby: a bot in a game without teams: '%s'", cells[SCREENS_LOBBY_TAG].text);
	/* the details and heads */
	{
		char text[SCREENS_LINE_SIZE];

		screens_player_detail(&rows[2], &summary, text, (int)sizeof(text));
		CHECK(!strcmp(text, "JOHNSON  BOT HEROIC  BLUE  8 KILLS"), "detail: '%s'", text);
		screens_player_head(&summary, text, (int)sizeof(text));
		CHECK(!strcmp(text, "12 OF 16   BOTS 6"), "head: '%s'", text);
		summary.maximum = 0;
		summary.bots = 0;
		screens_player_head(&summary, text, (int)sizeof(text));
		CHECK(!strcmp(text, "12 PLAYERS"), "head without a capacity: '%s'", text);
	}
}

static void friend_cells(void)
{
	struct screens_friend_row rows[SCREENS_FRIENDS_MAXIMUM];
	struct screens_friends_summary summary;
	struct screens_cell cells[8];
	int count, index;
	char text[SCREENS_LINE_SIZE];

	screens_mock_reset();
	count = screens_mock_provider()->friends(SCREENS_TAB_FRIENDS, rows, SCREENS_FRIENDS_MAXIMUM, &summary);
	CHECK(count == 12 && summary.service == SCREENS_SERVICE_CONNECTED && !strcmp(summary.own_code, "7F3K-9QXA-2M4D-R"), "mock friends: %d", count);
	screens_friend_cells(SCREENS_TAB_FRIENDS, &rows[0], cells);
	CHECK(!strcmp(cells[SCREENS_FRIEND_STATUS].text, "PLAYING bloodgulch 6/16"), "'%s'", cells[SCREENS_FRIEND_STATUS].text);
	CHECK(cells[SCREENS_FRIEND_MARK].text[0] == '+' && cells[SCREENS_FRIEND_STATUS].color == SCREENS_COLOR_AZURE, "a party member's mark in the accent: '%s'", cells[0].text);
	screens_friend_cells(SCREENS_TAB_FRIENDS, &rows[1], cells);
	CHECK(!strcmp(cells[SCREENS_FRIEND_STATUS].text, "IN LOBBY Infinity 3/8") && cells[SCREENS_FRIEND_STATUS].color == SCREENS_COLOR_GREEN && cells[0].text[0] == '*', "'%s'",
		cells[SCREENS_FRIEND_STATUS].text);
	screens_friend_cells(SCREENS_TAB_FRIENDS, &rows[2], cells);
	CHECK(!strcmp(cells[SCREENS_FRIEND_STATUS].text, "PARTY - ONLINE - IN THE MENUS"), "'%s'", cells[SCREENS_FRIEND_STATUS].text);
	screens_friend_cells(SCREENS_TAB_FRIENDS, &rows[3], cells);
	CHECK(!strcmp(cells[SCREENS_FRIEND_STATUS].text, "AWAY") && cells[SCREENS_FRIEND_STATUS].color == SCREENS_COLOR_AMBER && cells[0].text[0] == 'o', "away");
	screens_friend_cells(SCREENS_TAB_FRIENDS, &rows[4], cells);
	CHECK(!strcmp(cells[SCREENS_FRIEND_STATUS].text, "OFFLINE") && cells[SCREENS_FRIEND_STATUS].color == SCREENS_COLOR_GREY && cells[SCREENS_FRIEND_NAME].color == SCREENS_COLOR_GREY,
		"offline is grey");
	screens_friend_cells(SCREENS_TAB_FRIENDS, &rows[5], cells);
	CHECK(!strcmp(cells[SCREENS_FRIEND_STATUS].text, "PLAYING chillout 16/16 FULL") && cells[SCREENS_FRIEND_STATUS].color == SCREENS_COLOR_AMBER, "a full game in amber: '%s'",
		cells[SCREENS_FRIEND_STATUS].text);
	screens_friend_cells(SCREENS_TAB_FRIENDS, &rows[6], cells);
	CHECK(strstr(cells[SCREENS_FRIEND_STATUS].text, "NEEDS MAP") && strstr(cells[SCREENS_FRIEND_STATUS].text, "4/12") && strstr(cells[SCREENS_FRIEND_STATUS].text, "..."),
		"a long map name is cut, the count and the reason kept: '%s'", cells[SCREENS_FRIEND_STATUS].text);
	screens_friend_cells(SCREENS_TAB_FRIENDS, &rows[8], cells);
	CHECK(strstr(cells[SCREENS_FRIEND_STATUS].text, "NEWER BUILD"), "'%s'", cells[SCREENS_FRIEND_STATUS].text);
	screens_friend_cells(SCREENS_TAB_FRIENDS, &rows[9], cells);
	CHECK(strstr(cells[SCREENS_FRIEND_STATUS].text, "DOWNLOAD") && cells[SCREENS_FRIEND_STATUS].color == SCREENS_COLOR_GREEN, "the DOWNLOAD marker, joinable: '%s'",
		cells[SCREENS_FRIEND_STATUS].text);
	/* details */
	screens_friend_detail(SCREENS_TAB_FRIENDS, &rows[0], &summary, text, (int)sizeof(text));
	CHECK(!strcmp(text, "A: JOIN ALICE'S GAME, 6 OF 16."), "'%s'", text);
	screens_friend_detail(SCREENS_TAB_FRIENDS, &rows[5], &summary, text, (int)sizeof(text));
	CHECK(!strcmp(text, "GRETA'S GAME IS FULL, 16 OF 16 PLAYERS."), "'%s'", text);
	screens_friend_detail(SCREENS_TAB_FRIENDS, &rows[4], &summary, text, (int)sizeof(text));
	CHECK(!strcmp(text, "FRAN IS OFFLINE."), "'%s'", text);
	screens_friend_detail(SCREENS_TAB_FRIENDS, &rows[2], &summary, text, (int)sizeof(text));
	CHECK(!strcmp(text, "A: INVITE DAN."), "'%s'", text);
	for (index = 0; index < 8; index++)
	{
		struct screens_friend_row row = rows[0];
		int join;

		row.presence = (unsigned char)index;
		for (join = 0; join < 9; join++)
		{
			row.join = (unsigned char)join;
			screens_friend_detail(SCREENS_TAB_FRIENDS, &row, &summary, text, (int)sizeof(text));
			CHECK(text[0] && ascii_clean(text) && screens_text_width(SCREENS_FONT_LARGE, text) <= 520.0f, "detail %d %d: '%s'", index, join, text);
		}
	}
	/* the requests */
	count = screens_mock_provider()->friends(SCREENS_TAB_REQUESTS, rows, SCREENS_FRIENDS_MAXIMUM, NULL);
	CHECK(count == 3, "requests: %d", count);
	screens_friend_cells(SCREENS_TAB_REQUESTS, &rows[0], cells);
	CHECK(!strcmp(cells[SCREENS_FRIEND_STATUS].text, "WANTS TO BE FRIENDS") && cells[SCREENS_FRIEND_MARK].text[0] == '?', "'%s'", cells[SCREENS_FRIEND_STATUS].text);
	screens_friend_cells(SCREENS_TAB_REQUESTS, &rows[2], cells);
	CHECK(!strcmp(cells[SCREENS_FRIEND_STATUS].text, "REQUEST SENT") && cells[SCREENS_FRIEND_STATUS].color == SCREENS_COLOR_GREY, "'%s'", cells[SCREENS_FRIEND_STATUS].text);
	count = screens_mock_provider()->friends(SCREENS_TAB_BLOCKED, rows, SCREENS_FRIENDS_MAXIMUM, NULL);
	screens_friend_cells(SCREENS_TAB_BLOCKED, &rows[0], cells);
	CHECK(count == 2 && !strcmp(cells[SCREENS_FRIEND_STATUS].text, "BLOCKED") && cells[SCREENS_FRIEND_STATUS].color == SCREENS_COLOR_ALERT && cells[0].text[0] == 'x', "blocked");
}

static void party_cells(void)
{
	struct screens_party_member members[SCREENS_PARTY_MAXIMUM];
	struct screens_party_summary summary;
	struct screens_cell cells[8];
	char text[SCREENS_LINE_SIZE];
	int count;

	screens_mock_reset();
	count = screens_mock_provider()->party(members, SCREENS_PARTY_MAXIMUM, &summary);
	CHECK(count == 5 && summary.present && summary.size == 5 && summary.maximum == 8 && summary.follow, "party: %d", count);
	screens_member_cells(&members[0], cells);
	CHECK(!strcmp(cells[SCREENS_MEMBER_MARK].text, "L") && !strcmp(cells[SCREENS_MEMBER_STATE].text, "READY") && cells[SCREENS_MEMBER_STATE].color == SCREENS_COLOR_GREEN, "leader");
	screens_member_cells(&members[1], cells);
	CHECK(!strcmp(cells[SCREENS_MEMBER_STATE].text, "JOINING 40%") && cells[SCREENS_MEMBER_STATE].color == SCREENS_COLOR_AMBER, "'%s'", cells[SCREENS_MEMBER_STATE].text);
	screens_member_cells(&members[2], cells);
	CHECK(!strcmp(cells[SCREENS_MEMBER_STATE].text, "DOWNLOADING 12%"), "'%s'", cells[SCREENS_MEMBER_STATE].text);
	screens_member_cells(&members[3], cells);
	CHECK(!strcmp(cells[SCREENS_MEMBER_STATE].text, "NEEDS MAP") && cells[SCREENS_MEMBER_STATE].color == SCREENS_COLOR_ALERT, "a failed member stays with its reason");
	screens_member_cells(&members[4], cells);
	CHECK(!strcmp(cells[SCREENS_MEMBER_STATE].text, "FULL - RETRYING 14s"), "'%s'", cells[SCREENS_MEMBER_STATE].text);
	screens_party_head(&summary, text, (int)sizeof(text));
	CHECK(!strcmp(text, "PARTY 5 OF 8 - FOLLOW LEADER ON"), "'%s'", text);
	memset(&summary, 0, sizeof(summary));
	screens_party_head(&summary, text, (int)sizeof(text));
	CHECK(!strcmp(text, "NO PARTY"), "'%s'", text);
}

static void tabs(void)
{
	struct screens_friends_summary summary;
	struct screens_party_summary party;
	char text[SCREENS_LINE_SIZE];

	memset(&summary, 0, sizeof(summary));
	memset(&party, 0, sizeof(party));
	summary.counts[0] = 4;
	summary.counts[1] = 1;
	summary.counts[3] = 0;
	party.present = 1;
	party.size = 3;
	party.maximum = 8;
	screens_tab_strip(SCREENS_TAB_FRIENDS, &summary, &party, text, (int)sizeof(text));
	CHECK(!strcmp(text, "[FRIENDS 4]  REQUESTS 1  PARTY 3/8  BLOCKED 0"), "'%s'", text);
	screens_tab_strip(SCREENS_TAB_BLOCKED, &summary, &party, text, (int)sizeof(text));
	CHECK(!strcmp(text, "FRIENDS 4  REQUESTS 1  PARTY 3/8  [BLOCKED 0]"), "'%s'", text);
	party.present = 0;
	summary.counts[2] = 0;
	screens_tab_label(SCREENS_TAB_PARTY, 0, &summary, &party, text, (int)sizeof(text));
	CHECK(!strcmp(text, "PARTY 0"), "'%s'", text);
	/* a tab's label fits its widget in the small font, at the biggest counts */
	summary.counts[1] = 50;
	party.present = 1;
	party.size = 8;
	screens_tab_label(SCREENS_TAB_REQUESTS, 1, &summary, &party, text, (int)sizeof(text));
	CHECK(screens_text_width(SCREENS_FONT_SMALL, text) <= (float)SCREENS_TAB_WIDTH, "the longest tab label fits: '%s' %.1f", text, screens_text_width(SCREENS_FONT_SMALL, text));
}

static void sorting(void)
{
	struct screens_friend_row rows[SCREENS_FRIENDS_MAXIMUM];
	int count, index, rank = 0, last = -1;

	screens_mock_reset();
	count = screens_mock_provider()->friends(SCREENS_TAB_FRIENDS, rows, SCREENS_FRIENDS_MAXIMUM, NULL);
	screens_friends_sort(rows, count);
	/* a joinable game, a lobby, a game that cannot be joined, online, away, offline: in that order, ties by name */
	for (index = 0; index < count; index++)
	{
		struct screens_friend_row *row = &rows[index];

		rank = row->presence == SCREENS_PRESENCE_PLAYING && (row->join == SCREENS_JOIN_OPEN || row->join == SCREENS_JOIN_DOWNLOAD) ? 0 :
			row->presence == SCREENS_PRESENCE_LOBBY ? 1 : row->presence == SCREENS_PRESENCE_PLAYING ? 2 : row->presence == SCREENS_PRESENCE_ONLINE ? 3 :
			row->presence == SCREENS_PRESENCE_AWAY ? 4 : 5;
		CHECK(rank >= last, "friend %d (%s) comes after one of a later kind", index, row->name);
		if (rank == last && index)
			CHECK(strcmp(rows[index - 1].name, row->name) <= 0, "%s before %s", rows[index - 1].name, row->name);
		last = rank;
	}
	CHECK(!strcmp(rows[0].name, "ALICE") && !strcmp(rows[count - 1].name, "FRAN"), "first %s last %s", rows[0].name, rows[count - 1].name);
	/* a stable sort leaves a sorted list as it is, and a full list of 50 is no problem */
	{
		struct screens_friend_row again[SCREENS_FRIENDS_MAXIMUM];

		memcpy(again, rows, sizeof(again));
		screens_friends_sort(again, count);
		CHECK(!memcmp(again, rows, (size_t)count * sizeof(rows[0])), "sorting twice changes nothing");
	}
}

static int actions_of(int *list, int action)
{
	int index;

	for (index = 0; list[index] != SCREENS_ACTION_NONE && index <= SCREENS_ACTIONS_MAXIMUM; index++)
	{
		if (list[index] == action)
			return 1;
	}
	return 0;
}

static void actions(void)
{
	struct screens_roster_row rows[SCREENS_ROSTER_MAXIMUM];
	struct screens_roster_summary summary;
	struct screens_provider const *provider = screens_mock_provider();
	int list[SCREENS_ACTIONS_MAXIMUM + 1], index;
	struct screens_friend_row friend_rows[SCREENS_FRIENDS_MAXIMUM];
	struct screens_party_member members[SCREENS_PARTY_MAXIMUM];
	struct screens_party_summary party;

	screens_mock_reset();
	provider->roster(rows, SCREENS_ROSTER_MAXIMUM, &summary);
	/* a bot, for the host */
	provider->roster_actions(&rows[2], &summary, list);
	CHECK(actions_of(list, SCREENS_ACTION_DROP_BOT) && actions_of(list, SCREENS_ACTION_BOT_SKILL_UP) && actions_of(list, SCREENS_ACTION_BOT_SKILL_DOWN) &&
		actions_of(list, SCREENS_ACTION_CHANGE_TEAM) && actions_of(list, SCREENS_ACTION_BOT_SETTINGS) && actions_of(list, SCREENS_ACTION_CANCEL) && !actions_of(list, SCREENS_ACTION_KICK),
		"a bot's actions");
	/* a bot, for a client: nothing but the way out */
	summary.host = 0;
	provider->roster_actions(&rows[2], &summary, list);
	CHECK(list[0] == SCREENS_ACTION_CANCEL && list[1] == SCREENS_ACTION_NONE, "a client has no bot actions");
	summary.host = 1;
	/* a person, for the host and for a client */
	summary.host = 1;
	provider->roster_actions(&rows[5], &summary, list);
	CHECK(actions_of(list, SCREENS_ACTION_MUTE) && actions_of(list, SCREENS_ACTION_VOTE_KICK) && actions_of(list, SCREENS_ACTION_KICK) && actions_of(list, SCREENS_ACTION_BAN) &&
		actions_of(list, SCREENS_ACTION_ADD_FRIEND) && actions_of(list, SCREENS_ACTION_INVITE_PARTY), "a person's actions for the host");
	summary.host = 0;
	provider->roster_actions(&rows[5], &summary, list);
	CHECK(actions_of(list, SCREENS_ACTION_MUTE) && actions_of(list, SCREENS_ACTION_VOTE_KICK) && !actions_of(list, SCREENS_ACTION_KICK) && !actions_of(list, SCREENS_ACTION_BAN),
		"host-only items are absent for a client, not disabled");
	provider->roster_actions(&rows[6], &summary, list);
	CHECK(actions_of(list, SCREENS_ACTION_UNMUTE) && !actions_of(list, SCREENS_ACTION_MUTE), "a muted player offers UNMUTE");
	provider->roster_actions(&rows[1], &summary, list);
	CHECK(!actions_of(list, SCREENS_ACTION_ADD_FRIEND) && !actions_of(list, SCREENS_ACTION_INVITE_PARTY), "a friend in the party offers neither");
	summary.host = 1;
	provider->roster_actions(&rows[0], &summary, list);
	CHECK(actions_of(list, SCREENS_ACTION_CHANGE_TEAM) && actions_of(list, SCREENS_ACTION_BOT_SETTINGS) && !actions_of(list, SCREENS_ACTION_MUTE), "this machine's own row: the team and the bot settings");
	summary.host = 0;
	provider->roster_actions(&rows[0], &summary, list);
	CHECK(actions_of(list, SCREENS_ACTION_CHANGE_TEAM) && !actions_of(list, SCREENS_ACTION_BOT_SETTINGS), "the bot settings are the host's");
	summary.host = 1;
	for (index = 0; index < 12; index++)
	{
		int count = 0;

		summary.host = (unsigned char)(index & 1);
		provider->roster_actions(&rows[index], &summary, list);
		while (list[count] != SCREENS_ACTION_NONE)
			count++;
		CHECK(count >= 1 && count <= SCREENS_ACTIONS_MAXIMUM && list[count - 1] == SCREENS_ACTION_CANCEL, "row %d: %d actions, the last is CANCEL", index, count);
	}
	/* a friend's actions by tab */
	provider->friends(SCREENS_TAB_FRIENDS, friend_rows, SCREENS_FRIENDS_MAXIMUM, NULL);
	provider->friend_actions(SCREENS_TAB_FRIENDS, &friend_rows[0], list);
	CHECK(actions_of(list, SCREENS_ACTION_JOIN_GAME) && actions_of(list, SCREENS_ACTION_REMOVE_FRIEND) && actions_of(list, SCREENS_ACTION_BLOCK) && actions_of(list, SCREENS_ACTION_VIEW_CODE),
		"a friend in an open game");
	provider->friend_actions(SCREENS_TAB_FRIENDS, &friend_rows[5], list);
	CHECK(!actions_of(list, SCREENS_ACTION_JOIN_GAME), "no JOIN for a full game");
	provider->friend_actions(SCREENS_TAB_FRIENDS, &friend_rows[4], list);
	CHECK(!actions_of(list, SCREENS_ACTION_JOIN_GAME) && !actions_of(list, SCREENS_ACTION_INVITE_GAME) && !actions_of(list, SCREENS_ACTION_INVITE_PARTY), "no invitation for an offline friend");
	provider->friends(SCREENS_TAB_REQUESTS, friend_rows, SCREENS_FRIENDS_MAXIMUM, NULL);
	provider->friend_actions(SCREENS_TAB_REQUESTS, &friend_rows[0], list);
	CHECK(actions_of(list, SCREENS_ACTION_ACCEPT) && actions_of(list, SCREENS_ACTION_DECLINE) && actions_of(list, SCREENS_ACTION_BLOCK), "a request got");
	provider->friend_actions(SCREENS_TAB_REQUESTS, &friend_rows[2], list);
	CHECK(!actions_of(list, SCREENS_ACTION_ACCEPT), "a request sent cannot be accepted");
	provider->friends(SCREENS_TAB_BLOCKED, friend_rows, SCREENS_FRIENDS_MAXIMUM, NULL);
	provider->friend_actions(SCREENS_TAB_BLOCKED, &friend_rows[0], list);
	CHECK(actions_of(list, SCREENS_ACTION_UNBLOCK), "a blocked friend");
	/* the party */
	provider->party(members, SCREENS_PARTY_MAXIMUM, &party);
	provider->party_actions(&members[0], &party, list);
	CHECK(actions_of(list, SCREENS_ACTION_FOLLOW_OFF) && actions_of(list, SCREENS_ACTION_DISBAND_PARTY), "this machine, the leader: follow, disband");
	provider->party_actions(&members[2], &party, list);
	CHECK(actions_of(list, SCREENS_ACTION_MAKE_LEADER) && actions_of(list, SCREENS_ACTION_REMOVE_FROM_PARTY), "a member, for the leader");
}

static void model(void)
{
	static struct screens_model model_data;
	struct screens_provider const *provider = screens_mock_provider();
	char answer[SCREENS_LINE_SIZE];
	unsigned long generation;

	/* no provider set and screens.enable at 1: the mock's */
	screens_mock_reset();
	CHECK(screens_model_update(&model_data, 0) == 1 && model_data.valid, "first update");
	CHECK(model_data.roster_count == 12 && model_data.friends_count[SCREENS_TAB_FRIENDS] == 12 && model_data.friends_count[SCREENS_TAB_REQUESTS] == 3 &&
		model_data.friends_count[SCREENS_TAB_BLOCKED] == 2 && model_data.party_count == 5, "counts");
	CHECK(model_data.friends_summary.counts[SCREENS_TAB_FRIENDS] == 12 && model_data.friends_summary.counts[SCREENS_TAB_PARTY] == 5 && model_data.friends_summary.online == 11,
		"summary counts: online %d", model_data.friends_summary.online);
	CHECK(!strcmp(model_data.friends[SCREENS_TAB_FRIENDS][0].name, "ALICE"), "the friends are sorted in the model");
	CHECK(screens_model_update(&model_data, 0) == 0, "nothing changed: no rebuild");
	generation = model_data.generation;
	CHECK(provider->bot_add(SCREENS_TEAM_NONE, -1, answer, (int)sizeof(answer)) == 1, "a bot is added");
	CHECK(screens_model_update(&model_data, 0) == 1 && model_data.generation == generation + 1 && model_data.roster_count == 13, "a change rebuilds the model");
	CHECK(screens_model_update(&model_data, 1) == 1, "force");
	/* a provider with no generation function is rebuilt on every call */
	{
		static struct screens_provider plain;

		plain = *provider;
		plain.generation = NULL;
		screens_provider_set(&plain);
		CHECK(screens_model_update(&model_data, 0) == 1 && screens_model_update(&model_data, 0) == 1, "no generation: always rebuilt");
		/* a table with no friend functions: the friends screen is empty, nothing else is */
		plain.friends = NULL;
		plain.party = NULL;
		screens_model_update(&model_data, 1);
		CHECK(model_data.roster_count == 13 && model_data.friends_count[SCREENS_TAB_FRIENDS] == 0 && model_data.party_count == 0, "a backend with only a roster");
		screens_provider_set(NULL);
	}
}

static void mock_actions(void)
{
	struct screens_provider const *provider = screens_mock_provider();
	struct screens_roster_row rows[SCREENS_ROSTER_MAXIMUM];
	struct screens_roster_summary summary;
	struct screens_friend_row friend_rows[SCREENS_FRIENDS_MAXIMUM];
	char answer[SCREENS_LINE_SIZE];
	int count;

	screens_mock_reset();
	count = provider->roster(rows, SCREENS_ROSTER_MAXIMUM, &summary);
	CHECK(provider->roster_action(SCREENS_ACTION_MUTE, &rows[1], answer, (int)sizeof(answer)), "mute");
	provider->roster(rows, SCREENS_ROSTER_MAXIMUM, &summary);
	CHECK(rows[1].flags & SCREENS_ROW_MUTED, "muted");
	CHECK(provider->roster_action(SCREENS_ACTION_UNMUTE, &rows[1], answer, (int)sizeof(answer)), "unmute");
	provider->roster(rows, SCREENS_ROSTER_MAXIMUM, &summary);
	CHECK(!(rows[1].flags & SCREENS_ROW_MUTED), "unmuted");
	CHECK(provider->roster_action(SCREENS_ACTION_DROP_BOT, &rows[2], answer, (int)sizeof(answer)), "drop a bot");
	CHECK(provider->roster(rows, SCREENS_ROSTER_MAXIMUM, &summary) == count - 1 && summary.bots == 5, "a bot dropped");
	/* a row that left: the action says so in words */
	{
		struct screens_roster_row gone = rows[0];

		gone.machine = 99999;
		CHECK(!provider->roster_action(SCREENS_ACTION_KICK, &gone, answer, (int)sizeof(answer)) && !strcmp(answer, "THAT PLAYER LEFT."), "'%s'", answer);
	}
	/* bots are added to the smaller team, up to the game's capacity, and refused in words after */
	while (provider->bot_add(SCREENS_TEAM_NONE, 4, answer, (int)sizeof(answer)))
		;
	CHECK(!strcmp(answer, "THE GAME IS FULL."), "'%s'", answer);
	count = provider->roster(rows, SCREENS_ROSTER_MAXIMUM, &summary);
	CHECK(count == 16, "a full game: %d", count);
	{
		int red = 0, blue = 0, index;

		for (index = 0; index < count; index++)
		{
			red += rows[index].team == SCREENS_TEAM_RED;
			blue += rows[index].team == SCREENS_TEAM_BLUE;
		}
		CHECK(red - blue >= -1 && red - blue <= 1, "the teams are even: %d and %d", red, blue);
	}
	CHECK(provider->bot_drop(-1, answer, (int)sizeof(answer)) && provider->roster(rows, SCREENS_ROSTER_MAXIMUM, &summary) == 15, "the weakest bot (the last) is dropped");
	screens_mock_reset();
	provider->roster(rows, SCREENS_ROSTER_MAXIMUM, &summary);
	CHECK(provider->roster_action(SCREENS_ACTION_BOT_SKILL_UP, &rows[2], answer, (int)sizeof(answer)), "skill up");
	provider->roster(rows, SCREENS_ROSTER_MAXIMUM, &summary);
	CHECK(rows[2].skill == 4, "heroic to veteran: %d", rows[2].skill);
	/* friends */
	provider->friends(SCREENS_TAB_REQUESTS, friend_rows, SCREENS_FRIENDS_MAXIMUM, NULL);
	CHECK(provider->friend_action(SCREENS_ACTION_ACCEPT, &friend_rows[0], answer, (int)sizeof(answer)), "accept");
	CHECK(provider->friends(SCREENS_TAB_FRIENDS, friend_rows, SCREENS_FRIENDS_MAXIMUM, NULL) == 13 && provider->friends(SCREENS_TAB_REQUESTS, friend_rows, SCREENS_FRIENDS_MAXIMUM, NULL) == 2,
		"the request moved to the friends");
	provider->friends(SCREENS_TAB_FRIENDS, friend_rows, SCREENS_FRIENDS_MAXIMUM, NULL);
	CHECK(provider->friend_action(SCREENS_ACTION_BLOCK, &friend_rows[0], answer, (int)sizeof(answer)), "block");
	CHECK(provider->friends(SCREENS_TAB_BLOCKED, friend_rows, SCREENS_FRIENDS_MAXIMUM, NULL) == 3, "blocked");
	CHECK(provider->friend_action(SCREENS_ACTION_UNBLOCK, &friend_rows[2], answer, (int)sizeof(answer)), "unblock");
	CHECK(provider->friends(SCREENS_TAB_BLOCKED, friend_rows, SCREENS_FRIENDS_MAXIMUM, NULL) == 2, "unblocked");
	/* adding by code: the checks, in words */
	CHECK(!provider->friend_add("NOT A CODE!", answer, (int)sizeof(answer)) && !strcmp(answer, "NOT A CODE."), "'%s'", answer);
	CHECK(!provider->friend_add("7F3K", answer, (int)sizeof(answer)) && strstr(answer, "13 CHARACTERS"), "'%s'", answer);
	CHECK(!provider->friend_add("7f3k 9qxa 2m4d r", answer, (int)sizeof(answer)) && !strcmp(answer, "THAT IS YOUR OWN CODE."), "'%s'", answer);
	CHECK(!provider->friend_add("9FQ2-KD4A-7HNE-3", answer, (int)sizeof(answer)) && !strcmp(answer, "ALREADY A FRIEND."), "'%s'", answer);
	CHECK(provider->friend_add("Z3M9-7PQK-2TXD-W", answer, (int)sizeof(answer)) && strstr(answer, "REQUEST SENT"), "'%s'", answer);
	CHECK(provider->friends(SCREENS_TAB_REQUESTS, friend_rows, SCREENS_FRIENDS_MAXIMUM, NULL) == 3 && !strcmp(friend_rows[2].name, "NEW FRIEND"), "the request waits in the list");
	/* the party */
	{
		struct screens_party_member members[SCREENS_PARTY_MAXIMUM];
		struct screens_party_summary party;

		provider->party(members, SCREENS_PARTY_MAXIMUM, &party);
		CHECK(provider->party_action(SCREENS_ACTION_FOLLOW_OFF, &members[0], answer, (int)sizeof(answer)), "follow off");
		provider->party(members, SCREENS_PARTY_MAXIMUM, &party);
		CHECK(!party.follow, "not following");
		CHECK(provider->party_action(SCREENS_ACTION_REMOVE_FROM_PARTY, &members[3], answer, (int)sizeof(answer)), "remove");
		CHECK(provider->party(members, SCREENS_PARTY_MAXIMUM, &party) == 4, "removed from the party");
		CHECK(provider->party_action(SCREENS_ACTION_LEAVE_PARTY, &members[0], answer, (int)sizeof(answer)), "leave");
		CHECK(provider->party(members, SCREENS_PARTY_MAXIMUM, &party) == 0 && !party.present, "no party");
	}
}

static void mock_text(void)
{
	struct screens_roster_row rows[SCREENS_ROSTER_MAXIMUM];
	struct screens_roster_summary summary;
	struct screens_friend_row friend_rows[SCREENS_FRIENDS_MAXIMUM];
	struct screens_party_member members[SCREENS_PARTY_MAXIMUM];
	struct screens_toast toast;
	int count, index, tab;

	screens_mock_reset();
	count = screens_mock_provider()->roster(rows, SCREENS_ROSTER_MAXIMUM, &summary);
	for (index = 0; index < count; index++)
	{
		CHECK(strlen(rows[index].name) >= 1 && strlen(rows[index].name) <= SCREENS_NAME_LENGTH && ascii_clean(rows[index].name), "roster name %d '%s'", index, rows[index].name);
		CHECK(!(rows[index].flags & SCREENS_ROW_BOT) == (rows[index].skill < 0), "a bot has a skill, a person none");
	}
	for (tab = 0; tab < SCREENS_TABS; tab++)
	{
		count = screens_mock_provider()->friends(tab, friend_rows, SCREENS_FRIENDS_MAXIMUM, NULL);
		for (index = 0; index < count; index++)
		{
			CHECK(strlen(friend_rows[index].name) >= 1 && strlen(friend_rows[index].name) <= SCREENS_NAME_LENGTH && ascii_clean(friend_rows[index].name), "friend name '%s'",
				friend_rows[index].name);
			CHECK(ascii_clean(friend_rows[index].code) && strlen(friend_rows[index].code) == 16, "friend code '%s'", friend_rows[index].code);
		}
	}
	count = screens_mock_provider()->party(members, SCREENS_PARTY_MAXIMUM, NULL);
	for (index = 0; index < count; index++)
		CHECK(ascii_clean(members[index].name), "member name");
	while (screens_mock_provider()->toast_pop(&toast))
	{
		CHECK(ascii_clean(toast.text) && ascii_clean(toast.detail) && toast.text[0], "toast text '%s'", toast.text);
		CHECK(screens_text_width(SCREENS_FONT_SMALL, toast.text) <= 330.0f && screens_text_width(SCREENS_FONT_SMALL, toast.detail) <= 330.0f, "a toast fits its band: '%s' '%s'", toast.text,
			toast.detail);
	}
}

static void view_make_case(void)
{
	static struct screens_model model_data;
	struct screens_view_input input;
	struct halo_social_view view;
	struct screens_toast_state toasts;
	struct screens_toast toast;
	long keys[HALO_SOCIAL_ROWS];
	char const *tiles[4] = { "Enemy here!", "Need backup!", "Cover me!", "Fall back!" };
	int index;
	long id;

	screens_mock_reset();
	screens_model_update(&model_data, 1);
	screens_toast_reset(&toasts);
	memset(&input, 0, sizeof(input));
	input.model = &model_data;
	input.toasts = &toasts;
	input.now = 1000;
	input.has_friends = input.can_friend_action = input.can_bots = 1;
	input.page_name = "CALLS";
	for (index = 0; index < 4; index++)
		input.tiles[index] = tiles[index];
	screens_view_make(&view, &input, keys);
	CHECK(view.active && view.tab == HALO_SOCIAL_TAB_FRIENDS && view.row_count == HALO_SOCIAL_ROWS, "friends: %d rows", view.row_count);
	CHECK(!strcmp(view.head, "bloodgulch  Slayer  12 OF 16   BOTS 6") && !strcmp(view.party, "PARTY 5/8"), "head '%s' party '%s'", view.head, view.party);
	CHECK(!strcmp(view.tabs[0], "FRIENDS 11") && !strcmp(view.tabs[1], "PARTY 5") && !strcmp(view.tabs[2], "BOTS 6"), "tabs");
	CHECK(!strcmp(view.rows[0].name, "ALICE") && view.rows[0].action == HALO_SOCIAL_ACTION_JOIN && !strcmp(view.rows[0].status, "PLAYING 6/16"), "the first friend, joinable, without the map: '%s'", view.rows[0].status);
	for (index = 0; index < view.row_count; index++)
	{
		CHECK(strcmp(view.rows[index].name, "FRAN"), "an offline friend is not in the view");
		CHECK(ascii_clean(view.rows[index].name) && ascii_clean(view.rows[index].status) && keys[index] > 0, "row %d clean", index);
	}
	CHECK(view.rows[4].action == HALO_SOCIAL_ACTION_INVITE && strstr(view.rows[4].status, "FULL"), "a full game: invite, not join: '%s' %d", view.rows[4].status, view.rows[4].action);
	CHECK(!strcmp(view.tiles[1], "Need backup!") && !strcmp(view.page, "CALLS"), "the quick chat page");
	/* a friend who went offline since is not shown */
	model_data.friends[SCREENS_TAB_FRIENDS][0].presence = SCREENS_PRESENCE_OFFLINE;
	screens_view_make(&view, &input, keys);
	CHECK(strcmp(view.rows[0].name, "ALICE"), "an offline friend is skipped: '%s'", view.rows[0].name);
	model_data.friends[SCREENS_TAB_FRIENDS][0].presence = SCREENS_PRESENCE_PLAYING;
	/* no friend action: no buttons */
	input.can_friend_action = 0;
	screens_view_make(&view, &input, keys);
	CHECK(view.rows[0].action == HALO_SOCIAL_ACTION_NONE, "a provider that cannot act: no buttons");
	input.can_friend_action = 1;
	/* the party */
	input.tab = HALO_SOCIAL_TAB_PARTY;
	screens_view_make(&view, &input, keys);
	CHECK(view.row_count == 5 && !strcmp(view.rows[0].mark, "L") && !strcmp(view.rows[1].status, "JOINING 40%") && view.rows[1].action == HALO_SOCIAL_ACTION_NONE, "party rows");
	model_data.party_count = 0;
	screens_view_make(&view, &input, keys);
	CHECK(view.row_count == 0 && strstr(view.empty, "NO PARTY"), "an empty party says so: '%s'", view.empty);
	/* the bots */
	input.tab = HALO_SOCIAL_TAB_BOTS;
	screens_view_make(&view, &input, keys);
	CHECK(!strcmp(view.bots[0], "BOTS: 6") && !strcmp(view.bots[1], "SKILL: HEROIC") && view.bots_host, "bots tab for the host");
	model_data.roster_summary.host = 0;
	screens_view_make(&view, &input, keys);
	CHECK(!view.bots_host, "not for a client");
	input.can_bots = 0;
	model_data.roster_summary.host = 1;
	screens_view_make(&view, &input, keys);
	CHECK(!view.bots_host, "not without a provider that adds bots");
	/* no friend service */
	input.tab = HALO_SOCIAL_TAB_FRIENDS;
	input.has_friends = 0;
	memset(model_data.friends_count, 0, sizeof(model_data.friends_count));
	screens_view_make(&view, &input, keys);
	CHECK(view.row_count == 0 && strstr(view.empty, "NO FRIEND SERVICE"), "'%s'", view.empty);
	/* the toast */
	memset(&toast, 0, sizeof(toast));
	toast.kind = SCREENS_TOAST_INVITE;
	toast.id = 5;
	snprintf(toast.text, sizeof(toast.text), "ALICE INVITES YOU");
	snprintf(toast.detail, sizeof(toast.detail), "bloodgulch");
	screens_toast_push(&toasts, &toast, 1000);
	screens_toast_tick(&toasts, 1000);
	input.now = 2000;
	screens_view_make(&view, &input, keys);
	CHECK(view.toast_alpha == 255 && view.toast_invite && !strcmp(view.toast, "ALICE INVITES YOU") && !strcmp(view.toast_detail, "bloodgulch"), "the toast");
	screens_toast_answer(&toasts, 1, 1, 2000, &id);
	screens_view_make(&view, &input, keys);
	CHECK(strstr(view.toast_detail, "LEAVE THIS GAME"), "asking again from a game: '%s'", view.toast_detail);
	screens_toast_answer(&toasts, 0, 1, 2100, &id);
	screens_view_make(&view, &input, keys);
	CHECK(view.toast_alpha == 0 && !view.toast[0], "no toast once answered");
}

static void empty_text(void)
{
	char text[SCREENS_LINE_SIZE];
	struct screens_friends_summary summary;
	int tab, service, provider;

	memset(&summary, 0, sizeof(summary));
	for (service = 0; service < 4; service++)
	{
		for (provider = 0; provider < 2; provider++)
		{
			for (tab = 0; tab < SCREENS_TABS; tab++)
			{
				summary.service = (unsigned char)service;
				screens_empty_text(1, tab, &summary, provider, text, (int)sizeof(text));
				CHECK(text[0] && ascii_clean(text) && screens_text_width(SCREENS_FONT_LARGE, text) <= 520.0f, "empty %d %d %d: '%s'", service, provider, tab, text);
			}
		}
	}
	summary.service = SCREENS_SERVICE_CONNECTED;
	screens_empty_text(1, SCREENS_TAB_FRIENDS, &summary, 1, text, (int)sizeof(text));
	CHECK(strstr(text, "NO FRIENDS YET") && strstr(text, "Y: ADD"), "'%s'", text);
	screens_empty_text(1, SCREENS_TAB_FRIENDS, &summary, 0, text, (int)sizeof(text));
	CHECK(strstr(text, "NO FRIEND SERVICE"), "a build with no friends service says so: '%s'", text);
	summary.service = SCREENS_SERVICE_OFF;
	screens_empty_text(1, SCREENS_TAB_FRIENDS, &summary, 1, text, (int)sizeof(text));
	CHECK(strstr(text, "OFFLINE") && strstr(text, "KEPT"), "an offline service: '%s'", text);
	summary.service = SCREENS_SERVICE_DISABLED;
	screens_empty_text(1, SCREENS_TAB_BLOCKED, &summary, 1, text, (int)sizeof(text));
	CHECK(strstr(text, "FRIENDS ARE OFF"), "'%s'", text);
	screens_empty_text(0, 0, NULL, 1, text, (int)sizeof(text));
	CHECK(!strcmp(text, "NO PLAYERS YET."), "'%s'", text);
	screens_empty_text(0, 0, NULL, 0, text, (int)sizeof(text));
	CHECK(strstr(text, "NO PLAYER LIST"), "'%s'", text);
}

/* nav: the Players page: ten rows, no tabs, four buttons */
static void nav_players(void)
{
	struct screens_nav nav;
	int index;

	screens_nav_begin(&nav, 0, SCREENS_PLAYER_ROWS, 0, 4);
	screens_nav_rows(&nav, 12);
	CHECK(nav.area == SCREENS_AREA_ROWS && nav.row == 0 && nav.first == 0, "opens on the first row");
	screens_nav_key(&nav, SCREENS_KEY_DOWN);
	CHECK(nav.row == 1, "down: the second row");
	screens_nav_key(&nav, SCREENS_KEY_UP);
	CHECK(nav.row == 0, "up: the first");
	screens_nav_key(&nav, SCREENS_KEY_UP);
	CHECK(nav.area == SCREENS_AREA_BUTTONS && nav.button == 0, "up from the first row: the buttons (no tabs)");
	screens_nav_key(&nav, SCREENS_KEY_LEFT);
	CHECK(nav.button == 3, "left wraps to the last button");
	screens_nav_key(&nav, SCREENS_KEY_RIGHT);
	CHECK(nav.button == 0, "right wraps back");
	screens_nav_key(&nav, SCREENS_KEY_RIGHT);
	screens_nav_key(&nav, SCREENS_KEY_A);
	CHECK(nav.result == SCREENS_NAV_BUTTON && nav.button == 1, "A on a button");
	screens_nav_key(&nav, SCREENS_KEY_UP);
	CHECK(nav.area == SCREENS_AREA_ROWS && nav.row == 9, "up from the buttons: the last row shown");
	/* down through the rows scrolls the two that are past the ten shown */
	screens_nav_key(&nav, SCREENS_KEY_DOWN);
	CHECK(nav.first == 1 && nav.row == 9, "down at the last row shown scrolls");
	screens_nav_key(&nav, SCREENS_KEY_DOWN);
	CHECK(nav.first == 2, "scrolled to the end");
	screens_nav_key(&nav, SCREENS_KEY_DOWN);
	CHECK(nav.area == SCREENS_AREA_BUTTONS, "down at the end: the buttons");
	screens_nav_key(&nav, SCREENS_KEY_DOWN);
	CHECK(nav.area == SCREENS_AREA_ROWS && nav.row == 0, "down from the buttons wraps to the first row (no tabs)");
	screens_nav_key(&nav, SCREENS_KEY_A);
	CHECK(nav.result == SCREENS_NAV_OPEN_ACTIONS, "A on a row opens its actions");
	screens_nav_key(&nav, SCREENS_KEY_B);
	CHECK(nav.result == SCREENS_NAV_BACK, "B goes back");
	screens_nav_key(&nav, SCREENS_KEY_L);
	CHECK(nav.result == SCREENS_NAV_NOTHING, "no shoulders without tabs");
	/* scroll back up from the end */
	nav.area = SCREENS_AREA_ROWS;
	nav.row = 0;
	for (index = 0; index < 2; index++)
		screens_nav_key(&nav, SCREENS_KEY_UP);
	CHECK(nav.first == 0, "up at the first row scrolls back: %d", nav.first);
	/* few rows: no scrolling, the focus stays within them */
	screens_nav_begin(&nav, 0, SCREENS_PLAYER_ROWS, 0, 4);
	screens_nav_rows(&nav, 3);
	screens_nav_key(&nav, SCREENS_KEY_DOWN);
	screens_nav_key(&nav, SCREENS_KEY_DOWN);
	screens_nav_key(&nav, SCREENS_KEY_DOWN);
	CHECK(nav.area == SCREENS_AREA_BUTTONS, "past the third row: the buttons");
	/* no rows: A opens nothing, the focus rests on the buttons */
	screens_nav_begin(&nav, 0, SCREENS_PLAYER_ROWS, 0, 4);
	screens_nav_rows(&nav, 0);
	CHECK(nav.area == SCREENS_AREA_BUTTONS, "no rows: the buttons have the focus");
	screens_nav_key(&nav, SCREENS_KEY_UP);
	CHECK(nav.area == SCREENS_AREA_BUTTONS, "and stay");
	/* the rows shrink while a row is focused: the focus follows */
	screens_nav_begin(&nav, 0, SCREENS_PLAYER_ROWS, 0, 4);
	screens_nav_rows(&nav, 8);
	nav.row = 7;
	screens_nav_rows(&nav, 5);
	CHECK(nav.row == 4, "the focus follows the rows that went: %d", nav.row);
	screens_nav_rows(&nav, 0);
	CHECK(nav.area == SCREENS_AREA_BUTTONS, "all rows gone: the buttons");
}

static void nav_friends(void)
{
	struct screens_nav nav;

	screens_nav_begin(&nav, 1, SCREENS_FRIEND_ROWS, SCREENS_TABS, 4);
	screens_nav_rows(&nav, 12);
	CHECK(nav.area == SCREENS_AREA_TABS && nav.tab == 0, "opens on the first tab");
	screens_nav_key(&nav, SCREENS_KEY_RIGHT);
	CHECK(nav.tab == 1 && nav.result == SCREENS_NAV_TAB, "right: the next tab");
	screens_nav_key(&nav, SCREENS_KEY_LEFT);
	screens_nav_key(&nav, SCREENS_KEY_LEFT);
	CHECK(nav.tab == 3, "left wraps");
	screens_nav_key(&nav, SCREENS_KEY_R);
	CHECK(nav.tab == 0 && nav.result == SCREENS_NAV_TAB, "R turns the tab from anywhere");
	screens_nav_key(&nav, SCREENS_KEY_L);
	CHECK(nav.tab == 3, "L turns back");
	screens_nav_key(&nav, SCREENS_KEY_R);
	screens_nav_key(&nav, SCREENS_KEY_DOWN);
	CHECK(nav.area == SCREENS_AREA_ROWS && nav.row == 0, "down from the tabs: the first row");
	screens_nav_key(&nav, SCREENS_KEY_UP);
	CHECK(nav.area == SCREENS_AREA_TABS, "up from the first row: the tabs");
	screens_nav_key(&nav, SCREENS_KEY_UP);
	CHECK(nav.area == SCREENS_AREA_BUTTONS, "up from the tabs: the buttons");
	screens_nav_key(&nav, SCREENS_KEY_DOWN);
	CHECK(nav.area == SCREENS_AREA_TABS, "down from the buttons: the tabs");
	screens_nav_key(&nav, SCREENS_KEY_DOWN);
	nav.row = 8;
	screens_nav_key(&nav, SCREENS_KEY_DOWN);
	CHECK(nav.first == 1, "nine rows shown of twelve: down scrolls");
	screens_nav_key(&nav, SCREENS_KEY_R);
	CHECK(nav.first == 0 && nav.row == 0, "a new tab starts at its top");
	/* an empty tab: down from the tabs goes to the buttons */
	screens_nav_begin(&nav, 1, SCREENS_FRIEND_ROWS, SCREENS_TABS, 4);
	screens_nav_rows(&nav, 0);
	screens_nav_key(&nav, SCREENS_KEY_DOWN);
	CHECK(nav.area == SCREENS_AREA_BUTTONS, "an empty list: down goes to the buttons");
	screens_nav_key(&nav, SCREENS_KEY_UP);
	CHECK(nav.area == SCREENS_AREA_TABS, "and up to the tabs");
	screens_nav_key(&nav, SCREENS_KEY_A);
	CHECK(nav.result == SCREENS_NAV_TAB, "A on a tab chooses it");
	screens_nav_key(&nav, SCREENS_KEY_B);
	CHECK(nav.result == SCREENS_NAV_BACK, "B goes back from anywhere");
}

static void nav_tap(void)
{
	struct screens_nav nav;
	int index;

	screens_nav_begin(&nav, 1, SCREENS_FRIEND_ROWS, SCREENS_TABS, 4);
	screens_nav_rows(&nav, 12);
	/* a tab */
	for (index = 0; index < SCREENS_TABS; index++)
	{
		screens_nav_tap(&nav, (float)(SCREENS_TAB_LEFT + index * SCREENS_TAB_PITCH + 5), (float)(SCREENS_TAB_TOP + 5));
		CHECK(nav.result == SCREENS_NAV_TAB && nav.tab == index, "tab %d", index);
		screens_nav_tap(&nav, (float)(SCREENS_TAB_LEFT + index * SCREENS_TAB_PITCH + SCREENS_TAB_WIDTH - 1), (float)(SCREENS_TAB_TOP + SCREENS_TAB_HEIGHT - 1));
		CHECK(nav.result == SCREENS_NAV_TAB && nav.tab == index, "tab %d's far corner", index);
	}
	screens_nav_tap(&nav, (float)(SCREENS_TAB_LEFT + SCREENS_TAB_WIDTH + 1), (float)(SCREENS_TAB_TOP + 5));
	CHECK(nav.result == SCREENS_NAV_NOTHING, "the gap between two tabs");
	screens_nav_tap(&nav, (float)(SCREENS_TAB_LEFT + 4 * SCREENS_TAB_PITCH + 1), (float)(SCREENS_TAB_TOP + 5));
	CHECK(nav.result == SCREENS_NAV_NOTHING, "past the last tab");
	/* a row */
	screens_nav_tap(&nav, (float)(SCREENS_ROWS_LEFT + 10), (float)(SCREENS_ROWS_TOP + 3 * SCREENS_ROW_PITCH + 4));
	CHECK(nav.result == SCREENS_NAV_OPEN_ACTIONS && nav.row == 3 && nav.area == SCREENS_AREA_ROWS, "row 3");
	screens_nav_tap(&nav, (float)(SCREENS_ROWS_LEFT + SCREENS_ROW_WIDTH - 1), (float)(SCREENS_ROWS_TOP + 8 * SCREENS_ROW_PITCH + SCREENS_ROW_HEIGHT - 1));
	CHECK(nav.result == SCREENS_NAV_OPEN_ACTIONS && nav.row == 8, "the last row, its far corner");
	screens_nav_tap(&nav, (float)(SCREENS_ROWS_LEFT + 10), (float)(SCREENS_ROWS_TOP + 9 * SCREENS_ROW_PITCH + 4));
	CHECK(nav.result == SCREENS_NAV_NOTHING, "below the last row shown");
	screens_nav_tap(&nav, (float)(SCREENS_ROWS_LEFT - 1), (float)(SCREENS_ROWS_TOP + 4));
	CHECK(nav.result == SCREENS_NAV_NOTHING, "left of the rows");
	screens_nav_tap(&nav, (float)(SCREENS_ROWS_LEFT + 10), (float)(SCREENS_ROWS_TOP + SCREENS_ROW_PITCH));
	CHECK(nav.result == SCREENS_NAV_OPEN_ACTIONS && nav.row == 1, "the pixel under a row's last is the next row's first");
	screens_nav_begin(&nav, 1, SCREENS_FRIEND_ROWS, SCREENS_TABS, 4);
	screens_nav_rows(&nav, 2);
	screens_nav_tap(&nav, (float)(SCREENS_ROWS_LEFT + 10), (float)(SCREENS_ROWS_TOP + 4 * SCREENS_ROW_PITCH + 4));
	CHECK(nav.result == SCREENS_NAV_NOTHING, "a row that has nothing in it");
	/* the buttons: 120, 250, 380, 510 */
	for (index = 0; index < 4; index++)
	{
		CHECK(screens_button_left(index, 4) == 120 + 130 * index, "button %d is at %d", index, screens_button_left(index, 4));
		screens_nav_tap(&nav, (float)(120 + 130 * index + 3), (float)(SCREENS_BUTTONS_TOP + 3));
		CHECK(nav.result == SCREENS_NAV_BUTTON && nav.button == index && nav.area == SCREENS_AREA_BUTTONS, "button %d", index);
	}
	screens_nav_tap(&nav, 120 + 128 + 1, (float)(SCREENS_BUTTONS_TOP + 3));
	CHECK(nav.result == SCREENS_NAV_NOTHING, "between two buttons");
	/* five buttons of 126 at 4, 131, 258, 385, 512 */
	nav.button_count = 5;
	for (index = 0; index < 5; index++)
	{
		CHECK(screens_button_left(index, 5) == 4 + 127 * index && screens_button_width(5) == 126, "five: button %d", index);
		screens_nav_tap(&nav, (float)(4 + 127 * index + 125), (float)(SCREENS_BUTTONS_TOP + 3));
		CHECK(nav.result == SCREENS_NAV_BUTTON && nav.button == index, "five: button %d's far edge", index);
		screens_nav_tap(&nav, (float)(4 + 127 * index + 126), (float)(SCREENS_BUTTONS_TOP + 3));
		CHECK(nav.result == SCREENS_NAV_NOTHING, "five: the gap after button %d", index);
	}
	nav.button_count = 4;
	screens_nav_tap(&nav, 130, (float)(SCREENS_BUTTONS_TOP + SCREENS_BUTTON_HEIGHT));
	CHECK(nav.result == SCREENS_NAV_NOTHING, "below the buttons");
	CHECK(screens_button_left(0, 2) == 380 && screens_button_left(1, 2) == 510 && screens_button_left(0, 1) == 510, "the bar's places");
}

static struct screens_toast toast_make(int kind, long id, char const *text)
{
	struct screens_toast toast;

	memset(&toast, 0, sizeof(toast));
	toast.id = id;
	toast.kind = (unsigned char)kind;
	snprintf(toast.text, sizeof(toast.text), "%s", text);
	return toast;
}

static void toast_queue(void)
{
	struct screens_toast_state state;
	struct screens_toast toast;
	int index;

	screens_toast_reset(&state);
	CHECK(!screens_toast_tick(&state, 1000), "nothing queued, nothing shown");
	toast = toast_make(SCREENS_TOAST_NOTICE, 1, "ONE");
	screens_toast_push(&state, &toast, 1000);
	toast = toast_make(SCREENS_TOAST_NOTICE, 2, "TWO");
	screens_toast_push(&state, &toast, 1000);
	CHECK(screens_toast_tick(&state, 1000) && !strcmp(state.current.text, "ONE") && state.count == 1, "the first shown");
	/* the same text again within a minute is dropped */
	toast = toast_make(SCREENS_TOAST_NOTICE, 3, "ONE");
	screens_toast_push(&state, &toast, 2000);
	CHECK(state.count == 1 && state.dropped == 1, "the same toast within a minute is dropped");
	screens_toast_push(&state, &toast, 1000 + SCREENS_TOAST_SAME_MILLISECONDS + 1);
	CHECK(state.count == 2, "after a minute it is shown again");
	/* the next waits for the first's end and the gap */
	CHECK(screens_toast_tick(&state, 1000 + SCREENS_TOAST_MILLISECONDS - 1) && !strcmp(state.current.text, "ONE"), "still ONE just before its end");
	CHECK(!screens_toast_tick(&state, 1000 + SCREENS_TOAST_MILLISECONDS), "ended: the gap");
	CHECK(!screens_toast_tick(&state, 1000 + SCREENS_TOAST_MILLISECONDS + SCREENS_TOAST_GAP_MILLISECONDS - 1), "still in the gap");
	CHECK(screens_toast_tick(&state, 1000 + SCREENS_TOAST_MILLISECONDS + SCREENS_TOAST_GAP_MILLISECONDS) && !strcmp(state.current.text, "TWO"), "TWO after the gap");
	/* an invitation jumps the queue (behind other invitations) */
	screens_toast_reset(&state);
	for (index = 0; index < 3; index++)
	{
		char text[16];

		snprintf(text, sizeof(text), "N%d", index);
		toast = toast_make(SCREENS_TOAST_NOTICE, index, text);
		screens_toast_push(&state, &toast, 0);
	}
	toast = toast_make(SCREENS_TOAST_INVITE, 10, "A INVITES YOU");
	screens_toast_push(&state, &toast, 0);
	toast = toast_make(SCREENS_TOAST_INVITE, 11, "B INVITES YOU");
	screens_toast_push(&state, &toast, 0);
	CHECK(!strcmp(state.queue[0].text, "A INVITES YOU") && !strcmp(state.queue[1].text, "B INVITES YOU") && !strcmp(state.queue[2].text, "N0"), "invitations first, in order: %s %s %s",
		state.queue[0].text, state.queue[1].text, state.queue[2].text);
	/* two invitations from the same friend are both kept (they are never "the same toast") */
	toast = toast_make(SCREENS_TOAST_INVITE, 12, "A INVITES YOU");
	screens_toast_push(&state, &toast, 0);
	CHECK(state.count == 6, "a repeat invitation is kept: %d", state.count);
	/* a full queue drops the oldest notice, never an invitation */
	screens_toast_reset(&state);
	for (index = 0; index < SCREENS_TOASTS_MAXIMUM; index++)
	{
		char text[16];

		snprintf(text, sizeof(text), "M%d", index);
		toast = toast_make(index == 0 ? SCREENS_TOAST_INVITE : SCREENS_TOAST_NOTICE, 100 + index, text);
		screens_toast_push(&state, &toast, 0);
	}
	CHECK(state.count == SCREENS_TOASTS_MAXIMUM, "full");
	toast = toast_make(SCREENS_TOAST_NOTICE, 200, "EXTRA");
	screens_toast_push(&state, &toast, 0);
	CHECK(state.count == SCREENS_TOASTS_MAXIMUM && state.dropped == 1 && !strcmp(state.queue[0].text, "M0") && strcmp(state.queue[1].text, "M1") && !strcmp(state.queue[SCREENS_TOASTS_MAXIMUM - 1].text, "EXTRA"),
		"the oldest notice went, the invitation stayed: %s %s", state.queue[0].text, state.queue[1].text);
	/* every one an invitation: a new notice is dropped */
	screens_toast_reset(&state);
	for (index = 0; index < SCREENS_TOASTS_MAXIMUM; index++)
	{
		toast = toast_make(SCREENS_TOAST_INVITE, 300 + index, "X INVITES YOU");
		screens_toast_push(&state, &toast, 0);
	}
	toast = toast_make(SCREENS_TOAST_NOTICE, 400, "LOST");
	screens_toast_push(&state, &toast, 0);
	CHECK(state.count == SCREENS_TOASTS_MAXIMUM && state.dropped == 1 && strcmp(state.queue[SCREENS_TOASTS_MAXIMUM - 1].text, "LOST"), "no room: the notice is dropped");
}

static void toast_time(void)
{
	struct screens_toast_state state;
	struct screens_toast toast = toast_make(SCREENS_TOAST_NOTICE, 1, "ALICE IS ONLINE");
	unsigned long start = 5000;

	screens_toast_reset(&state);
	screens_toast_push(&state, &toast, start);
	screens_toast_tick(&state, start);
	CHECK(screens_toast_alpha(&state, start) == 0, "it fades in from nothing");
	CHECK(screens_toast_alpha(&state, start + SCREENS_TOAST_FADE_MILLISECONDS / 2) >= 120 && screens_toast_alpha(&state, start + SCREENS_TOAST_FADE_MILLISECONDS / 2) <= 135, "half way in: %d",
		screens_toast_alpha(&state, start + SCREENS_TOAST_FADE_MILLISECONDS / 2));
	CHECK(screens_toast_alpha(&state, start + 1000) == 255 && screens_toast_alpha(&state, start + 3000) == 255, "full in between");
	CHECK(screens_toast_alpha(&state, start + SCREENS_TOAST_MILLISECONDS - SCREENS_TOAST_FADE_MILLISECONDS / 2) >= 120 &&
		screens_toast_alpha(&state, start + SCREENS_TOAST_MILLISECONDS - SCREENS_TOAST_FADE_MILLISECONDS / 2) <= 135, "half way out");
	CHECK(screens_toast_alpha(&state, start + SCREENS_TOAST_MILLISECONDS) == 0, "gone at its end");
	/* an invitation waits thirty seconds, and a D-pad press answers it */
	screens_toast_reset(&state);
	toast = toast_make(SCREENS_TOAST_INVITE, 77, "ALICE INVITES YOU");
	screens_toast_push(&state, &toast, start);
	screens_toast_tick(&state, start);
	CHECK(screens_toast_tick(&state, start + SCREENS_TOAST_MILLISECONDS + 100), "an invitation outlasts a notice");
	CHECK(screens_toast_tick(&state, start + SCREENS_INVITE_MILLISECONDS - 1), "and runs until thirty seconds");
	CHECK(!screens_toast_tick(&state, start + SCREENS_INVITE_MILLISECONDS), "then it ends unanswered");
	/* answers: from the menus right joins at once, left is later; from a game right asks again */
	{
		long id = 0;

		screens_toast_reset(&state);
		toast = toast_make(SCREENS_TOAST_INVITE, 78, "ALICE INVITES YOU");
		screens_toast_push(&state, &toast, start);
		screens_toast_tick(&state, start);
		CHECK(screens_toast_answer(&state, 1, 0, start + 100, &id) == 1 && id == 78 && !state.shown, "from the menus right joins at once");
		screens_toast_push(&state, &toast, start + 5000);
		screens_toast_tick(&state, start + 5000 + SCREENS_TOAST_GAP_MILLISECONDS);
		CHECK(state.shown, "shown again");
		CHECK(screens_toast_answer(&state, 1, 1, start + 7000, &id) == 0 && state.shown && state.confirming, "from a game, right asks again");
		CHECK(screens_toast_answer(&state, 1, 1, start + 8000, &id) == 1 && !state.shown, "the second press joins");
		screens_toast_push(&state, &toast, start + 20000);
		screens_toast_tick(&state, start + 20000 + SCREENS_TOAST_GAP_MILLISECONDS);
		CHECK(screens_toast_answer(&state, 1, 1, start + 22000, &id) == 0 && state.confirming, "asked again");
		screens_toast_tick(&state, start + 22000 + 5000);
		CHECK(!state.confirming && state.shown, "the question lapses after five seconds, the invitation stays");
		CHECK(screens_toast_answer(&state, 0, 1, start + 28000, &id) == 1 && !state.shown, "left: later, from a game, at once");
		/* nothing to answer: a notice, or nothing shown */
		screens_toast_reset(&state);
		toast = toast_make(SCREENS_TOAST_NOTICE, 79, "NOTICE");
		screens_toast_push(&state, &toast, start);
		screens_toast_tick(&state, start);
		CHECK(screens_toast_answer(&state, 1, 0, start + 10, &id) == 0 && state.shown, "a notice takes no answer");
	}
	/* the millisecond clock wraps at 2^32: a toast shown across the wrap lives its time */
	{
		struct screens_toast_state wrapped;
		unsigned long near_end = (unsigned long)0xFFFFFFFFUL - 500;

		screens_toast_reset(&wrapped);
		toast = toast_make(SCREENS_TOAST_NOTICE, 80, "WRAP");
		screens_toast_push(&wrapped, &toast, near_end);
		screens_toast_tick(&wrapped, near_end);
		if (sizeof(unsigned long) == 4)
			CHECK(screens_toast_tick(&wrapped, near_end + 2000) && screens_toast_alpha(&wrapped, near_end + 2000) == 255, "across the wrap");
	}
}

static void disabled(void)
{
	CHECK(screens_enabled() == 0, "enable is 0");
	CHECK(screens_provider() == NULL, "off: no provider, whatever the mock has");
	{
		static struct screens_model model_data;

		CHECK(screens_model_update(&model_data, 1) == 0 && !model_data.valid && model_data.roster_count == 0, "off: the model stays empty");
	}
	/* a provider that was set is used, enable or not (the backend's own decision: the screens' entries are what enable gates) */
	screens_provider_set(screens_mock_provider());
	CHECK(screens_provider() == screens_mock_provider(), "a provider set is the provider");
	screens_provider_set(NULL);
}

static void enabled_levels(void)
{
	CHECK(screens_enabled() == (int)enable_setting, "level read once: %d", screens_enabled());
	CHECK(screens_provider() == (enable_setting == 1 ? screens_mock_provider() : NULL), "level %ld's provider", enable_setting);
}

int main(int argc, char **argv)
{
	char const *name = argc > 1 ? argv[1] : "";

	if (!strcmp(name, "disabled"))
		enable_setting = 0;
	else if (!strcmp(name, "level-2"))
		enable_setting = 2;
	else if (!strcmp(name, "level-clamped"))
		enable_setting = 9;
	if (!strcmp(name, "words"))
		words();
	else if (!strcmp(name, "fit-names"))
		fit_names();
	else if (!strcmp(name, "fit-columns"))
		fit_columns();
	else if (!strcmp(name, "fit-cut"))
		fit_cut();
	else if (!strcmp(name, "player-cells"))
		player_cells();
	else if (!strcmp(name, "friend-cells"))
		friend_cells();
	else if (!strcmp(name, "party-cells"))
		party_cells();
	else if (!strcmp(name, "tabs"))
		tabs();
	else if (!strcmp(name, "sorting"))
		sorting();
	else if (!strcmp(name, "actions"))
		actions();
	else if (!strcmp(name, "model"))
		model();
	else if (!strcmp(name, "mock-actions"))
		mock_actions();
	else if (!strcmp(name, "mock-text"))
		mock_text();
	else if (!strcmp(name, "view"))
		view_make_case();
	else if (!strcmp(name, "empty-text"))
		empty_text();
	else if (!strcmp(name, "nav-players"))
		nav_players();
	else if (!strcmp(name, "nav-friends"))
		nav_friends();
	else if (!strcmp(name, "nav-tap"))
		nav_tap();
	else if (!strcmp(name, "toast-queue"))
		toast_queue();
	else if (!strcmp(name, "toast-time"))
		toast_time();
	else if (!strcmp(name, "disabled"))
		disabled();
	else if (!strcmp(name, "level-1") || !strcmp(name, "level-2"))
		enabled_levels();
	else if (!strcmp(name, "level-clamped"))
		CHECK(screens_enabled() == 2, "a level above 2 reads as 2: %d", screens_enabled());
	else
	{
		fprintf(stderr, "no case named %s\n", name);
		return 2;
	}
	return 0;
}
