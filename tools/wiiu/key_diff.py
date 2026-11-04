#!/usr/bin/env python3
"""Which shader keys a run's draws need that a pack's key list lacks.

    tools/wiiu/key_diff.py NEEDED.txt KEYS.txt [-o MISSING.txt]

NEEDED.txt is a keys file: the lines the missing-shader capture writes on the card (halo/shader-misses-*.txt) or the digest harness writes with --keys
(tools/test_wiiu_game_digest.py run RUN --keys: RUN/out/keys.txt). KEYS.txt is the list shaders.pack is made from (tools/wiiu/shader_keys.txt in the
development repository). The keys of NEEDED that KEYS does not have are printed as a count and, with -o, written one per line in the format
tools/wiiu/shader_pack.sh takes (shader_pack.sh MISSING.txt adds them to the list and remakes the pack).

A pixel key ("P", 256 bytes as hex) is the same key when its bytes are. A vertex key ("V", the program's hash, its packed attribute mask, its
instruction count and words) is the same key when the program's hash and the mask are; its words must hash to the program's hash (FNV-1a 64 of the
count and the words, each written least significant byte first: shader_key_wire.h), or the line is refused. Lines that are neither (comments, the
capture's own headers) are skipped. Nothing here touches a console.

Exit status: 0 when nothing is missing, 1 when keys are, 2 for a file that cannot be read.
"""
import argparse
import struct
import sys

FNV_INIT = 0xcbf29ce484222325
MASK64 = (1 << 64) - 1
HEX = set("0123456789abcdefABCDEF")


def fnv(data, value=FNV_INIT):
    for byte in data:
        value = ((value ^ byte) * 0x100000001b3) & MASK64
    return value


def is_hex(text, length=None):
    return (length is None or len(text) == length) and bool(text) and all(c in HEX for c in text)


def read_key(line):
    """(kind, identity, canonical line) of a P or V line, None for any other line; ValueError for a broken one"""
    fields = line.split()
    if not fields or fields[0] not in ("P", "V"):
        return None
    if fields[0] == "P":
        if len(fields) != 2 or not is_hex(fields[1], 512):
            raise ValueError("a pixel key is 512 hex digits")
        key = fields[1].lower()
        return "P", key, "P " + key
    if len(fields) < 4 or not is_hex(fields[1], 16) or not fields[2].isdigit() or not fields[3].isdigit():
        raise ValueError("a vertex key is: V program-hash mask count words")
    program, mask, count = int(fields[1], 16), int(fields[2]), int(fields[3])
    if not 1 <= count <= 136 or len(fields) != 4 + count * 4 or not all(is_hex(word, 8) for word in fields[4:]):
        raise ValueError("a vertex key has 4 words of 8 hex digits for each of 1 to 136 instructions")
    words = [int(word, 16) for word in fields[4:]]
    if fnv(struct.pack("<I", count) + struct.pack("<%dI" % len(words), *words)) != program:
        raise ValueError("a vertex key's words do not hash to its program's hash")
    return "V", (program, mask), "V %016x %d %d %s" % (program, mask, count, " ".join("%08x" % word for word in words))


def read_keys(path):
    """{identity: canonical line} of a file's keys, (kind, identity) apart; (broken lines' count, first problem)"""
    keys, broken, problem = {}, 0, None
    with open(path, "r", encoding="ascii", errors="replace") as handle:
        for number, line in enumerate(handle, 1):
            try:
                key = read_key(line.strip())
            except ValueError as error:
                broken += 1
                problem = problem or "%s:%d: %s" % (path, number, error)
                continue
            if key:
                keys.setdefault((key[0], key[1]), key[2])
    return keys, broken, problem


def diff(needed, have):
    """the canonical lines of needed's keys that have lacks, pixel keys first"""
    return [needed[k] for k in sorted(needed, key=lambda k: (k[0] != "P", str(k[1]))) if k not in have]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], epilog=__doc__.split("\n\n", 1)[1],
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("needed")
    parser.add_argument("keys")
    parser.add_argument("-o", "--output", help="write the missing keys here")
    args = parser.parse_args(argv)
    try:
        needed, needed_broken, needed_problem = read_keys(args.needed)
        have, have_broken, have_problem = read_keys(args.keys)
    except OSError as error:
        print("key_diff: %s" % error, file=sys.stderr)
        return 2
    for problem in (needed_problem, have_problem):
        if problem:
            print("key_diff: " + problem, file=sys.stderr)
    missing = diff(needed, have)
    count = lambda keys, kind: sum(1 for k in keys if k[0] == kind)
    print("needed: %d pixel keys, %d vertex keys; the pack's list has %d and %d; missing: %d pixel keys, %d vertex keys%s" % (
        count(needed, "P"), count(needed, "V"), count(have, "P"), count(have, "V"),
        sum(1 for line in missing if line[0] == "P"), sum(1 for line in missing if line[0] == "V"),
        "; %d lines refused" % (needed_broken + have_broken) if needed_broken + have_broken else ""))
    if args.output:
        with open(args.output, "w", encoding="ascii") as handle:
            handle.write("".join(line + "\n" for line in missing))
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
