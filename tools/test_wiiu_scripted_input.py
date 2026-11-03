"""The scripted player of debug.test_input on the console (reports/research/2026-11-01/wiiu-scripted-input.md): the pattern the desktop build's xinput_sdl.c and the
console's xinput_wiiu.c share (port/linux/include/test_input_bot.h), apart from the game. It is compiled on the host (port/wiiu/tests/test_input_bot.c) and its
sticks and buttons at every tick of 5,000 compared with the pattern written again here, for the seeds 7 and 0 and for "look"; the settings it plays and the
ones it leaves alone; and that both input layers call it, and the console's only while a game runs (the game's ticks are its clock).

    python3 -m unittest tools.test_wiiu_scripted_input
"""
import math
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "port/wiiu/tests/test_input_bot.c"


def build(directory: Path) -> Path:
    program = directory / "test_input_bot"
    result = subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-ffp-contract=off", str(FIXTURE), "-o", str(program), "-lm"],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    return program


def pattern(tick: int, seed: int, looking: bool):
    """the pattern again, from the description in test_input_bot.h: (left x, left y, right x, right trigger, A, left trigger)"""
    t = tick / 30.0 + seed * 1.7
    if looking:
        return (0, 0, int(math.sin(t * 0.5) * 14000.0), 0, 0, 0)
    return (int(math.cos(t * 0.6 + seed) * 20000.0), int(math.sin(t * 0.9) * 32000.0), int(math.sin(t * 0.4) * 14000.0),
            255 if math.fmod(t, 3.0) < 0.3 else 0, 255 if math.fmod(t, 5.0) < 0.1 else 0, 255 if math.fmod(t, 7.0) < 0.2 else 0)


class ScriptedInputTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="halo-test-input-")
        cls.program = build(Path(cls.directory.name))

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def parse(self, setting):
        return subprocess.run([str(self.program), "parse", setting], capture_output=True, text=True).stdout.split()

    def test_the_settings_it_plays_and_the_ones_it_leaves_alone(self):
        self.assertEqual(self.parse("bot:7"), ["yes", "7", "0"])
        self.assertEqual(self.parse("tickbot:7"), ["yes", "7", "0"])
        self.assertEqual(self.parse("bot"), ["yes", "0", "0"])
        self.assertEqual(self.parse("look:3"), ["yes", "3", "1"])
        self.assertEqual(self.parse("tickhunt:7"), ["yes", "7", "2"])
        self.assertEqual(self.parse("hunt:2"), ["yes", "2", "2"])
        for left_alone in ("", "menu:a down a", "tickbots:7", "bots", "off"):
            self.assertEqual(self.parse(left_alone)[0], "no", left_alone)

    def test_the_pattern_tick_by_tick(self):
        ticks = 5000
        for seed, looking in ((7, 0), (0, 0), (7, 1)):
            table = subprocess.run([str(self.program), "table", str(seed), str(looking), str(ticks)], capture_output=True, text=True).stdout.splitlines()
            self.assertEqual(len(table), ticks)
            for line in table:
                tick, *values = map(int, line.split())
                expected = pattern(tick, seed, bool(looking))
                if looking:
                    self.assertEqual((values[0], values[1], values[2]), (0, 0, expected[2]), f"seed {seed} looking, tick {tick}")
                else:
                    self.assertEqual(tuple(values), expected, f"seed {seed}, tick {tick}")

    def hunt(self, distance, yaw, pitch):
        lines = subprocess.run([str(self.program), "hunt", "7", str(distance), str(yaw), str(pitch)], capture_output=True, text=True).stdout.splitlines()
        return [tuple(map(int, line.split())) for line in lines]

    def test_the_hunter_turns_to_the_enemy_walks_up_to_it_and_fires(self):
        # (columns: tick, left x, left y, right x, right trigger, A, left trigger, right y)
        far_left = self.hunt(40.0, 0.5, 0.1)
        self.assertTrue(all(row[3] < 0 for row in far_left), "an enemy to the left turns the right stick to the left, which is negative")
        self.assertTrue(all(row[7] > 0 for row in far_left), "an enemy above the aim looks up")
        self.assertTrue(all(row[2] == 32000 for row in far_left), "far, it walks forward")
        self.assertEqual(sum(row[4] > 0 for row in far_left), 0, "it does not fire with the enemy 0.5 radians off")
        right = self.hunt(40.0, -0.05, -0.02)
        self.assertTrue(all(row[3] > 0 for row in right))
        self.assertTrue(all(row[7] < 0 for row in right))
        self.assertTrue(all(row[4] == 255 for row in right), "it fires with the enemy ahead and near enough")
        close = self.hunt(5.0, 0.0, 0.0)
        self.assertTrue(all(row[2] == 0 for row in close), "close, it stands (and strafes)")
        self.assertTrue(any(row[1] for row in close))
        self.assertEqual(sum(row[4] > 0 for row in self.hunt(90.0, 0.0, 0.0)), 0, "out of range, it does not fire")
        # (a big turn is a full stick, no more)
        self.assertTrue(all(row[3] == 32000 for row in self.hunt(40.0, -3.0, 0.0)))
        # (with no enemy it is the pattern: the bot's, tick for tick)
        table = subprocess.run([str(self.program), "table", "7", "2", "900"], capture_output=True, text=True).stdout.splitlines()
        plain = subprocess.run([str(self.program), "table", "7", "0", "900"], capture_output=True, text=True).stdout.splitlines()
        self.assertEqual(table, plain)

    def test_it_walks_turns_and_fires(self):
        """what the fight on b30 needs of it: it moves, turns, fires the right trigger for 9 ticks in 90 and presses A and the left trigger"""
        table = [tuple(map(int, line.split())) for line in subprocess.run([str(self.program), "table", "7", "0", "900"], capture_output=True, text=True).stdout.splitlines()]
        self.assertGreater(max(row[2] for row in table), 30000)
        self.assertLess(min(row[2] for row in table), -30000)
        self.assertGreater(max(row[3] for row in table), 10000)
        self.assertTrue(85 <= sum(1 for row in table if row[4]) <= 100)
        self.assertTrue(any(row[5] for row in table) and any(row[6] for row in table))

    def test_the_constants_are_the_games(self):
        text = (ROOT / "port/include/xdk/xdk_xbox.h").read_text()
        for name, value in (("XINPUT_GAMEPAD_A", 0), ("XINPUT_GAMEPAD_LEFT_TRIGGER", 6), ("XINPUT_GAMEPAD_RIGHT_TRIGGER", 7)):
            self.assertRegex(text, rf"#define {name} {value}\b")

    def test_both_input_layers_play_it(self):
        sdl = (ROOT / "port/linux/src/xinput_sdl.c").read_text()
        wiiu = (ROOT / "port/wiiu/src/xinput_wiiu.c").read_text()
        for source in (sdl, wiiu):
            self.assertIn('#include "test_input_bot.h"', source)
            self.assertIn("test_input_bot_pad(", source)
            self.assertIn("test_input_bot_parse(", source)
        # (the console's: port 0's, from the ticks, while a game runs, before the skip's hold takes what a cinematic keeps)
        self.assertRegex(wiiu, r"if \(port == 0\)\s+test_input_gamepad\(&state->Gamepad\);\s+hold_to_skip\(")
        self.assertRegex(wiiu, r"seconds = network_test_game_seconds\(\);\s+if \(seconds < 0\.0\)\s+return;")
        self.assertNotIn("SDL_GetTicks", wiiu)


if __name__ == "__main__":
    unittest.main()
