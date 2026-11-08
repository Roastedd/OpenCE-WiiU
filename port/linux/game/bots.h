/* bots.h: computer players for local multiplayer games (port/linux/game/bots.c)

A bot is a player of a local (split screen) game whose controls come from
code instead of a controller: it is in the lobby, the scores and the game's
rules as any player is, spawns, respawns and scores. Each bot is the one
player of a machine of its own at the top of the machine indices
(BOTS_FIRST_MACHINE up), which no real machine takes: a split screen game has
only its own machine. The bots have no view and no controller: nothing is
drawn for them. */
#ifndef HALO_BOTS_H
#define HALO_BOTS_H

struct player_action;
struct network_player;

/* the most bots in one game (with one player, the Xbox's 16) */
#define BOTS_MAXIMUM 15
/* the first bot's machine index (the last machine indices of the session) */
#define BOTS_FIRST_MACHINE (HALO_PORT_MAXIMUM_NETWORK_MACHINES - BOTS_MAXIMUM)

enum
{
	/* bots.teams: in a team game, the bots on both teams (evening them
	with the players), or all on the team the players are not on */
	_bots_teams_even = 0,
	_bots_teams_against,
};

/* whether a game of this engine (game_engine_type) may have bots: slayer; the games with an objective too when bots.objectives is on */
boolean bots_game_allowed(long game_engine_index);

/* the settings (bots.count, bots.skill, bots.teams) */
long bots_wanted_count(void);
short bots_skill(void);
short bots_teams(void);

/* a machine is a bot's */
int bots_machine_is_bot(long machine_index);
/* the network player for bot number `bot_index` (0 up): its machine,
controller, name and no team or colour yet */
void bots_network_player(long bot_index, struct network_player *player);

/* the bots' tick (update_server_next_update, player_queues_new.c), once for each update the server builds and before it is built:
every bot decides its controls this tick. They are taken by bots_fill_action, into the queue of the bot's player. Nothing
outside a local game. */
void bots_tick(void);
/* the controls the bots' tick built for the player with that index (the player's absolute index): TRUE and the action, or FALSE
for a player that is no bot's */
boolean bots_fill_action(long player_index, struct player_action *action);
/* one bot's own skill level, by its number (0 up), from now on; NONE: the setting's again (bots.skill) */
void bots_set_skill(long bot_index, short skill);

/* ---------- bots in a game this machine hosts, and what the screens (a stream of its own) call

A bot is a player of a machine of its own (BOTS_FIRST_MACHINE up), so the screens know it by the machine index of the player they are drawing: a
roster row, a scoreboard line, the pause menu's players. bots.online must be on for a hosted game to have bots; with it off, nothing here does
anything and a hosted game is what it always was. Everything is on the game thread. */

/* bots.online: the games this machine hosts may have bots */
boolean bots_online_enabled(void);
/* the player of this machine index is a bot's (the same as bots_machine_is_bot): TRUE; its level (0 recruit .. 5 legendary), NONE for a human */
short bots_machine_skill(long machine_index);
/* the tag a screen shows by a bot's name: "BOT NORMAL", "BOT LEGENDARY" ..., an empty string for a human */
void bots_machine_tag(long machine_index, char *buffer, long size);
/* the bots in the game now */
long bots_roster_count(void);
/* the roster's mode (bots_director.h: 0 off, 1 fixed at bots.count, 2 fill to a total of players) and the total */
short bots_host_mode(void);
long bots_host_fill_to(void);
void bots_host_set_mode(short mode, long fill_to);
/* one bot more (on a team: 0 or 1, NONE for the smaller; at a level, NONE for bots.skill) and one bot fewer (the machine index of the bot's player):
what the host's lobby code does at its next idle, which makes the mode "fixed" with the count one more or less */
boolean bots_host_add(long team, short skill);
boolean bots_host_drop(long machine_index);
/* the lobby code's: how many bots a hosted game wants of `humans` humans in a game of `maximum_players`; the add and drop asked for; whether a
change may be made now (one in `grace_milliseconds`) */
long bots_hosted_wanted(long humans, long maximum_players, long bots_present);
boolean bots_host_take_add(long *team, short *skill);
long bots_host_take_drop(void);
boolean bots_host_change_allowed(long milliseconds, long grace_milliseconds);

/* a map's start and end (players.c) */
void bots_initialize_for_new_map(void);
void bots_dispose_from_old_map(void);

#endif
