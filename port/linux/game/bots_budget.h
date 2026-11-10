/* bots_budget.h: the bots' share of a tick (port/linux/game/bots_budget.c)

The bots run on the game thread inside the tick, where a joined or hosted game already spends most of a frame. Their
brain is given a budget: a most that one tick of all the bots may cost, a mean to hold, a cap on how many bots there
may be, and the places where the brain asks "may I do more this tick?" (the sight tests of a look, a path search, a
goal decision, each bot's turn). When a tick or the mean goes over, the load is shed in four steps, in this order:

    1  the bots look half as often (twice the ticks between two looks)
    2  they decide their goals half as often
    3  path searches only every other tick
    4  the bots farthest from the players hold their last controls for up to 3 ticks (a visibly dull moment, no crash)

and when the cap is broken three ticks running with the four steps already taken, the budget asks for a bot fewer
(bots_budget_take_shed_request: the director's, to come). The levels come back one at a time once the mean has been
well under the target for a while.

Two clocks measure the cost: the CLOCK mode counts microseconds (the console and the desktop, set by bots.budget_us), the
CREDIT mode counts units of work (bots.budget_credit: a sight test, a candidate, a path node, a goal decision, a bot's
motor cost each a fixed number), which is the same every run, so that the tests and the headless runs shed at the same
tick every time. With neither set the budget is OFF: nothing is limited and nothing is shed, the cost is only counted
(the bots play as they did before there was a budget). No setting has a default other than off until the console has
measured what the bots cost (reports/research/2026-11-07/design/bots.md section 14.3).

Plain data and rules with no call into the engine, so that the harness runs them as they are
(tools/harness/tests/test_bots_budget.py). */
#ifndef HALO_BOTS_BUDGET_H
#define HALO_BOTS_BUDGET_H

enum
{
	_bots_budget_off = 0,
	_bots_budget_clock,
	_bots_budget_credit,
};

/* the work, in credit (a tenth of the design's units, so that a path node is whole) */
enum
{
	BOTS_BUDGET_SIGHT_TEST = 80,
	BOTS_BUDGET_CANDIDATE = 10,
	BOTS_BUDGET_SEARCH_NODE = 5,
	BOTS_BUDGET_DECISION = 200,
	BOTS_BUDGET_MOTOR = 20,
};

/* the steps of the shedding (0: none) */
#define BOTS_BUDGET_TOP_LEVEL 4
/* the ticks a bot whose turn comes after the tick's cap was reached holds its last controls before it may think regardless (so that none is starved), and
how many may do that in one tick (so that those held together do not all think together) */
#define BOTS_BUDGET_OVER_HOLD_TICKS 6
#define BOTS_BUDGET_FORCED_PER_TICK 2
/* the ticks the last controls of a far bot are held at the top level */
#define BOTS_BUDGET_HOLD_TICKS 3
/* the ticks of the mean, and the least between two steps up, and before one back down */
#define BOTS_BUDGET_MEAN_TICKS 60
#define BOTS_BUDGET_STEP_UP_TICKS 30
#define BOTS_BUDGET_STEP_DOWN_TICKS 120
/* the ticks the cap may be broken running before the shedding steps up */
#define BOTS_BUDGET_OVER_RUN 3

struct bots_budget
{
	short mode;
	/* the most a tick may cost, and the mean to hold (microseconds or credit); the cap is 0 when off */
	long cap;
	long target;
	/* the bots allowed, and the path searches a tick (the stock three) */
	short maximum_bots;
	short searches_per_tick;

	/* this tick */
	long tick;
	long spent;
	short searches;
	/* the bots that thought this tick after the cap was reached, because they had been held long enough */
	short forced;

	/* the history */
	float mean;
	short over_run;
	short level;
	long level_tick;

	/* totals, for the log */
	long ticks;
	long ticks_over_cap;
	long steps_up;
	long steps_down;
	long deferred;

	/* a step to say, and a bot to shed (each said once) */
	short change_pending;
	char const *change_reason;
	short shed_pending;
};

/* the budget for a mode (off, clock or credit) with its cap (0 for the mode off); the target to hold is 2/5 of it, as the design's
1.2 ms is of its 3.0 ms; maximum_bots is the cap on the bots */
void bots_budget_initialize(struct bots_budget *budget, short mode, long cap, short maximum_bots, short searches_per_tick);
void bots_budget_begin_tick(struct bots_budget *budget, long tick);
/* work done this tick (credit mode counts it; the clock mode counts it too, for the log, but measures time) */
void bots_budget_spend(struct bots_budget *budget, long credit);
/* the clock mode: the microseconds this tick has taken so far (a poll between the bots) */
void bots_budget_elapsed(struct bots_budget *budget, long microseconds);
/* the cap of the tick has been reached and this bot (held for `held_ticks` ticks running) holds its last controls instead of thinking: the guard that bounds a
tick's cost at the cap and a few bots' thinks, however many bots there are. A bot held BOTS_BUDGET_OVER_HOLD_TICKS ticks thinks regardless, as far as
BOTS_BUDGET_FORCED_PER_TICK of them a tick. Never when off. */
int bots_budget_hold_over(struct bots_budget *budget, int held_ticks);
/* the tick's cap is reached: no more work this tick. Never when off. */
int bots_budget_over(struct bots_budget const *budget);
/* one more path search may be made this tick (under the step 3 only every other tick), and a search made */
int bots_budget_search_allowed(struct bots_budget const *budget);
void bots_budget_note_search(struct bots_budget *budget);
/* work left undone this tick because the budget was over (the log) */
void bots_budget_note_deferred(struct bots_budget *budget);
/* at the end of the tick: its cost (microseconds in the clock mode, ignored in the credit mode, which uses what was spent); steps
the shedding up or down */
void bots_budget_end_tick(struct bots_budget *budget, long microseconds);
/* the shedding's steps: how many times as long between looks, between goal decisions, and whether searches are every other tick
(see above); 1 and 1 and 0 at step 0 */
int bots_budget_look_scale(struct bots_budget const *budget);
int bots_budget_decision_scale(struct bots_budget const *budget);
/* the ticks a far bot holds its last controls (0 below the top step) */
int bots_budget_hold_ticks(struct bots_budget const *budget);
/* a step taken since the last ask: TRUE with the level now and why, once */
int bots_budget_take_change(struct bots_budget *budget, short *level, char const **reason);
/* the director should shed a bot (the cap broken at the top step): TRUE once for each time */
int bots_budget_take_shed_request(struct bots_budget *budget);
/* the bots allowed */
short bots_budget_allowed_bots(struct bots_budget const *budget);
/* the cap on the bots after a stretch of frames at a mean rate (the design's rule: one fewer under 40 a second, one more back
over 52, down to 1 and up to maximum); a rate of 0 or less is unknown and changes nothing */
short bots_budget_cap_for_frame_rate(short cap, short maximum, float frames_per_second);

#endif
