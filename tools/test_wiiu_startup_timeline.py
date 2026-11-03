"""port/linux/game/startup_timeline.c (the Wii U's start-up timeline) on its own: marks, notes and sums from several threads, the report's lines within the log's
240 bytes, and nothing made after the report.

python3 -m unittest discover -s tools -p test_wiiu_startup_timeline.py
STARTUP_TIMELINE_TARGET='--target=powerpc-linux-gnu -fuse-ld=lld -no-pie' STARTUP_TIMELINE_RUNNER='qemu-ppc -L /usr/powerpc-linux-gnu' ... runs it big-endian.
"""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
TEST = ROOT / "port/wiiu/tests/test_startup_timeline.c"
SOURCE = ROOT / "port/linux/game/startup_timeline.c"
HEADER = ROOT / "port/linux/include/halo_startup.h"


class StartupTimelineTest(unittest.TestCase):
    def run_test(self, source_text=None):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            # (the header alone on the include path: port/linux/include has stand-ins for the C library's)
            (folder / "include").mkdir()
            shutil.copy(HEADER, folder / "include/halo_startup.h")
            (folder / "linux/game").mkdir(parents=True)
            (folder / "wiiu/tests").mkdir(parents=True)
            (folder / "linux/game/startup_timeline.c").write_text(source_text if source_text is not None else SOURCE.read_text())
            shutil.copy(TEST, folder / "wiiu/tests/test_startup_timeline.c")
            command = [os.environ.get("CC", "cc"), *shlex.split(os.environ.get("STARTUP_TIMELINE_TARGET", "")), "-std=gnu11", "-O2", "-Wall", "-Werror", "-pthread",
                       f"-I{folder / 'include'}", str(folder / "wiiu/tests/test_startup_timeline.c"), "-o", str(folder / "check")]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            return subprocess.run([*shlex.split(os.environ.get("STARTUP_TIMELINE_RUNNER", "")), str(folder / "check")], capture_output=True, text=True)

    def test_the_report_has_the_marks_the_events_and_the_work(self):
        result = self.run_test()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("0 failures", result.stdout)

    def test_a_report_that_forgets_the_work_is_found(self):
        broken = SOURCE.read_text().replace("if (work_count)", "if (0)", 1)
        self.assertNotEqual(broken, SOURCE.read_text())
        self.assertNotEqual(self.run_test(broken).returncode, 0)


if __name__ == "__main__":
    unittest.main()
