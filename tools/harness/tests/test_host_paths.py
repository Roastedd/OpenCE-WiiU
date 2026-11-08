"""Files named by a host path (the card's folder, a path in the settings) in the game's folder: port/linux/include/stdio.h turns fopen, remove and rename there into the
translators of Xbox paths (halo_linux_fopen ...), which take "/vol/external01/wiiu/apps/halo/friends.key" for a path under the data root and open
"<data root>/vol/external01/wiiu/apps/halo/friends.key", a file that is not there. The friends layer's files are named by host path, so social_store.c takes the C library's (#undef fopen; the bots' navigation pack reader
(bots_nav_load.c) defines HALO_LINUX_PLATFORM_LAYER for the same reason). The tests preprocess the units as the game's build would and look at what their own lines call. A host run of the digest's program (friends.key
"could not be written" under /data) found this; the unit tests write through a stand-in for the card and could not."""
import re
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import ROOT, mutated, read

UNITS = ["port/linux/game/social_store.c"]
CALLS = re.compile(r"\bhalo_linux_(fopen|freopen|remove|rename)\s*\(")


def own_lines(text, path):
    """the preprocessed lines that came from the unit itself (the linemarkers say which file a line is of)"""
    own, mine = [], False
    for line in text.splitlines():
        marker = re.match(r'# \d+ "([^"]*)"', line)
        if marker:
            mine = marker.group(1).endswith(path.rsplit("/", 1)[1])
            continue
        if mine:
            own.append(line)
    return "\n".join(own)


def preprocessed(source_text, name):
    work = ROOT / "build" / "host-paths"
    work.mkdir(parents=True, exist_ok=True)
    unit = work / name
    unit.write_text(source_text, encoding="latin-1")
    command = ["clang", "-E", "-w", "-I", str(ROOT / "port/linux/include"), "-I", str(ROOT / "port/linux/game"), "-I", str(ROOT / "port/linux/src"),
               "-idirafter", str(ROOT / "port/include/xdk"), "-D_GNU_SOURCE", str(unit)]
    result = subprocess.run(command, capture_output=True, text=True, cwd=ROOT)
    assert result.returncode == 0, result.stderr[-3000:]
    return result.stdout


@pytest.mark.parametrize("path", UNITS)
def test_host_paths_use_the_c_library(path):
    text = preprocessed((ROOT / path).read_text(encoding="latin-1"), Path(path).name)
    mine = own_lines(text, path)
    assert re.search(r"\b(fopen)\s*\(", mine), f"{path} opens no file any more: this test has nothing to look at"
    assert not CALLS.search(mine), f"{path} opens, removes or renames by the Xbox path translators: {CALLS.search(mine).group(0)}"


@pytest.mark.parametrize("path,before", [("port/linux/game/social_store.c", "#undef fopen\n")])
def test_negative_control(path, before):
    """without the #undef the unit calls the translator, and the check above would say so"""
    source = (ROOT / path).read_text(encoding="latin-1")
    assert source.count(before) == 1
    text = preprocessed(source.replace(before, "", 1), "control_" + Path(path).name)
    assert CALLS.search(own_lines(text, path)), "the fault was not seen"
