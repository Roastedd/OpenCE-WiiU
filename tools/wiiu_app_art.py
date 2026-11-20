#!/usr/bin/env python3
"""Draws the Wii U app's menu icon and start-up pictures (port/wiiu/meta), the files tools/wiiu_build.py gives wuhbtool:

    python3 tools/wiiu_app_art.py            (writes icon.png, splash-tv.png, splash-drc.png)
    python3 tools/wiiu_app_art.py --check    (does not write: says whether the files in port/wiiu/meta are what this draws)

- icon.png, 128x128, what Aroma's menu shows (--icon);
- splash-tv.png, 1280x720, shown on the TV while the app starts (--tv-image);
- splash-drc.png, 854x480, shown on the GamePad meanwhile (--drc-image).

The art is OpenCE's own icon (port/assets/icon/opence-icon.png) and nothing else: no text. The menu icon is the icon in a tile with round corners; each
start-up picture is the same tile, centred on the icon's own deep blue. Needs Pillow only; the tile is drawn at four times the size and scaled down with
LANCZOS, so another Pillow version may differ by a level or two at an edge (--check allows a mean difference of half a level).
"""

import argparse
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageStat

ROOT = Path(__file__).resolve().parent.parent
META = ROOT / "port/wiiu/meta"
FONTS = ROOT / "port/assets/fonts"
# (the fonts of the earlier pictures stay in the checkout: tools/test_wiiu_app_art.py checks that they are there)
BLACK_FONT = FONTS / "Overpass-900.ttf"
MEDIUM_FONT = FONTS / "Overpass-750.ttf"
ICON = ROOT / "port/assets/icon/opence-icon.png"
SCALE = 4
BACKGROUND = (8, 24, 58)


def tile(side):
    """the icon, side x side pixels, in a tile with round corners (transparent outside it)"""
    big = side * SCALE
    art = Image.open(ICON).convert("RGBA").resize((big, big), Image.LANCZOS)
    mask = Image.new("L", (big, big), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, big - 1, big - 1], radius=big * 0.18, fill=255)
    art.putalpha(ImageChops.multiply(art.getchannel("A"), mask))
    return art.resize((side, side), Image.LANCZOS)


def icon():
    result = Image.new("RGB", (128, 128), BACKGROUND)
    art = tile(128)
    result.paste(art.convert("RGB"), (0, 0), art.getchannel("A"))
    return result


def splash(width, height):
    """the icon at the middle of a plain blue picture, a little under the picture's height"""
    result = Image.new("RGB", (width, height), BACKGROUND)
    side = round(height * 0.78)
    art = tile(side)
    result.paste(art.convert("RGB"), ((width - side) // 2, (height - side) // 2), art.getchannel("A"))
    return result


def same_picture(a, b, tolerance=0.5):
    """whether two pictures are the same to the eye: the same size, and the mean difference of each channel under tolerance (levels of 255). A
    Pillow or FreeType of another version rasterises an edge a little differently; the pictures in the repository are what this machine's draws"""
    if a.size != b.size:
        return False
    difference = ImageChops.difference(a.convert("RGB"), b.convert("RGB"))
    return max(ImageStat.Stat(difference).mean) < tolerance


def pictures():
    return {"icon.png": icon(), "splash-tv.png": splash(1280, 720), "splash-drc.png": splash(854, 480)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="write nothing; exit 1 if port/wiiu/meta's files are not what this draws")
    parser.add_argument("--output", type=Path, default=META, help="the folder to write (default port/wiiu/meta)")
    args = parser.parse_args()
    stale = []
    for name, image in pictures().items():
        path = args.output / name
        if args.check:
            if not path.is_file() or not same_picture(Image.open(path), image):
                stale.append(name)
            continue
        args.output.mkdir(parents=True, exist_ok=True)
        image.save(path, optimize=True)
        print(f"{path.relative_to(ROOT) if path.is_relative_to(ROOT) else path}: {image.width}x{image.height}")
    if stale:
        print("not what tools/wiiu_app_art.py draws:", ", ".join(stale))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
