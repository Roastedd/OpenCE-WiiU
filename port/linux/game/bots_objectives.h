/*
BOTS_OBJECTIVES.H

What the bots do in a game that has an objective (capture the flag, oddball, king of the hill) and when they go for an item that spawns on a
timer: pure decisions from a snapshot of the game (no engine calls), so that the host tests compile them as they are. bots.c fills the snapshot
from the engine (the flag and ball objects, the hills' goal positions, the players) and carries out what comes back (a goal point and what to do
there). Design: reports/research/2026-11-07/design/bots.md 7 and 8, the parts that need no squads and no vehicles.

Roles: the bots of a team are numbered in the order of their player slots; every third (the second of each three) defends, the rest attack. A team
of one bot attacks.
*/

#ifndef HALO_BOTS_OBJECTIVES_H
#define HALO_BOTS_OBJECTIVES_H

#define BOTS_OBJECTIVE_MEMBERS 32
#define BOTS_OBJECTIVE_HILLS 5

enum
{
	_bots_game_other = 0,
	_bots_game_ctf,
	_bots_game_oddball,
	_bots_game_king,
};

enum
{
	_bots_objective_none = 0,
	/* go to the flag or the ball and take it (the action button, held where it is) */
	_bots_objective_grab,
	/* carrying the enemy flag: to the own stand, where it scores */
	_bots_objective_capture,
	/* carrying it and the own flag is out (a capture needs it home): at the stand, until it is back */
	_bots_objective_wait,
	/* at the carrier of the own flag or of the ball (kill him) */
	_bots_objective_hunt,
	/* beside the carrier of the enemy flag, a teammate */
	_bots_objective_escort,
	/* at the own stand */
	_bots_objective_defend,
	/* touch the own flag lying away from its stand: it goes back */
	_bots_objective_return,
	/* on the hill */
	_bots_objective_hold,
	NUMBER_OF_BOTS_OBJECTIVES,
};

struct bots_objective_flag
{
	int present;
	int at_home;
	/* the member carrying it, -1 for none */
	int carrier;
	float home[3];
	float p[3];
};

struct bots_objective_member
{
	int team;
	int alive;
	int bot;
	/* the team of the flag it carries (-1 none), and whether it carries the ball */
	int carrying_flag;
	int carrying_ball;
	float p[3];
};

struct bots_objective_state
{
	int game;
	int teams;
	struct bots_objective_flag flag[2];
	/* the variant's: a capture needs the own flag at its stand */
	int flag_at_home_to_score;
	int ball_present;
	int ball_carrier;
	float ball[3];
	int hill_count;
	float hill[BOTS_OBJECTIVE_HILLS][3];
	int member_count;
	struct bots_objective_member member[BOTS_OBJECTIVE_MEMBERS];
};

struct bots_objective_goal
{
	int kind;
	float p[3];
	/* the member the goal follows (a carrier), -1 for a place */
	int target;
};

/* the bot's role: 0 attacks, 1 defends */
int bots_objective_role(struct bots_objective_state const *state, int me);
/* what member `me` does now; goal->kind is _bots_objective_none for a bot with nothing to do for the objective (it plays as in a slayer game) */
void bots_objective_decide(struct bots_objective_state const *state, int me, struct bots_objective_goal *goal);
/* the share of a skill level's decisions that follow the objective, in percent (the first levels forget it now and then) */
int bots_objective_weight_percent(int skill);

/* ---------- items on a timer */

/* the ticks until the next spawn of an item place (the engine's: when the game's time is a multiple of the period; a period of 0 is 30 seconds);
0 at the spawn tick itself */
long bots_items_ticks_until_spawn(long game_time, long period_seconds);
/* the class of an item from the name of its collection and first item ("weapons\rocket launcher\rocket launcher"): bots_nav.h's _bots_nav_item_ */
int bots_items_class_from_name(char const *name);
/* whether a bot of this skill (0 recruit .. 5 legendary) is after an item of this class on its timer: power weapons from normal, powerups from heroic */
int bots_items_class_wanted(int skill, int item_class);
/* whether to set out now: the item is there already, or it spawns before the bot can be there (eta ticks) plus a margin the better bots keep smaller */
int bots_items_should_go(int skill, long ticks_until_spawn, long eta_ticks, int present);

#endif
