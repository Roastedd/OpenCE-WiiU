#!/usr/bin/env python3
"""Two host runs of the network test (port/wiiu/tests/bw_tick_run.sh), compared tick by tick on every field the game's ticks decide.

The network test logs a line a tick: every player's state, the items, the score, the hits and drift counters, the network's counts and the
local camera. The network's counts depend on timing, and the local camera is updated in the game's own non-deterministic section
(main_non_deterministic_update_section: display-side and local to each machine, which differs between two runs of one build too); both are
left out, and everything else must be the same for every tick the two runs have in common.

    bw_tick_compare.py BASE.log OTHER.log      (exit 1 if any tick differs, or the runs have no tick in common)
"""

import re
import sys

LINE = re.compile(r"network test: tick (\d+)(.*)")


def ticks(path):
    found = {}
    for line in open(path, errors="replace"):
        match = LINE.search(line)
        if match:
            found[int(match.group(1))] = match.group(2)
    return found


def ticks_decided(text):
    text = re.sub(r"sent \d+ received \d+", "sent _ received _", text)
    return re.sub(r"local \d+ camera \([^)]*\)", "local _ camera _", text)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    base, other = ticks(sys.argv[1]), ticks(sys.argv[2])
    common = sorted(set(base) & set(other))
    different = [tick for tick in common if ticks_decided(base[tick]) != ticks_decided(other[tick])]
    print(f"{len(common)} ticks in common ({len(base)} and {len(other)}); {len(different)} differ"
          + (f", the first at tick {different[0]}" if different else ""))
    if different:
        print(f"  {sys.argv[1]}: {ticks_decided(base[different[0]])}")
        print(f"  {sys.argv[2]}: {ticks_decided(other[different[0]])}")
    return 1 if different or not common else 0


if __name__ == "__main__":
    sys.exit(main())
