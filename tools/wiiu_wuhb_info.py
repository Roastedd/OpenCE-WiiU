#!/usr/bin/env python3
"""What a .wuhb (wuhbtool's RomFS: a header of 64-bit offsets, a table of files, then their data) holds: its files and sizes, the menu's
text (meta/meta.ini: the names and author Aroma shows), and the size of each picture (meta/iconTex.tga, bootTvTex.tga, bootDrcTex.tga, which
wuhbtool made from the PNG files, gzipped).

    python3 tools/wiiu_wuhb_info.py build/linux-wiiu/halo.wuhb [--extract FOLDER]   (--extract writes the files of meta/)
    python3 tools/wiiu_wuhb_info.py halo.wuhb --expect-name "OpenCE-U"   (exit 1 if it differs)
"""
import argparse
import gzip
import struct
import sys
from pathlib import Path


def files(data: bytes):
    """(path, offset, size) of each file: the table's entries hold the name of the file alone and the parent's index; the directories'
    entries (wuhbtool's) are walked for the path"""
    if data[:4] != b"WUHB":
        raise SystemExit("not a WUHB")
    header_size, dir_hash, dir_table, dir_table_size, file_hash, file_hash_size, file_table, file_table_size, file_data = struct.unpack_from(">9Q", data, 8)
    dirs = {}
    at = 0
    while at < dir_table_size:
        parent, sibling, child_dir, child_file, hash_next, name_length = struct.unpack_from(">IIIIII", data, dir_table + at)
        name = data[dir_table + at + 24:dir_table + at + 24 + name_length].decode("utf-8", "replace")
        dirs[at] = (parent, name)
        at += 24 + ((name_length + 3) & ~3)

    def directory_path(offset):
        parts = []
        while offset in dirs and dirs[offset][1]:
            parts.append(dirs[offset][1])
            offset = dirs[offset][0]
        return "/".join(reversed(parts))

    found = []
    at = 0
    while at < file_table_size:
        parent, sibling, offset, size, hash_next, name_length = struct.unpack_from(">IIQQII", data, file_table + at)
        name = data[file_table + at + 32:file_table + at + 32 + name_length].decode("utf-8", "replace")
        folder = directory_path(parent)
        found.append((f"{folder}/{name}" if folder else name, file_data + offset, size))
        at += 32 + ((name_length + 3) & ~3)
    return found


def tga_size(blob: bytes):
    """the width and height of an uncompressed or run-length TGA picture"""
    return struct.unpack_from("<HH", blob, 12)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("wuhb", type=Path)
    parser.add_argument("--extract", type=Path)
    parser.add_argument("--expect-name")
    args = parser.parse_args()
    data = args.wuhb.read_bytes()
    listing = files(data)
    ini = ""
    for path, offset, size in listing:
        extra = ""
        blob = data[offset:offset + size]
        if path.endswith((".tga", ".tga.gz")):
            picture = gzip.decompress(blob) if path.endswith(".gz") else blob
            extra = "  %dx%d picture" % tga_size(picture)
        print(f"{size:>10,}  {path}{extra}")
        if path.endswith("meta.ini"):
            ini = blob.decode("utf-8", "replace")
        if args.extract and path.startswith("meta/"):
            args.extract.mkdir(parents=True, exist_ok=True)
            (args.extract / Path(path).name).write_bytes(blob)
    print("\nmeta/meta.ini:\n" + ini)
    if args.expect_name:
        names = [line.split("=", 1)[1].strip() for line in ini.splitlines() if line.startswith("longname=")]
        if args.expect_name not in names:
            print(f"the name is {names}, not {args.expect_name!r}", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
