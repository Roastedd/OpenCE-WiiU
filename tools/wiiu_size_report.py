#!/usr/bin/env python3
"""The Wii U build's sizes against wut's memory map, and its largest static objects.

Reads the link's ELF (section sizes and symbol sizes, with devkitPPC's binutils) and the regions of wut's linker script
($WUT_ROOT/share/wut.ld: the code region at 0x02000000, the data region at 0x10000000, the loader's at 0xC0000000). The
game keeps large tables in static arrays (the Xbox memory it replaces was a fixed window), and the data region is what
the heap shares, so these are the numbers the memory plan starts from.

    wiiu_size_report.py halo.elf [--map halo.elf.map] [--top 20]
"""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Tuple

PREFIX = os.environ.get("WIIU_BINUTILS_PREFIX", "powerpc-eabi-")
UNITS = {"": 1, "K": 1024, "M": 1024 * 1024, "G": 1024 * 1024 * 1024}


def mib(size: int) -> str:
    return f"{size / (1024 * 1024):.2f} MiB"


def regions(script: Path) -> Dict[str, Tuple[int, int]]:
    """name -> (origin, length) from the linker script's MEMORY block"""
    found = {}
    for name, origin, length, unit in re.findall(
            r"(\w+)\s*\([rwx]+\)\s*:\s*ORIGIN\s*=\s*(0x[0-9a-fA-F]+)\s*,\s*LENGTH\s*=\s*(\d+)([KMG]?)", script.read_text()):
        found[name] = (int(origin, 16), int(length) * UNITS[unit])
    return found


def sections(elf: Path) -> List[Tuple[str, int, int, str]]:
    """(name, address, size, flags) of the allocated sections"""
    out = subprocess.run([PREFIX + "readelf", "-S", "-W", str(elf)], capture_output=True, text=True, check=True).stdout
    result = []
    for line in out.splitlines():
        # "  [ 1] .text PROGBITS 02000000 001000 00d242 00 AX 0 0 32"
        match = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-f]{8})\s+[0-9a-f]+\s+([0-9a-f]+)\s+\S+\s*([A-Za-z]*)", line)
        if match and "A" in match.group(5):
            result.append((match.group(1), int(match.group(3), 16), int(match.group(4), 16), match.group(5)))
    return result


def symbols(elf: Path, kinds: str, top: int) -> List[Tuple[int, str, str]]:
    out = subprocess.run([PREFIX + "nm", "-S", "-C", "--size-sort", "-l", str(elf)], capture_output=True, text=True,
                         check=True).stdout
    found = []
    for line in out.splitlines():
        match = re.match(r"([0-9a-f]+)\s+([0-9a-f]+)\s+(\w)\s+(.*?)(?:\t(.*))?$", line)
        if match and match.group(3) in kinds:
            found.append((int(match.group(2), 16), match.group(4), match.group(5) or ""))
    return sorted(found, reverse=True)[:top]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("elf", type=Path)
    parser.add_argument("--map", type=Path)
    parser.add_argument("--top", type=int, default=20)
    args = parser.parse_args()
    wut = os.environ.get("WUT_ROOT")
    script = Path(wut) / "share" / "wut.ld" if wut else None
    limits = regions(script) if script and script.is_file() else {}
    print(f"{args.elf.name}: sizes against wut's memory map ({script if limits else 'wut.ld not found: no limits'})")
    secs = sections(args.elf)
    for name, (origin, length) in sorted(limits.items(), key=lambda item: item[1][0]):
        inside = [s for s in secs if origin <= s[1] < origin + length]
        if not inside:
            continue
        used = max(s[1] + s[2] for s in inside) - origin
        print(f"{name} {origin:#010x}, {mib(length)}: {mib(used)} used ({100 * used / length:.1f}%)"
              + ("" if used <= length else "  OVER THE REGION"))
        for sname, address, size, _ in inside:
            if size >= 1024:
                print(f"    {sname:<16} {address:#010x} {size:>10,} bytes  {mib(size)}")
    print(f"\nthe {args.top} largest static data objects (.data, .bss, .rodata):")
    for size, name, where in symbols(args.elf, "bBdDrR", args.top):
        print(f"  {size:>10,}  {name}  {where}")
    print(f"\nthe {args.top // 2} largest functions:")
    for size, name, where in symbols(args.elf, "tT", args.top // 2):
        print(f"  {size:>10,}  {name}  {where}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
