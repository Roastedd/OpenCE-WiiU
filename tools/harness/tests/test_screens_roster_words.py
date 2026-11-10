"""What VOTE TO KICK and CHANGE TEAM do on the Players page (port/linux/game/menu_screens.inl): the vote goes to the game's own (network_votekick_machine) against another machine's player once the
game is under way, with the words of every place it cannot be; a player changes their own team in the lobby of a game with teams (the lobby's SWITCH TEAM, by its controller), never another's or
a bot's, with the words. They answered "THAT IS NOT AVAILABLE IN THIS BUILD YET." before."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run

CASES = ["vote-in-game", "vote-refused", "team-own", "team-refused"]


def generated(fault=None):
    inl = read("port/linux/game/menu_screens.inl")
    code = "\n".join([function(inl, "scr_game_vote_kick"), function(inl, "scr_game_change_team")])
    if fault:
        code = mutated(code, *fault)
    return (("roster_words_code.inc", code),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("screens_roster_words", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", [
    # a vote against oneself or a bot or a player of this machine
    ("	if ((row->flags & (SCREENS_ROW_BOT | SCREENS_ROW_ME | SCREENS_ROW_LOCAL)) || !scr_game_players(&game, &state))", "	if (!scr_game_players(&game, &state))", "vote-refused"),
    # a vote in a lobby (the game has no players of its own yet)
    ("	if (state <= _client_state_pregame)\n	{\n		scr_answer(answer, size, \"A VOTE CAN BE CALLED", "	if (0)\n	{\n		scr_answer(answer, size, \"A VOTE CAN BE CALLED", "vote-refused"),
    # a vote that did not go said as done
    ("	if (!network_votekick_machine((short)row->machine))", "	if (network_votekick_machine((short)row->machine) && 0)", "vote-in-game"),
    # the vote against the wrong machine
    ("network_votekick_machine((short)row->machine)", "network_votekick_machine(0)", "vote-in-game"),
    # another person's team changed
    ("	if (!(row->flags & SCREENS_ROW_ME))\n	{\n		scr_answer(answer, size, \"ONLY A PLAYER CAN CHANGE THEIR OWN TEAM.\");", "	if (0)\n	{\n		scr_answer(answer, size, \"ONLY A PLAYER CAN CHANGE THEIR OWN TEAM.\");", "team-refused"),
    # a bot's team changed
    ("	if (row->flags & SCREENS_ROW_BOT)\n	{\n		scr_answer(answer, size, \"A BOT'S TEAM", "	if (0)\n	{\n		scr_answer(answer, size, \"A BOT'S TEAM", "team-refused"),
    # a game with no teams
    ("	if (!game->variant.universal_variant.teams)\n	{", "	if (0)\n	{", "team-refused"),
    # a game under way
    ("	if (state != _client_state_pregame)\n	{\n		scr_answer(answer, size, \"TEAMS CAN BE CHANGED", "	if (0)\n	{\n		scr_answer(answer, size, \"TEAMS CAN BE CHANGED", "team-refused"),
    # the wrong controller
    ("ui_widget_port_swap_team(row->controller >= 0 ? row->controller : 0)", "ui_widget_port_swap_team(0)", "team-own"),
])
def test_negative_control(before, after, case):
    status, output = run(build("screens_roster_words", generated((before, after))), case)
    assert status == CHECK_FAILED, output
