"""The computer setup helper's small pieces (tools/wiiu/setup_extras.py): the SD card check, the Mac leftovers, dragged paths and the space check.
No game data is used."""
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/wiiu"))

import setup_assistant  # noqa: E402
import setup_extras  # noqa: E402
from disc_image import SetupError  # noqa: E402
from disc_image import RETAIL_MAPS  # noqa: E402


def make_card(root, maps=True, shaders=True):
    app = Path(root) / "wiiu/apps/halo"
    (app / "data/maps").mkdir(parents=True)
    (app / "halo.wuhb").write_bytes(b"x")
    if shaders:
        (app / "shaders.pack").write_bytes(b"x")
    (app / "brokers.txt").write_text("x")
    if maps:
        for name in RETAIL_MAPS:
            (app / "data/maps" / name).write_bytes(b"x")
    return app


class CheckTest(unittest.TestCase):
    def test_a_complete_card_passes(self):
        with tempfile.TemporaryDirectory() as card:
            make_card(card)
            self.assertTrue(all(ok for ok, _ in setup_extras.check_card(card)))

    def test_a_card_without_the_shader_pack_or_maps_says_what_is_missing(self):
        with tempfile.TemporaryDirectory() as card:
            make_card(card, maps=False, shaders=False)
            text = " | ".join(t for ok, t in setup_extras.check_card(card) if not ok)
            self.assertIn("shaders.pack is missing", text)
            self.assertIn("no maps yet", text)

    def test_a_partly_converted_card_names_the_missing_maps(self):
        with tempfile.TemporaryDirectory() as card:
            app = make_card(card)
            (app / "data/maps" / RETAIL_MAPS[3]).unlink()
            text = " | ".join(t for ok, t in setup_extras.check_card(card) if not ok)
            self.assertIn("1 maps are missing", text)
            self.assertIn(RETAIL_MAPS[3], text)


class MacLeftoversTest(unittest.TestCase):
    def test_only_dot_underscore_files_and_ds_store_are_removed(self):
        with tempfile.TemporaryDirectory() as card:
            app = make_card(card)
            (app / "._halo.wuhb").write_bytes(b"x")
            (app / ".DS_Store").write_bytes(b"x")
            (app / "data/maps/._a10.map").write_bytes(b"x")
            (app / "data/saved").mkdir()
            (app / "data/saved/save.bin").write_bytes(b"keep")
            self.assertEqual(setup_extras.clean_metadata(app), 3)
            self.assertTrue((app / "halo.wuhb").is_file())
            self.assertTrue((app / "data/maps" / RETAIL_MAPS[0]).is_file())
            self.assertEqual((app / "data/saved/save.bin").read_bytes(), b"keep")

    def test_a_missing_folder_is_fine(self):
        self.assertEqual(setup_extras.clean_metadata("/does/not/exist"), 0)


class PathsTest(unittest.TestCase):
    def test_dragged_paths_lose_their_quotes_and_escapes(self):
        self.assertEqual(setup_extras.clean_path("  '/Users/me/My Games/Halo.iso' "), "/Users/me/My Games/Halo.iso")
        self.assertEqual(setup_extras.clean_path(r"/Users/me/My\ Games/Halo.iso "), "/Users/me/My Games/Halo.iso")
        self.assertEqual(setup_extras.clean_path(""), "")

    def test_a_small_card_is_refused_with_its_numbers(self):
        with tempfile.TemporaryDirectory() as card:
            self.assertIsNone(setup_extras.space_problem(card, needed=1))
            problem = setup_extras.space_problem(card, needed=10**18)
            self.assertIn("GB free", problem)

 

class BundleTest(unittest.TestCase):
    def fake_release(self, root):
        app = Path(root) / "app"
        app.mkdir()
        (app / "halo.wuhb").write_bytes(b"new app")
        (app / "shaders.pack").write_bytes(b"new pack")
        (app / "brokers.txt").write_text("new brokers")
        return app

    def test_a_card_that_has_its_maps_is_updated_without_an_iso(self):
        with tempfile.TemporaryDirectory() as release, tempfile.TemporaryDirectory() as card:
            self.fake_release(release)
            app = make_card(card)
            (app / "halo.wuhb").write_bytes(b"old app")
            (app / "brokers.txt").write_text("my brokers")
            old_root, setup_extras.ROOT = setup_extras.ROOT, Path(release)
            try:
                messages = []
                setup_assistant.setup_card("", card, lambda text, *_: messages.append(text))
            finally:
                setup_extras.ROOT = old_root
            self.assertEqual((app / "halo.wuhb").read_bytes(), b"new app")
            self.assertEqual((app / "halo.wuhb.backup").read_bytes(), b"old app")
            self.assertEqual((app / "shaders.pack").read_bytes(), b"new pack")
            self.assertEqual((app / "brokers.txt").read_text(), "my brokers")
            self.assertTrue(messages[-1].startswith("Maps ready. The game is updated."))

    def test_a_card_without_maps_and_no_iso_is_refused_before_anything_is_written(self):
        with tempfile.TemporaryDirectory() as release, tempfile.TemporaryDirectory() as card:
            self.fake_release(release)
            old_root, setup_extras.ROOT = setup_extras.ROOT, Path(release)
            try:
                with self.assertRaises(SetupError):
                    setup_assistant.setup_card("", card)
            finally:
                setup_extras.ROOT = old_root
            self.assertFalse((Path(card) / "wiiu").exists())

    def test_without_a_release_folder_nothing_is_installed(self):
        with tempfile.TemporaryDirectory() as release, tempfile.TemporaryDirectory() as card:
            old_root, setup_extras.ROOT = setup_extras.ROOT, Path(release)
            try:
                self.assertIsNone(setup_extras.bundle_dir())
                self.assertEqual(setup_extras.install_app(card), [])
            finally:
                setup_extras.ROOT = old_root


if __name__ == "__main__":
    unittest.main()
