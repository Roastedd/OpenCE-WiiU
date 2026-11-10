"""The GamePad social view's KEYBOARD button (screens_social.c) asks network_chat.c for its on-screen keyboard; the line typed goes to the friends layer's handler, not to the game, for the party's
chat and a friend's code. network_chat_update is too large to run apart from the game, so this reads the source for the order of what matters and shows that each part, left out, is seen."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import function, read


def facts(source):
    update = function(source, "chat_update")
    request = function(source, "network_chat_keyboard_request")
    return {
        # the request sets a flag with its destination
        "request": "chat_keyboard_requested = TRUE;" in request and "chat_keyboard_destination = (short)destination;" in request,
        # the update opens the keyboard for it, as D-pad Up does, and clears the flag
        "opens": "|| chat_keyboard_requested) && !console_is_active())" in update and "chat_keyboard_requested = FALSE;" in update,
        # the game's chat being off stops the game's chat only
        "gate": "if (destination == NETWORK_CHAT_TO_GAME && !TEST_FLAG(chat_config_flags(), _chat_config_allowed_bit))" in update,
        # the line's destination is kept
        "kept": "chat_input.destination = destination;" in update,
        # Enter hands a line for the friends layer to the handler and says nothing in the game
        "enter": "if (chat_input.destination != NETWORK_CHAT_TO_GAME)" in update and "chat_line_handler(chat_input.destination, chat_input.text);" in update
                 and update.index("chat_line_handler(chat_input.destination") < update.index("chat_send(chat_input.channel, chat_input.text, TRUE, TRUE);"),
    }


def test_the_keyboard_request_reaches_the_keyboard_and_its_line_the_friends_layer():
    source = read("port/linux/game/network_chat.c")
    assert all(facts(source).values()), facts(source)


def test_each_part_left_out_is_seen():
    source = read("port/linux/game/network_chat.c")
    for before, after, key in (
        ("chat_keyboard_requested = TRUE;", "", "request"),
        ("|| chat_keyboard_requested) && !console_is_active())", ") && !console_is_active())", "opens"),
        ("if (destination == NETWORK_CHAT_TO_GAME && !TEST_FLAG", "if (!TEST_FLAG", "gate"),
        ("chat_input.destination = destination;", "", "kept"),
        ("chat_line_handler(chat_input.destination, chat_input.text);", "", "enter"),
    ):
        assert source.count(before) == 1, before
        assert not facts(source.replace(before, after))[key], f"{key} not seen"
