/*
BOTS_BUDGET.C

The bots' share of a tick (bots_budget.h). Plain rules, no engine: the harness compiles this file as it is.
*/

#include "bots_budget.h"

void bots_budget_initialize(struct bots_budget *budget, short mode, long cap, short maximum_bots, short searches_per_tick)
{
	budget->mode = mode;
	/* (a mode without a cap is off) */
	if (cap <= 0)
		budget->mode = _bots_budget_off;
	budget->cap = budget->mode == _bots_budget_off ? 0 : cap;
	budget->target = budget->cap * 2 / 5;
	budget->maximum_bots = maximum_bots;
	budget->searches_per_tick = searches_per_tick;
	budget->tick = 0;
	budget->spent = 0;
	budget->searches = 0;
	budget->forced = 0;
	budget->mean = 0.0f;
	budget->over_run = 0;
	budget->level = 0;
	budget->level_tick = 0;
	budget->ticks = 0;
	budget->ticks_over_cap = 0;
	budget->steps_up = 0;
	budget->steps_down = 0;
	budget->deferred = 0;
	budget->change_pending = 0;
	budget->change_reason = 0;
	budget->shed_pending = 0;
}

void bots_budget_begin_tick(struct bots_budget *budget, long tick)
{
	budget->tick = tick;
	budget->spent = 0;
	budget->searches = 0;
	budget->forced = 0;
}

void bots_budget_spend(struct bots_budget *budget, long credit)
{
	if (credit > 0)
		budget->spent += credit;
}

void bots_budget_elapsed(struct bots_budget *budget, long microseconds)
{
	if (budget->mode == _bots_budget_clock && microseconds > budget->spent)
		budget->spent = microseconds;
}

int bots_budget_over(struct bots_budget const *budget)
{
	return budget->mode != _bots_budget_off && budget->spent >= budget->cap;
}

int bots_budget_hold_over(struct bots_budget *budget, int held_ticks)
{
	if (!bots_budget_over(budget))
		return 0;
	if (held_ticks >= BOTS_BUDGET_OVER_HOLD_TICKS && budget->forced < BOTS_BUDGET_FORCED_PER_TICK)
	{
		budget->forced++;
		return 0;
	}
	return 1;
}

int bots_budget_search_allowed(struct bots_budget const *budget)
{
	if (budget->searches >= budget->searches_per_tick || bots_budget_over(budget))
		return 0;
	/* (the third step: every other tick) */
	if (budget->level >= 3 && (budget->tick & 1))
		return 0;
	return 1;
}

void bots_budget_note_search(struct bots_budget *budget)
{
	budget->searches++;
}

void bots_budget_note_deferred(struct bots_budget *budget)
{
	budget->deferred++;
}

static void bots_budget_step(struct bots_budget *budget, int direction, char const *reason)
{
	budget->level += (short)direction;
	budget->level_tick = budget->tick;
	if (direction > 0)
		budget->steps_up++;
	else
		budget->steps_down++;
	budget->change_pending = 1;
	budget->change_reason = reason;
}

void bots_budget_end_tick(struct bots_budget *budget, long microseconds)
{
	long cost = budget->mode == _bots_budget_clock ? microseconds : budget->spent;

	budget->ticks++;
	/* (the mean over the last BOTS_BUDGET_MEAN_TICKS: the first tick starts it) */
	if (budget->ticks == 1)
		budget->mean = (float)cost;
	else
		budget->mean += ((float)cost - budget->mean) / (float)BOTS_BUDGET_MEAN_TICKS;
	if (budget->mode == _bots_budget_off)
		return;
	if (cost > budget->cap)
	{
		budget->ticks_over_cap++;
		budget->over_run++;
	}
	else
		budget->over_run = 0;
	if (budget->over_run >= BOTS_BUDGET_OVER_RUN)
	{
		budget->over_run = 0;
		if (budget->level < BOTS_BUDGET_TOP_LEVEL)
			bots_budget_step(budget, 1, "the tick passed the cap three ticks running");
		else
			budget->shed_pending = 1;
	}
	else if (budget->mean > (float)budget->target && budget->level < BOTS_BUDGET_TOP_LEVEL &&
		budget->tick - budget->level_tick >= BOTS_BUDGET_STEP_UP_TICKS)
	{
		bots_budget_step(budget, 1, "the mean cost is over the target");
	}
	else if (budget->level > 0 && budget->over_run == 0 && budget->mean < 0.5f * (float)budget->target &&
		budget->tick - budget->level_tick >= BOTS_BUDGET_STEP_DOWN_TICKS)
	{
		bots_budget_step(budget, -1, "the mean cost is well under the target");
	}
}

int bots_budget_look_scale(struct bots_budget const *budget)
{
	return budget->level >= 1 ? 2 : 1;
}

int bots_budget_decision_scale(struct bots_budget const *budget)
{
	return budget->level >= 2 ? 2 : 1;
}

int bots_budget_hold_ticks(struct bots_budget const *budget)
{
	return budget->level >= BOTS_BUDGET_TOP_LEVEL ? BOTS_BUDGET_HOLD_TICKS : 0;
}

int bots_budget_take_change(struct bots_budget *budget, short *level, char const **reason)
{
	if (!budget->change_pending)
		return 0;
	budget->change_pending = 0;
	*level = budget->level;
	*reason = budget->change_reason;
	return 1;
}

int bots_budget_take_shed_request(struct bots_budget *budget)
{
	if (!budget->shed_pending)
		return 0;
	budget->shed_pending = 0;
	return 1;
}

short bots_budget_allowed_bots(struct bots_budget const *budget)
{
	return budget->maximum_bots;
}

short bots_budget_cap_for_frame_rate(short cap, short maximum, float frames_per_second)
{
	if (frames_per_second <= 0.0f)
		return cap;
	if (frames_per_second < 40.0f && cap > 1)
		return (short)(cap - 1);
	if (frames_per_second > 52.0f && cap < maximum)
		return (short)(cap + 1);
	return cap;
}
