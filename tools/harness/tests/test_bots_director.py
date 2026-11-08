"""The bots' roster rules (port/linux/game/bots_director.c, compiled as it is): how many bots a game wants (off, fixed, fill), the team of a new
bot, which bot goes (dead first, then by team size and score, a carrier last), one change at a time after a grace, and a goal given up."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run  # noqa: E402

CASES = ["wanted", "team", "leaver", "step", "stale"]

NEGATIVE_CONTROLS = {
    "off-wants": (("	if (mode == _bots_director_fixed)\n		wanted = count;", "	if (mode == _bots_director_fixed || mode == _bots_director_off)\n		wanted = count;"), "wanted"),
    "fill-ignores-humans": (("wanted = fill_to - humans;", "wanted = fill_to;"), "wanted"),
    "maximum-ignored": (("	if (wanted > maximum)\n		wanted = maximum;\n", ""), "wanted"),
    "room-ignored": (("	if (wanted > room)\n		wanted = room;\n", ""), "wanted"),
    "negative-wanted": (("	return wanted < 0 ? 0 : wanted;", "	return wanted;"), "wanted"),
    "team-wrong-way": (("return team1_players < team0_players ? 1 : 0;", "return team1_players > team0_players ? 1 : 0;"), "team"),
    "carrier-goes-first": (("(bots[index].carrying ? 1000000L : 0L)", "(bots[index].carrying ? -1000000L : 0L)"), "leaver"),
    "live-before-dead": (("(bots[index].dead ? 0L : 100000L)", "(bots[index].dead ? 100000L : 0L)"), "leaver"),
    "high-score-goes": (("(long)bots[index].score + 5000L", "-(long)bots[index].score + 5000L"), "leaver"),
    "small-team-goes": (("team_players[bots[index].team & 1] < team_players[1 - (bots[index].team & 1)] ? 10000L : 0L", "team_players[bots[index].team & 1] > team_players[1 - (bots[index].team & 1)] ? 10000L : 0L"), "leaver"),
    "carrier-forbidden-ignored": (("(bots[index].carrying && !allow_carrier)", "0"), "leaver"),
    "no-grace": (("	if (ticks_since_change < grace_ticks)\n		return 0;\n", ""), "step"),
    "removes-too-late": (("	if (present > wanted)\n		return -1;", "	if (present > wanted + 1)\n		return -1;"), "step"),
    "holders-given-up": (("return !holds_a_place && age_ticks > limit_ticks;", "return age_ticks > limit_ticks;"), "stale"),
}


def generated(fault=None):
    code = read("port/linux/game/bots_director.c")
    if fault:
        code = mutated(code, *fault)
    return (("bots_director.h", read("port/linux/game/bots_director.h")), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("bots_director", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("bots_director", generated(fault)), case)
    assert status == CHECK_FAILED, f"the roster rules with a fault ({control}) passed '{case}': the test cannot see it"
