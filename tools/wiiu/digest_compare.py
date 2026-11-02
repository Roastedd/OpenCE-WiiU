#!/usr/bin/env python3
"""Compares two runs of the game digest (tools/test_wiiu_game_digest.py, port/wiiu/tests/game_digest): frame by frame, by the game's tick.

    digest_compare.py RUN_A RUN_B [--multiset] [--ticks FIRST-LAST] [--menu] [--show N]
    digest_compare.py RUN_A RUN_B --traces [--unordered | --unordered-runs REGEX | --unordered-markers REGEX] [--show N]

A run is the folder the game digest wrote (frames.txt, summary.txt, and trace.txt when it was asked for one), or its frames.txt.

Each frame of the game is matched with the other run's frame of the same tick (and the same count of frames at that tick, should a tick be
drawn twice); the main menu's frames (tick -1) are left out, as their number follows the menus' timing, unless --menu matches them by their
order. Frames one run has and the other does not (one ran further) are counted, not compared.

By default a frame matches when its digest does: its lines in order, as the GX2 thread was given them. --multiset compares the frames' multiset
digests instead: the same draws, clears and presents in any order within the frame.

--traces compares the traces' lines instead of the digests (both runs need trace.txt, for the frames compared), and shows the first lines that
differ in each differing frame. With it, the order can be left out where a change may reorder draws and must not change them, for instance a
native decal pass that batches the decals by texture (N2(a) in reports/research/2026-11-01/native-gx2-renderer.md):

- --unordered: each frame as a multiset of its lines;
- --unordered-runs REGEX: each run of consecutive lines that match REGEX (re.search) as a multiset, the rest in order;
- --unordered-markers REGEX: from each marker line whose name matches REGEX to the next marker or present, as a multiset.

In a multiset the draws' numbers in the frame are left out (a reordered draw has another), as the frames' numbers and the texture cache's numbers
of the images are everywhere (the images' contents are compared).

Exit status: 0 if every frame compared matches (and at least one was compared), 1 if any differs, 2 if nothing could be compared.
"""

import argparse
import difflib
import re
import sys
from collections import Counter
from pathlib import Path
from typing import Dict, Iterator, List, Optional, Sequence, Tuple

NUMBERED = re.compile(r"^(draw|clear|present|marker|visibility begin|visibility end) (\d+)((?:\.\d+)?)")
MARKER = re.compile(r"^marker [\d.]+: (.*)$")
IMAGE_NUMBER = re.compile(r" i\d+:")

Key = Tuple[object, int]


def normalize(line: str, ordinal: bool = False) -> str:
    """the line as the glue hashes it (game_digest_glue.c's sink_normalize): without its frame's number nor the texture cache's numbers of its
    images, and with ordinal without the draw's number in the frame"""
    match = NUMBERED.match(line)
    if match:
        line = f"{match.group(1)} {'' if ordinal else match.group(3)}{line[match.end():]}"
    return IMAGE_NUMBER.sub(" i:", line)


def frames_file(run: Path) -> Path:
    return run if run.is_file() else run / "frames.txt"


def read_frames(run: Path) -> List[Dict[str, object]]:
    """frames.txt's frames: frame, tick, digest, multiset, lines, draws"""
    frames = []
    for text in frames_file(run).read_text().splitlines():
        words = text.split()
        if not words or words[0] != "frame":
            continue
        values: Dict[str, object] = {"frame": int(words[1])}
        for name, value in zip(words[2::2], words[3::2]):
            values[name] = int(value) if name in ("tick", "lines", "draws") else value
        frames.append(values)
    return frames


def keyed(frames: Sequence[Dict[str, object]], menu: bool) -> Dict[Key, Dict[str, object]]:
    """each frame by its tick and how many frames had that tick before it; the menu's by their order, with menu"""
    result: Dict[Key, Dict[str, object]] = {}
    seen: Counter = Counter()
    for frame in frames:
        tick = frame["tick"]
        if tick == -1 and not menu:
            continue
        name = "menu" if tick == -1 else tick
        result[(name, seen[name])] = frame
        seen[name] += 1
    return result


def in_range(key: Key, ticks: Optional[Tuple[int, int]]) -> bool:
    if ticks is None:
        return True
    return key[0] != "menu" and ticks[0] <= key[0] <= ticks[1]


def trace_frames(run: Path, wanted: Dict[int, Key]) -> Iterator[Tuple[Key, List[str]]]:
    """the trace's frames whose numbers are in wanted (frame -> key), each as its key and its lines (the present's included): each frame's lines
    are followed by a line "# frame N tick T" (game_digest_glue.c)"""
    trace = run / "trace.txt"
    if not trace.is_file():
        raise SystemExit(f"{run}: no trace.txt (run the game digest with a trace for the frames compared)")
    lines: List[str] = []
    with trace.open(errors="replace") as file:
        for text in file:
            text = text.rstrip("\n")
            if text.startswith("# frame "):
                # (a frame's lines are followed by its number: a trace of chosen ticks has only their frames)
                frame = int(text.split()[2])
                if frame in wanted:
                    yield wanted[frame], lines
                lines = []
                continue
            lines.append(text)


def canonical(lines: Sequence[str], how: str, pattern: Optional[re.Pattern]) -> List[str]:
    """the frame's lines as they are compared: normalized, and with the parts whose order does not count sorted (each run or span with a
    line of its own before it, so that a draw cannot move from one to another)"""
    if how == "ordered":
        return [normalize(line) for line in lines]
    if how == "unordered":
        return sorted(normalize(line, ordinal=True) for line in lines)
    result: List[str] = []
    group: List[str] = []

    def close() -> None:
        if group:
            result.append(f"[{len(group)} lines in any order]")
            result.extend(sorted(group))
            group.clear()

    if how == "runs":
        for line in lines:
            if pattern.search(line):
                group.append(normalize(line, ordinal=True))
            else:
                close()
                result.append(normalize(line))
        close()
        return result
    # (markers)
    inside = False
    for line in lines:
        marker = MARKER.match(line)
        if marker or line.startswith("present "):
            close()
            inside = bool(marker and pattern.search(marker.group(1)))
            result.append(normalize(line))
        elif inside:
            group.append(normalize(line, ordinal=True))
        else:
            result.append(normalize(line))
    close()
    return result


def describe(key: Key) -> str:
    name, nth = key
    text = "menu frame" if name == "menu" else f"tick {name}"
    return f"{text} #{nth}" if nth else text


def compare(run_a: Path, run_b: Path, multiset: bool = False, ticks: Optional[Tuple[int, int]] = None, menu: bool = False,
            traces: bool = False, how: str = "ordered", pattern: Optional[re.Pattern] = None, show: int = 10,
            out=sys.stdout) -> int:
    frames_a, frames_b = keyed(read_frames(run_a), menu), keyed(read_frames(run_b), menu)
    keys = [key for key in frames_a if key in frames_b and in_range(key, ticks)]
    only_a = sum(1 for key in frames_a if key not in frames_b and in_range(key, ticks))
    only_b = sum(1 for key in frames_b if key not in frames_a and in_range(key, ticks))
    if not keys:
        print(f"no frame to compare ({len(frames_a)} and {len(frames_b)} frames of the game)", file=out)
        return 2
    differing: List[Key] = []
    if traces:
        wanted_a = {frames_a[key]["frame"]: key for key in keys}
        wanted_b = {frames_b[key]["frame"]: key for key in keys}
        seen_a = dict(trace_frames(run_a, wanted_a))
        shown = 0
        compared = 0
        for key, lines_b in trace_frames(run_b, wanted_b):
            if key not in seen_a:
                continue
            compared += 1
            left, right = canonical(seen_a.pop(key), how, pattern), canonical(lines_b, how, pattern)
            if left != right:
                differing.append(key)
                if shown < show:
                    shown += 1
                    print(f"{describe(key)}: the traces differ", file=out)
                    diff = list(difflib.unified_diff(left, right, "A", "B", n=0, lineterm=""))
                    for text in diff[2:2 + 12]:
                        print(f"    {text[:300]}", file=out)
        if compared < len(keys):
            print(f"({len(keys) - compared} of the frames are not in both traces: compared {compared})", file=out)
        keys_compared = compared
    else:
        field = "multiset" if multiset else "digest"
        for key in sorted(keys, key=lambda k: (k[0] == "menu", k[0] if k[0] != "menu" else 0, k[1])):
            a, b = frames_a[key], frames_b[key]
            if a[field] != b[field]:
                differing.append(key)
                if len(differing) <= show:
                    print(f"{describe(key)}: {field} {a[field]} / {b[field]} (draws {a['draws']} / {b['draws']}, lines {a['lines']} / "
                          f"{b['lines']}; frames {a['frame']} / {b['frame']})", file=out)
        keys_compared = len(keys)
    ticks_compared = [key[0] for key in keys if key[0] != "menu"]
    span = f"ticks {min(ticks_compared)}-{max(ticks_compared)}" if ticks_compared else "the menu"
    what = "lines" if traces else ("multiset digests" if multiset else "digests")
    extra = f"; {only_a} frames only in A, {only_b} only in B" if only_a or only_b else ""
    if differing:
        first = min((key for key in differing if key[0] != "menu"), default=differing[0], key=lambda k: (k[0], k[1]))
        print(f"DIFFERENT: {len(differing)} of {keys_compared} frames ({span}) differ in their {what}, the first at {describe(first)}{extra}", file=out)
        return 1
    print(f"IDENTICAL: {keys_compared} frames ({span}), their {what}{extra}", file=out)
    return 0


def tick_range(text: str) -> Tuple[int, int]:
    first, _, last = text.partition("-")
    return int(first), int(last or first)


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], formatter_class=argparse.RawDescriptionHelpFormatter,
                                     epilog=__doc__.split("\n\n", 1)[1])
    parser.add_argument("run_a", type=Path)
    parser.add_argument("run_b", type=Path)
    parser.add_argument("--multiset", action="store_true", help="compare the frames' multiset digests (the order in a frame left out)")
    parser.add_argument("--ticks", type=tick_range, help="only the frames of these ticks, FIRST-LAST")
    parser.add_argument("--menu", action="store_true", help="also the main menu's frames, matched by their order")
    parser.add_argument("--traces", action="store_true", help="compare the traces' lines")
    order = parser.add_mutually_exclusive_group()
    order.add_argument("--unordered", action="store_true", help="with --traces: each frame's lines in any order")
    order.add_argument("--unordered-runs", metavar="REGEX", help="with --traces: runs of consecutive lines matching REGEX in any order")
    order.add_argument("--unordered-markers", metavar="REGEX", help="with --traces: spans from a marker named by REGEX in any order")
    parser.add_argument("--show", type=int, default=10, help="differing frames to show (default 10)")
    args = parser.parse_args(argv)
    how, pattern = "ordered", None
    if args.unordered:
        how = "unordered"
    elif args.unordered_runs:
        how, pattern = "runs", re.compile(args.unordered_runs)
    elif args.unordered_markers:
        how, pattern = "markers", re.compile(args.unordered_markers)
    if how != "ordered" and not args.traces:
        parser.error("--unordered, --unordered-runs and --unordered-markers compare traces: add --traces (or use --multiset)")
    return compare(args.run_a, args.run_b, multiset=args.multiset, ticks=args.ticks, menu=args.menu, traces=args.traces, how=how,
                   pattern=pattern, show=args.show)


if __name__ == "__main__":
    sys.exit(main())
