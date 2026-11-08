"""The navigation pack's reader (port/linux/game/bots_nav_load.c with bots_nav.c, compiled as they are): the map's record read on the calling thread or on a
thread of its own and taken once; no pack, no record for the map, another BSP's numbers, a damaged record and a file that is no pack are refused and the
game goes on without a graph."""

import subprocess
import sys
import tempfile
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import harness  # noqa: E402
from harness import build, mutated, read  # noqa: E402

CASES = ["sync", "thread", "no-pack", "other-map", "bsp-mismatch", "damaged", "not-a-pack"]

NEGATIVE_CONTROLS = {
    "surfaces-unchecked": (("if (surfaces != load.surfaces || walkable != load.walkable)", "if (walkable != load.walkable)"), "bsp-mismatch"),
    "walkable-unchecked": (("if (surfaces != load.surfaces || walkable != load.walkable)", "if (surfaces != load.surfaces)"), "bsp-mismatch"),
    "stem-case": (("stem[k] = (char)(stem[k] - 'A' + 'a');", "stem[k] = stem[k];"), "sync"),
    "whole-name": (("if (*p == '\\\\' || *p == '/')\n\t\t\tlast = p + 1;", "if (0)\n\t\t\tlast = p + 1;"), "sync"),
    "taken-twice": (("	nav = load.nav;\n	load.nav = NULL;", "	nav = load.nav;"), "sync"),
    "begun-again": (("if (load.state != _bots_nav_load_idle && strcmp(load.begun_for, stem) == 0)", "if (0)"), "sync"),
    "bad-record-used": (("	if (!nav)\n	{\n		platform_log(\"bots: nav: the record for", "	if (0)\n	{\n		platform_log(\"bots: nav: the record for"), "damaged"),
    "reset-keeps-state": (("	load.state = _bots_nav_load_idle;\n	load.begun_for[0] = 0;", "	load.begun_for[0] = 0;"), "thread"),
}


def generated(fault=None):
    code = read("port/linux/game/bots_nav_load.c")
    if fault:
        code = mutated(code, *fault)
    nav = read("port/linux/game/bots_nav.c")
    return (("bots_nav.h", read("port/linux/game/bots_nav.h")), ("bots_nav_load.h", read("port/linux/game/bots_nav_load.h")),
            ("bots_nav_c.inc", nav), ("bots_nav_load_c.inc", code),
            ("under_test.inc", '#include "bots_nav_c.inc"\n#include "bots_nav_load_c.inc"\n'))


def run(executable, case, path):
    result = subprocess.run([*harness.runner(), str(executable), case, path], capture_output=True, text=True, errors="replace", timeout=120)
    return result.returncode, (result.stdout + result.stderr).strip()


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    with tempfile.TemporaryDirectory() as folder:
        status, output = run(build("bots_nav_load", generated()), case, str(Path(folder) / "navs.pack"))
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    with tempfile.TemporaryDirectory() as folder:
        status, output = run(build("bots_nav_load", generated(fault)), case, str(Path(folder) / "navs.pack"))
    # (a check that failed, or a crash: the pack's reader with a fault may free what it hands out twice)
    assert status != 0, f"the pack's reader with a fault ({control}) passed '{case}': the test cannot see it"
