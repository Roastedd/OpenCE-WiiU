"""port/wiiu/src/wiiu_texture_layout.h (the levels of a Wii U texture: a 3D texture's depth halves a level at a time, so the 32 x 32 x 32 volume of the dynamic
lights' distance attenuation fits the 349440 bytes GX2 gave it; a 2D texture the heap has no block for is made without its top levels) on the numbers of the
console's logs.

python3 -m unittest discover -s tools -p test_wiiu_texture_layout.py
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "port/wiiu/src/wiiu_texture_layout.h"
TEST = ROOT / "port/wiiu/tests/test_texture_layout.c"


class TextureLayoutTest(unittest.TestCase):
    def run_test(self, header_text=None):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            (folder / "src").mkdir()
            (folder / "tests").mkdir()
            (folder / "src/wiiu_texture_layout.h").write_text(header_text if header_text is not None else HEADER.read_text())
            (folder / "tests/test_texture_layout.c").write_text(TEST.read_text())
            command = [os.environ.get("CC", "cc"), *shlex.split(os.environ.get("TEXTURE_LAYOUT_TARGET", "")), "-std=gnu11", "-O2", "-Wall", "-Werror",
                       str(folder / "tests/test_texture_layout.c"), "-o", str(folder / "check")]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            return subprocess.run([*shlex.split(os.environ.get("TEXTURE_LAYOUT_RUNNER", "")), str(folder / "check")], capture_output=True, text=True)

    def test_the_console_numbers_hold(self):
        result = self.run_test()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("0 failures", result.stdout)

    def test_a_volume_checked_with_its_first_levels_depth_is_found(self):
        broken = HEADER.read_text().replace("return wiiu_texture_level_size(depth ? depth : 1, level);", "return depth ? depth : 1;", 1)
        self.assertNotEqual(broken, HEADER.read_text())
        self.assertNotEqual(self.run_test(broken).returncode, 0)

    def test_a_floor_that_lets_a_mask_shrink_to_nothing_is_found(self):
        broken = HEADER.read_text().replace("#define WIIU_TEXTURE_SMALLEST_SIDE 256u", "#define WIIU_TEXTURE_SMALLEST_SIDE 1u", 1)
        self.assertNotEqual(broken, HEADER.read_text())
        self.assertNotEqual(self.run_test(broken).returncode, 0)

    def test_a_volume_that_may_be_cut_is_found(self):
        broken = HEADER.read_text().replace("if (kind != WIIU_TEXTURE_KIND_2D || skip == 0 || skip >= levels)", "if (skip == 0 || skip >= levels)", 1)
        self.assertNotEqual(broken, HEADER.read_text())
        self.assertNotEqual(self.run_test(broken).returncode, 0)

    def test_an_overflowing_fit_is_found(self):
        broken = HEADER.read_text().replace("(uint64_t)offset + (uint64_t)slice_bytes * slices <= total", "offset + slice_bytes * slices <= total", 1)
        self.assertNotEqual(broken, HEADER.read_text())
        self.assertNotEqual(self.run_test(broken).returncode, 0)


if __name__ == "__main__":
    unittest.main()
