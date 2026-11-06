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
    tools/test_wiiu_game_digest.py run RUN [--name NAME] [--poison] [--ticks N] [--trace FIRST-LAST|all] [--detail TICK] [--random] [--level NAME [--init LINE]... [--cues "SECOND LINE|..."]] [--mode host|local|solo|solo-bloodgulch|solo-deathless|menu|menu-cycle|menu-campaign|menu-solo]
    tools/test_wiiu_game_digest.py compare RUN_A RUN_B [digest_compare.py's options]
    GAME_DIGEST=1 python3 -m unittest tools.test_wiiu_game_digest      (GAME_DIGEST_TICKS: the ticks each run plays, 300 by default)

The modes: host (b30 hosted as network co-op, no one joining: the ticks' reference runs), local (the same as a split screen game of one player), and, for the
tick overlap (port/linux/game/tick_overlap.h), which only a campaign game of one player on this machine can use: solo (b30 as the console's map_name starts it, from
init.txt; it opens with a cinematic of about 1,070 ticks), solo-bloodgulch (a multiplayer map as a game of one player, no cinematic) and solo-deathless (b30 with
cheat_deathless_player and cheat_infinite_ammo, so that the fight goes on). HALO_TICK_* in the environment, and GAME_DIGEST_ENV="NAME=VALUE,NAME=VALUE" for the others
(HALO_RENDER_REPEAT=3), go to the game. menu, menu-cycle and menu-campaign are the main menu and the way back to it (a game started from it, the main menu
again, and so on: network_test.c's solo: and its main_menu cue); their --ticks counts frames.

--level NAME plays a stock level (a10 ... d40, or a multiplayer map's name) as a game of one player, with --init's lines of init.txt before its map_name (cheats) and --cues "<second> <console
line>|..." run from the first tick (debug.network_test_cues; a console line has no outer parentheses). GAME_DIGEST_SCENE="GROUP:tag name:COUNT|..." makes objects in front of a fixed
camera (port/wiiu/tests/game_digest/game_digest_scene.c has its settings), GAME_DIGEST_PASSES=1 counts the draws of each render pass, GAME_DIGEST_KEYS=/out/keys.txt writes the pixel keys,
GAME_DIGEST_READS=/out/reads.txt the map's reads, GAME_DIGEST_DOCKER_ARGS more arguments of docker run (a sampling profiler's folder) and GAME_DIGEST_CONTAINER_PREFIX a name for the
containers (all through GAME_DIGEST_ENV or the environment). reports/research/2026-11-04/banshee.md used them.

A build named here (the default) is this checkout's; another is a copy of the sources in build/game-digest/trees/NAME, of this checkout's files or of
a commit's (--rev, git archive), with this checkout's game digest folder over it (a branch before it has none) and --mutate's changes made in it
(each BEFORE must be found once in PATH). The builds and runs are in Docker, in the opence-ppc-be image; the runs' folders are under
build/game-digest/runs, each with out/frames.txt (a line a frame: its tick, digest, multiset digest, counts and random seeds), summary.txt (the
game-digest of all the frames of the game), stderr.txt (the game's log) and, as asked, trace.txt, detail.txt and random.txt. The maps are
data-be's (GAME_DIGEST_MAPS, else the first data-be/maps above this checkout). A run of 1400 ticks took about 260 s alone and about 270 s four at
once (an M-series Mac's Docker, 2026-11-01), a full build about a minute; the first 125 frames are the menus and the loading, which are not compared
(their first frames still vary now and then: game_digest_glue.c has why that does not reach the game's).

Nothing here touches a console. The game's tests are skipped unless GAME_DIGEST is set (they need Docker, the image and the maps); the compare
tool's, on made-up runs, always run.
"""
import argparse
import os
from pathlib import Path
import shlex
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
HARNESS = ["port/wiiu/tests/game_digest/build.py", "port/wiiu/tests/game_digest/game_digest_glue.c", "port/wiiu/tests/game_digest/game_digest_padding.c",
           "port/wiiu/tests/game_digest/game_digest_scene.c"]
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
MODES.update({"menu": "", "menu-cycle": "solo:levels\\test\\bloodgulch\\bloodgulch", "menu-campaign": "solo:levels\\b30\\b30",
              "menu-solo": "solo:levels\\b30\\b30"})
MENU_CUES = {"menu-cycle": "20 main_menu", "menu-campaign": "50 main_menu"}
MENU_MODES = ("menu", "menu-cycle", "menu-campaign", "menu-solo")
# solo: a campaign game of one player on this machine, which init.txt starts as the console's map_name does (the tick overlap's, which a network game
# or a split screen game never is); network_test.c's "log" mode logs it as it logs the others
SOLO_INIT = {"solo": "map_name levels\\b30\\b30\n", "solo-bloodgulch": "map_name levels\\test\\bloodgulch\\bloodgulch\n",
             "solo-deathless": "cheat_deathless_player 1\ncheat_infinite_ammo 1\nmap_name levels\\b30\\b30\n"}
# (menu-campaign: the player deathless, so that no revert to a checkpoint starts the cues' clock again; init.txt runs at the start, before the main menu)
SOLO_INIT["menu-campaign"] = "cheat_deathless_player 1\ncheat_infinite_ammo 1\n"
# menu-solo: the console's start (debug.network_test "solo:", config.toml only) with nothing in init.txt, and no cue to go back to the menu: what the cheats and the
# scripted player do is the cues' (GAME_DIGEST_ENV="HALO_NETWORK_TEST_CUES=0 cheat_deathless_player 1|0 cheat_infinite_ammo 1")
SOLO_INIT["menu-solo"] = ""
# the campaign's ten levels as games of one player (the "levels" regression: the console's campaign sweep, tickbot, deathless): solo-a10 ... solo-d40, each the level
# as map_name starts it, with the player deathless and the ammunition infinite. What matters is the level's first frames (the player's weapon is made in the first
# ticks, the first overlapped frame is drawn from a ring that has just been made); about 400 ticks are enough (tools/test_wiiu_game_digest_levels.py runs them all)
LEVELS = ("a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40")
for _level in LEVELS:
    MODES["solo-" + _level] = "log"
    SOLO_INIT["solo-" + _level] = "cheat_deathless_player 1\ncheat_infinite_ammo 1\nmap_name levels\\%s\\%s\n" % (_level, _level)
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


def level_path(level: str) -> str:
    """the scenario path of a stock level by its map's name: the campaign's levels\\a10\\a10, the multiplayer maps' levels\\test\\bloodgulch\\bloodgulch"""
    campaign = {"a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40"}
    return f"levels\\{level}\\{level}" if level in campaign else f"levels\\test\\{level}\\{level}"


def start(program: Path, run: str, ticks: int, trace: str = None, detail: str = None, random: bool = False, mode: str = "host", level: str = None,
          init: tuple = (), cues: str = None, extra: dict = None):
    """a run of the program to the frame of the game at tick ticks, in build/game-digest/runs/RUN, started; (its process, its folder)"""
    folder = DIGEST / "runs" / run
    shutil.rmtree(folder, ignore_errors=True)
    (folder / "data").mkdir(parents=True)
    (folder / "out").mkdir()
    (folder / "data" / "empty-brokers.txt").write_text("")
    if level:
        # (a stock level as a game of one player, with the lines of init.txt before its map_name: cheats, a game variant)
        mode = "solo"
        (folder / "data" / "init.txt").write_text("".join(line + "\n" for line in init) + f"map_name {level_path(level)}\n")
    elif mode in SOLO_INIT:
        (folder / "data" / "init.txt").write_text(SOLO_INIT[mode])
    environment = dict(GAME_ENVIRONMENT, HALO_NETWORK_TEST=MODES[mode], GAME_DIGEST_DIR="/out", GAME_DIGEST_TICKS=str(ticks),
                       GAME_DIGEST_TRACE={None: "0", "all": "1"}.get(trace, trace), GAME_DIGEST_DETAIL=detail or "",
                       GAME_DIGEST_RANDOM="1" if random else "0")
    if mode in MENU_MODES:
        environment.update(GAME_DIGEST_TICKS="0", GAME_DIGEST_FRAMES=str(ticks), HALO_NETWORK_TEST_CUES=MENU_CUES.get(mode, ""))
    if cues:
        # (debug.network_test_cues: "<second> <console line>|...", from the first tick the player has a unit)
        environment["HALO_NETWORK_TEST_CUES"] = cues
    # (the tick owners' settings of the environment, to compare the switch on and off: HALO_TICK_*)
    environment.update({key: value for key, value in os.environ.items() if key.startswith("HALO_TICK_")})
    # (and any others, "NAME=VALUE,NAME=VALUE": the render probe's HALO_RENDER_REPEAT=3)
    for setting in filter(None, os.environ.get("GAME_DIGEST_ENV", "").split(",")):
        key, _, value = setting.partition("=")
        environment[key] = value
    # (a run's own settings, over the rest: PLAIN is the build with none of the smooth defaults, whatever the map)
    environment.update(extra or {})
    # (GAME_DIGEST_CONTAINER_PREFIX: the container is named PREFIX+RUN, so that a session's own runs can be told from others)
    named = ["--name", os.environ["GAME_DIGEST_CONTAINER_PREFIX"] + run] if os.environ.get("GAME_DIGEST_CONTAINER_PREFIX") else []
    command = ["docker", "run", "--rm", *named, "--network", "none", "--ulimit", "core=0", "-v", f"{program}:/program:ro", "-v", f"{folder / 'data'}:/data",
               "-v", f"{folder / 'out'}:/out", "-v", f"{maps_folder()}:/data/maps:ro", "-w", "/data"]
    for key, value in environment.items():
        command += ["-e", f"{key}={value}"]
    # (GAME_DIGEST_DOCKER_ARGS: more arguments of docker run, such as -v for a sampling profiler's folder)
    command += shlex.split(os.environ.get("GAME_DIGEST_DOCKER_ARGS", ""))
    command += [IMAGE, "timeout", "-s", "KILL", str(600 + ticks), "qemu-ppc", "-cpu", "750", "-L", "/usr/powerpc-linux-gnu", "/program/halo",
                "--config", "/data/config.toml"]
    stderr = open(folder / "out" / "stderr.txt", "w")
    process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=stderr)
    stderr.close()
    return process, folder / "out"


# (the smooth defaults, port/linux/game/smooth_defaults.h, are on for a one-player map with no card file: a run of the loop without them says each card file 0)
PLAIN = {"HALO_TICK_THREAD": "0", "HALO_TICK_OVERLAP": "0", "HALO_TICK_SNAPSHOT": "0", "HALO_GOVERNOR": "off"}
EXPLICIT = {"HALO_TICK_THREAD": "1", "HALO_TICK_OVERLAP": "1", "HALO_TICK_SNAPSHOT": "1"}


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


def tick_lines(out: Path):
    """the game's tick lines of a run ("network test: tick N ..."), the camera and the network counts left out, in order"""
    import re

    lines = []
    for line in (out / "stderr.txt").read_text(errors="replace").splitlines():
        found = re.search(r"network test: tick (\d+) (.*)", line)
        if found:
            rest = re.sub(r"camera \([^)]*\)", "camera", found.group(2))
            lines.append((int(found.group(1)), re.sub(r"sent \d+ received \d+", "sent _ received _", rest)))
    return lines


@unittest.skipUnless(os.environ.get("GAME_DIGEST_LEVELS"), "the campaign's ten levels, each as a game of one player, the plain loop and the tick overlap: GAME_DIGEST_LEVELS=1 (and Docker's opence-ppc-be image, data-be's maps)")
class LevelsTest(unittest.TestCase):
    """The regression of the campaign's ten levels (the console's sweep, solo:levels\\X\\X, tickbot, deathless): each level as a game of one player, the first
    GAME_DIGEST_LEVELS_TICKS ticks (400), with the tick overlap's switches on (the ticks on a thread of their own, the snapshot, the overlap) and with none. The pass
    condition: no run halts, and the game's ticks are the same in both. The first frames of a level are what matters: the player's weapon is made in them, the first
    overlapped frame is drawn from a ring that has just been made, and a campaign script can switch the structure bsp in the first tick (a30 and d20 did, and the
    drawing met a bsp let go of: the console halted). GAME_DIGEST_LEVELS_ONLY=a30,d20 runs those only; GAME_DIGEST_LEVELS_NAME is the build's name."""

    # The variants of the frames' cadence: "fixed" (debug.fixed_step: a frame a tick, as every host run), "cadence" (frames of 16.667 ms of game time each, HALO_FIXED_STEP=0:
    # the console's 60 frames a second, two a tick, one with no tick in it), "stall" (the same, and the game thread held back 50 ms after giving the job in each of the first 5
    # overlapped frames, so that the tick thread runs ahead of the picture: the console's first frames of a level, which fixed_step never has). GAME_DIGEST_LEVELS_VARIANTS=fixed,cadence,stall
    VARIANTS = {"fixed": {}, "cadence": {"HALO_TICK_FRAME_MS": "16.667", "GAME_DIGEST_ENV": "HALO_FIXED_STEP=0"},
                "stall": {"HALO_TICK_FRAME_MS": "16.667", "GAME_DIGEST_ENV": "HALO_FIXED_STEP=0", "HALO_TICK_OVERLAP_FRAME_STALL": "50"}}

    def run_with(self, program, name, ticks, level, environment, extra=None):
        saved = {key: os.environ.get(key) for key in environment}
        os.environ.update(environment)
        try:
            return start(program, name, ticks, mode="solo-" + level, extra=extra)
        finally:
            for key, value in saved.items():
                if value is None:
                    os.environ.pop(key, None)
                else:
                    os.environ[key] = value

    def test_every_level_runs_overlapped_without_a_halt_and_with_the_plain_loops_ticks(self):
        program = build(os.environ.get("GAME_DIGEST_LEVELS_NAME", "here"))
        default_ticks = int(os.environ.get("GAME_DIGEST_LEVELS_TICKS", "400"))
        only = [name for name in os.environ.get("GAME_DIGEST_LEVELS_ONLY", "").split(",") if name] or list(LEVELS)
        # the card: "default" (no card file: the smooth defaults decide, as a player with no card file gets them), or "explicit" (the three tick files at 1, as the owner's card has them)
        card = os.environ.get("GAME_DIGEST_LEVELS_VARIANT", "default")
        # the cadence: "fixed" (a frame a tick), "cadence" (the console's frames of 16.667 ms) and "stall" (those, with the game thread held back)
        variants = [name for name in os.environ.get("GAME_DIGEST_LEVELS_VARIANTS", "fixed,cadence,stall").split(",") if name]
        for variant in variants:
            for level in only:
                # (b30 opens with a cinematic of about 1,070 ticks, serial frames: its first overlapped frames are past it)
                ticks = max(default_ticks, 1150) if level == "b30" else default_ticks
                with self.subTest(level=level, variant=variant):
                    plain_environment = {key: value for key, value in self.VARIANTS[variant].items() if key != "HALO_TICK_OVERLAP_FRAME_STALL"}
                    plain = self.run_with(program, f"levels-{level}-{variant}-plain", ticks, level, plain_environment, PLAIN)
                    overlapped = self.run_with(program, f"levels-{level}-{variant}-{card}", ticks, level, self.VARIANTS[variant], EXPLICIT if card == "explicit" else None)
                    finish(*plain)
                    finish(*overlapped)
                    a, b = tick_lines(plain[1]), tick_lines(overlapped[1])
                    self.assertGreater(len(a), ticks // 2, f"{level}: the plain loop played {len(a)} ticks")
                    count = min(len(a), len(b))
                    self.assertEqual(a[:count], b[:count], f"{level} ({variant}): the overlapped game's ticks differ from the plain loop's")
                    log = (overlapped[1] / "stderr.txt").read_text(errors="replace")
                    self.assertIn("smooth defaults: ON for this map", log, f"{level} ({variant}): the map was not decided to be one for the smooth defaults")
                    self.assertIn("tick overlap: on for this map", log, f"{level} ({variant}): the overlap was not started")
                    self.assertIn("overlapped frames from here", log, f"{level} ({variant}): no frame was overlapped")


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
    run_parser.add_argument("--mode", choices=sorted(MODES), default="host")
    run_parser.add_argument("--level", help="play this stock level (a10 ... d40, bloodgulch ...) as a game of one player (the solo mode's, with --init's lines)")
    run_parser.add_argument("--init", action="append", default=[], help="a line of init.txt before the map_name (--level), such as cheat_deathless_player 1")
    run_parser.add_argument("--cues", help="debug.network_test_cues: <second> <console line>, joined with |")
    compare_parser = commands.add_parser("compare", help="tools/wiiu/digest_compare.py on two runs (names under build/game-digest/runs, or folders)")
    compare_parser.add_argument("arguments", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.command == "build":
        print(build(args.name, args.rev, [tuple(m) for m in args.mutate], args.poison) / "halo")
        return 0
    if args.command == "run":
        program = (ROOT if args.name == "here" else DIGEST / "trees" / args.name) / "build" / ("game-digest-poison" if args.poison else "game-digest")
        summary = finish(*start(program, args.run, args.ticks, args.trace, args.detail, args.random, args.mode, args.level, tuple(args.init), args.cues))
        print(" ".join(f"{name} {value}" for name, value in summary.items()))
        return 0
    arguments = [str(DIGEST / "runs" / a / "out") if not a.startswith("-") and not Path(a).exists() and (DIGEST / "runs" / a).is_dir() else a
                 for a in args.arguments]
    return digest_compare.main(arguments)


if __name__ == "__main__":
    sys.exit(main())
