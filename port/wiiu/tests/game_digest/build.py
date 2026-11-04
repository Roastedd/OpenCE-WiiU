#!/usr/bin/env python3
"""The game digest's build: the real game on big-endian PowerPC Linux (as configure.py --linux-target=ppc builds it, run under qemu-ppc),
with the Wii U's Direct3D layer in place of the Linux one, and the D3D8 replay's GX2-thread reader as the sink of every draw.

    python3 port/wiiu/tests/game_digest/build.py [--build-dir DIR] [--jobs N] [--poison]

Run from the repository's root in the opence-ppc-be container (tools/test_wiiu_game_digest.py does). It writes DIR/build.ninja (default
build/game-digest) and runs ninja on it; the program is DIR/halo. It does not touch configure.py or build.ninja: the graph is the Linux one,
as tools/linux_build.py emits it for the big-endian build, with these differences, made as it is emitted (DigestWriter):

- the game's sources (source/ and port/linux/game) are compiled with HALO_WIIU, as the Wii U build compiles them, so that what the game
  does only on the Wii U (wiiu_decal_vertices, the index buffers renewed, and the native draws' hooks, all under HALO_WIIU) is what draws;
- the platform layer's renderer on OpenGL is left out (d3d8_gl.c, xbox_textures.c, the shaders' GLSL, the post-processing and text on
  OpenGL, the OpenGL loader, and memory_watch.c, whose page protection the Wii U's hash watch replaces), and the units that hand the
  Wii U's texture cache its pictures (hud_hires.c, menu_files.c, text_hires.c) are compiled with HALO_WIIU, as on the Wii U;
- the Wii U's Direct3D layer is added, built as tools/test_wiiu_d3d8_replay.py builds it (d3d8_gx2.c, xbox_textures_gx2.c,
  memory_watch_wiiu.c, dk_derive.c, dk_native*.c with the game's ABI and HALO_WIIU; the wiiu_* queue units with the C library's), with
  the D3D8 replay's GX2-thread reader (port/wiiu/tests/d3d8_replay/replay_gx2.c, as it is, with wiiu_gx2.c's decals_expand) and this
  folder's glue (game_digest_glue.c): the console-only functions the layer and the game call, and the sink that digests each frame;
- what the game does against the machine's time is tied to the frames, so that two runs under qemu, whose speed varies, draw the same: a few
  of the game's calls are linked to the glue's (WRAPPED, -Wl,--wrap: the clock in milliseconds, the map's reads and its copy to the cache,
  the texture and sound caches' asks, the local random numbers at a map's start) and a few units' calls of a clock or a thread are renamed to the glue's (UNIT_RENAMES); the glue
  has why for each. No source file is changed.

The Xbox memory window stays where the big-endian Linux build has it (0x80000000, HALO_PPC's 128 MB), so the maps are data-be's, as the
host runs of the ticks use (runs/host-mac-perf-20261031/ticks).
"""

import argparse
import os
import subprocess
import sys
from io import StringIO
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT))
os.chdir(ROOT)

from tools import linux_build  # noqa: E402
from tools.ninja_syntax import Writer  # noqa: E402

HERE = Path("port/wiiu/tests/game_digest")
REPLAY = Path("port/wiiu/tests/d3d8_replay")
WIIU_SRC = Path("port/wiiu/src")

# the Linux renderer on OpenGL, and the page-protection memory watch: the Wii U's layer replaces them
LEFT_OUT = {
    "port/linux/src/d3d8_gl.c", "port/linux/src/gl_functions.c", "port/linux/src/nv2a_psh.c", "port/linux/src/nv2a_vsh.c",
    "port/linux/src/xbox_textures.c", "port/linux/src/xgpu_post.c", "port/linux/src/xgpu_text.c", "port/linux/src/memory_watch.c",
}
# the platform layer's units whose Wii U side the Wii U's texture cache calls (menu art, the HUD's PNGs, the text's atlas), and the
# dedicated server's admin page, which on the Wii U has no server to show (server_mode.h: the game's server units are HALO_WIIU's too)
WIIU_PLATFORM_UNITS = {"port/linux/src/hud_hires.c", "port/linux/src/menu_files.c", "port/linux/src/text_hires.c", "port/linux/src/server_http.c"}
# the Wii U's Direct3D layer: the game-ABI units (and the native commands' game side) and the C library's side
LAYER_UNITS = ["port/wiiu/src/d3d8_gx2.c", "port/wiiu/src/xbox_textures_gx2.c", "port/wiiu/src/memory_watch_wiiu.c", "port/wiiu/src/dk_derive.c"]
HOST_UNITS = ["port/wiiu/src/wiiu_immediate.c", "port/wiiu/src/wiiu_indices.c", "port/wiiu/src/wiiu_inflight.c",
              "port/wiiu/src/wiiu_pending_reads.c", "port/wiiu/src/wiiu_stream_queue.c"]
# (the flags tools/test_wiiu_d3d8_replay.py builds the layer with, less the -Werror the game's headers would trip over in this mix)
LAYER_FLAGS = ("-fms-extensions -fshort-wchar -fcommon -fno-pic -fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks -fno-omit-frame-pointer "
               "-ffp-contract=off -O2 -g -DHALO_WIIU -std=gnu11 -D_GNU_SOURCE -DHALO_LINUX_PLATFORM_LAYER -Wall -Wno-unused-function "
               "-Wno-unknown-pragmas -Wno-microsoft-anon-tag -Wno-pragma-pack -Wno-ignored-attributes -Wno-duplicate-decl-specifier -Wno-missing-braces "
               "-Wno-unused-variable -Wno-ignored-pragmas -Wno-macro-redefined").split()
WINDOW_BASE = "0x80000000"
LAYER_FLAGS.append(f"-DHALO_PORT_WINDOW_BASE_OVERRIDE={WINDOW_BASE}UL")
HOST_FLAGS = "-std=gnu11 -D_GNU_SOURCE -O2 -g -Wall -ffp-contract=off".split()
# the platform layer's own functions that the Wii U's layer defines too, or that are the Linux window's: the layer's calls go to the
# glue's (the console's answers), not to SDL's window
LAYER_RENAMES = {
    "platform_video_initialize": "game_digest_video_initialize",
    "platform_video_swap": "game_digest_video_swap",
    "platform_screen_mode": "game_digest_screen_mode",
}


# the units that read a clock themselves, not through the game's system_milliseconds: their reads go to the glue's clock of the frames
# (game_digest_glue.c's game_digest_milliseconds has why): the loading screen's progress bar, and the scripted player's clock before a game's
# clock runs (xinput_sdl.c's test_input_seconds, which moves the menus' cursor in the first frames)
UNIT_RENAMES = {"source/interface/progress_bar.c": {"GetTickCount": "game_digest_milliseconds"},
                "port/linux/src/xinput_sdl.c": {"SDL_GetTicks": "game_digest_sdl_ticks"},
                # (and the silent sound's thread, which mixes in real time: game_digest_glue.c runs it by the frames)
                "port/linux/src/dsound_sdl.c": {"pthread_create": "game_digest_audio_thread_create", "clock_nanosleep": "game_digest_audio_sleep"}}
# the functions whose callers call the glue's __wrap_ (game_digest_glue.c has why): the layer's hand-over of each stream (for the detail of a
# frame), the map's copy and reads, the game's clock, the texture and sound caches' asks, the local random numbers (for who draws them), and the
# nickname's draw and each map's start (the local random numbers set at each map's start), and the call that sends constants it never set (its
# stack cleared first)
WRAPPED = ["host_gx2_submit", "cache_copy_get_status", "system_milliseconds", "cache_file_read", "_texture_cache_bitmap_get_hardware_format",
           "_sound_cache_sound_request", "get_global_local_random_seed_address", "network_game_get_random_player_name",
           "game_initialize_for_new_map", "rasterizer_set_model_lighting", "render_governor_frame", "texture_cache_bitmap_get_hardware_format_streaming"]
# (and with --poison, the frame's rendering, whose stack is filled first)
POISON_WRAPPED = ["render_frame"]
# the bytes the program's data is moved by with --poison
POISON_PADDING = 40000


def layer_units():
    """the Wii U layer's game-ABI units: the four the replay builds, and the native commands' game side as it is written (dk_native*.c)"""
    return LAYER_UNITS + sorted(p.as_posix() for p in WIIU_SRC.glob("dk_native*.c"))


class DigestWriter(Writer):
    """tools/linux_build.py's big-endian graph, changed as it is written (the module's docstring has what changes)"""

    def __init__(self, output, build_dir):
        super().__init__(output)
        self.build_dir = build_dir
        self.links = []
        self.dropped = set()

    def build(self, outputs, rule, inputs=None, implicit=None, order_only=None, variables=None, implicit_outputs=None, pool=None, dyndep=None):
        source = Path(inputs).as_posix() if isinstance(inputs, (str, Path)) else None
        if rule == "linux_cc" and source:
            if source in LEFT_OUT or source == "tools/map_validate.c":
                self.dropped.add(Path(outputs).as_posix())
                return []
            if source.startswith("source/") or source.startswith("port/linux/game/") or source in WIIU_PLATFORM_UNITS:
                variables = dict(variables or {})
                variables["cflags"] = variables["cflags"] + " -DHALO_WIIU"
            if source in UNIT_RENAMES:
                variables = dict(variables or {})
                variables["cflags"] = variables["cflags"] + "".join(f" -D{name}={value}" for name, value in UNIT_RENAMES[source].items())
        if rule == "linux_tool_link" or (rule == "phony" and outputs in ("linux", "linux_ppc")):
            return []
        if rule == "linux_link":
            self.links.append((outputs, [Path(i).as_posix() for i in inputs], variables, implicit))
            return []
        return super().build(outputs, rule, inputs=inputs, implicit=implicit, order_only=order_only, variables=variables,
                             implicit_outputs=implicit_outputs, pool=pool, dyndep=dyndep)


def generate(build_dir: Path, poison: bool = False) -> str:
    sln = SimpleNamespace(build_dir=build_dir, linux_cc=None, linux_target="ppc", compiler_launcher=None, port_release=False,
                          port_profile=False, port_lto="off", port_portable=False, port_pgo="off", port_pgo_profile=None)
    text = StringIO()
    n = DigestWriter(text, build_dir)
    n.variable("ninja_required_version", "1.3")
    n.variable("python", sys.executable)
    # (ninja's log and dependencies beside the digest's objects, not the checkout's build.ninja's)
    n.variable("builddir", build_dir.as_posix())
    # (the window where the big-endian build has it, for every unit: HALO_WIIU would put the game's at the Wii U's 0x41EA0000, which the
    # platform layer's mapping and data-be's maps are not at; tools/linux_build.py's configuration_defines gives it to every unit's flags)
    os.environ["HALO_WINDOW_BASE"] = WINDOW_BASE
    # (tools/linux_build.py names its build folder linux-ppc under build_dir: the digest's objects are kept apart from a real build's)
    linux_build.generate_linux_build(n, sln)
    if len(n.links) != 1:
        raise SystemExit(f"expected one link of the game, found {len(n.links)}")
    game_output, game_inputs, link_variables, link_implicit = n.links[0]
    objects = [i for i in game_inputs if i not in n.dropped]
    missing = sorted(LEFT_OUT - {Path(d).with_suffix(".c").as_posix().split("/obj/", 1)[1] for d in n.dropped if "/obj/" in d})
    if missing:
        raise SystemExit(f"the Linux graph no longer builds {missing}: tools/linux_build.py changed")

    obj = build_dir / "linux-ppc" / "digest"
    generated = build_dir / "linux-ppc" / "generated"
    semantics = build_dir / "linux-ppc" / "platform_msvc_semantics.h"
    target = linux_build.PPC_TARGET_FLAGS + ["-mcpu=750"]
    includes = ["-include", "port/linux/include/halo_linux_prefix.h", "-include", str(semantics), "-include", "stdint.h",
                f"-I{REPLAY}", f"-I{generated}", f"-I{WIIU_SRC}", "-Iport/linux/src", "-Iport/wiiu/include", "-Iport/linux/include",
                "-Isource", "-Isource/cseries", "-idirafter", "port/include/xdk"]
    renames = [f"-D{name}={value}" for name, value in LAYER_RENAMES.items()]
    # (wiiu_gx2.c's decals_expand, taken from the source as the replay's test takes it)
    decals = generated / "replay_decals.inc"
    n.rule("digest_decals", command=f"$python {HERE}/build.py --decals $out", description="DIGEST DECALS $out")
    n.build(decals, "digest_decals", implicit=[WIIU_SRC / "wiiu_gx2.c", HERE / "build.py"])
    n.rule("digest_cc", command="clang -MMD -MF $out.d $cflags -c $in -o $out", description="DIGEST CC $out", depfile="$out.d", deps="gcc")
    extra = []
    for unit in layer_units():
        out = obj / Path(unit).with_suffix(".o")
        extra.append(out.as_posix())
        n.build(out, "digest_cc", unit, implicit=[semantics], variables={"cflags": " ".join(target + LAYER_FLAGS + includes + renames)})
    for unit in [REPLAY / "replay_gx2.c", HERE / "game_digest_glue.c"]:
        out = obj / Path(unit).with_suffix(".o")
        extra.append(out.as_posix())
        poisoned = [f"-DGAME_DIGEST_POISON={POISON_PADDING}"] if poison and unit.name == "game_digest_glue.c" else []
        n.build(out, "digest_cc", str(unit), implicit=[semantics, decals],
                variables={"cflags": " ".join(target + LAYER_FLAGS + includes + poisoned)})
    for unit in HOST_UNITS:
        out = obj / Path(unit).with_suffix(".o")
        extra.append(out.as_posix())
        n.build(out, "digest_cc", unit, variables={"cflags": " ".join(target + HOST_FLAGS + [f"-I{WIIU_SRC}", "-Iport/linux/src"])})
    if poison:
        # (linked first: every other unit's data and the heap moved, game_digest_padding.c)
        padding = obj / HERE / "game_digest_padding.o"
        n.build(padding, "digest_cc", str(HERE / "game_digest_padding.c"),
                variables={"cflags": " ".join(target + HOST_FLAGS + [f"-DGAME_DIGEST_POISON={POISON_PADDING}"])})
        objects = [padding.as_posix()] + objects
    halo = build_dir / "halo"
    # (the Linux link's rule and flags, with the wrapped functions: Writer's own build, past DigestWriter's, which keeps the Linux graph's
    # link for this)
    link_variables = dict(link_variables)
    link_variables["ldflags"] = link_variables["ldflags"] + " " + " ".join(f"-Wl,--wrap={name}" for name in WRAPPED + (POISON_WRAPPED if poison else []))
    Writer.build(n, halo, "linux_link", objects + extra, implicit=link_implicit, variables=link_variables)
    n.build("game_digest", "phony", [str(halo)])
    n.default("game_digest")
    return text.getvalue()


def write_decals(output: Path) -> None:
    sys.path.insert(0, str(ROOT / "tools"))
    from harness import function

    source = (WIIU_SRC / "wiiu_gx2.c").read_text(encoding="latin-1")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("/* wiiu_gx2.c's, as it is */\n" + function(source, "decals_expand") + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--build-dir", default="build/game-digest", type=Path)
    parser.add_argument("--jobs", type=int)
    parser.add_argument("--decals", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--generate-only", action="store_true", help="write the ninja file and stop")
    parser.add_argument("--poison", action="store_true",
                        help="the stack filled with 0xa5 below each frame's rendering and the data laid out otherwise (game_digest_glue.c has why)")
    args = parser.parse_args()
    if args.decals:
        write_decals(args.decals)
        return 0
    args.build_dir.mkdir(parents=True, exist_ok=True)
    ninja_file = args.build_dir / "build.ninja"
    text = generate(args.build_dir, args.poison)
    if not ninja_file.is_file() or ninja_file.read_text() != text:
        ninja_file.write_text(text)
    if args.generate_only:
        return 0
    command = ["ninja", "-f", str(ninja_file)] + ([f"-j{args.jobs}"] if args.jobs else [])
    return subprocess.run(command).returncode


if __name__ == "__main__":
    sys.exit(main())
