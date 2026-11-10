"""port/linux/game/screens.c and screens_mock.c: the social screens' model. Every 11 character ASCII name (the widest letters and signs) fits every column; every row of every
state (a friend in each presence and join state, a party member in each state, a roster row in each flag combination) has cells that fit and are clean ASCII; the focus moves in
the order the XML builds (D-pad, A, B, the shoulders, taps); the toast queue and its timing; the model is rebuilt only when the generation moves; the mock's actions change its
data; and with screens.enable 0 nothing exists."""
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import ROOT, build, mutated, read, run

CASES = ["words", "fit-names", "fit-columns", "fit-cut", "wrap", "wrap-sentences", "player-cells", "friend-cells", "party-cells", "view", "empty-text", "tabs", "sorting", "actions", "model", "mock-actions", "mock-text",
         "nav-players", "nav-friends", "nav-tap", "toast-queue", "toast-time", "disabled", "level-1", "level-2", "level-clamped"]


def generated(code_fault=None, mock_fault=None, header_fault=None, view_fault=None):
    code = read("port/linux/game/screens.c")
    mock = read("port/linux/game/screens_mock.c")
    header = read("port/linux/game/screens.h")
    if code_fault:
        code = mutated(code, *code_fault)
    if mock_fault:
        mock = mutated(mock, *mock_fault)
    if header_fault:
        header = mutated(header, *header_fault)
    return (("halo_social.h", read("port/linux/include/halo_social.h")), ("screens_view.h", read("port/linux/game/screens_view.h")),
            ("screens_view_code.inc", (mutated(read("port/linux/game/screens_view.c"), *view_fault) if view_fault else read("port/linux/game/screens_view.c")).replace('#include "screens.h"', "").replace('#include "screens_view.h"', "")),
            ("screens.h", header), ("screens_metrics.inc", read("port/linux/game/screens_metrics.inc")), ("text_safe.h", read("port/linux/game/text_safe.h")),
            ("text_safe_code.inc", read("port/linux/game/text_safe.c").replace('#include "text_safe.h"', "")),
            ("screens_code.inc", code.replace('#include "screens.h"', "").replace('#include "screens_metrics.inc"', '#include "screens_metrics.inc"')),
            ("screens_mock_code.inc", mock.replace('#include "screens.h"', "").replace('#include "text_safe.h"', "")))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("screens_core", generated()), case)
    assert status == 0, output


def test_metrics_are_current():
    """the advances table is what the fonts give (tools/screens_metrics.py --check)"""
    result = subprocess.run([sys.executable, str(ROOT / "tools/screens_metrics.py"), "--check"], capture_output=True, text=True)
    if "No module named 'PIL'" in result.stderr:
        pytest.skip("Pillow is needed to read the fonts")
    assert result.returncode == 0, "port/linux/game/screens_metrics.inc is not what tools/screens_metrics.py writes"


@pytest.mark.parametrize("code_fault,mock_fault,header_fault,case", [
    # a text never cut
    (("if (screens_text_width(font, text) <= width)\n\t{", "if (1)\n\t{"), None, None, "fit-columns"),
    # a text that fits exactly is cut
    (("if (screens_text_width(font, text) <= width)\n\t{", "if (screens_text_width(font, text) < width)\n\t{"), None, None, "fit-cut"),
    # the dots not counted
    (("if (next + dots > width)", "if (next > width)"), None, None, "fit-cut"),
    # the lobby's name column narrower than the widest name
    (None, None, ("SCREENS_LOBBY_NAME_WIDTH = 190,", "SCREENS_LOBBY_NAME_WIDTH = 150,"), "fit-names"),
    # the friends' name column narrower than the widest name
    (None, None, ("SCREENS_FRIEND_NAME_WIDTH = 190,", "SCREENS_FRIEND_NAME_WIDTH = 120,"), "fit-names"),
    # the status column narrower than its words
    (None, None, ("SCREENS_FRIEND_STATUS_WIDTH = 282,", "SCREENS_FRIEND_STATUS_WIDTH = 160,"), "fit-columns"),
    # the join word cut with the map
    (("snprintf(suffix + strlen(suffix), sizeof(suffix) - strlen(suffix), \" %s\", join);", ""), None, None, "fit-columns"),
    # a detail line cut with dots instead of broken at its spaces (the console's "THE MAP IS DOW...")
    (("snprintf(line, sizeof(line), \"%s IS OFFLINE.\", row->name);\n\tscreens_wrap(SCREENS_FONT_LARGE, (float)SCREENS_LINE_BOX_WIDTH, SCREENS_LINE_BOX_LINES, line, text, size);",
      "snprintf(line, sizeof(line), \"%s IS OFFLINE.\", row->name);\n\tscreens_fit(SCREENS_FONT_LARGE, 520.0f, line, text, size);"), None, None, "wrap-sentences"),
    # a player's detail not wrapped
    (("snprintf(line + strlen(line), sizeof(line) - strlen(line), \"  %d MS\", row->ping > 999 ? 999 : row->ping);\n\tscreens_wrap(SCREENS_FONT_LARGE, (float)SCREENS_LINE_BOX_WIDTH, SCREENS_LINE_BOX_LINES, line, text, size);",
      "snprintf(line + strlen(line), sizeof(line) - strlen(line), \"  %d MS\", row->ping > 999 ? 999 : row->ping);\n\tscreens_fit(SCREENS_FONT_LARGE, 360.0f, line, text, size);"), None, None, "wrap-sentences"),
    # the empty lists' words not wrapped
    (("screens_wrap(SCREENS_FONT_LARGE, (float)SCREENS_LINE_BOX_WIDTH, SCREENS_LINE_BOX_LINES, line, text, size);\n}\n\n/* ---------- navigation */",
      "screens_fit(SCREENS_FONT_LARGE, 300.0f, line, text, size);\n}\n\n/* ---------- navigation */"), None, None, "wrap-sentences"),
    # a line wider than the box accepted
    (("if (end != start && measure(context, start, (int)(word_end - start)) > width)", "if (end != start && measure(context, start, (int)(word_end - start)) > width * 2.0f)"), None, None, "wrap"),
    # a cut not said
    (("return whole ? lines : -lines;", "return lines;"), None, None, "wrap"),
    # a word broken in the middle: a line taken whole when it is wider than the box
    (("if (measure(context, part, source_length) > width)\n\t\t{", "if (0)\n\t\t{"), None, None, "wrap"),
    # a bot with no tag
    (("snprintf(text, (size_t)size, \"BOT %s\", screens_skill_word(row->skill));", "snprintf(text, (size_t)size, \"%s\", screens_skill_word(row->skill));"), None, None, "player-cells"),
    # a bot's tag not amber
    (("*color = SCREENS_COLOR_AMBER;\n\t}\n\telse if ((row->flags & SCREENS_ROW_ME) && (row->flags & SCREENS_ROW_HOST))", "}\n\telse if ((row->flags & SCREENS_ROW_ME) && (row->flags & SCREENS_ROW_HOST))"), None, None, "player-cells"),
    # a ping colour off
    (("return ping < 80 ? SCREENS_COLOR_GREEN", "return ping < 40 ? SCREENS_COLOR_GREEN"), None, None, "player-cells"),
    # the skills in another order
    (("\"RECRUIT\", \"EASY\", \"NORMAL\"", "\"EASY\", \"RECRUIT\", \"NORMAL\""), None, None, "words"),
    # a request got drawn as a friend
    (("return \"WANTS TO BE FRIENDS\";", "return \"OFFLINE\";"), None, None, "friend-cells"),
    # the DOWNLOAD word lost
    (("return \"DOWNLOAD\";", "return \"\";"), None, None, "friend-cells"),
    # offline friends not last
    (("\treturn 5;\n}\n\nstatic int name_compare", "\treturn 0;\n}\n\nstatic int name_compare"), None, None, "sorting"),
    # a party member's progress dropped
    (("snprintf(text, sizeof(text), \"%s %d%%\", screens_member_word(member->state)", "snprintf(text, sizeof(text), \"%s %d\", screens_member_word(member->state)"), None, None, "party-cells"),
    # a service that is off said to be on
    (("else if (summary && summary->service == SCREENS_SERVICE_OFF && tab == SCREENS_TAB_FRIENDS)", "else if (0)"), None, None, "empty-text"),
    # the chosen tab not marked
    (("if (chosen)\n\t\tsnprintf(text, (size_t)size, \"[%s]\", line);", "if (0)\n\t\tsnprintf(text, (size_t)size, \"[%s]\", line);"), None, None, "tabs"),
    # up from the first row does not reach the tabs
    (("else if (nav->tab_count)\n\t\t\t\tnav->area = SCREENS_AREA_TABS;\n\t\t\telse if (nav->button_count)\n\t\t\t\tnav->area = SCREENS_AREA_BUTTONS;\n\t\t}\n\t\telse if (key == SCREENS_KEY_DOWN)\n\t\t{\n\t\t\tint visible",
      "else if (nav->button_count)\n\t\t\t\tnav->area = SCREENS_AREA_BUTTONS;\n\t\t}\n\t\telse if (key == SCREENS_KEY_DOWN)\n\t\t{\n\t\t\tint visible"), None, None, "nav-friends"),
    # the rows do not scroll
    (("else if (nav->first + visible < nav->row_count)\n\t\t\t\tnav->first++;", "else if (0)\n\t\t\t\tnav->first++;"), None, None, "nav-players"),
    # the buttons do not wrap to the left
    (("nav->button = (nav->button + nav->button_count - 1) % nav->button_count;", "nav->button = nav->button > 0 ? nav->button - 1 : 0;"), None, None, "nav-players"),
    # B does not go back
    (("if (key == SCREENS_KEY_B)\n\t{\n\t\tnav->result = SCREENS_NAV_BACK;", "if (key == SCREENS_KEY_B)\n\t{\n\t\tnav->result = SCREENS_NAV_NOTHING;"), None, None, "nav-players"),
    # the shoulders do not turn the tab
    (("if ((key == SCREENS_KEY_L || key == SCREENS_KEY_R) && nav->tab_count)", "if (0)"), None, None, "nav-friends"),
    # a new tab keeps the old scroll
    (("nav->first = nav->row = 0;\n\t\tnav->result = SCREENS_NAV_TAB;\n\t\treturn;", "nav->result = SCREENS_NAV_TAB;\n\t\treturn;"), None, None, "nav-friends"),
    # a tap on a row past the list
    (("&& nav->first + row < nav->row_count)", ")"), None, None, "nav-tap"),
    # a tap in the gap between two tabs counts
    (("x < SCREENS_TAB_LEFT + tab * SCREENS_TAB_PITCH + SCREENS_TAB_WIDTH)", "1)"), None, None, "nav-tap"),
    # the buttons' places moved
    (("510 - (count - 1 - index) * 130;", "510 - (count - 1 - index) * 128;"), None, None, "nav-tap"),
    # the five buttons narrower than they are
    (("return count >= 5 ? 126 : 128;", "return 128;"), None, None, "nav-tap"),
    # an invitation does not jump the queue
    (("if (toast_is_invite(toast))\n\t{\n\t\t/* (an invitation jumps", "if (0)\n\t{\n\t\t/* (an invitation jumps"), None, None, "toast-queue"),
    # the same toast not dropped
    (("now - state->recent_at[index] < SCREENS_TOAST_SAME_MILLISECONDS", "0"), None, None, "toast-queue"),
    # a full queue drops an invitation
    (("for (index = 0; index < state->count && toast_is_invite(&state->queue[index]); index++)\n\t\t\t;\n\t\tif (index == state->count)",
      "index = 0;\n\t\tif (index == state->count)"), None, None, "toast-queue"),
    # no gap between toasts
    (("(!state->ended || now - state->ended >= SCREENS_TOAST_GAP_MILLISECONDS)", "1"), None, None, "toast-queue"),
    # an invitation as short as a notice
    (("return toast_is_invite(toast) ? SCREENS_INVITE_MILLISECONDS : SCREENS_TOAST_MILLISECONDS;", "return SCREENS_TOAST_MILLISECONDS;"), None, None, "toast-time"),
    # no fade
    (("if (age < SCREENS_TOAST_FADE_MILLISECONDS)\n\t\treturn (int)(age * 255 / SCREENS_TOAST_FADE_MILLISECONDS);", "if (age < SCREENS_TOAST_FADE_MILLISECONDS)\n\t\treturn 255;"), None, None, "toast-time"),
    # a join from a game with one press
    (("if (accept && in_game && !state->confirming)", "if (0)"), None, None, "toast-time"),
    # the model rebuilt every time
    (("if (!force && model->valid && provider->generation && generation == model->generation)\n\t\treturn 0;", ""), None, None, "model"),
    # the provider used with the setting off
    (("return screens_enabled() == 1 ? screens_mock_provider() : NULL;", "return screens_mock_provider();"), None, None, "disabled"),
    # a level above two not clamped
    (("level = value < 0 ? 0 : value > 2 ? 2 : (int)value;", "level = value < 0 ? 0 : (int)value;"), None, None, "level-clamped"),
    # the mock's bot added to the larger team
    (None, ("team = red <= blue ? SCREENS_TEAM_RED : SCREENS_TEAM_BLUE;", "team = red <= blue ? SCREENS_TEAM_BLUE : SCREENS_TEAM_RED;"), None, "mock-actions"),
    # the mock lets a game overfill
    (None, ("if (mock.roster_count >= 16)", "if (mock.roster_count >= 99)"), None, "mock-actions"),
    # the mock: a name with a pipe
    (None, ("roster_add(\"WWWWWWWWWWW\"", "roster_add(\"WWWWW|WWWWW\""), None, "mock-text"),
    # the mock's code not checked
    (None, ("if (length != TEXT_SAFE_CODE_LENGTH)\n\t{", "if (0)\n\t{"), None, "mock-actions"),
    # a client offered the host's actions
    (("\t\tif (summary->host)\n\t\t{\n\t\t\tactions[count++] = SCREENS_ACTION_KICK;", "\t\t{\n\t\t\tactions[count++] = SCREENS_ACTION_KICK;"), None, None, "actions"),
])
def test_negative_control(code_fault, mock_fault, header_fault, case):
    status, output = run(build("screens_core", generated(code_fault, mock_fault, header_fault)), case)
    assert status != 0, f"the fault was not caught by {case}"
    assert "check failed" in output or status == 1, output


@pytest.mark.parametrize("view_fault", [
    # offline friends in the view
    ("\t\tif (friend_row->presence == SCREENS_PRESENCE_OFFLINE)\n\t\t\tcontinue;", ""),
    # the map left in a narrow row's status
    ("\t\tshown.map[0] = 0;\n", ""),
    # a full game offered as joinable
    ("(row->join == SCREENS_JOIN_OPEN || row->join == SCREENS_JOIN_DOWNLOAD);\n}", "1;\n}"),
    # buttons shown with no provider to act
    ("input->can_friend_action ? (joinable", "1 ? (joinable"),
    # the bots' buttons for a client
    ("view->bots_host = summary->host && input->can_bots;", "view->bots_host = input->can_bots;"),
    # the question for a second tap never shown
    ("toasts->confirming ? \"LEAVE THIS GAME? TAP JOIN AGAIN.\" : toasts->current.detail", "toasts->current.detail"),
    # no word for an empty friends list
    ("\t\tscreens_empty_text(1, SCREENS_TAB_FRIENDS, &model->friends_summary, input->has_friends, view->empty, (int)sizeof(view->empty));", "\t\t;"),
    # an invitation not marked
    ("view->toast_invite = toasts->current.kind == SCREENS_TOAST_INVITE;", "view->toast_invite = 0;"),
])
def test_view_negative_control(view_fault):
    status, output = run(build("screens_core", generated(view_fault=view_fault)), "view")
    assert status != 0, "the fault was not caught by the view case"
