"""The game's debug memory (source/cseries/debug_memory.c) with threads allocating at once, as the tick thread and the drawing thread do when the ticks run beside the drawing
(port/wiiu/tests/test_debug_memory.c): every pointer is accounted for at the end. Plain and with the thread sanitizer, where the host has it. The same code without its lock
must be found (a corrupted list, or a data race to the sanitizer): the network overlap's first runs halted a joined client on exactly that (reports/research/2026-11-07/netoverlap-result.md)."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / 'port/wiiu/tests/test_debug_memory.c'
STUB = ROOT / 'port/wiiu/tests/debug_memory_stub'
SOURCE = ROOT / 'source/cseries/debug_memory.c'


def source_text():
    """the source, with the Xbox layout's size checks (32-bit pointers) let pass on a 64-bit host: the lists work with any size"""
    return re.sub(r'(sizeof\(struct debug_memory_(?:globals|header)\) == 0x20|offsetof\(struct debug_memory_header, checksum\) == 0x1C) \? 1 : -1', r'\1 ? 1 : 1',
                  SOURCE.read_text())


def build_and_run(sanitizer=None, mutate=None):
    with tempfile.TemporaryDirectory(prefix='halo-debug-memory-') as directory:
        directory = Path(directory)
        text = source_text()
        if mutate:
            assert text.count(mutate[0]) == 1, mutate[0]
            text = text.replace(mutate[0], mutate[1], 1)
        (directory / 'debug_memory.c').write_text(text)
        command = [os.environ.get('CC', 'cc'), '-std=gnu11', '-O1', '-g', '-Wall', '-Wno-unused-function', '-pthread', '-DHALO_WIIU=1', '-I' + str(directory), '-I' + str(STUB),
                   str(FIXTURE), '-o', str(directory / 'test')]
        if sanitizer:
            command[1:1] = [f'-fsanitize={sanitizer}', '-fno-omit-frame-pointer']
        built = subprocess.run(command, capture_output=True, text=True)
        assert built.returncode == 0, built.stderr
        return subprocess.run([str(directory / 'test')], capture_output=True, text=True, timeout=600)


NO_LOCK = ('#define DEBUG_MEMORY_LOCK() debug_memory_lock()', '#define DEBUG_MEMORY_LOCK()')
NO_UNLOCK = ('#define DEBUG_MEMORY_UNLOCK() debug_memory_unlock()', '#define DEBUG_MEMORY_UNLOCK()')


class DebugMemoryTests(unittest.TestCase):
    def test_four_threads_allocating_and_freeing_lose_no_pointer(self):
        result = build_and_run()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('debug memory: ok', result.stdout)

    @unittest.skipIf(shutil.which('cc') is None, 'needs a C compiler')
    def test_the_thread_sanitizer_finds_no_race(self):
        result = build_and_run('thread')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_without_the_lock_the_list_is_lost_or_a_race_is_found(self):
        # (the lock gone: the pointers' list is changed by two threads at once; the program stops on a check or the sanitizer says data race)
        result = build_and_run('thread', NO_LOCK)
        self.assertTrue(result.returncode != 0 or 'data race' in result.stderr, result.stdout + result.stderr)
        self.assertTrue('data race' in result.stderr or 'check failed' in result.stdout or 'not empty' in result.stdout, result.stdout + result.stderr)

    def test_a_lock_that_is_never_let_go_of_is_found(self):
        # (the first thread keeps it: the others wait for good; the test's own timeout ends the run)
        with self.assertRaises(subprocess.TimeoutExpired):
            with tempfile.TemporaryDirectory(prefix='halo-debug-memory-') as directory:
                directory = Path(directory)
                (directory / 'debug_memory.c').write_text(source_text().replace(*NO_UNLOCK, 1))
                subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-O1', '-pthread', '-DHALO_WIIU=1', '-I' + str(directory), '-I' + str(STUB), str(FIXTURE), '-o', str(directory / 'test')],
                               check=True, capture_output=True)
                subprocess.run([str(directory / 'test')], capture_output=True, timeout=20)


if __name__ == '__main__':
    unittest.main()
