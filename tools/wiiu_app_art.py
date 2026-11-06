#!/usr/bin/env python3
"""Draws the Wii U app's menu icon and start-up pictures (port/wiiu/meta), the files tools/wiiu_build.py gives wuhbtool:

    python3 tools/wiiu_app_art.py            (writes icon.png, splash-tv.png, splash-drc.png)
    python3 tools/wiiu_app_art.py --check    (does not write: says whether the files in port/wiiu/meta are what this draws)

- icon.png, 128x128, what Aroma's menu shows (--icon);
- splash-tv.png, 1280x720, shown on the TV while the app starts (--tv-image);
- splash-drc.png, 854x480, shown on the GamePad meanwhile (--drc-image).

The look is the Wii U's own shell: a clean white and pale-blue picture with soft round shapes, a glossy blue-and-teal ring (a ring world, drawn here from
circles and gradients: it is not the game's logo or any picture of the game, which belong to Microsoft) with "CE" (Combat Evolved) in it, a bold title with a
thick rounded outline, and a pill for the subtitle. The title is set in Overpass (port/assets/fonts, SIL Open Font License 1.1); the outline is drawn by
Pillow's stroke, which has round joins. The splash screens say that the port is unofficial and needs the player's own copy.
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

PAPER_TOP = (252, 254, 255)
PAPER_BOTTOM = (206, 232, 248)
RING_LIGHT = (96, 226, 236)
RING_DARK = (16, 84, 196)
INK = (14, 70, 150)
OUTLINE = (24, 128, 220)
WHITE = (255, 255, 255)
MUTED = (84, 118, 150)
PILL = (230, 244, 252)
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


def ring(image, centre, radius, letters=True):
    """the badge: a glossy ring (blue and teal, a soft shadow under it, a bright arc along its upper left edge, a small moon on its rim) with "CE" in it"""
    size = image.size
    cx, cy = centre
    # (the shadow, a little below and to the right)
    shadow = Image.new("L", size, 0)
    ImageDraw.Draw(shadow).ellipse([cx - radius, cy - radius + radius * 0.07, cx + radius, cy + radius + radius * 0.07], fill=170)
    image.paste(Image.new("RGB", size, (96, 150, 196)), (0, 0), blurred(shadow, radius * 0.05))
    # (the band: outer circle minus the inner one, filled with the gradient)
    band = Image.new("L", size, 0)
    d = ImageDraw.Draw(band)
    d.ellipse([cx - radius, cy - radius, cx + radius, cy + radius], fill=255)
    d.ellipse([cx - radius * 0.70, cy - radius * 0.70, cx + radius * 0.70, cy + radius * 0.70], fill=0)
    image.paste(diagonal(size, RING_LIGHT, RING_DARK), (0, 0), band)
    # (the inside of the ring: a paler disc, so that the letters stand on something)
    disc = Image.new("L", size, 0)
    ImageDraw.Draw(disc).ellipse([cx - radius * 0.70, cy - radius * 0.70, cx + radius * 0.70, cy + radius * 0.70], fill=255)
    image.paste(diagonal(size, (255, 255, 255), (214, 236, 250)), (0, 0), disc)
    # (the rim lights: a thin white line inside the band's edges, and the bright arc at the upper left)
    draw = ImageDraw.Draw(image)
    line = max(2, round(radius * 0.025))
    draw.ellipse([cx - radius * 0.985, cy - radius * 0.985, cx + radius * 0.985, cy + radius * 0.985], outline=(214, 248, 250), width=line)
    gloss = Image.new("L", size, 0)
    ImageDraw.Draw(gloss).arc([cx - radius * 0.90, cy - radius * 0.90, cx + radius * 0.90, cy + radius * 0.90], 195, 275, fill=255, width=max(3, round(radius * 0.07)))
    image.paste(Image.new("RGB", size, WHITE), (0, 0), blurred(gloss, radius * 0.012))
    # (the ring world's segments: short light ticks along the middle of the band)
    ticks = Image.new("L", size, 0)
    td = ImageDraw.Draw(ticks)
    for i in range(36):
        angle = math.radians(i * 10 + 5)
        inner, outer = radius * 0.80, radius * 0.90
        td.line([(cx + inner * math.cos(angle), cy + inner * math.sin(angle)), (cx + outer * math.cos(angle), cy + outer * math.sin(angle))],
                fill=110, width=max(2, round(radius * 0.022)))
    image.paste(Image.new("RGB", size, WHITE), (0, 0), ticks)
    # (the moon on the rim at the upper right, with its own small gloss)
    mx, my, mr = cx + radius * 0.74, cy - radius * 0.74, radius * 0.13
    draw.ellipse([mx - mr * 1.5, my - mr * 1.5, mx + mr * 1.5, my + mr * 1.5], fill=WHITE)
    draw.ellipse([mx - mr, my - mr, mx + mr, my + mr], fill=RING_DARK)
    draw.ellipse([mx - mr * 0.55, my - mr * 0.6, mx - mr * 0.05, my - mr * 0.1], fill=(170, 226, 250))
    if letters:
        font = ImageFont.truetype(str(BLACK_FONT), round(radius * 0.62))
        outlined_text(image, (cx, cy + radius * 0.22), "CE", font, WHITE, OUTLINE, max(2, round(radius * 0.05)),
                      shadow=(0, radius * 0.03, radius * 0.02, 0.35), spacing=radius * 0.01, align="centre")


def sparkle(image, centre, radius, alpha=230):
    """a four-pointed star, the Wii U shell's twinkle"""
    layer = Image.new("L", image.size, 0)
    cx, cy = centre
    k = radius * 0.22
    ImageDraw.Draw(layer).polygon([(cx, cy - radius), (cx + k, cy - k), (cx + radius, cy), (cx + k, cy + k), (cx, cy + radius), (cx - k, cy + k),
                                   (cx - radius, cy), (cx - k, cy - k)], fill=alpha)
    image.paste(Image.new("RGB", image.size, WHITE), (0, 0), blurred(layer, radius * 0.02))


def background(size):
    """the white and pale-blue paper of the Wii U's shell, with soft round bubbles"""
    width, height = size
    image = gradient(size, PAPER_TOP, PAPER_BOTTOM)
    bubbles = Image.new("L", size, 0)
    d = ImageDraw.Draw(bubbles)
    for x, y, r, a in ((0.10, 0.20, 0.20, 90), (0.88, 0.14, 0.15, 70), (0.78, 0.92, 0.30, 90), (0.30, 0.98, 0.22, 70), (0.97, 0.55, 0.10, 60), (0.52, 0.06, 0.07, 60)):
        d.ellipse([width * x - height * r, height * y - height * r, width * x + height * r, height * y + height * r], fill=a)
    image.paste(Image.new("RGB", size, WHITE), (0, 0), blurred(bubbles, height * 0.004))
    return image


def icon():
    size = 128 * SCALE
    image = background((size, size))
    # (a tile with round corners, as the Wii U's channels are: the picture shows through outside it)
    ring(image, (size / 2, size / 2), size * 0.38)
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, size - 1, size - 1], radius=size * 0.20, fill=255)
    result = Image.new("RGB", (size, size), PAPER_BOTTOM)
    result.paste(image, (0, 0), mask)
    return result.resize((128, 128), Image.LANCZOS)


def pill(image, box, fill):
    ImageDraw.Draw(image).rounded_rectangle(box, radius=(box[3] - box[1]) / 2, fill=fill)


def splash(width, height):
    """the start-up picture: the ring at the left, the title, the subtitle in its pill and the notice at the right (everything in proportion to the height)"""
    w, h = width * SCALE, height * SCALE
    image = background((w, h))
    unit = h / 720.0
    ring(image, (w * 0.24, h * 0.47), h * 0.30)
    for x, y, r in ((0.43, 0.20, 0.030), (0.93, 0.80, 0.026), (0.075, 0.82, 0.022), (0.40, 0.88, 0.016)):
        sparkle(image, (w * x, h * y), h * r)
    left = w * 0.46
    # (the title as large as fits between the ring and the right edge's margin, at most 120 px of a 720-line picture)
    size = round(120 * unit)
    while size > 8 and text_width(ImageFont.truetype(str(BLACK_FONT), size), TITLE, 4 * unit) > w - left - 90 * unit:
        size -= 1
    title = ImageFont.truetype(str(BLACK_FONT), size)
    outlined_text(image, (left, h * 0.45), TITLE, title, WHITE, OUTLINE, round(7 * unit), shadow=(0, 8 * unit, 6 * unit, 0.30), spacing=4 * unit)
    sub = ImageFont.truetype(str(MEDIUM_FONT), round(32 * unit))
    sub_width = text_width(sub, SUBTITLE, 1.5 * unit)
    top = h * 0.45 + 48 * unit
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
        draw_text(ImageDraw.Draw(image), (left, h * 0.45 + 150 * unit + i * 25 * unit), text, note, MUTED)
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
