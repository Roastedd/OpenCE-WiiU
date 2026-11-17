#!/usr/bin/env python3
"""Draws the Wii U app's menu icon and start-up pictures (port/wiiu/meta), the files tools/wiiu_build.py gives wuhbtool:

    python3 tools/wiiu_app_art.py            (writes icon.png, splash-tv.png, splash-drc.png)
    python3 tools/wiiu_app_art.py --check    (does not write: says whether the files in port/wiiu/meta are what this draws)

- icon.png, 128x128, the original helmet tile on navy (--icon);
- splash-tv.png, 1280x720, shown on the TV while the app starts (--tv-image);
- splash-drc.png, 854x480, shown on the GamePad meanwhile (--drc-image).

The icon restores the original upstream helmet tile. The splash screens use the Wii U's shell: a bright blue picture with a few soft bubbles, the project's helmet render (port/assets/icon/opence-icon-render.png, the artwork of
OpenCE's own icon, the same one the earlier Wii U pictures showed) in a round medallion with a white rim and a plain blue band, a bold title with a thick rounded
outline and a pill for the subtitle. The title is set in Overpass (port/assets/fonts, SIL Open Font License 1.1); the outline is drawn by Pillow's stroke, which has round
joins. The splash screens say that the port is unofficial and needs the player's own copy (the game and its characters belong to Microsoft).
Needs Pillow only (no NumPy); every shape is drawn by Pillow's own rasteriser at four times the size and scaled down with LANCZOS, so another Pillow
version may differ by a level or two at an edge (--check allows a mean difference of half a level).
"""

import argparse
import math
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont, ImageStat

ROOT = Path(__file__).resolve().parent.parent
META = ROOT / "port/wiiu/meta"
FONTS = ROOT / "port/assets/fonts"
BLACK_FONT = FONTS / "Overpass-900.ttf"
MEDIUM_FONT = FONTS / "Overpass-750.ttf"

TITLE = "OPENCE-U"
SUBTITLE = "Halo: Combat Evolved for Wii U"
NOTICE = "Unofficial fan port. Not made, endorsed or supported by Microsoft or the game's makers. It needs your own copy of the game."

SKY_TOP = (58, 178, 238)
SKY_BOTTOM = (16, 84, 190)
NAVY_TOP = (22, 70, 140)
NAVY_BOTTOM = (6, 26, 70)
RIM = (255, 255, 255)
BAND = (14, 66, 150)
INK = (8, 40, 100)
OUTLINE = (12, 58, 138)
WHITE = (255, 255, 255)
SOFT = (214, 236, 252)
PILL = (255, 255, 255)
RENDER = ROOT / "port/assets/icon/opence-icon-render.png"
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


def text_width(font, text, spacing):
    return sum(font.getlength(letter) for letter in text) + spacing * (len(text) - 1)


def draw_text(draw, anchor, text, font, fill, spacing=0.0, align="left"):
    """text in letters spaced by spacing (pixels), its left, middle or right end at anchor[0], its baseline at anchor[1]"""
    width = text_width(font, text, spacing)
    x = anchor[0] - {"left": 0, "centre": width / 2, "right": width}[align]
    for letter in text:
        draw.text((x, anchor[1]), letter, font=font, fill=fill, anchor="ls")
        x += font.getlength(letter) + spacing


def diagonal(size, first, second):
    """a gradient from the top left corner (first) to the bottom right (second)"""
    ramp = Image.linear_gradient("L")
    vertical = ramp.resize(size, Image.BILINEAR)
    horizontal = ramp.transpose(Image.ROTATE_90).resize(size, Image.BILINEAR)
    mix = ImageChops.add(vertical.point(lambda v: v // 2), horizontal.point(lambda v: v // 2))
    return Image.composite(Image.new("RGB", size, second), Image.new("RGB", size, first), mix)


def blurred(mask, radius):
    return mask.filter(ImageFilter.GaussianBlur(radius))


def outlined_text(image, anchor, text, font, fill, outline, stroke, shadow=None, spacing=0.0, align="left"):
    """text with a thick outline of round joins and, if shadow is (dx, dy, blur, alpha), a soft shadow under it: letters one by one, baseline at anchor[1]"""
    width = text_width(font, text, spacing)
    x0 = anchor[0] - {"left": 0, "centre": width / 2, "right": width}[align]
    if shadow:
        dx, dy, radius, alpha = shadow
        layer = Image.new("L", image.size, 0)
        x = x0
        for letter in text:
            ImageDraw.Draw(layer).text((x + dx, anchor[1] + dy), letter, font=font, fill=255, anchor="ls", stroke_width=stroke)
            x += font.getlength(letter) + spacing
        layer = blurred(layer, radius).point(lambda v: v * alpha)
        image.paste(Image.new("RGB", image.size, INK), (0, 0), layer)
    draw = ImageDraw.Draw(image)
    x = x0
    for letter in text:
        draw.text((x, anchor[1]), letter, font=font, fill=fill, anchor="ls", stroke_width=stroke, stroke_fill=outline)
        x += font.getlength(letter) + spacing


def disc(size, centre, radius):
    mask = Image.new("L", size, 0)
    ImageDraw.Draw(mask).ellipse([centre[0] - radius, centre[1] - radius, centre[0] + radius, centre[1] + radius], fill=255)
    return mask


def medallion(image, centre, radius):
    """the helmet render in a round medallion: a soft shadow, a white rim, a plain blue band, and inside them the render on a navy gradient"""
    size = image.size
    cx, cy = centre
    shadow = disc(size, (cx, cy + radius * 0.05), radius)
    image.paste(Image.new("RGB", size, (6, 40, 110)), (0, 0), blurred(shadow, radius * 0.05).point(lambda v: v * 0.45))
    image.paste(Image.new("RGB", size, RIM), (0, 0), disc(size, centre, radius))
    image.paste(Image.new("RGB", size, BAND), (0, 0), disc(size, centre, radius * 0.965))
    draw = ImageDraw.Draw(image)
    inner = radius * 0.86
    face = gradient(size, NAVY_TOP, NAVY_BOTTOM)
    image.paste(face, (0, 0), disc(size, centre, inner))
    # (the render, its helmet in the middle of the medallion, cut to the disc)
    render = Image.open(RENDER).convert("RGBA")
    side = round(inner * 2.15)
    render = render.resize((side, side), Image.LANCZOS)
    layer = Image.new("RGBA", size, (0, 0, 0, 0))
    layer.paste(render, (round(cx - side * 0.47), round(cy - side * 0.45)))
    alpha = ImageChops.multiply(layer.getchannel("A"), disc(size, centre, inner))
    image.paste(layer.convert("RGB"), (0, 0), alpha)


def background(size):
    """the shell's bright blue, with a few soft bubbles"""
    width, height = size
    image = gradient(size, SKY_TOP, SKY_BOTTOM)
    bubbles = Image.new("L", size, 0)
    d = ImageDraw.Draw(bubbles)
    for x, y, r, a in ((0.08, 0.12, 0.22, 46), (0.92, 0.10, 0.16, 40), (0.80, 0.98, 0.34, 46), (0.34, 1.02, 0.24, 36), (1.0, 0.55, 0.10, 34)):
        d.ellipse([width * x - height * r, height * y - height * r, width * x + height * r, height * y + height * r], fill=a)
    image.paste(Image.new("RGB", size, WHITE), (0, 0), blurred(bubbles, height * 0.004))
    return image


def icon():
    """the original Wii U tile, from ORION's upstream artwork"""
    return Image.open(ROOT / "port/assets/icon/opence-icon.png").convert("RGB").resize((128, 128), Image.LANCZOS)


def pill(image, box, fill):
    ImageDraw.Draw(image).rounded_rectangle(box, radius=(box[3] - box[1]) / 2, fill=fill)


def splash(width, height):
    """the start-up picture: the medallion at the left, the title, the subtitle in its pill and the notice at the right (everything in proportion to the height)"""
    w, h = width * SCALE, height * SCALE
    image = background((w, h))
    unit = h / 720.0
    medallion(image, (w * 0.25, h * 0.48), h * 0.34)
    left = w * 0.47
    # (the title as large as fits between the medallion and the right edge's margin, at most 120 px of a 720-line picture)
    size = round(120 * unit)
    while size > 8 and text_width(ImageFont.truetype(str(BLACK_FONT), size), TITLE, 4 * unit) > w - left - 80 * unit:
        size -= 1
    title = ImageFont.truetype(str(BLACK_FONT), size)
    outlined_text(image, (left, h * 0.46), TITLE, title, WHITE, OUTLINE, round(7 * unit), shadow=(0, 8 * unit, 6 * unit, 0.35), spacing=4 * unit)
    sub = ImageFont.truetype(str(MEDIUM_FONT), round(32 * unit))
    sub_width = text_width(sub, SUBTITLE, 1.5 * unit)
    top = h * 0.46 + 46 * unit
    pill(image, [left, top, left + sub_width + 56 * unit, top + 60 * unit], PILL)
    draw_text(ImageDraw.Draw(image), (left + 28 * unit, top + 42 * unit), SUBTITLE, sub, INK, spacing=1.5 * unit)
    note = ImageFont.truetype(str(MEDIUM_FONT), round(17 * unit))
    words, lines, line = NOTICE.split(" "), [], ""
    limit = w - left - 70 * unit
    for word in words:
        trial = (line + " " + word).strip()
        if text_width(note, trial, 0) > limit and line:
            lines.append(line)
            line = word
        else:
            line = trial
    lines.append(line)
    for i, text in enumerate(lines):
        draw_text(ImageDraw.Draw(image), (left, h * 0.46 + 148 * unit + i * 25 * unit), text, note, SOFT)
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
