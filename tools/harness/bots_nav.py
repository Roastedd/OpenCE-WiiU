#!/usr/bin/env python3
"""Bakes the bots' navigation graphs: a headless game of each map with the bake switched on (debug.bots_bake_nav), run by the big-endian build under
qemu-ppc in the opence-ppc-be image (tools/ppc-be/run.sh builds both), and the one-map packs merged into navs.pack (tools/bots_nav_pack.py).

    python3 tools/harness/bots_nav.py bake MAP[=CELL] [MAP[=CELL] ...] --out DIR [--tree TREE] [--maps FOLDER] [--cell HUNDREDTHS] [--reach TENTHS] [--stride N]
    python3 tools/harness/bots_nav.py bake --all --out DIR

Each map takes seconds to two minutes (the bake's log is DIR/<map>/game.log: the biped's numbers, the floor nodes, the walks, the areas, the flights and
the places each step's count and time, and at the end which places the graph joins). MAP=CELL bakes that map with its own floor grid (hundredths of a unit: boardingaction=25), the others with --cell or the bake's own. DIR/navs.pack is the merged pack; DIR/report.txt says what the
bake made (counts, and from each start the places reached). Never on the console: the bake is a game run on the Mac, and the console only reads the pack.
"""

import argparse
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
import bots_sim  # noqa: E402

MAPS = ["beavercreek", "bloodgulch", "boardingaction", "carousel", "chillout", "damnation", "hangemhigh", "longest", "prisoner", "putput", "ratrace",
        "sidewinder", "wizard"]


def bake(map_name, out, tree=None, maps=None, cell=None, reach=None, stride=None, timeout=3600):
    """Bakes one map into out/<map>/<map>.nav; returns the path and the log text."""
    folder = Path(out) / map_name
    folder.mkdir(parents=True, exist_ok=True)
    environment = {"HALO_BOTS_BAKE_NAV": f"/out/{map_name}.nav"}
    if cell:
        environment["HALO_BOTS_BAKE_CELL"] = str(cell)
    if reach:
        environment["HALO_BOTS_BAKE_REACH"] = str(reach)
    if stride:
        environment["HALO_BOTS_BAKE_STRIDE"] = str(stride)
    started = time.monotonic()
    _, text = bots_sim.run(map_name, bots=1, ticks=200, runner="ppc-be", tree=tree, maps=maps, out=folder, environment=environment, timeout=timeout,
                           block=False)
    path = folder / f"{map_name}.nav"
    if not path.is_file():
        raise RuntimeError(f"the bake of {map_name} wrote nothing: {text[-800:]}")
    return path, text, time.monotonic() - started


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=["bake"])
    parser.add_argument("maps", nargs="*")
    parser.add_argument("--all", action="store_true", help="the 13 multiplayer maps")
    parser.add_argument("--out", required=True)
    parser.add_argument("--tree")
    parser.add_argument("--maps-folder", dest="maps_folder")
    parser.add_argument("--cell", type=int)
    parser.add_argument("--reach", type=int)
    parser.add_argument("--stride", type=int)
    args = parser.parse_args(argv)
    names = MAPS if args.all else args.maps
    if not names:
        parser.error("name the maps, or --all")
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    paths = []
    for item in names:
        name, _, own_cell = item.partition("=")
        path, text, seconds = bake(name, out, args.tree, args.maps_folder, int(own_cell) if own_cell else args.cell, args.reach, args.stride)
        done = [line for line in text.splitlines() if "bake: done" in line or "bake: wrote" in line]
        print(f"{name}: {seconds:.0f} s wall; " + " | ".join(line.split("bake: ", 1)[-1] for line in done))
        paths.append(str(path))
    pack = out / "navs.pack"
    subprocess.run([sys.executable, str(HERE.parent / "bots_nav_pack.py"), "merge", str(pack), *paths], check=True)
    report = subprocess.run([sys.executable, str(HERE.parent / "bots_nav_pack.py"), "info", str(pack), "--reach"], check=True, capture_output=True, text=True)
    (out / "report.txt").write_text(report.stdout)
    print(report.stdout)
    return 0


if __name__ == "__main__":
    sys.exit(main())
