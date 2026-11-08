"""The smooth defaults (port/linux/game/smooth_defaults.c) and the map summary (port/wiiu/src/wiiu_map_summary.c), apart from the game and the console: which maps
the defaults are for (one local player, no network of any kind, no film, no split screen), what each of the four card files (tick_thread.txt, tick_snapshot.txt,
tick_overlap.txt, governor.txt) comes to for a map of each kind (absent or -1: the default; 0: off; above 0: on, but not for a map that is not one), the decision at each
map start, and the map summary's figures. Plain and with the address and undefined-behaviour sanitizers, where the host has them."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

SMOOTH = (ROOT / 'port/wiiu/tests/test_smooth_defaults.c', [ROOT / 'port/linux/game/smooth_defaults.c'], ['-DHALO_WIIU=1', '-I' + str(ROOT / 'port/linux/game')])
SUMMARY = (ROOT / 'port/wiiu/tests/test_map_summary.c', [ROOT / 'port/wiiu/src/wiiu_map_summary.c'], ['-pthread', '-lm'])


def build_and_run(case, arguments=(), sanitizer=None):
    fixture, sources, flags = case
    with tempfile.TemporaryDirectory(prefix='halo-smooth-') as directory:
        program = Path(directory) / 'test'
        command = [os.environ.get('CC', 'cc'), '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', str(fixture),
                   *map(str, sources), *[flag for flag in flags if not flag.startswith('-l')], '-o', str(program), *[flag for flag in flags if flag.startswith('-l')]]
        if sanitizer:
            command[1:1] = [f'-fsanitize={sanitizer}', '-fno-omit-frame-pointer']
        built = subprocess.run(command, capture_output=True, text=True)
        assert built.returncode == 0, built.stderr
        return subprocess.run([str(program), *arguments], capture_output=True, text=True, timeout=300)


class SmoothDefaultsTests(unittest.TestCase):
    def test_the_rule_the_maps_and_the_decisions(self):
        result = build_and_run(SMOOTH)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('all checks passed', result.stdout)

    def test_the_owners_card_of_ones_is_not_an_order_for_a_map_that_is_not_one(self):
        result = build_and_run(SMOOTH, ['owner'])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_a_card_file_of_zero_is_off_for_each_of_the_four(self):
        for which in ('thread', 'snapshot', 'overlap', 'governor'):
            with self.subTest(which=which):
                result = build_and_run(SMOOTH, ['opt-out', which])
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_sanitizers(self):
        for arguments in ([], ['owner']):
            result = build_and_run(SMOOTH, arguments, 'address,undefined')
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_a_rule_that_turns_the_default_on_for_a_network_map_is_detected(self):
        fixture, sources, flags = SMOOTH
        with tempfile.TemporaryDirectory(prefix='halo-smooth-fault-') as directory:
            source = (ROOT / 'port/linux/game/smooth_defaults.c').read_text()
            before = '\telse if (reason)\n\t{\n\t\tout->value[which] = 0;\n\t\tout->source[which] = SMOOTH_SOURCE_MAP_OFF;\n\t}'
            self.assertEqual(source.count(before), 1)
            mutated = Path(directory) / 'smooth_defaults.c'
            mutated.write_text(source.replace(before, '\telse if (0)\n\t{\n\t\tout->value[which] = 0;\n\t}', 1))
            result = build_and_run((fixture, [mutated], flags))
            self.assertNotEqual(result.returncode, 0)


    def test_a_rule_that_lets_a_network_game_in_without_its_card_file_is_detected(self):
        fixture, sources, flags = SMOOTH
        with tempfile.TemporaryDirectory(prefix='halo-smooth-fault-') as directory:
            source = (ROOT / 'port/linux/game/smooth_defaults.c').read_text()
            before = '(map->connection == SMOOTH_CONNECTION_CLIENT && map->net_overlap >= 1)'
            self.assertEqual(source.count(before), 1)
            mutated = Path(directory) / 'smooth_defaults.c'
            mutated.write_text(source.replace(before, '(map->connection == SMOOTH_CONNECTION_CLIENT)', 1))
            result = build_and_run((fixture, [mutated], flags))
            self.assertNotEqual(result.returncode, 0)

    def test_a_governor_switched_on_in_a_network_game_is_detected(self):
        fixture, sources, flags = SMOOTH
        with tempfile.TemporaryDirectory(prefix='halo-smooth-fault-') as directory:
            source = (ROOT / 'port/linux/game/smooth_defaults.c').read_text()
            before = 'if (!reason && main_smooth_defaults_net_game())'
            self.assertEqual(source.count(before), 1)
            mutated = Path(directory) / 'smooth_defaults.c'
            mutated.write_text(source.replace(before, 'if (0)', 1))
            result = build_and_run((fixture, [mutated], flags))
            self.assertNotEqual(result.returncode, 0)

    def test_a_rule_that_lets_co_op_or_a_host_in_on_a_client_setting_is_detected(self):
        fixture, sources, flags = SMOOTH
        for before, after in (('const int network_allowed = !map->coop && (', 'const int network_allowed = ('),
                              ('map->connection == SMOOTH_CONNECTION_SERVER && map->net_overlap >= 2', 'map->connection == SMOOTH_CONNECTION_SERVER && map->net_overlap >= 1')):
            with self.subTest(before=before[:50]), tempfile.TemporaryDirectory(prefix='halo-smooth-fault-') as directory:
                source = (ROOT / 'port/linux/game/smooth_defaults.c').read_text()
                self.assertEqual(source.count(before), 1)
                mutated = Path(directory) / 'smooth_defaults.c'
                mutated.write_text(source.replace(before, after, 1))
                result = build_and_run((fixture, [mutated], flags))
                self.assertNotEqual(result.returncode, 0)


class MapSummaryTests(unittest.TestCase):
    def test_the_figures_and_the_lines(self):
        result = build_and_run(SUMMARY)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('all checks passed', result.stdout)

    def test_sanitizers(self):
        result = build_and_run(SUMMARY, sanitizer='address,undefined')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
