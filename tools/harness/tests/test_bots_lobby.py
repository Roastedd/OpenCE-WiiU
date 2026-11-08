"""The real lobby of network_server_manager.c's bots (bots.count): as many as the setting asks for join a local Slayer
game's lobby, as far as it has room, on the teams bots.teams asks for, follow the players' team changes and leave when
fewer are asked for; none in a game other machines may join, in another gametype, or once the game has started."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["fills", "room", "fewer", "even", "against", "follows-players", "never-network", "slayer-only",
         "not-once-started", "no-player-no-bots", "names-and-machines", "objectives-games", "hosted-off", "hosted-lobby", "hosted-ingame", "hosted-ingame-off"]
GAME_MANAGER_FUNCTIONS = ("network_game_invalidate_player", "network_player_is_valid", "network_game_player_is_valid",
                          "network_game_add_player", "network_game_remove_player")

# a fault in network_game_server_port_bots, and the case that must catch it
NEGATIVE_CONTROLS = {
    "bots-in-network-games": (("else if (bots_online_enabled() && bots_game_allowed(", "else if (bots_game_allowed("), "hosted-off"),
    "bots-in-any-gametype": (("if (bots_game_allowed(server->game.variant.game_engine_index))\n			wanted = PIN(bots_wanted_count(), 0, BOTS_MAXIMUM);", "wanted = PIN(bots_wanted_count(), 0, BOTS_MAXIMUM);"), "slayer-only"),
    "bots-once-started": (("boolean pregame = server->state == _network_game_server_state_pregame && !server->sent_start_game_message;",
                           "boolean pregame = server->state == _network_game_server_state_pregame;"), "not-once-started"),
    "local-game-ingame": (("if (local ? !pregame : !(pregame || (bots_online_enabled() && ingame)))", "if (!pregame && !ingame)"), "not-once-started"),
    "ingame-all-at-once": (("		return;\n	}\n\n	/* those not wanted leave (the last first) */", "	}\n\n	/* those not wanted leave (the last first) */"), "hosted-ingame"),
    "ingame-no-clock": (("if (!bots_host_change_allowed((long)system_milliseconds(), 4000) )\n			return;", ""), "hosted-ingame"),
    "teams-not-evened": (("team_counts[1] < team_counts[0] ? 1 : 0", "1"), "even"),
    "against-the-players": (("team_index = human_team == 0 ? 1 : 0;\n		else\n			team_index = team_counts[1] < team_counts[0] ? 1 : 0;", "team_index = human_team;\n		else\n			team_index = team_counts[1] < team_counts[0] ? 1 : 0;"), "against"),
    "one-too-many-stays": (("bot_index >= wanted; bot_index--", "bot_index > wanted; bot_index--"), "fewer"),
    "bots-alone": (("if (human_team == NONE)\n\t\twanted = 0;", ""), "no-player-no-bots"),
    "teams-not-followed": (("else if (server->game.players[slot_of_bot[bot_index]].team_index != team_index)",
                            "else if (0)"), "follows-players"),
}


def generated(fault=None):
    server = read("source/networking/network_server_manager.c")
    manager = read("source/networking/network_game_manager.c")
    bots_header = read("port/linux/game/bots.h")
    bots = read("port/linux/game/bots.c")
    limits = read("port/linux/include/halo_port_limits.h")
    config = ""
    for name in ("HALO_PORT_MAXIMUM_NETWORK_MACHINES", "HALO_PORT_MAXIMUM_NETWORK_PLAYERS"):
        config += f"#define {name} {constant(limits, name)}\n"
    config += f"#define MAXIMUM_LOCAL_PLAYERS {constant(read('source/game/players.h'), 'MAXIMUM_LOCAL_PLAYERS')}\n"
    config += f"#define NUMBER_OF_MULTIPLAYER_TEAMS {constant(server, 'NUMBER_OF_MULTIPLAYER_TEAMS')}\n"
    config += f"#define BOTS_MAXIMUM {constant(bots_header, 'BOTS_MAXIMUM')}\n"
    match = re.search(r"^#define BOTS_FIRST_MACHINE .*$", bots_header, re.M)
    assert match, "BOTS_FIRST_MACHINE not found in bots.h"
    config += match.group(0) + "\n"
    config += enum_with(bots_header, "_bots_teams_against") + "\n"
    config += enum_with(server, "_network_game_server_state_pregame") + "\n"
    config += enum_with(read("source/game/game_engine.h"), "game_engine_slayer") + "\n"
    config += structure(read("source/game/players.h"), "network_player") + "\n"
    code = ""
    for name in GAME_MANAGER_FUNCTIONS:
        code += function(manager, name) + "\n"
    for name in ("bots_machine_is_bot", "bots_network_player"):
        code += function(bots, name) + "\n"
    director = read("port/linux/game/bots_director.c").replace('#include "bots_director.h"', "")
    code += director + "\n"
    lobby = function(server, "network_game_server_port_bots")
    if fault:
        lobby = mutated(lobby, *fault)
    code += lobby + "\n"
    return (("config.inc", config), ("bots_director.h", read("port/linux/game/bots_director.h")), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("bots_lobby", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("bots_lobby", generated(fault)), case)
    assert status == CHECK_FAILED, f"the bots' lobby with a fault ({control}) passed '{case}': the test cannot see it"
