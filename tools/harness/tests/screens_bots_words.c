#include "harness.h"

/* What ADD BOT and DROP BOT answer (port/linux/game/menu_screens.inl, scr_game_bots_change) in each game this machine can be in, on stand-ins for the engine's world: a game of this machine's
players alone (a split screen game: its lobby, and once it is going), a game this machine hosts for others (with the experimental bots.online off and on), a game someone else hosts, and no
game. The words must say what is true: bots play in a game on this machine, are set in its lobby before it starts, and are not used in hosted or online games. */

#define NUMBEROF(array) ((int)(sizeof(array) / sizeof((array)[0])))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define HALO_PORT_MAXIMUM_NETWORK_PLAYERS 16
#define BOTS_MAXIMUM 15
#define BOTS_FIRST_MACHINE 100
#define SCREENS_LINE_SIZE 72
#define SCREENS_TEAM_NONE (-1)
#define _client_state_pregame 3
#define _client_state_ingame 5

struct network_player
{
	long machine_index;
	int valid;
};
struct network_game
{
	struct network_player players[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	short maximum_players;
	struct
	{
		long game_engine_index;
	} variant;
};

/* the world */
static struct network_game game;
static short client_state = _client_state_pregame;
static int have_game = 1, have_server = 1, splitscreen = 1, online_bots, objectives;
static long bots_count_setting, bots_max_setting = 15;
static char written[32];
static int host_added, host_dropped;

static int scr_game_players(struct network_game **out, short *state)
{
	*out = have_game ? &game : NULL;
	*state = have_game ? client_state : NONE;
	return have_game && client_state >= _client_state_pregame;
}
static void *global_network_game_server_get(void)
{
	static int server;

	return have_server ? &server : NULL;
}
static int network_game_is_splitscreen_local(void) { return have_server && splitscreen; }
static int bots_online_enabled(void) { return online_bots; }
static long bots_wanted_count(void) { return bots_count_setting; }
static long config_integer(char const *name) { return !strcmp(name, "bots.max") ? bots_max_setting : 0; }
static int network_player_is_valid(struct network_player const *player) { return player->valid; }
static int bots_machine_is_bot(long machine) { return machine >= BOTS_FIRST_MACHINE && machine < BOTS_FIRST_MACHINE + BOTS_MAXIMUM; }
static boolean bots_host_add(long team, short skill) { (void)team; (void)skill; host_added++; return TRUE; }
static boolean bots_host_drop(long machine) { (void)machine; host_dropped++; return TRUE; }
static void bots_set_skill(long index, short skill) { (void)index; (void)skill; }
static boolean bots_game_allowed(long engine) { return engine == 1 || (objectives && engine != 0); }
static int config_write(char const *name, char const *value)
{
	CHECK(!strcmp(name, "bots.count"), "writes %s", name);
	snprintf(written, sizeof(written), "%s", value);
	return 1;
}
static void scr_answer(char *answer, int size, char const *text)
{
	if (answer && size > 0)
		snprintf(answer, (size_t)size, "%s", text);
}
static boolean scr_game_bots_hosted(void)
{
	return bots_online_enabled() && global_network_game_server_get() && !network_game_is_splitscreen_local();
}

#include "bots_words_code.inc"

static void world(int server, int split, short state)
{
	have_game = 1;
	have_server = server;
	splitscreen = split;
	client_state = state;
	online_bots = objectives = 0;
	bots_count_setting = 2;
	written[0] = 0;
	host_added = host_dropped = 0;
	memset(&game, 0, sizeof(game));
	game.maximum_players = 16;
	game.variant.game_engine_index = 1;
	game.players[0] = (struct network_player){ 0, 1 };
}

static int says(char const *answer, char const *words)
{
	return strstr(answer, words) != NULL;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	char answer[SCREENS_LINE_SIZE];
	int done;

	CASE("local-lobby")
	{
		/* a game on this machine, before it starts: the setting changes, no words */
		world(1, 1, _client_state_pregame);
		answer[0] = 0;
		done = scr_game_bots_change(1, -1, -1, NONE, answer, (int)sizeof(answer));
		CHECK(done && !answer[0] && !strcmp(written, "3"), "adding: %d '%s' wrote '%s'", done, answer, written);
		done = scr_game_bots_change(-1, -1, -1, NONE, answer, (int)sizeof(answer));
		CHECK(done && !strcmp(written, "1"), "dropping: %d wrote '%s'", done, written);
		bots_count_setting = 15;
		done = scr_game_bots_change(1, -1, -1, NONE, answer, (int)sizeof(answer));
		CHECK(!done && says(answer, "MOST BOTS"), "past the most: '%s'", answer);
		bots_count_setting = 0;
		done = scr_game_bots_change(-1, -1, -1, NONE, answer, (int)sizeof(answer));
		CHECK(!done && says(answer, "NO BOT TO DROP"), "none to drop: '%s'", answer);
	}
	else CASE("local-gametype")
	{
		/* a gametype bots do not play in: told, not "done" with nothing happening */
		world(1, 1, _client_state_pregame);
		game.variant.game_engine_index = 2;
		done = scr_game_bots_change(1, -1, -1, NONE, answer, (int)sizeof(answer));
		CHECK(!done && says(answer, "GAME TYPE") && !written[0], "'%s' wrote '%s'", answer, written);
		objectives = 1;
		done = scr_game_bots_change(1, -1, -1, NONE, answer, (int)sizeof(answer));
		CHECK(done && !strcmp(written, "3"), "with bots.objectives on a game with an objective takes them: %d", done);
	}
	else CASE("local-running")
	{
		/* the pause menu of a game on this machine: a bot cannot be added or dropped once it is going, and it is said */
		world(1, 1, _client_state_ingame);
		done = scr_game_bots_change(1, -1, -1, NONE, answer, (int)sizeof(answer));
		CHECK(!done && says(answer, "LOBBY") && says(answer, "BEFORE THE GAME STARTS") && !written[0], "adding: '%s'", answer);
		done = scr_game_bots_change(-1, -1, -1, NONE, answer, (int)sizeof(answer));
		CHECK(!done && says(answer, "BEFORE THE GAME STARTS"), "dropping: '%s'", answer);
	}
	else CASE("hosted")
	{
		/* a game this machine hosts for others (an internet game): no bots, and the words say that, not "the lobby of a game on this machine" */
		world(1, 0, _client_state_pregame);
		done = scr_game_bots_change(1, -1, -1, NONE, answer, (int)sizeof(answer));
		CHECK(!done && says(answer, "HOSTED OR ONLINE") && says(answer, "LOCAL GAME") && !says(answer, "ON THIS MACHINE"), "adding: '%s'", answer);
		done = scr_game_bots_change(-1, -1, -1, NONE, answer, (int)sizeof(answer));
		CHECK(!done && says(answer, "HOSTED OR ONLINE"), "dropping: '%s'", answer);
		world(1, 0, _client_state_ingame);
		done = scr_game_bots_change(1, -1, -1, NONE, answer, (int)sizeof(answer));
		CHECK(!done && says(answer, "HOSTED OR ONLINE") && !written[0] && !host_added, "a hosted game in play: '%s'", answer);
	}
	else CASE("hosted-online-bots")
	{
		/* with the experimental bots.online on the stream's own calls are used */
		world(1, 0, _client_state_pregame);
		online_bots = 1;
		done = scr_game_bots_change(1, -1, -1, NONE, answer, (int)sizeof(answer));
		CHECK(done && host_added == 1 && !written[0], "adding: %d added %d", done, host_added);
		game.players[1] = (struct network_player){ BOTS_FIRST_MACHINE + 2, 1 };
		done = scr_game_bots_change(-1, -1, -1, NONE, answer, (int)sizeof(answer));
		CHECK(done && host_dropped == 1, "dropping: %d dropped %d", done, host_dropped);
	}
	else CASE("not-the-host")
	{
		/* a game someone else hosts, or none: only for a local game, not for online play */
		world(0, 0, _client_state_pregame);
		done = scr_game_bots_change(1, -1, -1, NONE, answer, (int)sizeof(answer));
		CHECK(!done && says(answer, "LOCAL GAME") && says(answer, "NOT ONLINE PLAY"), "'%s'", answer);
		have_game = 0;
		done = scr_game_bots_change(1, -1, -1, NONE, answer, (int)sizeof(answer));
		CHECK(!done && says(answer, "LOCAL GAME"), "no game: '%s'", answer);
	}
	else
		return 2;
	return 0;
}
