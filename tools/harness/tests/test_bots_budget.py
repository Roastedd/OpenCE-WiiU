"""The bots' share of a tick (port/linux/game/bots_budget.c, compiled as it is): off it limits and sheds nothing; in the credit
mode (the same every run) a tick over its cap ends its work, three over running step the shedding up in its order (look less
often, decide less often, search every other tick, hold the far bots' controls), a mean over the target does too, the steps come
back one at a time, and the cap broken at the top step asks for a bot fewer."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run  # noqa: E402

CASES = ["off", "cap-of-a-tick", "clock", "steps-in-order", "searches-every-other-tick", "sheds-a-bot", "mean-over-the-target",
         "steps-back-down", "frame-rate", "deferred"]

# a fault in bots_budget.c, and the case that must catch it
NEGATIVE_CONTROLS = {
    "off-is-over": (("return budget->mode != _bots_budget_off && budget->spent >= budget->cap;", "return budget->spent >= budget->cap;"),
                    "off"),
    "off-sheds": (("\tif (budget->mode == _bots_budget_off)\n\t\treturn;\n\tif (cost > budget->cap)", "\tif (cost > budget->cap)"), "off"),
    "capless-mode-is-on": (("\tif (cap <= 0)\n\t\tbudget->mode = _bots_budget_off;\n", ""), "off"),
    "cap-not-reached": (("budget->spent >= budget->cap;", "budget->spent > budget->cap;"), "cap-of-a-tick"),
    "search-over-the-cap": (("if (budget->searches >= budget->searches_per_tick || bots_budget_over(budget))",
                             "if (budget->searches >= budget->searches_per_tick)"), "cap-of-a-tick"),
    "target-not-two-fifths": (("budget->target = budget->cap * 2 / 5;", "budget->target = budget->cap / 2;"), "cap-of-a-tick"),
    "clock-takes-credit": (("long cost = budget->mode == _bots_budget_clock ? microseconds : budget->spent;",
                            "long cost = budget->spent;"), "clock"),
    "clock-poll-goes-back": (("if (budget->mode == _bots_budget_clock && microseconds > budget->spent)",
                              "if (budget->mode == _bots_budget_clock)"), "clock"),
    "credit-takes-the-clock": (("if (budget->mode == _bots_budget_clock && microseconds > budget->spent)",
                                "if (microseconds > budget->spent)"), "clock"),
    "steps-after-two": (("if (budget->over_run >= BOTS_BUDGET_OVER_RUN)", "if (budget->over_run >= 2)"), "steps-in-order"),
    "run-not-broken-by-a-good-tick": (("\telse\n\t\tbudget->over_run = 0;\n\tif (budget->over_run >=", "\tif (budget->over_run >="),
                                      "steps-in-order"),
    "decisions-at-step-one": (("return budget->level >= 2 ? 2 : 1;", "return budget->level >= 1 ? 2 : 1;"), "steps-in-order"),
    "holds-at-step-three": (("return budget->level >= BOTS_BUDGET_TOP_LEVEL ? BOTS_BUDGET_HOLD_TICKS : 0;",
                             "return budget->level >= 3 ? BOTS_BUDGET_HOLD_TICKS : 0;"), "steps-in-order"),
    "change-said-twice": (("\tbudget->change_pending = 0;\n\t*level", "\t*level"), "steps-in-order"),
    "searches-every-tick": (("if (budget->level >= 3 && (budget->tick & 1))", "if (budget->level >= 4 && (budget->tick & 1))"),
                            "searches-every-other-tick"),
    "shed-past-the-top": (("if (budget->level < BOTS_BUDGET_TOP_LEVEL)\n\t\t\tbots_budget_step(budget, 1, \"the tick passed",
                           "if (1)\n\t\t\tbots_budget_step(budget, 1, \"the tick passed"), "sheds-a-bot"),
    "shed-asked-twice": (("\tbudget->shed_pending = 0;\n\treturn 1;", "\treturn 1;"), "sheds-a-bot"),
    "mean-steps-at-once": (("budget->tick - budget->level_tick >= BOTS_BUDGET_STEP_UP_TICKS)", "1)"), "mean-over-the-target"),
    "mean-under-steps-up": (("else if (budget->mean > (float)budget->target &&", "else if (budget->mean >= 0.0f &&"),
                            "mean-over-the-target"),
    "steps-down-at-once": (("budget->tick - budget->level_tick >= BOTS_BUDGET_STEP_DOWN_TICKS)", "1)"), "steps-back-down"),
    "steps-down-while-over": (("budget->level > 0 && budget->over_run == 0 &&", "budget->level > 0 &&"), "steps-back-down"),
    "frame-rate-below-one": (("if (frames_per_second < 40.0f && cap > 1)", "if (frames_per_second < 40.0f)"), "frame-rate"),
    "frame-rate-unknown": (("\tif (frames_per_second <= 0.0f)\n\t\treturn cap;\n", ""), "frame-rate"),
    "deferred-uncounted": (("\tbudget->deferred++;", ""), "deferred"),
}


def generated(fault=None):
    code = read("port/linux/game/bots_budget.c")
    if fault:
        code = mutated(code, *fault)
    return (("bots_budget.h", read("port/linux/game/bots_budget.h")), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("bots_budget", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("bots_budget", generated(fault)), case)
    assert status == CHECK_FAILED, f"the budget with a fault ({control}) passed '{case}': the test cannot see it"
