#!/usr/bin/env python3
"""Checks of the checkpoint's write on a thread (reports/research/2026-11-04/async-save.md) on the digest harness's runs.

    tools/wiiu/async_save_check.py log RUN...             each run's checkpoints: every one copied was written (or written again), none refused, none lost
    tools/wiiu/async_save_check.py same RUN_A RUN_B       the runs' game ticks and frames equal, their savegame.bin (z/, GAME_DIGEST_KEEP_DATA=1) equal up to a few bytes

A RUN is a name under build/game-digest/runs or a folder. Run both with HALO_ASYNC_SAVE_VERIFY=1 to have the copy checked against the game state it was taken from
and the file read back against the copy (the log says so). The one place the two paths' files differ is 2 bytes of the objects region at offset 0x24f8 (file offset
18025380 in the b30 / bloodgulch runs of 2026-11-04): a stack-address-like field that changes with what the game thread logs (with halo/async_save_verify.txt on or off
too, in either path), so `same` allows the differences it is given (--allow FIRST-LAST, file offsets) and says how many bytes it found outside them.
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RUNS = ROOT / "build" / "game-digest" / "runs"


def folder(name: str) -> Path:
    return Path(name) if Path(name).is_dir() else RUNS / name


def log_of(run: str) -> list:
    return (folder(run) / "out" / "stderr.txt").read_text(errors="replace").splitlines()


def check_log(run: str) -> int:
    lines = [line for line in log_of(run) if "checkpoint" in line and "network test" not in line]
    copied = len([line for line in lines if re.search(r"checkpoint \d+: the game state copied", line)])
    written = len([line for line in lines if re.search(r"checkpoint \d+: \d+ KB written on the writer thread", line)])
    refused = [line for line in lines if re.search(r"NOT written|NOT the copy|DIFFERENT|READ BACK DIFFERENT|LOST|cannot|could not", line)]
    again = len([line for line in lines if re.search(r"checkpoint \d+: written$", line)])
    sync = len([line for line in lines if "written on the game thread" in line])
    waits = len([line for line in lines if re.search(r"waited \d+ ms for its write", line)])
    reverts = len([line for line in lines if "reverted to the saved game" in line])
    print(f"{run}: {copied} copied, {written} written on the thread, {again} written again on the game thread, {sync} written on the game thread, {waits} waits, {reverts} reverts, "
          f"{len(refused)} refusals")
    for line in refused[:10]:
        print("   ", line[:200])
    return 0 if copied + sync > 0 and copied == written + again + len([line for line in refused if "LOST" in line]) and not [line for line in refused if "LOST" not in line and "NOT written" not in line] else 1


def tick_lines(run: str) -> list:
    lines = []
    for line in log_of(run):
        found = re.search(r"network test: tick (\d+) (.*)", line)
        if found:
            rest = re.sub(r"camera \([^)]*\)", "camera", found.group(2))
            lines.append((int(found.group(1)), re.sub(r"sent \d+ received \d+", "sent _ received _", rest)))
    return lines


def crcs(run: str) -> list:
    return re.findall(r"checkpoint \d+: crc (\w+) of", "\n".join(log_of(run)))


def same(a: str, b: str, allow: list) -> int:
    bad = 0
    ta, tb = tick_lines(a), tick_lines(b)
    count = min(len(ta), len(tb))
    print(f"game ticks: {len(ta)} and {len(tb)} lines, the first {count} {'equal' if ta[:count] == tb[:count] else 'DIFFER'}")
    bad += ta[:count] != tb[:count]
    ca, cb = crcs(a), crcs(b)
    print(f"checkpoint CRCs: {len(ca)} and {len(cb)}, {sum(x == y for x, y in zip(ca, cb))} equal")
    fa, fb = folder(a) / "data" / "z" / "savegame.bin", folder(b) / "data" / "z" / "savegame.bin"
    if fa.is_file() and fb.is_file():
        da, db = fa.read_bytes(), fb.read_bytes()
        different = [i for i in range(min(len(da), len(db))) if da[i] != db[i]] if da != db else []
        outside = [i for i in different if not any(first <= i <= last for first, last in allow)]
        print(f"savegame.bin: {len(da)} and {len(db)} bytes, {len(different)} bytes differ ({len(outside)} outside the allowed){' at ' + str(different[:8]) if different else ''}")
        bad += len(da) != len(db) or bool(outside)
    else:
        print("savegame.bin: not kept (GAME_DIGEST_KEEP_DATA=1)")
    return 1 if bad else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], epilog=__doc__.split("\n\n", 1)[1], formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    log = commands.add_parser("log")
    log.add_argument("runs", nargs="+")
    pair = commands.add_parser("same")
    pair.add_argument("a")
    pair.add_argument("b")
    pair.add_argument("--allow", action="append", default=[], metavar="FIRST-LAST")
    args = parser.parse_args()
    if args.command == "log":
        return max(check_log(run) for run in args.runs)
    allow = [tuple(int(x) for x in item.split("-")) for item in args.allow]
    return same(args.a, args.b, allow)


if __name__ == "__main__":
    sys.exit(main())
