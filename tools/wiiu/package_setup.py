#!/usr/bin/env python3
"""Package the source-only computer setup helper; no game data or binaries."""
import argparse
import hashlib
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FILES = (
    "setup_wiiu.py", "Setup Wii U.bat", "Setup Wii U.command", "LICENSE", "AUTHORS.md",
    "docs/ISO_SETUP.md", "tools/wiiu/disc_image.py", "tools/wiiu/setup_assistant.py",
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


def package(destination):
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    records = []
    with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name in FILES:
            data = (ROOT / name).read_bytes()
            records.append(f"{hashlib.sha256(data).hexdigest()}  {name}")
            archive.write(ROOT / name, name)
        archive.writestr("README.md", README)
        archive.writestr("SHA256SUMS", "\n".join(records) + "\n")
    return destination


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", nargs="?", default=ROOT / "build/opence-wiiu-setup.zip")
    print(package(parser.parse_args().output))
