"""The render view snapshot's ring (port/linux/game/tick_ring.h), apart from the game: its rules on one thread, and a tick thread and a drawing thread
beside each other with random jobs (port/wiiu/tests/test_tick_ring.c), plain, with the address and undefined-behaviour sanitizers and with the thread
sanitizer, where the host has them. A ring that lets the tick thread write a pinned slot must fail the threaded case.

TICK_RING_TARGET='--target=powerpc-linux-gnu -fuse-ld=lld -no-pie'
TICK_RING_RUNNER='qemu-ppc -cpu 750 -L /usr/powerpc-linux-gnu'
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / 'port/wiiu/tests/test_tick_ring.c'


class TickRingTests(unittest.TestCase):
    def fixture(self, case, sanitizer=None):
        with tempfile.TemporaryDirectory(prefix='halo-tick-ring-') as directory:
            directory = Path(directory)
            command = [os.environ.get('CC', 'cc'), *shlex.split(os.environ.get('TICK_RING_TARGET', '')),
                       '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-ffp-contract=off', '-pthread',
                       str(FIXTURE), '-o', str(directory / 'test')]
            if sanitizer:
                command[1:1] = [f'-fsanitize={sanitizer}', '-fno-omit-frame-pointer']
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            return subprocess.run([*shlex.split(os.environ.get('TICK_RING_RUNNER', '')), str(directory / 'test'), case],
                                  capture_output=True, text=True, timeout=600)

    def test_the_rules(self):
        result = self.fixture('rules')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_a_tick_thread_beside_a_drawing_thread(self):
        result = self.fixture('threads')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    @unittest.skipIf(bool(os.environ.get('TICK_RING_TARGET')), 'Sanitizers run on the host')
    def test_sanitizers(self):
        for case in ('rules', 'threads'):
            for sanitizer in ('address,undefined', 'thread'):
                with self.subTest(case=case, sanitizer=sanitizer):
                    result = self.fixture(case, sanitizer)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_a_ring_that_ignores_the_pins_is_detected(self):
        result = self.fixture('broken')
        self.assertNotEqual(result.returncode, 0)

    @unittest.skipIf(bool(os.environ.get('TICK_RING_TARGET')), 'Sanitizers run on the host')
    def test_a_ring_that_ignores_the_pins_is_a_race_to_the_thread_sanitizer(self):
        result = self.fixture('broken', 'thread')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('data race', result.stderr)


if __name__ == '__main__':
    unittest.main()
