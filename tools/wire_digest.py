#!/usr/bin/env python3
"""The merged streams against the tree they were cut from, on the host: the game digest's program (the whole game drawn through the Wii U's Direct3D layer under qemu-ppc,
port/wiiu/tests/game_digest) built from this checkout and from a base commit, and run with the friends layer ON and everything else OFF.

    python3 tools/wire_digest.py [--base REV] [--ticks N] [--frames N] [--controls] OUT_DIR

Settings of the head's runs: HALO_SOCIAL=1 (the friends layer starts: its thread, its identity file, its once-a-frame call), HALO_SCREENS=0, and no bots setting but the games' own
(local Slayer with HALO_BOTS=3 is the split screen game's existing bots: bots.online, bots.nav and bots.objectives are all at their defaults, off). The base's runs have the same
environment (it ignores what it does not know). Four scenarios, each run by both builds at once:

  menu                  the main menu, 200 frames, matched by their order
  local-slayer-bots     Blood Gulch Slayer as the game server of this machine's one player, 3 bots, N ticks
  blood-gulch-plain     Blood Gulch as a game of one player, the plain loop (no tick thread, no overlap, no governor), N ticks
  coop-hosted           b30 hosted as network co-op, no one joining, N ticks

Every frame's digest is compared (tools/wiiu/digest_compare.py), the game's own tick lines (the players' state, "network test: tick N") are compared as text, and the head's log is
read for the lines that show the layer was on. --controls adds the negative controls: the head with screens.enable 1 must draw other frames (the pause menu has a button, the lobby its
tags, the main menu its toasts), and a head built with a deliberate fault in the Direct3D layer (test_wiiu_game_digest.BROKEN_KEY) must differ from the base. Nothing here touches a console.
It needs Docker, the opence-ppc-be image and the maps (tools/test_wiiu_game_digest.py's notes).
"""
import argparse
import json
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "wiiu"))
import digest_compare  # noqa: E402
import test_wiiu_game_digest as digest  # noqa: E402

# the head's environment: the friends layer on, the screens off, the bots' own switches at their defaults
OFF = {"HALO_SOCIAL": "1", "HALO_SCREENS": "0"}

SCENARIOS = (
    # name, digest mode, ticks (None: the frames), extra environment for the game, the run's own settings (digest.start's extra)
    ("menu", "menu", None, {}, {}),
    ("local-slayer-bots", "local-slayer", "ticks", {"HALO_BOTS": "3"}, {}),
    ("blood-gulch-plain", "solo-bloodgulch", "ticks", {}, digest.PLAIN),
    ("coop-hosted", "host", "ticks", {}, {}),
)


def environment(extra):
    return ",".join(f"{key}={value}" for key, value in extra.items())


def start(program, run, mode, count, extra_environment, own):
    os.environ["GAME_DIGEST_ENV"] = environment(extra_environment)
    return digest.start(program, run, count, mode=mode, extra=own)


def tick_lines(out):
    return digest.tick_lines(out)


def logged(out, pattern):
    return [line for line in (out / "stderr.txt").read_text(errors="replace").splitlines() if re.search(pattern, line)]


def compare(a, b, menu=False):
    with open(os.devnull, "w") as quiet:
        return digest_compare.compare(a, b, out=quiet, menu=True) if menu else digest_compare.compare(a, b, out=quiet)


def frames_of(out):
    return len([line for line in (out / "frames.txt").read_text().splitlines() if line.strip()])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", default="1b3e0dcb")
    parser.add_argument("--ticks", type=int, default=300)
    parser.add_argument("--frames", type=int, default=200)
    parser.add_argument("--controls", action="store_true")
    parser.add_argument("--only", action="append", help="a scenario's name (several): the others are left out")
    parser.add_argument("out", type=Path)
    arguments = parser.parse_args()
    arguments.out.mkdir(parents=True, exist_ok=True)
    results = {"base": arguments.base, "head_environment": OFF}
    head = digest.build()
    base = digest.build("base", rev=arguments.base)
    for name, mode, count, extra, own in SCENARIOS:
        if arguments.only and name not in arguments.only:
            continue
        number = arguments.frames if count is None else arguments.ticks
        pair = (start(base, f"wire-{name}-base", mode, number, dict(OFF, **extra), own), start(head, f"wire-{name}-head", mode, number, dict(OFF, **extra), own))
        summaries = [digest.finish(*run) for run in pair]
        out_a, out_b = pair[0][1], pair[1][1]
        result = {
            "frames": [frames_of(out_a), frames_of(out_b)],
            "digest_compare": compare(out_a, out_b, menu=count is None),
            "game_digest": [summaries[0].get("game-digest"), summaries[1].get("game-digest")],
            "tick_lines": [len(tick_lines(out_a)), len(tick_lines(out_b))],
            "tick_lines_equal": tick_lines(out_a) == tick_lines(out_b),
            "social_lines_in_head": len(logged(out_b, r"social:")),
            "social_on_line": logged(out_b, r"screens: friends are on")[:1],
            "social_identity_line": logged(out_b, r"social: identity")[:1],
        }
        results[name] = result
        print(name, json.dumps(result, default=str), flush=True)
        if arguments.controls and name in ("menu", "local-slayer-bots"):
            # the screens on: other frames (the pause menu's button, the lobby's tags, the main menu's toasts)
            on = start(head, f"wire-{name}-screens", mode, number, dict(OFF, HALO_SCREENS="1", **extra), own)
            digest.finish(*on)
            results[name]["control_screens_1_differs"] = compare(out_a, on[1], menu=count is None) != 0
            print(name, "control screens 1 differs:", results[name]["control_screens_1_differs"], flush=True)
    if arguments.controls:
        broken = digest.build("broken-key", mutations=[digest.BROKEN_KEY])
        name, mode, count, extra, own = SCENARIOS[1]
        if not arguments.only or name in arguments.only:
            out_a = digest.DIGEST / "runs" / f"wire-{name}-base" / "out"
            run = start(broken, f"wire-{name}-broken", mode, arguments.ticks, dict(OFF, **extra), own)
            digest.finish(*run)
            results["control_broken_key_differs"] = compare(out_a, run[1]) != 0
            print("control broken key differs:", results["control_broken_key_differs"], flush=True)
    (arguments.out / "wire-digest.json").write_text(json.dumps(results, indent=1, default=str))
    print(json.dumps(results, indent=1, default=str))
    failed = [name for name, _, _, _, _ in SCENARIOS if name in results and (results[name]["digest_compare"] != 0 or not results[name]["tick_lines_equal"])]
    controls = [value for key, value in results.items() if key.startswith("control_")]
    controls += [value for name, _, _, _, _ in SCENARIOS if name in results for key, value in results[name].items() if key.startswith("control_")]
    return 1 if failed or (arguments.controls and not all(controls)) else 0


if __name__ == "__main__":
    sys.exit(main())
