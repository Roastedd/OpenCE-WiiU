/*
BOTS_DIRECTOR.C (test)

The bots' roster rules (bots_director.c, compiled as it is): how many bots a game wants, the team of a new one, which one goes, whether to add
or remove one now, and a goal given up. test_bots_director.py takes the code and its header from the sources.
*/

#include "harness.h"

#include "bots_director.h"
#include "under_test.inc"

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	/* off is none; fixed is the count whoever is there; fill makes up to the total with the humans in front; never past the maximum or the room */
	CASE("wanted")
	{
		CHECK(bots_director_wanted(_bots_director_off, 6, 12, 2, 15, 10) == 0, "off wants bots");
		CHECK(bots_director_wanted(_bots_director_fixed, 6, 12, 2, 15, 10) == 6, "fixed");
		CHECK(bots_director_wanted(_bots_director_fixed, 20, 12, 2, 15, 30) == 15, "fixed past the maximum");
		CHECK(bots_director_wanted(_bots_director_fill, 6, 12, 2, 15, 10) == 10, "fill: 12 less 2");
		CHECK(bots_director_wanted(_bots_director_fill, 6, 12, 5, 15, 10) == 7, "fill with five humans");
		CHECK(bots_director_wanted(_bots_director_fill, 6, 12, 12, 15, 10) == 0, "fill with a full game of humans");
		CHECK(bots_director_wanted(_bots_director_fill, 6, 12, 14, 15, 10) == 0, "fill with more humans than the total");
		CHECK(bots_director_wanted(_bots_director_fill, 6, 30, 1, 8, 20) == 8, "fill past the maximum");
		CHECK(bots_director_wanted(_bots_director_fill, 6, 12, 2, 15, 4) == 4, "fill past the room");
		CHECK(bots_director_wanted(_bots_director_fixed, 6, 12, 2, 15, -3) == 0, "negative room");
		return 0;
	}
	CASE("team")
	{
		CHECK(bots_director_pick_team(3, 3) == 0 && bots_director_pick_team(3, 2) == 1 && bots_director_pick_team(2, 3) == 0 &&
			bots_director_pick_team(0, 0) == 0, "teams");
		return 0;
	}
	/* a dead bot goes before a live one, a bot with nothing in its hands before a carrier, a larger team's before a smaller's, the lowest score first */
	CASE("leaver")
	{
		struct bots_director_bot bots[6];

		memset(bots, 0, sizeof(bots));
		CHECK(bots_director_pick_leaver(bots, 6, 0) == -1, "a leaver among nobody");
		bots[0].present = bots[1].present = bots[2].present = bots[3].present = 1;
		bots[0].team = 0; bots[0].score = 5;
		bots[1].team = 0; bots[1].score = 2;
		bots[2].team = 0; bots[2].score = 9;
		bots[3].team = 1; bots[3].score = 0;
		CHECK(bots_director_pick_leaver(bots, 6, 0) == 1, "the larger team's lowest score: %d", bots_director_pick_leaver(bots, 6, 0));
		bots[2].dead = 1;
		CHECK(bots_director_pick_leaver(bots, 6, 0) == 2, "a dead one first: %d", bots_director_pick_leaver(bots, 6, 0));
		bots[2].dead = 0;
		bots[1].carrying = 1;
		CHECK(bots_director_pick_leaver(bots, 6, 0) == 0, "the carrier last: %d", bots_director_pick_leaver(bots, 6, 0));
		CHECK(bots_director_pick_leaver(bots, 6, 1) == 0, "the carrier first when carriers may go: %d", bots_director_pick_leaver(bots, 6, 1));
		bots[0].carrying = bots[2].carrying = bots[3].carrying = 1;
		CHECK(bots_director_pick_leaver(bots, 6, 0) == -1, "a carrier went");
		CHECK(bots_director_pick_leaver(bots, 6, 1) >= 0, "no one may go when carriers may");
		return 0;
	}
	/* one change at a time, and not before the grace is over */
	CASE("step")
	{
		CHECK(bots_director_step(5, 3, 1000, 600) == 1 && bots_director_step(3, 5, 1000, 600) == -1 && bots_director_step(4, 4, 1000, 600) == 0 &&
			bots_director_step(4, 5, 1000, 600) == -1, "steps");
		CHECK(bots_director_step(5, 3, 599, 600) == 0 && bots_director_step(5, 3, 600, 600) == 1, "grace");
		return 0;
	}
	CASE("stale")
	{
		CHECK(!bots_director_goal_stale(100, 1350, 0) && bots_director_goal_stale(1351, 1350, 0) && !bots_director_goal_stale(1351, 1350, 1), "stale");
		return 0;
	}
	fprintf(stderr, "no such case: %s\n", case_name);
	return 2;
}
