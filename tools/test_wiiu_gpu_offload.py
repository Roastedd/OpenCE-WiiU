"""halo/gpu_offload.txt's parser (port/wiiu/src/wiiu_gpu_offload.c) and the blends' arithmetic without square roots (port/linux/game/render_interpolation_fast.h)
against the original arithmetic of render_interpolation.c, over 400,000 random bases (160,000 rotations): the same decision "a rotation or not", the quaternion
within 3e-6 and the normalised blend within 3e-7. Native (1 / sqrtf) and, as the console runs it, big-endian PowerPC with frsqrte and three Newton steps:

python3 -m unittest discover -s tools -p test_wiiu_gpu_offload.py
GPU_OFFLOAD_TARGET='--target=powerpc-linux-gnu -fuse-ld=lld -no-pie' GPU_OFFLOAD_RUNNER=qemu-ppc ... runs the PowerPC one (in the opence-wiiu container).
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
TEST = ROOT / "port/wiiu/tests/test_interpolation_fast.c"
SOURCE = ROOT / "port/wiiu/src/wiiu_gpu_offload.c"
HEADER = ROOT / "port/linux/game/render_interpolation_fast.h"


class InterpolationFastTest(unittest.TestCase):
    def build(self, folder, header_text=None):
        include = ROOT / "port/linux/game"
        if header_text is not None:
            include = Path(folder) / "game"
            include.mkdir()
            (include / HEADER.name).write_text(header_text)
        command = [os.environ.get("CC", "cc"), *shlex.split(os.environ.get("GPU_OFFLOAD_TARGET", "")), "-std=gnu11", "-O2", "-Wall", "-Werror",
                   "-ffp-contract=off", "-DWIIU_GPU_OFFLOAD_TEST", "-I" + str(include), str(TEST), str(SOURCE), "-lm", "-o", str(Path(folder) / "check")]
        built = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        return Path(folder) / "check"

    def run_check(self, check):
        runner = shlex.split(os.environ.get("GPU_OFFLOAD_RUNNER", ""))
        return subprocess.run([*runner, str(check)], capture_output=True, text=True, timeout=1800)

    def test_matches_the_original_arithmetic(self):
        with tempfile.TemporaryDirectory(prefix="opence-gpu-offload-") as folder:
            result = self.run_check(self.build(folder))
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("all match", result.stdout)

    def test_a_wrong_branch_is_caught(self):
        # (the quaternion of a basis whose trace is not positive: the w of the second branch taken from the wrong difference)
        text = HEADER.read_text()
        before = "*w = (m21 - m12) * inverse_s;\n\t\t*i = quarter_s;"
        self.assertEqual(text.count(before), 1)
        with tempfile.TemporaryDirectory(prefix="opence-gpu-offload-") as folder:
            result = self.run_check(self.build(folder, text.replace(before, "*w = (m12 - m21) * inverse_s;\n\t\t*i = quarter_s;")))
            self.assertNotEqual(result.returncode, 0)

    def test_a_wrong_newton_step_is_caught(self):
        # (qemu's frsqrte is nearly exact, so a missing step does not show there; a step that is wrong does. Native has no steps: 1 / sqrtf.)
        if "powerpc" not in os.environ.get("GPU_OFFLOAD_TARGET", ""):
            self.skipTest("no PowerPC target")
        text = HEADER.read_text()
        step = "\ty = y * (1.5f - 0.5f * x * y * y);\n"
        self.assertEqual(text.count(step), 3)
        with tempfile.TemporaryDirectory(prefix="opence-gpu-offload-") as folder:
            result = self.run_check(self.build(folder, text.replace(step, step.replace("1.5f", "1.4f"))))
            self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
