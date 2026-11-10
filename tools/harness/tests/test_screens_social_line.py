"""A line typed on the GamePad's keyboard (port/linux/game/screens_social.c): the tab shown picks where it goes (the party's chat, a friend's code, the game's chat), the provider takes it, and its
answer comes back as a toast; the game's chat is left to the game."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run

CASES = ["destination", "lines"]


def generated(fault=None):
    source = read("port/linux/game/screens_social.c")
    code = "\n".join([function(source, "keyboard_destination"), function(source, "screens_social_line")])
    if fault:
        code = mutated(code, *fault)
    return (("social_line_code.inc", code),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("screens_social_line", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", [
    # the party tab sends to the game
    ("tab == HALO_SOCIAL_TAB_PARTY ? NETWORK_CHAT_TO_PARTY", "tab == HALO_SOCIAL_TAB_BOTS ? NETWORK_CHAT_TO_PARTY", "destination"),
    # the friends tab chats
    ("tab == HALO_SOCIAL_TAB_FRIENDS ? NETWORK_CHAT_TO_FRIEND_CODE", "tab == HALO_SOCIAL_TAB_BOTS ? NETWORK_CHAT_TO_FRIEND_CODE", "destination"),
    # a line for the game taken
    ("	else\n		return 0;\n	platform_log", "	else\n		done = 0;\n	platform_log", "lines"),
    # the answer lost
    ("	if (answer[0])\n	{\n		struct screens_toast toast;", "	if (0)\n	{\n		struct screens_toast toast;", "lines"),
    # a refusal shown as done
    ("toast.kind = done ? SCREENS_TOAST_NOTICE : SCREENS_TOAST_JOIN_FAILED;", "toast.kind = SCREENS_TOAST_NOTICE;", "lines"),
])
def test_negative_control(before, after, case):
    status, output = run(build("screens_social_line", generated((before, after))), case)
    assert status == CHECK_FAILED, output
