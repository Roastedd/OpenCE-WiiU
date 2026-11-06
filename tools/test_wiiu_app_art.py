"""The Wii U app's icon and start-up pictures (tools/wiiu_app_art.py): the files in port/wiiu/meta are what the script draws, at the sizes
wuhbtool wants (128x128, 1280x720, 854x480), and the build gives them to wuhbtool with the game's name.
Skipped where Pillow is not installed (the pictures are checked wherever the script can run)."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

try:
    from PIL import Image
except ImportError:  # pragma: no cover
    Image = None


@unittest.skipIf(Image is None, "needs Pillow")
class AppArtTest(unittest.TestCase):
    def test_the_files_are_what_the_script_draws_at_the_sizes_wuhbtool_wants(self):
        import wiiu_app_art

        sizes = {"icon.png": (128, 128), "splash-tv.png": (1280, 720), "splash-drc.png": (854, 480)}
        for name, image in wiiu_app_art.pictures().items():
            self.assertEqual(image.size, sizes[name], name)
            on_disk = Image.open(wiiu_app_art.META / name)
            self.assertEqual(on_disk.size, sizes[name], name)
            self.assertTrue(wiiu_app_art.same_picture(on_disk, image), f"{name}: run tools/wiiu_app_art.py")

    def test_the_font_the_titles_are_set_in_is_in_the_checkout(self):
        import wiiu_app_art

        self.assertTrue(wiiu_app_art.BLACK_FONT.is_file())
        self.assertTrue(wiiu_app_art.MEDIUM_FONT.is_file())


class BuildNamesTest(unittest.TestCase):
    def test_the_wuhb_rule_carries_the_name_and_the_pictures(self):
        text = (ROOT / "tools" / "wiiu_build.py").read_text()
        self.assertIn('WUHB_NAME = "Halo: Combat Evolved (OpenCE)"', text)
        self.assertIn("--icon=$icon --tv-image=$tv_image --drc-image=$drc_image", text)


if __name__ == "__main__":
    unittest.main()
