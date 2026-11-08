#!/usr/bin/env python3
"""Writes port/linux/game/screens_metrics.inc: the advance of each printable ASCII character in the two fonts the menus draw text in, in the units of the 640 x 480 menu
screen, so that screens.c can cut a text to the width of a column without a font (screens_fit). Overpass-900 stands for ui\\large_ui (capitals 13 units high) and
Overpass-750 for ui\\small_ui (capitals 10 units), as port/assets/fonts/fonts.json maps them; each advance is rounded up to a tenth of a unit and given 3 percent more, for
the kerning and the rounding of a renderer that is not this one.

    python3 tools/screens_metrics.py           write the file
    python3 tools/screens_metrics.py --check   exit 1 if the file is not what the fonts give (a test runs this)
"""
import math
import sys
from pathlib import Path

from PIL import ImageFont

ROOT = Path(__file__).resolve().parent.parent
FONTS = [("Overpass-900.ttf", 13.0), ("Overpass-750.ttf", 10.0)]
MARGIN = 1.03


def advances(file: str, capitals: float) -> list:
    path = ROOT / "port/assets/fonts" / file
    probe = ImageFont.truetype(str(path), 1000)
    box = probe.getbbox("H")
    em = 1000 * capitals / (box[3] - box[1])
    scaled = ImageFont.truetype(str(path), 1000)
    values = [math.ceil(scaled.getlength(chr(code)) * em / 1000 * MARGIN * 10) / 10 for code in range(0x20, 0x7F)]
    # (a character the font draws no advance for, the grave accent: as wide as the widest, so that a cut text stays safe)
    return [value if value > 0 or code == 0x20 else max(values) for code, value in zip(range(0x20, 0x7F), values)]


def text() -> str:
    lines = ["/* screens_metrics.inc: written by tools/screens_metrics.py; the advances of 0x20 to 0x7E in units of the 640 x 480 menu screen */"]
    for (file, capitals), name in zip(FONTS, ("large", "small")):
        values = advances(file, capitals)
        lines.append(f"/* {file}, capitals {capitals:g} units */")
        lines.append(f"static const float screens_advance_{name}[95] =")
        lines.append("{")
        for start in range(0, 95, 10):
            lines.append("\t" + ", ".join(f"{value:.1f}f" for value in values[start:start + 10]) + ",")
        lines.append("};")
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    target = ROOT / "port/linux/game/screens_metrics.inc"
    if "--check" in sys.argv:
        sys.exit(0 if target.exists() and target.read_text() == text() else 1)
    target.write_text(text())
