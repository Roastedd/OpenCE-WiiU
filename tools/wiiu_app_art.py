#!/usr/bin/env python3
"""Draws the Wii U app's menu icon and start-up pictures (port/wiiu/meta), the files tools/wiiu_build.py gives wuhbtool:

    python3 tools/wiiu_app_art.py            (writes icon.png, splash-tv.png, splash-drc.png)
    python3 tools/wiiu_app_art.py --check    (does not write: says whether the files in port/wiiu/meta are what this draws)

- icon.png, 128x128, what Aroma's menu shows (--icon);
- splash-tv.png, 1280x720, shown on the TV while the app starts (--tv-image);
- splash-drc.png, 854x480, shown on the GamePad meanwhile (--drc-image).

The art is the port's own and made here from shapes and text, so that the file can be made again: a hexagon with "CE" in it (Combat Evolved),
on a dark gradient; the title is set in Overpass (port/assets/fonts, SIL Open Font License 1.1). It uses no picture of the game, of its
characters or of its logo, which belong to Microsoft; the splash screens say that the port is unofficial and needs the player's own copy.
Needs Pillow only (no NumPy); every shape is drawn by Pillow's own rasteriser at four times the size and scaled down with LANCZOS, so another Pillow
version may differ by a level or two at an edge (--check allows a mean difference of half a level).
"""

import argparse
import math
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont, ImageStat

ROOT = Path(__file__).resolve().parent.parent
META = ROOT / "port/wiiu/meta"
FONTS = ROOT / "port/assets/fonts"
BLACK_FONT = FONTS / "Overpass-900.ttf"
MEDIUM_FONT = FONTS / "Overpass-750.ttf"

TITLE = "HALO: COMBAT EVOLVED"
SUBTITLE = "OpenCE for Wii U"
NOTICE = "Unofficial fan port. Not made, endorsed or supported by Microsoft or the game's makers. It needs your own copy of the game."

SKY_TOP = (13, 38, 66)
SKY_BOTTOM = (3, 10, 20)
ACCENT_LIGHT = (96, 232, 214)
ACCENT_DARK = (18, 148, 160)
FACE = (6, 20, 34)
WHITE = (244, 250, 252)
MUTED = (150, 178, 196)
SCALE = 4


def gradient(size, top, bottom):
    """a vertical gradient, rows from top to bottom"""
    width, height = size
    image = Image.new("RGB", size)
    draw = ImageDraw.Draw(image)
    for y in range(height):
        t = y / max(1, height - 1)
        draw.line([(0, y), (width, y)], fill=tuple(round(top[c] + (bottom[c] - top[c]) * t) for c in range(3)))
    return image


def hexagon(centre, radius, turn=90.0):
    """the corners of a regular hexagon, one pointing up (turn 90)"""
    return [(centre[0] + radius * math.cos(math.radians(turn + 60 * i)),
             centre[1] - radius * math.sin(math.radians(turn + 60 * i))) for i in range(6)]


def text_width(font, text, spacing):
    return sum(font.getlength(letter) for letter in text) + spacing * (len(text) - 1)


def draw_text(draw, anchor, text, font, fill, spacing=0.0, align="left"):
    """text in letters spaced by spacing (pixels), its left, middle or right end at anchor[0], its baseline at anchor[1]"""
    width = text_width(font, text, spacing)
    x = anchor[0] - {"left": 0, "centre": width / 2, "right": width}[align]
    for letter in text:
        draw.text((x, anchor[1]), letter, font=font, fill=fill, anchor="ls")
        x += font.getlength(letter) + spacing


def emblem(image, centre, radius):
    """the badge: a hexagon in the accent's gradient, a dark face inside it, "CE" on the face and two chevrons under it"""
    draw = ImageDraw.Draw(image)
    outer = hexagon(centre, radius)
    # (the rim: the accent's gradient, light at the top)
    rim = gradient(image.size, ACCENT_LIGHT, ACCENT_DARK)
    mask = Image.new("L", image.size, 0)
    ImageDraw.Draw(mask).polygon(outer, fill=255)
    image.paste(rim, (0, 0), mask)
    draw.polygon(hexagon(centre, radius * 0.86), fill=FACE)
    draw.polygon(hexagon(centre, radius * 0.80), outline=ACCENT_DARK, width=max(1, round(radius * 0.02)))
    font = ImageFont.truetype(str(BLACK_FONT), round(radius * 0.74))
    draw_text(draw, (centre[0], centre[1] + radius * 0.20), "CE", font, WHITE, spacing=radius * 0.02, align="centre")
    for i, shade in enumerate((ACCENT_LIGHT, ACCENT_DARK)):
        y = centre[1] + radius * (0.40 + 0.14 * i)
        half = radius * 0.22
        draw.line([(centre[0] - half, y), (centre[0], y - half * 0.55), (centre[0] + half, y)], fill=shade, width=max(2, round(radius * 0.045)),
                  joint="curve")


def lattice(image, step, colour):
    """a faint pattern of hexagons over the background (the splash screens'): each as wide as step, in rows that interlock"""
    draw = ImageDraw.Draw(image)
    radius = step / math.sqrt(3)
    row = 0
    y = -radius
    while y < image.height + radius:
        x = -step + (step / 2 if row % 2 else 0)
        while x < image.width + step:
            draw.polygon(hexagon((x, y), radius), outline=colour, width=max(1, round(image.height / 360)))
            x += step
        y += radius * 1.5
        row += 1


def icon():
    size = 128 * SCALE
    image = gradient((size, size), SKY_TOP, SKY_BOTTOM)
    emblem(image, (size / 2, size / 2), size * 0.44)
    return image.resize((128, 128), Image.LANCZOS)


def splash(width, height):
    """the start-up picture: the emblem at the left, the title, the subtitle and the notice at the right (everything in proportion to the height)"""
    w, h = width * SCALE, height * SCALE
    image = gradient((w, h), SKY_TOP, SKY_BOTTOM)
    # (the lattice is faint: a few levels brighter than the sky)
    layer = gradient((w, h), (22, 56, 88), (10, 24, 38))
    pattern = Image.new("L", (w, h), 0)
    lattice(pattern, h / 9.0, 255)
    pattern = pattern.point(lambda v: v * 0.22)
    image.paste(layer, (0, 0), pattern)
    draw = ImageDraw.Draw(image)
    unit = h / 720.0
    emblem(image, (w * 0.22, h * 0.46), h * 0.29)
    left = w * 0.43
    # (the title as large as fits between the emblem and the right edge's margin, at most 76 px of a 720-line picture)
    size = round(76 * unit)
    while size > 8 and text_width(ImageFont.truetype(str(BLACK_FONT), size), "COMBAT EVOLVED", 3 * unit) > w - left - 70 * unit:
        size -= 1
    title = ImageFont.truetype(str(BLACK_FONT), size)
    # (the title in two lines, which fits both pictures: HALO: / COMBAT EVOLVED)
    draw_text(draw, (left, h * 0.45), "HALO:", title, WHITE, spacing=6 * unit)
    draw_text(draw, (left, h * 0.45 + 84 * unit), "COMBAT EVOLVED", title, WHITE, spacing=3 * unit)
    draw.rectangle([left, h * 0.45 + 112 * unit, left + 120 * unit, h * 0.45 + 118 * unit], fill=ACCENT_LIGHT)
    sub = ImageFont.truetype(str(MEDIUM_FONT), round(34 * unit))
    draw_text(draw, (left, h * 0.45 + 168 * unit), SUBTITLE, sub, ACCENT_LIGHT, spacing=2 * unit)
    note = ImageFont.truetype(str(MEDIUM_FONT), round(17 * unit))
    words, lines, line = NOTICE.split(" "), [], ""
    limit = w - left - 60 * unit
    for word in words:
        trial = (line + " " + word).strip()
        if text_width(note, trial, 0) > limit and line:
            lines.append(line)
            line = word
        else:
            line = trial
    lines.append(line)
    for i, text in enumerate(lines):
        draw_text(draw, (left, h * 0.45 + 232 * unit + i * 24 * unit), text, note, MUTED)
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
