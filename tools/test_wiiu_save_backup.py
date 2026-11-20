"""port/linux/game/save_backup.c (the rolling backup of the saved game and the profiles, halo/save_backup.txt) on its own, over POSIX files
(port/wiiu/tests/test_save_backup.c): a good file kept as NAME.bak (renamed or copied), a file cut short or failing its check leaving the backup alone, the backup
swapped in for a saved game that fails its checksum and everything put back if the backup fails too, a profile's backup read. With the switch off (HALO_SAVE_BACKUP=0)
no file is touched.

python3 -m unittest tools.test_wiiu_save_backup
"""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TEST = ROOT / "port/wiiu/tests/test_save_backup.c"


class SaveBackupTest(unittest.TestCase):
    def build(self, folder):
        check = Path(folder) / "check"
        built = subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-Wall", "-Werror", "-Wno-unused-function", str(TEST), "-o", str(check)], capture_output=True, text=True)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        return check

    def test_the_backup_is_kept_and_used(self):
        with tempfile.TemporaryDirectory() as folder:
            data = Path(folder) / "data"
            data.mkdir()
            result = subprocess.run([str(self.build(folder)), str(data)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("0 failures", result.stdout)

    def test_switched_off_nothing_is_touched(self):
        with tempfile.TemporaryDirectory() as folder:
            data = Path(folder) / "data"
            data.mkdir()
            result = subprocess.run([str(self.build(folder)), str(data)], capture_output=True, text=True, env={**os.environ, "HALO_SAVE_BACKUP": "0"})
            self.assertNotEqual(result.returncode, 0)  # the checks of the backup fail: none is made
            self.assertIn("no backup", result.stdout)
            self.assertEqual([p.name for p in data.glob("*.bak")], [])


if __name__ == "__main__":
    unittest.main()
