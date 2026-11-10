"""What ADD BOT and DROP BOT answer on the Players page and in the lobby, in each game a machine can be in. On the first console session the owner, hosting an internet game, was told
"BOTS CAN BE CHANGED IN THE LOBBY OF A GAME ON THIS MACHINE", which was no help: bots are not used in hosted or internet games at all. The words now say what is true: bots play in a
game on this machine, are set in its lobby before it starts (so not from the pause menu once it is going), and are not used in hosted or online games."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run

CASES = ["local-lobby", "local-gametype", "local-running", "hosted", "hosted-online-bots", "not-the-host"]


def generated(fault=None):
    inl = read("port/linux/game/menu_screens.inl")
    code = function(inl, "scr_game_bots_change")
    if fault:
        code = mutated(code, *fault)
    return (("bots_words_code.inc", code),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("screens_bots_words", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", [
    # the old words for a hosted game
    ("BOTS ARE NOT USED IN HOSTED OR ONLINE GAMES, ONLY IN A LOCAL GAME.", "BOTS CAN BE CHANGED IN THE LOBBY OF A GAME ON THIS MACHINE.", "hosted"),
    # a hosted game told as a local one that has started
    ("	if (!network_game_is_splitscreen_local())\n	{", "	if (0)\n	{", "hosted"),
    # a game that is going told nothing
    ("BOTS ARE CHANGED IN THE LOBBY, BEFORE THE GAME STARTS.", "", "local-running"),
    # a bot "added" in a game type that has none
    ("	if (!bots_game_allowed(game->variant.game_engine_index))\n	{", "	if (0)\n	{", "local-gametype"),
    # the words for a game someone else hosts
    ("BOTS ARE ONLY FOR A LOCAL GAME ON THIS MACHINE, NOT ONLINE PLAY.", "BOTS CAN BE CHANGED BY THE HOST OF THE GAME.", "not-the-host"),
    # the bots.online path lost
    ("if (scr_game_bots_hosted() && state >= _client_state_pregame)", "if (0)", "hosted-online-bots"),
])
def test_negative_control(before, after, case):
    status, output = run(build("screens_bots_words", generated((before, after))), case)
    assert status == CHECK_FAILED, output
