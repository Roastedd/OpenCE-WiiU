"""The menus' file work, counted on the host (reports/research/2026-11-09/menulag-result.md): the Create Game screens' gametype list asked for every row's name and
the chosen gametype's variant and options in every frame (a read of z:\\saved\\hdmu.map and of the variant's file through the file layer each: on a card with the
default gametypes some 300 calls a frame, 215 ms on the Wii U), and a card with no profile yet looked for z:\\lastprof.txt in every frame.

The real game under qemu-ppc (tools/test_wiiu_game_digest.py's program, the Wii U's menus and saved games) opens the gametype list, with the file layer's counting on (port/linux/include/halo_file_counts.h, the card's
halo/filecount.txt on a console: HALO_FILECOUNT here) and reads from its log how many of the menu frames touched the card and the calls they made. Three runs of 700 frames (the scripted player leaves the screen in the 246th; a summary every 300 menu frames):

  kept          the lists keep what they read (the default): the first frames read the card, then none do
  not kept      halo/menu_cache.txt 0 (HALO_MENU_CACHE=0): the lists read the card in every frame as they did: the negative control. The test fails if this run does NOT show
                the lag, so that a counting layer that counts nothing, or a screen that reads nothing, cannot pass
  lastprof      the profile looked for once a second: a tree with the old test (a mutation of player_ui.c) is the negative control for it
  not kept      and the frames drawn are the same ones: the lists show what they showed

and two runs of the main menu from the start (the start-up as the console has it) with the directory cache on and off (HALO_DIRECTORY_CACHE=0): the same frames, and the
cache answers lookups from what it has and the card is asked less.

The three runs of the gametype screen use the old start-up (HALO_STARTUP=0, which gives the saved games' checks the string lists to make the default gametypes with) and the
directory cache off: that start-up has the checks' thread and the screen made at load working on the saved games at once, which the game does not do on a console (the screen
is opened long after the checks have ended), and which crashes it now and then (1 run of 8 in the base). A run that crashes is made again, up to five times.

    GAME_DIGEST_MENULAG=1 python3 -m unittest tools.test_wiiu_menulag      (Docker's opence-ppc-be image and data-be's maps, as tools/test_wiiu_game_digest.py), and tools.test_wiiu_directory_cache (the cache on its own)
"""
import os
import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parent / "wiiu"))
import digest_compare  # noqa: E402
import test_wiiu_game_digest as digest  # noqa: E402

PROGRAMS = [None]
SCREEN = "main_menu/gametype_select/gametype_select_screen"
FRAMES = 700
# (the old start-up: the saved games' checks come after the string lists are loaded, so that the default gametypes are made on the card; HALO_STARTUP=0)
ENVIRONMENT = {"HALO_STARTUP": "0", "HALO_FILECOUNT": "1", "HALO_SCREENS": "0", "HALO_MENU_OPEN": SCREEN, "HALO_DIRECTORY_CACHE": "0"}
# the old test of the profile's look-up, for the control
LASTPROF = ("source/interface/player_ui.c", "(!looked_in_vain || system_milliseconds() - looked_in_vain_time >= 1000))", "(1))")


def counts(out: Path):
    """(the menu frames that touched the card, the card calls they made, the most calls a frame made, the menu frames counted), from the log's summaries, less the first frame: it
    is the screen's opening (the default gametypes made on this card, the lists read for the first time), which is not what a frame costs once the screen is up"""
    log = (out / "stderr.txt").read_text(errors="replace")
    summaries = re.findall(r"files: (\d+) menu frames: (\d+) touched the card, (\d+) card calls, worst frame (\d+) \((\d+) calls", log)
    if not summaries:
        raise AssertionError(f"{out}: the log has no file counts:\n" + log[-2000:])
    said = {int(n): int(c) for n, c in re.findall(r"files: menu frame (\d+): (\d+) card calls", log)}
    first = said.get(1, 0)
    return (sum(int(s[1]) for s in summaries) - (1 if first else 0), sum(int(s[2]) for s in summaries) - first, max([c for n, c in said.items() if n > 1] or [0]),
            sum(int(s[0]) for s in summaries))


def play(program, name, extra, environment=ENVIRONMENT, frames=FRAMES):
    """the run, made again (up to five times) if it crashed or halted; (the folder of its output)"""
    os.environ["GAME_DIGEST_ENV"] = ",".join(f"{key}={value}" for key, value in dict(environment, **extra).items())
    for attempt in range(5):
        process, out = digest.start(PROGRAMS[0], name, frames, mode="menu")
        try:
            digest.finish(process, out)
        except AssertionError:
            continue
        if not re.search(r"halted|Aborted|Segmentation|corrupted", (out / "stderr.txt").read_text(errors="replace")):
            return out
    raise AssertionError(f"{name}: the game crashed five times")


@unittest.skipUnless(os.environ.get("GAME_DIGEST_MENULAG"), "the menus' file work: GAME_DIGEST_MENULAG=1 (and Docker's opence-ppc-be image, data-be's maps; runs of 700 frames)")
class MenuLagTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        PROGRAMS[:] = [digest.build("menulag")]
        cls.outs = {"kept": play(None, "menulag-kept", {}), "not-kept": play(None, "menulag-not-kept", {"HALO_MENU_CACHE": "0"})}
        PROGRAMS[:] = [digest.build("menulag-lastprof", mutations=[LASTPROF])]
        cls.outs["lastprof-old"] = play(None, "menulag-lastprof-old", {})
        PROGRAMS[:] = [digest.build("menulag")]
        cls.counts = {name: counts(out) for name, out in cls.outs.items()}
        # (the main menu from the start, the console's start-up)
        cls.start = {on: play(None, f"menulag-start-{on}", {"HALO_DIRECTORY_CACHE": on}, {"HALO_FILECOUNT": "1"}, 700) for on in ("1", "0")}

    def test_the_lists_that_keep_what_they_read_leave_the_card_alone(self):
        touched, calls, worst, frames = self.counts["kept"]
        self.assertGreaterEqual(frames, 600, "the run did not reach 600 menu frames")
        # (the first frames read the rows' names and the chosen gametype's variant, a new chosen one is read once, and the profile is looked for once a second)
        self.assertLessEqual(touched, 20, f"{touched} of {frames} menu frames touched the card")
        self.assertLessEqual(calls, 3000, f"{calls} card calls in {frames} menu frames")

    def test_the_negative_control_shows_the_lag(self):
        # the lists reading the card in every frame, as they did: every frame on the screen touches it, hundreds of calls each
        touched, calls, worst, frames = self.counts["not-kept"]
        # (the game's scripted player leaves the screen in the 246th menu frame: the frames on the screen are those)
        self.assertGreaterEqual(touched, 200, f"only {touched} of {frames} menu frames touched the card without the lists keeping what they read")
        self.assertGreaterEqual(worst, 150, f"the busiest frame made only {worst} card calls")
        self.assertGreaterEqual(calls, 20 * self.counts["kept"][1], f"{calls} calls against {self.counts['kept'][1]} kept")

    def test_a_card_with_no_profile_is_looked_in_once_a_second(self):
        # the old look-up, in every frame on the screen (13 calls: the drive's folder, the folder of the saved games, the file's name, not there), against about one frame in sixty
        touched, calls, worst, frames = self.counts["lastprof-old"]
        self.assertGreaterEqual(touched, 200, f"only {touched} of {frames} menu frames looked for the profile with the old test")
        self.assertLess(self.counts["kept"][0], touched / 10)

    def test_the_frames_drawn_are_the_same(self):
        with open(os.devnull, "w") as quiet:
            self.assertEqual(digest_compare.compare(self.outs["kept"], self.outs["not-kept"], out=quiet, menu=True), 0)

    def test_the_directory_cache_answers_lookups_and_the_frames_are_the_same(self):
        with open(os.devnull, "w") as quiet:
            self.assertEqual(digest_compare.compare(self.start["1"], self.start["0"], out=quiet, menu=True), 0)
        first = {on: re.search(r"files: menu frame 1: (\d+) card calls \(.*?(\d+) answered from the cache", (out / "stderr.txt").read_text(errors="replace")) for on, out in self.start.items()}
        self.assertTrue(first["1"] and first["0"], "the first menu frame's file count is not in the log")
        self.assertGreaterEqual(int(first["1"].group(2)), 20, "the cache answered few lookups")
        self.assertEqual(int(first["0"].group(2)), 0, "the run with the cache off had lookups answered from it")
        self.assertLess(int(first["1"].group(1)), 0.9 * int(first["0"].group(1)), f"{first['1'].group(1)} card calls with the cache, {first['0'].group(1)} without")


if __name__ == "__main__":
    unittest.main()
