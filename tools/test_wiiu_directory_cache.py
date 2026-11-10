"""port/linux/src/directory_cache.h (the Wii U's file layer keeps the names in the directories its path translation looks in) on its own, over a file layer in memory that
counts the directories opened and the entries read (port/wiiu/tests/test_directory_cache.c): the spelling a lookup answers with, one read serving every lookup of a
directory, a change or the time making it read again, the directories pushed out, a directory too big to keep, four threads. The negative control is the same checks over
lookups that read the directory each time (-DDIRECTORY_CACHE_OFF), which must fail them.

python3 -m unittest tools.test_wiiu_directory_cache
DIRECTORY_CACHE_TARGET='--target=powerpc-linux-gnu -fuse-ld=lld -no-pie' DIRECTORY_CACHE_RUNNER='qemu-ppc -L /usr/powerpc-linux-gnu' ... runs it big-endian (in the opence-ppc-be image).
"""
import os
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TEST = ROOT / "port/wiiu/tests/test_directory_cache.c"
INCLUDE = ROOT / "port/linux/src"


class DirectoryCacheTest(unittest.TestCase):
    def run_test(self, *flags):
        with tempfile.TemporaryDirectory() as folder:
            command = [os.environ.get("CC", "cc"), *shlex.split(os.environ.get("DIRECTORY_CACHE_TARGET", "")), "-std=gnu11", "-O2", "-Wall", "-Werror", "-pthread", *flags,
                       f"-I{INCLUDE}", str(TEST), "-o", str(Path(folder) / "check")]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            return subprocess.run([*shlex.split(os.environ.get("DIRECTORY_CACHE_RUNNER", "")), str(Path(folder) / "check")], capture_output=True, text=True)

    def test_the_names_of_a_directory_are_read_once_and_kept(self):
        result = self.run_test()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("0 failures", result.stdout)

    def test_lookups_that_read_the_directory_each_time_are_found(self):
        result = self.run_test("-DDIRECTORY_CACHE_OFF")
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("a hundred lookups opened", result.stdout)


if __name__ == "__main__":
    unittest.main()
