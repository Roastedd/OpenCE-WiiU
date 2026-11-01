"""The production packet-memory pool; host and optional big-endian checks.

PACKET_POOL_TARGET='--target=powerpc-linux-gnu -fuse-ld=lld -no-pie'
PACKET_POOL_RUNNER='qemu-ppc -cpu 750 -L /usr/powerpc-linux-gnu'
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'port/wiiu/src/wiiu_packet_pool.c'
FIXTURE = ROOT / 'port/wiiu/tests/test_packet_pool.c'


class PacketPoolTests(unittest.TestCase):
    def fixture(self, case='', sanitizer=False, fault=None):
        with tempfile.TemporaryDirectory(prefix='halo-packet-pool-') as directory:
            directory = Path(directory)
            source = SOURCE
            if fault:
                text = source.read_text()
                before, after = fault
                self.assertEqual(text.count(before), 1)
                source = directory / 'mutated.c'
                source.write_text(text.replace(before, after, 1))
            command = [os.environ.get('CC', 'cc'), *shlex.split(os.environ.get('PACKET_POOL_TARGET', '')),
                       '-std=gnu11', '-O2', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers',
                       '-ffp-contract=off', '-pthread', '-I' + str(SOURCE.parent),
                       '-DPOOL_SOURCE="' + str(source) + '"', str(FIXTURE), '-o', str(directory / 'pool')]
            if sanitizer:
                command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            environment = dict(os.environ)
            if sanitizer:
                environment['ASAN_OPTIONS'] = 'halt_on_error=1:abort_on_error=0:disable_coredump=1:exitcode=86'
            return subprocess.run([*shlex.split(os.environ.get('PACKET_POOL_RUNNER', '')), str(directory / 'pool'), *case.split()],
                                  capture_output=True, text=True, timeout=120, env=environment)

    def test_owned_storage_bounds_failure_and_trim(self):
        result = self.fixture()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_concurrent_ownership_and_trim(self):
        result = self.fixture('threads')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    @unittest.skipIf(bool(os.environ.get('PACKET_POOL_TARGET')), 'Sanitizer runs on the host')
    def test_address_and_undefined_sanitizers(self):
        for case in ('', 'threads'):
            result = self.fixture(case, sanitizer=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    @unittest.skipIf(bool(os.environ.get('PACKET_POOL_TARGET')), 'Sanitizer runs on the host')
    def test_returned_cache_storage_is_poisoned_for_late_readers(self):
        result = self.fixture('late-read', sanitizer=True)
        self.assertEqual(result.returncode, 86, result.stdout + result.stderr)
        self.assertIn('use-after-poison', result.stderr)

    def test_cache_bound_fault_is_detected(self):
        result = self.fixture(fault=('packet_pool.stats.cached_blocks < PACKET_POOL_BLOCKS &&\n\t\tcapacity <= PACKET_POOL_BYTES - packet_pool.stats.cached_bytes', '1'))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('pool check failed', result.stderr)

    def test_shared_live_buffer_fault_is_detected(self):
        result = self.fixture(fault=('packet_pool.free[index] = header->entry.next;\n\t\tpacket_pool.stats.cached_blocks--;',
                                    '/* retained head: deliberate double lease */\n\t\tpacket_pool.stats.cached_blocks--;'))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('pool check failed', result.stderr)

    def test_heap_operation_under_pool_lock_is_detected(self):
        before = 'pthread_mutex_unlock(&packet_pool.lock);\n\tif (!cached)\n\t\tfree(header);'
        after = 'if (!cached)\n\t\tfree(header);\n\tpthread_mutex_unlock(&packet_pool.lock);'
        result = self.fixture(fault=(before, after))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('lock_violations == 0', result.stderr)


if __name__ == '__main__':
    unittest.main()
