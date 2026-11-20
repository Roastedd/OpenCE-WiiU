"""game.wiiu_auto_continue's decision (port/wiiu/src/wiiu_auto_continue.c) apart from the console: it goes on once, after the main menu has been up for a while,
and never with the setting off, after a crash note, or once a controller button has been held or pressed."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AutoContinueTests(unittest.TestCase):
    def test_the_decision(self):
        with tempfile.TemporaryDirectory(prefix='halo-autocontinue-') as directory:
            program = Path(directory) / 'test'
            built = subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-DWIIU_AUTO_CONTINUE_TEST=1',
                                    str(ROOT / 'port/wiiu/tests/test_auto_continue.c'), str(ROOT / 'port/wiiu/src/wiiu_auto_continue.c'), '-o', str(program)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            result = subprocess.run([str(program)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('all checks passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
