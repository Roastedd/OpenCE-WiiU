"""port/wiiu/src/wiiu_sqrtf.c gives the same float as a correctly rounded sqrtf: every one of the 2^32 inputs natively (about 15 s), every 7919th
under SQRTF_RUNNER (qemu-ppc: the whole range takes half an hour there).

python3 -m unittest discover -s tools -p test_wiiu_sqrtf.py
SQRTF_TARGET='--target=powerpc-linux-gnu -fuse-ld=lld -no-pie' SQRTF_RUNNER=qemu-ppc ... runs it big-endian.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port/wiiu/src/wiiu_sqrtf.c"
TEST = ROOT / "port/wiiu/tests/test_sqrtf.c"


class SqrtfTest(unittest.TestCase):
    def build(self, folder, source_text=None):
        source = SOURCE
        if source_text is not None:
            source = Path(folder) / "wiiu_sqrtf.c"
            source.write_text(source_text)
        command = [os.environ.get("CC", "cc"), *shlex.split(os.environ.get("SQRTF_TARGET", "")), "-std=gnu11", "-O2", "-Wall", "-Werror",
                   "-ffp-contract=off", "-DWIIU_SQRTF_TEST", str(TEST), str(source), "-lm", "-o", str(Path(folder) / "check")]
        built = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        return Path(folder) / "check"

    def run_check(self, check, sampled):
        runner = shlex.split(os.environ.get("SQRTF_RUNNER", ""))
        return subprocess.run([*runner, str(check), *(["sampled"] if sampled else [])], capture_output=True, text=True, timeout=3600)

    def test_every_input(self):
        with tempfile.TemporaryDirectory(prefix="opence-sqrtf-") as folder:
            result = self.run_check(self.build(folder), sampled=bool(os.environ.get("SQRTF_RUNNER")))
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("every input matches", result.stdout)

    def test_a_wrong_correction_is_caught(self):
        # (the halfway-point check moves the root only when the rounded double is a float away: with three Newton steps the
        # estimate is 0.0006 of a place out, so seldom; it is what makes the result correct by construction rather than by this test. A check the wrong way round is caught.)
        text = SOURCE.read_text()
        before = "if (above * above < d)"
        self.assertEqual(text.count(before), 1)
        with tempfile.TemporaryDirectory(prefix="opence-sqrtf-") as folder:
            result = self.run_check(self.build(folder, text.replace(before, "if (above * above > d)")), sampled=True)
            self.assertNotEqual(result.returncode, 0)

    def test_too_few_newton_steps_are_caught(self):
        text = SOURCE.read_text()
        step = "\ty = y * (1.5 - 0.5 * scaled * y * y);\n"
        self.assertEqual(text.count(step), 3)
        with tempfile.TemporaryDirectory(prefix="opence-sqrtf-") as folder:
            result = self.run_check(self.build(folder, text.replace(step * 3, step * 2)), sampled=True)
            self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
