"""The Wii U's frame pacing (port/wiiu/src/wiiu_pacing.c): the statistics and the swap policy of halo/pacing.txt (port/wiiu/tests/test_pacing.c), and the display-clock
stamp and the locked 30 rule (port/wiiu/tests/test_pacing_stamp.c: a model of the display pipeline, and the rule run on frame streams made up for it). Each of the rules'
parts is also broken on purpose (a copy of wiiu_pacing.c with one line changed) and the test must then fail: the shrinkage that keeps the stamp from doing harm, the sign of
its move, the guard that keeps the picture from going back in time and inside its pair, the lock's dwell, its cap on switches, its limit of what a locked 30 holds and the
quiet seconds after a reset.

python3 -m unittest discover -s tools -p test_wiiu_pacing.py
"""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port/wiiu/src/wiiu_pacing.c"
HEADER = ROOT / "port/wiiu/src/wiiu_pacing.h"
TESTS = ROOT / "port/wiiu/tests"


def build_and_run(test, source_text=None):
    """test (a file of port/wiiu/tests) built with wiiu_pacing.c (or source_text in its place), run; (return code, output)"""
    with tempfile.TemporaryDirectory(prefix="opence-pacing-") as folder:
        folder = Path(folder)
        (folder / "src").mkdir()
        (folder / "tests").mkdir()
        shutil.copy(HEADER, folder / "src/wiiu_pacing.h")
        (folder / "src/wiiu_pacing.c").write_text(source_text if source_text is not None else SOURCE.read_text())
        shutil.copy(TESTS / test, folder / "tests" / test)
        command = [os.environ.get("CC", "cc"), *shlex.split(os.environ.get("PACING_TARGET", "")), "-std=gnu11", "-O2", "-Wall", "-Werror", "-ffp-contract=off",
                   str(folder / "tests" / test), str(folder / "src/wiiu_pacing.c"), "-lm", "-o", str(folder / "check")]
        built = subprocess.run(command, capture_output=True, text=True)
        if built.returncode:
            return built.returncode, built.stdout + built.stderr
        result = subprocess.run([*shlex.split(os.environ.get("PACING_RUNNER", "")), str(folder / "check")], capture_output=True, text=True, timeout=600)
        return result.returncode, result.stdout + result.stderr


def mutated(*changes):
    """wiiu_pacing.c with each (before, after) made once"""
    text = SOURCE.read_text()
    for before, after in changes:
        if text.count(before) != 1:
            raise LookupError(f"negative control no longer applies (found {text.count(before)} times): {before!r}")
        text = text.replace(before, after, 1)
    return text


class PacingTest(unittest.TestCase):
    def test_the_statistics_and_the_policy(self):
        code, output = build_and_run("test_pacing.c")
        self.assertEqual(code, 0, output)
        self.assertIn("pacing: all passed", output)

    def test_the_stamp_and_the_locked_30(self):
        code, output = build_and_run("test_pacing_stamp.c")
        self.assertEqual(code, 0, output)
        self.assertIn("pacing stamp and locked 30: all checks passed", output)

    def test_the_stamp_takes_out_judder_where_the_vsync_is_predictable_and_does_no_harm_where_it_is_not(self):
        code, output = build_and_run("test_pacing_stamp.c")
        self.assertEqual(code, 0, output)
        self.assertRegex(output, r"A steady 22 ms \+-0\.5: [0-9.]+ fps, judder rms 4\.\d\d ms as drawn, [12]\.\d\d stamped")


def display_model(works, depth, frames=3000, gpu_ms=4.0, seed=1):
    """The model of the display pipeline of halo/pacing.txt's depth (mode 3): a frame is built in works[k % len(works)] ms (+- 0.6), is ready gpu_ms after its swap, and flips at the
    first vsync after that, a refresh after the flip before at least; the producer is held while more than `depth` swaps wait for their flip. The share of flips that lasted more
    than 17.5 ms, and the frames a second."""
    import math
    import random
    refresh = 1000.0 / 60.0
    rnd = random.Random(seed)
    t = 0.0
    last_flip = 0.0
    pending = []
    flips = []
    for k in range(frames):
        z = t + max(4.0, works[k % len(works)] + rnd.gauss(0, 0.6))
        flip = math.ceil((z + gpu_ms) / refresh - 1e-9) * refresh
        flip = max(flip, last_flip + refresh)
        pending = [f for f in pending if f > z] + [flip]
        last_flip = flip
        flips.append(flip)
        t = z
        while len(pending) > depth:
            t = min(pending)
            pending = [f for f in pending if f > t]
    gaps = [flips[i + 1] - flips[i] for i in range(len(flips) - 1)]
    return sum(1 for g in gaps if g > 17.5) / len(gaps), 1000.0 * len(gaps) / (flips[-1] - flips[0])


class DeeperQueueModelTest(unittest.TestCase):
    """A model, not the console: a second swap in flight lets the game thread run a frame ahead of the display, so a slow frame after a fast one is absorbed."""

    def test_a_slow_frame_after_a_fast_one_is_absorbed_by_a_second_swap(self):
        one_late, one_fps = display_model([10, 26], 1)
        two_late, two_fps = display_model([10, 26], 2)
        self.assertGreater(one_late, 0.10)
        self.assertLess(two_late, 0.6 * one_late)
        self.assertGreater(two_fps, one_fps + 2.0)

    def test_a_steady_frame_gains_nothing_and_loses_nothing(self):
        for works in ([14, 14], [30, 30]):
            one_late, one_fps = display_model(works, 1)
            two_late, two_fps = display_model(works, 2)
            self.assertAlmostEqual(one_fps, two_fps, delta=0.5)
            self.assertAlmostEqual(one_late, two_late, delta=0.02)


CONTROLS = {
    # the stamp
    "the shrinkage is left out (the move is taken as the prediction gives it)": (("stamp_clamp(stamp->covariance / stamp->variance, 0.0, 1.0)", "1.0"),),
    "the move has the wrong sign": (("frame->correction_ms = stamp_clamp(stamp->shrink * frame->predicted_deviation_ms,",
                                     "frame->correction_ms = stamp_clamp(-stamp->shrink * frame->predicted_deviation_ms,"),),
    "the picture may go back in time": (("		moved = stamp->shown_ms;\n		stamp->window_backward++;", "		stamp->window_backward++;"),),
    "the picture may go past its pair": (("		moved = base + span;\n		stamp->window_clamped++;", "		stamp->window_clamped++;"),
                                         ("return stamp_clamp((moved - base) / span, 0.0, 1.0);", "return (moved - base) / span;")),
    # the locked 30
    "no dwell": (("|| now_ms - lock->since_ms < set->dwell_s * 1000.0)", ")"),),
    "no cap on the switches": (("	if (recent >= set->max_switches)\n		return;\n", ""),),
    "a locked 30 that would not hold is locked": ((" && (double)over_hold <= 0.20 * (double)frames)", ")"),),
    "no quiet seconds after a reset": (("if (now_ms - lock->started_ms < set->quiet_s * 1000.0 ||", "if ("),),
}


class PacingNegativeControlsTest(unittest.TestCase):
    def test_each_broken_rule_is_caught(self):
        for name, changes in CONTROLS.items():
            with self.subTest(name):
                code, output = build_and_run("test_pacing_stamp.c", mutated(*changes))
                self.assertNotEqual(code, 0, f"{name}: the test still passes\n{output}")


if __name__ == "__main__":
    unittest.main()
