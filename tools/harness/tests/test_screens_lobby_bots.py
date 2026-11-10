"""The BOTS row of the lobby of a game on this machine (port/linux/game/menu_screens.inl): the count bots.count holds, "(L/R)" when the screens that change it are on, the words for a game
type with no bots, and nothing in a game that is hosted or joined (bots play in a game on this machine only)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run

CASES = ["count", "not-here"]


def generated(fault=None):
    inl = read("port/linux/game/menu_screens.inl")
    code = function(inl, "scr_lobby_bots_text")
    if fault:
        code = mutated(code, *fault)
    return (("lobby_bots_code.inc", code),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("screens_lobby_bots", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", [
    # a hosted game shows bots
    ("	if (!game || !network_game_is_splitscreen_local() || !global_network_game_server_get())", "	if (!game)", "not-here"),
    # the keys shown without the screens
    ('scr_enabled() ? L"  (L/R)" : L""', 'L"  (L/R)"', "count"),
    # a game type with no bots shown a count
    ("	if (!bots_game_allowed(game->variant.game_engine_index))", "	if (0)", "count"),
])
def test_negative_control(before, after, case):
    status, output = run(build("screens_lobby_bots", generated((before, after))), case)
    assert status == CHECK_FAILED, output
