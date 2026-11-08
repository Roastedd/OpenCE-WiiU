/*
BOTS_DIRECTOR.H

The bots' roster rules (pure: no engine calls; the host tests compile it as it is): how many bots a game wants as humans come and go, which bot
goes when a human takes its place, which team a new bot joins, and when a bot has stood so long without getting anywhere that its goal is new.
bots.c asks and the host's lobby code (network_server_manager.c) does it. Design: reports/research/2026-11-07/design/bots.md 11.3, without the
frame-rate shedding (the budget's request for a bot fewer is what it has).
*/

#ifndef HALO_BOTS_DIRECTOR_H
#define HALO_BOTS_DIRECTOR_H

enum
{
	/* no bots (the game's, a local game's bots.count excepted) */
	_bots_director_off = 0,
	/* exactly `count`, humans coming and going as they like */
	_bots_director_fixed,
	/* bots up to `fill_to` players in all: the humans push them out, and they come back when the humans leave */
	_bots_director_fill,
};

struct bots_director_bot
{
	int present;
	int team;
	int dead;
	/* carrying a flag, a ball or a skull */
	int carrying;
	int score;
};

/* how many bots the game wants: fixed -> count; fill -> fill_to less the humans; never more than `maximum`, nor more than the game has room for
(free slots with the bots that are in: `room`), nor less than 0 */
int bots_director_wanted(int mode, int count, int fill_to, int humans, int maximum, int room);

/* the team (0 or 1) a new bot joins: the one with fewer players, the first on a tie */
int bots_director_pick_team(int team0_players, int team1_players);

/* the bot that goes when one must (a human joins a full game, or the game wants fewer): a dead one before a live one, one that carries nothing,
the larger team's, the lowest score; -1 when every bot carries something and `allow_carrier` is 0 */
int bots_director_pick_leaver(struct bots_director_bot const *bots, int count, int allow_carrier);

/* whether to add a bot now, to remove one, or leave it: +1, -1, 0; `ticks_since_change` keeps the roster from changing more than once in
`grace_ticks` (a human's leaving is waited out) */
int bots_director_step(int wanted, int present, long ticks_since_change, long grace_ticks);

/* a goal held for `age_ticks` without being reached is given up after `limit_ticks`; goals that hold a place (defend, hold, wait) are not */
int bots_director_goal_stale(long age_ticks, long limit_ticks, int holds_a_place);

#endif
