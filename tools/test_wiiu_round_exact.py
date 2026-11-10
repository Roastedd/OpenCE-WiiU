"""halo_rint_long (port/linux/game/round_exact.h) and the colours' conversions to pixels (source/bitmaps/bitmaps_inlines.h) give the original's results on the Wii U
(port/wiiu/tests/test_round_exact.c: 300 million values of every kind against (long)__builtin_rint, 40 million colours against the conversions as they were: the same
pixels and the same number of failed checks). The three conversion functions are extracted from the header. Negative controls: the magic constant changed, the limit moved
past 2^52, a conversion that truncates, and a check that is not kept.

python3 -m unittest discover -s tools -p test_wiiu_round_exact.py
ROUND_TARGET=... ROUND_RUNNER=qemu-ppc runs it big-endian (CC=powerpc-linux-gnu-gcc in the opence-ppc-be image), compiled as the game is (-ffp-contract=off, no -ffast-math).
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "port/linux/game/round_exact.h"
INLINES = ROOT / "source/bitmaps/bitmaps_inlines.h"
TEST = ROOT / "port/wiiu/tests/test_round_exact.c"


def functions(text):
    start = text.index("__inline pixel32 real_argb_color_to_pixel32(")
    end = text.index("#endif // __BITMAPS_INLINES_H")
    return text[start:end]


class RoundExactTest(unittest.TestCase):
    def run_fixture(self, header=None, inlines=None):
        with tempfile.TemporaryDirectory(prefix="opence-round-") as folder:
            folder = Path(folder)
            (folder / "round_exact.h").write_text(header if header is not None else HEADER.read_text())
            (folder / "pixels_fast.inc").write_text(functions(inlines if inlines is not None else INLINES.read_text()))
            command = [os.environ.get("CC", "cc"), *shlex.split(os.environ.get("ROUND_TARGET", "")), "-std=gnu11", "-O2", "-Wall", "-Wno-unused-function",
                       "-ffp-contract=off", "-fno-strict-aliasing", "-I" + str(folder), str(TEST), "-lm", "-o", str(folder / "check")]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            return subprocess.run([*shlex.split(os.environ.get("ROUND_RUNNER", "")), str(folder / "check")], capture_output=True, text=True, timeout=3600)

    def mutated(self, text, before, after):
        self.assertEqual(text.count(before), 1, before)
        return text.replace(before, after)

    def test_same_results(self):
        result = self.run_fixture()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("every case matches", result.stdout)

    def test_a_wrong_constant_is_caught(self):
        result = self.run_fixture(header=self.mutated(HEADER.read_text(), "(value + 6755399441055744.0) - 6755399441055744.0", "(value + 6755399441055745.0) - 6755399441055745.0"))
        self.assertNotEqual(result.returncode, 0)

    def test_a_wider_limit_is_caught(self):
        header = self.mutated(HEADER.read_text(), "value > -2251799813685248.0 && value < 2251799813685248.0", "value > -4503599627370496.0 && value < 4503599627370496.0")
        self.assertNotEqual(self.run_fixture(header=header).returncode, 0)

    def test_a_truncating_conversion_is_caught(self):
        inlines = self.mutated(INLINES.read_text(), "long red = halo_rint_long((double)color->red * scale);", "long red = (long)((double)color->red * scale);")
        self.assertNotEqual(self.run_fixture(inlines=inlines).returncode, 0)

    def test_a_dropped_check_is_caught(self):
        inlines = self.mutated(INLINES.read_text(), "		match_assert(\n			\"..\\\\bitmaps\\\\bitmaps_inlines.h\",\n			188,\n			verify == result);\n", "")
        self.assertNotEqual(self.run_fixture(inlines=inlines).returncode, 0)


if __name__ == "__main__":
    unittest.main()
