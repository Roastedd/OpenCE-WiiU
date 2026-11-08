"""The bots' objective play and item timers (port/linux/game/bots_objectives.c, compiled as it is): capture the flag (grab, capture, wait for the own
flag, hunt the carrier, return a dropped flag, escort, defend), oddball, king, roles by order, the skill's weight, the engine's spawn rule for items, the
class of a name, who wants what and when to set out."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run  # noqa: E402

CASES = ["roles", "ctf-start", "ctf-carry", "ctf-defend", "ctf-no-flags", "oddball", "king", "weight", "spawn-timer", "item-classes", "item-wanted",
         "item-go"]

NEGATIVE_CONTROLS = {
    "role-counts-humans": (("if (!state->member[index].bot || state->member[index].team != state->member[me].team)", "if (state->member[index].team != state->member[me].team)"), "roles"),
    "role-counts-other-teams": (("if (!state->member[index].bot || state->member[index].team != state->member[me].team)", "if (!state->member[index].bot)"), "roles"),
    "ctf-wrong-flag": (("int team = m->team & 1, enemy = 1 - team;", "int team = m->team & 1, enemy = team;"), "ctf-start"),
    "dead-play": (("if (me < 0 || me >= state->member_count || !state->member[me].alive)", "if (me < 0 || me >= state->member_count)"), "ctf-start"),
    "carrier-goes-for-the-flag": (("set_goal(goal, _bots_objective_capture, own->home, -1);", "set_goal(goal, _bots_objective_grab, theirs->p, -1);"), "ctf-carry"),
    "wait-ignored": (("if (own->at_home || !state->flag_at_home_to_score)", "if (1)"), "ctf-carry"),
    "variant-ignored": (("if (own->at_home || !state->flag_at_home_to_score)", "if (own->at_home)"), "ctf-carry"),
    "no-hunt": (("set_goal(goal, _bots_objective_hunt, state->member[own->carrier].p, own->carrier);", "set_goal(goal, _bots_objective_defend, own->home, -1);"),
                "ctf-defend"),
    "return-never": (("set_goal(goal, _bots_objective_return, own->p, -1);", "set_goal(goal, _bots_objective_defend, own->home, -1);"), "ctf-defend"),
    "flagless-play": (("if (!own->present || !theirs->present)\n		return;", ""), "ctf-no-flags"),
    "ball-carrier-hunts": (("	if (state->member[me].carrying_ball)\n		return;\n", ""), "oddball"),
    "ball-guards-enemy": (("if (state->teams && state->member[state->ball_carrier].team == state->member[me].team)", "if (0)"), "oddball"),
    "hill-farthest": (("if (distance < best_distance)", "if (distance > best_distance || best < 0)"), "king"),
    "weights-flat": (("static int const percent[6] = { 30, 50, 75, 90, 100, 100 };", "static int const percent[6] = { 100, 100, 100, 100, 100, 100 };"), "weight"),
    "spawn-off-by-one": (("return into == 0 ? 0 : period - into;", "return into == 0 ? 0 : period - into - 1;"), "spawn-timer"),
    "period-zero": (("long period = (period_seconds > 0 ? period_seconds : 30) * 30;", "long period = (period_seconds > 0 ? period_seconds : 60) * 30;"), "spawn-timer"),
    "rockets-plain": (("strstr(lower, \"rocket\") || ", ""), "item-classes"),
    "case-sensitive": (("lower[k] = (name[k] >= 'A' && name[k] <= 'Z') ? (char)(name[k] - 'A' + 'a') : name[k];", "lower[k] = name[k];"), "item-classes"),
    "power-from-recruit": (("return skill >= 2;", "return skill >= 0;"), "item-wanted"),
    "powerups-from-normal": (("return skill >= 3;", "return skill >= 2;"), "item-wanted"),
    "margin-flat": (("long margin = skill >= 4 ? 30 : skill >= 3 ? 60 : 180;", "long margin = 60;"), "item-go"),
    "present-ignored": (("	if (present)\n		return 1;\n	return ticks_until_spawn", "	return ticks_until_spawn"), "item-go"),
}


def generated(fault=None):
    code = read("port/linux/game/bots_objectives.c")
    if fault:
        code = mutated(code, *fault)
    return (("bots_objectives.h", read("port/linux/game/bots_objectives.h")), ("bots_nav.h", read("port/linux/game/bots_nav.h")), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("bots_objectives", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("bots_objectives", generated(fault)), case)
    assert status == CHECK_FAILED, f"the objective play with a fault ({control}) passed '{case}': the test cannot see it"
