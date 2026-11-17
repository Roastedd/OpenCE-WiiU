#!/usr/bin/env python3
"""Package the source-only computer setup helper; no game data or binaries."""
import argparse
import hashlib
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FILES = (
    "setup_wiiu.py", "Setup Wii U.bat", "Setup Wii U.command", "LICENSE", "AUTHORS.md",
    "docs/ISO_SETUP.md", "tools/wiiu/disc_image.py", "tools/wiiu/setup_assistant.py", "tools/wiiu/setup_extras.py",
    "tools/wiiu/convert_maps.py", "tools/wiiu/map_convert.py",
    "tools/wiiu/map_schema/tag_schema.json.gz", "tools/wiiu/map_schema/tag_layouts.json.gz",
    "tools/wiiu/map_schema/schema_dump.nm.gz",
)
README = """# OpenCE-WiiU setup helper

You need Python 3.10 or later and your own Xbox copy of Halo.

Windows: open Setup Wii U.bat (or drag the ISO onto it).
macOS: open Setup Wii U.command.
Linux: run python3 setup_wiiu.py.

Choose the ISO and SD card, then choose whether to convert the maps here or
copy the ISO for conversion on the Wii U. Direct console import requires
the new importer build; the original rc1 does not import ISO files.

The app, matching shader pack, and broker list come from the game release.
This helper prepares only your game data. It never uploads the ISO or maps.
Existing maps, settings, and saves are not overwritten.

See docs/ISO_SETUP.md for instructions and console validation limits.
Project and corresponding source: https://github.com/Roastedd/OpenCE-WiiU
The helper is AGPL-3.0-or-later; see LICENSE and the project's credits.
"""


BUNDLE_README = """# OpenCE-WiiU

Halo: Combat Evolved on the Wii U. This download has the game and the setup program in one place.

You need:
- a Wii U running Aroma, and an SD card with about 4 GB free,
- your own Xbox copy of Halo as a disc image (.iso),
- a computer with Python 3.10 or newer (https://www.python.org/downloads/).

1. Unzip this folder anywhere.
2. Windows: double-click "Setup Wii U.bat". Mac: double-click "Setup Wii U.command"
   (the first time, Control-click it, choose Open, and confirm).
3. Choose your Xbox ISO and your SD card, then press Prepare SD card.
4. When it says it is ready, eject the card, put it in the Wii U and start OpenCE-U
   from the Wii U Menu.

The setup program puts the game in sd:/wiiu/apps/halo/ and converts the maps from
your disc image, on your computer. Nothing is uploaded. If the card already has its
maps, run it again without an ISO to update the game only.

More help: docs/ISO_SETUP.md. Project and source: https://github.com/Roastedd/OpenCE-WiiU
The setup program is AGPL-3.0-or-later; see LICENSE.
"""


def package(destination, app=None, readme=README):
    """the zip: the helper, and with app (a folder holding halo.wuhb, shaders.pack and brokers.txt) the game files in a folder called app"""
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    records = []
    with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name in FILES:
            data = (ROOT / name).read_bytes()
            records.append(f"{hashlib.sha256(data).hexdigest()}  {name}")
            archive.write(ROOT / name, name)
        if app:
            for name in ("halo.wuhb", "shaders.pack", "brokers.txt"):
                data = (Path(app) / name).read_bytes()
                records.append(f"{hashlib.sha256(data).hexdigest()}  app/{name}")
                archive.write(Path(app) / name, f"app/{name}")
        archive.writestr("README.md", readme)
        archive.writestr("SHA256SUMS", "\n".join(records) + "\n")
    return destination


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", nargs="?", default=ROOT / "build/opence-wiiu-setup.zip")
    parser.add_argument("--app", help="a folder with halo.wuhb, shaders.pack and brokers.txt: makes the one-download release bundle")
    args = parser.parse_args()
    print(package(args.output, args.app, BUNDLE_README if args.app else README))
