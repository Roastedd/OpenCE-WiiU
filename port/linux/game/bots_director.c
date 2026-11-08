/*
BOTS_DIRECTOR.C

The roster rules of bots_director.h.
*/

#include "bots_director.h"

int bots_director_wanted(int mode, int count, int fill_to, int humans, int maximum, int room)
{
	int wanted = 0;

	if (mode == _bots_director_fixed)
		wanted = count;
	else if (mode == _bots_director_fill)
		wanted = fill_to - humans;
	if (wanted > maximum)
		wanted = maximum;
	if (wanted > room)
		wanted = room;
	return wanted < 0 ? 0 : wanted;
}

int bots_director_pick_team(int team0_players, int team1_players)
{
	return team1_players < team0_players ? 1 : 0;
}

int bots_director_pick_leaver(struct bots_director_bot const *bots, int count, int allow_carrier)
{
	int index, team_players[2] = { 0, 0 }, best = -1;
	long best_key = 0;

	for (index = 0; index < count; index++)
		if (bots[index].present)
			team_players[bots[index].team & 1]++;
	for (index = 0; index < count; index++)
	{
		long key;

		if (!bots[index].present || (bots[index].carrying && !allow_carrier))
			continue;
		/* (the key, the smaller the better: a carrier last, then the living, the smaller team, the higher score) */
		key = (bots[index].carrying ? 1000000L : 0L) + (bots[index].dead ? 0L : 100000L) + (team_players[bots[index].team & 1] < team_players[1 - (bots[index].team & 1)] ? 10000L : 0L) +
			(long)bots[index].score + 5000L - (long)index / 1000L;
		if (best < 0 || key < best_key)
		{
			best = index;
			best_key = key;
		}
	}
	return best;
}

int bots_director_step(int wanted, int present, long ticks_since_change, long grace_ticks)
{
	if (ticks_since_change < grace_ticks)
		return 0;
	if (present < wanted)
		return 1;
	if (present > wanted)
		return -1;
	return 0;
}

int bots_director_goal_stale(long age_ticks, long limit_ticks, int holds_a_place)
{
	return !holds_a_place && age_ticks > limit_ticks;
}
