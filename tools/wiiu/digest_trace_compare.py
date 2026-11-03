#!/usr/bin/env python3
"""Two game digest runs' traces compared line by line, frame by frame (by the game's tick), with the textures' numbers left out.

    digest_trace_compare.py TRACE_A TRACE_B [--ticks FIRST-LAST] [--show N] [--keep-ids]

The traces are the game digest's trace.txt (tools/test_wiiu_game_digest.py run RUN --trace all; the game digest harness is on the branch
wiiu-native-n6). Under a loaded machine two runs of one build do not always draw every frame of the game the same by their digests, because a
draw's line has its textures' numbers (i47:...), which count the textures the menus before the game made and so shift together from one run
to another by a few; each texture's contents hash, which follows the number in the line, is the same. This compares the lines with the numbers
left out (--keep-ids puts them back), so that a change of what is drawn is told from a change of the numbers. A frame is its lines in order, as
the digest has them; the frames' and draws' own numbers are left out as the digest leaves them.

Exit status: 0 if every frame in both traces' common ticks has the same lines, 1 if any differs, 2 if there is nothing to compare.
"""

import argparse
import re
import sys

NUMBERED = re.compile(r"^(draw|clear|present|marker|visibility begin|visibility end) (\d+)((?:\.\d+)?)")
IDENTIFIER = re.compile(r"\bi\d+:")
FRAME = re.compile(r"^# frame (\d+) tick (-?\d+)")


def normalize(line, keep_ids):
    match = NUMBERED.match(line)
    if match:
        line = f"{match.group(1)} {match.group(3)}{line[match.end():]}"
    return line if keep_ids else IDENTIFIER.sub("i:", line)


def frames(path, first, last, keep_ids):
    """{tick: [lines]} for the frames of the game (tick >= 1) in range; a tick drawn twice keeps its later frame"""
    result, current = {}, []
    with open(path, errors="replace") as handle:
        for text in handle:
            text = text.rstrip("\n")
            match = FRAME.match(text)
            if not match:
                current.append(normalize(text, keep_ids))
                continue
            tick = int(match.group(2))
            if tick >= 1 and first <= tick <= last:
                result[tick] = current
            current = []
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], formatter_class=argparse.RawDescriptionHelpFormatter,
                                     epilog=__doc__.split("\n\n", 1)[1])
    parser.add_argument("a")
    parser.add_argument("b")
    parser.add_argument("--ticks", default="1-1000000")
    parser.add_argument("--show", type=int, default=3, help="how many differing frames to show (default 3)")
    parser.add_argument("--keep-ids", action="store_true")
    args = parser.parse_args()
    first, _, last = args.ticks.partition("-")
    first, last = int(first), int(last or first)
    a, b = frames(args.a, first, last, args.keep_ids), frames(args.b, first, last, args.keep_ids)
    common = sorted(set(a) & set(b))
    if not common:
        print("no tick is in both traces")
        return 2
    different = [tick for tick in common if a[tick] != b[tick]]
    shown = 0
    for tick in different[:args.show]:
        la, lb = a[tick], b[tick]
        print(f"tick {tick}: {len(la)} and {len(lb)} lines")
        for index, (x, y) in enumerate(zip(la, lb)):
            if x != y:
                print(f"  line {index}:\n    A {x[:600]}\n    B {y[:600]}")
                shown += 1
                break
        else:
            print("  the shorter is a prefix of the longer")
    if different:
        print(f"DIFFERENT: {len(different)} of {len(common)} frames (ticks {common[0]}-{common[-1]}), the first at tick {different[0]}")
        return 1
    print(f"IDENTICAL: {len(common)} frames (ticks {common[0]}-{common[-1]}), their lines" + ("" if args.keep_ids else " (textures' numbers left out)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
