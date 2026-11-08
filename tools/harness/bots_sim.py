"""A map played headless with N bots for T ticks, and what the bots did (port/linux/game/bots.c, bots_stats.c).

    python3 tools/harness/bots_sim.py run --map bloodgulch --bots 6 --ticks 3600 --out runs/bots/a
    python3 tools/harness/bots_sim.py compare runs/bots/a runs/bots/b      (two runs of the same input: identical?)

The game plays a local (split screen) game by itself: debug.network_test "local:<map>:<variant>" starts it, the scripted
controller 1 of debug.test_input ("tickbot") plays the one human, debug.fixed_step makes each frame exactly one tick and
debug.random_seed pins the game's random numbers, so two runs of the same build and input play the same game; the null
renderer draws nothing. bots.sim_ticks ends the run after that many ticks of bots with the block of lines that starts
"bots-sim:" (each bot's frags, deaths, seconds stuck, path searches that failed or ended short, sight tests and think
time, then the totals). run reads that block and the game's own per-second player lines into result.json:

    frags, deaths      each bot's, and the total (the kills and deaths the game counted)
    stuck_s            seconds a bot stood still while it walked (the stuck check, once a second)
    path_failures      searches that found no path; path_short, searches that ended short of the goal
    think_us           a bot's microseconds in a tick (mean, 99th percentile, longest), and all bots' in a tick
    ms_per_tick        the game loop's wall time a tick, from the first tick of bots to the last (the whole tick:
                       players, objects, the network layer; nothing drawn)

One caution: the scripted human (controller 1) is the only part of a game the machine's load can change. Many games
played at once on one machine have, rarely, moved it differently in its first ticks (and so the bots' fights with it);
compare two runs made one after the other, and play a run again if the first lines differ.

The think times and ms_per_tick are of the machine the game ran on: under qemu (the ppc runner) they say how the
code scales, not what the Wii U takes. Everything else is counted from the game's state, so it is the same in two runs.

Runners: "native" is ninja linux's build/linux/halo with the maps in assets/maps (what tools/test_bots.py plays);
"ppc-be" is the big-endian build under qemu-ppc in the opence-ppc-be image (tools/ppc-be), with the converted maps
(data-be/maps), which is what runs on a Mac. The folders given to the container must be under /Users (Colima mounts
nothing else).
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parent.parent.parent  # the game's folder (upstream/OpenCE or a worktree)
WORKSPACE = REPOSITORY.parent.parent                         # HALOCE_WIIU, when the game is in scratch/ or upstream/
BOTS_FIRST_MACHINE = 113

# a line of the game's log, without the "halo-linux: " that stderr adds
LOG_PREFIX = "halo-linux: "
PLAYER = re.compile(r"player (\d+): (?:\((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\)|dead).*? s-?\d+ k(-?\d+) d(-?\d+) f-?\d+ "
                    r"t-?\d+ m(\d+)")
BEGIN = re.compile(r"bots-sim: begin \((?P<why>[^)]*)\) map (?P<map>\S*) ticks (?P<ticks>\d+) bots (?P<bots>\d+)"
                   r"(?: ms_per_tick (?P<ms>[\d.]+))?")
BOT = re.compile(r"bots-sim: bot (?P<bot>\d+) (?P<skill>\w+) frags (?P<frags>-?\d+) deaths (?P<deaths>-?\d+) "
                 r"stuck_s (?P<stuck>\d+) giveups (?P<giveups>\d+) searches (?P<searches>\d+) failed (?P<failed>\d+) "
                 r"short (?P<short>\d+) rays (?P<rays>\d+) think_us (?P<mean>\d+)/(?P<p99>\d+)/(?P<max>\d+)")
TOTAL = re.compile(r"bots-sim: total frags (?P<frags>-?\d+) deaths (?P<deaths>-?\d+) stuck_s (?P<stuck>\d+) searches "
                   r"(?P<searches>\d+) failed (?P<failed>\d+) short (?P<short>\d+) rays (?P<rays>\d+)")
TICK_US = re.compile(r"bots-sim: tick_us mean (?P<mean>\d+) p99 (?P<p99>\d+) max (?P<max>\d+) ticks (?P<ticks>\d+)")
MINUTE = re.compile(r"bots: tick (\d+), (\d+) bots \((\w+)\): searches (\d+) \((\d+) failed, (\d+) short")


def number(text):
    """a count or a time of a log line, else the text"""
    if re.fullmatch(r"-?\d+", text):
        return int(text)
    if re.fullmatch(r"-?\d+\.\d+", text):
        return float(text)
    return text


def parse(text):
    """The bots-sim block of a game's log and the per-second player lines: a dict, or ValueError if the block is not
    there or is not whole (a run that did not reach its ticks, or that crashed)."""
    lines = [line[len(LOG_PREFIX):] if line.startswith(LOG_PREFIX) else line for line in text.splitlines()]
    block = [line for line in lines if line.startswith("bots-sim:")]
    if not block or not block[-1].startswith("bots-sim: end"):
        raise ValueError("no whole bots-sim block in the log (the run ended before bots.sim_ticks, or crashed)")
    # (the last block: a map that ended before the run's end said one too)
    start = max(index for index, line in enumerate(block) if line.startswith("bots-sim: begin"))
    block = block[start:]
    result = {"bots": {}, "log_players": {}}
    for line in block:
        for pattern, key in ((BEGIN, "begin"), (TOTAL, "total"), (TICK_US, "tick_us")):
            match = pattern.search(line)
            if match:
                result[key] = {name: number(value) for name, value in match.groupdict().items() if value is not None}
        match = BOT.search(line)
        if match:
            fields = match.groupdict()
            index, skill = int(fields.pop("bot")), fields.pop("skill")
            result["bots"][index] = {"skill": skill, **{name: int(value) for name, value in fields.items()}}
    for key in ("begin", "total", "tick_us"):
        if key not in result:
            raise ValueError(f"the bots-sim block has no {key} line")
    if len(result["bots"]) != result["begin"]["bots"]:
        raise ValueError(f"the bots-sim block has {len(result['bots'])} bot lines for {result['begin']['bots']} bots")
    # the game's per-second lines for every player (also the human's): the last kills and deaths
    for line in lines:
        if "network test: tick" not in line:
            continue
        for match in PLAYER.finditer(line):
            result["log_players"][int(match.group(1))] = {
                "kills": int(match.group(5)), "deaths": int(match.group(6)), "machine": int(match.group(7))}
    return result


def summary(parsed, wall_seconds=None, ticks=None):
    """The numbers a baseline is made of."""
    total, tick = parsed["total"], parsed["tick_us"]
    bots = parsed["bots"]
    result = {
        "map": parsed["begin"]["map"], "ticks": parsed["begin"]["ticks"], "bots": parsed["begin"]["bots"],
        "frags": total["frags"], "deaths": total["deaths"], "stuck_s": total["stuck"],
        "path_searches": total["searches"], "path_failures": total["failed"], "path_short": total["short"],
        "sight_tests": total["rays"],
        "stuck_share": round(total["stuck"] / max(1, parsed["begin"]["bots"] * parsed["begin"]["ticks"] / 30.0), 4),
        "bot_think_us": {"mean": tick["mean"], "p99": tick["p99"], "max": tick["max"]},
        "per_bot": {str(index): {name: bot[name] for name in ("skill", "frags", "deaths", "stuck", "failed", "short")}
                    for index, bot in sorted(bots.items())},
    }
    if "ms" in parsed["begin"]:
        result["ms_per_tick"] = parsed["begin"]["ms"]
    if wall_seconds is not None:
        result["wall_seconds"] = round(wall_seconds, 1)
    return result


def kept_lines(text, with_block=True):
    """The lines two runs of the same input must share: every per-second player line of the network test (positions,
    health, weapons, kills and deaths of all players, less a dead player's camera), the bots' minute lines and, unless with_block is false, the
    bots-sim block less its times (think_us, ms_per_tick and the tick_us line)."""
    keep = []
    for line in text.splitlines():
        line = line[len(LOG_PREFIX):] if line.startswith(LOG_PREFIX) else line
        if line.startswith("network test: tick"):
            # (the human's camera once it is dead drifts by the wall clock, not the game's ticks: not a part of the game)
            keep.append(re.sub(r"camera \([^)]*\)", "camera", line))
        elif re.match(r"bots: (tick|fights since)", line):
            keep.append(line)
        elif with_block and line.startswith("bots-sim:"):
            keep.append(re.sub(r" think_us \S+| ms_per_tick \S+|bots-sim: tick_us.*", "", line))
    return keep


def fingerprint(text):
    """sha256 of the lines two runs of the same input must share (kept_lines), and their number."""
    keep = kept_lines(text)
    return hashlib.sha256("\n".join(keep).encode()).hexdigest(), len(keep)


def differences(first, second, prefix=False):
    """The first lines where the shared parts of two logs differ, as (line number, first, second) triples. With prefix,
    only the lines both logs have are compared (runs of builds that stop at different times, one without the block)."""
    a, b = kept_lines(first, not prefix), kept_lines(second, not prefix)
    found = []
    for index in range(min(len(a), len(b)) if prefix else max(len(a), len(b))):
        left = a[index] if index < len(a) else None
        right = b[index] if index < len(b) else None
        if left != right:
            found.append((index + 1, left, right))
            if len(found) >= 3:
                break
    return found


# ---------- running the game

def game_environment(settings):
    """The game's environment for a headless run of settings (a dict: see run's arguments)."""
    ticks = settings["ticks"]
    environment = {
        "SDL_VIDEODRIVER": "offscreen", "SDL_AUDIODRIVER": "dummy", "HALO_HIDDEN_WINDOW": "1", "HALO_FULLSCREEN": "0",
        "HALO_NO_VSYNC": "1", "HALO_NO_AUDIO": "1", "HALO_NULL_RENDERER": "1", "HALO_UPDATE_AUTO": "false",
        "HALO_NET_ONLINE": "false", "HALO_NET_ADDRESS": "127.0.0.1", "HALO_NET_BROADCAST": "127.255.255.255",
        "HALO_NETWORK_TEST": f"local:{settings['map']}:{settings['variant']}", "HALO_NETWORK_TEST_START": "1",
        "HALO_NETWORK_TEST_SCORE": "500", "HALO_NETWORK_TEST_LOG": str(settings["log_ticks"]),
        "HALO_TEST_INPUT": f"tickbot:{settings['seed']}", "HALO_FIXED_STEP": "1", "HALO_RANDOM_SEED": str(settings["seed"]),
        "HALO_BOTS": str(settings["bots"]), "HALO_BOT_SKILL": settings["skill"], "HALO_BOT_TEAMS": settings["teams"],
        "HALO_BOTS_SIM_TICKS": str(ticks),
        # (a safety stop by the clock, well past the run: an emulator plays a few hundred ticks a second)
        "HALO_EXIT_AFTER": str(settings["timeout"]),
    }
    environment.update(settings.get("environment", {}))
    return environment


def run_native(settings, out):
    """build/linux/halo in the repository, the maps of assets/maps."""
    game = REPOSITORY / "build/linux/halo"
    maps = REPOSITORY / "assets/maps"
    if not game.is_file() or not (maps / f"{settings['map']}.map").is_file():
        raise FileNotFoundError(f"needs build/linux/halo and assets/maps/{settings['map']}.map")
    work = out / "work"
    (work / "data").mkdir(parents=True, exist_ok=True)
    (work / "save").mkdir(exist_ok=True)
    if not (work / "data/maps").exists():
        (work / "data/maps").symlink_to(maps.resolve())
    shutil.copy(game, work / "halo")
    environment = {**os.environ, "HALO_DATA_ROOT": str(work / "data"), "HALO_SAVE_ROOT": str(work / "save"),
                   **game_environment(settings)}
    result = subprocess.run([str(work / "halo")], cwd=work, env=environment, capture_output=True, text=True,
                            errors="replace", timeout=settings["timeout"] + 120)
    debug = work / "data/debug.txt"
    return result.returncode, result.stdout + result.stderr, debug.read_text(errors="replace") if debug.is_file() else ""


def run_ppc_be(settings, out):
    """The big-endian build under qemu-ppc in the opence-ppc-be image (tools/ppc-be/run.sh builds it)."""
    tree = Path(settings.get("tree") or REPOSITORY).resolve()
    maps = Path(settings.get("maps") or WORKSPACE / "data-be/maps").resolve()
    game = tree / "build/linux-ppc/halo"
    if not game.is_file() or not (maps / f"{settings['map']}.map").is_file():
        raise FileNotFoundError(f"needs {game} (tools/ppc-be/run.sh build) and {maps}/{settings['map']}.map")
    out.mkdir(parents=True, exist_ok=True)
    environment = game_environment(settings)
    names = " ".join(f"{name}={subprocess.list2cmdline([value])}" for name, value in sorted(environment.items()))
    script = (
        "mkdir -p /run/game/data /run/game/save /run/game/home && ln -s /maps /run/game/data/maps && cd /run/game && "
        f"HOME=/run/game/home QEMU_LD_PREFIX=/usr/powerpc-linux-gnu HALO_DATA_ROOT=/run/game/data HALO_SAVE_ROOT=/run/game/save "
        f"{names} timeout -s KILL {settings['timeout'] + 240} qemu-ppc /ppc/build/linux-ppc/halo > /out/game.log 2>&1; "
        "echo \"exit status $?\" >> /out/game.log; cp /run/game/data/debug.txt /out/debug.txt 2>/dev/null || true")
    command = ["docker", "run", "--rm", "--ulimit", "core=0", "-v", f"{tree}:/ppc:ro", "-v", f"{maps}:/maps:ro",
               "-v", f"{out.resolve()}:/out", "opence-ppc-be", "bash", "-c", script]
    subprocess.run(command, check=False, timeout=settings["timeout"] + 600)
    log = out / "game.log"
    text = log.read_text(errors="replace") if log.is_file() else ""
    status = re.findall(r"exit status (\d+)", text)
    debug = out / "debug.txt"
    return int(status[-1]) if status else -1, text, debug.read_text(errors="replace") if debug.is_file() else ""


def runner_available(name):
    """Whether the runner can play here (the maps and the build are there)."""
    if name == "native":
        return (REPOSITORY / "build/linux/halo").is_file() and (REPOSITORY / "assets/maps/bloodgulch.map").is_file()
    if name == "ppc-be":
        return (REPOSITORY / "build/linux-ppc/halo").is_file() and (WORKSPACE / "data-be/maps/bloodgulch.map").is_file() \
            and shutil.which("docker") is not None and subprocess.run(
                ["docker", "image", "inspect", "opence-ppc-be"], capture_output=True).returncode == 0
    return False


def default_runner():
    for name in ("native", "ppc-be"):
        if runner_available(name):
            return name
    return None


def run(map_name="bloodgulch", variant="slayer", bots=4, skill="normal", ticks=1800, seed=7, runner=None, out=None,
        tree=None, maps=None, log_ticks=30, teams="even", timeout=None, environment=None, block=True):
    """Play the game; returns (summary, log text). The folder out gets game.log, debug.txt and result.json. With block
    false the build has no bots.sim_ticks (an older one): the run lasts timeout seconds and only the log is returned
    (summary None), for comparing the games of two builds."""
    runner = runner or default_runner()
    if runner is None:
        raise FileNotFoundError("no runner can play here: needs build/linux/halo and assets/maps, or tools/ppc-be's build "
                                "and image")
    settings = {"map": map_name, "variant": variant, "bots": bots, "skill": skill, "ticks": ticks, "seed": seed,
                "tree": tree, "maps": maps, "log_ticks": log_ticks, "teams": teams, "environment": environment or {},
                # (1,000 ticks a minute and more under qemu, a few thousand a second natively)
                "timeout": timeout or max(120, int(ticks / 8))}
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    status, text, debug = (run_native if runner == "native" else run_ppc_be)(settings, out)
    wall = time.monotonic() - started
    (out / "game.log").write_text(text)
    (out / "debug.txt").write_text(debug)
    if status != 0:
        raise RuntimeError(f"the game ended with status {status}: {text[-1500:]}")
    if re.search(r"assert|exception", text + debug, re.I):
        raise RuntimeError("an assert or exception in the game's log")
    if not block:
        return None, text
    parsed = parse(text)
    result = summary(parsed, wall)
    result["runner"] = runner
    result["settings"] = {key: value for key, value in settings.items() if key not in ("environment", "tree", "maps")}
    result["fingerprint"], result["fingerprint_lines"] = fingerprint(text)
    (out / "result.json").write_text(json.dumps(result, indent=1) + "\n")
    return result, text


def main(arguments=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    commands = parser.add_subparsers(dest="command", required=True)
    play = commands.add_parser("run", help="play a map with bots for a number of ticks")
    play.add_argument("--map", default="bloodgulch", dest="map_name")
    play.add_argument("--variant", default="slayer")
    play.add_argument("--bots", type=int, default=4)
    play.add_argument("--skill", default="normal")
    play.add_argument("--teams", default="even")
    play.add_argument("--ticks", type=int, default=1800, help="ticks of bots (30 a second)")
    play.add_argument("--seed", type=int, default=7)
    play.add_argument("--runner", choices=("native", "ppc-be"))
    play.add_argument("--tree", help="the game's folder for the ppc-be runner (default: this one)")
    play.add_argument("--maps", help="the converted maps for the ppc-be runner (default: data-be/maps of the workspace)")
    play.add_argument("--log-ticks", type=int, default=30, help="ticks between the game's player lines")
    play.add_argument("--no-block", action="store_true",
                      help="a build without bots.sim_ticks: play --timeout seconds, keep the log (to compare with compare --prefix)")
    play.add_argument("--timeout", type=int, help="seconds the game may take (with --no-block: how long it plays)")
    play.add_argument("--out", required=True)
    compare = commands.add_parser("compare", help="two runs of the same input: do they match?")
    compare.add_argument("first")
    compare.add_argument("second")
    compare.add_argument("--prefix", action="store_true", help="only the lines both logs have, without the bots-sim block")
    given = parser.parse_args(arguments)
    if given.command == "run":
        result, _ = run(given.map_name, given.variant, given.bots, given.skill, given.ticks, given.seed, given.runner,
                        given.out, given.tree, given.maps, given.log_ticks, given.teams, given.timeout,
                        block=not given.no_block)
        print(json.dumps(result, indent=1) if result else f"played {given.timeout} seconds: {given.out}/game.log")
        return 0
    first = (Path(given.first) / "game.log").read_text(errors="replace")
    second = (Path(given.second) / "game.log").read_text(errors="replace")
    found = differences(first, second, given.prefix)
    if not found:
        shared = min(len(kept_lines(first, not given.prefix)), len(kept_lines(second, not given.prefix)))
        print(f"identical: {shared} lines" + ("" if given.prefix else f", sha256 {fingerprint(first)[0][:16]}"))
        return 0
    for number, left, right in found:
        print(f"line {number}:\n  {left}\n  {right}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
