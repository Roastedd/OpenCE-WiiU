#!/usr/bin/env python3
"""One line for each run folder of tools/harness/netoverlap_two_machines.sh: tools/net_compare.py's judgement of the two machines' logs and the figures of the machine
that ran the tick overlap (the Wii U digest's program): frames overlapped and serial, the drawing's waits at the barrier, the tick thread's late fall backs, and anything that
stopped the game.

    tools/harness/netoverlap_report.py RUN_FOLDER... [--json OUT]

Exit status 1 if any folder's judgement is not PASS or a machine stopped on an assertion.
"""
import argparse
import json
import re
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

STOPPED = re.compile(r"unused or changed|EXCEPTION|Assertion|assertion|display_assert|halted|HALT|data verify|#412|crash|SIGSEGV|segmentation|Segmentation|index 0x|fatal", re.I)
# (lines the game logs about itself that name these words and are not a stop)
NOT_STOPPED = re.compile(r"no assertion|assertions? kept|release|ASSERT_VALID|the game host went down|network_game_abort|halted time|time is halted", re.I)


COMPARE_OPTIONS: list = []


def judge(folder: Path) -> dict:
    host, client = folder / "host.log", folder / "client.log"
    result = subprocess.run([sys.executable, str(ROOT / "tools/net_compare.py"), *COMPARE_OPTIONS, "--compare", str(host), str(client)], capture_output=True, text=True)
    text = result.stdout + result.stderr
    out = {"folder": folder.name, "judgement": (text.split(":", 1)[0] or "?").strip(), "exit": result.returncode}
    found = re.search(r"(\d+) seconds compared, tick offsets \[([^\]]*)\]; positions apart mean ([\d.]+) largest ([\d.]+), aim largest (\d+) degrees; mismatched seconds (.*)", text)
    if found:
        out.update(seconds=int(found[1]), offsets=found[2], position_mean=float(found[3]), position_max=float(found[4]), aim_max=int(found[5]), mismatched=found[6].strip())
    found = re.search(r"drift: (\d+) of (\d+) ticks apart \(([\d.]+)%\), mean ([\d.]+), largest ([\d.]+)", text)
    if found:
        out.update(drift_apart=int(found[1]), drift_ticks=int(found[2]), drift_share=float(found[3]), drift_mean=float(found[4]))
    found = re.search(r"client corrections: (\d+)", text)
    if found:
        out["corrections"] = int(found[1])
    out["problems"] = re.findall(r"PROBLEM: (.*)", text) + re.findall(r"FAIL[^\n]*\n((?:  [^\n]*\n)*)", text)
    return out


def machine(path: Path) -> dict:
    out = {"stopped": [], "overlap_windows": 0}
    if not path.is_file():
        return out
    frames = overlapped = serial = primed = held = 0
    waits = barriers = barrier_waits = 0
    barrier_ms = 0.0
    fps, ticks_a_second, busy, wait_ms, work_ms = [], [], [], [], []
    fallbacks = 0
    late = 0
    last_tick = 0
    for line in path.read_text(errors="replace").splitlines():
        found = re.search(r"tick overlap \(([\d.]+) s, (\d+) frames: (\d+) overlapped, (\d+) serial, (\d+) primed, (\d+) held, (\d+) no pair\): ([\d.]+) ticks a second, ([\d.]+) frames a second", line)
        if found:
            out["overlap_windows"] += 1
            frames += int(found[2])
            overlapped += int(found[3])
            serial += int(found[4])
            primed += int(found[5])
            held += int(found[6])
            ticks_a_second.append(float(found[8]))
            fps.append(float(found[9]))
            continue
        found = re.search(r"tick overlap, tick thread: ([\d.]+) ms a job .* busy (\d+)% of the time", line)
        if found:
            busy.append(int(found[2]))
            continue
        found = re.search(r"tick overlap, game thread, ms a frame \((\d+)\): ([\d.]+) .* ([\d.]+) \(most [\d.]+\) of work to the end of the draw, waiting for the tick thread ([\d.]+)", line)
        if found:
            work_ms.append(float(found[3]))
            wait_ms.append(float(found[4]))
            continue
        found = re.search(r"tick overlap, network game: (\d+) overlapped frames, the drawing reached the barrier in (\d+) and waited for the tick thread in (\d+) \(([\d.]+) ms in all", line)
        if found:
            barriers += int(found[2])
            barrier_waits += int(found[3])
            barrier_ms += float(found[4])
            continue
        found = re.search(r"in the window: \d+ misses, \d+ catch-ups, (\d+) fallbacks", line)
        if found:
            fallbacks += int(found[1])
        if "serial frames for" in line and "the tick thread was late" in line:
            late += 1
        found = re.search(r"network test: tick (\d+) ", line)
        if found:
            last_tick = max(last_tick, int(found[1]))
        if STOPPED.search(line) and not NOT_STOPPED.search(line):
            out["stopped"].append(line.strip()[:160])
    out.update(frames=frames, overlapped=overlapped, serial=serial, primed=primed, held=held, barriers=barriers, barrier_waits=barrier_waits, barrier_ms=round(barrier_ms, 1),
               fallbacks_after_first=max(0, fallbacks - 1000), fallbacks=fallbacks, late_fall_backs=late, last_tick=last_tick)
    if fps:
        out["fps_median"] = round(statistics.median(fps), 1)
        out["ticks_a_second_median"] = round(statistics.median(ticks_a_second), 1)
    if busy:
        out["tick_thread_busy_median"] = statistics.median(busy)
    if wait_ms:
        out["game_thread_wait_ms_mean"] = round(statistics.mean(wait_ms), 2)
        out["work_ms_mean"] = round(statistics.mean(work_ms), 2)
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("folders", nargs="+")
    parser.add_argument("--json")
    parser.add_argument("--seconds", type=int, default=400, help="net_compare's --seconds (seconds of play compared)")
    parser.add_argument("--pair-ticks", type=int, default=2, help="net_compare's pair_ticks tolerance")
    args = parser.parse_args()
    COMPARE_OPTIONS.extend(["--seconds", str(args.seconds), "--tolerance", f"pair_ticks={args.pair_ticks}"])
    rows, bad = [], False
    for name in args.folders:
        folder = Path(name)
        row = judge(folder)
        row["client"] = machine(folder / "client.log")
        row["host"] = machine(folder / "host.log")
        rows.append(row)
        client, host = row["client"], row["host"]
        print(f"{row['folder']}: {row['judgement']} {row.get('seconds', 0)} s, positions apart mean {row.get('position_mean', 'n/a')} max {row.get('position_max', 'n/a')}, "
              f"drift {row.get('drift_share', 'n/a')}% mean {row.get('drift_mean', 'n/a')}, corrections {row.get('corrections', 'n/a')}, mismatched {row.get('mismatched', 'n/a')}")
        if client.get("overlap_windows") or host.get("overlap_windows"):
            who = client if client.get("overlap_windows") else host
            print(f"    overlap: {who.get('overlapped', 0)} overlapped / {who.get('serial', 0)} serial / {who.get('primed', 0)} primed / {who.get('held', 0)} held frames, "
                  f"{who.get('ticks_a_second_median')} ticks/s {who.get('fps_median')} fps (medians), tick thread busy {who.get('tick_thread_busy_median')}%, barrier {who.get('barrier_waits')} waits of "
                  f"{who.get('barriers')} ({who.get('barrier_ms')} ms), late fall backs {who.get('late_fall_backs')}, snapshot fallbacks {who.get('fallbacks')}")
        for role in ("client", "host"):
            if row[role].get("stopped"):
                print(f"    {role} STOPPED: {row[role]['stopped'][:3]}")
                bad = True
        if row["judgement"] != "PASS":
            bad = True
    if args.json:
        Path(args.json).write_text(json.dumps(rows, indent=1))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
