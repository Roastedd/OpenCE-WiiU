"""The real game drawn through the Wii U's Direct3D layer on Linux, digested frame by frame: the parity oracle for the native renderer's packages
(reports/research/2026-11-01/native-gx2-renderer.md, N6).

port/wiiu/tests/game_digest/build.py builds the game for 32-bit big-endian PowerPC Linux, as the host runs of the ticks do, with the game's sources
compiled with HALO_WIIU and the Wii U's Direct3D layer (d3d8_gx2.c and its units) in place of the Linux one; the D3D8 replay's GX2-thread reader
(port/wiiu/tests/d3d8_replay/replay_gx2.c) turns every draw into a line of what the GPU is given, and the glue (game_digest_glue.c) digests each frame.
Under qemu-ppc -cpu 750, with the host runs' settings (b30, co-op hosted with no one joining, the scripted player tickbot:7, random seed 12345, a
tick a frame), the game plays the same ticks each run, and the glue ties to the frames what it does against the machine's time, so two runs of one
build draw every frame of the game the same. A change to the rendering that keeps the pictures keeps every frame's digest; one that does not shows
the first tick it changes (tools/wiiu/digest_compare.py, which also compares the traces' lines, and in any order within a frame or a span).

    tools/test_wiiu_game_digest.py build [--name NAME] [--rev REV] [--mutate PATH BEFORE AFTER]... [--poison] [--wiiu-input]
    tools/test_wiiu_game_digest.py run RUN [--name NAME] [--poison] [--ticks N] [--trace FIRST-LAST|all] [--detail TICK] [--random] [--keys] [--level NAME [--init LINE]... [--cues "SECOND LINE|..."] [--sweep PHASES]] [--custom-map NAME] [--mode host|local|solo|solo-bloodgulch|solo-deathless|menu|menu-cycle|menu-campaign|menu-solo|split2|split3|split4]
    tools/test_wiiu_game_digest.py compare RUN_A RUN_B [digest_compare.py's options]
    GAME_DIGEST=1 python3 -m unittest tools.test_wiiu_game_digest      (GAME_DIGEST_TICKS: the ticks each run plays, 300 by default)

The modes: host (b30 hosted as network co-op, no one joining: the ticks' reference runs), local (the same as a split screen game of one player), and, for the
tick overlap (port/linux/game/tick_overlap.h), which only a campaign game of one player on this machine can use: solo (b30 as the console's map_name starts it, from
init.txt; it opens with a cinematic of about 1,070 ticks), solo-bloodgulch (a multiplayer map as a game of one player, no cinematic) and solo-deathless (b30 with
cheat_deathless_player and cheat_infinite_ammo, so that the fight goes on). HALO_TICK_* in the environment, and GAME_DIGEST_ENV="NAME=VALUE,NAME=VALUE" for the others
(HALO_RENDER_REPEAT=3), go to the game. menu, menu-cycle and menu-campaign are the main menu and the way back to it (a game started from it, the main menu
again, and so on: network_test.c's solo: and its main_menu cue); their --ticks counts frames. split2, split3 and split4 are Blood Gulch as a split screen game of two,
three or four players, joined in the lobby by the virtual controllers of debug.virtual_gamepads' script (SPLIT_JOIN), with the program built --wiiu-input
(tools/test_wiiu_game_digest.py build --wiiu-input: the Wii U's own controller layer, xinput_wiiu.c, in place of SDL's, with no console behind it).
REPLAY_KEYS_FILE=/out/keys.txt in GAME_DIGEST_ENV is the older name of GAME_DIGEST_KEYS (--keys): the pixel shader keys the draws use, once each, as the shader pack's manifest
writes them ("P " and the wire bytes in hex), now with the vertex keys and a comment with the frame before each, for tools/wiiu/key_diff.py to check against a pack.

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

--keys writes the shader keys the run's draws need (the pixel and vertex keys the console's GX2 thread looks up in shaders.pack, in the lines the missing-shader
capture writes on the card) to RUN/out/keys.txt, for tools/wiiu/key_diff.py to compare with tools/wiiu/shader_keys.txt: which shaders a map needs that the pack
lacks, before the map is played on a console. --custom-map NAME plays a Halo Custom Edition map as a game of one player (slayer, as the console's
`game_variant slayer`, `map_name custom_maps\\NAME` do): the folder of the maps, with bitmaps.map, sounds.map and loc.map, is GAME_DIGEST_CUSTOM_MAPS (it
is mounted read-only as /data/custom_maps, and nothing is copied); the game's settings are the defaults, as the Mac's runs have them, or those of
GAME_DIGEST_CONFIG, a copy of a card's config.toml (display.high_res_hud, per_pixel_lighting and the like change the keys).

--level NAME plays a stock level (a10 ... d40, or a multiplayer map's name) as a game of one player, with --init's lines of init.txt before its map_name (cheats, a
game variant) and --cues "<second> <console line>|..." run from the first tick the player has a unit. --sweep PHASES (bsp+objects+effects+damage+weapons+scripts) tours what
the map can draw so that its shader keys are met on the host (port/wiiu/tests/game_digest/game_digest_sweep.c, which has the phases, GAME_DIGEST_SWEEP_WAIT, _HOLD,
_EFFECT_HOLD, _PASSES, _SKIP_FILE and _SCRIPTS); with --keys, RUN/out/keys.txt marks each key with the frame it first came in and RUN/out/sweep.txt says which stop was
on that frame. GAME_DIGEST_CONTAINER_PREFIX names each run's container PREFIX+RUN.

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
           "port/wiiu/tests/game_digest/game_digest_scene.c", "port/wiiu/tests/game_digest/game_digest_sweep.c", "port/wiiu/tests/game_digest/game_digest_input.c",
           "port/wiiu/tests/game_digest/stand_in/coreinit/time.h"]
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
# split screen: bloodgulch as a game of this machine's players (host:), whose lobby the Wii U's virtual controllers (debug.virtual_gamepads and its script,
# port/wiiu/src/wiiu_virtual_pads.h) fill in, in the program built with --wiiu-input (the Wii U's controller layer, xinput_wiiu.c, in place of SDL's). Pad n is
# the controller n - 1. The script, in the clock of 30 presented frames a second from when the lobby opens: pad 2's START joins (the 4-way screen opens),
# the spinner moves past the two default profiles (whose names a game cannot use) to a profile made for the run (game_digest_input.c), A selects it; each other
# joiner the same; a selected player's A ends the screen; the game starts HALO_NETWORK_TEST_START seconds into the lobby, and from 30 s every pad walks
SPLIT_JOIN = {2: "3 2 start; 4 2 right; 4.5 2 right; 5 2 a; 8 2 a",
              3: "3 2 start; 4 2 right; 4.5 2 right; 5 2 a; 6 3 start; 7 3 right; 7.5 3 right; 8 3 right; 8.5 3 a; 11 2 a",
              4: "3 2 start; 4 2 right; 4.5 2 right; 5 2 a; 6 3 start; 7 3 right; 7.5 3 right; 8 3 right; 8.5 3 a; 9.5 4 start; 10.5 4 right; 11 4 right; "
                 "11.5 4 right; 12 4 right; 12.5 4 a; 14 2 a"}
SPLIT_MODES = {f"split{n}": n for n in SPLIT_JOIN}
MODES.update({name: "host:levels\\test\\bloodgulch\\bloodgulch" for name in SPLIT_MODES})
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


def build(name: str = "here", rev: str = None, mutations=(), poison: bool = False, wiiu_input: bool = False) -> Path:
    """the game digest's program for the sources (tree), built in Docker (with poison, build.py --poison's, in build/game-digest-poison); its
    folder"""
    sources = tree(name, rev, mutations)
    folder = "build/game-digest-poison" if poison else "build/game-digest-input" if wiiu_input else "build/game-digest"
    # (GAME_DIGEST_RELEASE=1: the release flavour, configure.py --release's HALO_RELEASE, as the Wii U's RELEASE=1 build has it)
    run_command(["docker", "run", "--rm", "-e", f"GAME_DIGEST_RELEASE={os.environ.get('GAME_DIGEST_RELEASE', '0')}", "-e", f"HALO_NO_HUD_SENTINEL={os.environ.get('HALO_NO_HUD_SENTINEL', '0')}", "-v", f"{sources}:/src",
                 "-w", "/src", IMAGE, "python3", "port/wiiu/tests/game_digest/build.py",
                 "--build-dir", folder, *(["--poison"] if poison else []), *(["--wiiu-input"] if wiiu_input else [])], timeout=3600)
    program = sources / folder
    if not (program / "halo").is_file():
        raise AssertionError(f"no program in {program}")
    return program


def level_path(level: str) -> str:
    """the scenario path of a stock level by its map's name: the campaign's levels\\a10\\a10, the multiplayer maps' levels\\test\\bloodgulch\\bloodgulch"""
    campaign = {"a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40"}
    return f"levels\\{level}\\{level}" if level in campaign else f"levels\\test\\{level}\\{level}"


def start(program: Path, run: str, ticks: int, trace: str = None, detail: str = None, random: bool = False, mode: str = "host", level: str = None,
          init: tuple = (), cues: str = None, extra: dict = None, keys: bool = False, custom_map: str = None, variant: str = "slayer", sweep: str = None):
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
    elif level:
        # (a stock level as a game of one player, with the lines of init.txt before its map_name: cheats, a game variant)
        mode = "solo"
        (folder / "data" / "init.txt").write_text("".join(line + "\n" for line in init) + f"map_name {level_path(level)}\n")
    elif mode in SOLO_INIT:
        (folder / "data" / "init.txt").write_text(SOLO_INIT[mode])
    environment = dict(GAME_ENVIRONMENT, HALO_NETWORK_TEST=MODES[mode], GAME_DIGEST_DIR="/out", GAME_DIGEST_TICKS=str(ticks),
                       GAME_DIGEST_TRACE={None: "0", "all": "1"}.get(trace, trace), GAME_DIGEST_DETAIL=detail or "",
                       GAME_DIGEST_RANDOM="1" if random else "0")
    if mode in SPLIT_MODES:
        environment.update(HALO_NETWORK_TEST_START="25", HALO_VIRTUAL_GAMEPADS=str(SPLIT_MODES[mode]),
                           HALO_VIRTUAL_GAMEPAD_SCRIPT=SPLIT_JOIN[SPLIT_MODES[mode]] + "; " + "; ".join(f"30 {pad} walk" for pad in range(1, SPLIT_MODES[mode] + 1)))
    if mode in MENU_MODES:
        environment.update(GAME_DIGEST_TICKS="0", GAME_DIGEST_FRAMES=str(ticks), HALO_NETWORK_TEST_CUES=MENU_CUES.get(mode, ""))
    if keys:
        environment["GAME_DIGEST_KEYS"] = "/out/keys.txt"
        # (and at the end of the run every vertex key the game's programs can have, drawn with or not: replay_gx2.c's replay_keys_finish)
        environment["GAME_DIGEST_KEYS_ALL_VERTEX"] = "1"
    if sweep:
        # (game_digest_sweep.c: the phases of the tour, bsp+objects+effects+damage, and GAME_DIGEST_SWEEP_WAIT / _HOLD from the environment)
        environment["GAME_DIGEST_SWEEP"] = sweep
        # (the run ends when the tour does, whatever the tick limit)
        environment["GAME_DIGEST_SWEEP_EXIT"] = "1"
        environment.update({key: value for key, value in os.environ.items() if key.startswith("GAME_DIGEST_SWEEP_") and key != "GAME_DIGEST_SWEEP_SKIP_FILE"})
        if os.environ.get("GAME_DIGEST_SWEEP_SKIP_FILE"):
            # (the tags an earlier run did: lines "object NAME", "effect NAME", "damage NAME", "weapon NAME", in the run's data folder)
            shutil.copyfile(os.environ["GAME_DIGEST_SWEEP_SKIP_FILE"], folder / "data" / "sweep_skip.txt")
            environment["GAME_DIGEST_SWEEP_SKIP"] = "sweep_skip.txt"
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
    if custom_map:
        if not os.environ.get("GAME_DIGEST_CUSTOM_MAPS"):
            raise SystemExit("--custom-map needs GAME_DIGEST_CUSTOM_MAPS: the folder of the Custom Edition maps and bitmaps.map, sounds.map, loc.map")
        command += ["-v", f"{os.environ['GAME_DIGEST_CUSTOM_MAPS']}:/data/custom_maps:ro"]
    for key, value in environment.items():
        command += ["-e", f"{key}={value}"]
    # (GAME_DIGEST_DOCKER_ARGS: more arguments of docker run, such as -v for a sampling profiler's folder)
    command += shlex.split(os.environ.get("GAME_DIGEST_DOCKER_ARGS", ""))
    # (a Custom Edition map's tag cache is at 0x40440000, where qemu puts the game's 8 MB stack and its libraries: a smaller stack, as the host runs of
    # the Custom Edition maps have it, tools/stagekit)
    emulator = ["qemu-ppc", "-cpu", "750", "-L", "/usr/powerpc-linux-gnu"] + (["-s", "0x380000"] if custom_map else [])
    command += [IMAGE, "timeout", "-s", "KILL", str(600 + ticks), *emulator, "/program/halo", "--config", "/data/config.toml"]
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


@unittest.skipUnless(os.environ.get("GAME_DIGEST_SAVE"), "a checkpoint that waits for a quiet moment, and a revert to it: GAME_DIGEST_SAVE=1 (and Docker's opence-ppc-be image, data-be's maps; four runs of about 1,400 ticks)")
class SaveWaitAndRevertTest(unittest.TestCase):
    """The frames while a checkpoint waits for its quiet moment (game_save_no_timeout asks every 10 ticks whether the players are safe, and a fight holds it off for as long as
    it lasts: 82 s once in the owner's a50, 2026-11-06) were the serial loop's, and a revert read the 20 MiB back from the card (1.4 to 1.8 s). They are now the overlap's, and
    the revert takes the state from the copy the checkpoint left in memory (reports/research/2026-11-06/stutter-a50.md). Blood Gulch as a game of one player, the frames of
    16.667 ms (two a tick), and game_safe_to_save made false for ticks 100 to 449 (a mutation: the map has no one to fight); the cues (which start again from the top at
    each revert, from the tick it goes back to): a checkpoint at 4 s, which must wait in the first round and need not in the second, and a revert to it at 22 s, twice. Four runs: as built; with the old wait (halo/tick_overlap_save_serial.txt, HALO_TICK_OVERLAP_SAVE_SERIAL=1); with the
    revert read from the card (HALO_ASYNC_SAVE_REVERT_IMAGE=0); and the plain loop. Pass: no halt; the game's ticks are the plain loop's up to the first revert in all of them
    (the checkpoint is a copy: it changes nothing) and the as-built run's and the card-read run's are the same to the end (the bytes a revert takes are the same); the as-built run
    has no serial frame for a save and the old-wait run has; the as-built run takes its two reverts from the copy and the other from the card."""

    CAMERA = {"HALO_TICK_FRAME_MS": "16.667", "HALO_FIXED_STEP": "0"}
    UNSAFE = ("source/game/game.c", "boolean safe = TRUE;", "boolean safe = !(game_time_get() >= 100 && game_time_get() < 450);")
    CUES = "4 game_save_no_timeout|22 game_revert"

    def test_a_waiting_checkpoint_keeps_the_frames_overlapped_and_a_revert_needs_no_card(self):
        ticks = int(os.environ.get("GAME_DIGEST_SAVE_TICKS", "1400"))
        program = build("svwait", mutations=[self.UNSAFE])
        variants = {"as-built": {}, "old-wait": {"HALO_TICK_OVERLAP_SAVE_SERIAL": "1"}, "card-revert": {"HALO_ASYNC_SAVE_REVERT_IMAGE": "0"}, "plain": PLAIN}
        runs = {name: start(program, f"savewait-{name}", ticks, mode="solo-bloodgulch", cues=self.CUES, extra=dict(self.CAMERA, **extra)) for name, extra in variants.items()}
        logs, lines = {}, {}
        for name, (process, out) in runs.items():
            finish(process, out)
            logs[name] = (out / "stderr.txt").read_text(errors="replace")
            lines[name] = tick_lines(out)
        for name, log in logs.items():
            self.assertGreaterEqual(len([line for line in log.splitlines() if "checkpoint: reverted to the saved game" in line]), 2, f"{name}: both reverts happened")
            self.assertIn("checkpoint 2:", log, f"{name}: the waiting checkpoint was made")
        # (the first revert: the tick lines before its log line are the game's before it)
        first_revert = [index for index, line in enumerate(logs["plain"].splitlines()) if "checkpoint: reverted to the saved game" in line][0]
        before = len([line for line in logs["plain"].splitlines()[:first_revert] if "network test: tick" in line])
        self.assertGreater(before, 300, "ticks were played before the first revert")
        for name in ("as-built", "old-wait", "card-revert"):
            count = min(before, len(lines[name]), len(lines["plain"]))
            self.assertEqual(lines["plain"][:count], lines[name][:count], f"{name}: the game's ticks before the first revert differ from the plain loop's")
        count = min(len(lines["as-built"]), len(lines["card-revert"]))
        self.assertGreater(count, ticks // 2)
        self.assertEqual(lines["as-built"][:count], lines["card-revert"][:count], "the game's ticks differ when a revert takes the state from the copy and from the card")
        self.assertIn("keeps the frames overlapped", logs["as-built"])
        self.assertNotIn("serial frames from here (a save)", logs["as-built"], "a checkpoint that waits made the frames serial")
        self.assertIn("makes the frames serial", logs["old-wait"])
        self.assertIn("serial frames from here (a save)", logs["old-wait"], "the test never made a checkpoint wait")
        self.assertEqual(len([line for line in logs["as-built"].splitlines() if "the saved game is taken from the copy held in memory" in line]), 2)
        self.assertNotIn("the saved game is taken from the copy held in memory", logs["card-revert"])


@unittest.skipUnless(os.environ.get("GAME_DIGEST_STALL"), "the script stall report: GAME_DIGEST_STALL=1 (and Docker's opence-ppc-be image, data-be's maps; a run of about 3,600 ticks)")
class ScriptStallReportTest(unittest.TestCase):
    """A script that waits in one sleep_until for minutes (a50's gravity room waits for a count of actors, or for a volume to clear of them, that never comes and the
    level does nothing) is named in the log: hs_runtime.c's hs_runtime_report_stalls writes one line for a wait of 2 minutes of game time; the build here has 10 seconds
    (a mutation of the constant). The room is played from the console as in DeadUnitsAndDoorsTest, the Covenant killed as they are placed until 115 s and the last one left
    standing in the volume: mission_gravity_room then waits for the volume to clear (`(= (volume_test_objects gravity_trigger (ai_actors gravity_pad_covenant)) false)`)."""

    def test_a_long_wait_in_a_sleep_until_is_in_the_log(self):
        ticks = int(os.environ.get("GAME_DIGEST_STALL_TICKS", "3600"))
        program = build("a50stall", mutations=[("source/hs/hs_runtime.c", "HS_STALL_REPORT_TICKS = 2*60*30,", "HS_STALL_REPORT_TICKS = 10*30,")])
        cues = ["1 unit_exit_vehicle (player0)", "2 switch_bsp 1", "3 object_teleport (player0) gravity_teleport0_flag", "4 wake mission_gravity_room"]
        cues += [f"{second} ai_kill gravity_pad_covenant" for second in range(25, 116, 15)]
        process, out = start(program, "stall", ticks, level="a50", init=("cheat_deathless_player 1", "cheat_infinite_ammo 1"), cues="|".join(cues), extra=PLAIN)
        finish(process, out)
        log = (out / "stderr.txt").read_text(errors="replace")
        reports = [line for line in log.splitlines() if "hs: the script mission_gravity_room has waited" in line]
        self.assertGreaterEqual(len(reports), 2, f"mission_gravity_room waits more than once (the Marines' conversation, the wave and the room's clearing): {reports}")
        for report in reports:
            self.assertRegex(report, r"has waited \d+ s in the sleep_until at syntax node \d+ \(its condition is a call of [^)]+\)$")
        # (the script's last wait of this run is the one that never ends: the Covenant left standing, or the waves)
        self.assertEqual(len({report.split("syntax node ")[1].split(" ")[0] for report in reports}), len(reports), "a wait at one node is reported once")


@unittest.skipUnless(os.environ.get("GAME_DIGEST_DOORS"), "dead units and automatic doors: GAME_DIGEST_DOORS=1 (and Docker's opence-ppc-be image, data-be's maps; a run of about 7,400 ticks)")
class DeadUnitsAndDoorsTest(unittest.TestCase):
    """An automatic door is held open by the units near it, but not by dead ones (device_machines.c: the dead bit of the unit's damage flags, a word at 0xB6 of the
    datum). On the big-endian builds the check read the byte at 0xB6, the word's high-order byte, so a body in a doorway held the door open for good. a50's gravity
    room (the cruiser's first room) makes each wave of Covenant through one of four doors and only while that door is shut: with bodies in the four doorways no wave
    came after the fourth, `gravity_wave` stayed true, and mission_gravity_room never got to open `gravity_bay_door` (the owner's console, 2026-11-05: the objective
    says to find Captain Keyes and nothing opens). This plays the room from the console (bsp 1, the player put in it, the script woken), kills each wave where it
    stands as the Covenant are placed, and asks for the bay door's position: it is open (above 0) a few seconds after the last wave is dead, and never is with the
    old check. The console's lines are written to the log by a mutation of terminal.c (the test's own)."""

    CONSOLE_LOG = ("source/interface/terminal.c", "\t\t\tserver_console_output(line->buffer);",
                   "\t\t\t{ void platform_log(char const *format, ...); platform_log(\"console: %s\", line->buffer); }\n\t\t\tserver_console_output(line->buffer);")

    def test_the_bay_door_of_the_gravity_room_opens_when_its_waves_are_dead(self):
        ticks = int(os.environ.get("GAME_DIGEST_DOORS_TICKS", "7400"))
        program = build("a50doors", mutations=[self.CONSOLE_LOG])
        cues = ["1 unit_exit_vehicle (player0)", "2 switch_bsp 1", "3 object_teleport (player0) gravity_teleport0_flag", "4 wake mission_gravity_room"]
        cues += [f"{second} ai_kill gravity_pad_covenant" for second in range(25, 196, 15)]
        cues += [f"{second} inspect (device_get_position gravity_bay_door)" for second in (150, 180, 210, 240)]
        process, out = start(program, "doors", ticks, level="a50", init=("cheat_deathless_player 1", "cheat_infinite_ammo 1"), cues="|".join(cues), extra=PLAIN)
        finish(process, out)
        lines = (out / "stderr.txt").read_text(errors="replace").splitlines()
        # (a cue's answer is the next "console: " line, with the tick's own lines between now and then)
        positions = []
        for index, line in enumerate(lines):
            if "cue at tick" in line and line.endswith("inspect (device_get_position gravity_bay_door)"):
                answer = next((later for later in lines[index + 1:index + 12] if "console: " in later), None)
                if answer:
                    positions.append(float(answer.split("console: ")[1]))
        # (the cues' clock starts when the player has a unit, which is later in some runs: the samples at 150 and 180 s are in all of them)
        self.assertGreaterEqual(len(positions), 2, f"the samples of the bay door's position: {positions}")
        self.assertGreater(max(positions), 0.0, f"gravity_bay_door stayed shut (its positions at 150 s and after: {positions})")


@unittest.skipUnless(os.environ.get("GAME_DIGEST_WEAPON"), "the first person weapon and the ticks' thread: GAME_DIGEST_WEAPON=1 (and Docker's opence-ppc-be image, data-be's maps; runs of 400 ticks)")
class FirstPersonWeaponSwitchTest(unittest.TestCase):
    """The first person weapon is changed by the ticks (first_person_weapons_update: a switch sets weapon_index to NONE, rebuilds the remapping tables and sets the new weapon)
    while the overlap draws the frame on the game thread, which read the field again at each use: between its check that the weapon is not NONE and the call that is given it
    the ticks could set NONE, and object_function_values_for_render(NONE) halted the console (the owner's a50 game, 2026-11-06, 1806 s: `object index #65535 (0xffffffff) is unused
    or changed`, data.c 412, from first_person_weapon_draw). The two interleavings are made to happen, in a30 with the smooth defaults (the tick overlap on), by mutations of
    this test's own:

      draw: the draw sets weapon_index to NONE itself (every 4th draw) just before it asks for the unit's lighting, which is where the field was read again, as the ticks' thread
            would have set it there. Deterministic.
      flap: the tick thread, at the end of each first_person_weapons_update, sets weapon_index to NONE for 150 microseconds and back for 150, 100 times: the two threads at once, as
            on the console, with a window as wide as many of the console's switches. (A race of two threads: the old sources halt in it in most runs, not in every one.)

    The old sources (OLD_REV, the console's build of that day) halt in both; the fixed ones draw the frames, and say in the log that the weapon was changed under a frame
    (`first person weapon: the weapon ... the frame chose was ...`) in the draw run, so that the test cannot pass for the interleaving not having happened."""

    OLD_REV = os.environ.get("GAME_DIGEST_WEAPON_OLD", "652af041")
    DRAW = ("source/interface/first_person_weapons.c", "\t\t\t\t\tstruct render_lighting *lighting= object_get_cached_render_lighting(unit_index, FLT_MAX);",
            "\t\t\t\t\t{ static long test_draws; if (++test_draws % 4 == 0) first_person_weapon->weapon_index= NONE; }\n"
            "\t\t\t\t\tstruct render_lighting *lighting= object_get_cached_render_lighting(unit_index, FLT_MAX);")
    FLAP = ("source/interface/first_person_weapons.c", "\t\t\tfirst_person_weapon_update(local_player_index);\n",
            "\t\t\tfirst_person_weapon_update(local_player_index);\n"
            "\t\t\t{\n"
            "\t\t\t\textern int usleep(unsigned int);\n"
            "\t\t\t\tlong held= first_person_weapon->weapon_index;\n"
            "\t\t\t\tint flaps;\n\n"
            "\t\t\t\tfor (flaps= 0; held!=NONE && flaps<100; flaps++)\n"
            "\t\t\t\t{\n"
            "\t\t\t\t\tfirst_person_weapon->weapon_index= NONE;\n"
            "\t\t\t\t\tusleep(150);\n"
            "\t\t\t\t\tfirst_person_weapon->weapon_index= held;\n"
            "\t\t\t\t\tusleep(150);\n"
            "\t\t\t\t}\n"
            "\t\t\t}\n")
    # (a30: the player has a weapon in hand in the level's first frames, which are overlapped: LevelsTest's regression found the first person weapon there)
    LEVEL = "a30"

    def play(self, name, mutation, rev=None):
        """a30 for GAME_DIGEST_WEAPON_TICKS ticks (400) with the smooth defaults and the mutation: (the run's end, its log); the end is the exception text of a halted run"""
        ticks = int(os.environ.get("GAME_DIGEST_WEAPON_TICKS", "400"))
        program = build(name, rev, mutations=[mutation])
        process, out = start(program, name, ticks, level=self.LEVEL, init=("cheat_deathless_player 1", "cheat_infinite_ammo 1"))
        try:
            finish(process, out)
            ended = None
        except AssertionError as error:
            ended = str(error)
        return ended, (out / "stderr.txt").read_text(errors="replace")

    def assertHalted(self, ended, log):
        self.assertTrue(ended is not None or "EXCEPTION halt" in log, "the run did not halt")
        self.assertRegex(ended or log, r"index #65535 \(0xffffffff\) is unused or changed")

    def assertNotHalted(self, ended, log):
        self.assertIsNone(ended, ended)
        self.assertNotIn("EXCEPTION halt", log)
        self.assertNotIn("is unused or changed", log)

    def test_a_weapon_set_to_none_in_the_middle_of_a_frame_is_not_drawn(self):
        ended, log = self.play("weapon-draw", self.DRAW)
        self.assertIn("tick overlap: on for this map", log)
        self.assertNotHalted(ended, log)
        self.assertIn("first person weapon: the weapon", log, "the weapon was never changed under a frame: the draw mutation did not land")

    def test_the_old_draw_halts_on_the_same_switch(self):
        if not os.environ.get("GAME_DIGEST_WEAPON_OLD_RUN"):
            self.skipTest("the control: GAME_DIGEST_WEAPON_OLD_RUN=1 (the sources of GAME_DIGEST_WEAPON_OLD, 652af041)")
        ended, log = self.play("weapon-draw-old", self.DRAW, self.OLD_REV)
        self.assertHalted(ended, log)

    def test_a_weapon_that_the_tick_thread_switches_all_the_time_does_not_halt_the_frames(self):
        ended, log = self.play("weapon-flap", self.FLAP)
        self.assertIn("tick overlap: on for this map", log)
        self.assertNotHalted(ended, log)

    def test_the_old_draw_halts_when_the_tick_thread_switches_the_weapon(self):
        if not os.environ.get("GAME_DIGEST_WEAPON_OLD_RUN"):
            self.skipTest("the control: GAME_DIGEST_WEAPON_OLD_RUN=1 (the sources of GAME_DIGEST_WEAPON_OLD, 652af041)")
        ended, log = self.play("weapon-flap-old", self.FLAP, self.OLD_REV)
        self.assertHalted(ended, log)


@unittest.skipUnless(os.environ.get("GAME_DIGEST_BLEND_REUSE"), "the interpolation's blends of poses at rest, taken from the frame before: GAME_DIGEST_BLEND_REUSE=1 (and Docker's opence-ppc-be image, data-be's maps; six runs of 400 ticks)")
class BlendReuseTest(unittest.TestCase):
    """The blend of an object whose two poses are the same bit for bit (or that snapped) is taken from the frame before (render_interpolation.c, overlap_object: halo/gpu_offload.txt bit 8
    makes every blend again, as before the reuse; bit 16 also makes each reused blend afresh and compares). The frames of 16.667 ms (two a tick, so that the fraction varies), Blood Gulch as a
    game of one player, in the plain loop (the serial frames' blends) and the tick overlap (the ring's), the tick thread serialised so that two runs of the overlap draw alike
    (HALO_TICK_OVERLAP_SERIALIZE: they do not otherwise), the HUD's sheet builder thread off (its timing is not the frames'). Pass: every frame of the three runs of each loop digests the
    same (reuse off, reuse on, reuse checked), the check compared blends and found none that differs, and the runs with the reuse took blends from the frames before while the run without it did not."""

    FRAMES = {"HALO_TICK_FRAME_MS": "16.667", "HALO_FIXED_STEP": "0", "HALO_HUD_SHEETS": "0", "HALO_TICK_OVERLAP_SERIALIZE": "1"}

    def play(self, name, loop, bits):
        extra = dict(self.FRAMES, HALO_GPU_OFFLOAD=str(bits))
        if loop == "plain":
            extra.update(PLAIN)
        # (the program takes these from its environment: GAME_DIGEST_ENV for the ones it reads there, extra for the ones the run sets itself)
        process, out = start(build(), name, int(os.environ.get("GAME_DIGEST_BLEND_REUSE_TICKS", "400")), mode="solo-bloodgulch", extra=extra)
        return process, out

    def test_the_reuse_changes_no_frame_and_the_check_finds_no_difference(self):
        for loop in ("plain", "overlap"):
            runs = {bits: self.play(f"blend-reuse-{loop}-{bits}", loop, bits) for bits in (8, 0, 16)}
            outs = {bits: run[1] for bits, run in runs.items()}
            for process, out in runs.values():
                finish(process, out)
            with open(os.devnull, "w") as quiet:
                self.assertEqual(digest_compare.compare(outs[8], outs[0], out=quiet), 0, f"{loop}: the reuse changed a frame")
                self.assertEqual(digest_compare.compare(outs[8], outs[16], out=quiet), 0, f"{loop}: the checked run drew another frame")
            checked = (outs[16] / "stderr.txt").read_text(errors="replace")
            import re

            found = re.findall(r"gpu offload reuse check: (\d+) blends taken from an earlier frame compared with the same made afresh, (\d+) differ", checked)
            self.assertTrue(found, f"{loop}: the check said nothing")
            self.assertGreater(int(found[-1][0]), 1000, f"{loop}: the check compared too few blends to mean anything")
            self.assertEqual(found[-1][1], "0", f"{loop}: a reused blend differs from the same blend made afresh")
            self.assertNotIn("differs from the same blend made afresh", checked)
            if loop == "overlap":
                taken = lambda out: [float(n) for n in re.findall(r"([\d.]+) blends taken from an earlier frame", (out / "stderr.txt").read_text(errors="replace"))]
                self.assertGreater(max(taken(outs[0]) or [0.0]), 1.0, "the overlap took no blend from an earlier frame")
                self.assertEqual(max(taken(outs[8]) or [0.0]), 0.0, "the run with the reuse off took a blend from an earlier frame")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], formatter_class=argparse.RawDescriptionHelpFormatter,
                                     epilog=__doc__.split("\n\n", 1)[1])
    commands = parser.add_subparsers(dest="command", required=True)
    build_parser = commands.add_parser("build", help="build the game digest's program")
    build_parser.add_argument("--name", default="here")
    build_parser.add_argument("--rev", help="a commit's sources (git archive), with this checkout's harness")
    build_parser.add_argument("--mutate", nargs=3, action="append", default=[], metavar=("PATH", "BEFORE", "AFTER"))
    build_parser.add_argument("--poison", action="store_true", help="build.py --poison's program (the stack filled, the data moved)")
    build_parser.add_argument("--wiiu-input", action="store_true", help="build.py --wiiu-input's program (the Wii U's controller layer: the split screen modes)")
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
    run_parser.add_argument("--level", help="play this stock level (a10 ... d40, bloodgulch ...) as a game of one player (the solo mode's, with --init's lines)")
    run_parser.add_argument("--init", action="append", default=[], help="a line of init.txt before the map_name (--level), such as cheat_deathless_player 1")
    run_parser.add_argument("--sweep", help="the sweep's phases (game_digest_sweep.c): bsp+objects+effects+damage, a tour of what the map can draw")
    run_parser.add_argument("--cues", help="debug.network_test_cues: <second> <console line>, joined with |")
    run_parser.add_argument("--mode", choices=sorted(MODES), default="host")
    compare_parser = commands.add_parser("compare", help="tools/wiiu/digest_compare.py on two runs (names under build/game-digest/runs, or folders)")
    compare_parser.add_argument("arguments", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.command == "build":
        print(build(args.name, args.rev, [tuple(m) for m in args.mutate], args.poison, args.wiiu_input) / "halo")
        return 0
    if args.command == "run":
        program = (ROOT if args.name == "here" else DIGEST / "trees" / args.name) / "build" / (
            "game-digest-poison" if args.poison else "game-digest-input" if args.mode in SPLIT_MODES else "game-digest")
        summary = finish(*start(program, args.run, args.ticks, args.trace, args.detail, args.random, args.mode, args.level, tuple(args.init), args.cues, None,
                                args.keys, args.custom_map, args.variant, args.sweep))
        print(" ".join(f"{name} {value}" for name, value in summary.items()))
        return 0
    arguments = [str(DIGEST / "runs" / a / "out") if not a.startswith("-") and not Path(a).exists() and (DIGEST / "runs" / a).is_dir() else a
                 for a in args.arguments]
    return digest_compare.main(arguments)


if __name__ == "__main__":
    sys.exit(main())
