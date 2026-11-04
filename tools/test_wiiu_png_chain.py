"""port/wiiu/src/wiiu_png_chain.h (the texel work of the Wii U's texture cache, done a word at a time) makes the bytes of the loops it replaced.

python3 -m unittest discover -s tools -p test_wiiu_png_chain.py
PNG_CHAIN_TARGET='--target=powerpc-linux-gnu -fuse-ld=lld -no-pie' PNG_CHAIN_RUNNER=qemu-ppc ... runs it big-endian (the console's byte order; in the
opence-ppc-be image: qemu-ppc needs -L /usr/powerpc-linux-gnu, PNG_CHAIN_RUNNER='qemu-ppc -L /usr/powerpc-linux-gnu').
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "port/wiiu/src/wiiu_png_chain.h"
TEST = ROOT / "port/wiiu/tests/test_png_chain.c"


class PngChainTest(unittest.TestCase):
    def run_test(self, header_text=None):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            (folder / "src").mkdir()
            (folder / "tests").mkdir()
            (folder / "src/wiiu_png_chain.h").write_text(header_text if header_text is not None else HEADER.read_text())
            (folder / "tests/test_png_chain.c").write_text(TEST.read_text())
            command = [os.environ.get("CC", "cc"), *shlex.split(os.environ.get("PNG_CHAIN_TARGET", "")), "-std=gnu11", "-O2", "-Wall", "-Werror",
                       str(folder / "tests/test_png_chain.c"), "-o", str(folder / "check")]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            return subprocess.run([*shlex.split(os.environ.get("PNG_CHAIN_RUNNER", "")), str(folder / "check")], capture_output=True, text=True)

    def test_every_function_gives_the_old_loops_bytes(self):
        result = self.run_test()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("0 failures", result.stdout)

    def test_a_mean_that_rounds_down_is_found(self):
        broken = HEADER.read_text().replace("+ 0x00020002u;\n\tconst uint32_t odd", "+ 0x00010001u;\n\tconst uint32_t odd", 1)
        self.assertNotEqual(broken, HEADER.read_text())
        self.assertNotEqual(self.run_test(broken).returncode, 0)

    def test_a_swap_that_leaves_the_bytes_is_found(self):
        broken = HEADER.read_text().replace("texels[index] = (v & 0x00FF00FFu) | ((v >> 16) & 0x0000FF00u) | ((v << 16) & 0xFF000000u);", "texels[index] = v;", 1)
        broken = broken.replace("texels[index] = (v & 0xFF00FF00u) | ((v >> 16) & 0x000000FFu) | ((v << 16) & 0x00FF0000u);", "texels[index] = v;", 1)
        self.assertNotEqual(broken, HEADER.read_text())
        self.assertNotEqual(self.run_test(broken).returncode, 0)


if __name__ == "__main__":
    unittest.main()
