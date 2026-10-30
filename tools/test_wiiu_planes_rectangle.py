"""structure_visibility.c's Wii U planes_intersect_rectangle (two corners a plane) gives the original eight-corner answer
(port/wiiu/tests/test_planes_rectangle.c, 20 million random cases with special values).

python3 -m unittest discover -s tools -p test_wiiu_planes_rectangle.py
PLANES_TARGET=... PLANES_RUNNER=qemu-ppc runs it big-endian.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "source/structures/structure_visibility.c"
TEST = ROOT / "port/wiiu/tests/test_planes_rectangle.c"
BEGIN = "#ifdef HALO_WIIU\n/* port: on the Wii U each plane is tested at the two corners"


def extract(text):
    """the Wii U block and the original body that follows its #endif, compiled as the Wii U has them"""
    start = text.index(BEGIN)
    end = text.index("\treturn _intersection_in;\n}\n", start) + len("\treturn _intersection_in;\n}\n")
    return "#define HALO_WIIU\n" + text[start:end]


class PlanesRectangleTest(unittest.TestCase):
    def run_fixture(self, block):
        with tempfile.TemporaryDirectory(prefix="opence-planes-") as folder:
            folder = Path(folder)
            (folder / "planes_rectangle.inc").write_text(block)
            command = [os.environ.get("CC", "cc"), *shlex.split(os.environ.get("PLANES_TARGET", "")), "-std=gnu11", "-O2", "-Wall",
                       "-ffp-contract=off", "-I" + str(folder), str(TEST), "-lm", "-o", str(folder / "check")]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            return subprocess.run([*shlex.split(os.environ.get("PLANES_RUNNER", "")), str(folder / "check")],
                                  capture_output=True, text=True, timeout=1800)

    def test_same_answers(self):
        result = self.run_fixture(extract(SOURCE.read_text()))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("every case matches", result.stdout)

    def test_a_wrong_corner_is_caught(self):
        block = extract(SOURCE.read_text())
        before = "if (ix0 > ix1) { most_i = ix0; least_i = ix1; } else { most_i = ix1; least_i = ix0; }"
        self.assertEqual(block.count(before), 1)
        result = self.run_fixture(block.replace(before, "most_i = ix1; least_i = ix0;"))
        self.assertNotEqual(result.returncode, 0)

    def test_a_missing_fallback_is_caught(self):
        block = extract(SOURCE.read_text())
        before = "return planes_intersect_rectangle_corners(bounds, cull_plane_count, cull_planes);"
        self.assertEqual(block.count(before), 1)
        result = self.run_fixture(block.replace(before, "(void)0;", 1))
        self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
