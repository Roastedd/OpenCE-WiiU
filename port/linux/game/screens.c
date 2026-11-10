/*
SCREENS.C

The social screens' model: the provider in use, the words and colours, the text cut to a column's width, the cells of every row, the order the focus moves in, the toast queue and
its timing. No game types and no drawing (see screens.h), so the harness compiles this file alone.
*/

#include "screens.h"

#include <stdio.h>
#include <string.h>

/* port_config.c's */
long config_integer(const char *name);

#include "screens_metrics.inc"

/* ---------- the setting and the provider */

int screens_enabled(void)
{
	static int level = -1;

	if (level < 0)
	{
		long value = config_integer("screens.enable");

		level = value < 0 ? 0 : value > 2 ? 2 : (int)value;
	}
	return level;
}

static struct screens_provider const *provider_in_use;

struct screens_provider const *screens_provider(void)
{
	if (provider_in_use)
		return provider_in_use;
	/* (1: the mock; 2: the game's roster with the backend's rest, which menu_screens.inl makes and sets; none before that) */
	return screens_enabled() == 1 ? screens_mock_provider() : NULL;
}

void screens_provider_set(struct screens_provider const *provider)
{
	provider_in_use = provider;
}

/* the friends service's table (the social stream's), if a build has one: screens.enable 2's provider takes its friends, party, toasts and actions from it */
static struct screens_provider const *backend_table;

struct screens_provider const *screens_backend(void)
{
	return backend_table;
}

void screens_backend_set(struct screens_provider const *backend)
{
	backend_table = backend;
}

/* ---------- colours and words */

unsigned long screens_color_rgb(int color)
{
	static const unsigned long table[SCREENS_COLORS] =
	{
		0x2896FF, 0xFFFFFF, 0xFFB020, 0x40D050, 0x9CABBA, 0xFF8078, 0xF8847A, 0x86AAF8, 0x6CBAFF,
	};

	return color >= 0 && color < SCREENS_COLORS ? table[color] : table[SCREENS_COLOR_AZURE];
}

char const *screens_skill_word(int skill)
{
	static char const *const words[SCREENS_SKILLS] = { "RECRUIT", "EASY", "NORMAL", "HEROIC", "VETERAN", "LEGENDARY" };

	return skill >= 0 && skill < SCREENS_SKILLS ? words[skill] : "";
}

char const *screens_team_word(int team)
{
	return team == SCREENS_TEAM_RED ? "RED" : team == SCREENS_TEAM_BLUE ? "BLUE" : "";
}

char const *screens_presence_word(int presence)
{
	switch (presence)
	{
	case SCREENS_PRESENCE_AWAY:
		return "AWAY";
	case SCREENS_PRESENCE_ONLINE:
		return "ONLINE";
	case SCREENS_PRESENCE_LOBBY:
		return "IN LOBBY";
	case SCREENS_PRESENCE_PLAYING:
		return "PLAYING";
	case SCREENS_PRESENCE_REQUEST_SENT:
		return "REQUEST SENT";
	case SCREENS_PRESENCE_REQUEST_GOT:
		return "WANTS TO BE FRIENDS";
	case SCREENS_PRESENCE_BLOCKED:
		return "BLOCKED";
	default:
		return "OFFLINE";
	}
}

char const *screens_join_word(int join)
{
	switch (join)
	{
	case SCREENS_JOIN_FULL:
		return "FULL";
	case SCREENS_JOIN_CLOSED:
		return "CLOSED";
	case SCREENS_JOIN_PASSWORD:
		return "PASSWORD";
	case SCREENS_JOIN_NEWER_BUILD:
		return "NEWER BUILD";
	case SCREENS_JOIN_OLDER_BUILD:
		return "OLDER BUILD";
	case SCREENS_JOIN_DOWNLOAD:
		return "DOWNLOAD";
	case SCREENS_JOIN_NEEDS_MAP:
		return "NEEDS MAP";
	default:
		return "";
	}
}

char const *screens_member_word(int state)
{
	static char const *const words[] = { "READY", "IN MENUS", "JOINING", "DOWNLOADING", "NEEDS MAP", "FULL - RETRYING", "NEWER BUILD", "NO ROUTE", "IN GAME", "OFFLINE",
		"STAYING" };

	return state >= 0 && state < (int)(sizeof(words) / sizeof(words[0])) ? words[state] : "";
}

char const *screens_service_word(int service)
{
	switch (service)
	{
	case SCREENS_SERVICE_CONNECTING:
		return "CONNECTING...";
	case SCREENS_SERVICE_CONNECTED:
		return "CONNECTED";
	case SCREENS_SERVICE_DISABLED:
		return "FRIENDS ARE OFF";
	default:
		return "OFFLINE";
	}
}

char const *screens_tab_word(int tab)
{
	static char const *const words[SCREENS_TABS] = { "FRIENDS", "REQUESTS", "PARTY", "BLOCKED" };

	return tab >= 0 && tab < SCREENS_TABS ? words[tab] : "";
}

char const *screens_action_word(int action)
{
	static char const *const words[SCREENS_ACTIONS] =
	{
		"", "MUTE", "UNMUTE", "START A VOTE TO KICK", "KICK", "BAN", "MUTE FOR EVERYONE", "ADD FRIEND", "INVITE TO PARTY", "DROP BOT", "RAISE SKILL", "LOWER SKILL",
		"CHANGE TEAM", "JOIN GAME", "INVITE TO GAME", "VIEW CODE", "REMOVE FRIEND", "BLOCK", "UNBLOCK", "ACCEPT", "DECLINE", "MAKE LEADER", "REMOVE FROM PARTY",
		"LEAVE PARTY", "DISBAND PARTY", "FOLLOW LEADER: ON", "FOLLOW LEADER: OFF", "BOT SETTINGS", "CANCEL",
	};

	return action > 0 && action < SCREENS_ACTIONS ? words[action] : "";
}

/* ---------- text fit */

float screens_text_width(int font, char const *text)
{
	float const *advance = font == SCREENS_FONT_LARGE ? screens_advance_large : screens_advance_small;
	float width = 0.0f;

	for (; text && *text; text++)
	{
		unsigned char character = (unsigned char)*text;

		width += character >= 0x20 && character <= 0x7E ? advance[character - 0x20] : advance[0x3F - 0x20];
	}
	return width;
}

int screens_fit(int font, float width, char const *text, char *destination, int size)
{
	int length = 0;

	if (size <= 0)
		return 0;
	destination[0] = 0;
	if (!text)
		return 0;
	if (screens_text_width(font, text) <= width)
	{
		snprintf(destination, (size_t)size, "%s", text);
		return (int)strlen(destination);
	}
	/* (cut, and three dots: the longest start that with them fits) */
	{
		float dots = screens_text_width(font, "...");
		float used = 0.0f;
		float const *advance = font == SCREENS_FONT_LARGE ? screens_advance_large : screens_advance_small;

		while (text[length] && length < size - 4)
		{
			unsigned char character = (unsigned char)text[length];
			float next = used + (character >= 0x20 && character <= 0x7E ? advance[character - 0x20] : advance[0x3F - 0x20]);

			if (next + dots > width)
				break;
			used = next;
			length++;
		}
		/* (a trailing space before the dots is dropped) */
		while (length > 0 && text[length - 1] == ' ')
			length--;
		memcpy(destination, text, (size_t)length);
		if (width < dots)
			length = 0;
		memcpy(destination + length, "...", 4);
		return length + 3;
	}
}

static float range_width(int font, char const *text, int length)
{
	float const *advance = font == SCREENS_FONT_LARGE ? screens_advance_large : screens_advance_small;
	float width = 0.0f;
	int index;

	for (index = 0; index < length; index++)
	{
		unsigned char character = (unsigned char)text[index];

		width += character >= 0x20 && character <= 0x7E ? advance[character - 0x20] : advance[0x3F - 0x20];
	}
	return width;
}

/* the metrics' measure (screens_wrap's): the font is the context */
static float metrics_measure(void *context, char const *text, int length)
{
	return range_width((int)(long)context, text, length);
}

int screens_wrap_measured(float (*measure)(void *context, char const *text, int length), void *context, float width, int max_lines, char const *text, char *destination, int size)
{
	char const *position = text, *line_start[SCREENS_WRAP_LINES];
	int line_length[SCREENS_WRAP_LINES], lines = 0, whole = 1, length = 0, index;

	if (size <= 0)
		return 0;
	destination[0] = 0;
	if (!text)
		return 0;
	if (max_lines > SCREENS_WRAP_LINES)
		max_lines = SCREENS_WRAP_LINES;
	if (max_lines < 1)
		max_lines = 1;
	while (*position == ' ')
		position++;
	while (*position && lines < max_lines)
	{
		char const *start = position, *end = position, *next = position;

		/* (the most whole words that fit; the first is taken whatever its width) */
		for (;;)
		{
			char const *word_end = next;

			while (*word_end && *word_end != ' ')
				word_end++;
			if (word_end == next)
				break;
			if (end != start && measure(context, start, (int)(word_end - start)) > width)
				break;
			end = word_end;
			next = word_end;
			while (*next == ' ')
				next++;
		}
		line_start[lines] = start;
		line_length[lines] = (int)(end - start);
		lines++;
		position = next;
	}
	if (*position)
		whole = 0;
	for (index = 0; index < lines; index++)
	{
		char part[SCREENS_WRAP_SOURCE];
		char const *source = line_start[index];
		int source_length = line_length[index];

		if (index == lines - 1 && !whole)
			source_length = (int)strlen(source);
		if (source_length >= (int)sizeof(part) - 3)
			source_length = (int)sizeof(part) - 4;
		memcpy(part, source, (size_t)source_length);
		part[source_length] = 0;
		if (measure(context, part, source_length) > width)
		{
			/* (a line that is still too wide, a word wider than the box or the text past the last line: cut, and dots) */
			whole = 0;
			while (source_length > 0)
			{
				memcpy(part + source_length, "...", 4);
				if (measure(context, part, source_length + 3) <= width)
					break;
				source_length--;
				while (source_length > 0 && part[source_length - 1] == ' ')
					source_length--;
			}
			memcpy(part + source_length, "...", 4);
		}
		if (length + (index ? 1 : 0) + (int)strlen(part) >= size)
		{
			whole = 0;
			break;
		}
		if (index)
			destination[length++] = '\r';
		memcpy(destination + length, part, strlen(part) + 1);
		length += (int)strlen(part);
	}
	return whole ? lines : -lines;
}

int screens_wrap(int font, float width, int max_lines, char const *text, char *destination, int size)
{
	return screens_wrap_measured(metrics_measure, (void *)(long)font, width, max_lines, text, destination, size);
}

/* ---------- cells */

static void cell_set(struct screens_cell *cell, int font, float width, int color, char const *text)
{
	/* (the engine starts a text a few units in from its widget's left and clips at the right edge: a text is cut to the column less that inset and a margin) */
	screens_fit(font, width - SCREENS_TEXT_INSET, text, cell->text, (int)sizeof(cell->text));
	/* (the small font on the panel's moving background: the menus' azure, 3.3 to 1 against its brightest places, is its lighter sky blue, 4.9) */
	if (font == SCREENS_FONT_SMALL && color == SCREENS_COLOR_AZURE)
		color = SCREENS_COLOR_SKY;
	cell->color = (unsigned char)color;
}

static int ping_color(int ping)
{
	return ping < 80 ? SCREENS_COLOR_GREEN : ping < 160 ? SCREENS_COLOR_AMBER : SCREENS_COLOR_ALERT;
}

static void tag_text(struct screens_roster_row const *row, char *text, int size, int *color)
{
	*color = SCREENS_COLOR_AZURE;
	text[0] = 0;
	if (row->flags & SCREENS_ROW_BOT)
	{
		snprintf(text, (size_t)size, "BOT %s", screens_skill_word(row->skill));
		*color = SCREENS_COLOR_AMBER;
	}
	else if ((row->flags & SCREENS_ROW_ME) && (row->flags & SCREENS_ROW_HOST))
	{
		snprintf(text, (size_t)size, "YOU - HOST");
		*color = SCREENS_COLOR_WHITE;
	}
	else if (row->flags & SCREENS_ROW_ME)
	{
		snprintf(text, (size_t)size, "YOU");
		*color = SCREENS_COLOR_WHITE;
	}
	else if (row->flags & SCREENS_ROW_HOST)
	{
		snprintf(text, (size_t)size, "HOST");
		*color = SCREENS_COLOR_WHITE;
	}
	else if (row->flags & SCREENS_ROW_PARTY)
	{
		snprintf(text, (size_t)size, "PARTY");
	}
	else if (row->flags & SCREENS_ROW_FRIEND)
	{
		snprintf(text, (size_t)size, "FRIEND");
		*color = SCREENS_COLOR_GREEN;
	}
}

static int team_color(int team)
{
	return team == SCREENS_TEAM_RED ? SCREENS_COLOR_RED_TEAM : team == SCREENS_TEAM_BLUE ? SCREENS_COLOR_BLUE_TEAM : SCREENS_COLOR_AZURE;
}

void screens_player_cells(struct screens_roster_row const *row, struct screens_roster_summary const *summary, struct screens_cell *cells)
{
	char text[SCREENS_LINE_SIZE];
	int color;

	(void)summary;
	memset(cells, 0, SCREENS_PLAYER_CELLS * sizeof(*cells));
	cell_set(&cells[SCREENS_PLAYER_NAME], SCREENS_FONT_LARGE, (float)SCREENS_PLAYER_NAME_WIDTH,
		(row->flags & SCREENS_ROW_MUTED) ? SCREENS_COLOR_GREY : SCREENS_COLOR_AZURE, row->name);
	tag_text(row, text, (int)sizeof(text), &color);
	cell_set(&cells[SCREENS_PLAYER_TAG], SCREENS_FONT_SMALL, (float)SCREENS_PLAYER_TAG_WIDTH, color, text);
	cell_set(&cells[SCREENS_PLAYER_TEAM], SCREENS_FONT_SMALL, (float)SCREENS_PLAYER_TEAM_WIDTH, team_color(row->team), screens_team_word(row->team));
	if (row->ping >= 0)
	{
		snprintf(text, sizeof(text), "%d MS", row->ping > 999 ? 999 : row->ping);
		cell_set(&cells[SCREENS_PLAYER_PING], SCREENS_FONT_SMALL, (float)SCREENS_PLAYER_PING_WIDTH, ping_color(row->ping), text);
	}
	else
	{
		cell_set(&cells[SCREENS_PLAYER_PING], SCREENS_FONT_SMALL, (float)SCREENS_PLAYER_PING_WIDTH, SCREENS_COLOR_GREY, "--");
	}
}

void screens_lobby_cells(struct screens_roster_row const *row, int teams, struct screens_cell *cells)
{
	char text[SCREENS_LINE_SIZE];
	char tag[SCREENS_LINE_SIZE];
	int color;

	memset(cells, 0, SCREENS_LOBBY_CELLS * sizeof(*cells));
	cell_set(&cells[SCREENS_LOBBY_NAME], SCREENS_FONT_LARGE, (float)SCREENS_LOBBY_NAME_WIDTH, SCREENS_COLOR_AZURE, row->name);
	/* (the team word, then a bot's tag; a person's own tag only where no team takes the place: YOU, HOST are the Players page's, the lobby keeps its own line short) */
	text[0] = 0;
	if (teams && row->team != SCREENS_TEAM_NONE)
		snprintf(text, sizeof(text), "%s", screens_team_word(row->team));
	tag_text(row, tag, (int)sizeof(tag), &color);
	if (row->flags & SCREENS_ROW_BOT)
		snprintf(text + strlen(text), sizeof(text) - strlen(text), "%s%s", text[0] ? " " : "", tag);
	else if (!text[0])
		snprintf(text, sizeof(text), "%s", tag);
	cell_set(&cells[SCREENS_LOBBY_TAG], SCREENS_FONT_SMALL, (float)SCREENS_LOBBY_TAG_WIDTH,
		(row->flags & SCREENS_ROW_BOT) ? SCREENS_COLOR_AMBER : teams && row->team != SCREENS_TEAM_NONE ? team_color(row->team) : color, text);
}

static int friend_mark(struct screens_friend_row const *row, int *color)
{
	switch (row->presence)
	{
	case SCREENS_PRESENCE_PLAYING:
	case SCREENS_PRESENCE_LOBBY:
		*color = row->join == SCREENS_JOIN_OPEN || row->join == SCREENS_JOIN_DOWNLOAD || row->presence == SCREENS_PRESENCE_LOBBY ? SCREENS_COLOR_GREEN : SCREENS_COLOR_AMBER;
		return '*';
	case SCREENS_PRESENCE_ONLINE:
		*color = SCREENS_COLOR_GREEN;
		return '*';
	case SCREENS_PRESENCE_AWAY:
		*color = SCREENS_COLOR_AMBER;
		return 'o';
	case SCREENS_PRESENCE_REQUEST_SENT:
		*color = SCREENS_COLOR_GREY;
		return '?';
	case SCREENS_PRESENCE_REQUEST_GOT:
		*color = SCREENS_COLOR_AZURE;
		return '?';
	case SCREENS_PRESENCE_BLOCKED:
		*color = SCREENS_COLOR_ALERT;
		return 'x';
	default:
		*color = SCREENS_COLOR_GREY;
		return '-';
	}
}

/* the multiplayer maps' names as the game shows them (the lobby's list, ui\shell\main_menu\mp_map_list: the Xbox's and Halo PC's own); another map's is its file's name */
static struct
{
	char const *file;
	char const *title;
} const map_titles[] =
{
	{ "beavercreek", "Battle Creek" }, { "bloodgulch", "Blood Gulch" }, { "boardingaction", "Boarding Action" }, { "carousel", "Derelict" }, { "chillout", "Chill Out" },
	{ "damnation", "Damnation" }, { "dangercanyon", "Danger Canyon" }, { "deathisland", "Death Island" }, { "gephyrophobia", "Gephyrophobia" },
	{ "hangemhigh", "Hang 'Em High" }, { "icefields", "Ice Fields" }, { "infinity", "Infinity" }, { "longest", "Longest" }, { "prisoner", "Prisoner" },
	{ "putput", "Chiron TL-34" }, { "ratrace", "Rat Race" }, { "sidewinder", "Sidewinder" }, { "timberland", "Timberland" }, { "wizard", "Wizard" },
};

void screens_map_title(char const *map, char *title, int size)
{
	int index, length;

	if (size <= 0)
		return;
	title[0] = 0;
	if (!map)
		return;
	for (index = 0; index < (int)(sizeof(map_titles) / sizeof(map_titles[0])); index++)
	{
		/* (a map file's name is compared as it is, in lower case: Custom Edition maps of a stock map's name are the stock map's) */
		char lower[SCREENS_WORD_SIZE];
		int at;

		for (at = 0; map[at] && at < (int)sizeof(lower) - 1; at++)
			lower[at] = map[at] >= 'A' && map[at] <= 'Z' ? (char)(map[at] + 32) : map[at];
		lower[at] = 0;
		if (!strcmp(lower, map_titles[index].file))
		{
			snprintf(title, (size_t)size, "%s", map_titles[index].title);
			return;
		}
	}
	/* (another map's: the file's name with its underscores as spaces, so that a cut falls between words) */
	for (length = 0; map[length] && length < size - 1; length++)
		title[length] = map[length] == '_' ? ' ' : map[length];
	title[length] = 0;
}

int screens_fit_words(int font, float width, char const *text, char *destination, int size)
{
	int length, last_space = -1;
	float dots = screens_text_width(font, "...");

	if (size <= 0)
		return 0;
	destination[0] = 0;
	if (!text)
		return 0;
	if (screens_text_width(font, text) <= width)
		return screens_fit(font, width, text, destination, size);
	/* (the most whole words that fit with the dots; a first word wider than that is cut where it is) */
	for (length = 0; text[length] && length < size - 4; length++)
	{
		if (range_width(font, text, length + 1) + dots > width)
			break;
		if (text[length + 1] == ' ' || !text[length + 1])
			last_space = length + 1;
	}
	if (last_space <= 0)
		return screens_fit(font, width, text, destination, size);
	memcpy(destination, text, (size_t)last_space);
	while (last_space > 0 && destination[last_space - 1] == ' ')
		last_space--;
	memcpy(destination + last_space, "...", 4);
	return last_space + 3;
}

/* "PLAYING <map> 6/16 DOWNLOAD": the words and numbers kept whole, the map's name (as the game shows it) cut between its words to what is left of the column */
static void game_status(struct screens_friend_row const *row, char const *word, int font, float width, char *text, int size)
{
	char prefix[SCREENS_LINE_SIZE], suffix[SCREENS_LINE_SIZE], map[SCREENS_LINE_SIZE], title[SCREENS_LINE_SIZE];
	char const *join = screens_join_word(row->join);
	float room;

	snprintf(prefix, sizeof(prefix), "%s", word);
	suffix[0] = 0;
	if (row->maximum > 0)
		snprintf(suffix, sizeof(suffix), " %d/%d", row->players, row->maximum);
	if (join[0])
		snprintf(suffix + strlen(suffix), sizeof(suffix) - strlen(suffix), " %s", join);
	/* (half a unit under: a text that fits to the last digit is cut by the float's rounding) */
	room = width - SCREENS_TEXT_INSET - 0.5f - screens_text_width(font, prefix) - screens_text_width(font, suffix) - screens_text_width(font, " ");
	map[0] = 0;
	screens_map_title(row->map, title, (int)sizeof(title));
	if (title[0] && room > 24.0f)
	{
		screens_fit_words(font, room, title, map, (int)sizeof(map));
		/* (a name cut to fewer than five letters says nothing: "Da...", "Dange...": the status goes without it) */
		if (strcmp(map, title) && strlen(map) < 8)
			map[0] = 0;
	}
	snprintf(text, (size_t)size, "%s%s%s%s", prefix, map[0] ? " " : "", map, suffix);
}

void screens_friend_cells(int tab, struct screens_friend_row const *row, struct screens_cell *cells)
{
	char text[SCREENS_LINE_SIZE];
	int color, mark;
	char mark_text[2];

	memset(cells, 0, SCREENS_FRIEND_CELLS * sizeof(*cells));
	mark = row->party && tab == SCREENS_TAB_FRIENDS ? '+' : friend_mark(row, &color);
	if (row->party && tab == SCREENS_TAB_FRIENDS)
		color = SCREENS_COLOR_AZURE;
	mark_text[0] = (char)mark;
	mark_text[1] = 0;
	cell_set(&cells[SCREENS_FRIEND_MARK], SCREENS_FONT_LARGE, 24.0f, color, mark_text);
	cell_set(&cells[SCREENS_FRIEND_NAME], SCREENS_FONT_LARGE, (float)SCREENS_FRIEND_NAME_WIDTH,
		row->presence == SCREENS_PRESENCE_OFFLINE ? SCREENS_COLOR_GREY : SCREENS_COLOR_AZURE, row->name);
	switch (row->presence)
	{
	case SCREENS_PRESENCE_PLAYING:
		game_status(row, "PLAYING", SCREENS_FONT_SMALL, (float)SCREENS_FRIEND_STATUS_WIDTH, text, (int)sizeof(text));
		break;
	case SCREENS_PRESENCE_LOBBY:
		game_status(row, "IN LOBBY", SCREENS_FONT_SMALL, (float)SCREENS_FRIEND_STATUS_WIDTH, text, (int)sizeof(text));
		break;
	case SCREENS_PRESENCE_ONLINE:
		snprintf(text, sizeof(text), "ONLINE - IN THE MENUS");
		break;
	default:
		snprintf(text, sizeof(text), "%s", screens_presence_word(row->presence));
		break;
	}
	if (row->party && tab == SCREENS_TAB_FRIENDS && row->presence != SCREENS_PRESENCE_PLAYING && row->presence != SCREENS_PRESENCE_LOBBY)
	{
		char party[SCREENS_LINE_SIZE];

		snprintf(party, sizeof(party), "PARTY - %s", text);
		snprintf(text, sizeof(text), "%s", party);
	}
	cell_set(&cells[SCREENS_FRIEND_STATUS], SCREENS_FONT_SMALL, (float)SCREENS_FRIEND_STATUS_WIDTH, color, text);
}

void screens_member_cells(struct screens_party_member const *member, struct screens_cell *cells)
{
	char text[SCREENS_LINE_SIZE];
	int color = SCREENS_COLOR_AZURE;

	memset(cells, 0, SCREENS_MEMBER_CELLS * sizeof(*cells));
	cell_set(&cells[SCREENS_MEMBER_MARK], SCREENS_FONT_LARGE, 24.0f, SCREENS_COLOR_AZURE, member->leader ? "L" : "");
	cell_set(&cells[SCREENS_MEMBER_NAME], SCREENS_FONT_LARGE, (float)SCREENS_FRIEND_NAME_WIDTH, member->leader ? SCREENS_COLOR_WHITE : SCREENS_COLOR_AZURE, member->name);
	snprintf(text, sizeof(text), "%s", screens_member_word(member->state));
	switch (member->state)
	{
	case SCREENS_MEMBER_JOINING:
	case SCREENS_MEMBER_DOWNLOADING:
		snprintf(text, sizeof(text), "%s %d%%", screens_member_word(member->state), member->amount < 0 ? 0 : member->amount > 100 ? 100 : member->amount);
		color = SCREENS_COLOR_AMBER;
		break;
	case SCREENS_MEMBER_FULL_RETRYING:
		snprintf(text, sizeof(text), "%s %ds", screens_member_word(member->state), member->amount < 0 ? 0 : member->amount > 99 ? 99 : member->amount);
		color = SCREENS_COLOR_AMBER;
		break;
	case SCREENS_MEMBER_NEEDS_MAP:
	case SCREENS_MEMBER_NEWER_BUILD:
	case SCREENS_MEMBER_NO_ROUTE:
		color = SCREENS_COLOR_ALERT;
		break;
	case SCREENS_MEMBER_READY:
	case SCREENS_MEMBER_IN_GAME:
		color = SCREENS_COLOR_GREEN;
		break;
	case SCREENS_MEMBER_OFFLINE:
		color = SCREENS_COLOR_GREY;
		break;
	default:
		break;
	}
	cell_set(&cells[SCREENS_MEMBER_STATE], SCREENS_FONT_SMALL, (float)SCREENS_MEMBER_STATE_WIDTH, color, text);
}

/* ---------- details and heads */

void screens_player_detail(struct screens_roster_row const *row, struct screens_roster_summary const *summary, char *text, int size)
{
	char line[SCREENS_LINE_SIZE], part[SCREENS_LINE_SIZE];
	int color;

	snprintf(line, sizeof(line), "%s", row->name);
	tag_text(row, part, (int)sizeof(part), &color);
	if (part[0])
		snprintf(line + strlen(line), sizeof(line) - strlen(line), "  %s", part);
	if (summary->teams && row->team != SCREENS_TEAM_NONE)
		snprintf(line + strlen(line), sizeof(line) - strlen(line), "  %s", screens_team_word(row->team));
	if (row->score >= 0)
		snprintf(line + strlen(line), sizeof(line) - strlen(line), "  %d %s", row->score, row->score == 1 ? "KILL" : "KILLS");
	if (row->ping >= 0)
		snprintf(line + strlen(line), sizeof(line) - strlen(line), "  %d MS", row->ping > 999 ? 999 : row->ping);
	screens_wrap(SCREENS_FONT_LARGE, (float)SCREENS_LINE_BOX_WIDTH, SCREENS_LINE_BOX_LINES, line, text, size);
}

void screens_friend_detail(int tab, struct screens_friend_row const *row, struct screens_friends_summary const *summary, char *text, int size)
{
	char line[SCREENS_LINE_SIZE];

	(void)summary;
	if (tab == SCREENS_TAB_REQUESTS || row->presence == SCREENS_PRESENCE_REQUEST_GOT)
		snprintf(line, sizeof(line), "%s WANTS TO BE YOUR FRIEND. A: ACCEPT OR DECLINE.", row->name);
	else if (tab == SCREENS_TAB_BLOCKED)
		snprintf(line, sizeof(line), "%s IS BLOCKED. A: UNBLOCK.", row->name);
	else if (row->presence == SCREENS_PRESENCE_REQUEST_SENT)
		snprintf(line, sizeof(line), "%s WILL SEE YOUR REQUEST WHEN THEY ARE ONLINE.", row->name);
	else if (row->presence == SCREENS_PRESENCE_PLAYING || row->presence == SCREENS_PRESENCE_LOBBY)
	{
		switch (row->join)
		{
		case SCREENS_JOIN_OPEN:
			if (row->maximum > 0)
				snprintf(line, sizeof(line), "A: JOIN %s'S GAME, %d OF %d.", row->name, row->players, row->maximum);
			else
				snprintf(line, sizeof(line), "A: JOIN %s'S GAME.", row->name);
			break;
		case SCREENS_JOIN_DOWNLOAD:
			snprintf(line, sizeof(line), "A: JOIN %s'S GAME. THE MAP IS DOWNLOADED FIRST.", row->name);
			break;
		case SCREENS_JOIN_FULL:
			if (row->maximum > 0)
				snprintf(line, sizeof(line), "%s'S GAME IS FULL, %d OF %d PLAYERS.", row->name, row->players, row->maximum);
			else
				snprintf(line, sizeof(line), "%s'S GAME IS FULL.", row->name);
			break;
		case SCREENS_JOIN_CLOSED:
			snprintf(line, sizeof(line), "%s'S GAME HAS STARTED OR IS CLOSED.", row->name);
			break;
		case SCREENS_JOIN_PASSWORD:
			snprintf(line, sizeof(line), "%s'S GAME NEEDS A PASSWORD.", row->name);
			break;
		case SCREENS_JOIN_NEWER_BUILD:
			snprintf(line, sizeof(line), "%s RUNS A NEWER BUILD. UPDATE TO JOIN.", row->name);
			break;
		case SCREENS_JOIN_OLDER_BUILD:
			snprintf(line, sizeof(line), "%s RUNS AN OLDER BUILD.", row->name);
			break;
		case SCREENS_JOIN_NEEDS_MAP:
			snprintf(line, sizeof(line), "YOU DO NOT HAVE THE MAP AND IT CANNOT BE FETCHED.");
			break;
		default:
			snprintf(line, sizeof(line), "%s IS IN A GAME.", row->name);
			break;
		}
	}
	else if (row->presence == SCREENS_PRESENCE_ONLINE || row->presence == SCREENS_PRESENCE_AWAY)
		snprintf(line, sizeof(line), "A: INVITE %s.", row->name);
	else
		snprintf(line, sizeof(line), "%s IS OFFLINE.", row->name);
	screens_wrap(SCREENS_FONT_LARGE, (float)SCREENS_LINE_BOX_WIDTH, SCREENS_LINE_BOX_LINES, line, text, size);
}

void screens_player_head(struct screens_roster_summary const *summary, char *text, int size)
{
	char line[SCREENS_LINE_SIZE];

	if (summary->maximum > 0)
		snprintf(line, sizeof(line), "%d OF %d", summary->players, summary->maximum);
	else
		snprintf(line, sizeof(line), "%d PLAYERS", summary->players);
	if (summary->bots > 0)
		snprintf(line + strlen(line), sizeof(line) - strlen(line), "   BOTS %d", summary->bots);
	screens_fit(SCREENS_FONT_LARGE, 220.0f, line, text, size);
}

void screens_tab_label(int tab, int chosen, struct screens_friends_summary const *summary, struct screens_party_summary const *party, char *text, int size)
{
	char line[SCREENS_LINE_SIZE];
	int count = tab >= 0 && tab < SCREENS_TABS ? summary->counts[tab] : 0;

	if (tab == SCREENS_TAB_PARTY && party && party->present)
		snprintf(line, sizeof(line), "%s %d/%d", screens_tab_word(tab), party->size, party->maximum);
	else
		snprintf(line, sizeof(line), "%s %d", screens_tab_word(tab), count);
	if (chosen)
		snprintf(text, (size_t)size, "[%s]", line);
	else
		snprintf(text, (size_t)size, "%s", line);
}

void screens_tab_strip(int tab, struct screens_friends_summary const *summary, struct screens_party_summary const *party, char *text, int size)
{
	int index, length = 0;

	if (size > 0)
		text[0] = 0;
	for (index = 0; index < SCREENS_TABS && length < size - 1; index++)
	{
		char label[SCREENS_LINE_SIZE];

		screens_tab_label(index, index == tab, summary, party, label, (int)sizeof(label));
		length += snprintf(text + length, (size_t)(size - length), "%s%s", index ? "  " : "", label);
	}
}

void screens_party_head(struct screens_party_summary const *party, char *text, int size)
{
	char line[SCREENS_LINE_SIZE];

	if (!party->present)
		snprintf(line, sizeof(line), "NO PARTY");
	else
		snprintf(line, sizeof(line), "PARTY %d OF %d - FOLLOW LEADER %s", party->size, party->maximum, party->follow ? "ON" : "OFF");
	screens_fit(SCREENS_FONT_LARGE, 420.0f, line, text, size);
}

/* the order of the friends: a joinable game first, then a lobby, the rest of the online, away, offline; ties by name */
static int friend_rank(struct screens_friend_row const *row)
{
	if (row->presence == SCREENS_PRESENCE_PLAYING && (row->join == SCREENS_JOIN_OPEN || row->join == SCREENS_JOIN_DOWNLOAD))
		return 0;
	if (row->presence == SCREENS_PRESENCE_LOBBY)
		return 1;
	if (row->presence == SCREENS_PRESENCE_PLAYING)
		return 2;
	if (row->presence == SCREENS_PRESENCE_ONLINE)
		return 3;
	if (row->presence == SCREENS_PRESENCE_AWAY)
		return 4;
	return 5;
}

static int name_compare(char const *a, char const *b)
{
	for (; *a && *b; a++, b++)
	{
		int x = *a >= 'a' && *a <= 'z' ? *a - 32 : *a, y = *b >= 'a' && *b <= 'z' ? *b - 32 : *b;

		if (x != y)
			return x - y;
	}
	return (unsigned char)*a - (unsigned char)*b;
}

int screens_friends_sort(struct screens_friend_row *rows, int count)
{
	int i, j;

	/* (an insertion sort: stable, and 50 rows at most) */
	for (i = 1; i < count; i++)
	{
		struct screens_friend_row held = rows[i];

		for (j = i - 1; j >= 0; j--)
		{
			int rank_held = friend_rank(&held), rank_other = friend_rank(&rows[j]);

			if (rank_other < rank_held || (rank_other == rank_held && name_compare(rows[j].name, held.name) <= 0))
				break;
			rows[j + 1] = rows[j];
		}
		rows[j + 1] = held;
	}
	return count;
}

/* ---------- what A on a row offers, by who the row is (the screens' own rule, which a backend may replace; a provider's own list replaces it) */
void screens_default_roster_actions(struct screens_roster_row const *row, struct screens_roster_summary const *summary, int *actions)
{
	int count = 0;

	if (row->flags & SCREENS_ROW_BOT)
	{
		if (summary->host)
		{
			actions[count++] = SCREENS_ACTION_DROP_BOT;
			if (row->skill < SCREENS_SKILLS - 1)
				actions[count++] = SCREENS_ACTION_BOT_SKILL_UP;
			if (row->skill > 0)
				actions[count++] = SCREENS_ACTION_BOT_SKILL_DOWN;
			if (summary->teams)
				actions[count++] = SCREENS_ACTION_CHANGE_TEAM;
			actions[count++] = SCREENS_ACTION_BOT_SETTINGS;
		}
	}
	else if (!(row->flags & SCREENS_ROW_ME))
	{
		actions[count++] = (row->flags & SCREENS_ROW_MUTED) ? SCREENS_ACTION_UNMUTE : SCREENS_ACTION_MUTE;
		actions[count++] = SCREENS_ACTION_VOTE_KICK;
		if (!(row->flags & SCREENS_ROW_FRIEND))
			actions[count++] = SCREENS_ACTION_ADD_FRIEND;
		if (!(row->flags & SCREENS_ROW_PARTY))
			actions[count++] = SCREENS_ACTION_INVITE_PARTY;
		if (summary->host)
		{
			actions[count++] = SCREENS_ACTION_KICK;
			actions[count++] = SCREENS_ACTION_BAN;
		}
	}
	else
	{
		if (summary->teams)
			actions[count++] = SCREENS_ACTION_CHANGE_TEAM;
		if (summary->host)
			actions[count++] = SCREENS_ACTION_BOT_SETTINGS;
	}
	actions[count++] = SCREENS_ACTION_CANCEL;
	actions[count] = SCREENS_ACTION_NONE;
}

void screens_default_friend_actions(int tab, struct screens_friend_row const *row, int *actions)
{
	int count = 0;

	if (tab == SCREENS_TAB_REQUESTS && row->presence == SCREENS_PRESENCE_REQUEST_GOT)
	{
		actions[count++] = SCREENS_ACTION_ACCEPT;
		actions[count++] = SCREENS_ACTION_DECLINE;
		actions[count++] = SCREENS_ACTION_BLOCK;
	}
	else if (tab == SCREENS_TAB_REQUESTS)
	{
		actions[count++] = SCREENS_ACTION_DECLINE;
	}
	else if (tab == SCREENS_TAB_BLOCKED)
	{
		actions[count++] = SCREENS_ACTION_UNBLOCK;
	}
	else
	{
		if ((row->presence == SCREENS_PRESENCE_PLAYING || row->presence == SCREENS_PRESENCE_LOBBY) &&
			(row->join == SCREENS_JOIN_OPEN || row->join == SCREENS_JOIN_DOWNLOAD))
		{
			actions[count++] = SCREENS_ACTION_JOIN_GAME;
		}
		if (row->presence != SCREENS_PRESENCE_OFFLINE)
			actions[count++] = row->party ? SCREENS_ACTION_INVITE_GAME : SCREENS_ACTION_INVITE_PARTY;
		actions[count++] = SCREENS_ACTION_VIEW_CODE;
		actions[count++] = SCREENS_ACTION_REMOVE_FRIEND;
		actions[count++] = SCREENS_ACTION_BLOCK;
	}
	actions[count++] = SCREENS_ACTION_CANCEL;
	actions[count] = SCREENS_ACTION_NONE;
}

void screens_default_party_actions(struct screens_party_member const *member, struct screens_party_summary const *summary, int *actions)
{
	int count = 0;
	int leader_me = summary->i_lead;

	if (member->me)
	{
		actions[count++] = summary->follow ? SCREENS_ACTION_FOLLOW_OFF : SCREENS_ACTION_FOLLOW_ON;
		actions[count++] = leader_me ? SCREENS_ACTION_DISBAND_PARTY : SCREENS_ACTION_LEAVE_PARTY;
	}
	else if (leader_me)
	{
		actions[count++] = SCREENS_ACTION_MAKE_LEADER;
		actions[count++] = SCREENS_ACTION_REMOVE_FROM_PARTY;
	}
	actions[count++] = SCREENS_ACTION_CANCEL;
	actions[count] = SCREENS_ACTION_NONE;
}


static void empty_line(int screen, int tab, struct screens_friends_summary const *summary, int have_provider, char *line, int line_size)
{
	if (!have_provider)
		snprintf(line, (size_t)line_size, screen == 0 ? "THERE IS NO PLAYER LIST HERE." : "THERE IS NO FRIEND SERVICE IN THIS BUILD.");
	else if (screen == 0)
		snprintf(line, (size_t)line_size, "NO PLAYERS YET.");
	else if (summary && summary->service == SCREENS_SERVICE_DISABLED)
		snprintf(line, (size_t)line_size, "FRIENDS ARE OFF. TURN THEM ON IN NETWORK SETUP.");
	else if (summary && summary->service == SCREENS_SERVICE_OFF && tab == SCREENS_TAB_FRIENDS)
		snprintf(line, (size_t)line_size, "FRIENDS ARE OFFLINE. YOUR LIST IS KEPT.");
	else if (tab == SCREENS_TAB_FRIENDS)
		snprintf(line, (size_t)line_size, "NO FRIENDS YET. Y: ADD A FRIEND BY CODE.");
	else if (tab == SCREENS_TAB_REQUESTS)
		snprintf(line, (size_t)line_size, "NO REQUESTS.");
	else if (tab == SCREENS_TAB_PARTY)
		snprintf(line, (size_t)line_size, "NO PARTY. INVITE A FRIEND TO START ONE.");
	else
		snprintf(line, (size_t)line_size, "NO ONE IS BLOCKED.");
}

void screens_empty_text(int screen, int tab, struct screens_friends_summary const *summary, int have_provider, char *text, int size)
{
	char line[SCREENS_LINE_SIZE];

	empty_line(screen, tab, summary, have_provider, line, (int)sizeof(line));
	screens_fit(SCREENS_FONT_LARGE, 520.0f, line, text, size);
}

void screens_empty_wrapped(int screen, int tab, struct screens_friends_summary const *summary, int have_provider, char *text, int size)
{
	char line[SCREENS_LINE_SIZE];

	empty_line(screen, tab, summary, have_provider, line, (int)sizeof(line));
	screens_wrap(SCREENS_FONT_LARGE, (float)SCREENS_LINE_BOX_WIDTH, SCREENS_LINE_BOX_LINES, line, text, size);
}

void screens_empty_block(int screen, int tab, struct screens_friends_summary const *summary, int have_provider, char *text, int size)
{
	char line[SCREENS_LINE_SIZE];

	empty_line(screen, tab, summary, have_provider, line, (int)sizeof(line));
	/* (in the middle of the list's place: up to three lines of 440, not the detail line's two of 496) */
	screens_wrap(SCREENS_FONT_LARGE, (float)SCREENS_EMPTY_BOX_WIDTH, SCREENS_EMPTY_BOX_LINES, line, text, size);
}

void screens_scroll_text(int focus, int first, int shown, int count, char *text, int size)
{
	if (size <= 0)
		return;
	text[0] = 0;
	if (count <= shown || shown <= 0)
		return;
	/* (the row chosen's place in the whole list; with none chosen, the rows in view) */
	if (focus >= 0 && focus < shown)
		snprintf(text, (size_t)size, "%d OF %d", first + focus + 1, count);
	else
		snprintf(text, (size_t)size, "%d TO %d OF %d", first + 1, first + shown, count);
}

/* ---------- navigation */

int screens_button_left(int index, int count)
{
	return count >= 5 ? 4 + index * 127 : 510 - (count - 1 - index) * 130;
}

int screens_button_width(int count)
{
	return count >= 5 ? 126 : 128;
}

void screens_nav_begin(struct screens_nav *nav, int screen, int rows_visible, int tab_count, int button_count)
{
	memset(nav, 0, sizeof(*nav));
	nav->screen = screen;
	nav->rows_visible = rows_visible;
	nav->tab_count = tab_count;
	nav->button_count = button_count;
	nav->area = tab_count ? SCREENS_AREA_TABS : SCREENS_AREA_ROWS;
}

void screens_nav_rows(struct screens_nav *nav, int row_count)
{
	nav->row_count = row_count < 0 ? 0 : row_count;
	if (nav->first > nav->row_count - nav->rows_visible)
		nav->first = nav->row_count > nav->rows_visible ? nav->row_count - nav->rows_visible : 0;
	if (nav->row >= nav->rows_visible)
		nav->row = nav->rows_visible - 1;
	if (nav->first + nav->row >= nav->row_count)
		nav->row = nav->row_count > nav->first ? nav->row_count - nav->first - 1 : 0;
	if (nav->row < 0)
		nav->row = 0;
	/* (the focus was on a row that is gone: to the next place that has something) */
	if (nav->area == SCREENS_AREA_ROWS && !nav->row_count)
		nav->area = nav->button_count ? SCREENS_AREA_BUTTONS : nav->tab_count ? SCREENS_AREA_TABS : SCREENS_AREA_ROWS;
}

static void to_rows_end(struct screens_nav *nav)
{
	int visible = nav->row_count < nav->rows_visible ? nav->row_count : nav->rows_visible;

	nav->area = SCREENS_AREA_ROWS;
	nav->row = visible > 0 ? visible - 1 : 0;
}

void screens_nav_key(struct screens_nav *nav, int key)
{
	nav->result = SCREENS_NAV_NOTHING;
	if (key == SCREENS_KEY_B)
	{
		nav->result = SCREENS_NAV_BACK;
		return;
	}
	if ((key == SCREENS_KEY_L || key == SCREENS_KEY_R) && nav->tab_count)
	{
		nav->tab = (nav->tab + (key == SCREENS_KEY_R ? 1 : nav->tab_count - 1)) % nav->tab_count;
		nav->first = nav->row = 0;
		nav->result = SCREENS_NAV_TAB;
		return;
	}
	switch (nav->area)
	{
	case SCREENS_AREA_TABS:
		if (key == SCREENS_KEY_LEFT || key == SCREENS_KEY_RIGHT)
		{
			nav->tab = (nav->tab + (key == SCREENS_KEY_RIGHT ? 1 : nav->tab_count - 1)) % nav->tab_count;
			nav->first = nav->row = 0;
			nav->result = SCREENS_NAV_TAB;
		}
		else if (key == SCREENS_KEY_DOWN)
		{
			if (nav->row_count)
			{
				nav->area = SCREENS_AREA_ROWS;
				nav->row = 0;
			}
			else if (nav->button_count)
				nav->area = SCREENS_AREA_BUTTONS;
		}
		else if (key == SCREENS_KEY_UP)
		{
			if (nav->button_count)
				nav->area = SCREENS_AREA_BUTTONS;
			else if (nav->row_count)
				to_rows_end(nav);
		}
		else if (key == SCREENS_KEY_A || key == SCREENS_KEY_START)
			nav->result = SCREENS_NAV_TAB;
		break;
	case SCREENS_AREA_ROWS:
		if (key == SCREENS_KEY_UP)
		{
			if (nav->row > 0)
				nav->row--;
			else if (nav->first > 0)
				nav->first--;
			else if (nav->tab_count)
				nav->area = SCREENS_AREA_TABS;
			else if (nav->button_count)
				nav->area = SCREENS_AREA_BUTTONS;
		}
		else if (key == SCREENS_KEY_DOWN)
		{
			int visible = nav->row_count < nav->rows_visible ? nav->row_count : nav->rows_visible;

			if (nav->row < visible - 1)
				nav->row++;
			else if (nav->first + visible < nav->row_count)
				nav->first++;
			else if (nav->button_count)
				nav->area = SCREENS_AREA_BUTTONS;
			else if (nav->tab_count)
				nav->area = SCREENS_AREA_TABS;
		}
		else if (key == SCREENS_KEY_A || key == SCREENS_KEY_START)
		{
			if (nav->row_count)
				nav->result = SCREENS_NAV_OPEN_ACTIONS;
		}
		break;
	case SCREENS_AREA_BUTTONS:
		if (key == SCREENS_KEY_LEFT)
			nav->button = (nav->button + nav->button_count - 1) % nav->button_count;
		else if (key == SCREENS_KEY_RIGHT)
			nav->button = (nav->button + 1) % nav->button_count;
		else if (key == SCREENS_KEY_UP)
		{
			if (nav->row_count)
				to_rows_end(nav);
			else if (nav->tab_count)
				nav->area = SCREENS_AREA_TABS;
		}
		else if (key == SCREENS_KEY_DOWN)
		{
			if (nav->tab_count)
				nav->area = SCREENS_AREA_TABS;
			else if (nav->row_count)
			{
				nav->area = SCREENS_AREA_ROWS;
				nav->row = 0;
			}
		}
		else if (key == SCREENS_KEY_A || key == SCREENS_KEY_START)
			nav->result = SCREENS_NAV_BUTTON;
		break;
	}
}

void screens_nav_tap(struct screens_nav *nav, float x, float y)
{
	nav->result = SCREENS_NAV_NOTHING;
	if (nav->tab_count && y >= SCREENS_TAB_TOP && y < SCREENS_TAB_TOP + SCREENS_TAB_HEIGHT)
	{
		int tab = (int)((x - SCREENS_TAB_LEFT) / SCREENS_TAB_PITCH);

		if (x >= SCREENS_TAB_LEFT && tab >= 0 && tab < nav->tab_count && x < SCREENS_TAB_LEFT + tab * SCREENS_TAB_PITCH + SCREENS_TAB_WIDTH)
		{
			nav->area = SCREENS_AREA_TABS;
			if (nav->tab != tab)
				nav->first = nav->row = 0;
			nav->tab = tab;
			nav->result = SCREENS_NAV_TAB;
		}
		return;
	}
	if (y >= SCREENS_ROWS_TOP && y < SCREENS_ROWS_TOP + nav->rows_visible * SCREENS_ROW_PITCH && x >= SCREENS_ROWS_LEFT && x < SCREENS_ROWS_LEFT + SCREENS_ROW_WIDTH)
	{
		int row = (int)((y - SCREENS_ROWS_TOP) / SCREENS_ROW_PITCH);

		if (row >= 0 && row < nav->rows_visible && y < SCREENS_ROWS_TOP + row * SCREENS_ROW_PITCH + SCREENS_ROW_HEIGHT && nav->first + row < nav->row_count)
		{
			/* (a tap on the focused row opens it, as the pointer's click is A; on another it moves the focus and clicks, which the engine does in one: a row is chosen and opened) */
			nav->area = SCREENS_AREA_ROWS;
			nav->row = row;
			nav->result = SCREENS_NAV_OPEN_ACTIONS;
		}
		return;
	}
	if (y >= SCREENS_BUTTONS_TOP && y < SCREENS_BUTTONS_TOP + SCREENS_BUTTON_HEIGHT)
	{
		int button;

		for (button = 0; button < nav->button_count; button++)
		{
			int left = screens_button_left(button, nav->button_count);

			if (x >= left && x < left + screens_button_width(nav->button_count))
			{
				nav->area = SCREENS_AREA_BUTTONS;
				nav->button = button;
				nav->result = SCREENS_NAV_BUTTON;
				return;
			}
		}
	}
}

/* ---------- toasts */

void screens_toast_reset(struct screens_toast_state *state)
{
	memset(state, 0, sizeof(*state));
}

static int toast_is_invite(struct screens_toast const *toast)
{
	return toast->kind == SCREENS_TOAST_INVITE;
}

static unsigned long toast_lifetime(struct screens_toast const *toast)
{
	return toast_is_invite(toast) ? SCREENS_INVITE_MILLISECONDS : SCREENS_TOAST_MILLISECONDS;
}

void screens_toast_push(struct screens_toast_state *state, struct screens_toast const *toast, unsigned long now)
{
	int index;

	/* (the same line within a minute: dropped; an invitation is never the same as one before: it has its own id) */
	if (!toast_is_invite(toast))
	{
		for (index = 0; index < 4; index++)
		{
			if (state->recent_text[index][0] && !strcmp(state->recent_text[index], toast->text) && now - state->recent_at[index] < SCREENS_TOAST_SAME_MILLISECONDS)
			{
				state->dropped++;
				return;
			}
		}
		snprintf(state->recent_text[state->recent_next], SCREENS_LINE_SIZE, "%s", toast->text);
		state->recent_at[state->recent_next] = now;
		state->recent_next = (state->recent_next + 1) % 4;
	}
	if (state->count >= SCREENS_TOASTS_MAXIMUM)
	{
		/* (the oldest that is not an invitation goes; if every one is an invitation the new one is dropped) */
		for (index = 0; index < state->count && toast_is_invite(&state->queue[index]); index++)
			;
		if (index == state->count)
		{
			state->dropped++;
			return;
		}
		memmove(&state->queue[index], &state->queue[index + 1], (size_t)(state->count - index - 1) * sizeof(state->queue[0]));
		state->count--;
		state->dropped++;
	}
	if (toast_is_invite(toast))
	{
		/* (an invitation jumps the queue, behind the ones already waiting that are invitations) */
		for (index = 0; index < state->count && toast_is_invite(&state->queue[index]); index++)
			;
		memmove(&state->queue[index + 1], &state->queue[index], (size_t)(state->count - index) * sizeof(state->queue[0]));
		state->queue[index] = *toast;
		state->count++;
		return;
	}
	state->queue[state->count++] = *toast;
}

int screens_toast_tick(struct screens_toast_state *state, unsigned long now)
{
	if (state->shown && now - state->began >= toast_lifetime(&state->current))
	{
		state->shown = 0;
		state->confirming = 0;
		state->ended = now;
	}
	if (state->confirming && now - state->confirm_began >= 5000)
		state->confirming = 0;
	if (!state->shown && state->count > 0 && (!state->ended || now - state->ended >= SCREENS_TOAST_GAP_MILLISECONDS))
	{
		state->current = state->queue[0];
		memmove(&state->queue[0], &state->queue[1], (size_t)(state->count - 1) * sizeof(state->queue[0]));
		state->count--;
		state->shown = 1;
		state->began = now;
		state->confirming = 0;
	}
	return state->shown;
}

int screens_toast_alpha(struct screens_toast_state const *state, unsigned long now)
{
	unsigned long age = now - state->began, life = toast_lifetime(&state->current);

	if (!state->shown)
		return 0;
	if (age >= life)
		return 0;
	if (age < SCREENS_TOAST_FADE_MILLISECONDS)
		return (int)(age * 255 / SCREENS_TOAST_FADE_MILLISECONDS);
	if (life - age < SCREENS_TOAST_FADE_MILLISECONDS)
		return (int)((life - age) * 255 / SCREENS_TOAST_FADE_MILLISECONDS);
	return 255;
}

int screens_toast_answer(struct screens_toast_state *state, int accept, int in_game, unsigned long now, long *invite_id)
{
	if (!state->shown || !toast_is_invite(&state->current))
		return 0;
	if (accept && in_game && !state->confirming)
	{
		state->confirming = 1;
		state->confirm_began = now;
		return 0;
	}
	if (invite_id)
		*invite_id = state->current.id;
	state->shown = 0;
	state->confirming = 0;
	state->ended = now;
	return 1;
}

/* ---------- the model */

static void model_clear(struct screens_model *model)
{
	memset(model, 0, sizeof(*model));
}

int screens_model_update(struct screens_model *model, int force)
{
	struct screens_provider const *provider = screens_provider();
	unsigned long generation;
	int tab;

	if (!provider)
	{
		int changed = model->valid;

		model_clear(model);
		return changed;
	}
	generation = provider->generation ? provider->generation() : 0;
	if (!force && model->valid && provider->generation && generation == model->generation)
		return 0;
	model_clear(model);
	model->valid = 1;
	model->generation = generation;
	if (provider->roster)
	{
		model->roster_count = provider->roster(model->roster, SCREENS_ROSTER_MAXIMUM, &model->roster_summary);
		if (model->roster_count < 0)
			model->roster_count = 0;
		if (model->roster_count > SCREENS_ROSTER_MAXIMUM)
			model->roster_count = SCREENS_ROSTER_MAXIMUM;
	}
	if (provider->friends)
	{
		for (tab = 0; tab < SCREENS_TABS; tab++)
		{
			if (tab == SCREENS_TAB_PARTY)
				continue;
			model->friends_count[tab] = provider->friends(tab, model->friends[tab], SCREENS_FRIENDS_MAXIMUM, tab == SCREENS_TAB_FRIENDS ? &model->friends_summary : NULL);
			if (model->friends_count[tab] < 0)
				model->friends_count[tab] = 0;
			if (model->friends_count[tab] > SCREENS_FRIENDS_MAXIMUM)
				model->friends_count[tab] = SCREENS_FRIENDS_MAXIMUM;
		}
		screens_friends_sort(model->friends[SCREENS_TAB_FRIENDS], model->friends_count[SCREENS_TAB_FRIENDS]);
		for (tab = 0; tab < SCREENS_TABS; tab++)
			model->friends_summary.counts[tab] = (short)model->friends_count[tab];
	}
	if (provider->party)
	{
		model->party_count = provider->party(model->party, SCREENS_PARTY_MAXIMUM, &model->party_summary);
		if (model->party_count < 0)
			model->party_count = 0;
		if (model->party_count > SCREENS_PARTY_MAXIMUM)
			model->party_count = SCREENS_PARTY_MAXIMUM;
		model->friends_summary.counts[SCREENS_TAB_PARTY] = (short)model->party_count;
	}
	return 1;
}
