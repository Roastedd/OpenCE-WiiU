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

    GAME_DIGEST_MENULAG=1 python3 -m unittest tools.test_wiiu_menulag      (Docker's opence-ppc-be image and data-be's maps, as tools/test_wiiu_game_digest.py)
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

SCREEN = "main_menu/gametype_select/gametype_select_screen"
FRAMES = 700
# (the old start-up: the saved games' checks come after the string lists are loaded, so that the default gametypes are made on the card; HALO_STARTUP=0)
ENVIRONMENT = {"HALO_STARTUP": "0", "HALO_FILECOUNT": "1", "HALO_SCREENS": "0", "HALO_MENU_OPEN": SCREEN}
# the old test of the profile's look-up, for the control
LASTPROF = ("source/interface/player_ui.c", "(!looked_in_vain || system_milliseconds() - looked_in_vain_time >= 1000))", "(1))")


def counts(out: Path):
    """(the menu frames that touched the card, the card calls they made, the most calls a frame made) from the log's summaries"""
    log = (out / "stderr.txt").read_text(errors="replace")
    summaries = re.findall(r"files: (\d+) menu frames: (\d+) touched the card, (\d+) card calls, worst frame (\d+) \((\d+) calls", log)
    if not summaries:
        raise AssertionError(f"{out}: the log has no file counts:\n" + log[-2000:])
    return (sum(int(s[1]) for s in summaries), sum(int(s[2]) for s in summaries), max(int(s[4]) for s in summaries), sum(int(s[0]) for s in summaries))


def play(program, name, extra):
    os.environ["GAME_DIGEST_ENV"] = ",".join(f"{key}={value}" for key, value in dict(ENVIRONMENT, **extra).items())
    return digest.start(program, name, FRAMES, mode="menu")


@unittest.skipUnless(os.environ.get("GAME_DIGEST_MENULAG"), "the menus' file work: GAME_DIGEST_MENULAG=1 (and Docker's opence-ppc-be image, data-be's maps; runs of 700 frames)")
class MenuLagTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        program = digest.build("menulag")
        control = digest.build("menulag-lastprof", mutations=[LASTPROF])
        runs = {"kept": play(program, "menulag-kept", {}), "not-kept": play(program, "menulag-not-kept", {"HALO_MENU_CACHE": "0"}),
                "lastprof-old": play(control, "menulag-lastprof-old", {})}
        cls.outs = {}
        for name, run in runs.items():
            digest.finish(*run)
            cls.outs[name] = run[1]
        cls.counts = {name: counts(out) for name, out in cls.outs.items()}

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


if __name__ == "__main__":
    unittest.main()
