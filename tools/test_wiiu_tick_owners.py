"""The tick owners' queues and the object pool's hand-off (port/linux/game/tick_owners.c), apart from the game; host and optional
big-endian checks.

TICK_OWNERS_TARGET='--target=powerpc-linux-gnu -fuse-ld=lld -no-pie'
TICK_OWNERS_RUNNER='qemu-ppc -cpu 750 -L /usr/powerpc-linux-gnu'
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / 'port/wiiu/tests/test_tick_owners.c'
CASES = ['mode0', 'mode1', 'mode2', 'full', 'nested', 'owners', 'discard', 'threads', 'pool', 'tags', 'defer', 'waited', 'blocked']
THREAD_CASES = ['threads', 'waited', 'pool']


class TickOwnersTests(unittest.TestCase):
    def fixture(self, case, sanitizer=False, fault=None, thread_sanitizer=False):
        with tempfile.TemporaryDirectory(prefix='halo-tick-owners-') as directory:
            directory = Path(directory)
            source = ROOT / 'port/linux/game/tick_owners.c'
            include = ['-I' + str(ROOT / 'port/wiiu/tests/tick_owners_stub'), '-I' + str(ROOT / 'port/linux/game')]
            if fault:
                text = source.read_text()
                before, after = fault
                self.assertEqual(text.count(before), 1)
                source = directory / 'tick_owners.c'
                source.write_text(text.replace(before, after, 1))
                include.insert(0, '-I' + str(directory))
            command = [os.environ.get('CC', 'cc'), *shlex.split(os.environ.get('TICK_OWNERS_TARGET', '')),
                       '-std=gnu11', '-O2', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-missing-field-initializers',
                       '-Wno-unused-function', '-ffp-contract=off', '-pthread', *include, str(FIXTURE), '-o', str(directory / 'test')]
            if sanitizer:
                command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            if thread_sanitizer:
                command[1:1] = ['-fsanitize=thread', '-fno-omit-frame-pointer']
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            return subprocess.run([*shlex.split(os.environ.get('TICK_OWNERS_RUNNER', '')), str(directory / 'test'), case],
                                  capture_output=True, text=True, timeout=120)

    def test_each_case(self):
        for case in CASES:
            with self.subTest(case=case):
                result = self.fixture(case)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    @unittest.skipIf(bool(os.environ.get('TICK_OWNERS_TARGET')), 'Sanitizers run on the host')
    def test_sanitizers(self):
        for case in CASES:
            with self.subTest(case=case):
                result = self.fixture(case, sanitizer=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    @unittest.skipIf(bool(os.environ.get('TICK_OWNERS_TARGET')), 'The thread sanitizer runs on the host')
    def test_thread_sanitizer(self):
        # the cases with more than one thread, under the thread sanitizer: a data race in the queues' plumbing is a failure
        for case in THREAD_CASES:
            with self.subTest(case=case):
                result = self.fixture(case, thread_sanitizer=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_a_queue_that_forgets_its_order_is_detected(self):
        # the oldest record taken last: the order checks fail
        result = self.fixture('full', fault=('memcpy(record, queue->records + (queue->head % (unsigned long)queue->capacity)',
                                              'memcpy(record, queue->records + ((queue->tail - 1) % (unsigned long)queue->capacity)'))
        self.assertNotEqual(result.returncode, 0)

    def test_a_pool_compacted_inside_a_frame_is_detected(self):
        # the compaction does not wait for the frame
        result = self.fixture('pool', fault=('tick_owners_pool_waits++;\n\t\tpthread_mutex_lock(&tick_owners_pool_lock);', 'tick_owners_pool_waits++;'))
        self.assertNotEqual(result.returncode, 0)


if __name__ == '__main__':
    unittest.main()
