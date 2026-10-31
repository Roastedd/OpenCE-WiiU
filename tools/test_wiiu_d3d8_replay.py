"""The Wii U's Direct3D layer replayed on Linux (port/wiiu/tests/d3d8_replay/replay_main.c): what the GPU is given for every draw of a made-up game, as the
GX2 thread reads the command streams, digested; the digest is the one recorded here whatever the GX2 thread's schedule, and so is the digest of the
resources' Lock fields for each way the streams run. A change to the layer that keeps the pictures keeps both.

D3D8_REPLAY_TARGET='--target=powerpc-linux-gnu -fsigned-char -msvr4-struct-return' D3D8_REPLAY_RUNNER='qemu-ppc -cpu 750', in the opence-wiiu
container, as verify.sh runs the other big-endian tests; with no target the test is skipped (the layer has PowerPC assembly). D3D8_REPLAY_TRACES=folder keeps each run's trace there,
to be compared line by line with another build's.
"""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from harness import function

REPLAY = ROOT / "port/wiiu/tests/d3d8_replay"
# the units of the layer, built as the Wii U build builds them: the platform layer's (the game's ABI) and the C library's side (wiiu_*.c)
GAME_UNITS = ["port/wiiu/src/d3d8_gx2.c", "port/wiiu/src/xbox_textures_gx2.c", "port/wiiu/src/memory_watch_wiiu.c", "port/wiiu/src/dk_derive.c",
              "port/linux/src/d3d8_resources.c"]
HOST_UNITS = ["port/wiiu/src/wiiu_immediate.c", "port/wiiu/src/wiiu_indices.c", "port/wiiu/src/wiiu_inflight.c", "port/wiiu/src/wiiu_pending_reads.c",
              "port/wiiu/src/wiiu_stream_queue.c"]
REPLAY_UNITS = ["replay_world.c", "replay_gx2.c", "replay_game.c", "replay_main.c"]
GAME_FLAGS = ("-fms-extensions -fshort-wchar -fcommon -fno-pic -fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks -fno-omit-frame-pointer "
              "-ffp-contract=off -O2 -g -DHALO_WIIU -std=gnu11 -D_GNU_SOURCE -DHALO_LINUX_PLATFORM_LAYER -Wall -Werror -Wno-unused-function "
              "-Wno-unknown-pragmas -Wno-microsoft-anon-tag -Wno-pragma-pack -Wno-ignored-attributes -Wno-duplicate-decl-specifier -Wno-missing-braces "
              "-Wno-unused-variable -Wno-ignored-pragmas -Wno-macro-redefined").split()
HOST_FLAGS = "-std=gnu11 -D_GNU_SOURCE -O2 -g -Wall -Werror -ffp-contract=off".split()

FRAMES = 2470
# what the GPU is given (every schedule's), and the Lock fields for the streams run as they are handed over and posted to the queue. (The
# digest hashes the pixel shader key's struct as it is: upstream's merge added a field to it, ce_meter_channels, and with that the digest went
# from 2e63215d0e9d18a5 to this, every draw's pixel key hash one to one with the old and nothing else of any draw changed. The shader pack's keys
# are shader_key_wire.h's field order, which the field is not in.)
DIGEST = "3599a1f106da2276"
LOCKS = {"sync": "ec90e29cf5d40ffb", "async": "4f0177eef06689d2"}
SCHEDULES = [("sync", "lag", 1), ("async", "lag", 1), ("async", "eager", 7), ("async", "eager", 99)]


def run_command(command, **arguments):
    result = subprocess.run(command, capture_output=True, text=True, **arguments)
    if result.returncode:
        raise AssertionError(" ".join(command) + "\n" + result.stdout + result.stderr)
    return result


def build(folder, mutations=None):
    """the replay built in folder, with each (path, before, after) of mutations made in the source it names; the executable's path"""
    sources = folder / "sources"
    for relative in GAME_UNITS + HOST_UNITS + ["port/wiiu/src/wiiu_gx2.c"]:
        text = (ROOT / relative).read_text(encoding="latin-1")
        for path, before, after in mutations or []:
            if path == relative:
                if text.count(before) != 1:
                    raise LookupError(f"negative control no longer applies to {path} (found {text.count(before)} times): {before!r}")
                text = text.replace(before, after, 1)
        (sources / relative).parent.mkdir(parents=True, exist_ok=True)
        (sources / relative).write_text(text, encoding="latin-1")
    generated = folder / "generated"
    generated.mkdir(exist_ok=True)
    run_command([sys.executable, str(ROOT / "tools/linux_msvc_semantics.py"), "--output", str(generated / "platform_msvc_semantics.h"),
                 "--inlines", str(ROOT / "port/include/xdk")])
    wiiu_gx2 = (sources / "port/wiiu/src/wiiu_gx2.c").read_text(encoding="latin-1")
    (generated / "replay_decals.inc").write_text("/* wiiu_gx2.c's, as it is */\n" + function(wiiu_gx2, "decals_expand") + "\n")
    compiler = os.environ.get("CC", "clang")
    target = shlex.split(os.environ.get("D3D8_REPLAY_TARGET", ""))
    includes = ["-include", str(ROOT / "port/linux/include/halo_linux_prefix.h"), "-include", str(generated / "platform_msvc_semantics.h"),
                "-include", "stdint.h", "-I" + str(REPLAY), "-I" + str(generated), "-I" + str(sources / "port/wiiu/src"),
                "-I" + str(ROOT / "port/linux/src"), "-I" + str(ROOT / "port/wiiu/include"), "-I" + str(ROOT / "port/linux/include"),
                "-I" + str(ROOT / "source"), "-I" + str(ROOT / "source/cseries"), "-idirafter", str(ROOT / "port/include/xdk"),
                "-I" + str(ROOT / "port/wiiu/src")]
    objects = []
    for unit in [sources / u for u in GAME_UNITS] + [REPLAY / u for u in REPLAY_UNITS]:
        objects.append(folder / (unit.stem + ".o"))
        run_command([compiler, *target, *GAME_FLAGS, *includes, "-c", str(unit), "-o", str(objects[-1])])
    for unit in [sources / u for u in HOST_UNITS]:
        objects.append(folder / (unit.stem + ".o"))
        run_command([compiler, *target, *HOST_FLAGS, "-I" + str(ROOT / "port/wiiu/src"), "-I" + str(ROOT / "port/linux/src"),
                     "-c", str(unit), "-o", str(objects[-1])])
    executable = folder / "replay"
    run_command([compiler, *target, "-fuse-ld=lld", "-static", *map(str, objects), "-lm", "-lpthread", "-o", str(executable)])
    return executable


def replay(executable, mode, schedule, seed, frames=FRAMES, trace=None):
    """one run: its digest, its Lock fields' digest and what it counted"""
    command = [*shlex.split(os.environ.get("D3D8_REPLAY_RUNNER", "")), str(executable), mode, schedule, str(seed), str(frames)]
    if trace:
        command.append(str(trace))
    result = run_command(command, timeout=600)
    values = {}
    for line in result.stdout.split("\n"):
        words = line.split()
        for name, value in zip(words[0::2], words[1::2]):
            values[name] = value
    return values


@unittest.skipUnless(os.environ.get("D3D8_REPLAY_TARGET"), "PowerPC only: D3D8_REPLAY_TARGET and D3D8_REPLAY_RUNNER (qemu-ppc -cpu 750)")
class D3D8ReplayTest(unittest.TestCase):
    def build_in(self, folder, mutations=None):
        (Path(folder) / "sources").mkdir()
        return build(Path(folder), mutations)

    def test_every_schedule_gives_the_recorded_pictures(self):
        traces = os.environ.get("D3D8_REPLAY_TRACES")
        with tempfile.TemporaryDirectory(prefix="opence-d3d8-replay-") as folder:
            executable = self.build_in(folder)
            for mode, schedule, seed in SCHEDULES:
                trace = Path(traces) / f"{mode}-{schedule}-{seed}.txt" if traces else None
                values = replay(executable, mode, schedule, seed, trace=trace)
                with self.subTest(mode=mode, schedule=schedule, seed=seed):
                    self.assertEqual(values["digest"], DIGEST)
                    self.assertEqual(values["locks"], LOCKS[mode])
                    # (the frame's end finds no state the setters did not say they wrote, and no target changed under the layer)
                    self.assertEqual(values["states-missed"], "0")
                    self.assertEqual(values["targets-missed"], "0")
                    self.assertGreater(int(values["draws"]), 5000)

    def mutated_digests(self, mutations, runs):
        with tempfile.TemporaryDirectory(prefix="opence-d3d8-replay-") as folder:
            executable = self.build_in(folder, mutations)
            return [replay(executable, mode, schedule, seed, frames=60)["digest"] for mode, schedule, seed in runs]

    def hazard_seen(self, mutation):
        """a read not noted, as each way of running the streams sees it: what was written after a draw reaches the stream that reads it, unless it was
        run before the write (handed over at once, or the GX2 thread caught up)"""
        runs = [("sync", "lag", 1), ("async", "lag", 1)]
        for reference, mutated in zip(self.mutated_digests([], runs), self.mutated_digests([mutation], runs)):
            self.assertNotEqual(reference, mutated)

    def test_a_missed_stream_read_is_seen(self):
        # (the draws' vertex streams not noted as read: the dynamic vertices written after a draw reach it)
        self.hazard_seen(("port/wiiu/src/d3d8_gx2.c", "dk_stream_note_read((const char *)PLATFORM_PHYSICAL_TO_VIRTUAL(device.streams[stream].data) + first * stride,",
                          "if (0) dk_stream_note_read((const char *)PLATFORM_PHYSICAL_TO_VIRTUAL(device.streams[stream].data) + first * stride,"))

    def test_a_missed_texel_read_is_seen(self):
        # (a compressed texture's texels, read where the game keeps them, not noted: rewritten before the GX2 thread makes the image)
        self.hazard_seen(("port/wiiu/src/xbox_textures_gx2.c",
                          "\t\tdk_stream_note_read(base, face_size * (face_count - 1) + xgpu_texture_level_offset(description, description->levels));", "\t\t(void)0;"))

    def test_a_texture_stage_state_not_sent_changes_the_pictures(self):
        mutation = ("port/wiiu/src/d3d8_gx2.c", "\t\tuint32_t marked = texture_states_dirty[stage];",
                    "\t\tuint32_t marked = stage == 3 ? 0 : texture_states_dirty[stage];")
        reference, mutated = self.mutated_digests([], [("sync", "lag", 1)]) + self.mutated_digests([mutation], [("sync", "lag", 1)])
        self.assertNotEqual(reference, mutated)

    def test_a_setter_that_does_not_mark_its_state_is_found_at_the_frame_end(self):
        # (the frame's end compares the words no setter marked with the GX2 thread's copy, and logs any that differ)
        mutation = ("port/wiiu/src/d3d8_gx2.c", "{ render_state_mark(state); D3D__RenderState[state] = value; }", "{ D3D__RenderState[state] = value; }")
        with tempfile.TemporaryDirectory(prefix="opence-d3d8-replay-") as folder:
            values = replay(self.build_in(folder, [mutation]), "sync", "lag", 1, frames=60)
        self.assertNotEqual(values["digest"], DIGEST)
        self.assertNotEqual(values["states-missed"], "0")

    def test_targets_not_bound_again_after_setrendertarget_change_the_pictures(self):
        mutation = ("port/wiiu/src/d3d8_gx2.c", "\tdevice.depth_stencil = depth_stencil;\n\ttargets_dirty = TRUE;", "\tdevice.depth_stencil = depth_stencil;")
        reference, mutated = self.mutated_digests([], [("sync", "lag", 1)]) + self.mutated_digests([mutation], [("sync", "lag", 1)])
        self.assertNotEqual(reference, mutated)

    def test_facts_not_sent_when_they_change_change_the_pictures(self):
        mutation = ("port/wiiu/src/d3d8_gx2.c", "|| !facts_known) && (!facts_known || !words_equal(&facts_now, &facts_told, sizeof(facts_now))))",
                    "|| !facts_known) && !facts_known)")
        reference, mutated = self.mutated_digests([], [("sync", "lag", 1)]) + self.mutated_digests([mutation], [("sync", "lag", 1)])
        self.assertNotEqual(reference, mutated)

    def test_a_stage_remembered_past_what_its_texture_comes_from_changes_the_pictures(self):
        # (each serial a stage's facts are remembered under, left out: a write to the texels, the menus' art, a HUD placeholder, a render target noted,
        # the frame; and the text's atlas remembered, whose rows drawn since are sent at each lookup)
        faults = [("port/wiiu/src/d3d8_gx2.c", "serials[0] = memory_watch_serial();", "serials[0] = 0;"),
                  ("port/wiiu/src/d3d8_gx2.c", "serials[2] = menu_art_serial();", "serials[2] = 0;"),
                  ("port/wiiu/src/d3d8_gx2.c", "serials[3] = hud_hires_placeholder_serial();", "serials[3] = 0;"),
                  ("port/wiiu/src/d3d8_gx2.c", "serials[4] = rendered_serial;", "serials[4] = 0;"),
                  ("port/wiiu/src/xbox_textures_gx2.c", "*lasting = !palettized && !no_cache && !atlas;", "*lasting = !palettized && !no_cache;")]
        reference = self.mutated_digests([], [("sync", "lag", 1)])[0]
        for fault in faults:
            with self.subTest(fault=fault[1]):
                self.assertNotEqual(self.mutated_digests([fault], [("sync", "lag", 1)])[0], reference)

    def test_a_stage_remembered_across_frames_keeps_a_texture_from_counting_as_used(self):
        # (the texture cache drops a texture no draw has looked up for 1800 frames: the one draw of the light frames must look its texture up each frame)
        fault = ("port/wiiu/src/d3d8_gx2.c", "serials[5] = device.frame;", "serials[5] = 0;")
        with tempfile.TemporaryDirectory(prefix="opence-d3d8-replay-") as folder:
            values = replay(self.build_in(folder, [fault]), "sync", "lag", 1)
        self.assertNotEqual(values["digest"], DIGEST)


if __name__ == "__main__":
    unittest.main()
