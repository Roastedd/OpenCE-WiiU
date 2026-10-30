#!/usr/bin/env python3
"""Converts the maps of a Halo Xbox disc for the Wii U build: every .map of a
folder rewritten in big-endian byte order with its addresses moved to the Wii U's
memory window (map_convert.py, with the schema in map_schema/), into another folder.

    python3 tools/wiiu/convert_maps.py XBOX_MAPS_FOLDER OUTPUT_FOLDER

Copy the output folder's maps to the SD card's wiiu/apps/halo/data/maps/. Only
Python 3 is needed. A map already converted (the output exists and is newer than
its source) is left as it is; --force converts it again.

The schema files are the big-endian build's own (schema_dump and tag_layouts.py in
the tools that made them): they describe the game's tag definitions, not any map's
contents. A map the converter cannot read is refused and reported, and the others
are still converted.
"""

import argparse
import gzip
import json
import sys
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import map_convert  # noqa: E402

SCHEMA = HERE / "map_schema"
# the Wii U's window is at 0x41EA0000 (halo_port_capacity.h); the maps' addresses
# are the Xbox's, from 0x80000000
WIIU_REBASE = (0x41EA0000 - 0x80000000) & 0xFFFFFFFF


def schema_text(name: str) -> str:
    return gzip.decompress((SCHEMA / f"{name}.gz").read_bytes()).decode("utf-8")


def converter() -> "map_convert.Converter":
    symbols = {}
    for line in schema_text("schema_dump.nm").splitlines():
        parts = line.split()
        if len(parts) >= 3:
            symbols.setdefault(int(parts[0], 16), []).append(parts[2])
    return map_convert.Converter(json.loads(schema_text("tag_schema.json")), json.loads(schema_text("tag_layouts.json")),
                                 symbols)


def convert(source: Path, destination: Path, level: int) -> bool:
    data = map_convert.read_map(source)
    try:
        map_ = converter().convert(data, audit=False, rebase=WIIU_REBASE, audit_pointers=False)
    except map_convert.Refused as refusal:
        print(f"{source.name}: refused: {refusal}", file=sys.stderr)
        return False
    temporary = destination.with_name(destination.name + ".part")
    temporary.write_bytes(bytes(map_.data[:map_convert.HEADER_SIZE]) +
                          zlib.compress(bytes(map_.data[map_convert.HEADER_SIZE:]), level))
    temporary.replace(destination)
    print(f"{source.name}: {map_.counts['tags']} tags, {map_.counts['pointers moved']} addresses moved")
    for name, size in map_.unhandled.most_common():
        print(f"  left as it is: {name} ({size} {'tags' if name.startswith('group') else 'bytes'})")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("source", type=Path, help="the Xbox disc's maps folder")
    parser.add_argument("destination", type=Path, help="where the converted maps go")
    parser.add_argument("--force", action="store_true", help="convert maps already converted")
    parser.add_argument("--level", type=int, default=6, help="zlib level of the written maps")
    args = parser.parse_args()
    maps = sorted(args.source.glob("*.map"))
    if not maps:
        print(f"no .map files in {args.source}", file=sys.stderr)
        return 1
    args.destination.mkdir(parents=True, exist_ok=True)
    failed = 0
    for source in maps:
        destination = args.destination / source.name
        if not args.force and destination.exists() and destination.stat().st_mtime >= source.stat().st_mtime:
            print(f"{source.name}: already converted")
            continue
        if not convert(source, destination, args.level):
            failed += 1
    print(f"{len(maps) - failed} of {len(maps)} maps ready in {args.destination}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
