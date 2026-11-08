/*
BOTS_OBJECTIVES.C (test)

The bots' objective play and item timers (bots_objectives.c, compiled as it is): capture the flag (grab, capture, wait for the own flag, hunt the
carrier of the own flag, return a dropped flag, escort, defend), oddball, king, the roles by order, the skill's weight, and the item timers
(the engine's spawn rule, the class of a name, who wants what, when to set out). test_bots_objectives.py takes the code and its header from the
sources.
*/

#include "harness.h"

#include "bots_nav.h"
#include "bots_objectives.h"
#include "under_test.inc"

static struct bots_objective_state state;

/* a CTF game: flags at (0,0) for team 0 and (100,0) for team 1, both home; members added by add() */
static void ctf_new(void)
{
	memset(&state, 0, sizeof(state));
	state.game = _bots_game_ctf;
	state.teams = 1;
	state.flag_at_home_to_score = 1;
	state.flag[0].present = state.flag[1].present = 1;
	state.flag[0].at_home = state.flag[1].at_home = 1;
	state.flag[0].carrier = state.flag[1].carrier = -1;
	state.flag[1].home[0] = state.flag[1].p[0] = 100.0f;
	state.ball_carrier = -1;
}

static int add(int team, int bot, float x, float y)
{
	struct bots_objective_member *m = &state.member[state.member_count];

	m->team = team;
	m->bot = bot;
	m->alive = 1;
	m->carrying_flag = -1;
	m->p[0] = x;
	m->p[1] = y;
	return state.member_count++;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	struct bots_objective_goal goal;

	/* the roles: of a team's bots, the second of every three defends; a team of one bot attacks; humans count for nothing */
	CASE("roles")
	{
		int a, b, c, d, human;

		ctf_new();
		/* (a bot of the other team, and a human, before it: neither counts) */
		add(1, 1, 0, 0);
		human = add(0, 0, 0, 0);
		a = add(0, 1, 0, 0);
		CHECK(bots_objective_role(&state, a) == 0, "one bot defends");
		b = add(0, 1, 0, 0);
		c = add(0, 1, 0, 0);
		d = add(0, 1, 0, 0);
		CHECK(bots_objective_role(&state, a) == 0 && bots_objective_role(&state, b) == 1 && bots_objective_role(&state, c) == 0 &&
			bots_objective_role(&state, d) == 0, "roles %d %d %d %d", bots_objective_role(&state, a), bots_objective_role(&state, b),
			bots_objective_role(&state, c), bots_objective_role(&state, d));
		(void)human;
		CHECK(state.member[0].team == 1, "the table");
		return 0;
	}
	/* attackers go for the enemy flag, defenders to their own stand */
	CASE("ctf-start")
	{
		int a, b;

		ctf_new();
		a = add(0, 1, 5, 0);
		b = add(0, 1, 6, 0);
		bots_objective_decide(&state, a, &goal);
		CHECK(goal.kind == _bots_objective_grab && goal.p[0] == 100.0f, "attacker %d at %f", goal.kind, goal.p[0]);
		bots_objective_decide(&state, b, &goal);
		CHECK(goal.kind == _bots_objective_defend && goal.p[0] == 0.0f, "defender %d at %f", goal.kind, goal.p[0]);
		/* (the other team's bot: its own flag is the one at 100) */
		{
			int c = add(1, 1, 90, 0);

			bots_objective_decide(&state, c, &goal);
			CHECK(goal.kind == _bots_objective_grab && goal.p[0] == 0.0f, "the other team's attacker %d at %f", goal.kind, goal.p[0]);
		}
		/* (a dead bot has nothing to do) */
		state.member[a].alive = 0;
		bots_objective_decide(&state, a, &goal);
		CHECK(goal.kind == _bots_objective_none, "a dead bot has a goal");
		return 0;
	}
	/* a bot with the enemy flag goes home; if the own flag is out and the variant needs it home, it waits at the stand; without that rule it goes anyway */
	CASE("ctf-carry")
	{
		int a = (ctf_new(), add(0, 1, 50, 0));

		state.member[a].carrying_flag = 1;
		state.flag[1].carrier = a;
		bots_objective_decide(&state, a, &goal);
		CHECK(goal.kind == _bots_objective_capture && goal.p[0] == 0.0f, "carrier %d", goal.kind);
		state.flag[0].at_home = 0;
		state.flag[0].p[0] = 30.0f;
		bots_objective_decide(&state, a, &goal);
		CHECK(goal.kind == _bots_objective_wait && goal.p[0] == 0.0f, "carrier with the own flag out %d", goal.kind);
		state.flag_at_home_to_score = 0;
		bots_objective_decide(&state, a, &goal);
		CHECK(goal.kind == _bots_objective_capture, "carrier where the flag need not be home %d", goal.kind);
		return 0;
	}
	/* the own flag carried off: everyone hunts the carrier (his position follows); lying away from the stand: it is touched; a teammate carrying: escort */
	CASE("ctf-defend")
	{
		int a, b, enemy;

		ctf_new();
		a = add(0, 1, 5, 0);
		b = add(0, 1, 6, 0);
		enemy = add(1, 0, 40, 10);
		state.flag[0].at_home = 0;
		state.flag[0].carrier = enemy;
		state.flag[0].p[0] = 40.0f;
		state.flag[0].p[1] = 10.0f;
		bots_objective_decide(&state, a, &goal);
		CHECK(goal.kind == _bots_objective_hunt && goal.target == enemy && goal.p[0] == 40.0f && goal.p[1] == 10.0f, "hunt %d target %d", goal.kind, goal.target);
		bots_objective_decide(&state, b, &goal);
		CHECK(goal.kind == _bots_objective_hunt, "the defender hunts too: %d", goal.kind);
		state.flag[0].carrier = -1;
		bots_objective_decide(&state, b, &goal);
		CHECK(goal.kind == _bots_objective_return && goal.p[0] == 40.0f, "return %d", goal.kind);
		state.flag[0].at_home = 1;
		state.flag[0].p[0] = 0.0f;
		state.flag[1].at_home = 0;
		state.flag[1].carrier = a;
		state.member[a].carrying_flag = 1;
		{
			int c = add(0, 1, 7, 0);
			int d = add(0, 1, 8, 0);

			bots_objective_decide(&state, c, &goal);
			CHECK(goal.kind == _bots_objective_escort && goal.target == a, "escort %d target %d", goal.kind, goal.target);
			bots_objective_decide(&state, d, &goal);
			CHECK(goal.kind == _bots_objective_escort || goal.kind == _bots_objective_defend, "third bot %d", goal.kind);
			bots_objective_decide(&state, b, &goal);
			CHECK(goal.kind == _bots_objective_defend, "the defender during the carry %d", goal.kind);
		}
		return 0;
	}
	/* a game with no flags (the stands are not on the map) gives the bots nothing to do for it */
	CASE("ctf-no-flags")
	{
		int a = (ctf_new(), add(0, 1, 5, 0));

		state.flag[1].present = 0;
		bots_objective_decide(&state, a, &goal);
		CHECK(goal.kind == _bots_objective_none, "a goal without the flag: %d", goal.kind);
		state.game = _bots_game_other;
		state.flag[1].present = 1;
		bots_objective_decide(&state, a, &goal);
		CHECK(goal.kind == _bots_objective_none, "a goal in a slayer game: %d", goal.kind);
		return 0;
	}
	/* oddball: the ball is fetched; its carrier is hunted (in a team game a teammate's is guarded); the carrier himself has nothing to do */
	CASE("oddball")
	{
		int a, b;

		memset(&state, 0, sizeof(state));
		state.game = _bots_game_oddball;
		state.ball_carrier = -1;
		a = add(0, 1, 0, 0);
		b = add(1, 1, 20, 0);
		bots_objective_decide(&state, a, &goal);
		CHECK(goal.kind == _bots_objective_none, "no ball, and a goal");
		state.ball_present = 1;
		state.ball[0] = 10.0f;
		bots_objective_decide(&state, a, &goal);
		CHECK(goal.kind == _bots_objective_grab && goal.p[0] == 10.0f, "fetch %d", goal.kind);
		state.ball_carrier = b;
		state.member[b].carrying_ball = 1;
		bots_objective_decide(&state, a, &goal);
		CHECK(goal.kind == _bots_objective_hunt && goal.target == b && goal.p[0] == 20.0f, "hunt %d", goal.kind);
		bots_objective_decide(&state, b, &goal);
		CHECK(goal.kind == _bots_objective_none, "the carrier has a goal %d", goal.kind);
		state.teams = 1;
		state.member[b].team = 0;
		bots_objective_decide(&state, a, &goal);
		CHECK(goal.kind == _bots_objective_escort, "a teammate's ball is hunted: %d", goal.kind);
		return 0;
	}
	/* king: the nearest hill, held */
	CASE("king")
	{
		int a;

		memset(&state, 0, sizeof(state));
		state.game = _bots_game_king;
		a = add(0, 1, 0, 0);
		bots_objective_decide(&state, a, &goal);
		CHECK(goal.kind == _bots_objective_none, "no hill, and a goal");
		state.hill_count = 2;
		state.hill[0][0] = 50.0f;
		state.hill[1][0] = 20.0f;
		bots_objective_decide(&state, a, &goal);
		CHECK(goal.kind == _bots_objective_hold && goal.p[0] == 20.0f, "hold %d at %f", goal.kind, goal.p[0]);
		return 0;
	}
	CASE("weight")
	{
		CHECK(bots_objective_weight_percent(0) == 30 && bots_objective_weight_percent(5) == 100 && bots_objective_weight_percent(-3) == 30 &&
			bots_objective_weight_percent(9) == 100, "weights");
		CHECK(bots_objective_weight_percent(0) < bots_objective_weight_percent(1) && bots_objective_weight_percent(3) < bots_objective_weight_percent(4), "order");
		return 0;
	}
	/* the engine's spawn rule: at the multiples of the period (30 s a default); 0 at the spawn itself */
	CASE("spawn-timer")
	{
		CHECK(bots_items_ticks_until_spawn(0, 90) == 0, "at 0: %ld", bots_items_ticks_until_spawn(0, 90));
		CHECK(bots_items_ticks_until_spawn(1, 90) == 2699, "at 1: %ld", bots_items_ticks_until_spawn(1, 90));
		CHECK(bots_items_ticks_until_spawn(2700, 90) == 0, "at the second spawn");
		CHECK(bots_items_ticks_until_spawn(2699, 90) == 1, "one before");
		CHECK(bots_items_ticks_until_spawn(100, 0) == 800, "a period of 0 is 30 s: %ld", bots_items_ticks_until_spawn(100, 0));
		CHECK(bots_items_ticks_until_spawn(-5, 30) == 0, "before the game");
		return 0;
	}
	CASE("item-classes")
	{
		CHECK(bots_items_class_from_name("weapons\\rocket launcher\\rocket launcher") == _bots_nav_item_power_weapon, "rockets");
		CHECK(bots_items_class_from_name("levels\\test\\bloodgulch\\items\\sniper rifle sniper rifle") == _bots_nav_item_power_weapon, "sniper");
		CHECK(bots_items_class_from_name("Weapons\\Flamethrower") == _bots_nav_item_power_weapon, "case");
		CHECK(bots_items_class_from_name("powerups\\over shield") == _bots_nav_item_powerup, "overshield");
		CHECK(bots_items_class_from_name("powerups\\active camouflage") == _bots_nav_item_powerup, "camo");
		CHECK(bots_items_class_from_name("weapons\\frag grenade\\frag grenade") == _bots_nav_item_grenades, "grenades");
		CHECK(bots_items_class_from_name("weapons\\assault rifle\\assault rifle") == _bots_nav_item_weapon, "ar");
		CHECK(bots_items_class_from_name("scenery\\rock") == _bots_nav_item_unknown, "rock");
		return 0;
	}
	/* the first levels pass power weapons by, the middle ones take weapons, the upper ones powerups too; nobody times plain weapons */
	CASE("item-wanted")
	{
		int skill;

		for (skill = 0; skill < 6; skill++)
		{
			CHECK(bots_items_class_wanted(skill, _bots_nav_item_power_weapon) == (skill >= 2), "power weapons at %d", skill);
			CHECK(bots_items_class_wanted(skill, _bots_nav_item_powerup) == (skill >= 3), "powerups at %d", skill);
			CHECK(!bots_items_class_wanted(skill, _bots_nav_item_weapon) && !bots_items_class_wanted(skill, _bots_nav_item_unknown), "plain at %d", skill);
		}
		return 0;
	}
	/* set out when the item is there, or spawns before the bot can be there (the better bots later: they time it closer) */
	CASE("item-go")
	{
		CHECK(bots_items_should_go(2, 5000, 100, 1), "an item that is there is gone to");
		CHECK(!bots_items_should_go(2, 5000, 100, 0), "set out too early");
		CHECK(bots_items_should_go(2, 250, 100, 0), "normal sets out 180 ticks early: spawn in 250, eta 100");
		CHECK(!bots_items_should_go(2, 300, 100, 0), "normal at 300 / 100");
		CHECK(bots_items_should_go(5, 120, 100, 0) && !bots_items_should_go(5, 140, 100, 0), "legendary's margin");
		CHECK(bots_items_should_go(3, 160, 100, 0) && !bots_items_should_go(3, 170, 100, 0), "heroic's margin");
		return 0;
	}
	fprintf(stderr, "no such case: %s\n", case_name);
	return 2;
}
