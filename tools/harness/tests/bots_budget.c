/*
BOTS_BUDGET.C (test)

The bots' share of a tick (bots_budget.c, compiled as it is): off it limits nothing and sheds nothing; on, in the credit
mode that is the same every run, a tick over its cap ends the tick's work, three such ticks running step the shedding up in
its order, a mean over the target does too, the steps come back one at a time, and a cap broken at the top step asks for a
bot fewer. test_bots_budget.py takes the code and its header from the sources.
*/

#include "harness.h"

#include "bots_budget.h"
#include "under_test.inc"

static struct bots_budget budget;

/* a tick that costs this much (credit), the tick numbered `tick` */
static void spend_tick(long tick, long credit)
{
	bots_budget_begin_tick(&budget, tick);
	bots_budget_spend(&budget, credit);
	bots_budget_end_tick(&budget, 0);
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	long tick;
	short level;
	char const *reason;
	int step;

	/* off (the default): no cap, never over, nothing shed however much it costs, the stock three searches a tick */
	CASE("off")
	{
		bots_budget_initialize(&budget, _bots_budget_off, 0, 15, 3);
		CHECK(budget.mode == _bots_budget_off && budget.cap == 0, "mode %d cap %ld", budget.mode, budget.cap);
		for (tick = 0; tick < 500; tick++)
		{
			bots_budget_begin_tick(&budget, tick);
			bots_budget_spend(&budget, 1000000);
			bots_budget_elapsed(&budget, 900000);
			CHECK(!bots_budget_over(&budget), "off, and over at tick %ld", tick);
			bots_budget_end_tick(&budget, 900000);
		}
		CHECK(budget.level == 0 && budget.steps_up == 0 && !bots_budget_take_change(&budget, &level, &reason) &&
			!bots_budget_take_shed_request(&budget), "off, and shed");
		/* (three searches a tick, and a search is counted even though nothing limits the cost) */
		bots_budget_begin_tick(&budget, 501);
		for (step = 0; step < 3; step++)
		{
			CHECK(bots_budget_search_allowed(&budget), "search %d refused", step);
			bots_budget_note_search(&budget);
		}
		CHECK(!bots_budget_search_allowed(&budget), "a fourth search allowed");
		CHECK(bots_budget_look_scale(&budget) == 1 && bots_budget_decision_scale(&budget) == 1 &&
			bots_budget_hold_ticks(&budget) == 0, "off, and scaled");
		/* (a mode with no cap is off) */
		bots_budget_initialize(&budget, _bots_budget_credit, 0, 15, 3);
		CHECK(budget.mode == _bots_budget_off, "credit mode with no cap is on");
		return 0;
	}
	/* the cap of a tick: credit spent reaches it, the work stops (searches too), a new tick starts again */
	CASE("cap-of-a-tick")
	{
		bots_budget_initialize(&budget, _bots_budget_credit, 1000, 8, 3);
		CHECK(budget.target == 400, "the target is %ld, 2/5 of 1000", budget.target);
		bots_budget_begin_tick(&budget, 10);
		bots_budget_spend(&budget, BOTS_BUDGET_SIGHT_TEST * 5);
		CHECK(!bots_budget_over(&budget) && bots_budget_search_allowed(&budget), "400 of 1000 and over");
		bots_budget_spend(&budget, BOTS_BUDGET_SIGHT_TEST * 7);
		CHECK(budget.spent == 960 && !bots_budget_over(&budget), "960 and over");
		bots_budget_spend(&budget, BOTS_BUDGET_MOTOR * 2);
		CHECK(budget.spent == 1000 && bots_budget_over(&budget), "1000 of 1000 and not over");
		CHECK(!bots_budget_search_allowed(&budget), "a search allowed over the cap");
		bots_budget_begin_tick(&budget, 11);
		CHECK(!bots_budget_over(&budget) && budget.spent == 0 && bots_budget_search_allowed(&budget), "the next tick starts over");
		CHECK(bots_budget_allowed_bots(&budget) == 8, "the bots allowed are %d", bots_budget_allowed_bots(&budget));
		/* a negative credit is nothing */
		bots_budget_spend(&budget, -50);
		CHECK(budget.spent == 0, "a negative credit counted");
		return 0;
	}
	/* the clock mode: the poll of the time so far is the spend; the end of the tick takes the microseconds */
	CASE("clock")
	{
		bots_budget_initialize(&budget, _bots_budget_clock, 3000, 8, 3);
		bots_budget_begin_tick(&budget, 5);
		bots_budget_elapsed(&budget, 2900);
		CHECK(!bots_budget_over(&budget), "2900 us of 3000 and over");
		bots_budget_elapsed(&budget, 3001);
		CHECK(bots_budget_over(&budget), "3001 us of 3000 and not over");
		/* (a poll that went back, a clock stepping back, changes nothing) */
		bots_budget_elapsed(&budget, 10);
		CHECK(bots_budget_over(&budget), "a poll going back cleared it");
		/* (the credit mode ignores the time) */
		bots_budget_initialize(&budget, _bots_budget_credit, 3000, 8, 3);
		bots_budget_begin_tick(&budget, 5);
		bots_budget_elapsed(&budget, 99999);
		CHECK(!bots_budget_over(&budget), "the credit mode took the clock");
		/* (the end of a clock tick is its time, not the credit) */
		bots_budget_initialize(&budget, _bots_budget_clock, 3000, 8, 3);
		for (tick = 0; tick < 3; tick++)
		{
			bots_budget_begin_tick(&budget, tick);
			bots_budget_spend(&budget, 5);
			bots_budget_end_tick(&budget, 5000);
		}
		CHECK(budget.ticks_over_cap == 3 && budget.level == 1, "5000 us ticks over 3000: %ld over, level %d", budget.ticks_over_cap,
			budget.level);
		return 0;
	}
	/* three ticks over the cap running step the shedding up, in order; each step said once with its reason; two over and a
	good one do not count */
	CASE("steps-in-order")
	{
		bots_budget_initialize(&budget, _bots_budget_credit, 1000, 8, 3);
		tick = 0;
		spend_tick(tick++, 1500);
		spend_tick(tick++, 1500);
		spend_tick(tick++, 100);
		spend_tick(tick++, 1500);
		spend_tick(tick++, 1500);
		CHECK(budget.level == 0 && !bots_budget_take_change(&budget, &level, &reason), "two and two over stepped up");
		for (step = 1; step <= BOTS_BUDGET_TOP_LEVEL; step++)
		{
			spend_tick(tick++, 1500);
			spend_tick(tick++, 1500);
			spend_tick(tick++, 1500);
			CHECK(budget.level == step || (step == 1 && budget.level == 1), "step %d: level %d", step, budget.level);
			CHECK(bots_budget_take_change(&budget, &level, &reason) && level == step && reason && reason[0], "step %d not said", step);
			CHECK(!bots_budget_take_change(&budget, &level, &reason), "step %d said twice", step);
			CHECK(!bots_budget_take_shed_request(&budget), "a bot shed at step %d", step);
			/* (what each step does) */
			CHECK(bots_budget_look_scale(&budget) == (step >= 1 ? 2 : 1), "look scale at step %d", step);
			CHECK(bots_budget_decision_scale(&budget) == (step >= 2 ? 2 : 1), "decision scale at step %d", step);
			CHECK(bots_budget_hold_ticks(&budget) == (step >= 4 ? 3 : 0), "hold at step %d", step);
		}
		return 0;
	}
	/* the third step: path searches only on every other tick */
	CASE("searches-every-other-tick")
	{
		bots_budget_initialize(&budget, _bots_budget_credit, 1000, 8, 3);
		budget.level = 2;
		bots_budget_begin_tick(&budget, 20);
		CHECK(bots_budget_search_allowed(&budget), "searches refused at step 2");
		bots_budget_begin_tick(&budget, 21);
		CHECK(bots_budget_search_allowed(&budget), "searches refused at step 2, an odd tick");
		budget.level = 3;
		bots_budget_begin_tick(&budget, 20);
		CHECK(bots_budget_search_allowed(&budget), "an even tick refused at step 3");
		bots_budget_begin_tick(&budget, 21);
		CHECK(!bots_budget_search_allowed(&budget), "an odd tick allowed at step 3");
		return 0;
	}
	/* the top step and the cap broken again: a bot fewer is asked for, once for each run of three */
	CASE("sheds-a-bot")
	{
		bots_budget_initialize(&budget, _bots_budget_credit, 1000, 8, 3);
		budget.level = BOTS_BUDGET_TOP_LEVEL;
		tick = 100;
		spend_tick(tick++, 1500);
		spend_tick(tick++, 1500);
		CHECK(!bots_budget_take_shed_request(&budget), "asked after two");
		spend_tick(tick++, 1500);
		CHECK(bots_budget_take_shed_request(&budget), "not asked after three");
		CHECK(!bots_budget_take_shed_request(&budget), "asked twice for one run");
		CHECK(budget.level == BOTS_BUDGET_TOP_LEVEL && !bots_budget_take_change(&budget, &level, &reason), "stepped past the top");
		spend_tick(tick++, 1500);
		spend_tick(tick++, 1500);
		spend_tick(tick++, 1500);
		CHECK(bots_budget_take_shed_request(&budget), "not asked for the second run");
		return 0;
	}
	/* a mean over the target, with no tick over the cap, steps up, but not within 30 ticks of the last step */
	CASE("mean-over-the-target")
	{
		bots_budget_initialize(&budget, _bots_budget_credit, 1000, 8, 3);
		/* 600 a tick: over the target 400, under the cap 1000 */
		for (tick = 1; tick < BOTS_BUDGET_STEP_UP_TICKS; tick++)
			spend_tick(tick, 600);
		CHECK(budget.level == 0, "stepped up within 30 ticks (level %d)", budget.level);
		spend_tick(tick++, 600);
		CHECK(budget.level == 1 && budget.ticks_over_cap == 0, "not stepped up at tick 30 (level %d)", budget.level);
		CHECK(bots_budget_take_change(&budget, &level, &reason), "the step not said");
		/* (the next step not before another 30 ticks) */
		for (step = 0; step < BOTS_BUDGET_STEP_UP_TICKS - 1; step++)
			spend_tick(tick++, 600);
		CHECK(budget.level == 1, "a second step too soon (level %d)", budget.level);
		spend_tick(tick++, 600);
		CHECK(budget.level == 2, "no second step (level %d)", budget.level);
		/* a mean under the target steps nothing up */
		bots_budget_initialize(&budget, _bots_budget_credit, 1000, 8, 3);
		for (tick = 1; tick < 400; tick++)
			spend_tick(tick, 390);
		CHECK(budget.level == 0, "a mean under the target stepped up (level %d)", budget.level);
		return 0;
	}
	/* the steps come back one at a time, 120 ticks apart, once the mean is under half the target, and not while the cap is being
	broken */
	CASE("steps-back-down")
	{
		bots_budget_initialize(&budget, _bots_budget_credit, 1000, 8, 3);
		budget.level = 3;
		budget.level_tick = 0;
		budget.mean = 600.0f;
		/* cheap ticks bring the mean under 200 after a while */
		for (tick = 1; tick < BOTS_BUDGET_STEP_DOWN_TICKS; tick++)
			spend_tick(tick, 50);
		CHECK(budget.level == 3 && budget.mean < 200.0f, "stepped down within 120 ticks of the last step (level %d), mean %.0f",
			budget.level, budget.mean);
		spend_tick(tick++, 50);
		CHECK(budget.level == 2 && budget.steps_down == 1, "no step down at tick 120 (level %d)", budget.level);
		/* (one at a time: the next not for another 120) */
		for (; tick < 2 * BOTS_BUDGET_STEP_DOWN_TICKS; tick++)
			spend_tick(tick, 50);
		CHECK(budget.level == 2, "two steps down too close (level %d)", budget.level);
		spend_tick(tick++, 50);
		CHECK(budget.level == 1, "no second step down (level %d)", budget.level);
		CHECK(bots_budget_take_change(&budget, &level, &reason) && level == 1, "the step down not said");
		/* a tick over the cap (not yet a run of three), however low the mean, is no time to step down */
		bots_budget_initialize(&budget, _bots_budget_credit, 1000, 8, 3);
		budget.level = 2;
		budget.level_tick = 0;
		budget.ticks = 100;
		budget.mean = 0.0f;
		spend_tick(200, 1500);
		CHECK(budget.level == 2 && budget.mean < 200.0f, "stepped down on a tick over the cap (level %d)", budget.level);
		spend_tick(201, 50);
		CHECK(budget.level == 1, "no step down on the next good tick (level %d)", budget.level);
		return 0;
	}
	/* the cap on the bots for the frame rate: one fewer under 40, one more back over 52, between 1 and the most */
	CASE("frame-rate")
	{
		CHECK(bots_budget_cap_for_frame_rate(8, 15, 39.0f) == 7, "39 fps");
		CHECK(bots_budget_cap_for_frame_rate(1, 15, 20.0f) == 1, "below one bot");
		CHECK(bots_budget_cap_for_frame_rate(7, 15, 45.0f) == 7, "45 fps changes the cap");
		CHECK(bots_budget_cap_for_frame_rate(7, 15, 53.0f) == 8, "53 fps");
		CHECK(bots_budget_cap_for_frame_rate(15, 15, 60.0f) == 15, "past the most");
		CHECK(bots_budget_cap_for_frame_rate(7, 15, 0.0f) == 7 && bots_budget_cap_for_frame_rate(7, 15, -1.0f) == 7,
			"an unknown rate changed the cap");
		return 0;
	}
	/* work left undone is counted for the log */
	CASE("deferred")
	{
		bots_budget_initialize(&budget, _bots_budget_credit, 1000, 8, 3);
		bots_budget_note_deferred(&budget);
		bots_budget_note_deferred(&budget);
		CHECK(budget.deferred == 2, "%ld deferred", budget.deferred);
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
