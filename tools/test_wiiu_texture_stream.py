"""The plan of a texture's streaming (port/linux/game/texture_stream.h), apart from the game: port/wiiu/tests/test_texture_stream.c checks where a bitmap's read is cut,
which levels the tail has, and that the head and the tail cover the bytes of the one read without writing any twice. A plan that cuts at something that is not a sector
must fail it.

TEXTURE_STREAM_TARGET='--target=powerpc-linux-gnu -fuse-ld=lld -no-pie'
TEXTURE_STREAM_RUNNER='qemu-ppc -cpu 750 -L /usr/powerpc-linux-gnu'
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / 'port/wiiu/tests/test_texture_stream.c'


class TextureStreamTests(unittest.TestCase):
    def fixture(self, case, *defines):
        with tempfile.TemporaryDirectory(prefix='halo-texture-stream-') as directory:
            directory = Path(directory)
            command = [os.environ.get('CC', 'cc'), *shlex.split(os.environ.get('TEXTURE_STREAM_TARGET', '')),
                       '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', *defines,
                       str(FIXTURE), '-o', str(directory / 'test')]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            return subprocess.run([*shlex.split(os.environ.get('TEXTURE_STREAM_RUNNER', '')), str(directory / 'test'), case],
                                  capture_output=True, text=True, timeout=600)

    def test_known_textures(self):
        result = self.fixture('plan')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_random_textures(self):
        result = self.fixture('random')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_a_cut_that_is_not_at_a_sector_is_detected(self):
        result = self.fixture('broken', '-DBROKEN')
        self.assertNotEqual(result.returncode, 0)


if __name__ == '__main__':
    unittest.main()
