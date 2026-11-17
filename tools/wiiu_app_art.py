#!/usr/bin/env python3
"""Draws the Wii U app's menu icon and start-up pictures (port/wiiu/meta), the files tools/wiiu_build.py gives wuhbtool:

    python3 tools/wiiu_app_art.py            (writes icon.png, splash-tv.png, splash-drc.png)
    python3 tools/wiiu_app_art.py --check    (does not write: says whether the files in port/wiiu/meta are what this draws)

- icon.png, 128x128, the original helmet tile on navy (--icon);
- splash-tv.png, 1280x720, shown on the TV while the app starts (--tv-image);
- splash-drc.png, 854x480, shown on the GamePad meanwhile (--drc-image).

The icon restores ORION's original upstream helmet tile. Both splash screens
show the helmet artwork on navy, with no text. Attribution and the fan-port
notice are retained in AUTHORS.md and the player guide.
Needs Pillow only (no NumPy); every shape is drawn by Pillow's own rasteriser at four times the size and scaled down with LANCZOS, so another Pillow
version may differ by a level or two at an edge (--check allows a mean difference of half a level).
"""

import argparse
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageStat

ROOT = Path(__file__).resolve().parent.parent
META = ROOT / "port/wiiu/meta"
ARTWORK = ROOT / "port/assets/icon"
ICON_SOURCE = ARTWORK / "opence-icon.png"
RENDER = ARTWORK / "opence-icon-render.png"
SCALE = 4


def gradient(size, top, bottom):
    width, height = size
    image = Image.new("RGB", size)
    draw = ImageDraw.Draw(image)
    for y in range(height):
        fraction = y / max(1, height - 1)
        color = tuple(round(top[c] + (bottom[c] - top[c]) * fraction) for c in range(3))
        draw.line([(0, y), (width, y)], fill=color)
    return image


def icon():
    """the original Wii U tile, from ORION's upstream artwork"""
    return Image.open(ICON_SOURCE).convert("RGB").resize((128, 128), Image.LANCZOS)


def splash(width, height):
    """artwork only: the original helmet on a dark navy background"""
    w, h = width * SCALE, height * SCALE
    image = gradient((w, h), (12, 28, 48), (4, 10, 20))
    helmet = Image.open(RENDER).convert("RGBA")
    side = round(h * 0.92)
    helmet = helmet.resize((side, side), Image.LANCZOS)
    image.paste(helmet, ((w - side) // 2, h - side), helmet)
    return image.resize((width, height), Image.LANCZOS)


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
