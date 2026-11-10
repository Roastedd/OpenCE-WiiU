/*
MENU_SCREENS.INL

The social screens' menu functions (tools/port_settings.py's social_files, included by menu_functions.c): the Players page, the actions of a row, the Friends screen with its tabs,
ADD FRIEND, and the lobby's tags and keys. They fill the rows' texts and colours from screens.h's provider (through the model, rebuilt only when the provider says it changed), run
the provider's actions, and say what it answered in plain words. With screens.enable 0 none of these functions does anything and the lobby is as it was.

The friends layer once a frame (menu_screens_social_frame: presence, the join's hooks and its upkeep) and the Direct Link list a friend's JOIN opens are here too. The friends backend
itself is screens_social_backend.c.

Not here: the toasts (screens_toast.c), the pause menu's PLAYERS button (menu_tags.c), the GamePad's social view (port/wiiu/src/wiiu_social_view.c).
*/

/* the widgets' rows: the name of a row's widget is the prefix and its number from 1 (the lobby's, from 0) */
#define SCR_PLAYER_PREFIX "player_item_"
#define SCR_FRIEND_PREFIX "friend_item_"
#define SCR_ACTION_PREFIX "action_item_"
/* a long text's buffer (a detail line, a head) */
#define SCR_LONG 96

static struct
{
	struct screens_model model;
	/* the Players page */
	short players_first;
	boolean players_fresh;
	short players_last_focus;
	unsigned char player_colors[SCREENS_PLAYER_ROWS][SCREENS_PLAYER_CELLS];
	/* the speaker icon a row has: 0 none, 1 speaking, 2 muted; the row's machine */
	unsigned char player_icons[SCREENS_PLAYER_ROWS];
	/* the lobby's tags' colours (rows 0 up) */
	unsigned char lobby_colors[LOBBY_ROWS];
	/* the Friends screen */
	short friends_tab;
	short friends_first[SCREENS_TABS];
	boolean friends_fresh;
	short friends_last_focus;
	unsigned char friend_colors[SCREENS_FRIEND_ROWS][SCREENS_FRIEND_CELLS];
	unsigned char tab_colors[SCREENS_TABS];
	/* the actions of the row chosen: whose (0 a player, 1 a friend, 2 a party member), the row, the actions, what the provider answered last */
	int actions_kind;
	int actions_tab;
	struct screens_roster_row actions_player;
	struct screens_friend_row actions_friend;
	struct screens_party_member actions_member;
	int actions[SCREENS_ACTIONS_MAXIMUM + 1];
	int actions_count;
	char actions_title[SCREENS_LINE_SIZE];
	char actions_answer[SCREENS_LINE_SIZE];
	/* ADD FRIEND */
	char add_text[32];
	char add_answer[SCREENS_LINE_SIZE];
	/* the log's lines are a few, not a line a frame */
	unsigned long last_logged;
} scr;

/* ---------- helpers: rows named by a prefix */

static short scr_row_index(struct widget_instance const *row, char const *prefix)
{
	size_t length = strlen(prefix);

	return row && row->name && !strncmp(row->name, prefix, length) ? (short)(atoi(row->name + length) - 1) : NONE;
}

/* the row of the list that has the focus (its number from 0 among those named by the prefix), else NONE (a tab, a button) */
static short scr_focused(struct widget_instance *list, char const *prefix)
{
	return list->focused_child ? scr_row_index(list->focused_child, prefix) : NONE;
}

/* the focus put on a row (NONE: the last child, the buttons' bar) */
static void scr_focus(struct widget_instance *list, char const *prefix, short row)
{
	struct widget_instance *child, *last = NULL;
	short index = 0;

	for (child = list->child; child; child = child->next, index++)
	{
		last = child;
		if (row != NONE && scr_row_index(child, prefix) == row)
		{
			list->focused_child = child;
			list->parameters.list.selected_index = index;
			return;
		}
	}
	if (last && row == NONE)
	{
		list->focused_child = last;
		list->parameters.list.selected_index = (short)(index - 1);
	}
}

/* as list_scroll: a list of more rows than shown, the row kept off its ends while there are more past them; the item chosen, or NONE */
static short scr_scroll(struct widget_instance *list, char const *prefix, short *first, short count, short rows)
{
	short row = scr_focused(list, prefix);

	if (row == rows - 1 && *first + rows < count && list_scrolls_at_end())
	{
		(*first)++;
		scr_focus(list, prefix, --row);
	}
	else if (row == 0 && *first > 0 && list_scrolls_at_end())
	{
		(*first)--;
		scr_focus(list, prefix, ++row);
	}
	return row == NONE || row >= rows ? NONE : (short)(*first + row);
}

/* the focus off a row that is not shown (the list got shorter): the last shown, else the buttons */
static void scr_focus_clamp(struct widget_instance *list, char const *prefix, short shown)
{
	short row = scr_focused(list, prefix);

	if (row != NONE && row >= shown)
		scr_focus(list, prefix, shown > 0 ? (short)(shown - 1) : NONE);
}

static void scr_text(struct widget_instance *parent, char const *name, char const *text)
{
	wchar_t wide[SCR_LONG];
	struct widget_instance *widget = named(parent, name, 0);

	if (!widget)
		return;
	text_to_wide(text ? text : "", wide, SCR_LONG);
	text_set_length(widget, wide, SCR_LONG);
}

/* a sentence broken into the two lines a detail line, an answer, has (screens_wrap: the engine's text box cuts what is wider than itself, so a sentence is never given to one as it is) */
static void scr_sentence(char *text, int size)
{
	char whole[SCR_LONG];

	snprintf(whole, sizeof(whole), "%s", text);
	screens_wrap(SCREENS_FONT_LARGE, (float)SCREENS_LINE_BOX_WIDTH, SCREENS_LINE_BOX_LINES, whole, text, size);
}

static boolean scr_enabled(void)
{
	return screens_enabled() != 0;
}

static struct screens_provider const *scr_provider(void);

/* the line in the log for what a screen shows now (a few, at the screen's start and on a change: the scripted runs read them) */
static void scr_log(char const *format, ...) __attribute__((format(printf, 1, 2)));
static void scr_log(char const *format, ...)
{
	char line[200];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	platform_log("screens: %s", line);
}

/* ---------- the provider for screens.enable 2: the game's own players (the lobby's and the game's), bots tagged by their machine; mute and drop a bot work, the rest is the friends
backend's if it set one (screens_backend), else it says so. The mock for 1 is screens_mock.c's. */

static struct screens_provider scr_game_table;
/* the backend the table was last made from (a friends service that comes up after the first screen is taken in then) */
static struct screens_provider const *scr_table_backend;
/* the map of the game the players are of (the friends' presence names it: a friend in another map is not the player of the same name here) */
static char scr_map_name[SCREENS_WORD_SIZE];

static boolean scr_game_players(struct network_game **game, short *state)
{
	void *client = global_network_game_client_get();
	short data;

	*game = client ? network_game_client_get_game(client) : NULL;
	*state = client ? network_game_client_get_state(client, &data) : NONE;
	if (*game)
		snprintf(scr_map_name, sizeof(scr_map_name), "%s", tag_name_strip_path((*game)->map.name));
	return *game != NULL && *state >= _client_state_pregame;
}

static void scr_game_row(struct network_player const *player, int me_machine, struct screens_roster_row *row)
{
	boolean bot = bots_machine_is_bot(player->machine_index) != 0;

	memset(row, 0, sizeof(*row));
	text_safe_name(row->name, (short)sizeof(row->name), player->name, NUMBEROF(player->name), "PLAYER");
	row->team = (signed char)((player->team_index == 0 || player->team_index == 1) ? player->team_index : SCREENS_TEAM_NONE);
	/* (each bot's own level: bots_machine_skill is NONE for a person) */
	row->skill = (signed char)(bot ? (bots_machine_skill(player->machine_index) >= 0 ? bots_machine_skill(player->machine_index) : bots_skill()) : -1);
	row->flags = (unsigned short)((bot ? SCREENS_ROW_BOT : 0) | (player->machine_index == me_machine && !bot ? SCREENS_ROW_ME : 0) |
		(network_voice_machine_speaking(player->machine_index) ? SCREENS_ROW_SPEAKING : 0) |
		(network_voice_machine_muted(player->machine_index) ? SCREENS_ROW_MUTED : 0));
	/* (a friend or a party member of the same name, when the friends layer is on: a hint by name, the game does not know a player's code) */
	if (!bot && player->machine_index != me_machine && social_is_enabled())
		row->flags |= screens_social_roster_flags(row->name, scr_map_name);
	row->ping = -1;
	row->score = -1;
	row->machine = player->machine_index;
}

static unsigned long scr_game_generation(void)
{
	/* (the game's players change without a counter of ours: four looks a second) */
	return system_milliseconds() / 250;
}

/* ... and with a friends backend, its changes too (it counts them) */
static unsigned long scr_game_generation_with_backend(void)
{
	struct screens_provider const *backend = screens_backend();

	return scr_game_generation() * 2654435761ul + (backend && backend->generation ? backend->generation() : 0);
}

static int scr_game_roster(struct screens_roster_row *rows, int maximum, struct screens_roster_summary *summary)
{
	struct network_game *game;
	short state, index;
	int count = 0, bots = 0, me = network_game_client_get_local_machine_index();
	void *server = global_network_game_server_get();

	if (summary)
		memset(summary, 0, sizeof(*summary));
	if (!scr_game_players(&game, &state))
		return 0;
	for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS && count < maximum; index++)
	{
		if (!network_player_is_valid(&game->players[index]))
			continue;
		scr_game_row(&game->players[index], me, &rows[count]);
		/* (the host's own player is the one on the game's server machine: this machine's, when it hosts) */
		if (server && (rows[count].flags & SCREENS_ROW_ME))
			rows[count].flags |= SCREENS_ROW_HOST;
		bots += (rows[count].flags & SCREENS_ROW_BOT) != 0;
		count++;
	}
	if (summary)
	{
		wchar_t const *kind = game->variant.human_readable_game_description;

		summary->players = (short)count;
		summary->bots = (short)bots;
		summary->maximum = game->maximum_players;
		summary->skill = bots_skill();
		summary->bots_wanted = (short)bots_wanted_count();
		summary->host = server != NULL;
		summary->teams = game->variant.universal_variant.teams != 0;
		summary->running = state > _client_state_pregame;
		snprintf(summary->map, sizeof(summary->map), "%s", tag_name_strip_path(game->map.name));
		text_safe_name(summary->gametype, (short)sizeof(summary->gametype), kind, NUMBEROF(game->variant.human_readable_game_description), "");
	}
	return count;
}

static void scr_answer(char *answer, int size, char const *text)
{
	if (answer && size > 0)
		snprintf(answer, (size_t)size, "%s", text);
}

/* A bot is added or dropped where the game's own rule lets it: in the pregame lobby of a game on this machine the setting is changed (bots.count: the lobby of a local Slayer
game makes as many as it says, network_server_manager.c), and in a game this machine hosts for others (bots.online on: the bots stream's host calls, which the lobby code does at
its next idle). A game someone else hosts, or a hosted one with bots.online off, answers in words. skill NONE: bots.skill's; team NONE: the smaller. */
static boolean scr_game_bots_hosted(void)
{
	return bots_online_enabled() && global_network_game_server_get() && !network_game_is_splitscreen_local();
}

static int scr_game_bots_change(int delta, int team, int skill, long machine, char *answer, int size)
{
	long wanted = bots_wanted_count(), maximum = config_integer("bots.max");
	struct network_game *game;
	short state, index;
	char value[16];

	if (!scr_game_players(&game, &state) || !global_network_game_server_get())
	{
		scr_answer(answer, size, "BOTS ARE ONLY FOR A LOCAL GAME ON THIS MACHINE, NOT ONLINE PLAY.");
		return 0;
	}
	if (scr_game_bots_hosted() && state >= _client_state_pregame)
	{
		long present = 0, humans = 0, newest = NONE;

		for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; index++)
		{
			if (!network_player_is_valid(&game->players[index]))
				continue;
			if (bots_machine_is_bot(game->players[index].machine_index))
			{
				present++;
				newest = MAX(newest, game->players[index].machine_index);
			}
			else
				humans++;
		}
		/* (no bot named: the newest, the one with the highest machine) */
		if (delta < 0 && machine < 0)
			machine = newest;
		if (delta > 0)
		{
			if (humans + present >= game->maximum_players || present >= MIN(BOTS_MAXIMUM, maximum))
			{
				scr_answer(answer, size, "THE GAME HAS NO ROOM FOR ANOTHER BOT.");
				return 0;
			}
			return bots_host_add(team == SCREENS_TEAM_NONE ? NONE : team, (short)(skill < 0 ? NONE : skill)) ? 1 : 0;
		}
		if (present == 0 || machine < 0 || !bots_host_drop(machine))
		{
			scr_answer(answer, size, "THERE IS NO BOT TO DROP.");
			return 0;
		}
		return 1;
	}
	/* (what is true of bots today: they play in a game on this machine, set in its lobby before it starts; a game this machine hosts for others has none unless bots.online is on) */
	if (!network_game_is_splitscreen_local())
	{
		scr_answer(answer, size, "BOTS ARE NOT USED IN HOSTED OR ONLINE GAMES, ONLY IN A LOCAL GAME.");
		return 0;
	}
	if (state != _client_state_pregame)
	{
		scr_answer(answer, size, "BOTS ARE CHANGED IN THE LOBBY, BEFORE THE GAME STARTS.");
		return 0;
	}
	if (!bots_game_allowed(game->variant.game_engine_index))
	{
		scr_answer(answer, size, "THIS GAME TYPE CANNOT HAVE BOTS YET. SLAYER CAN.");
		return 0;
	}
	if (wanted + delta < 0 || wanted + delta > MIN(BOTS_MAXIMUM, maximum))
	{
		scr_answer(answer, size, delta > 0 ? "THAT IS THE MOST BOTS THERE MAY BE." : "THERE IS NO BOT TO DROP.");
		return 0;
	}
	/* (the new bot's level: the next bot to be made is the one after those wanted now) */
	if (delta > 0 && skill >= 0)
		bots_set_skill(wanted, (short)skill);
	snprintf(value, sizeof(value), "%ld", wanted + delta);
	return config_write("bots.count", value) ? 1 : 0;
}

static int scr_game_bot_add(int team, int skill, char *answer, int size)
{
	return scr_game_bots_change(1, team, skill, NONE, answer, size);
}

static int scr_game_bot_drop(long machine, char *answer, int size)
{
	return scr_game_bots_change(-1, SCREENS_TEAM_NONE, -1, machine, answer, size);
}

/* RAISE SKILL and LOWER SKILL: one step of the bot's own level, now (the host's) */
static int scr_game_bot_skill(struct screens_roster_row const *row, int delta, char *answer, int size)
{
	int skill = (int)row->skill + delta;

	if (!(row->flags & SCREENS_ROW_BOT) || !global_network_game_server_get())
	{
		scr_answer(answer, size, "ONLY THE HOST CAN CHANGE A BOT.");
		return 0;
	}
	if (skill < 0 || skill >= SCREENS_SKILLS)
	{
		scr_answer(answer, size, delta > 0 ? "THAT BOT IS AT THE HIGHEST SKILL." : "THAT BOT IS AT THE LOWEST SKILL.");
		return 0;
	}
	bots_set_skill(row->machine - BOTS_FIRST_MACHINE, (short)skill);
	return 1;
}

/* KICK and BAN: the host's, a person who is not this machine's (the scoreboard's own call) */
static int scr_game_kick(struct screens_roster_row const *row, boolean ban, char *answer, int size)
{
	if (!global_network_game_server_get() || (row->flags & (SCREENS_ROW_BOT | SCREENS_ROW_ME | SCREENS_ROW_LOCAL)))
	{
		scr_answer(answer, size, "ONLY THE HOST CAN DO THAT, TO ANOTHER MACHINE'S PLAYER.");
		return 0;
	}
	if (!network_game_server_kick_machine_of_player(row->machine, ban))
	{
		scr_answer(answer, size, "THAT DID NOT WORK.");
		return 0;
	}
	return 1;
}

static int scr_game_roster_action(int action, struct screens_roster_row const *row, char *answer, int size)
{
	struct screens_provider const *backend = screens_backend();

	switch (action)
	{
	case SCREENS_ACTION_MUTE:
	case SCREENS_ACTION_UNMUTE:
		network_voice_mute_machine(row->machine, action == SCREENS_ACTION_MUTE);
		return 1;
	case SCREENS_ACTION_DROP_BOT:
		return scr_game_bots_change(-1, SCREENS_TEAM_NONE, -1, row->machine, answer, size);
	case SCREENS_ACTION_BOT_SKILL_UP:
	case SCREENS_ACTION_BOT_SKILL_DOWN:
		return scr_game_bot_skill(row, action == SCREENS_ACTION_BOT_SKILL_UP ? 1 : -1, answer, size);
	case SCREENS_ACTION_KICK:
	case SCREENS_ACTION_BAN:
		return scr_game_kick(row, action == SCREENS_ACTION_BAN, answer, size);
	default:
		if (backend && backend->roster_action)
			return backend->roster_action(action, row, answer, size);
		scr_answer(answer, size, "THAT IS NOT AVAILABLE IN THIS BUILD YET.");
		return 0;
	}
}

/* what A on a row offers here: what works (mute, a bot's drop and skill, a person's kick and ban, the party invitation of a friend), and the backend's own list if it has one */
static void scr_game_roster_actions(struct screens_roster_row const *row, struct screens_roster_summary const *summary, int *actions)
{
	struct screens_provider const *backend = screens_backend();
	int all[SCREENS_ACTIONS_MAXIMUM + 1], index, count = 0;

	if (backend && backend->roster_actions)
	{
		backend->roster_actions(row, summary, actions);
		return;
	}
	screens_default_roster_actions(row, summary, all);
	for (index = 0; all[index] != SCREENS_ACTION_NONE; index++)
	{
		int action = all[index];

		if (action == SCREENS_ACTION_MUTE || action == SCREENS_ACTION_UNMUTE || action == SCREENS_ACTION_CANCEL || action == SCREENS_ACTION_BOT_SETTINGS ||
			((action == SCREENS_ACTION_DROP_BOT || action == SCREENS_ACTION_BOT_SKILL_UP || action == SCREENS_ACTION_BOT_SKILL_DOWN) && summary->host) ||
			((action == SCREENS_ACTION_KICK || action == SCREENS_ACTION_BAN) && summary->host && !(row->flags & (SCREENS_ROW_BOT | SCREENS_ROW_ME | SCREENS_ROW_LOCAL))) ||
			(action == SCREENS_ACTION_INVITE_PARTY && (row->flags & SCREENS_ROW_FRIEND) && backend && backend->roster_action))
		{
			actions[count++] = action;
		}
	}
	actions[count] = SCREENS_ACTION_NONE;
}

/* the provider the screens use: for screens.enable 1 the mock's, for 2 the game's roster with the friends backend's rest */
static struct screens_provider const *scr_provider(void)
{
	struct screens_provider const *backend;

	if (screens_enabled() != 2)
		return screens_provider();
	backend = screens_backend();
	/* (made once, and again if a friends service is set after the first screen asked: the layer comes up on its own thread) */
	if (!scr_game_table.name || backend != scr_table_backend)
	{
		memset(&scr_game_table, 0, sizeof(scr_game_table));
		if (backend)
			scr_game_table = *backend;
		scr_table_backend = backend;
		scr_game_table.name = "game";
		scr_game_table.generation = backend && backend->generation ? scr_game_generation_with_backend : scr_game_generation;
		scr_game_table.roster = scr_game_roster;
		scr_game_table.roster_action = scr_game_roster_action;
		scr_game_table.bot_add = scr_game_bot_add;
		scr_game_table.bot_drop = scr_game_bot_drop;
		scr_game_table.roster_actions = scr_game_roster_actions;
		screens_provider_set(&scr_game_table);
		scr_log("provider for screens.enable 2: the game's players%s", backend ? ", friends from the backend" : ", no friends service");
	}
	return &scr_game_table;
}

/* the model brought up to date (the provider made first) */
static void scr_model_update(boolean force)
{
	scr_provider();
	screens_model_update(&scr.model, force);
}

/* ---------- colours of the cells (pc_menu_text_color) */

static boolean scr_text_color(struct widget_instance *widget, real_argb_color *color)
{
	short row;
	int index = -1, shade = -1;
	char const *name = widget->name;

	if (!name || strncmp(name, "scr_", 4) || !scr_enabled() || !widget->parent)
		return FALSE;
	if (!strncmp(name, "scr_player_", 11))
	{
		static char const *const cells[SCREENS_PLAYER_CELLS] = { "name", "tag", "team", "ping" };

		row = scr_row_index(widget->parent, SCR_PLAYER_PREFIX);
		for (index = 0; index < SCREENS_PLAYER_CELLS; index++)
		{
			if (!strcmp(name + 11, cells[index]))
				break;
		}
		if (row >= 0 && row < SCREENS_PLAYER_ROWS && index < SCREENS_PLAYER_CELLS)
			shade = scr.player_colors[row][index];
	}
	else if (!strncmp(name, "scr_friend_", 11))
	{
		static char const *const cells[SCREENS_FRIEND_CELLS] = { "mark", "name", "status" };

		row = scr_row_index(widget->parent, SCR_FRIEND_PREFIX);
		for (index = 0; index < SCREENS_FRIEND_CELLS; index++)
		{
			if (!strcmp(name + 11, cells[index]))
				break;
		}
		if (row >= 0 && row < SCREENS_FRIEND_ROWS && index < SCREENS_FRIEND_CELLS)
			shade = scr.friend_colors[row][index];
	}
	else if (!strcmp(name, "scr_lobby_tag"))
	{
		/* (the lobby's rows are list_item_0 up) */
		row = widget->parent->name && !strncmp(widget->parent->name, "list_item_", 10) ? (short)atoi(widget->parent->name + 10) : NONE;
		if (row >= 0 && row < LOBBY_ROWS)
			shade = scr.lobby_colors[row];
	}
	if (shade < 0)
		return FALSE;
	{
		unsigned long rgb = screens_color_rgb(shade);

		color->red = (real)((rgb >> 16) & 0xFF) / 255.0f;
		color->green = (real)((rgb >> 8) & 0xFF) / 255.0f;
		color->blue = (real)(rgb & 0xFF) / 255.0f;
	}
	return TRUE;
}

/* the speaker beside a Players row's name (menu_functions_text_box_drawn: as the lobby's) */
static boolean scr_text_box_drawn(struct widget_instance *widget, rectangle2d const *bounds)
{
	short row;
	rectangle2d icon, text, cursor;
	short size;

	if (!scr_enabled() || !widget->name || strcmp(widget->name, "scr_player_name") || !widget->parent)
		return FALSE;
	row = scr_row_index(widget->parent, SCR_PLAYER_PREFIX);
	if (row < 0 || row >= SCREENS_PLAYER_ROWS || !scr.player_icons[row])
		return TRUE;
	size = (short)MIN(14, bounds->y1 - bounds->y0);
	icon.x1 = (short)(bounds->x1 - 4);
	icon.y0 = (short)((bounds->y0 + bounds->y1 - size) / 2);
	if (widget->parameters.text_box.text && widget->parameters.text_box.text[0])
	{
		draw_unicode_string_compute_bounds(bounds, widget->parameters.text_box.text, &text, &cursor);
		if (text.x0 - size - 6 >= bounds->x0)
			icon.x1 = (short)(text.x0 - 6);
		icon.y0 = (short)(draw_unicode_string_capital_middle(bounds, widget->parameters.text_box.text) - size / 2);
	}
	icon.x0 = (short)(icon.x1 - size);
	icon.y1 = (short)(icon.y0 + size);
	network_voice_draw_icon(&icon, scr.player_icons[row] == 2, 1.0f);
	return TRUE;
}

/* ---------- the actions of a row */

static void scr_actions_prepare(void)
{
	struct screens_provider const *provider = scr_provider();

	scr.actions[0] = SCREENS_ACTION_NONE;
	if (!provider)
		return;
	if (scr.actions_kind == 0)
	{
		if (provider->roster_actions)
			provider->roster_actions(&scr.actions_player, &scr.model.roster_summary, scr.actions);
		else
			screens_default_roster_actions(&scr.actions_player, &scr.model.roster_summary, scr.actions);
		snprintf(scr.actions_title, sizeof(scr.actions_title), "%s", scr.actions_player.name);
	}
	else if (scr.actions_kind == 1)
	{
		if (provider->friend_actions)
			provider->friend_actions(scr.actions_tab, &scr.actions_friend, scr.actions);
		else
			screens_default_friend_actions(scr.actions_tab, &scr.actions_friend, scr.actions);
		snprintf(scr.actions_title, sizeof(scr.actions_title), "%s", scr.actions_friend.name);
	}
	else
	{
		if (provider->party_actions)
			provider->party_actions(&scr.actions_member, &scr.model.party_summary, scr.actions);
		else
			screens_default_party_actions(&scr.actions_member, &scr.model.party_summary, scr.actions);
		snprintf(scr.actions_title, sizeof(scr.actions_title), "%s", scr.actions_member.name);
	}
	for (scr.actions_count = 0; scr.actions[scr.actions_count] != SCREENS_ACTION_NONE && scr.actions_count < SCREENS_ACTIONS_MAXIMUM; scr.actions_count++)
		;
	scr.actions_answer[0] = 0;
}

/* "port screens actions open" (the screen's created) */
static boolean scr_actions_open(struct widget_instance *screen)
{
	scr_log("actions of %s: %d", scr.actions_title, scr.actions_count);
	{
		short index;
		char line[SCREENS_LINE_SIZE * 2];

		line[0] = 0;
		for (index = 0; index < scr.actions_count; index++)
			snprintf(line + strlen(line), sizeof(line) - strlen(line), "%s%s", index ? " | " : "", screens_action_word(scr.actions[index]));
		scr_log("actions: %s", line);
	}
	(void)screen;
	return TRUE;
}

/* "port screens actions update" */
static void scr_actions_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list), *row;
	char answer[SCR_LONG];

	scr_text(screen, "actions_title", scr.actions_title);
	snprintf(answer, sizeof(answer), "%s", scr.actions_answer);
	scr_sentence(answer, (int)sizeof(answer));
	scr_text(screen, "actions_answer", answer);
	for (row = list->child; row; row = row->next)
	{
		short index = scr_row_index(row, SCR_ACTION_PREFIX);

		if (index == NONE)
			continue;
		row->visible = index < scr.actions_count;
		if (index < scr.actions_count)
			scr_text(row, "scr_action_text", screens_action_word(scr.actions[index]));
	}
	scr_focus_clamp(list, SCR_ACTION_PREFIX, (short)scr.actions_count);
}

/* the words of a refusal, as a message over the screen (a join that failed: plain words, not a code) */
static void scr_say(char const *text)
{
	wchar_t wide[SCREENS_LINE_SIZE];

	text_to_wide(text, wide, SCREENS_LINE_SIZE);
	display_error_text_deferred(wide, NONE);
}

static boolean scr_join_screen_open(struct widget_instance *widget, boolean *widget_deleted);

/* "port screens action run" (an action row's A): the provider does it; with nothing to say the screen goes back, else it stays with the answer */
static boolean scr_action_run(struct widget_instance *row, boolean *widget_deleted)
{
	struct screens_provider const *provider = scr_provider();
	short index = scr_row_index(row, SCR_ACTION_PREFIX);
	int action, done = 0;
	char answer[SCREENS_LINE_SIZE];

	if (index < 0 || index >= scr.actions_count || !provider)
		return campaign_fail();
	action = scr.actions[index];
	answer[0] = 0;
	if (action == SCREENS_ACTION_CANCEL)
	{
		ui_widget_port_go_back(row);
		*widget_deleted = TRUE;
		return TRUE;
	}
	if (action == SCREENS_ACTION_BOT_SETTINGS)
	{
		/* (the bot settings screen is opened by the actions screen's X, as the lobby's map vote is: a press posted) */
		ui_widget_port_post_button(0, BUTTON_X);
		return TRUE;
	}
	if (scr.actions_kind == 0 && provider->roster_action)
		done = provider->roster_action(action, &scr.actions_player, answer, (int)sizeof(answer));
	else if (scr.actions_kind == 1 && provider->friend_action)
		done = provider->friend_action(action, &scr.actions_friend, answer, (int)sizeof(answer));
	else if (scr.actions_kind == 2 && provider->party_action)
		done = provider->party_action(action, &scr.actions_member, answer, (int)sizeof(answer));
	else
		scr_answer(answer, (int)sizeof(answer), "THAT IS NOT AVAILABLE IN THIS BUILD YET.");
	scr_log("action %s on %s: %s %s", screens_action_word(action), scr.actions_title, done ? "done" : "refused", answer);
	scr_model_update(TRUE);
	if (!done)
	{
		/* (a join that failed is told in a message of its own; the others stay on the line under the rows) */
		if (action == SCREENS_ACTION_JOIN_GAME && answer[0])
			scr_say(answer);
		else
			snprintf(scr.actions_answer, sizeof(scr.actions_answer), "%s", answer[0] ? answer : "THAT DID NOT WORK.");
		return campaign_fail();
	}
	if (action == SCREENS_ACTION_JOIN_GAME && scr.actions_kind == 1)
		return scr_join_screen_open(row, widget_deleted);
	if (answer[0])
	{
		snprintf(scr.actions_answer, sizeof(scr.actions_answer), "%s", answer);
		scr_actions_prepare();
		return TRUE;
	}
	ui_widget_port_go_back(row);
	*widget_deleted = TRUE;
	return TRUE;
}

/* ---------- the Players page */

/* "port players open" (the screen's created) */
static boolean scr_players_open(struct widget_instance *screen)
{
	(void)screen;
	scr.players_first = 0;
	scr.players_fresh = TRUE;
	scr.players_last_focus = NONE;
	scr_model_update(TRUE);
	scr_log("players page: %d players, %d bots, provider %s", scr.model.roster_count, scr.model.roster_summary.bots,
		scr_provider() ? scr_provider()->name : "none");
	return TRUE;
}

/* "port players update" */
static void scr_players_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list), *row;
	struct screens_provider const *provider = scr_provider();
	struct screens_roster_summary const *summary = &scr.model.roster_summary;
	short count, item, shown;
	char text[SCR_LONG];
	boolean host;

	scr_model_update(FALSE);
	count = (short)scr.model.roster_count;
	item = scr_scroll(list, SCR_PLAYER_PREFIX, &scr.players_first, count, SCREENS_PLAYER_ROWS);
	if (scr.players_first > MAX(0, count - SCREENS_PLAYER_ROWS))
		scr.players_first = (short)MAX(0, count - SCREENS_PLAYER_ROWS);
	shown = (short)MIN(count, SCREENS_PLAYER_ROWS);
	if (scr.players_fresh)
	{
		scr.players_fresh = FALSE;
		scr_focus(list, SCR_PLAYER_PREFIX, shown > 0 ? 0 : NONE);
	}
	for (row = list->child; row; row = row->next)
	{
		short index = scr_row_index(row, SCR_PLAYER_PREFIX);
		struct screens_cell cells[SCREENS_PLAYER_CELLS];

		if (index == NONE)
			continue;
		row->visible = index < shown;
		scr.player_icons[index] = 0;
		if (index >= shown)
			continue;
		screens_player_cells(&scr.model.roster[scr.players_first + index], summary, cells);
		scr_text(row, "scr_player_name", cells[SCREENS_PLAYER_NAME].text);
		scr_text(row, "scr_player_tag", cells[SCREENS_PLAYER_TAG].text);
		scr_text(row, "scr_player_team", cells[SCREENS_PLAYER_TEAM].text);
		scr_text(row, "scr_player_ping", cells[SCREENS_PLAYER_PING].text);
		memcpy(scr.player_colors[index], (unsigned char[SCREENS_PLAYER_CELLS]){ cells[0].color, cells[1].color, cells[2].color, cells[3].color }, SCREENS_PLAYER_CELLS);
		scr.player_icons[index] = (scr.model.roster[scr.players_first + index].flags & SCREENS_ROW_MUTED) ? 2 :
			(scr.model.roster[scr.players_first + index].flags & SCREENS_ROW_SPEAKING) ? 1 : 0;
	}
	scr_focus_clamp(list, SCR_PLAYER_PREFIX, shown);
	if (!shown && scr_focused(list, SCR_PLAYER_PREFIX) != NONE)
		scr_focus(list, SCR_PLAYER_PREFIX, NONE);
	screens_player_head(summary, text, (int)sizeof(text));
	scr_text(screen, "players_head", text);
	scr_text(screen, "players_banner", summary->running ? "THE GAME KEEPS RUNNING WHILE THIS IS OPEN." : "");
	item = scr_focused(list, SCR_PLAYER_PREFIX);
	if (!provider || !count)
		screens_empty_wrapped(0, 0, NULL, provider != NULL, text, (int)sizeof(text));
	else if (item != NONE && scr.players_first + item < count)
		screens_player_detail(&scr.model.roster[scr.players_first + item], summary, text, (int)sizeof(text));
	else
		text[0] = 0;
	scr_text(screen, "players_detail", text);
	if (item != scr.players_last_focus)
	{
		scr.players_last_focus = item;
		if (item != NONE && scr.players_first + item < count)
			scr_log("players focus row %d: %s", scr.players_first + item, scr.model.roster[scr.players_first + item].name);
		else
			scr_log("players focus: buttons or tabs");
	}
	/* the host's buttons only for the host, and only if the provider can */
	host = summary->host != 0;
	visible_set(named(list, "players_button_bots", 0), TRUE);
	visible_set(named(list, "players_button_add", 0), host && provider && provider->bot_add);
	visible_set(named(list, "players_button_drop", 0), host && provider && provider->bot_drop);
	visible_set(named(list, "players_button_friends", 0), provider && provider->friends);
	focus_off_hidden(named(list, "players_button_bar", 0));
}

/* "port players row" (a row's A): its actions made, and the actions screen opens if it has any besides CANCEL */
static boolean scr_players_row(struct widget_instance *row)
{
	short index = scr_row_index(row, SCR_PLAYER_PREFIX), at = (short)(scr.players_first + index);

	if (!scr_enabled() || index < 0 || at >= scr.model.roster_count)
		return campaign_fail();
	scr.actions_kind = 0;
	scr.actions_player = scr.model.roster[at];
	scr_actions_prepare();
	return scr.actions_count > 1 ? TRUE : campaign_fail();
}

/* ADD BOT and DROP BOT, the buttons and the shoulders (the host's) */
static boolean scr_bot_change(boolean add)
{
	struct screens_provider const *provider = scr_provider();
	char answer[SCREENS_LINE_SIZE];
	int done;

	answer[0] = 0;
	if (!scr_enabled() || !provider || !(add ? provider->bot_add : provider->bot_drop) || !scr.model.roster_summary.host)
		return campaign_fail();
	done = add ? provider->bot_add(SCREENS_TEAM_NONE, -1, answer, (int)sizeof(answer)) : provider->bot_drop(-1, answer, (int)sizeof(answer));
	scr_log("%s a bot: %s %s", add ? "adding" : "dropping", done ? "done" : "refused", answer);
	scr_model_update(TRUE);
	if (!done)
	{
		scr_say(answer[0] ? answer : "THAT DID NOT WORK.");
		return campaign_fail();
	}
	return TRUE;
}

/* ---------- the Friends screen */

static void scr_friends_cells_for_row(short tab, short at, struct screens_cell *cells)
{
	if (tab == SCREENS_TAB_PARTY)
		screens_member_cells(&scr.model.party[at], cells);
	else
		screens_friend_cells(tab, &scr.model.friends[tab][at], cells);
}

static short scr_friends_count(short tab)
{
	return (short)(tab == SCREENS_TAB_PARTY ? scr.model.party_count : scr.model.friends_count[tab]);
}

/* "port friends open" */
static boolean scr_friends_open(struct widget_instance *screen)
{
	(void)screen;
	scr.friends_tab = SCREENS_TAB_FRIENDS;
	memset(scr.friends_first, 0, sizeof(scr.friends_first));
	scr.friends_fresh = TRUE;
	scr.friends_last_focus = NONE;
	scr_model_update(TRUE);
	scr_log("friends screen: %d friends, %d requests, %d in the party, %d blocked, service %s", scr.model.friends_count[0], scr.model.friends_count[1],
		scr.model.party_count, scr.model.friends_count[3], screens_service_word(scr.model.friends_summary.service));
	return TRUE;
}

static void scr_friends_tab_set(struct widget_instance *widget, short tab)
{
	struct widget_instance *list = named(screen_of(widget), "friends_list", 0);

	tab = (short)((tab + SCREENS_TABS) % SCREENS_TABS);
	if (tab == scr.friends_tab)
		return;
	scr.friends_tab = tab;
	scr.friends_first[tab] = 0;
	scr.friends_last_focus = NONE;
	scr_log("friends tab %s", screens_tab_word(tab));
	if (list)
		scr_focus(list, SCR_FRIEND_PREFIX, scr_friends_count(tab) > 0 ? 0 : NONE);
}

/* "port friends tab N | previous | next" */
static boolean scr_friends_tab(struct widget_instance *widget, char const *which)
{
	if (!scr_enabled())
		return campaign_fail();
	if (!strcmp(which, "previous"))
		scr_friends_tab_set(widget, (short)(scr.friends_tab - 1));
	else if (!strcmp(which, "next"))
		scr_friends_tab_set(widget, (short)(scr.friends_tab + 1));
	else
		scr_friends_tab_set(widget, (short)atoi(which));
	return TRUE;
}

/* the four tabs' words ("[FRIENDS 12]", "REQUESTS 3", ...). A tab is a text widget with no words of its own: its text is set in it by its name. (The first version
asked the tab itself for a widget of that name, which has none inside it, and no tab had a word on the console.) */
static void scr_friends_tab_labels(struct widget_instance *list, short tab, struct screens_friends_summary const *summary, struct screens_party_summary const *party)
{
	short index;
	char name[24], text[SCR_LONG];

	for (index = 0; index < SCREENS_TABS; index++)
	{
		snprintf(name, sizeof(name), "friends_tab_%d", index);
		screens_tab_label(index, index == tab, summary, party, text, (int)sizeof(text));
		scr_text(list, name, text);
	}
}

/* "port friends update" */
static void scr_friends_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list), *row;
	struct screens_provider const *provider = scr_provider();
	struct screens_friends_summary const *summary = &scr.model.friends_summary;
	short tab = scr.friends_tab, count, shown, item, index;
	char text[SCR_LONG];

	scr_model_update(FALSE);
	count = scr_friends_count(tab);
	item = scr_scroll(list, SCR_FRIEND_PREFIX, &scr.friends_first[tab], count, SCREENS_FRIEND_ROWS);
	if (scr.friends_first[tab] > MAX(0, count - SCREENS_FRIEND_ROWS))
		scr.friends_first[tab] = (short)MAX(0, count - SCREENS_FRIEND_ROWS);
	shown = (short)MIN(count, SCREENS_FRIEND_ROWS);
	if (scr.friends_fresh)
	{
		scr.friends_fresh = FALSE;
		scr_focus(list, SCR_FRIEND_PREFIX, shown > 0 ? 0 : NONE);
	}
	for (row = list->child; row; row = row->next)
	{
		struct screens_cell cells[SCREENS_FRIEND_CELLS];

		index = scr_row_index(row, SCR_FRIEND_PREFIX);
		if (index == NONE)
			continue;
		row->visible = index < shown;
		if (index >= shown)
			continue;
		scr_friends_cells_for_row(tab, (short)(scr.friends_first[tab] + index), cells);
		scr_text(row, "scr_friend_mark", cells[SCREENS_FRIEND_MARK].text);
		scr_text(row, "scr_friend_name", cells[SCREENS_FRIEND_NAME].text);
		scr_text(row, "scr_friend_status", cells[SCREENS_FRIEND_STATUS].text);
		for (item = 0; item < SCREENS_FRIEND_CELLS; item++)
			scr.friend_colors[index][item] = cells[item].color;
	}
	scr_focus_clamp(list, SCR_FRIEND_PREFIX, shown);
	scr_friends_tab_labels(list, tab, summary, &scr.model.party_summary);
	snprintf(text, sizeof(text), "%s", provider && provider->friends ? screens_service_word(summary->service) : "NO FRIEND SERVICE");
	scr_text(screen, "friends_service", text);
	if (summary->own_code[0])
		snprintf(text, sizeof(text), "YOUR CODE: %s", summary->own_code);
	else
		text[0] = 0;
	scr_text(screen, "friends_code", text);
	item = scr_focused(list, SCR_FRIEND_PREFIX);
	if (!provider || !provider->friends || !count)
		screens_empty_wrapped(1, tab, summary, provider && provider->friends, text, (int)sizeof(text));
	else if (tab == SCREENS_TAB_PARTY)
		screens_party_head(&scr.model.party_summary, text, (int)sizeof(text));
	else if (item != NONE && scr.friends_first[tab] + item < count)
		screens_friend_detail(tab, &scr.model.friends[tab][scr.friends_first[tab] + item], summary, text, (int)sizeof(text));
	else
		text[0] = 0;
	/* (a join the player asked for: its progress, or why it failed, in the place of the row's details) */
	{
		char join_line[SCR_LONG];

		if (scr_enabled() && screens_social_join_line(join_line, (int)sizeof(join_line)))
		{
			snprintf(text, sizeof(text), "%s", join_line);
			scr_sentence(text, (int)sizeof(text));
		}
	}
	scr_text(screen, "friends_detail", text);
	if (item != scr.friends_last_focus)
	{
		scr.friends_last_focus = item;
		scr_log("friends focus %s", item == NONE ? "tabs or buttons" : "a row");
	}
	/* the buttons that apply to the row chosen */
	{
		struct screens_friend_row const *friend_row = tab != SCREENS_TAB_PARTY && item != NONE && scr.friends_first[tab] + item < count ?
			&scr.model.friends[tab][scr.friends_first[tab] + item] : NULL;
		boolean joinable = friend_row && tab == SCREENS_TAB_FRIENDS && (friend_row->presence == SCREENS_PRESENCE_PLAYING || friend_row->presence == SCREENS_PRESENCE_LOBBY) &&
			(friend_row->join == SCREENS_JOIN_OPEN || friend_row->join == SCREENS_JOIN_DOWNLOAD);
		boolean online = friend_row && tab == SCREENS_TAB_FRIENDS && friend_row->presence != SCREENS_PRESENCE_OFFLINE;

		visible_set(named(list, "friends_button_add", 0), provider && provider->friend_add);
		visible_set(named(list, "friends_button_join", 0), joinable && provider && provider->friend_action);
		visible_set(named(list, "friends_button_invite", 0), online && provider && provider->friend_action);
		focus_off_hidden(named(list, "friends_button_bar", 0));
	}
}

/* "port friends row" */
static boolean scr_friends_row(struct widget_instance *row)
{
	short index = scr_row_index(row, SCR_FRIEND_PREFIX), at;

	if (!scr_enabled() || index < 0)
		return campaign_fail();
	at = (short)(scr.friends_first[scr.friends_tab] + index);
	if (at >= scr_friends_count(scr.friends_tab))
		return campaign_fail();
	if (scr.friends_tab == SCREENS_TAB_PARTY)
	{
		scr.actions_kind = 2;
		scr.actions_member = scr.model.party[at];
	}
	else
	{
		scr.actions_kind = 1;
		scr.actions_tab = scr.friends_tab;
		scr.actions_friend = scr.model.friends[scr.friends_tab][at];
	}
	scr_actions_prepare();
	return scr.actions_count > 1 ? TRUE : campaign_fail();
}

/* A friend's game is joined by the game's own way for a game that is found by its invitation: the Direct Link list (the browser's, with the games reached through the tunnel). The
friends layer has the host reached (social_join_friend); the player presses A on the game when it is in the list. The menus' own join (ui_widget_port_join) is theirs alone. */
#define SCR_JOIN_NAME "pc\\main_menu\\multiplayer_type_select\\join_game\\join_game_screen"

static boolean scr_join_screen_open(struct widget_instance *widget, boolean *widget_deleted)
{
	multiplayer.mode = _multiplayer_mode_direct_link;
	scr_log("a friend's game is joining: the Direct Link list opens");
	if (!ui_widget_port_open(widget, SCR_JOIN_NAME, widget_deleted))
		return campaign_fail();
	return TRUE;
}

/* the Direct Link list's line while the game is not in it yet (lobby_browser_update's ticker): what the join is doing */
static boolean scr_join_ticker(wchar_t *text, int size)
{
	char line[SCR_LONG];

	if (!scr_enabled() || !screens_social_join_line(line, (int)sizeof(line)))
		return FALSE;
	text_to_wide(line, text, size);
	return TRUE;
}

/* "port friends join" and "port friends invite" (the buttons): the row chosen's action without the list */
static boolean scr_friends_button(struct widget_instance *button, int action, boolean *widget_deleted)
{
	struct widget_instance *list = named(screen_of(button), "friends_list", 0);
	struct screens_provider const *provider = scr_provider();
	short item = list ? scr_focused(list, SCR_FRIEND_PREFIX) : NONE, at;
	char answer[SCREENS_LINE_SIZE];
	int done;

	answer[0] = 0;
	if (!scr_enabled() || !provider || !provider->friend_action || item == NONE || scr.friends_tab == SCREENS_TAB_PARTY)
		return campaign_fail();
	at = (short)(scr.friends_first[scr.friends_tab] + item);
	if (at >= scr_friends_count(scr.friends_tab))
		return campaign_fail();
	if (action == SCREENS_ACTION_INVITE_GAME && !network_game_is_active())
		action = SCREENS_ACTION_INVITE_PARTY;
	done = provider->friend_action(action, &scr.model.friends[scr.friends_tab][at], answer, (int)sizeof(answer));
	scr_log("%s %s: %s %s", screens_action_word(action), scr.model.friends[scr.friends_tab][at].name, done ? "done" : "refused", answer);
	scr_model_update(TRUE);
	if (!done)
	{
		if (answer[0])
			scr_say(answer);
		return campaign_fail();
	}
	if (action == SCREENS_ACTION_JOIN_GAME)
		return scr_join_screen_open(button, widget_deleted);
	if (answer[0])
		scr_say(answer);
	return TRUE;
}

/* ---------- ADD FRIEND */

static void scr_add_done(char const *text)
{
	snprintf(scr.add_text, sizeof(scr.add_text), "%s", text);
}

static void scr_add_check(char *text, int size)
{
	char normalised[TEXT_SAFE_CODE_LENGTH + 4];
	short length;

	if (!scr.add_text[0])
		snprintf(text, (size_t)size, "TYPE YOUR FRIEND'S CODE: 13 LETTERS AND NUMBERS.");
	else if ((length = text_safe_code_normalise(normalised, (short)sizeof(normalised), scr.add_text)) < 0)
		snprintf(text, (size_t)size, "NOT A CODE: CODES HAVE NO I, L, O OR U.");
	else if (length < TEXT_SAFE_CODE_LENGTH)
		snprintf(text, (size_t)size, "KEEP TYPING: %d OF %d.", length, TEXT_SAFE_CODE_LENGTH);
	else
		snprintf(text, (size_t)size, "THAT LOOKS LIKE A CODE. SEND REQUEST.");
}

/* "port friend add init" */
static boolean scr_add_init(struct widget_instance *screen)
{
	(void)screen;
	scr.add_text[0] = 0;
	scr.add_answer[0] = 0;
	scr_model_update(FALSE);
	return TRUE;
}

/* "port friend add update" */
static void scr_add_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list), *row = named(list, "op_code", 0);
	char text[SCR_LONG];

	scr_text(screen, "add_own_code", scr.model.friends_summary.own_code[0] ? scr.model.friends_summary.own_code : "(NO CODE YET)");
	text_field_show(named(list, "code_value", 0), scr.add_text, text_field_editing(row));
	if (scr.add_answer[0])
		snprintf(text, sizeof(text), "%s", scr.add_answer);
	else
		scr_add_check(text, (int)sizeof(text));
	scr_sentence(text, (int)sizeof(text));
	scr_text(screen, "add_answer", text);
}

/* "port friend add edit" (the code's row's A) */
static boolean scr_add_edit(struct widget_instance *row)
{
	if (text_field_editing(row))
	{
		text_field_end(TRUE);
		return TRUE;
	}
	scr.add_answer[0] = 0;
	text_field_begin(row, scr.add_text, 24, scr_add_done);
	return TRUE;
}

/* "port friend add send" */
static boolean scr_add_send(struct widget_instance *button)
{
	struct screens_provider const *provider = scr_provider();
	char answer[SCREENS_LINE_SIZE];
	int done;

	if (text_field_editing(NULL))
		text_field_end(TRUE);
	if (!provider || !provider->friend_add)
		return campaign_fail();
	answer[0] = 0;
	done = provider->friend_add(scr.add_text, answer, (int)sizeof(answer));
	scr_log("friend code sent: %s %s", done ? "done" : "refused", answer);
	snprintf(scr.add_answer, sizeof(scr.add_answer), "%s", answer[0] ? answer : done ? "REQUEST SENT." : "THAT DID NOT WORK.");
	if (done)
		scr.add_text[0] = 0;
	scr_model_update(TRUE);
	(void)button;
	return done ? TRUE : campaign_fail();
}

/* "port friend add back" */
static boolean scr_add_back(struct widget_instance *widget, boolean *widget_deleted)
{
	if (text_field_editing(NULL))
		text_field_end(FALSE);
	scr.add_text[0] = 0;
	ui_widget_port_go_back(widget);
	*widget_deleted = TRUE;
	return TRUE;
}

/* ---------- the lobby's tags, and its keys (A on a row, L and R for the bots) */

/* what the lobby's row for a network player says as the screens say it (the same cells: a team word and a bot's tag) */
static void scr_lobby_cells(struct network_player const *player, boolean teams, struct screens_cell *cells)
{
	struct screens_roster_row row;

	scr_game_row(player, network_game_client_get_local_machine_index(), &row);
	screens_lobby_cells(&row, teams, cells);
}

/* the lobby rows' name and tag (lobby_update): the name in a text of its own (and the row's centred text emptied), the tag of the team and a bot's skill at the right */
static void scr_lobby_update(struct widget_instance *list, short first, short count, boolean teams)
{
	struct widget_instance *row = list->child;
	short index = 0;

	if (!scr_enabled())
		return;
	for (; row && index < LOBBY_ROWS; row = row->next, index++)
	{
		struct screens_cell cells[SCREENS_LOBBY_CELLS];
		struct widget_instance *tag, *name;
		char line[SCREENS_LINE_SIZE];

		if (strncmp(row->name, "list_item_", 10))
			break;
		tag = named(row, "scr_lobby_tag", 0);
		name = named(row, "scr_lobby_name", 0);
		visible_set(tag, index < count);
		visible_set(name, index < count);
		if (index >= count)
			continue;
		scr_lobby_cells(lobby_players[first + index], teams, cells);
		/* (a second to fourth local player: its controller, as the row's text had it) */
		snprintf(line, sizeof(line), "%s", cells[SCREENS_LOBBY_NAME].text);
		if (lobby_local_player_count() >= 2 && (short)lobby_players[first + index]->machine_index == network_game_client_get_local_machine_index())
		{
			snprintf(line + strlen(line), sizeof(line) - strlen(line), " [P%d]", lobby_players[first + index]->controller_index + 1);
			screens_fit(SCREENS_FONT_LARGE, (float)SCREENS_LOBBY_NAME_WIDTH, line, cells[SCREENS_LOBBY_NAME].text, (int)sizeof(cells[SCREENS_LOBBY_NAME].text));
			snprintf(line, sizeof(line), "%s", cells[SCREENS_LOBBY_NAME].text);
		}
		scr_text(row, "scr_lobby_name", line);
		text_set(named(row, "list_item_text", 0), L"");
		scr_text(row, "scr_lobby_tag", cells[SCREENS_LOBBY_TAG].text);
		scr.lobby_colors[index] = cells[SCREENS_LOBBY_TAG].color;
	}
}

/* "port lobby row" (a lobby row's A): the same actions as the Players page, for the player of the row */
static boolean scr_lobby_row(struct widget_instance *row)
{
	short index = (short)atoi(row->name + 10), at = (short)(multiplayer.lobby_first + index);
	struct network_game *game;
	short state;

	if (!scr_enabled())
		return FALSE;
	if (strncmp(row->name, "list_item_", 10) || at >= lobby_player_count || !scr_game_players(&game, &state))
		return campaign_fail();
	scr_model_update(TRUE);
	scr.actions_kind = 0;
	scr_game_row(lobby_players[at], network_game_client_get_local_machine_index(), &scr.actions_player);
	if (global_network_game_server_get() && (scr.actions_player.flags & SCREENS_ROW_ME))
		scr.actions_player.flags |= SCREENS_ROW_HOST;
	scr.model.roster_summary.host = global_network_game_server_get() != NULL;
	scr.model.roster_summary.teams = game->variant.universal_variant.teams != 0;
	scr_actions_prepare();
	return scr.actions_count > 1 ? TRUE : campaign_fail();
}

/* "port lobby bot add" and "port lobby bot drop" (R and L in the lobby: the host's) */
static boolean scr_lobby_bot(boolean add)
{
	if (!scr_enabled())
		return FALSE;
	scr_model_update(TRUE);
	if (!scr.model.roster_summary.host)
		scr.model.roster_summary.host = global_network_game_server_get() != NULL;
	return scr_bot_change(add);
}

/* the lobby's line under its rows says what the new keys are (lobby_join_help's text box, when the screens are on and nobody else is joining) */
static wchar_t const *scr_lobby_help(wchar_t const *text)
{
	return scr_enabled() && !text[0] ? L"A: PLAYER OPTIONS   L/R: BOTS" : text;
}

/* ---------- the friends layer, once a frame (screens_toast.c's call: before its own check of screens.enable, so that presence is right with the screens off) */

void p2p_identifier_from_hash(unsigned char const *hash, unsigned char *identifier);

/* this machine is in a game of any kind, or hosts or has joined a lobby: a join is not started over it */
static int scr_game_busy(void)
{
	void *client = global_network_game_client_get();
	short data;

	return game_in_progress() || global_network_game_server_get() != NULL || (client && network_game_client_get_state(client, &data) >= _client_state_pregame);
}

/* the host's game is in the client's list of games found (lobby_browser_joined_game's test, for the host of the key hash) */
static int scr_game_ready(unsigned char const *host_hash)
{
	void *client = global_network_game_client_get();
	struct advertised_game *games = client ? network_game_client_get_available_games(client) : NULL;
	unsigned char identifier[6];
	short index;

	p2p_identifier_from_hash(host_hash, identifier);
	for (index = 0; games && index < MAXIMUM_ADVERTISED_GAMES; index++)
	{
		if (network_game_client_advertised_game_is_valid(&games[index]) && !memcmp(games[index].xnaddr + 2, identifier, sizeof(identifier)))
			return 1;
	}
	return 0;
}

static unsigned long scr_social_clock(void)
{
	return system_milliseconds();
}

static int scr_social_version(void)
{
	return HALO_PORT_NETWORK_VERSION;
}

/* what friends see this machine doing. A split screen game (a game on this machine, with or without bots) has a server and is in "network server" connection as any hosted game is,
but takes no one else: it is SOLO, in its lobby and in play, never a lobby or a game to join (on the console it was told to friends as a joinable game, "bloodgulch 4/128") */
static int scr_social_activity(void)
{
	void *client = global_network_game_client_get();
	short data;

	if (network_game_is_splitscreen_local())
		return SOCIAL_ACTIVITY_SOLO;
	if (game_in_progress())
		return game_connection() == _game_connection_local ? SOCIAL_ACTIVITY_SOLO : SOCIAL_ACTIVITY_GAME;
	if (global_network_game_server_get() || (client && network_game_client_get_state(client, &data) >= _client_state_pregame))
		return SOCIAL_ACTIVITY_LOBBY;
	return SOCIAL_ACTIVITY_MENUS;
}

void menu_screens_social_frame(void)
{
	static boolean started;
	static int told_activity = -1;
	static unsigned long name_checked, ready_since, failed_since;
	static long told_profile = NONE;
	static char told_name[SCREENS_NAME_SIZE];
	struct social_join_status status;
	unsigned long now;
	int activity;

	if (!social_is_enabled())
		return;
	now = system_milliseconds();
	if (!started)
	{
		struct screens_social_hooks hooks = { scr_social_clock, scr_social_version };

		started = TRUE;
		/* (what only the game knows: the maps, the list of games found, whether this machine is in a game) */
		social_join_set_game_hooks(lobby_browser_map_state, scr_game_ready, scr_game_busy);
		screens_social_backend_hooks(&hooks);
		if (screens_enabled() == 2)
		{
			screens_backend_set(screens_social_backend());
			scr_log("friends are on: the screens take their friends, party and toasts from the layer");
		}
		else
			scr_log("friends are on (screens.enable %d: no friends screens)", screens_enabled());
	}
	activity = scr_social_activity();
	if (activity != told_activity)
	{
		told_activity = activity;
		social_set_activity(activity);
	}
	/* (the first profile's name, unless social.name says another. The profile is read from the card: when the player's profile is another one than last time, else every half minute in
	the menus, and never in a game once it has been read, so that no frame waits for the card) */
	if (!config_string("social.name")[0])
	{
		long profile_index = player_ui_get_active_player_profile_index(0);

		if (profile_index != NONE && (profile_index != told_profile || (!game_in_progress() && now - name_checked >= 30000) || !name_checked))
		{
			struct player_profile profile;

			name_checked = now ? now : 1;
			told_profile = profile_index;
			if (player_profile_get(profile_index, &profile))
			{
				char name[SCREENS_NAME_SIZE];

				text_safe_name(name, (short)sizeof(name), profile.player_name, NUMBEROF(profile.player_name), "PLAYER");
				if (strcmp(name, told_name))
				{
					snprintf(told_name, sizeof(told_name), "%s", name);
					social_set_name(name);
				}
			}
		}
	}
	/* (the join: with no friends screens there is nobody to finish it, so it is not run at all, and neither is the party's following of its leader, which starts in the same call) */
	if (!screens_enabled())
		return;
	social_join_status_get(&status);
	if (status.state == SOCIAL_JOIN_WORKING)
	{
		social_join_update();
		ready_since = failed_since = 0;
	}
	else if (status.state == SOCIAL_JOIN_READY)
	{
		/* (the player pressed A on the game: this machine is in it now. Not pressed in two minutes: given up) */
		if (scr_game_busy() || (ready_since && now - ready_since > 120000))
		{
			social_join_done();
			ready_since = 0;
		}
		else if (!ready_since)
			ready_since = now ? now : 1;
	}
	else if (status.state == SOCIAL_JOIN_FAILED)
	{
		/* (its words are on the Friends screen and in a toast; then the machine is free for the next) */
		if (!failed_since)
			failed_since = now ? now : 1;
		else if (now - failed_since > 8000)
		{
			social_join_done();
			failed_since = 0;
		}
	}
}

/* ---------- the dispatch: the function names the XML runs */

/* the provider made (screens.enable 2: the game's roster with the backend's rest) before the first screen needs it: screens_toast.c asks every frame */
void menu_screens_provider_prepare(void)
{
	if (screens_enabled() == 2 && (!screens_provider() || screens_backend() != scr_table_backend))
		scr_provider();
}

static boolean scr_event(char const *name, struct widget_instance *widget, boolean *widget_deleted, boolean *mine)
{
	*mine = TRUE;
	if (!strcmp(name, "port players open"))
		return scr_players_open(widget);
	if (!strcmp(name, "port players row"))
		return scr_players_row(widget);
	if (!strcmp(name, "port players add bot"))
		return scr_bot_change(TRUE);
	if (!strcmp(name, "port players drop bot"))
		return scr_bot_change(FALSE);
	if (!strcmp(name, "port screens actions open"))
		return scr_actions_open(widget);
	if (!strcmp(name, "port screens action run"))
		return scr_action_run(widget, widget_deleted);
	if (!strcmp(name, "port friends open"))
		return scr_friends_open(widget);
	if (!strncmp(name, "port friends tab ", 17))
		return scr_friends_tab(widget, name + 17);
	if (!strcmp(name, "port friends row"))
		return scr_friends_row(widget);
	if (!strcmp(name, "port friends join"))
		return scr_friends_button(widget, SCREENS_ACTION_JOIN_GAME, widget_deleted);
	if (!strcmp(name, "port friends invite"))
		return scr_friends_button(widget, SCREENS_ACTION_INVITE_GAME, widget_deleted);
	if (!strcmp(name, "port friend add init"))
		return scr_add_init(widget);
	if (!strcmp(name, "port friend add edit"))
		return scr_add_edit(widget);
	if (!strcmp(name, "port friend add send"))
		return scr_add_send(widget);
	if (!strcmp(name, "port friend add back"))
		return scr_add_back(widget, widget_deleted);
	if (!strcmp(name, "port lobby row"))
		return scr_lobby_row(widget);
	if (!strcmp(name, "port lobby bot add"))
		return scr_lobby_bot(TRUE);
	if (!strcmp(name, "port lobby bot drop"))
		return scr_lobby_bot(FALSE);
	*mine = FALSE;
	return FALSE;
}

static boolean scr_data(char const *name, struct widget_instance *widget)
{
	if (!strcmp(name, "port screens menu update"))
	{
		/* (the Multiplayer screen's bar: FRIENDS only while the screens are on and the provider has friends) */
		struct screens_provider const *provider = scr_enabled() ? scr_provider() : NULL;

		visible_set(named(widget, "menu_friends_button", 0), provider && provider->friends);
		focus_off_hidden(widget);
	}
	else if (!strcmp(name, "port players update"))
		scr_players_update(widget);
	else if (!strcmp(name, "port screens actions update"))
		scr_actions_update(widget);
	else if (!strcmp(name, "port friends update"))
		scr_friends_update(widget);
	else if (!strcmp(name, "port friend add update"))
		scr_add_update(widget);
	else
		return FALSE;
	return TRUE;
}
