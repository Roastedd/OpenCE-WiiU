#!/usr/bin/env python3
"""Draws the Wii U app's menu icon and start-up pictures (port/wiiu/meta), the files tools/wiiu_build.py gives wuhbtool:

    python3 tools/wiiu_app_art.py            (writes icon.png, splash-tv.png, splash-drc.png)
    python3 tools/wiiu_app_art.py --check    (does not write: says whether the files in port/wiiu/meta are what this draws)

- icon.png, 128x128, what Aroma's menu shows (--icon);
- splash-tv.png, 1280x720, shown on the TV while the app starts (--tv-image);
- splash-drc.png, 854x480, shown on the GamePad meanwhile (--drc-image).

The look is a night sky over a ring world: a deep navy gradient with a field of stars, a warm sunrise glow on the horizon, a bright banded ring arching
across the sky, and a steel helmet with a gold visor, drawn here from polygons (original artwork, no game assets), lit cool from the left and warm from the
right. The title is set in Overpass (port/assets/fonts, SIL Open Font License 1.1) in white with a cyan glow; the subtitle is letter-spaced; the splash
screens say that the port is unofficial and needs the player's own copy (the game and its characters belong to Microsoft).
Needs Pillow only (no NumPy); every shape is drawn by Pillow's own rasteriser at a larger size and scaled down with LANCZOS, so another Pillow version may
differ by a level or two at an edge (--check allows a mean difference of half a level).
"""

import argparse
import math
import random
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont, ImageStat

ROOT = Path(__file__).resolve().parent.parent
META = ROOT / "port/wiiu/meta"
FONTS = ROOT / "port/assets/fonts"
BLACK_FONT = FONTS / "Overpass-900.ttf"
MEDIUM_FONT = FONTS / "Overpass-750.ttf"

TITLE = "OPENCE-U"
SUBTITLE = "HALO: COMBAT EVOLVED"
PLATFORM = "FOR NINTENDO WII U"
NOTICE = "Unofficial fan port. Not made, endorsed or supported by Microsoft or the game's makers. It needs your own copy of the game."

SPACE_TOP = (3, 8, 24)
SPACE_MID = (8, 22, 58)
SPACE_LOW = (18, 56, 110)
GLOW_WARM = (255, 170, 60)
CYAN = (90, 220, 255)
WHITE = (255, 255, 255)
SOFT = (170, 200, 230)
GOLD = (255, 205, 90)
SCALE = 2


def lerp(a, b, t):
    return tuple(round(a[c] + (b[c] - a[c]) * t) for c in range(3))


def gradient(size, stops):
    """a vertical gradient through stops [(position 0..1, colour), ...]"""
    width, height = size
    image = Image.new("RGB", size)
    draw = ImageDraw.Draw(image)
    for y in range(height):
        t = y / max(1, height - 1)
        for (p0, c0), (p1, c1) in zip(stops, stops[1:]):
            if p0 <= t <= p1:
                draw.line([(0, y), (width, y)], fill=lerp(c0, c1, (t - p0) / max(1e-9, p1 - p0)))
                break
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


def blurred(mask, radius):
    return mask.filter(ImageFilter.GaussianBlur(max(0.1, radius)))


def glow(image, mask, colour, radius, strength):
    """a soft light of colour around the shape of mask (an 'L' image)"""
    layer = blurred(mask, radius).point(lambda v: min(255, int(v * strength)))
    image.paste(Image.new("RGB", image.size, colour), (0, 0), layer)


def disc(size, centre, radius):
    mask = Image.new("L", size, 0)
    ImageDraw.Draw(mask).ellipse([centre[0] - radius, centre[1] - radius, centre[0] + radius, centre[1] + radius], fill=255)
    return mask


def sky(size, seed=7):
    """the night sky: a gradient, stars, the sunrise glow on the horizon and the ring arching across it"""
    w, h = size
    image = gradient(size, [(0.0, SPACE_TOP), (0.55, SPACE_MID), (1.0, SPACE_LOW)])
    rng = random.Random(seed)
    stars = Image.new("L", size, 0)
    bright = Image.new("L", size, 0)
    sd, bd = ImageDraw.Draw(stars), ImageDraw.Draw(bright)
    unit = h / 720.0
    for _ in range(int(w * h / (26000 * (SCALE / 2) ** 2)) + 40):
        x, y = rng.random() * w, rng.random() ** 1.6 * h * 0.95
        r = rng.choice((0.7, 0.9, 1.1, 1.4)) * unit * SCALE / 2
        a = rng.randint(90, 255)
        sd.ellipse([x - r, y - r, x + r, y + r], fill=a)
        if rng.random() < 0.06:
            bd.ellipse([x - 2.2 * r, y - 2.2 * r, x + 2.2 * r, y + 2.2 * r], fill=200)
    image.paste(Image.new("RGB", size, (220, 235, 255)), (0, 0), stars)
    glow(image, bright, (150, 200, 255), 5 * unit, 1.0)
    # (the sunrise: a warm glow low on the right, under the ring)
    sun = Image.new("L", size, 0)
    ImageDraw.Draw(sun).ellipse([w * 0.55, h * 0.78, w * 1.35, h * 1.5], fill=255)
    glow(image, sun, GLOW_WARM, h * 0.16, 0.55)
    glow(image, sun, (255, 230, 170), h * 0.06, 0.35)
    # (the planet's dark limb under the glow)
    limb = Image.new("L", size, 0)
    ImageDraw.Draw(limb).ellipse([w * 0.50, h * 0.90, w * 1.45, h * 2.2], fill=255)
    image.paste(Image.new("RGB", size, (3, 8, 22)), (0, 0), limb)
    rim = Image.new("L", size, 0)
    rd = ImageDraw.Draw(rim)
    rd.ellipse([w * 0.50, h * 0.90, w * 1.45, h * 2.2], outline=255, width=max(2, round(3 * unit)))
    glow(image, rim, (255, 190, 100), 3 * unit, 1.0)
    # (the ring: an arc of a huge circle across the upper right, a bright band with darker lanes)
    ring = Image.new("L", size, 0)
    cx, cy, radius = w * 0.50, h * 2.02, h * 1.92
    lanes = (14, 5, 9, 4)
    d = ImageDraw.Draw(ring)
    off = 0.0
    for i, lane in enumerate(lanes):
        r_out = radius - off
        r_in = r_out - lane * unit
        shade = (255, 200, 235, 170)[i]
        lane_mask = Image.new("L", size, 0)
        ImageDraw.Draw(lane_mask).ellipse([cx - r_out, cy - r_out, cx + r_out, cy + r_out], fill=shade)
        ImageDraw.Draw(lane_mask).ellipse([cx - r_in, cy - r_in, cx + r_in, cy + r_in], fill=0)
        ring = ImageChops.lighter(ring, lane_mask)
        off += (lane + 2) * unit
    # (fade the ring toward its ends so it reads as an arc across the top of the picture)
    fade = Image.linear_gradient("L").transpose(Image.ROTATE_90).resize(size, Image.BILINEAR)
    fade = fade.point(lambda v: int(255 * (0.30 + 0.70 * math.sin(math.pi * v / 255.0))))
    ring = ImageChops.multiply(ring, fade)
    glow(image, ring, CYAN, 9 * unit, 0.7)
    image.paste(Image.new("RGB", size, (200, 238, 255)), (0, 0), ring)
    return image


HELMET_R = [(500, 90), (610, 100), (715, 160), (790, 270), (830, 410), (835, 540), (815, 650), (770, 740), (700, 810), (610, 850), (540, 872), (500, 876)]
VISOR_R = [(500, 345), (690, 345), (782, 398), (802, 472), (772, 556), (700, 612), (600, 642), (500, 648)]
CREST_R = [(500, 96), (540, 98), (556, 330), (500, 330)]
GUARD_R = [(500, 690), (572, 690), (594, 812), (500, 840)]


def mirrored(points):
    return points + [(1000 - x, y) for x, y in reversed(points)]


def helmet(image, centre, height):
    """the helmet: steel, with a gold visor, a crest, a mouth guard and cheek vents; centre is the middle of its 1000x1000 box, height its pixel height"""
    size = image.size
    k = height / 1000.0
    ox, oy = centre[0] - 500 * k, centre[1] - 500 * k

    def p(points):
        return [(ox + x * k, oy + y * k) for x, y in points]

    outline = mirrored(HELMET_R)
    mask = Image.new("L", size, 0)
    ImageDraw.Draw(mask).polygon(p(outline), fill=255)
    # (a halo of cool light behind it, then its shadow side)
    glow(image, mask, CYAN, height * 0.06, 0.55)
    # (the steel: lit from the upper left; a vertical gradient under a horizontal one)
    steel = gradient(size, [(0.0, (236, 244, 252)), (0.45, (178, 192, 210)), (1.0, (92, 104, 126))])
    shade = Image.linear_gradient("L").rotate(90).resize(size, Image.BILINEAR)
    side = Image.new("RGB", size, (14, 24, 52))
    shade_mask = Image.new("L", size, 0)
    ramp = Image.linear_gradient("L").transpose(Image.ROTATE_270).resize((round(1000 * k), round(1000 * k)), Image.BILINEAR)
    shade_mask.paste(ramp.point(lambda v: int(v * 0.62)), (round(ox), round(oy)))
    body = steel.copy()
    body.paste(side, (0, 0), shade_mask)
    image.paste(body, (0, 0), mask)
    d = ImageDraw.Draw(image)
    line = max(2, round(height * 0.006))
    # (panel lines)
    crest = Image.new("L", size, 0)
    ImageDraw.Draw(crest).polygon(p(mirrored(CREST_R)), fill=255)
    image.paste(Image.new("RGB", size, (70, 86, 112)), (0, 0), crest.point(lambda v: int(v * 0.55)))
    d.polygon(p(mirrored(CREST_R)), outline=(30, 44, 70), width=line)
    facets = Image.new("L", size, 0)
    fd = ImageDraw.Draw(facets)
    fd.polygon(p([(300, 210), (415, 130), (470, 135), (440, 300), (290, 335)]), fill=255)
    fd.polygon(p([(215, 330), (290, 335), (270, 470), (190, 440)]), fill=255)
    image.paste(Image.new("RGB", size, (255, 255, 255)), (0, 0), ImageChops.multiply(facets, mask).point(lambda v: int(v * 0.28)))
    shadows = Image.new("L", size, 0)
    sd = ImageDraw.Draw(shadows)
    sd.polygon(p([(600, 140), (700, 190), (770, 300), (790, 420), (640, 330)]), fill=255)
    image.paste(Image.new("RGB", size, (20, 34, 64)), (0, 0), ImageChops.multiply(shadows, mask).point(lambda v: int(v * 0.32)))
    d.line(p([(300, 210), (415, 130)]), fill=(210, 226, 244), width=max(1, line // 2))
    guard = Image.new("L", size, 0)
    ImageDraw.Draw(guard).polygon(p(mirrored(GUARD_R)), fill=255)
    image.paste(Image.new("RGB", size, (40, 52, 76)), (0, 0), guard)
    for i in range(4):
        x = 470 + i * 20
        d.line(p([(x, 705), (x - 4 + (i - 1.5) * 3, 805)]), fill=(120, 140, 170), width=max(1, line // 2))
    for side_x in (1, -1):
        for j in range(3):
            y = 690 + j * 26
            x0, x1 = 230 + j * 6, 330 - j * 6
            pts = [(x0, y), (x1, y + 6)] if side_x == 1 else [(1000 - x0, y), (1000 - x1, y + 6)]
            d.line(p(pts), fill=(40, 54, 82), width=line)
    # (the visor: gold, dark amber at the top, a bright sweep, a dark reflection)
    vmask = Image.new("L", size, 0)
    ImageDraw.Draw(vmask).polygon(p(mirrored(VISOR_R)), fill=255)
    ys = [y for _, y in p(VISOR_R)]
    t0, t1 = min(ys) / size[1], max(ys) / size[1]
    span = t1 - t0
    gold = gradient(size, [(0.0, (70, 40, 0)), (t0, (70, 40, 0)), (t0 + 0.25 * span, (150, 92, 8)), (t0 + 0.62 * span, (255, 188, 40)), (t1, (255, 214, 96)), (1.0, (255, 214, 96))])
    glow(image, vmask, GOLD, height * 0.035, 0.45)
    image.paste(gold, (0, 0), vmask)
    sweep = Image.new("L", size, 0)
    ImageDraw.Draw(sweep).polygon(p([(250, 410), (410, 360), (470, 360), (300, 560), (262, 556)]), fill=255)
    sweep = ImageChops.multiply(sweep, vmask)
    image.paste(Image.new("RGB", size, (255, 250, 200)), (0, 0), blurred(sweep, height * 0.004).point(lambda v: int(v * 0.85)))
    dark = Image.new("L", size, 0)
    ImageDraw.Draw(dark).polygon(p([(560, 360), (760, 380), (740, 470), (640, 540), (590, 500)]), fill=255)
    dark = ImageChops.multiply(dark, vmask)
    image.paste(Image.new("RGB", size, (55, 28, 0)), (0, 0), blurred(dark, height * 0.003).point(lambda v: int(v * 0.8)))
    d.polygon(p(mirrored(VISOR_R)), outline=(40, 24, 4), width=line)
    # (the outline and a warm rim light on the right edge)
    d.polygon(p(outline), outline=(18, 30, 58), width=line + 1)
    rim = Image.new("L", size, 0)
    ImageDraw.Draw(rim).line(p(HELMET_R[2:9]), fill=255, width=max(2, round(height * 0.012)))
    glow(image, rim, GLOW_WARM, height * 0.008, 1.0)


def icon():
    size = 128 * SCALE * 2
    image = sky((size, size), seed=11)
    helmet(image, (size * 0.5, size * 0.56), size * 0.74)
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, size - 1, size - 1], radius=size * 0.20, fill=255)
    result = Image.new("RGB", (size, size), SPACE_TOP)
    result.paste(image, (0, 0), mask)
    # (a thin light edge on the tile)
    edge = Image.new("L", (size, size), 0)
    ImageDraw.Draw(edge).rounded_rectangle([0, 0, size - 1, size - 1], radius=size * 0.20, outline=255, width=max(2, size // 64))
    result.paste(Image.new("RGB", (size, size), (120, 190, 240)), (0, 0), edge.point(lambda v: int(v * 0.6)))
    return result.resize((128, 128), Image.LANCZOS)


def splash(width, height):
    """the start-up picture: the helmet at the left, the title, the subtitle, the platform and the notice at the right (everything in proportion to the height)"""
    w, h = width * SCALE, height * SCALE
    image = sky((w, h))
    unit = h / 720.0
    helmet(image, (w * 0.25, h * 0.575), h * 0.74)
    left = w * 0.47
    size = round(124 * unit)
    while size > 8 and text_width(ImageFont.truetype(str(BLACK_FONT), size), TITLE, 5 * unit) > w - left - 70 * unit:
        size -= 1
    title = ImageFont.truetype(str(BLACK_FONT), size)
    base = h * 0.43
    mask = Image.new("L", image.size, 0)
    draw_text(ImageDraw.Draw(mask), (left, base), TITLE, title, 255, spacing=5 * unit)
    glow(image, mask, CYAN, 10 * unit, 0.9)
    shadow = blurred(mask, 4 * unit).point(lambda v: int(v * 0.6))
    shifted = Image.new("L", image.size, 0)
    shifted.paste(shadow, (0, round(6 * unit)))
    image.paste(Image.new("RGB", image.size, (2, 8, 24)), (0, 0), shifted)
    draw = ImageDraw.Draw(image)
    draw_text(draw, (left, base), TITLE, title, WHITE, spacing=5 * unit)
    # (a thin line of light under the title)
    line_y = base + 22 * unit
    bar = Image.new("L", image.size, 0)
    ImageDraw.Draw(bar).rectangle([left, line_y, left + (w - left - 90 * unit), line_y + 3 * unit], fill=255)
    glow(image, bar, GOLD, 4 * unit, 0.8)
    image.paste(Image.new("RGB", image.size, GOLD), (0, 0), bar)
    sub = ImageFont.truetype(str(MEDIUM_FONT), round(36 * unit))
    draw = ImageDraw.Draw(image)
    draw_text(draw, (left, line_y + 58 * unit), SUBTITLE, sub, (240, 246, 255), spacing=5 * unit)
    plat = ImageFont.truetype(str(MEDIUM_FONT), round(22 * unit))
    draw_text(draw, (left, line_y + 98 * unit), PLATFORM, plat, GOLD, spacing=8 * unit)
    note = ImageFont.truetype(str(MEDIUM_FONT), round(16 * unit))
    words, lines, current = NOTICE.split(" "), [], ""
    limit = w - left - 90 * unit
    for word in words:
        trial = (current + " " + word).strip()
        if text_width(note, trial, 0) > limit and current:
            lines.append(current)
            current = word
        else:
            current = trial
    lines.append(current)
    for i, text in enumerate(lines):
        draw_text(draw, (left, h * 0.90 - (len(lines) - 1 - i) * 24 * unit), text, note, SOFT)
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
