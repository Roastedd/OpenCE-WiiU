"""port/wiiu/src/wiiu_chat_input.h: the Wii U's controllers as the text chat's on-screen keyboard reads them (every button's bit, the Wii U's letters, a Wii Remote's
Xbox letters, the stick's push) and the GamePad's finger as a tap (once, where it came down, as a share of the picture, only while fresh)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run

CASES = ["buttons", "others", "letters", "stick", "tap-edge", "tap-fresh", "tap-scale"]


def generated(fault=None):
    mapping = read("port/wiiu/src/wiiu_chat_input.h")
    if fault:
        mapping = mutated(mapping, *fault)
    return (("wiiu_input.h", read("port/wiiu/src/wiiu_input.h")), ("halo_keyboard.h", read("port/linux/include/halo_keyboard.h")),
            ("wiiu_chat_input.h", mapping))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("wiiu_chat_input", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("fault,case", [
    # Start mapped to the wrong bit
    (("buttons |= 1UL << HALO_CHAT_PAD_START;", "buttons |= 1UL << HALO_CHAT_PAD_B;"), "buttons"),
    # B and A swapped, as the places would have them
    (("if (held & WIIU_PAD_B)\n\t\tbuttons |= 1UL << HALO_CHAT_PAD_B;", "if (held & WIIU_PAD_B)\n\t\tbuttons |= 1UL << HALO_CHAT_PAD_A;"), "letters"),
    # a Wii Remote's Xbox letters not counted
    (("if (pad->xbox_faces & WIIU_XBOX_B)\n\t\theld |= WIIU_PAD_B;", ""), "letters"),
    # the stick's threshold too low
    (("#define WIIU_CHAT_STICK_PUSH 0.7f", "#define WIIU_CHAT_STICK_PUSH 0.2f"), "stick"),
    # the stick's up taken for down
    (("if (pad->left_y > WIIU_CHAT_STICK_PUSH)\n\t\tbuttons |= 1UL << HALO_CHAT_PAD_STICK_UP;", "if (pad->left_y > WIIU_CHAT_STICK_PUSH)\n\t\tbuttons |= 1UL << HALO_CHAT_PAD_STICK_DOWN;"), "buttons"),
    # a tap on every poll of a held finger
    (("if (down && !tap->was_down)", "if (down)"), "tap-edge"),
    # a tap that never goes stale
    (("if (tap->pending && ++tap->age >= WIIU_CHAT_TAP_POLLS)", "if (0)"), "tap-fresh"),
    # not scaled
    (("*x = pixel_x / WIIU_CHAT_TOUCH_WIDTH;", "*x = pixel_x;"), "tap-scale"),
])
def test_negative_control(fault, case):
    status, output = run(build("wiiu_chat_input", generated(fault)), case)
    assert status == CHECK_FAILED, output
