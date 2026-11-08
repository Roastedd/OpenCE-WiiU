"""port/linux/game/text_safe.c: what may be drawn of text from outside. Every 16-bit code point through the name, line and character sanitisers (printable ASCII,
never "|", the right lengths, never empty), its folding table against players.c's, its names against player_name_clean's and its lines against chat_text_clean's."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run

CASES = ["characters", "fold-agrees", "name-all-codepoints", "name-random", "name-fallback", "name-agrees", "text-all-bytes", "text-agrees-with-chat", "utf8",
         "code"]


def generated(fault=None):
    players = read("source/game/players.c")
    chat = read("port/linux/game/network_chat.c")
    code = read("port/linux/game/text_safe.c")
    if fault:
        code = mutated(code, *fault)
    config = f"#define CHAT_TEXT_SIZE {constant(chat, 'CHAT_TEXT_SIZE')}\n"
    under_test = "\n".join([function(players, "player_name_character_ascii"), function(players, "player_name_clean"), function(chat, "chat_text_clean")])
    return (("config.inc", config), ("text_safe.h", read("port/linux/game/text_safe.h")), ("text_safe_code.inc", code), ("under_test.inc", under_test))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("text_safe", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("fault,case", [
    # a pipe let through
    (("return code >= 0x20 && code <= 0x7E && code != '|';", "return code >= 0x20 && code <= 0x7E;"), "characters"),
    # control characters let through
    (("return code >= 0x20 && code <= 0x7E && code != '|';", "return code <= 0x7E && code != '|';"), "characters"),
    # the folding table off by one
    (("return latin_fold[code - 0xC0];", "return latin_fold[code - 0xC1];"), "fold-agrees"),
    # a name not cut
    (("short maximum = TEXT_SAFE_NAME_LENGTH;", "short maximum = size - 1;"), "name-all-codepoints"),
    # the zero-width characters kept
    (("(character >= 0x200B && character <= 0x200F)", "0"), "name-agrees"),
    # no fallback
    (("if (readable)\n\t\treturn length;", "if (readable || length)\n\t\treturn length;"), "name-fallback"),
    # spaces at the start of a line kept
    (("if (length == 0 || spaces >= 2)\n\t\t\t\tcontinue;", "if (spaces >= 2)\n\t\t\t\tcontinue;"), "text-all-bytes"),
    # the destination's size ignored
    (("if (maximum > size - 1)\n\t\tmaximum = (short)(size - 1);\n\tfor (index = 0; source && index < source_size", "for (index = 0; source && index < source_size"), "text-all-bytes"),
    # a code's I read as I
    (("if (character == 'I' || character == 'L')", "if (character == 'L')"), "code"),
])
def test_negative_control(fault, case):
    status, output = run(build("text_safe", generated(fault)), case)
    assert status == CHECK_FAILED, output
