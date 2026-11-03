"""The real game drawn through the Wii U's Direct3D layer on Linux, digested frame by frame: the parity oracle for the native renderer's packages
(reports/research/2026-11-01/native-gx2-renderer.md, N6).

port/wiiu/tests/game_digest/build.py builds the game for 32-bit big-endian PowerPC Linux, as the host runs of the ticks do, with the game's sources
compiled with HALO_WIIU and the Wii U's Direct3D layer (d3d8_gx2.c and its units) in place of the Linux one; the D3D8 replay's GX2-thread reader
(port/wiiu/tests/d3d8_replay/replay_gx2.c) turns every draw into a line of what the GPU is given, and the glue (game_digest_glue.c) digests each frame.
Under qemu-ppc -cpu 750, with the host runs' settings (b30, co-op hosted with no one joining, the scripted player tickbot:7, random seed 12345, a
tick a frame), the game plays the same ticks each run, and the glue ties to the frames what it does against the machine's time, so two runs of one
build draw every frame of the game the same. A change to the rendering that keeps the pictures keeps every frame's digest; one that does not shows
the first tick it changes (tools/wiiu/digest_compare.py, which also compares the traces' lines, and in any order within a frame or a span).

    tools/test_wiiu_game_digest.py build [--name NAME] [--rev REV] [--mutate PATH BEFORE AFTER]... [--poison]
    tools/test_wiiu_game_digest.py run RUN [--name NAME] [--poison] [--ticks N] [--trace FIRST-LAST|all] [--detail TICK] [--random] [--keys] [--custom-map NAME] [--mode host|local|solo|solo-bloodgulch|solo-deathless|menu|menu-cycle|menu-campaign]
    tools/test_wiiu_game_digest.py compare RUN_A RUN_B [digest_compare.py's options]
    GAME_DIGEST=1 python3 -m unittest tools.test_wiiu_game_digest      (GAME_DIGEST_TICKS: the ticks each run plays, 300 by default)

The modes: host (b30 hosted as network co-op, no one joining: the ticks' reference runs), local (the same as a split screen game of one player), and, for the
tick overlap (port/linux/game/tick_overlap.h), which only a campaign game of one player on this machine can use: solo (b30 as the console's map_name starts it, from
init.txt; it opens with a cinematic of about 1,070 ticks), solo-bloodgulch (a multiplayer map as a game of one player, no cinematic) and solo-deathless (b30 with
cheat_deathless_player and cheat_infinite_ammo, so that the fight goes on). HALO_TICK_* in the environment, and GAME_DIGEST_ENV="NAME=VALUE,NAME=VALUE" for the others
(HALO_RENDER_REPEAT=3), go to the game. menu, menu-cycle and menu-campaign are the main menu and the way back to it (a game started from it, the main menu
again, and so on: network_test.c's solo: and its main_menu cue); their --ticks counts frames.

A build named here (the default) is this checkout's; another is a copy of the sources in build/game-digest/trees/NAME, of this checkout's files or of
a commit's (--rev, git archive), with this checkout's game digest folder over it (a branch before it has none) and --mutate's changes made in it
(each BEFORE must be found once in PATH). The builds and runs are in Docker, in the opence-ppc-be image; the runs' folders are under
build/game-digest/runs, each with out/frames.txt (a line a frame: its tick, digest, multiset digest, counts and random seeds), summary.txt (the
game-digest of all the frames of the game), stderr.txt (the game's log) and, as asked, trace.txt, detail.txt and random.txt. The maps are
data-be's (GAME_DIGEST_MAPS, else the first data-be/maps above this checkout). A run of 1400 ticks took about 260 s alone and about 270 s four at
once (an M-series Mac's Docker, 2026-11-01), a full build about a minute; the first 125 frames are the menus and the loading, which are not compared
(their first frames still vary now and then: game_digest_glue.c has why that does not reach the game's).

--keys writes the shader keys the run's draws need (the pixel and vertex keys the console's GX2 thread looks up in shaders.pack, in the lines the missing-shader
capture writes on the card) to RUN/out/keys.txt, for tools/wiiu/key_diff.py to compare with tools/wiiu/shader_keys.txt: which shaders a map needs that the pack
lacks, before the map is played on a console. --custom-map NAME plays a Halo Custom Edition map as a game of one player (slayer, as the console's
`game_variant slayer`, `map_name custom_maps\\NAME` do): the folder of the maps, with bitmaps.map, sounds.map and loc.map, is GAME_DIGEST_CUSTOM_MAPS (it
is mounted read-only as /data/custom_maps, and nothing is copied); the game's settings are the defaults, as the Mac's runs have them, or those of
GAME_DIGEST_CONFIG, a copy of a card's config.toml (display.high_res_hud, per_pixel_lighting and the like change the keys).

Nothing here touches a console. The game's tests are skipped unless GAME_DIGEST is set (they need Docker, the image and the maps); the compare
tool's, on made-up runs, always run.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "wiiu"))
import digest_compare  # noqa: E402

IMAGE = "opence-ppc-be"
DIGEST = ROOT / "build" / "game-digest"
HARNESS = ["port/wiiu/tests/game_digest/build.py", "port/wiiu/tests/game_digest/game_digest_glue.c", "port/wiiu/tests/game_digest/game_digest_padding.c"]
# the host runs' settings (runs/host-mac-perf-20261031/ticks/baseline-ppc-command.json), without the null renderer and debug.exit_after: the
# layer draws, and the glue ends the run at its tick
GAME_ENVIRONMENT = {
    "HALO_DATA_ROOT": "/data", "HALO_SAVE_ROOT": "/data", "HALO_NETWORK_TEST_START": "2", "HALO_NETWORK_TEST_LOG": "1",
    "HALO_TEST_INPUT": "tickbot:7", "HALO_RANDOM_SEED": "12345", "HALO_FIXED_STEP": "1", "HALO_NET_ADDRESS": "127.0.0.1",
    "HALO_NET_BROADCAST": "127.0.0.1", "HALO_NET_BROKERS_FILE": "/data/empty-brokers.txt", "HALO_NETWORK_TEST_PUBLIC": "0", "HALO_NO_AUDIO": "1",
    "HALO_HIDDEN_WINDOW": "1", "SDL_VIDEO_DRIVER": "offscreen", "SDL_AUDIO_DRIVER": "dummy",
}
# host: b30 hosted as network co-op, no one joining (the host runs'); local: the same as a local game of one player
MODES = {"host": "host:levels\\b30\\b30:coop", "local": "local:levels\\b30\\b30:coop", "solo": "log", "solo-bloodgulch": "log", "solo-deathless": "log"}
# the main menu and back: menu (the main menu alone, no network test), menu-cycle (from the main menu a game of one player on bloodgulch, the main menu
# again 20 s in, as the pause menu's Quit goes, and so on: network_test.c's "solo:"), menu-campaign (the same with b30, 50 s in, past its opening
# cinematic). Their --ticks is a count of frames (the menu has no game ticks), and their frames are not compared
MODES.update({"menu": "", "menu-cycle": "solo:levels\\test\\bloodgulch\\bloodgulch", "menu-campaign": "solo:levels\\b30\\b30"})
MENU_CUES = {"menu-cycle": "20 main_menu", "menu-campaign": "50 main_menu"}
MENU_MODES = ("menu", "menu-cycle", "menu-campaign")
# solo: a campaign game of one player on this machine, which init.txt starts as the console's map_name does (the tick overlap's, which a network game
# or a split screen game never is); network_test.c's "log" mode logs it as it logs the others
SOLO_INIT = {"solo": "map_name levels\\b30\\b30\n", "solo-bloodgulch": "map_name levels\\test\\bloodgulch\\bloodgulch\n",
             "solo-deathless": "cheat_deathless_player 1\ncheat_infinite_ammo 1\nmap_name levels\\b30\\b30\n"}
# (menu-campaign: the player deathless, so that no revert to a checkpoint starts the cues' clock again; init.txt runs at the start, before the main menu)
SOLO_INIT["menu-campaign"] = "cheat_deathless_player 1\ncheat_infinite_ammo 1\n"
# a broken key: the told draw facts compared by whether any were told, not by what they are (the D3D8 replay's own control of the same), so a
# draw whose textures' facts change is drawn with the last ones sent
BROKEN_KEY = ("port/wiiu/src/d3d8_gx2.c", "|| !facts_known) && (!facts_known || !words_equal(&facts_now, &facts_told, sizeof(facts_now))))",
              "|| !facts_known) && !facts_known)")


def run_command(command, **arguments):
    result = subprocess.run(command, capture_output=True, text=True, **arguments)
    if result.returncode:
        raise AssertionError(" ".join(map(str, command)) + "\n" + result.stdout[-4000:] + result.stderr[-4000:])
    return result


def maps_folder() -> Path:
    if os.environ.get("GAME_DIGEST_MAPS"):
        return Path(os.environ["GAME_DIGEST_MAPS"])
    for folder in ROOT.parents:
        if (folder / "data-be" / "maps").is_dir():
            return folder / "data-be" / "maps"
    raise SystemExit("no data-be/maps above this checkout: set GAME_DIGEST_MAPS (tools/ppc-be/convert_maps.sh makes them)")


def sync_file(source_text: bytes, target: Path) -> None:
    """target made to hold source_text, left as it is (and its time) if it does already, so the build goes on from what it built"""
    if target.is_file() and target.read_bytes() == source_text:
        return
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(source_text)


def tree(name: str, rev: str = None, mutations=()) -> Path:
    """the sources a build is made from: this checkout (here) or build/game-digest/trees/NAME (this checkout's files, or REV's), with this
    checkout's game digest harness, and the mutations (path, before, after) made"""
    if name == "here" and not rev and not mutations:
        return ROOT
    folder = DIGEST / "trees" / name
    files = {}
    if rev:
        archive = subprocess.Popen(["git", "-C", str(ROOT), "archive", "--format=tar", rev], stdout=subprocess.PIPE)
        with tarfile.open(fileobj=archive.stdout, mode="r|") as tar:
            for member in tar:
                if member.isfile():
                    files[member.name] = tar.extractfile(member).read()
        if archive.wait():
            raise SystemExit(f"git archive {rev} failed")
    else:
        listed = run_command(["git", "-C", str(ROOT), "ls-files", "-z", "--cached", "--others", "--exclude-standard"]).stdout
        for name_ in filter(None, listed.split("\0")):
            if (ROOT / name_).is_file():
                files[name_] = (ROOT / name_).read_bytes()
    for name_ in HARNESS:
        files[name_] = (ROOT / name_).read_bytes()
    for path, before, after in mutations:
        text = files[path].decode("latin-1")
        if text.count(before) != 1:
            raise LookupError(f"the mutation no longer applies to {path} (found {text.count(before)} times): {before!r}")
        files[path] = text.replace(before, after, 1).encode("latin-1")
    for name_, data in files.items():
        sync_file(data, folder / name_)
    return folder


def build(name: str = "here", rev: str = None, mutations=(), poison: bool = False) -> Path:
    """the game digest's program for the sources (tree), built in Docker (with poison, build.py --poison's, in build/game-digest-poison); its
    folder"""
    sources = tree(name, rev, mutations)
    folder = "build/game-digest-poison" if poison else "build/game-digest"
    run_command(["docker", "run", "--rm", "-v", f"{sources}:/src", "-w", "/src", IMAGE, "python3", "port/wiiu/tests/game_digest/build.py",
                 "--build-dir", folder, *(["--poison"] if poison else [])], timeout=3600)
    program = sources / folder
    if not (program / "halo").is_file():
        raise AssertionError(f"no program in {program}")
    return program


def start(program: Path, run: str, ticks: int, trace: str = None, detail: str = None, random: bool = False, mode: str = "host", keys: bool = False,
          custom_map: str = None, variant: str = "slayer"):
    """a run of the program to the frame of the game at tick ticks, in build/game-digest/runs/RUN, started; (its process, its folder)"""
    folder = DIGEST / "runs" / run
    shutil.rmtree(folder, ignore_errors=True)
    (folder / "data").mkdir(parents=True)
    (folder / "out").mkdir()
    (folder / "data" / "empty-brokers.txt").write_text("")
    if os.environ.get("GAME_DIGEST_CONFIG"):
        # (the settings of a console's card, for the keys it would need: a copy of its config.toml)
        shutil.copyfile(os.environ["GAME_DIGEST_CONFIG"], folder / "data" / "config.toml")
    if custom_map:
        # (a Custom Edition map as a game of one player: init.txt, as the console's map_name starts it; a multiplayer scenario needs a game variant)
        mode = "solo"
        (folder / "data" / "init.txt").write_text((f"game_variant {variant}\n" if variant else "") + f"map_name custom_maps\\{custom_map}\n")
    elif mode in SOLO_INIT:
        (folder / "data" / "init.txt").write_text(SOLO_INIT[mode])
    environment = dict(GAME_ENVIRONMENT, HALO_NETWORK_TEST=MODES[mode], GAME_DIGEST_DIR="/out", GAME_DIGEST_TICKS=str(ticks),
                       GAME_DIGEST_TRACE={None: "0", "all": "1"}.get(trace, trace), GAME_DIGEST_DETAIL=detail or "",
                       GAME_DIGEST_RANDOM="1" if random else "0")
    if mode in MENU_MODES:
        environment.update(GAME_DIGEST_TICKS="0", GAME_DIGEST_FRAMES=str(ticks), HALO_NETWORK_TEST_CUES=MENU_CUES.get(mode, ""))
    if keys:
        environment["GAME_DIGEST_KEYS"] = "/out/keys.txt"
    # (the tick owners' settings of the environment, to compare the switch on and off: HALO_TICK_*)
    environment.update({key: value for key, value in os.environ.items() if key.startswith("HALO_TICK_")})
    # (and any others, "NAME=VALUE,NAME=VALUE": the render probe's HALO_RENDER_REPEAT=3)
    for setting in filter(None, os.environ.get("GAME_DIGEST_ENV", "").split(",")):
        key, _, value = setting.partition("=")
        environment[key] = value
    command = ["docker", "run", "--rm", "--network", "none", "--ulimit", "core=0", "-v", f"{program}:/program:ro", "-v", f"{folder / 'data'}:/data",
               "-v", f"{folder / 'out'}:/out", "-v", f"{maps_folder()}:/data/maps:ro", "-w", "/data"]
    if custom_map:
        if not os.environ.get("GAME_DIGEST_CUSTOM_MAPS"):
            raise SystemExit("--custom-map needs GAME_DIGEST_CUSTOM_MAPS: the folder of the Custom Edition maps and bitmaps.map, sounds.map, loc.map")
        command += ["-v", f"{os.environ['GAME_DIGEST_CUSTOM_MAPS']}:/data/custom_maps:ro"]
    for key, value in environment.items():
        command += ["-e", f"{key}={value}"]
    # (a Custom Edition map's tag cache is at 0x40440000, where qemu puts the game's 8 MB stack and its libraries: a smaller stack, as the host runs of
    # the Custom Edition maps have it, tools/stagekit)
    emulator = ["qemu-ppc", "-cpu", "750", "-L", "/usr/powerpc-linux-gnu"] + (["-s", "0x380000"] if custom_map else [])
    command += [IMAGE, "timeout", "-s", "KILL", str(600 + ticks), *emulator, "/program/halo", "--config", "/data/config.toml"]
    stderr = open(folder / "out" / "stderr.txt", "w")
    process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=stderr)
    stderr.close()
    return process, folder / "out"


def finish(process, out: Path) -> dict:
    """the run's end: its summary (summary.txt's names and values). Its data folder (the maps' copy to the cache partition, about 300 MB) is
    removed unless GAME_DIGEST_KEEP_DATA is set or the run failed"""
    if process.wait():
        raise AssertionError(f"the run in {out} ended with {process.returncode}:\n" + (out / "stderr.txt").read_text(errors="replace")[-3000:])
    if not os.environ.get("GAME_DIGEST_KEEP_DATA"):
        shutil.rmtree(out.parent / "data", ignore_errors=True)
    summary = {}
    for line in (out / "summary.txt").read_text().splitlines():
        name, _, value = line.partition(" ")
        summary[name] = value
    return summary


def ticks_wanted() -> int:
    return int(os.environ.get("GAME_DIGEST_TICKS", "300"))


@unittest.skipUnless(os.environ.get("GAME_DIGEST"), "needs Docker's opence-ppc-be image and data-be's maps: GAME_DIGEST=1")
class GameDigestTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.program = build()
        cls.ticks = ticks_wanted()
        runs = [start(cls.program, f"test-{name}", cls.ticks) for name in ("a", "b")]
        cls.summaries = [finish(*run) for run in runs]
        cls.outs = [out for _, out in runs]

    def test_two_runs_draw_every_frame_of_the_game_the_same(self):
        with open(os.devnull, "w") as quiet:
            self.assertEqual(digest_compare.compare(self.outs[0], self.outs[1], out=quiet), 0)
        self.assertEqual(self.summaries[0]["game-frames"], str(self.ticks))
        self.assertEqual(self.summaries[0]["game-digest"], self.summaries[1]["game-digest"])
        # (and the game drew: b30 from its first tick)
        self.assertGreater(int(self.summaries[0]["draws"]), 100 * self.ticks)

    def test_a_build_laid_out_otherwise_with_its_stack_poisoned_draws_the_same(self):
        # (build.py --poison: the program's data and heap moved and the stack below each frame's rendering filled with 0xa5: what the game
        # hands the GPU and never set, which a code change elsewhere would move, shows as a frame that differs; game_digest_glue.c)
        process, out = start(build(poison=True), "test-poison", self.ticks)
        summary = finish(process, out)
        with open(os.devnull, "w") as quiet:
            self.assertEqual(digest_compare.compare(self.outs[0], out, out=quiet), 0)
        self.assertEqual(summary["game-digest"], self.summaries[0]["game-digest"])

    def test_a_broken_key_changes_the_frames(self):
        program = build("broken-key", mutations=[BROKEN_KEY])
        process, out = start(program, "test-broken-key", self.ticks)
        summary = finish(process, out)
        with open(os.devnull, "w") as quiet:
            self.assertEqual(digest_compare.compare(self.outs[0], out, out=quiet), 1)
        self.assertNotEqual(summary["game-digest"], self.summaries[0]["game-digest"])


class DigestCompareTest(unittest.TestCase):
    """tools/wiiu/digest_compare.py on made-up runs (no Docker): frames matched by tick, the menus left out, the orders it may leave out"""

    FRAME = ["clear {f}.0: flags 3f", "draw {f}.1: prim 5 sky", "marker {f}.1: decals", "draw {f}.2: prim 5 decal a", "draw {f}.3: prim 5 decal b",
             "marker {f}.3: hud", "draw {f}.4: prim 6 hud", "present {f}: back"]

    def write_run(self, folder: Path, menus: int, frames: dict) -> Path:
        """a run folder with menus menu frames, then the frames of the game (tick: lines with {f} for the frame's number), traced"""
        import hashlib

        folder.mkdir(parents=True)
        lines_out, trace = [], []
        number = 0
        for tick, lines in [(-1, ["draw {f}.1: menu", "present {f}: back"])] * menus + sorted(frames.items()):
            text = [line.format(f=number) for line in lines]
            digest = hashlib.sha1("\n".join(digest_compare.normalize(line) for line in text).encode()).hexdigest()[:16]
            multiset = hashlib.sha1("\n".join(sorted(digest_compare.normalize(line, True) for line in text)).encode()).hexdigest()[:16]
            lines_out.append(f"frame {number} tick {tick} digest {digest} multiset {multiset} lines {len(text)} draws {len(text) - 1}")
            trace += text + [f"# frame {number} tick {tick}"]
            number += 1
        (folder / "frames.txt").write_text("\n".join(lines_out) + "\n")
        (folder / "trace.txt").write_text("\n".join(trace) + "\n")
        return folder

    def compare(self, a, b, **options):
        with open(os.devnull, "w") as quiet:
            return digest_compare.compare(a, b, out=quiet, **options)

    def test_frames_are_matched_by_tick_whatever_the_menus_took(self):
        import tempfile

        with tempfile.TemporaryDirectory() as folder:
            a = self.write_run(Path(folder) / "a", 3, {1: self.FRAME, 2: self.FRAME})
            b = self.write_run(Path(folder) / "b", 5, {1: self.FRAME, 2: self.FRAME})
            self.assertEqual(self.compare(a, b), 0)
            self.assertEqual(self.compare(a, b, traces=True), 0)
            changed = self.FRAME[:3] + ["draw {f}.2: prim 5 decal A"] + self.FRAME[4:]
            c = self.write_run(Path(folder) / "c", 3, {1: self.FRAME, 2: changed})
            self.assertEqual(self.compare(a, c), 1)
            self.assertEqual(self.compare(a, c, ticks=(1, 1)), 0)
            self.assertEqual(self.compare(a, c, traces=True, how="unordered"), 1)

    def test_an_order_left_out_where_it_may_change(self):
        import re
        import tempfile

        swapped = self.FRAME[:3] + ["draw {f}.2: prim 5 decal b", "draw {f}.3: prim 5 decal a"] + self.FRAME[5:]
        moved = [self.FRAME[0], "draw {f}.1: prim 6 hud"] + self.FRAME[2:6] + ["draw {f}.4: prim 5 sky", self.FRAME[7]]
        with tempfile.TemporaryDirectory() as folder:
            a = self.write_run(Path(folder) / "a", 0, {1: self.FRAME})
            b = self.write_run(Path(folder) / "b", 0, {1: swapped})
            c = self.write_run(Path(folder) / "c", 0, {1: moved})
            self.assertEqual(self.compare(a, b), 1)
            self.assertEqual(self.compare(a, b, multiset=True), 0)
            self.assertEqual(self.compare(a, b, traces=True, how="unordered"), 0)
            self.assertEqual(self.compare(a, b, traces=True, how="markers", pattern=re.compile("decals")), 0)
            self.assertEqual(self.compare(a, b, traces=True, how="markers", pattern=re.compile("hud")), 1)
            self.assertEqual(self.compare(a, b, traces=True, how="runs", pattern=re.compile("decal")), 0)
            # (a draw moved out of its span is a change even where the order in a span is not)
            self.assertEqual(self.compare(a, c, traces=True, how="markers", pattern=re.compile(".")), 1)
            self.assertEqual(self.compare(a, c, traces=True, how="unordered"), 0)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], formatter_class=argparse.RawDescriptionHelpFormatter,
                                     epilog=__doc__.split("\n\n", 1)[1])
    commands = parser.add_subparsers(dest="command", required=True)
    build_parser = commands.add_parser("build", help="build the game digest's program")
    build_parser.add_argument("--name", default="here")
    build_parser.add_argument("--rev", help="a commit's sources (git archive), with this checkout's harness")
    build_parser.add_argument("--mutate", nargs=3, action="append", default=[], metavar=("PATH", "BEFORE", "AFTER"))
    build_parser.add_argument("--poison", action="store_true", help="build.py --poison's program (the stack filled, the data moved)")
    run_parser = commands.add_parser("run", help="run a build to a tick")
    run_parser.add_argument("run")
    run_parser.add_argument("--name", default="here", help="the build's name (build)")
    run_parser.add_argument("--poison", action="store_true", help="the build's --poison program")
    run_parser.add_argument("--ticks", type=int, default=1400)
    run_parser.add_argument("--trace", help="FIRST-LAST: the lines of the frames of these ticks to trace.txt; all: every frame's")
    run_parser.add_argument("--detail", help="TICK, fFRAME or fFIRST-LAST: those frames' constants and formats to detail.txt")
    run_parser.add_argument("--random", action="store_true", help="each frame's callers of the local random numbers to random.txt")
    run_parser.add_argument("--keys", action="store_true", help="the shader keys the draws need, to RUN/out/keys.txt (key_diff.py compares them with the pack's)")
    run_parser.add_argument("--custom-map", help="play this Custom Edition map (GAME_DIGEST_CUSTOM_MAPS's folder) as a game of one player")
    run_parser.add_argument("--variant", default="slayer", help="the game variant a multiplayer Custom Edition map is played with (--custom-map); empty for a campaign map")
    run_parser.add_argument("--mode", choices=sorted(MODES), default="host")
    compare_parser = commands.add_parser("compare", help="tools/wiiu/digest_compare.py on two runs (names under build/game-digest/runs, or folders)")
    compare_parser.add_argument("arguments", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.command == "build":
        print(build(args.name, args.rev, [tuple(m) for m in args.mutate], args.poison) / "halo")
        return 0
    if args.command == "run":
        program = (ROOT if args.name == "here" else DIGEST / "trees" / args.name) / "build" / ("game-digest-poison" if args.poison else "game-digest")
        summary = finish(*start(program, args.run, args.ticks, args.trace, args.detail, args.random, args.mode, args.keys, args.custom_map, args.variant))
        print(" ".join(f"{name} {value}" for name, value in summary.items()))
        return 0
    arguments = [str(DIGEST / "runs" / a / "out") if not a.startswith("-") and not Path(a).exists() and (DIGEST / "runs" / a).is_dir() else a
                 for a in args.arguments]
    return digest_compare.main(arguments)


if __name__ == "__main__":
    sys.exit(main())
