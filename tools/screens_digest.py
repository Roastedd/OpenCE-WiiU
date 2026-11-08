#!/usr/bin/env python3
"""The social screens in the real game, on the host: the game digest's program (the whole game drawn through the Wii U's Direct3D layer under qemu-ppc, port/wiiu/tests/game_digest)
built from this checkout and from the commit it was cut from, and run.

    python3 tools/screens_digest.py [--base REV] [--ticks N] [--frames N] OUT_DIR

1. OFF: the base's and this checkout's programs, screens.enable 0 (its default), run the same menu (frames) and the same game with bots (ticks), and every frame's digest is compared:
   they must be the same. (Negative control: the same runs with screens.enable 1 must differ.)
2. ON: this checkout with screens.enable 1 opens each screen by debug.menu_open (the menu's own start on one screen), presses buttons on the game's own test input
   (debug.test_input menu:...: the D-pad, A, B, one a second), and the log's `screens:` lines say what each screen showed and did. The pause menu's patch is read in the log of a
   hosted game.
Nothing here touches a console. It needs Docker, the opence-ppc-be image and the maps (tools/test_wiiu_game_digest.py's notes); a run takes minutes.
"""
import argparse
import json
import os
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "wiiu"))
import digest_compare  # noqa: E402
import test_wiiu_game_digest as digest  # noqa: E402

PLAYERS = "main_menu/multiplayer_type_select/screens/players_screen"
FRIENDS = "main_menu/multiplayer_type_select/screens/friends_screen"
ADD = "main_menu/multiplayer_type_select/screens/add/friend_add_screen"
BOTS = "main_menu/settings_select/player_setup/player_profile_edit/bot_settings/bot_settings_screen"


def run(program, name, mode, count, environment=None, **arguments):
    os.environ["GAME_DIGEST_ENV"] = ",".join(f"{key}={value}" for key, value in (environment or {}).items())
    process, out = digest.start(program, name, count, mode=mode, **arguments)
    summary = digest.finish(process, out)
    return out, summary


def compare(a, b, menu=False):
    with open(os.devnull, "w") as quiet:
        return digest_compare.compare(a, b, out=quiet, menu=menu) if "menu" in digest_compare.compare.__code__.co_varnames else digest_compare.compare(a, b, out=quiet)


def logged(out, pattern):
    return [line for line in (out / "stderr.txt").read_text(errors="replace").splitlines() if re.search(pattern, line)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", default="HEAD~0")
    parser.add_argument("--ticks", type=int, default=300)
    parser.add_argument("--frames", type=int, default=200)
    parser.add_argument("--skip-off", action="store_true")
    parser.add_argument("--skip-on", action="store_true")
    parser.add_argument("out", type=Path)
    arguments = parser.parse_args()
    arguments.out.mkdir(parents=True, exist_ok=True)
    results = {}
    here = digest.build()
    if not arguments.skip_off:
        base = digest.build("base", rev=arguments.base)
        # (the menu: the main menu's frames matched by their order; the game: local Slayer with 3 bots, matched by tick)
        out_a, _ = run(base, "scr-base-menu", "menu", arguments.frames)
        out_b, _ = run(here, "scr-off-menu", "menu", arguments.frames)
        results["off-menu"] = digest_compare.compare(out_a, out_b, out=sys.stdout, menu=True)
        out_a, summary_a = run(base, "scr-base-bots", "local-slayer", arguments.ticks, {"HALO_BOTS": "3"})
        out_b, summary_b = run(here, "scr-off-bots", "local-slayer", arguments.ticks, {"HALO_BOTS": "3"})
        results["off-bots"] = digest_compare.compare(out_a, out_b, out=sys.stdout)
        results["off-bots-summary"] = [summary_a.get("game-digest"), summary_b.get("game-digest")]
        # negative control: with the screens on the pause menu gets a button and the lobby its tags, which the frames of a game show
        out_c, summary_c = run(here, "scr-on-bots", "local-slayer", arguments.ticks, {"HALO_BOTS": "3", "HALO_SCREENS": "1"})
        results["on-bots-differs-from-off"] = summary_c.get("game-digest") != summary_b.get("game-digest")
        results["pause-log"] = logged(out_c, r"pause menu has")
        out_d, _ = run(here, "scr-on-menu", "menu", arguments.frames, {"HALO_SCREENS": "1"})
        results["on-menu-main-differs-from-off"] = digest_compare.compare(out_b := (digest.DIGEST / "runs" / "scr-off-menu" / "out"), out_d, out=open(os.devnull, "w"), menu=True) != 0
    if not arguments.skip_on:
        for name, screen, buttons in (("players", PLAYERS, "wait,wait,down,down,down,a,down,down,b,up,up,up,up,b"), ("friends", FRIENDS, "wait,wait,right,right,a,wait,down,down,a,b,b"),
                                      ("add", ADD, "wait,wait,a,b"), ("bots", BOTS, "wait,wait,down,right,right,down,left")):
            out, _ = run(here, f"scr-on-{name}", "menu", arguments.frames, {"HALO_SCREENS": "1", "HALO_MENU_OPEN": screen, "HALO_TEST_INPUT": f"menu:{buttons}"})
            results[f"on-{name}"] = logged(out, r"screens:|menus: .*(error|not|problem|unresolved)|there is no widget")
    (arguments.out / "screens-digest.json").write_text(json.dumps(results, indent=1, default=str))
    print(json.dumps(results, indent=1, default=str))


if __name__ == "__main__":
    main()
