#include "harness.h"

/* What VOTE TO KICK and CHANGE TEAM do on the Players page (port/linux/game/menu_screens.inl, scr_game_vote_kick and scr_game_change_team) in each place a machine can be: a lobby, a game
under way, another machine's player, a bot, this machine's own player, a game with and without teams. The engine's world is stood in for; the words must say what is true. */

#define SCREENS_LINE_SIZE 72
#define _client_state_pregame 3
#define _client_state_ingame 4

enum
{
	SCREENS_ROW_BOT = 1 << 0,
	SCREENS_ROW_ME = 1 << 1,
	SCREENS_ROW_LOCAL = 1 << 5,
};

struct screens_roster_row
{
	char name[12];
	unsigned short flags;
	long machine;
	signed char controller;
};
struct network_game
{
	struct
	{
		struct
		{
			int teams;
		} universal_variant;
	} variant;
};

static struct network_game game;
static short client_state = _client_state_ingame;
static int have_game = 1;
static int voted_machine = -1, votes, vote_result = 1;
static int swapped_controller = -1, swaps, swap_result = 1;

static int scr_game_players(struct network_game **out, short *state)
{
	*out = have_game ? &game : NULL;
	*state = have_game ? client_state : NONE;
	return have_game && client_state >= _client_state_pregame;
}
static boolean network_votekick_machine(short machine)
{
	votes++;
	voted_machine = machine;
	return vote_result;
}
static boolean ui_widget_port_swap_team(short controller)
{
	swaps++;
	swapped_controller = controller;
	return swap_result;
}
static void scr_answer(char *answer, int size, char const *text)
{
	if (answer && size > 0)
		snprintf(answer, (size_t)size, "%s", text);
}

#include "roster_words_code.inc"

static void world(short state, int teams)
{
	have_game = 1;
	client_state = state;
	game.variant.universal_variant.teams = teams;
	voted_machine = -1;
	votes = 0;
	vote_result = 1;
	swapped_controller = -1;
	swaps = 0;
	swap_result = 1;
}

static struct screens_roster_row row(unsigned short flags, long machine, signed char controller)
{
	struct screens_roster_row value;

	memset(&value, 0, sizeof(value));
	snprintf(value.name, sizeof(value.name), "Other");
	value.flags = flags;
	value.machine = machine;
	value.controller = controller;
	return value;
}

static int says(char const *answer, char const *words)
{
	return strstr(answer, words) != NULL;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	char answer[SCREENS_LINE_SIZE];
	struct screens_roster_row other = row(0, 7, 0), me = row(SCREENS_ROW_ME, 1, 2), bot = row(SCREENS_ROW_BOT, 100, 0), local = row(SCREENS_ROW_LOCAL, 1, 1);

	CASE("vote-in-game")
	{
		/* another machine's player in a game under way: this machine's vote goes to the game's, by the machine */
		world(_client_state_ingame, 0);
		answer[0] = 0;
		CHECK(scr_game_vote_kick(&other, answer, sizeof(answer)) == 1 && votes == 1 && voted_machine == 7, "voted %d against machine %d", votes, voted_machine);
		CHECK(says(answer, "YOUR VOTE IS IN"), "'%s'", answer);
		/* the game says no such player (gone): said */
		vote_result = 0;
		CHECK(scr_game_vote_kick(&other, answer, sizeof(answer)) == 0 && says(answer, "NOT IN THE GAME ANY MORE"), "gone: '%s'", answer);
	}
	else CASE("vote-refused")
	{
		/* a lobby: no vote yet, the words say the host can kick */
		world(_client_state_pregame, 0);
		CHECK(scr_game_vote_kick(&other, answer, sizeof(answer)) == 0 && votes == 0 && says(answer, "ONCE THE GAME HAS STARTED") && says(answer, "HOST CAN KICK"), "lobby: '%s'", answer);
		world(_client_state_ingame, 0);
		CHECK(scr_game_vote_kick(&me, answer, sizeof(answer)) == 0 && votes == 0 && says(answer, "ANOTHER MACHINE"), "oneself: '%s'", answer);
		CHECK(scr_game_vote_kick(&bot, answer, sizeof(answer)) == 0 && votes == 0, "a bot");
		CHECK(scr_game_vote_kick(&local, answer, sizeof(answer)) == 0 && votes == 0, "a player of this machine");
		have_game = 0;
		CHECK(scr_game_vote_kick(&other, answer, sizeof(answer)) == 0 && votes == 0, "no game");
	}
	else CASE("team-own")
	{
		/* the lobby of a game with teams: this machine's own player, by its controller */
		world(_client_state_pregame, 1);
		CHECK(scr_game_change_team(&me, answer, sizeof(answer)) == 1 && swaps == 1 && swapped_controller == 2, "swapped %d controller %d", swaps, swapped_controller);
		me.controller = -1;
		CHECK(scr_game_change_team(&me, answer, sizeof(answer)) == 1 && swapped_controller == 0, "no controller known: the first");
		swap_result = 0;
		CHECK(scr_game_change_team(&me, answer, sizeof(answer)) == 0 && says(answer, "DID NOT WORK"), "'%s'", answer);
	}
	else CASE("team-refused")
	{
		world(_client_state_pregame, 1);
		CHECK(scr_game_change_team(&other, answer, sizeof(answer)) == 0 && swaps == 0 && says(answer, "OWN TEAM"), "another person: '%s'", answer);
		CHECK(scr_game_change_team(&bot, answer, sizeof(answer)) == 0 && swaps == 0 && says(answer, "BOTS > TEAMS"), "a bot: '%s'", answer);
		world(_client_state_pregame, 0);
		CHECK(scr_game_change_team(&me, answer, sizeof(answer)) == 0 && swaps == 0 && says(answer, "NO TEAMS"), "no teams: '%s'", answer);
		world(_client_state_ingame, 1);
		CHECK(scr_game_change_team(&me, answer, sizeof(answer)) == 0 && swaps == 0 && says(answer, "IN THE LOBBY"), "a game under way: '%s'", answer);
		have_game = 0;
		CHECK(scr_game_change_team(&me, answer, sizeof(answer)) == 0 && swaps == 0 && says(answer, "NO GAME"), "no game: '%s'", answer);
	}
	else
		return 2;
	return 0;
}
