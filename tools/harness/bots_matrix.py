"""Several headless bot games at once (tools/harness/bots_sim.py's run), and the share of the bots' seconds they stood stuck, added up over seeds.

    python3 tools/harness/bots_matrix.py --out DIR --pack navs.pack --maps bloodgulch,chillout --modes legacy,nav --seeds 1,2,3 --bots 6 --ticks 3600
    python3 tools/harness/bots_matrix.py --out DIR --tour ...        (the bots meet nobody and go place to place: debug.bots_tour)

A mode is a name for the switches of the game: "legacy" (bots.nav 0: the engine's surfaces), "nav" (bots.nav 1 with the pack, read on the game
thread at the map's start so that every run has the graph from tick 0) and anything else of the form NAME=VAR=VALUE,VAR=VALUE (more
environment, e.g. "objectives=HALO_BOTS_OBJECTIVES=1,HALO_BOTS_NAV=1"). Each run is a folder DIR/MAP-MODE-sSEED with game.log and result.json;
a run that exists with a result.json is not played again (--force plays it again). Counts are exact; runs are one process each, so --jobs of
them play at the same time (each is a qemu process).

The table gives, for each map and mode, the stuck seconds over the bot-seconds (a bot-second is a bot in a game for a second) summed over the seeds,
the least and most of the seeds, the frags and the path searches that ended short. The gate of the bots' navigation is that "nav" is not above
"legacy" on any map.
"""

import argparse
import json
import shutil
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import bots_sim  # noqa: E402

MODES = {
    "legacy": {"HALO_BOTS_NAV": "0"},
    "nav": {"HALO_BOTS_NAV": "1", "HALO_BOTS_NAV_SYNC": "1", "HALO_BOTS_NAV_PACK": "/out/navs.pack"},
}


def mode_environment(mode):
    """The environment of a mode: a name in MODES, or NAME=VAR=VALUE,VAR=VALUE added to the "nav" mode's."""
    if mode in MODES:
        return dict(MODES[mode])
    if "=" in mode:
        _, pairs = mode.split("=", 1)
        environment = dict(MODES["nav"])
        environment.update(dict(item.split("=", 1) for item in pairs.split(",") if item))
        return environment
    raise ValueError(f"unknown mode {mode}")


def play(job):
    """One run of a job (a dict); returns (job, result or None, error text)."""
    out = Path(job["out"])
    if (out / "result.json").is_file() and not job["force"]:
        return job, json.loads((out / "result.json").read_text()), ""
    out.mkdir(parents=True, exist_ok=True)
    environment = mode_environment(job["mode"])
    if environment.get("HALO_BOTS_NAV") == "1" or "HALO_BOTS_NAV_PACK" in environment:
        shutil.copy(job["pack"], out / "navs.pack")
    if job["tour"]:
        environment["HALO_BOTS_TOUR"] = "1"
    if job["stuck_log"]:
        environment["HALO_BOTS_STUCK_LOG"] = str(job["stuck_log"])
    environment.update(job["environment"])
    try:
        result, _ = bots_sim.run(job["map"], job["variant"], job["bots"], job["skill"], job["ticks"], job["seed"], "ppc-be", out, job["tree"], job["maps"],
                                 log_ticks=30, teams=job["teams"], environment=environment, timeout=job["timeout"])
        return job, result, ""
    except Exception as error:  # a failed run is reported, the others go on
        return job, None, str(error)[-600:]


def table(results):
    """Rows (map, mode, runs, stuck seconds, bot-seconds, share, least, most, frags, short) from [(job, result)]."""
    groups = {}
    for job, result in results:
        groups.setdefault((job["map"], job["mode"]), []).append(result)
    rows = []
    for (map_name, mode), found in sorted(groups.items()):
        stuck = sum(r["stuck_s"] for r in found)
        seconds = sum(r["bots"] * r["ticks"] / 30.0 for r in found)
        shares = [r["stuck_share"] for r in found]
        rows.append((map_name, mode, len(found), stuck, seconds, stuck / max(seconds, 1.0), min(shares), max(shares), sum(r["frags"] for r in found),
                     sum(r["path_short"] for r in found), sum(r["path_searches"] for r in found)))
    return rows


def format_table(rows):
    lines = [f"{'map':16} {'mode':14} {'runs':>4} {'stuck s':>8} {'bot-s':>7} {'share':>7} {'least':>7} {'most':>7} {'frags':>6} {'short/searches':>15}"]
    for map_name, mode, runs, stuck, seconds, share, least, most, frags, short, searches in rows:
        lines.append(f"{map_name:16} {mode:14} {runs:4d} {stuck:8d} {seconds:7.0f} {share * 100:6.2f}% {least * 100:6.2f}% {most * 100:6.2f}% {frags:6d} "
                     f"{short:7d}/{searches:<7d}")
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--out", required=True)
    parser.add_argument("--pack", help="navs.pack for the nav modes")
    parser.add_argument("--maps", default="bloodgulch,chillout,boardingaction")
    parser.add_argument("--modes", default="legacy,nav")
    parser.add_argument("--seeds", default="1,2,3")
    parser.add_argument("--bots", type=int, default=6)
    parser.add_argument("--ticks", type=int, default=3600)
    parser.add_argument("--skill", default="normal")
    parser.add_argument("--variant", default="slayer")
    parser.add_argument("--teams", default="even")
    parser.add_argument("--tour", action="store_true")
    parser.add_argument("--stuck-log", type=int, default=0, help="1: log each stuck second, 2: and every bot's tick (debug.bots_stuck_log)")
    parser.add_argument("--tree")
    parser.add_argument("--maps-folder", dest="maps_folder")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--timeout", type=int, help="seconds a game may take (the default is ticks / 8: a machine that is busy needs more)")
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--env", action="append", default=[], metavar="NAME=VALUE")
    args = parser.parse_args(argv)
    out = Path(args.out)
    jobs = []
    for map_name in args.maps.split(","):
        for mode in args.modes.split(","):
            for seed in (int(item) for item in args.seeds.split(",")):
                jobs.append({"map": map_name, "mode": mode, "seed": seed, "bots": args.bots, "ticks": args.ticks, "skill": args.skill,
                             "variant": args.variant, "teams": args.teams, "tour": args.tour, "stuck_log": args.stuck_log, "tree": args.tree,
                             "maps": args.maps_folder, "pack": args.pack, "force": args.force, "timeout": args.timeout,
                             "environment": dict(item.split("=", 1) for item in args.env),
                             "out": str(out / f"{map_name}-{mode.split('=')[0]}-s{seed}")})
    started = time.monotonic()
    good, failed = [], []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for job, result, error in pool.map(play, jobs):
            if result is None:
                failed.append((job, error))
                print(f"FAILED {job['out']}: {error}", flush=True)
            else:
                good.append((job, result))
                print(f"done {job['out']}: stuck {result['stuck_s']} ({result['stuck_share'] * 100:.1f}%) frags {result['frags']}", flush=True)
    text = format_table(table(good))
    print(text)
    (out / "table.txt").write_text(text + "\n")
    print(f"{time.monotonic() - started:.0f} s wall, {len(failed)} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
