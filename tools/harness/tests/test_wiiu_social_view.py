"""port/wiiu/src/wiiu_social_view.c: the GamePad's social view. Every touch target at least 44 pixels each way and inside the screen, none over another, where a finger lands (the toast's
buttons first, the host's bots buttons), the quads (a background over the whole picture, every glyph inside the screen and inside its pane, the longest names and lines cut to their
places, nothing when the view is off), and the published view and the touch events."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, mutated, read, run

CASES = ["layout", "hits", "quads", "fit", "publish"]


def generated(code_fault=None, header_fault=None):
    code = read("port/wiiu/src/wiiu_social_view.c")
    header = read("port/wiiu/src/wiiu_social_view.h")
    keyboard = read("port/wiiu/src/wiiu_keyboard.c")
    if code_fault:
        code = mutated(code, *code_fault)
    if header_fault:
        header = mutated(header, *header_fault)
    return (("halo_keyboard.h", read("port/linux/include/halo_keyboard.h")), ("halo_social.h", read("port/linux/include/halo_social.h")),
            ("wiiu_keyboard.h", read("port/wiiu/src/wiiu_keyboard.h")), ("wiiu_social_view.h", header),
            ("wiiu_keyboard_code.inc", keyboard.replace('#include "wiiu_keyboard.h"', "")),
            ("wiiu_social_view_code.inc", code.replace('#include "wiiu_social_view.h"', "")))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("wiiu_social_logic", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("code_fault,header_fault,case", [
    # rows too short for a finger
    (None, ("WSV_ROW_HEIGHT = 44,", "WSV_ROW_HEIGHT = 40,"), "layout"),
    # the tab pitch narrower than the tabs: two tabs over each other
    (None, ("WSV_TAB_PITCH = 126,", "WSV_TAB_PITCH = 100,"), "layout"),
    # a tile too short
    (None, ("WSV_TILE_HEIGHT = 56,", "WSV_TILE_HEIGHT = 40,"), "layout"),
    # the tiles over the keyboard button
    (None, ("WSV_KEYBOARD_LEFT = 548,", "WSV_KEYBOARD_LEFT = 500,"), "layout"),
    # an invitation's buttons not above the tabs
    (("\tif (view->toast_alpha > 0 && view->toast_invite)\n\t{", "\tif (0)\n\t{"), None, "hits"),
    # a client given the bots' buttons
    (("for (index = 0; index < 4 && view->bots_host; index++)", "for (index = 0; index < 4; index++)"), None, "hits"),
    # a row with no action given a button
    (("view->rows[index].action != HALO_SOCIAL_ACTION_NONE && inside(&rectangle, x, y)", "inside(&rectangle, x, y)"), None, "hits"),
    # the mirror left under the view
    (("\trect_add(&b, 0.0f, 0.0f, (float)WSV_WIDTH, (float)WSV_HEIGHT, color_background);\n", ""), None, "quads"),
    # a text never cut
    (("if (wiiu_keyboard_text_width(atlas, font, scale, text, (int)strlen(text)) <= width)\n\t{", "if (1)\n\t{"), None, "quads"),
    # a status allowed to run into the button
    (("row->action ? (float)WSV_ROW_BUTTON_LEFT - 6.0f : rectangle.x1 - 8.0f, rectangle.y0 + 39.0f", "rectangle.x1 - 8.0f, rectangle.y0 + 39.0f"), None, "quads"),
    # drawn while off
    (("\tif (!view->active || !atlas || !atlas->ready)\n\t\treturn 0;", "\tif (!atlas || !atlas->ready)\n\t\treturn 0;"), None, "quads"),
    # the quads not capped
    (("\tif (b->count >= b->maximum)\n\t\treturn;\n\tq = &b->quads[b->count++];", "\tq = &b->quads[b->count++];"), None, "quads"),
    # a half-written view read
    (("if (!(before & 1u) && before == published.sequence)", "if (1)"), None, "publish"),
    # a queue that holds nine
    (("if (queue.written - queue.read >= 8)", "if (queue.written - queue.read >= 9)"), None, "publish"),
])
def test_negative_control(code_fault, header_fault, case):
    status, output = run(build("wiiu_social_logic", generated(code_fault, header_fault)), case)
    assert status != 0, f"the fault was not caught by {case}"
