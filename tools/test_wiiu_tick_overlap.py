"""The hand-off between the game thread and the tick thread (port/linux/game/tick_overlap.c with the queues of tick_owners.c and the ring's rules of
tick_ring.h), apart from the game: port/wiiu/tests/test_tick_overlap.c runs some thousands of frames of a game thread and a tick thread beside each
other. Plain, with the address and undefined-behaviour sanitizers and with the thread sanitizer, where the host has them. Faults put into a copy of
the source must be found.

TICK_OVERLAP_TARGET='--target=powerpc-linux-gnu -fuse-ld=lld -no-pie'
TICK_OVERLAP_RUNNER='qemu-ppc -cpu 750 -L /usr/powerpc-linux-gnu'
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / 'port/wiiu/tests/test_tick_overlap.c'
CASES = ['frames', 'small']


class TickOverlapTests(unittest.TestCase):
    def fixture(self, case, sanitizer=None, fault=None):
        with tempfile.TemporaryDirectory(prefix='halo-tick-overlap-') as directory:
            directory = Path(directory)
            include = ['-I' + str(ROOT / 'port/wiiu/tests/tick_overlap_stub'), '-I' + str(ROOT / 'port/linux/game')]
            if fault:
                source = (ROOT / 'port/linux/game' / fault[0]).read_text()
                self.assertEqual(source.count(fault[1]), 1, fault[1])
                (directory / fault[0]).write_text(source.replace(fault[1], fault[2], 1))
                include.insert(0, '-I' + str(directory))
            command = [os.environ.get('CC', 'cc'), *shlex.split(os.environ.get('TICK_OVERLAP_TARGET', '')),
                       '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-unused-function',
                       '-Wno-missing-field-initializers', '-ffp-contract=off', '-pthread', *include, str(FIXTURE), '-o', str(directory / 'test')]
            if sanitizer:
                command[1:1] = [f'-fsanitize={sanitizer}', '-fno-omit-frame-pointer']
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            return subprocess.run([*shlex.split(os.environ.get('TICK_OVERLAP_RUNNER', '')), str(directory / 'test'), case],
                                  capture_output=True, text=True, timeout=900)

    def test_each_case(self):
        for case in CASES:
            with self.subTest(case=case):
                result = self.fixture(case)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    @unittest.skipIf(bool(os.environ.get('TICK_OVERLAP_TARGET')), 'Sanitizers run on the host')
    def test_sanitizers(self):
        for case in CASES:
            for sanitizer in ('address,undefined', 'thread'):
                with self.subTest(case=case, sanitizer=sanitizer):
                    result = self.fixture(case, sanitizer)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_a_tick_thread_that_takes_what_the_game_thread_pushes_beside_its_job_is_detected(self):
        # the drain limit gone: the tick thread takes everything in the queue, including what the game thread pushed after posting the job
        result = self.fixture('frames', fault=('tick_overlap.c', 'return LOAD(o.running_sequence) - 1;', 'return (unsigned long)-1;'))
        self.assertNotEqual(result.returncode, 0)

    def test_a_game_thread_that_drains_its_queues_in_a_frame_is_detected(self):
        # the deferral gone: the game thread applies the tick thread's requests whenever it passes a drain point
        result = self.fixture('frames', fault=('tick_overlap.c', 'if (o.enabled <= 0 || !LOAD(o.job_in_flight) || LOAD(o.forcing))', 'if (1)'))
        self.assertNotEqual(result.returncode, 0)

    @unittest.skipIf(bool(os.environ.get('TICK_OVERLAP_TARGET')), 'Sanitizers run on the host')
    def test_a_job_that_says_it_is_over_without_a_release_is_a_race_to_the_thread_sanitizer(self):
        # the job's writes not ordered before "over": on a PowerPC the game thread could read a job's numbers (and the ring's captures) as they were
        result = self.fixture('frames', 'thread', fault=('tick_overlap.c', '__atomic_store_n(&o.pending, FALSE, __ATOMIC_RELEASE);',
                                                         '__atomic_store_n(&o.pending, FALSE, __ATOMIC_RELAXED);'))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('data race', result.stderr)

    def test_a_ring_that_writes_over_the_captures_a_frame_draws_from_is_detected(self):
        # (the ring's rules, mutated: the newest capture is the one written, pinned or not)
        before = ('if (tick_ring_pinned(ring, index))\n\t\t\tcontinue;\n\t\tif (!ring->ready[index])\n\t\t\treturn index;\n'
                  '\t\tif (published != TICK_RING_NONE && ring->tick[index] <= published - 2 && (best < 0 || ring->tick[index] < ring->tick[best]))\n')
        after = ('if (!ring->ready[index])\n\t\t\treturn index;\n'
                 '\t\tif (best < 0 || ring->tick[index] > ring->tick[best])\n')
        result = self.fixture('frames', fault=('tick_ring.h', before, after))
        self.assertNotEqual(result.returncode, 0)


if __name__ == '__main__':
    unittest.main()
