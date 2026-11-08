#!/usr/bin/env python3
"""How many of a frame's draws could be instances of an earlier one, from the digest harness's trace (tools/test_wiiu_game_digest.py run ... --trace FIRST-LAST).

A draw's trace line has the mesh (primitive, count, offset, the index hash, each stream's address, size and content hash), the state (targets, state, vertex and pixel key, textures)
and what differs from one use of a mesh to the next (vertex parameters, pixel parameters, the constants' hash). Two draws are the same *group* when mesh and state are equal; the
rest is per instance. The census counts, per frame and over the run:

  - repeats anywhere in the frame (the claim of reports/research/2026-11-03/gpu-offload.md section 1: 42 percent),
  - draws identical in mesh and state to the draw just before (what a group command that keeps the draw order could take: nothing is reordered),
  - draws whose group was drawn in the last W draws (what a collection of the instances of a model could take if it hoisted a draw past the W-1 before it; W = 2 .. 64),
  - draws that repeat, in the same order, a block of the draws before (A B C A B C: the parts of one model drawn for a second object; the period 1 to 8): what a group that keeps the
    draw order could take if the *block* were the unit (a draw of period 1 is an adjacent repeat),
  - draws with the same mesh as the draw before and another state,
  - of the repeats, which per-instance inputs differ (vertex parameters, pixel parameters, constants).

It also prints a model of what the groups would save, from the console's per-part costs (gpu-offload.md 2.2: the draw's own path in the Direct3D layer, 11.8 us a part), as an
upper bound for a design that skips the layer's work for the instances after the first, and says it is one: no millisecond here was measured.

    python3 tools/wiiu/instancing_census.py RUN_FOLDER [RUN_FOLDER ...]       (each with out/trace.txt)
"""
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

DRAW = re.compile(r"^draw (\d+)\.(\d+): prim (\S+) count (\S+) offset (\S+) indices (\S+) (.*?); targets (.*?); state (\S+) vertex key (\S+) pixel key (\S+); textures (.*?); "
                  r"vertex parameters (\S+) pixel parameters (\S+) constants (\S+) format (\S+)")
WINDOWS = (2, 3, 4, 6, 8, 16, 32, 64)
# gpu-offload.md section 2.2: the draw itself in the Direct3D layer, microseconds a part (facts 2.7, format 1.7, write 2.9, command 0.8, constants 1.2 and the rest of its 11.8)
LAYER_US = 11.8
# the layer's work that stays for every instance if the constants are still written for each (constants 1.2 of the 11.8)
INSTANCE_KEEPS_US = 1.2


def frames(path):
    """the draws of each frame, in order: (mesh, state, instance) tuples"""
    frame = []
    for line in Path(path).open(errors="replace"):
        if line.startswith("present "):
            if frame:
                yield frame
            frame = []
            continue
        match = DRAW.match(line)
        if match:
            _, _, prim, count, offset, indices, streams, targets, state, vkey, pkey, textures, vparams, pparams, constants, fmt = match.groups()
            frame.append(((prim, count, offset, indices, streams), (targets, state, vkey, pkey, textures, fmt), (vparams, pparams, constants)))
    if frame:
        yield frame


def census(path):
    total = Counter()
    for draws in frames(path):
        if len(draws) < 20:
            continue
        total["frames"] += 1
        total["draws"] += len(draws)
        seen = {}
        last_index = {}
        for index, (mesh, state, instance) in enumerate(draws):
            group = (mesh, state)
            if group in seen:
                total["repeat"] += 1
                first = seen[group]
                total["repeat_vp_differs"] += instance[0] != first[0]
                total["repeat_pp_differs"] += instance[1] != first[1]
                total["repeat_constants_differ"] += instance[2] != first[2]
                total["repeat_identical"] += instance == first
                gap = index - last_index[group]
                for window in WINDOWS:
                    if gap <= window:
                        total[f"window{window}"] += 1
                if gap == 1:
                    total["adjacent"] += 1
            else:
                seen[group] = instance
            last_index[group] = index
            if index and draws[index - 1][0] == mesh and draws[index - 1][1] != state:
                total["same_mesh_other_state"] += 1
        # (blocks: draw i repeats draw i - p, and so do the p draws before it, for some period p up to 8; the first copy of a block is not a repeat)
        ids = [hash((m, s)) for m, s, _ in draws]
        covered = [False] * len(draws)
        for period in range(1, 9):
            run = 0
            for i in range(period, len(draws)):
                run = run + 1 if ids[i] == ids[i - period] else 0
                if run >= period:
                    for j in range(i - run + 1, i + 1):
                        covered[j] = True
        total["blocks"] += sum(covered)
        total["groups"] += len(seen)
        total["largest"] = max(total["largest"], max(Counter((m, s) for m, s, _ in draws).values()))
    return total


def report(name, total):
    frames_n = total["frames"] or 1
    draws = total["draws"] or 1
    print(f"{name}: {total['frames']} frames, {total['draws'] / frames_n:.1f} draws a frame in {total['groups'] / frames_n:.1f} groups (largest group {total['largest']})")
    rows = [("repeats anywhere in the frame", total["repeat"]), ("the same mesh and state as the draw just before (order kept)", total["adjacent"])]
    rows += [(f"group drawn in the last {w} draws (hoisting past {w - 1})", total[f"window{w}"]) for w in WINDOWS]
    rows += [("repeats of a block of 1 to 8 draws in the same order (order kept)", total["blocks"])]
    rows += [("same mesh as the draw before, another state", total["same_mesh_other_state"])]
    for label, count in rows:
        print(f"  {label:62s} {count / frames_n:7.1f} a frame  {100.0 * count / draws:5.1f}%")
    repeats = total["repeat"] or 1
    print(f"  of the repeats: vertex parameters differ {100.0 * total['repeat_vp_differs'] / repeats:.0f}%, pixel parameters {100.0 * total['repeat_pp_differs'] / repeats:.0f}%, "
          f"constants {100.0 * total['repeat_constants_differ'] / repeats:.0f}%, all three the same {100.0 * total['repeat_identical'] / repeats:.0f}%")
    for label, key in (("order kept (adjacent)", "adjacent"), ("blocks, order kept", "blocks"), ("hoisting within 8", "window8"), ("hoisting within 64", "window64"), ("anywhere in the frame", "repeat")):
        per_frame = total[key] / frames_n
        print(f"  model, an upper bound ({label}): the layer's {LAYER_US - INSTANCE_KEEPS_US:.1f} us a skipped draw path x {per_frame:.1f} = {per_frame * (LAYER_US - INSTANCE_KEEPS_US) / 1000.0:.2f} ms a frame of the game thread")
    print()


def main(arguments):
    if not arguments:
        print(__doc__)
        return 2
    for folder in arguments:
        path = Path(folder) / "out" / "trace.txt"
        if not path.is_file():
            path = Path(folder)
        report(Path(folder).name, census(path))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
