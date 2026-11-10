"""The words of the social screens against the boxes they are put in. On the console a sentence was cut by its box's edge ("... WHEN THEY ARE ONI", "THE MAP IS DOW...") because the box
is as wide as it is and the engine's text widget neither wraps nor measures. Here, with the metrics the game cuts with (port/linux/game/screens_metrics.inc, the fonts' own advances and
3 percent), every static text of the screens' XML fits its widget, and every sentence the screens and the layers under them can say (a literal of the sources, with the widest name, number
and map put in its place) is shorter than the buffers that carry it (SCREENS_LINE_SIZE) and breaks at its spaces into the two lines of a detail line or an answer (512 wide) with no word
wider than a line. The C side, with the real functions and every state, is tools/harness/tests/test_screens.py "wrap-sentences".

A sentence that no longer fits fails the test with its text: shorten it (or give it a bigger box in tools/port_settings.py), do not exempt it."""
import re
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import port_settings  # noqa: E402

GAME = ROOT / "port/linux/game"
CE = ROOT / "port/assets/menus/ce"
LINE_SIZE = 72            # screens.h SCREENS_LINE_SIZE: the buffer an answer, a toast, a detail is made in, the end included
BOX_WIDTH = int(re.search(r"SCREENS_LINE_BOX_WIDTH = (\d+)", (GAME / "screens.h").read_text()).group(1))   # (512 wide widgets: what the console draws starts 5 units in)
BOX_LINES = 2             # screens.h SCREENS_LINE_BOX_LINES
# the engine's message box (a refusal said over a screen: menu_screens.inl scr_say; error.xml's error_text_box 280 x 159 with text_x 9, text_y 35, which the engine wraps at its spaces
# in 280 - 4 * 9 = 244 units and 159 - 2 * 35 = 89 high, in the small font when the large would be taller): at most 4 lines of the large font, else 5 of the small
MESSAGE_WIDTH, MESSAGE_LINES_LARGE, MESSAGE_LINES_SMALL = 244, 4, 5
# the sources that make the words (the screens, the game's side of them, the backend that speaks for the friends layer, the layers' own sentences)
SOURCES = ["screens.c", "screens_mock.c", "screens_social_backend.c", "screens_social.c", "menu_screens.inl", "social_join.c", "social.c", "social_party.c", "social_friends.c"]
# calls whose texts are the log's, not the player's
LOG_CALLS = re.compile(r"\b(scr_log|platform_log|fprintf|printf|CHECK|assert|strcmp|strstr|strncmp|social_log|log_line)\(")
# (a log line is "screens: ..." in lower case, broken across lines it is not seen by the call test; the player's sentences are capitals)
LOG_PREFIX = re.compile(r"^[a-z]+: ")


def advances(name):
    inc = (GAME / "screens_metrics.inc").read_text()
    body = re.search(r"screens_advance_%s\[95\] =\s*\{(.*?)\};" % name, inc, re.S).group(1)
    return [float(value) for value in re.findall(r"([\d.]+)f", body)]


LARGE, SMALL = advances("large"), advances("small")


def width(text, table=LARGE):
    return sum(table[ord(c) - 32] if 32 <= ord(c) < 127 else table[ord("?") - 32] for c in text)


def lines_needed(text, limit, table=LARGE):
    """the lines a sentence breaks into at its spaces (None: a word is wider than a line), as screens_wrap does"""
    lines, current = 1, ""
    for word in text.split():
        trial = word if not current else current + " " + word
        if width(trial, table) <= limit:
            current = trial
        elif not current or width(word, table) > limit:
            return None
        else:
            lines += 1
            current = word
    return lines


def literals():
    """(file, line number, sentence with its places filled) for every string literal of the sources that has at least three words"""
    found = []
    pattern = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
    for name in SOURCES:
        path = GAME / name
        if not path.exists():
            continue
        for number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
            if LOG_CALLS.search(line) and "answer" not in line:
                continue
            for match in pattern.finditer(line):
                text = match.group(1).replace("\\\\", "\\")
                if text.count(" ") < 2 or LOG_PREFIX.match(text) or text.startswith("ACTION %s"):
                    continue
                # (a name, a map, a number put in: the widest each can be; a %.4s of a code is 4 characters)
                text = re.sub(r"%\.(\d+)s", lambda m: "W" * int(m.group(1)), text)
                text = re.sub(r"%\d*(?:l|ll|h)?[sc]", "WWWWWWWWWWW", text)
                text = re.sub(r"%\d*(?:l|ll|h)?[dux]", "128", text).replace("%%", "%")
                found.append((name, number, text.upper()))
    return found


def problems_with(sentences):
    """what is wrong with each sentence: too long for the buffer, a word wider than the box, more lines than the box has"""
    wrong = []
    for name, number, text in sentences:
        where = f"{name}:{number}: {text!r}"
        if len(text) > LINE_SIZE - 1:
            wrong.append(f"{where} is {len(text)} characters: a buffer of {LINE_SIZE} cuts it mid-word")
            continue
        needed = lines_needed(text, BOX_WIDTH)
        if needed is None:
            wrong.append(f"{where} has a word wider than the {BOX_WIDTH} unit box")
        elif needed > BOX_LINES:
            wrong.append(f"{where} needs {needed} lines, the box has {BOX_LINES}")
        large, small = lines_needed(text, MESSAGE_WIDTH), lines_needed(text, MESSAGE_WIDTH, SMALL)
        if not ((large is not None and large <= MESSAGE_LINES_LARGE) or (small is not None and small <= MESSAGE_LINES_SMALL)):
            wrong.append(f"{where} does not fit the message box ({large} lines large, {small} small)")
    return wrong


def test_the_corpus_is_found():
    found = literals()
    assert len(found) > 150
    texts = [text for _, _, text in found]
    assert any("REQUEST SENT. THEY WILL SEE IT WHEN THEY ARE ONLINE." == text for text in texts)
    assert any("THE MAP IS DOWNLOADED FIRST" in text for text in texts)


def test_every_sentence_fits_its_box():
    wrong = problems_with(literals())
    assert not wrong, "\n".join(wrong)


def test_a_sentence_too_long_is_found():
    """negative controls: a 72 character answer (the buffer cuts it), a 3 line sentence, a word wider than the box"""
    base = literals()
    assert not problems_with(base)
    assert problems_with(base + [("x.c", 1, "A" * 36 + " " + "B" * 35)])
    assert problems_with(base + [("x.c", 1, "THIS SENTENCE IS TOO LONG FOR A BOX OF TWO LINES AND SAYS IT AT LENGTH: " * 1)])
    assert problems_with(base + [("x.c", 1, "W" * 60 + " AND MORE")])
    # (the message box: a box of one line's width fails the same sentences; its rule is checked with the real numbers above)
    assert lines_needed("THAT PLAYER'S CODE IS NOT KNOWN. ASK FOR IT: FRIENDS > ADD FRIEND.", MESSAGE_WIDTH) > 2
    assert problems_with(base + [("x.c", 1, "A PLAYER'S CODE IS NOT KNOWN HERE. ASK FOR IT: FRIENDS, THEN ADD FRIEND.")]), "the 72 character answer of the first console session"


def static_texts():
    """(widget, text, width, font table) for each widget of the social files with a text of its own, and each string a string list of theirs has"""
    paths = [CE / name for name in port_settings.social_files()] + [CE / "main_menu.settings_select.player_setup.player_profile_edit.bot_settings.xml"]
    result = []
    for path in paths:
        root = ElementTree.parse(path).getroot()
        strings = {element.get("name"): [string.get("text") for string in element.findall("string")] for element in root if element.tag == "strings"}
        for element in root:
            if element.tag != "widget" or element.get("type") != "text":
                continue
            table = SMALL if "small" in (element.get("font") or "") else LARGE
            limit = float(element.get("width", 0)) - 2 * float(element.get("text_x", 0)) - 5   # (the console starts a text 5 units in)
            texts = [element.get("text")] if element.get("text") else strings.get(element.get("string_list"), [])
            for text in texts:
                result.append((element.get("name"), text.replace("\\r", "\r").replace("\\n", "\r").replace("\n", "\r"), limit, table))
    return result


def static_problems(items):
    wrong = []
    for name, text, limit, table in items:
        for part in text.split("\r"):
            if width(part, table) > limit:
                wrong.append(f"{name}: {part!r} is {width(part, table):.0f} units, its widget {limit:.0f}")
    return wrong


def test_every_static_text_fits_its_widget():
    items = static_texts()
    assert len(items) > 20
    wrong = static_problems(items)
    assert not wrong, "\n".join(wrong)
    # (a negative control: the same texts in boxes a third too narrow)
    assert static_problems([(name, text, limit / 3, table) for name, text, limit, table in items])


def test_the_detail_boxes_hold_two_lines():
    """the XML's detail line, answer and the add screen's answer are tall enough for the two lines they are given: a large line is 22 units, the first starts 4 down"""
    widgets = {}
    for name in port_settings.social_files():
        for element in ElementTree.parse(CE / name).getroot():
            if element.tag == "widget":
                widgets[element.get("name").rsplit("/", 1)[-1]] = element
    for name in ("friends_detail", "players_detail", "actions_answer", "add_answer"):
        element = widgets[name]
        assert float(element.get("width")) >= BOX_WIDTH
        assert float(element.get("height")) >= float(element.get("text_y", 0)) + BOX_LINES * 22, name
