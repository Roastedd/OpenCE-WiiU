"""periodic_function_evaluate with the Wii U's integer-part fast path (source/math/periodic_functions.c, under HALO_WIIU) gives the original's bits for every time
(port/wiiu/tests/test_periodic_exact.c: 60 million random times of every kind, with random tables, all function types and some that are none). The function is extracted
from its source and compiled with and without HALO_WIIU. Three negative controls: the fast path taken for negative times, its limit moved far past 2^31, and a fraction that
rounds.

python3 -m unittest discover -s tools -p test_wiiu_periodic_exact.py
PERIODIC_TARGET=... PERIODIC_RUNNER=qemu-ppc runs it big-endian (CC=powerpc-linux-gnu-gcc in the opence-ppc-be image), compiled as the game is (-ffp-contract=off).
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "source/math/periodic_functions.c"
TEST = ROOT / "port/wiiu/tests/test_periodic_exact.c"


def function_text(path, signature):
    text = path.read_text()
    start = text.index(signature)
    end = text.index("\n}\n", start) + 3
    return text[start:end]


class PeriodicExactTest(unittest.TestCase):
    def run_fixture(self, mutate=None):
        with tempfile.TemporaryDirectory(prefix="opence-periodic-") as folder:
            folder = Path(folder)
            text = function_text(SOURCE, "real periodic_function_evaluate(")
            if mutate:
                before, after = mutate
                self.assertEqual(text.count(before), 1, before)
                text = text.replace(before, after)
            (folder / "periodic.inc").write_text(text)
            command = [os.environ.get("CC", "cc"), *shlex.split(os.environ.get("PERIODIC_TARGET", "")), "-std=gnu11", "-O2", "-Wall", "-Wno-unused-function",
                       "-ffp-contract=off", "-fno-strict-aliasing", "-I" + str(folder), str(TEST), "-lm", "-o", str(folder / "check")]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            return subprocess.run([*shlex.split(os.environ.get("PERIODIC_RUNNER", "")), str(folder / "check")], capture_output=True, text=True, timeout=3600)

    def test_same_bits(self):
        result = self.run_fixture()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("every case matches", result.stdout)

    def test_a_negative_time_on_the_fast_path_is_caught(self):
        result = self.run_fixture(("if (time >= 0.0f && time < 2147483648.0f)", "if (time < 2147483648.0f)"))
        self.assertNotEqual(result.returncode, 0)

    def test_a_wider_limit_is_caught(self):
        result = self.run_fixture(("if (time >= 0.0f && time < 2147483648.0f)", "if (time >= 0.0f && time < 1.0e30f)"))
        self.assertNotEqual(result.returncode, 0)

    def test_a_rounding_fraction_is_caught(self):
        result = self.run_fixture(("fraction = time - (real)whole;", "fraction = (real)((double)time - (double)whole) * 0.99999f;"))
        self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
