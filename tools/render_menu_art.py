#!/usr/bin/env python3
"""Draws the menus' pictures that were drawn for this port
(port/assets/menus/drawn) as the PNG frames the menus embed:

    python3 tools/render_menu_art.py [drawn/....svg ...]

Each drawn/<path>.svg stands for the frame ce/<path>.png of the PC
version's bitmaps (port/assets/menus/ce/bitmaps.xml names it), where the PC
version has a 3D render or a screenshot, which are not shipped. The SVG's
width and height are the frame's size in the 640x480 menu screen; it is
drawn at twice that size, as an 8-bit RGBA PNG, the only kind the game reads
(tools/embed_assets.py checks). With no arguments, every SVG in drawn/ is
drawn. tools/ce_menus.py draws them the same way when it writes the menus
again.

Needs only Python 3 and rsvg-convert.
"""

import argparse
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ElementTree
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MENUS = ROOT / "port/assets/menus"
DRAWN = MENUS / "drawn"
SCALE = 2
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
SVG = "{http://www.w3.org/2000/svg}"


def frame_size(svg: Path) -> tuple:
    """the frame's width and height in the menu screen: the SVG's own"""
    root = ElementTree.parse(svg).getroot()
    try:
        return int(root.get("width")), int(root.get("height"))
    except (TypeError, ValueError):
        sys.exit(f"{svg}: its <svg> needs the frame's width and height, in whole units")


def title(svg: Path) -> str:
    """what the picture shows: its <title>"""
    element = ElementTree.parse(svg).getroot().find(f"{SVG}title")
    return " ".join(element.text.split()) if element is not None and element.text else ""


def png_of(svg: Path) -> str:
    """the frame's PNG, as bitmaps.xml and menus.json name it"""
    return "ce/" + svg.resolve().relative_to(DRAWN).with_suffix(".png").as_posix()


def chunks(data: bytes):
    position = len(PNG_SIGNATURE)
    while position + 12 <= len(data):
        length = struct.unpack(">I", data[position:position + 4])[0]
        yield data[position + 4:position + 8], data[position + 8:position + 8 + length]
        position += 12 + length


def chunk(kind: bytes, body: bytes) -> bytes:
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))


def checked(data: bytes, name: str, size: tuple) -> bytes:
    """rsvg-convert's PNG with only its header, pixels and end (the game
    reads no other chunks), once it is the size and the kind the game reads"""
    if data[:8] != PNG_SIGNATURE:
        sys.exit(f"{name}: rsvg-convert wrote no PNG")
    header, pixels = None, b""
    for kind, body in chunks(data):
        if kind == b"IHDR":
            header = body
        elif kind == b"IDAT":
            pixels += body
    if header is None or not pixels:
        sys.exit(f"{name}: rsvg-convert wrote a PNG with no header or no pixels")
    width, height, depth, colour, _, _, interlace = struct.unpack(">IIBBBBB", header)
    if (width, height) != size:
        sys.exit(f"{name}: drawn {width}x{height}, not {size[0]}x{size[1]}")
    if depth != 8 or colour != 6 or interlace != 0:
        sys.exit(f"{name}: drawn as a PNG that is not 8-bit RGBA and not interlaced")
    return PNG_SIGNATURE + chunk(b"IHDR", header) + chunk(b"IDAT", pixels) + chunk(b"IEND", b"")


def render(svg: Path, output: Path) -> None:
    """draws the SVG at twice its size to output"""
    width, height = frame_size(svg)
    with tempfile.TemporaryDirectory() as directory:
        drawn = Path(directory) / "drawn.png"
        subprocess.run(["rsvg-convert", "--zoom", str(SCALE), "--output", str(drawn), str(svg)], check=True)
        data = checked(drawn.read_bytes(), str(svg), (width * SCALE, height * SCALE))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(data)


def frames() -> dict:
    """the PNG frames bitmaps.xml names, and their sizes"""
    found = {}
    for frame in ElementTree.parse(MENUS / "ce/bitmaps.xml").getroot().iter("frame"):
        if frame.get("png"):
            found[frame.get("png")] = (int(frame.get("width")), int(frame.get("height")))
    return found


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("svgs", nargs="*", type=Path, help="the drawings to draw (all of drawn/ if none)")
    arguments = parser.parse_args()
    svgs = [svg.resolve() for svg in arguments.svgs] or sorted(DRAWN.rglob("*.svg"))
    named = frames()
    for svg in svgs:
        if not svg.is_file() or DRAWN not in svg.parents:
            sys.exit(f"{svg}: not a drawing in {DRAWN.relative_to(ROOT)}")
        png = png_of(svg)
        if png not in named:
            sys.exit(f"{svg}: no <frame> in ce/bitmaps.xml names {png}")
        if named[png] != frame_size(svg):
            sys.exit(f"{svg}: {frame_size(svg)[0]}x{frame_size(svg)[1]}, but its frame is "
                     f"{named[png][0]}x{named[png][1]}")
        render(svg, MENUS / png)
        print(f"{(MENUS / png).relative_to(ROOT)}: {title(svg)}")


if __name__ == "__main__":
    main()
