"""port/wiiu/src/wiiu_keyboard.c: the GamePad's chat keyboard (keys of at least 80 x 60 pixels, where a finger lands, the glyph atlas's packing, the quads that draw it
and the keyboard as the game's thread publishes it)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run

CASES = ["layout", "hit", "atlas", "atlas-fails", "quads", "line", "states", "publish"]


def generated(code_fault=None, header_fault=None):
    code = read("port/wiiu/src/wiiu_keyboard.c")
    header = read("port/wiiu/src/wiiu_keyboard.h")
    if code_fault:
        code = mutated(code, *code_fault)
    if header_fault:
        header = mutated(header, *header_fault)
    return (("halo_keyboard.h", read("port/linux/include/halo_keyboard.h")), ("wiiu_keyboard.h", header), ("wiiu_keyboard_code.inc", code.replace('#include "wiiu_keyboard.h"', "")))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("wiiu_keyboard_logic", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("code_fault,header_fault,case", [
    # keys too short for a thumb
    (None, ("WK_KEY_HEIGHT = 60,", "WK_KEY_HEIGHT = 40,"), "layout"),
    (None, ("WK_KEY_WIDTH = 80,", "WK_KEY_WIDTH = 60,"), "layout"),
    # a wide key found by its second half
    (("\t\tif (hit->row == WK_ROWS - 1)\n\t\t\thit->column -= hit->column % WK_WIDE_COLUMNS;", ""), None, "hit"),
    # the block one pixel too far down
    (("y < (float)(WK_KEYS_TOP + WK_ROWS * WK_KEY_HEIGHT))", "y <= (float)(WK_KEYS_TOP + WK_ROWS * WK_KEY_HEIGHT))"), None, "hit"),
    # a close chip nobody can hit
    (("if (inside(&rectangle, x, y))\n\t\thit->kind = WK_HIT_CLOSE;", ""), None, "hit"),
    # glyphs packed with no room between
    (("x += width + WK_ATLAS_PADDING;", "x += width;"), None, "atlas"),
    # no solid block
    (("memset(atlas->pixels + (size_t)at * WK_ATLAS_WIDTH, 255, SOLID_BLOCK);", ""), None, "atlas"),
    # an atlas that runs off its end
    (("if (y + height + WK_ATLAS_PADDING > WK_ATLAS_HEIGHT)\n\t\t\t\treturn 0;", ""), None, "atlas-fails"),
    # the end of the line not kept in its box
    (("for (start = 0; start < length && wiiu_keyboard_text_width", "for (start = 0; 0 && wiiu_keyboard_text_width"), None, "line"),
    # a caret that never blinks
    (("if ((now_ms / 500u) % 2u == 0)", "if (1)"), None, "line"),
    # the picked key not lit
    (("\t\t\tif (picked)\n\t\t\t\tcolor = color_picked;", ""), None, "quads"),
    # the quads not capped
    (("if (b->count >= b->maximum)\n\t\treturn;", ""), None, "quads"),
    # the sequence of the published keyboard not looked at
    (("if (!(before & 1u) && before == published.sequence)", "if (1)"), None, "publish"),
])
def test_negative_control(code_fault, header_fault, case):
    status, output = run(build("wiiu_keyboard_logic", generated(code_fault, header_fault)), case)
    # (an atlas that runs off its end writes past its pixels: a failed check or a crash, either says the fault is seen)
    assert status == CHECK_FAILED or (case == "atlas-fails" and status != 0), output
