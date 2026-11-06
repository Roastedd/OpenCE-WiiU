"""The Wii U's shaders for keys the pack lacks (gx2_shaders.c, shader_key_wire.h, pack_gen.c).

The lookup, compiled from gx2_shaders.c itself with the rest of the game stood in for (port/wiiu/tests/test_shader_fallback.c): a pixel key the pack
lacks is drawn with its canonical key's shader, else with its stand-in; a key left out of the pack is skipped; each key the pack lacks is written down.
SHADER_FALLBACK_TARGET='--target=powerpc-linux-gnu -fuse-ld=lld -no-pie' SHADER_FALLBACK_RUNNER='qemu-ppc -L /usr/powerpc-linux-gnu' runs it
big-endian, as the console: its hashes must be those the pack builder made on a little-endian computer.

And the pack builder's check (pack_gen.c --check, built as tools/wiiu/shader_pack.sh builds it): on random keys, a key and its canonical key have the
same GLSL, word for word, and the stand-in of each is one of those the pack has; with two broken canonical keys and two broken stand-ins it must
report, as controls. That needs clang-i686 and qemu-i386 (the opence-wiiu container) and is skipped elsewhere.
"""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / 'port/wiiu/src'
TESTS = ROOT / 'port/wiiu/tests'
sys.path.insert(0, str(ROOT / 'tools'))
from harness import enum_with, structure

PACK_DEFINE = '#define PACK_FILE "/vol/external01/wiiu/apps/halo/shaders.pack"'


def stand_in_xgpu():
    """xgpu.h as much as gx2_shaders.c and shader_key_wire.h use: the render states, the comparisons and fog modes, the key."""
    pdb = (ROOT / 'port/include/xdk/xdk_pdb.h').read_text(encoding='latin-1')
    xgpu = (ROOT / 'port/linux/src/xgpu.h').read_text()
    return ('#ifndef TEST_XGPU_H\n#define TEST_XGPU_H\n#include <stdint.h>\ntypedef uint32_t DWORD;\n' +
            enum_with(pdb, 'D3DRS_PSALPHAINPUTS0') + '\n' + enum_with(pdb, 'D3DCMP_NEVER') + '\n' + enum_with(pdb, 'D3DFOG_NONE') + '\n' +
            enum_with(xgpu, '_xgpu_sampler_none') + '\n' + structure(xgpu, 'nv2a_pixel_shader_key') + '\n' +
            'void platform_log(const char *format, ...) __attribute__((format(printf, 1, 2)));\n#endif\n')


def sanitizers_link():
    """whether the compiler here has the address and undefined-behaviour sanitizers' libraries (the opence-wiiu container's clang has not)"""
    with tempfile.TemporaryDirectory(prefix='opence-sanitizers-') as directory:
        (Path(directory) / 'empty.c').write_text('int main(void) { return 0; }\n')
        return subprocess.run([os.environ.get('CC', 'cc'), '-fsanitize=address,undefined', 'empty.c', '-o', 'empty'], cwd=directory,
                              capture_output=True).returncode == 0


class LookupTests(unittest.TestCase):
    def run_lookup(self, sanitizer=False):
        with tempfile.TemporaryDirectory(prefix='opence-shader-fallback-') as directory:
            directory = Path(directory)
            source = (SRC / 'gx2_shaders.c').read_text()
            self.assertEqual(source.count(PACK_DEFINE), 1)
            (directory / 'gx2_shaders.c').write_text(source.replace(PACK_DEFINE, '#define PACK_FILE TEST_PACK_FILE'))
            (directory / 'xgpu.h').write_text(stand_in_xgpu())
            (directory / 'halo_startup.h').write_text((ROOT / 'port/linux/include/halo_startup.h').read_text())  # (the start-up timeline's marks, nothing on a host)
            command = [os.environ.get('CC', 'cc'), *shlex.split(os.environ.get('SHADER_FALLBACK_TARGET', '')),
                       '-std=gnu11', '-O2', '-g', '-Wall', '-Wextra', '-Werror', '-I' + str(directory), '-I' + str(SRC),
                       '-DSHADERS_SOURCE="' + str(directory / 'gx2_shaders.c') + '"',
                       '-DTEST_PACK_FILE="' + str(directory / 'shaders.pack') + '"']
            if sanitizer:
                command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-sanitize-recover=all']
            command += [str(TESTS / 'test_shader_fallback.c'), '-o', str(directory / 'check')]
            result = subprocess.run(command, capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([*shlex.split(os.environ.get('SHADER_FALLBACK_RUNNER', '')), str(directory / 'check')],
                                    capture_output=True, text=True, timeout=300)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('shader fallback tests passed', result.stdout)

    def test_lookup(self):
        self.run_lookup()

    @unittest.skipIf(bool(os.environ.get('SHADER_FALLBACK_TARGET')), 'the sanitizers run on this machine')
    def test_lookup_sanitizers(self):
        if not sanitizers_link():
            self.skipTest('no sanitizer libraries for this compiler')
        self.run_lookup(sanitizer=True)

    def test_backend_counts_each_way(self):
        """wiiu_gx2.c takes how each key was found, counts the draws made with another key's shader, and logs them every five seconds."""
        backend = (SRC / 'wiiu_gx2.c').read_text()
        self.assertEqual(backend.count('gx.ps_found = dk_shader_found(DK_SHADER_STAGE_PIXEL);'), 2)
        self.assertEqual(backend.count('gx.vs_found = dk_shader_found(DK_SHADER_STAGE_VERTEX);'), 2)
        draw = backend[backend.index('static void draw(const struct dk_command_draw *command)'):backend.index('int wiiu_memcpy_selftest(void);')]
        self.assertLess(draw.index('gx.draws_made++;'), draw.index('gx.drawn_fallback++;'))
        self.assertIn('if (gx.ps_found == DK_SHADER_FOUND_EQUIVALENT)\n\t\tgx.drawn_equivalent++;', draw)
        self.assertIn('gx.skipped_left_out++;', draw)
        log = backend[backend.index('wiiu_log("GX2:   %u draws made'):]
        log = log[:log.index(';\n') + 1]
        for counter in ['gx.drawn_equivalent', 'gx.drawn_fallback', 'gx.skipped_missing', 'gx.skipped_left_out']:
            self.assertIn(counter, log)
        self.assertIn('gx.skipped_left_out = gx.drawn_equivalent = gx.drawn_fallback = 0;', backend)


@unittest.skipUnless(shutil.which('clang-i686') and shutil.which('qemu-i386'), 'the pack builder is built in the opence-wiiu container')
class GeneratorCheckTests(unittest.TestCase):
    def pack_gen(self, arguments, header_change=None):
        with tempfile.TemporaryDirectory(prefix='opence-pack-gen-') as directory:
            directory = Path(directory)
            for name in ['port/linux/src/nv2a_vsh.c', 'port/linux/src/nv2a_psh.c', 'port/linux/src/xgpu_text.c', 'port/linux/src/xgpu.h',
                         'port/wiiu/tools/shader_pack/pack_gen.c', 'port/wiiu/src/shader_key_wire.h']:
                shutil.copy(ROOT / name, directory)
            if header_change:
                header = (directory / 'shader_key_wire.h').read_text()
                self.assertEqual(header.count(header_change[0]), 1, header_change[0])
                (directory / 'shader_key_wire.h').write_text(header.replace(*header_change))
            (directory / 'gl.h').write_text('typedef unsigned int GLuint, GLenum, GLbitfield;\ntypedef int GLint, GLsizei;\n'
                                            'typedef unsigned char GLboolean, GLubyte;\ntypedef float GLfloat;\ntypedef long GLsizeiptr, GLintptr;\n'
                                            'typedef char GLchar;\n')
            subprocess.run([sys.executable, 'tools/linux_msvc_semantics.py', '--output', str(directory / 'platform_msvc_semantics.h'),
                            '--inlines', 'port/include/xdk'], cwd=ROOT, check=True, capture_output=True)
            # (tools/wiiu/shader_pack.sh's flags)
            flags = ('--target=i686-linux-gnu -m32 -fms-extensions -fshort-wchar -malign-double -fcommon -fno-pic -fno-strict-aliasing -fwrapv '
                     '-fno-delete-null-pointer-checks -freg-struct-return -ffp-contract=off -O1 -ffunction-sections -fdata-sections -std=gnu11 '
                     '-D_GNU_SOURCE -DHALO_LINUX_PLATFORM_LAYER -DHALO_GLES -w').split()
            command = ['clang-i686', *flags, '-include', str(ROOT / 'port/linux/include/halo_linux_prefix.h'),
                       '-include', str(directory / 'platform_msvc_semantics.h'), '-I' + str(directory), '-I' + str(ROOT / 'port/linux/src'),
                       '-I' + str(ROOT / 'port/linux/include'), '-I' + str(ROOT / 'source'), '-I' + str(ROOT / 'source/cseries'),
                       '-idirafter', str(ROOT / 'port/include/xdk'),
                       *(str(directory / name) for name in ['pack_gen.c', 'nv2a_vsh.c', 'nv2a_psh.c', 'xgpu_text.c']),
                       '-no-pie', '-Wl,--gc-sections', '-Wl,--unresolved-symbols=ignore-all', '-o', str(directory / 'pack_gen')]
            result = subprocess.run(command, capture_output=True, text=True, timeout=300)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return subprocess.run(['qemu-i386', '-L', '/usr/i686-linux-gnu', str(directory / 'pack_gen'), *arguments],
                                  capture_output=True, text=True, timeout=1200)

    def test_canonical_keys_have_the_same_glsl(self):
        result = self.pack_gen(['--check', '200000', '20261030'])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr[-3000:])
        self.assertIn('200000 random pixel keys have the GLSL of their canonical keys', result.stdout)

    def test_the_check_finds_a_stand_in_the_pack_has_not(self):
        # the numbered stand-ins without the alpha kill; a stand-in for NEVER
        for change, said in [(('(index >> 3) & 1, (index >> 4) & 1, out);', '(index >> 3) & 1, 0, out);'), 'none of the numbered ones'),
                             (('if (key->alpha_test_function == D3DCMP_NEVER)\n\t\treturn 0;', ''), 'stand-in')]:
            result = self.pack_gen(['--check', '20000', '1'], change)
            self.assertNotEqual(result.returncode, 0, change)
            self.assertIn(said, result.stderr, change)

    def test_the_check_finds_a_canonical_key_that_clears_too_much(self):
        # a clip plane's compare mode cleared, and the last combiner stage the count runs
        for change in [('out->combiner_state[D3DRS_PSCOMPAREMODE] = compare;', 'out->combiner_state[D3DRS_PSCOMPAREMODE] = 0;'),
                       ('for (stage = count; stage < 8; stage++)', 'for (stage = count ? count - 1 : 0; stage < 8; stage++)')]:
            result = self.pack_gen(['--check', '20000', '1'], change)
            self.assertNotEqual(result.returncode, 0, change)
            self.assertIn('have different GLSL', result.stderr, change)


if __name__ == '__main__':
    unittest.main()
