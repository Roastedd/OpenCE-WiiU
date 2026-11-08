/*
BOTS_OBJECTIVES.C

The decisions of bots_objectives.h.
*/

#include "bots_nav.h"
#include "bots_objectives.h"

#include <math.h>
#include <string.h>

int bots_objective_weight_percent(int skill)
{
	static int const percent[6] = { 30, 50, 75, 90, 100, 100 };

	return percent[skill < 0 ? 0 : skill > 5 ? 5 : skill];
}

/* the bots of the member's team before it (by order of the table: the players' slots), and whether the team has a second */
int bots_objective_role(struct bots_objective_state const *state, int me)
{
	int index, rank = 0, count = 0;

	for (index = 0; index < state->member_count; index++)
	{
		if (!state->member[index].bot || state->member[index].team != state->member[me].team)
			continue;
		if (index < me)
			rank++;
		count++;
	}
	return count >= 2 && rank % 3 == 1 ? 1 : 0;
}

static void set_goal(struct bots_objective_goal *goal, int kind, float const *p, int target)
{
	goal->kind = kind;
	memcpy(goal->p, p, sizeof(goal->p));
	goal->target = target;
}

static void decide_ctf(struct bots_objective_state const *state, int me, struct bots_objective_goal *goal)
{
	struct bots_objective_member const *m = &state->member[me];
	int team = m->team & 1, enemy = 1 - team;
	struct bots_objective_flag const *own = &state->flag[team], *theirs = &state->flag[enemy];
	int role = bots_objective_role(state, me);

	if (!own->present || !theirs->present)
		return;
	/* carrying the enemy's flag: home to score (the own flag must be at its stand if the variant says so: then wait there for it) */
	if (m->carrying_flag == enemy)
	{
		if (own->at_home || !state->flag_at_home_to_score)
			set_goal(goal, _bots_objective_capture, own->home, -1);
		else
			set_goal(goal, _bots_objective_wait, own->home, -1);
		return;
	}
	/* the own flag is carried off: kill the carrier */
	if (own->carrier >= 0 && own->carrier < state->member_count)
	{
		set_goal(goal, _bots_objective_hunt, state->member[own->carrier].p, own->carrier);
		return;
	}
	/* the own flag lies away from its stand: the defenders (and the nearest of the others, by the table's order) touch it */
	if (!own->at_home)
	{
		if (role == 1 || state->member[me].bot)
			set_goal(goal, _bots_objective_return, own->p, -1);
		return;
	}
	/* a teammate carries the enemy flag: the attackers go with him */
	if (theirs->carrier >= 0 && theirs->carrier < state->member_count && state->member[theirs->carrier].team == m->team)
	{
		if (role == 0 && theirs->carrier != me)
			set_goal(goal, _bots_objective_escort, state->member[theirs->carrier].p, theirs->carrier);
		else if (role == 1)
			set_goal(goal, _bots_objective_defend, own->home, -1);
		return;
	}
	if (role == 1)
		set_goal(goal, _bots_objective_defend, own->home, -1);
	else
		set_goal(goal, _bots_objective_grab, theirs->p, -1);
}

static void decide_oddball(struct bots_objective_state const *state, int me, struct bots_objective_goal *goal)
{
	if (state->member[me].carrying_ball)
		return;
	if (state->ball_carrier >= 0 && state->ball_carrier < state->member_count)
	{
		/* (a teammate with the ball: in a team game the others stay near it) */
		if (state->teams && state->member[state->ball_carrier].team == state->member[me].team)
			set_goal(goal, _bots_objective_escort, state->member[state->ball_carrier].p, state->ball_carrier);
		else
			set_goal(goal, _bots_objective_hunt, state->member[state->ball_carrier].p, state->ball_carrier);
		return;
	}
	if (state->ball_present)
		set_goal(goal, _bots_objective_grab, state->ball, -1);
}

static void decide_king(struct bots_objective_state const *state, int me, struct bots_objective_goal *goal)
{
	int index, best = -1;
	float best_distance = 1.0e30f;

	for (index = 0; index < state->hill_count; index++)
	{
		float dx = state->hill[index][0] - state->member[me].p[0], dy = state->hill[index][1] - state->member[me].p[1];
		float distance = (float)sqrt(dx * dx + dy * dy);

		if (distance < best_distance)
		{
			best_distance = distance;
			best = index;
		}
	}
	if (best >= 0)
		set_goal(goal, _bots_objective_hold, state->hill[best], -1);
}

void bots_objective_decide(struct bots_objective_state const *state, int me, struct bots_objective_goal *goal)
{
	memset(goal, 0, sizeof(*goal));
	goal->kind = _bots_objective_none;
	goal->target = -1;
	if (me < 0 || me >= state->member_count || !state->member[me].alive)
		return;
	switch (state->game)
	{
	case _bots_game_ctf:
		decide_ctf(state, me, goal);
		break;
	case _bots_game_oddball:
		decide_oddball(state, me, goal);
		break;
	case _bots_game_king:
		decide_king(state, me, goal);
		break;
	default:
		break;
	}
}

/* ---------- items on a timer */

long bots_items_ticks_until_spawn(long game_time, long period_seconds)
{
	long period = (period_seconds > 0 ? period_seconds : 30) * 30;
	long into = game_time % period;

	if (game_time < 0)
		return 0;
	return into == 0 ? 0 : period - into;
}

int bots_items_class_from_name(char const *name)
{
	char lower[512];
	size_t k;

	for (k = 0; name[k] && k < sizeof(lower) - 1; k++)
		lower[k] = (name[k] >= 'A' && name[k] <= 'Z') ? (char)(name[k] - 'A' + 'a') : name[k];
	lower[k] = 0;
	if (strstr(lower, "rocket") || strstr(lower, "sniper") || strstr(lower, "flamethrower") || strstr(lower, "fuel rod") ||
		strstr(lower, "fuel_rod") || strstr(lower, "plasma cannon") || strstr(lower, "plasma_cannon"))
	{
		return _bots_nav_item_power_weapon;
	}
	if (strstr(lower, "overshield") || strstr(lower, "camo") || strstr(lower, "powerup") || strstr(lower, "over shield"))
		return _bots_nav_item_powerup;
	if (strstr(lower, "grenade"))
		return _bots_nav_item_grenades;
	if (strstr(lower, "ammo") || strstr(lower, "clip") || strstr(lower, "health"))
		return _bots_nav_item_ammo;
	if (strstr(lower, "weapon") || strstr(lower, "rifle") || strstr(lower, "pistol") || strstr(lower, "shotgun") || strstr(lower, "needler") ||
		strstr(lower, "plasma"))
	{
		return _bots_nav_item_weapon;
	}
	return _bots_nav_item_unknown;
}

int bots_items_class_wanted(int skill, int item_class)
{
	if (item_class == _bots_nav_item_power_weapon)
		return skill >= 2;
	if (item_class == _bots_nav_item_powerup)
		return skill >= 3;
	return 0;
}

int bots_items_should_go(int skill, long ticks_until_spawn, long eta_ticks, int present)
{
	/* (the better bots keep a smaller margin: they arrive nearer to the spawn) */
	long margin = skill >= 4 ? 30 : skill >= 3 ? 60 : 180;

	if (present)
		return 1;
	return ticks_until_spawn <= eta_ticks + margin;
}
