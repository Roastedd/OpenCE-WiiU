"""The layout tables the wire tests are made from (tests/xplay_layouts_*.inc, tools/harness/xplay_layouts.py): each structure's
size and each scalar's offset against the game's own declarations on both byte orders (so the 32-bit x86 build, the PowerPC
build and the tables all agree on where every scalar is), and the tables against a fresh reading of the declarations (a field added
to a message without a table entry is found here: needs libclang, python3-clang, as the opence-ppc-be image has)."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import xplay  # noqa: E402
import xplay_layouts  # noqa: E402

LEGS = xplay.legs()
pytestmark = pytest.mark.skipif(not LEGS, reason="needs 32-bit x86 (-m32 or qemu-i386) or PowerPC (qemu-ppc) and clang")


@pytest.mark.parametrize("leg", sorted(LEGS))
@pytest.mark.parametrize("module", sorted(xplay_layouts.UNITS))
def test_layouts_match_declarations(leg, module):
    source = xplay_layouts.UNITS[module][0]
    xplay.check_layouts(leg, source, f"xplay_layouts_{module}.inc")


@pytest.mark.parametrize("module", sorted(xplay_layouts.UNITS))
def test_layouts_current(module):
    pytest.importorskip("clang.cindex")
    source, wanted = xplay_layouts.UNITS[module]
    text, _ = xplay_layouts.generate(module, source, wanted)
    assert (xplay.TESTS / f"xplay_layouts_{module}.inc").read_text() == text, \
        "regenerate with tools/harness/xplay_layouts.py (in the opence-ppc-be image)"


def test_a_wrong_table_is_caught():
    """the check can fail: a size that is not the structure's"""
    leg = sorted(LEGS)[0]
    broken = xplay.TESTS / "xplay_layouts_broken.inc"
    text = (xplay.TESTS / "xplay_layouts_voice.inc").read_text().replace("sizeof(struct distributed_voice_up) == 8",
                                                                         "sizeof(struct distributed_voice_up) == 9")
    broken.write_text(text)
    try:
        with pytest.raises(RuntimeError):
            xplay.check_layouts(leg, "port/linux/game/network_voice.c", broken.name)
    finally:
        broken.unlink()
