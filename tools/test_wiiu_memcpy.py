"""The Wii U's memcpy and memset (port/wiiu/src/wiiu_memcpy.c), its own self-test run big-endian under qemu-ppc, where dcbz clears a 32-byte line as the
Espresso's does: a copy onto itself (a = *f(&a), which the game's code does) and one to a lower address over its source come out as a forward copy's.

MEMCPY_TARGET='--target=powerpc-linux-gnu -fuse-ld=lld -no-pie' MEMCPY_RUNNER='qemu-ppc -cpu 750 -L /usr/powerpc-linux-gnu' (verify.sh step 7); with no
target the test is skipped (the copy is PowerPC assembly).
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port/wiiu/src/wiiu_memcpy.c"

DRIVER = r'''
#include <stdint.h>
#include <stdio.h>

int wiiu_memcpy_selftest(void);
extern volatile int wiiu_memcpy_touch;

int main(void)
{
	static uint8_t probe[64] __attribute__((aligned(32)));
	int wrong;

	/* (the self-test's buffers are statics, as this one: in the range dcbz is used on, else nothing here tests it) */
	if ((uintptr_t)probe < 0x10000000u || (uintptr_t)probe >= 0x50000000u)
	{
		printf("statics at %p, outside the range dcbz is used on\n", (void *)probe);
		return 2;
	}
	wiiu_memcpy_touch = TOUCH; /* (halo/memcpy_touch.txt: the copy's dcbt on or left out; the copy's bytes are the same) */
	wrong = wiiu_memcpy_selftest();
	printf("memcpy/memset self-test: %d wrong\n", wrong);
	return wrong ? 1 : 0;
}
'''

FAULTS = {
    # (the copy as it was: a line of the destination cleared before the source's bytes in it are read)
    "no_overlap_check": ("zeroable(destination) && !overlapping(destination, source, size))", "zeroable(destination))"),
    # (only a copy onto itself taken for one: one to a lower address over its source still cleared)
    "same_address_only": ("return d < s + size && s < d + size;", "return d == s;"),
}


@unittest.skipUnless(os.environ.get("MEMCPY_TARGET"), "PowerPC only: MEMCPY_TARGET and MEMCPY_RUNNER (qemu-ppc)")
class WiiUMemcpyTest(unittest.TestCase):
    def run_selftest(self, fault=None, touch=1):
        source = SOURCE.read_text()
        if fault:
            before, after = FAULTS[fault]
            self.assertEqual(source.count(before), 1, before)
            source = source.replace(before, after, 1)
        with tempfile.TemporaryDirectory(prefix="opence-memcpy-") as folder:
            folder = Path(folder)
            (folder / "wiiu_memcpy.c").write_text(source)
            (folder / "driver.c").write_text(DRIVER)
            # (the copy under test named apart from the C library's, which the driver's printf uses)
            built = subprocess.run([os.environ.get("CC", "cc"), *shlex.split(os.environ["MEMCPY_TARGET"]), "-std=gnu11", "-O2",
                                    "-ffreestanding", "-fno-builtin", "-Dmemcpy=wiiu_test_memcpy", "-Dmemset=wiiu_test_memset", f"-DTOUCH={touch}",
                                    str(folder / "wiiu_memcpy.c"), str(folder / "driver.c"), "-o", str(folder / "check")],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            return subprocess.run([*shlex.split(os.environ.get("MEMCPY_RUNNER", "")), str(folder / "check")],
                                  capture_output=True, text=True, timeout=120)

    def test_selftest_passes(self):
        result = self.run_selftest()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("self-test: 0 wrong", result.stdout)

    def test_selftest_passes_without_the_copys_dcbt(self):
        result = self.run_selftest(touch=0)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("self-test: 0 wrong", result.stdout)

    def test_selftest_finds_each_fault(self):
        for fault in FAULTS:
            with self.subTest(fault=fault):
                result = self.run_selftest(fault)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertNotIn("self-test: 0 wrong", result.stdout)


if __name__ == "__main__":
    unittest.main()
