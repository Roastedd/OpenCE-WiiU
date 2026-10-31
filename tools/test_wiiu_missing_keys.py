"""Production owned missing-key queue, shell close block and fair main-log worker.

MISSING_TARGET='--target=powerpc-linux-gnu -fuse-ld=lld -no-pie'
MISSING_RUNNER='qemu-ppc -L /usr/powerpc-linux-gnu' runs the same source big-endian.
Native sanitizer tests use AddressSanitizer and UndefinedBehaviorSanitizer.
"""
import os
from pathlib import Path
import shlex
import sys
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / 'port/wiiu/src'
TESTS = ROOT / 'port/wiiu/tests'
sys.path.insert(0,str(ROOT/'tools'))
from harness import function, structure, enum_with


def close_block():
    source = (SRC/'wiiu_shell.c').read_text()
    begin = source.index('\t/* Bounded wait only:')
    end = source.index('\twiiu_log("Closed");', begin)
    return 'static void extracted_shell_capture_close(void)\n{\n' + source[begin:end] + '}\n'


def producer_code():
    source=(SRC/'gx2_shaders.c').read_text()
    xgpu=(ROOT/'port/linux/src/xgpu.h').read_text()
    pdb=(ROOT/'port/include/xdk/xdk_pdb.h').read_text()
    declarations=source[source.index('static struct\n{'):source.index('static uint32_t big32')]
    return ('#include "'+str(SRC/'dk_shaders.h')+'"\n'
            'typedef uint32_t DWORD;\n'+enum_with(pdb,'D3DRS_PSALPHAINPUTS0')+'\n'+enum_with(pdb,'D3DCMP_NEVER')+'\n'+
            enum_with(pdb,'D3DFOG_NONE')+'\n'+enum_with(xgpu,'_xgpu_sampler_none')+'\n'+
            structure(xgpu,'nv2a_pixel_shader_key')+'\n#include "'+str(SRC/'shader_key_wire.h')+'"\n'
            '#define PROGRAM_LIMIT 512\n#define SEEN_LIMIT 4096\n'+declarations+'\n'+
            function(source,'dk_shader_program_note')+'\n'+function(source,'missing_note')+'\n')


def activity_code():
    source=(ROOT/'source/game/game_time.c').read_text()
    ui=(ROOT/'source/interface/ui_widget.c').read_text()
    prelude = r"""
#define HALO_WIIU 1
typedef unsigned char boolean;
typedef float real;
#define TRUE 1
#define FALSE 0
#define match_assert(file,line,condition) CHECK(condition)
static boolean we_are_at_the_main_menu;
void wiiu_missing_keys_gameplay(int active);
void game_time_wiiu_capture_menu(int main_menu);
"""
    storage=structure(source,'game_time_globals_struct')+r"""
static struct game_time_globals_struct time_storage;
static struct game_time_globals_struct *game_time_globals;
static void *game_state_malloc(const char *name,const char *type,long bytes)
{ (void)name; (void)type; CHECK(bytes==(long)sizeof(time_storage)); return &time_storage; }
enum { _game_connection_local, _game_connection_network_server, _game_connection_network_client, _game_connection_film_playback };
static short game_connection(void) { return _game_connection_local; }
static void update_server_start(void) {}
static void update_client_start(void) {}
"""
    names=['game_time_wiiu_capture_activity','game_time_wiiu_capture_menu','game_time_initialize',
           'game_time_initialize_for_new_map','game_time_dispose_from_old_map','game_time_end',
           'game_time_set_paused','game_time_start']
    return prelude+storage+function(ui,'main_menu_is_active')+'\n'+'\n'.join(function(source,name) for name in names)+'\n'+function(ui,'main_menu_active')+'\n'


class MissingKeysTests(unittest.TestCase):
    def fixture(self, shell=False, sanitizer=False):
        with tempfile.TemporaryDirectory(prefix='opence-key-tests-') as directory:
            directory = Path(directory)
            fixture = directory/'close.inc'
            fixture.write_text(close_block())
            producer=directory/'producer.inc'
            producer.write_text(producer_code())
            activity=directory/'activity.inc'
            activity.write_text(activity_code())
            command = [os.environ.get('CC','cc'), *shlex.split(os.environ.get('MISSING_TARGET','')),
                       '-std=gnu11','-O2','-g','-Wall','-Wextra','-Werror','-pthread',
                       '-D_GNU_SOURCE','-D_DARWIN_C_SOURCE','-D_FILE_OFFSET_BITS=64']
            if sanitizer:
                command += ['-fsanitize=address,undefined','-fno-omit-frame-pointer']
            if shell:
                command += [str(TESTS/'test_shell.c'), *(str(SRC/name) for name in
                            ['wiiu_shell_log.c','wiiu_shell_watch.c','wiiu_shell_blackbox.c'])]
            else:
                command += ['-DWIIU_MISSING_CLOSE_FIXTURE="'+str(fixture)+'"',
                            '-DWIIU_MISSING_PRODUCER_FIXTURE="'+str(producer)+'"',
                            '-DWIIU_MISSING_ACTIVITY_FIXTURE="'+str(activity)+'"',str(TESTS/'test_missing_keys.c')]
            command += ['-o',str(directory/'check')]
            result = subprocess.run(command,capture_output=True,text=True,timeout=60)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result = subprocess.run([*shlex.split(os.environ.get('MISSING_RUNNER','')),str(directory/'check')],
                                    capture_output=True,text=True,timeout=60)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            return result.stdout

    def test_actual_queue_records_and_lifecycle(self):
        self.assertIn('ACTIVE/IO-held close passed',self.fixture())

    def test_actual_worker_fairness_and_error_log_separation(self):
        self.assertIn('shell tests passed',self.fixture(shell=True))

    @unittest.skipIf(bool(os.environ.get('MISSING_TARGET')),'sanitizer uses native host')
    def test_queue_and_worker_sanitizers(self):
        self.fixture(sanitizer=True)
        self.fixture(shell=True,sanitizer=True)

    def test_main_only_service_and_render_path(self):
        shell=(SRC/'wiiu_shell.c').read_text()
        error=shell[shell.index('static void error_log_start(void)'):shell.index('void wiiu_error_file_text')]
        self.assertIn('memset(&config, 0, sizeof(config))',error)
        self.assertNotIn('.service',error)
        self.assertEqual(shell.count('log_config.service = wiiu_missing_keys_service;'),1)
        self.assertIn('wiiu_missing_keys_suspend();\n\tforeground = 0;',shell)
        self.assertIn('foreground = 1;\n\twiiu_missing_keys_resume();',shell)
        capture=(SRC/'wiiu_missing_keys.c').read_text()
        worker=capture[capture.index('static int service_turn(void)'):]
        self.assertNotIn('wiiu_missing_keys_report(',worker)
        self.assertNotIn('config.report(',worker)
        self.assertLess(shell.index('wiiu_missing_keys_report("close");'),shell.index('wiiu_log("Closed");'))
        source=(SRC/'gx2_shaders.c').read_text()
        note=source[source.index('static int missing_note('):source.index("/* the handle of the pack's entry")]
        for banned in ['fopen(', 'fprintf(', 'fclose(', 'malloc(', 'pthread_', 'OSYieldThread', 'sleep(']:
            self.assertNotIn(banned,note)
        self.assertLess(note.index('wiiu_missing_keys_observe()'),note.index('seen_count'))
        self.assertIn('seen[i].hash == hash && seen[i].stage == stage',note)
        self.assertIn('__atomic_load_n(&program_count, __ATOMIC_ACQUIRE)',note)
        self.assertIn('__atomic_store_n(&program_count, count_now + 1u, __ATOMIC_RELEASE)',source)


    def test_authoritative_activity_transition_order_and_primitive_abi(self):
        source=(ROOT/'source/game/game_time.c').read_text()
        ui=(ROOT/'source/interface/ui_widget.c').read_text()
        start=function(source,'game_time_start')
        self.assertLess(start.index('game_time_wiiu_capture_activity(TRUE)'),start.index('game_time_globals->active = TRUE'))
        pause=function(source,'game_time_set_paused')
        self.assertLess(pause.index('game_time_wiiu_capture_activity(game_time_globals->initialized)'),pause.index('game_time_globals->active = !paused'))
        self.assertGreater(pause.index('game_time_wiiu_capture_activity(FALSE)'),pause.index('game_time_globals->paused = paused'))
        for name in ['game_time_end','game_time_dispose_from_old_map']:
            body=function(source,name)
            self.assertGreater(body.index('game_time_wiiu_capture_activity(FALSE)'),body.index('game_time_globals->active = FALSE'))
        menu=function(ui,'main_menu_active')
        self.assertLess(menu.index('game_time_wiiu_capture_menu(FALSE)'),menu.index('we_are_at_the_main_menu = active'))
        self.assertGreater(menu.index('game_time_wiiu_capture_menu(TRUE)'),menu.index('we_are_at_the_main_menu = active'))
        fixups=(ROOT/'port/linux/include/halo_linux_source_fixups.h').read_text()
        self.assertIn('void wiiu_missing_keys_gameplay(int active);',fixups)
        self.assertIn('void game_time_wiiu_capture_menu(int main_menu);',fixups)


if __name__=='__main__':
    unittest.main()
