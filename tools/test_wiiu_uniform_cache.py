"""Exact command/payload equivalence against the frozen legacy conversion.

python3 -m unittest discover -s tools -p test_wiiu_uniform_cache.py
UNIFORM_CACHE_TARGET='--target=powerpc-linux-gnu -fuse-ld=lld -no-pie' \
UNIFORM_CACHE_RUNNER=qemu-ppc ... runs the same fixture big-endian.
"""
import hashlib
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from harness import function

SOURCE = ROOT / "port/wiiu/src/dk_derive.c"
HEADER = ROOT / "port/wiiu/src/wiiu_uniform_cache.h"
TEST = ROOT / "port/wiiu/tests/test_uniform_cache.c"
LEGACY = ROOT / "port/wiiu/tests/uniform_legacy.inc"


def current_code():
    """dk_derive.c's input capture and uniform step, as the GX2 thread runs them"""
    source = SOURCE.read_text()
    return function(source, "parameter_inputs") + "\n" + function(source, "derive_parameters")


class UniformCacheTest(unittest.TestCase):
    def run_fixture(self, header=None, current=None, sanitizer=False):
        with tempfile.TemporaryDirectory(prefix="opence-uniform-cache-") as folder:
            folder = Path(folder)
            (folder / "wiiu_uniform_cache.h").write_text(HEADER.read_text() if header is None else header)
            (folder / "current_uniform.inc").write_text(current_code() if current is None else current)
            command = [os.environ.get("CC", "cc"), *shlex.split(os.environ.get("UNIFORM_CACHE_TARGET", "")),
                       "-std=gnu11", "-O2", "-g", "-Wall", "-Wextra", "-Werror", "-ffp-contract=off",
                       "-fno-strict-aliasing", "-I" + str(folder), "-I" + str(HEADER.parent),
                       str(TEST), "-o", str(folder / "check")]
            if sanitizer:
                command[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            return subprocess.run([*shlex.split(os.environ.get("UNIFORM_CACHE_RUNNER", "")), str(folder / "check")],
                                  capture_output=True, text=True, timeout=30)

    def test_actual_input_capture_conversion_and_owned_commands(self):
        result = self.run_fixture()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("76-word mutations", result.stdout)

    @unittest.skipIf(bool(os.environ.get("UNIFORM_CACHE_TARGET")), "sanitizer uses the native host")
    def test_native_address_and_undefined_sanitizers(self):
        result = self.run_fixture(sanitizer=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_frozen_legacy_oracle_has_not_changed(self):
        text = LEGACY.read_text()
        expected = re.search(r"Function SHA-256: ([0-9a-f]{64})", text).group(1)
        legacy = function(text, "draw_parameters_send")
        self.assertEqual(hashlib.sha256(legacy.encode()).hexdigest(), expected)

    def test_missing_dependency_is_detected(self):
        header = HEADER.read_text()
        before = "wiiu_uniform_group(cache, input, 43, 1)"
        self.assertEqual(header.count(before), 1)
        result = self.run_fixture(header=header.replace(before, "wiiu_uniform_group(cache, input, 42, 1)", 1))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("uniform check failed", result.stderr)

    def test_changed_color_arithmetic_is_detected(self):
        header = HEADER.read_text()
        before = "value[0] = ((color >> 16) & 0xff) / 255.0f;"
        self.assertEqual(header.count(before), 1)
        result = self.run_fixture(header=header.replace(before, before.replace("255.0f", "256.0f"), 1))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("uniform check failed", result.stderr)

    def test_signed_zero_key_omission_is_detected(self):
        header = HEADER.read_text()
        before = "different |= cache->input[first + i] ^ wiiu_uniform_word(input + (first + i) * 4u);"
        self.assertEqual(header.count(before), 1)
        after = "different |= (cache->input[first + i] ^ wiiu_uniform_word(input + (first + i) * 4u)) & UINT32_C(0x7fffffff);"
        result = self.run_fixture(header=header.replace(before, after, 1))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("uniform check failed", result.stderr)

    def test_gpu_forget_on_cpu_cache_hit_is_detected(self):
        current = current_code()
        self.assertEqual(current.count("if (first ||"), 2)
        result = self.run_fixture(current=current.replace("if (first ||", "if (first * 0 ||"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("uniform check failed", result.stderr)


if __name__ == "__main__":
    unittest.main()
