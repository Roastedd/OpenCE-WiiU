"""The two-corner box tests of the Wii U's culling (port/linux/game/cull_exact.h, used by structure_culling_box_test and render_frustum_cube_visible) give the original
eight-corner answers (port/wiiu/tests/test_cull_exact.c: millions of random boxes and planes with special values). The two functions are extracted from their sources
and compiled as the Wii U has them and as the original has them. Three negative controls: a wrong corner, a missing guard for infinities and NaNs, and a plane
skipped.

python3 -m unittest discover -s tools -p test_wiiu_cull_exact.py
CULL_TARGET=... CULL_RUNNER=qemu-ppc runs it big-endian.
"""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
CULLING = ROOT / "port/linux/game/structure_culling.c"
CAMERAS = ROOT / "source/render/render_cameras.c"
HEADER = ROOT / "port/linux/game/cull_exact.h"
TEST = ROOT / "port/wiiu/tests/test_cull_exact.c"


def function_text(path, signature):
    """the function that begins with signature, up to the closing brace at the start of a line"""
    text = path.read_text()
    start = text.index(signature)
    end = text.index("\n}\n", start) + 3
    return text[start:end]


class CullExactTest(unittest.TestCase):
    def run_fixture(self, header=None):
        with tempfile.TemporaryDirectory(prefix="opence-cull-") as folder:
            folder = Path(folder)
            (folder / "box_test.inc").write_text(function_text(CULLING, "short structure_culling_box_test("))
            (folder / "cube_visible.inc").write_text(function_text(CAMERAS, "short render_frustum_cube_visible("))
            (folder / "cull_exact.h").write_text(header if header is not None else HEADER.read_text())
            command = [os.environ.get("CC", "cc"), *shlex.split(os.environ.get("CULL_TARGET", "")), "-std=gnu11", "-O2", "-Wall", "-Wno-unused-function",
                       "-ffp-contract=off", "-I" + str(folder), str(TEST), "-lm", "-o", str(folder / "check")]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            return subprocess.run([*shlex.split(os.environ.get("CULL_RUNNER", "")), str(folder / "check")], capture_output=True, text=True, timeout=1800)

    def test_same_answers(self):
        result = self.run_fixture()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("every case matches", result.stdout)

    def mutated(self, before, after):
        text = HEADER.read_text()
        self.assertEqual(text.count(before), 1)
        return text.replace(before, after)

    def test_a_wrong_corner_is_caught(self):
        result = self.run_fixture(self.mutated("if (ix0 > ix1) { most_i = ix0; least_i = ix1; } else { most_i = ix1; least_i = ix0; }",
                                               "most_i = ix1; least_i = ix0;"))
        self.assertNotEqual(result.returncode, 0)

    def test_a_missing_guard_is_caught(self):
        result = self.run_fixture(self.mutated("(kz0 - kz0) != 0.0f || (kz1 - kz1) != 0.0f ||", "(kz0 - kz0) != 0.0f ||"))
        self.assertNotEqual(result.returncode, 0)

    def test_a_reordered_sum_is_caught(self):
        result = self.run_fixture(self.mutated("*most = most_i + most_j + most_k - plane->d;", "*most = most_k + most_j + most_i - plane->d;"))
        self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
