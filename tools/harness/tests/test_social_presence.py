"""What friends are told of this machine's game. On the first console session a split screen game of one player and three bots was told to friends as a joinable game "bloodgulch 4/128": the
game's server listens as any does, the game was in the network server's connection (so not "local"), and internet play, allowed from before, hosted it. A split screen game is now told as
solo by the game, never hosted by internet play whatever listens (p2p_set_game_local, set as the server is made), and the numbers friends see are the game's own (its players, its most; never
the tunnel's 128)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run

CASES = ["hosted-16", "not-said-yet", "local-game", "hosting-wanted", "activity", "private", "menus-after-a-game", "joined-public"]


def generated(fault=None):
    p2p = read("port/linux/src/p2p.c")
    inc = read("port/linux/src/social_p2p.inc")
    inl = read("port/linux/game/menu_screens.inl")
    code = "\n".join([function(p2p, "hosting_wanted"), function(inc, "social_place_make"), function(inl, "scr_social_activity")])
    if fault:
        code = mutated(code, *fault)
    return (("social_codec.h", read("port/linux/game/social_codec.h")), ("social.h", read("port/linux/game/social.h")), ("social_core.h", read("port/linux/game/social_core.h")),
            ("presence_code.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("social_presence", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", [
    # internet play hosts a local game: the listening socket is all it asked
    ("return p2p.hosting_socket >= 0 && !p2p.hosting_lan_only && !p2p.game_local;", "return p2p.hosting_socket >= 0 && !p2p.hosting_lan_only;", "hosting-wanted"),
    # the place of a local game made as a hosted one
    ("if (p2p.hosting && p2p.has_token && !p2p.game_local && place->activity != SOCIAL_SOLO)", "if (p2p.hosting && p2p.has_token && place->activity != SOCIAL_SOLO)", "local-game"),
    # a split screen game told as a game (the console's first run)
    ("return network_game_is_splitscreen_local() || game_connection() == _game_connection_local ? SOCIAL_ACTIVITY_SOLO : SOCIAL_ACTIVITY_GAME;",
     "return game_connection() == _game_connection_local ? SOCIAL_ACTIVITY_SOLO : SOCIAL_ACTIVITY_GAME;", "activity"),
    # the tunnel's 128 as the most
    ("maximum = p2p.game_player_maximum > 0 ? p2p.game_player_maximum : 0;", "maximum = p2p.game_player_maximum > 0 ? p2p.game_player_maximum : 128;", "not-said-yet"),
    # joinable with no most known
    ("open && maximum > 0 && !place->private_game && config_boolean(", "open && !place->private_game && config_boolean(", "not-said-yet"),
    # the main menu's scene counted as a game (the console's "solo" at the main menu after a local game)
    ("if (game_in_progress() && !game_time_menu_scene())", "if (game_in_progress())", "menus-after-a-game"),
    # a leftover split screen server taken for a game
    ("return lobby ? SOCIAL_ACTIVITY_SOLO : SOCIAL_ACTIVITY_MENUS;", "return SOCIAL_ACTIVITY_SOLO;", "menus-after-a-game"),
    # a private game joinable by its presence
    ("!place->private_game && config_boolean(", "config_boolean(", "private"),
    # a game with a password or not listed not private
    ("place->private_game = (unsigned char)(has_password || !p2p_lobby_listed_locked());", "place->private_game = (unsigned char)has_password;", "private"),
    ("place->private_game = (unsigned char)(has_password || !p2p_lobby_listed_locked());", "place->private_game = (unsigned char)!p2p_lobby_listed_locked();", "private"),
    # a joined game's way in told though the host is not reached
    ("		if (joined_listed_invite_locked(hash, token))", "		if (1 || joined_listed_invite_locked(hash, token))", "joined-public"),
    # a full joined game told as joinable
    ("joined.game.players < joined.game.maximum_players &&", "", "joined-public"),
    # a joined game told from the menus
    ("place->activity != SOCIAL_SOLO && (place->activity == SOCIAL_GAME || place->activity == SOCIAL_LOBBY))", "place->activity != SOCIAL_SOLO)", "joined-public"),
    # the game's count not used
    ("count = p2p.game_player_maximum > 0 ? p2p.game_player_count : connected_player_count() + 1;", "count = connected_player_count() + 1;", "hosted-16"),
])
def test_negative_control(before, after, case):
    status, output = run(build("social_presence", generated((before, after))), case)
    assert status == CHECK_FAILED, output


def server_sets_local_before_listening(source):
    create = function(source, "network_game_server_create")
    dispose = function(source, "network_game_server_dispose")
    call = "p2p_set_game_local(!network_game_should_accept_remote_connections());"
    return call in create and create.index(call) < create.index("network_connection_new(") and "p2p_set_game_local(FALSE);" in dispose


def test_the_server_says_it_is_local_before_it_listens_and_not_after_it_is_gone():
    """the order matters: the connection listens as it is made, and internet play hosts a listening game at once; the flag is cleared with the server's counts"""
    source = read("source/networking/network_server_manager.c")
    assert server_sets_local_before_listening(source)
    assert not server_sets_local_before_listening(source.replace("p2p_set_game_local(FALSE);", ""))
    call = "p2p_set_game_local(!network_game_should_accept_remote_connections());"
    statement = "server->connection = network_connection_new(\n\t\t\tFLAG(_connection_create_server_bit),\n\t\t\tNETWORK_GAME_SERVER_PORT);"
    assert statement in source
    assert not server_sets_local_before_listening(source.replace(call, "").replace(statement, statement + "\n\t\t" + call))
    # (the game's own entry: a split screen game's server is made with remote connections refused, before the server is made)
    for path, name in (("source/interface/ui_widget_event_handler_functions.c", "split_screen_game_initialize"), ("port/linux/game/network_test.c", None)):
        text = read(path)
        text = function(text, name) if name else text[text.index("as Multiplayer > Split Screen does"):]
        assert text.index("network_game_accept_remote_connections(FALSE);") < text.index("create_global_network_game_server()"), path
