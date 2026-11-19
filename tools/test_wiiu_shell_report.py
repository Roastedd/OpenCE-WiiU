"""port/wiiu/src/wiiu_shell_report.c (the one-file bug report, halo/make_report.txt) on its own (port/wiiu/tests/test_shell_report.c): the top of the last log, the end of
it (400 lines), the end of data/debug.txt (100 lines), the crash note, the black box (or that there is none), the close trail, all under headings; the file under about 200 KB
with long lines; an unwritable place an error with nothing left behind.

python3 -m unittest tools.test_wiiu_shell_report
"""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TEST = ROOT / "port/wiiu/tests/test_shell_report.c"
SOURCE = ROOT / "port/wiiu/src/wiiu_shell_report.c"


class ShellReportTest(unittest.TestCase):
    def test_the_report(self):
        with tempfile.TemporaryDirectory() as folder:
            check = Path(folder) / "check"
            built = subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", str(TEST), str(SOURCE), "-o", str(check)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            data = Path(folder) / "data"
            data.mkdir()
            result = subprocess.run([str(check), str(data)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("0 failures", result.stdout)


if __name__ == "__main__":
    unittest.main()
