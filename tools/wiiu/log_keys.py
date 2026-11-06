#!/usr/bin/env python3
"""The pixel keys a Wii U run drew with a stand-in, from its log.txt, in the missing-shader capture's format.

    tools/wiiu/log_keys.py LOG.txt [LOG2.txt ...] [-o KEYS.txt]

port/wiiu/src/gx2_shaders.c writes the 256 bytes of every pixel key that is drawn with a stand-in (a shader class the pack has none of) once, in four lines of the
run's log: `shaders: key P <hash> <n>/4 <128 hex digits>`. The capture of the main log worker can be full or lost (the session of 2026-11-06 lost 41 keys to a full
card and a 128-file cap); the log is not. This joins the parts of each key, checks the length against 256 bytes (the hash is the game's and is not recomputed
here), and writes the keys as `P <512 hex>` lines, once each, for tools/wiiu/key_classes.py and key_diff.py. A key with a part
missing (a log cut short, or a line the log dropped) is reported on stderr and left out. Prints how many keys it wrote and, per key, the hash and the draws the run's
`stand-in key <hash> drew N times` lines say.
"""
import argparse
import re
import sys
from pathlib import Path

PART = re.compile(r"shaders: key P ([0-9a-f]{16}) (\d+)/(\d+) ([0-9a-f]+)\s*$")
DREW = re.compile(r"shaders: stand-in key ([0-9a-f]{16}) drew (\d+) times in this window")
WIRE_BYTES = 256


def read(paths):
    keys = {}
    parts = {}
    draws = {}
    for path in paths:
        for line in Path(path).read_text(errors="replace").splitlines():
            found = PART.search(line)
            if found:
                key, number, total, hexdigits = found.group(1), int(found.group(2)), int(found.group(3)), found.group(4)
                parts.setdefault((str(path), key), {})[number] = (total, hexdigits)
                continue
            found = DREW.search(line)
            if found:
                draws[found.group(1)] = draws.get(found.group(1), 0) + int(found.group(2))
    for (path, key), pieces in parts.items():
        total = next(iter(pieces.values()))[0]
        if sorted(pieces) != list(range(1, total + 1)) or any(t != total for t, _ in pieces.values()):
            print(f"{path}: key {key}: parts {sorted(pieces)} of {total}: left out", file=sys.stderr)
            continue
        wire = "".join(pieces[n][1] for n in range(1, total + 1))
        if len(wire) != WIRE_BYTES * 2:
            print(f"{path}: key {key}: {len(wire) // 2} bytes, not {WIRE_BYTES}: left out", file=sys.stderr)
            continue
        keys.setdefault(key, wire)
    return keys, draws


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("logs", nargs="+")
    parser.add_argument("-o", "--output", help="write the keys here (else standard output)")
    args = parser.parse_args(argv)
    keys, draws = read(args.logs)
    text = "".join(f"P {wire}\n" for wire in keys.values())
    if args.output:
        Path(args.output).write_text(text)
    else:
        sys.stdout.write(text)
    print(f"{len(keys)} stand-in keys", file=sys.stderr)
    for key in keys:
        print(f"  {key}: {draws.get(key, 0)} draws said in the log's windows", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
