"""Each of the friends layer's sources compiles on its own, as the game's build compiles it (one unit at a time, with only its own includes): the other tests include the
sources in one program, where a header one of them needs is already there."""
import os
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import social

UNITS = sorted(path.name for path in (ROOT / "port/linux/game").glob("social*.c"))


@pytest.mark.parametrize("unit", UNITS)
def test_unit_compiles_alone(unit):
    command = [os.environ.get("CC", "clang"), *social.target(), "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-unused-function",
               "-Wno-sign-compare", "-Wno-unused-command-line-argument", "-fsyntax-only", f"-I{ROOT / 'port/linux/src'}", f"-I{ROOT / 'port/third_party/monocypher'}",
               "-idirafter", str(ROOT / "port/linux/include"), "-idirafter", str(ROOT / "port/include/xdk"), str(ROOT / "port/linux/game" / unit)]
    result = subprocess.run(command, capture_output=True, text=True, cwd=ROOT)
    assert result.returncode == 0, result.stderr[-3000:]
