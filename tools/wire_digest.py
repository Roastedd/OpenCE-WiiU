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


SOCIAL = "main_menu/multiplayer_type_select/screens"


def smoke(head, arguments, results):
    """screens.enable 2 with the friends layer on, in the real game: the Friends screen, the Players page and ADD FRIEND opened by debug.menu_open (the menus' own way to start on one
    screen), the D-pad and A pressed on the game's own test input; the log's `screens:` lines say what each screen opened with, and which provider (the game's roster with the
    social backend). There is no network (the run has none), so the layer is up with no broker: the service reads CONNECTING and the lists are empty."""
    for name, screen, buttons in (("friends", f"{SOCIAL}/friends_screen", "wait,wait,right,right,a,wait,down,down,a,b,b"), ("players", f"{SOCIAL}/players_screen", "wait,wait,down,a,b,b"),
                                  ("add", f"{SOCIAL}/add/friend_add_screen", "wait,wait,a,b")):
        extra = dict(OFF, HALO_SCREENS="2", HALO_MENU_OPEN=screen, HALO_TEST_INPUT=f"menu:{buttons}")
        run = start(head, f"wire-smoke-{name}", "menu", arguments.frames, extra, {})
        digest.finish(*run)
        results[f"smoke-{name}"] = logged(run[1], r"screens:|social:|menus: .*(error|not|problem|unresolved)|there is no widget")[:40]
        print(f"smoke {name}:", *results[f"smoke-{name}"], sep="\n  ", flush=True)
    (arguments.out / "wire-smoke.json").write_text(json.dumps(results, indent=1, default=str))


def repeated(base, head, name, mode, number, menu, extra, own, times, results):
    """times runs of each build, two at a time; every run against every other: the base's against each other (how much one build moves by itself under this machine's load), the head's, and
    the base's against the head's"""
    runs = []
    for index in range(times):
        pair = (start(base, f"wire-{name}-base{index}", mode, number, extra, own), start(head, f"wire-{name}-head{index}", mode, number, extra, own))
        for kind, run in zip(("base", "head"), pair):
            summary = digest.finish(*run)
            runs.append((f"{kind}{index}", run[1], summary.get("game-digest")))
    matrix = {}
    for i, (a, out_a, _) in enumerate(runs):
        for b, out_b, _ in runs[i + 1:]:
            matrix[f"{a} vs {b}"] = compare(out_a, out_b, menu=menu)
    results[name + "-repeat"] = {"digests": {label: value for label, _, value in runs}, "compare": matrix,
                                 "base_vs_base_equal": all(v == 0 for k, v in matrix.items() if k.startswith("base") and " vs base" in k),
                                 "base_vs_head_equal": all(v == 0 for k, v in matrix.items() if k.startswith("base") and " vs head" in k)}
    print(name, "repeat", json.dumps(results[name + "-repeat"], default=str), flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", default="1b3e0dcb")
    parser.add_argument("--ticks", type=int, default=300)
    parser.add_argument("--frames", type=int, default=200)
    parser.add_argument("--controls", action="store_true")
    parser.add_argument("--repeat", type=int, default=0, help="N: run each scenario N times by each build and compare every pair (the base against itself too), to see how much the runs move by themselves")
    parser.add_argument("--smoke", action="store_true", help="only the screens-on smoke runs (screens.enable 2 with the friends layer on: each screen opened by debug.menu_open)")
    parser.add_argument("--only", action="append", help="a scenario's name (several): the others are left out")
    parser.add_argument("out", type=Path)
    arguments = parser.parse_args()
    arguments.out.mkdir(parents=True, exist_ok=True)
    results = {"base": arguments.base, "head_environment": OFF}
    head = digest.build()
    if arguments.smoke:
        smoke(head, arguments, results)
        return 0
    base = digest.build("base", rev=arguments.base)
    for name, mode, count, extra, own in SCENARIOS:
        if arguments.only and name not in arguments.only:
            continue
        number = arguments.frames if count is None else arguments.ticks
        if arguments.repeat:
            repeated(base, head, name, mode, number, count is None, dict(OFF, **extra), own, arguments.repeat, results)
            continue
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
