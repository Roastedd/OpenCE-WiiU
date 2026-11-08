"""The Wii U's smaller copies of the high-res HUD's largest sheets: tools/wiiu_hud_sheets.py (what rows are kept, the shrunk masks), the scale
port/wiiu/src/wiiu_hud_sheet.h gives the game's v coordinate, and tools/embed_assets.py --wiiu putting the copies in their sheets' place.

python3 -m unittest discover -s tools -p test_wiiu_hud_sheets.py        (the tests that read the sheets need NumPy and Pillow, as tools/hud_assets.py does)
"""
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

try:
    import numpy as np
    import wiiu_hud_sheets as sheets
except ImportError:  # pragma: no cover
    np = None
    sheets = None
import embed_assets

HEADER = ROOT / "port/wiiu/src/wiiu_hud_sheet.h"
TEST = ROOT / "port/wiiu/tests/test_hud_sheet.c"
CHAIN_HEADER = ROOT / "port/wiiu/src/wiiu_png_chain.h"
needs_numpy = unittest.skipIf(np is None, "NumPy and Pillow are needed")


def c_compile(folder, source, name, extra=()):
    command = [os.environ.get("CC", "cc"), *shlex.split(os.environ.get("HUD_SHEET_TARGET", "")), "-std=gnu11", "-O2", "-Wall", "-Werror", *extra,
               str(source), "-lm", "-o", str(folder / name)]
    built = subprocess.run(command, capture_output=True, text=True)
    if built.returncode:
        raise AssertionError(built.stdout + built.stderr)
    return folder / name


class ScaleTest(unittest.TestCase):
    def run_test(self, header_text=None):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            (folder / "src").mkdir()
            (folder / "tests").mkdir()
            (folder / "src/wiiu_hud_sheet.h").write_text(header_text if header_text is not None else HEADER.read_text())
            (folder / "tests/test_hud_sheet.c").write_text(TEST.read_text())
            check = c_compile(folder, folder / "tests/test_hud_sheet.c", "check")
            return subprocess.run([*shlex.split(os.environ.get("HUD_SHEET_RUNNER", "")), str(check)], capture_output=True, text=True)

    def test_the_game_finds_every_row_through_the_copy(self):
        result = self.run_test()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("0 failures", result.stdout)

    def test_an_inverted_scale_is_found(self):
        broken = HEADER.read_text().replace("(double)sheet_height * (double)width / ((double)height * (double)sheet_width)", "(double)height * (double)sheet_width / ((double)sheet_height * (double)width)", 1)
        self.assertNotEqual(broken, HEADER.read_text())
        self.assertNotEqual(self.run_test(broken).returncode, 0)

    def test_a_scale_that_ignores_the_width_is_found(self):
        broken = HEADER.read_text().replace("(double)sheet_height * (double)width /", "(double)sheet_height /", 1).replace("((double)height * (double)sheet_width)", "(double)height", 1)
        self.assertNotEqual(broken, HEADER.read_text())
        self.assertNotEqual(self.run_test(broken).returncode, 0)

    def test_a_sheet_that_waits_for_nobody_is_found(self):
        # (a sheet asked for and the builder not done: waiting, not made on the game thread)
        broken = HEADER.read_text().replace("return builder_done ? WIIU_HUD_SHEET_MAKE_HERE : WIIU_HUD_SHEET_WAIT;", "return WIIU_HUD_SHEET_MAKE_HERE;", 1)
        self.assertNotEqual(broken, HEADER.read_text())
        self.assertNotEqual(self.run_test(broken).returncode, 0)

    def test_a_builder_that_is_never_given_up_on_is_found(self):
        # (a sheet the builder was done with and holds nothing of would wait for ever, and the game would never draw it)
        broken = HEADER.read_text().replace("return builder_done ? WIIU_HUD_SHEET_MAKE_HERE : WIIU_HUD_SHEET_WAIT;", "return WIIU_HUD_SHEET_WAIT;", 1)
        self.assertNotEqual(broken, HEADER.read_text())
        self.assertNotEqual(self.run_test(broken).returncode, 0)

    def test_a_ready_sheet_that_is_not_taken_is_found(self):
        broken = HEADER.read_text().replace("\tif (ahead == WIIU_HUD_AHEAD_READY)\n\t\treturn WIIU_HUD_SHEET_TAKE_AHEAD;\n", "", 1)
        self.assertNotEqual(broken, HEADER.read_text())
        self.assertNotEqual(self.run_test(broken).returncode, 0)

    def test_a_copy_taller_than_its_sheet_is_found(self):
        broken = HEADER.read_text().replace("return height * divisor <= sheet_height;", "return height * divisor <= sheet_height * 2;", 1)
        self.assertNotEqual(broken, HEADER.read_text())
        self.assertNotEqual(self.run_test(broken).returncode, 0)


@needs_numpy
class ToolTest(unittest.TestCase):
    def synthetic(self, content_rows, blue_below=True, height=2048, width=256):
        """A sheet with content (all four channels set) in its top rows and, below, blue and nothing else."""
        rng = np.random.default_rng(7)
        rgba = np.zeros((height, width, 4), dtype=np.uint8)
        rgba[:content_rows] = rng.integers(1, 256, size=(content_rows, width, 4), dtype=np.uint8)
        if blue_below:
            rgba[content_rows:, :, 2] = rng.integers(0, 256, size=(height - content_rows, width), dtype=np.uint8)
        return rgba

    def test_a_crop_keeps_the_content_one_empty_row_and_a_multiple_of_64(self):
        for content in (1, 63, 64, 65, 100, 545, 664, 700):
            rgba = self.synthetic(content)
            rows = sheets.crop_rows(rgba)
            self.assertEqual(rows % 64, 0, content)
            self.assertGreater(rows, content, content)           # at least one row with nothing in it is kept
            self.assertLess(rows, content + 65, content)          # and no more than the multiple needs
            self.assertEqual(sheets.used_rows(np.ascontiguousarray(rgba[rows - 1:rows])), 0)  # the row the sampler's clamp repeats has no red, green or alpha

    def test_the_sprites_reach_decides_when_it_is_lower_than_the_texels(self):
        rgba = self.synthetic(100)
        self.assertEqual(sheets.crop_rows(rgba, sprite_rows=300), 320)
        self.assertEqual(sheets.crop_rows(rgba, sprite_rows=64), 128)     # (the texels' 100)
        self.assertEqual(sheets.crop_rows(rgba, sprite_rows=319), 320)
        self.assertEqual(sheets.crop_rows(rgba, sprite_rows=320), 384)    # row 320 is the sprites' first row under; one empty row is still kept

    def test_a_sheet_with_nothing_cut_is_kept_whole(self):
        rgba = self.synthetic(2047, height=2048)
        self.assertEqual(sheets.crop_rows(rgba), 2048)

    def test_the_rows_kept_are_the_sheets_and_nothing_else_is_in_the_copy(self):
        rgba = self.synthetic(300)
        copy, entry = sheets.make_copy("synthetic", "crop", rgba, {})
        self.assertEqual(copy.shape, (320, 256, 4))
        self.assertTrue(np.array_equal(copy, rgba[:320]))
        self.assertEqual((entry["source_height"], entry["height"], entry["width"]), (2048, 320, 256))
        self.assertGreater(entry["blue_only_texels_dropped"], 0)

    def test_the_copy_is_sampled_as_the_sheet_is(self):
        """Linear filtering in v with the sampler's clamp (what the HUD's sampler does), the sheet against its copy at the game's v times the scale: the same
        texel for every v of the sheet, wherever the sheet has nothing under the rows the copy holds."""
        rgba = self.synthetic(300, blue_below=False)
        copy, _ = sheets.make_copy("synthetic", "crop", rgba, {})
        sheet_h, copy_h = rgba.shape[0], copy.shape[0]
        scale = np.float32(sheet_h) / np.float32(copy_h)

        def sample(image, v):
            h = image.shape[0]
            y = v * h - 0.5
            y0 = np.floor(y).astype(int)
            f = (y - y0)[:, None]
            a = image[np.clip(y0, 0, h - 1), 7].astype(float)
            b = image[np.clip(y0 + 1, 0, h - 1), 7].astype(float)
            return a * (1 - f) + b * f

        v = (np.arange(sheet_h * 4) + 0.5) / (sheet_h * 4)
        self.assertTrue(np.allclose(sample(rgba, v), sample(copy, v * scale), atol=1e-3))

    def test_a_shrunk_sheet_is_the_games_first_mip_level(self):
        """wiiu_png_next_level (the code that makes the levels on the console) on the sheet, against shrink."""
        rng = np.random.default_rng(3)
        rgba = rng.integers(0, 256, size=(64, 128, 4), dtype=np.uint8)
        rgba[::7, ::5] = 255  # (the rounding at the top of the range)
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            source = folder / "level.c"
            source.write_text(
                '#include <stdio.h>\n#include <stdlib.h>\n#include <stdint.h>\n#include "' + str(CHAIN_HEADER) + '"\n'
                "int main(void){ enum { W = 128, H = 64 }; static uint32_t above[W * H], next[W / 2 * H / 2];\n"
                "if (fread(above, 4, W * H, stdin) != W * H) return 1;\n"
                "wiiu_png_next_level(above, W, H, next, W / 2, H / 2); fwrite(next, 4, W / 2 * H / 2, stdout); return 0; }\n")
            check = c_compile(folder, source, "level")
            out = subprocess.run([str(check)], input=rgba.tobytes(), capture_output=True)
            self.assertEqual(out.returncode, 0)
            self.assertTrue(np.array_equal(np.frombuffer(out.stdout, dtype=np.uint8).reshape(32, 64, 4), sheets.shrink(rgba)))

    def test_edges_are_exact_for_the_heights_kept_and_the_four_that_are_not_are_skipped(self):
        self.assertEqual([k for k in range(64, 2048, 64) if not sheets.edges_exact(2048, k)], [1600, 1728, 1856, 1984])
        rgba = self.synthetic(1550, blue_below=False)
        self.assertEqual(sheets.crop_rows(rgba), 1664)        # (1600 would be next, and its edges are read one row up)
        rgba = self.synthetic(1990, blue_below=False)
        self.assertEqual(sheets.crop_rows(rgba), 2048)

    def test_the_mip_levels_of_a_copy_are_the_sheets_to_the_sixth(self):
        """The levels the console makes (the mean of each 2 by 2) of the sheet and of its copy: the copy's rows are the sheet's top rows of every level to the sixth (every
        copy is a multiple of 64 rows), which the HUD's sprites, drawn at one to a few texels in eight, never go beyond."""
        for entry in json.loads(sheets.SHEETS_JSON.read_text())["assets"]:
            if entry["how"] != "crop":
                continue
            full = sheets.load_rgba(sheets.HUD_ASSETS / f"{entry['name']}.png")
            copy = sheets.load_rgba(sheets.WIIU_ASSETS / f"{entry['name']}.png")
            for level in range(1, 7):
                full, copy = sheets.shrink(full), sheets.shrink(copy)
                self.assertTrue(np.array_equal(full[:copy.shape[0]], copy), (entry["name"], level))

    def test_the_games_own_png_decoder_reads_the_committed_copies(self):
        """hud_hires.c's png_decode (the code the console runs, taken out of the source as it is) on every copy: the pixels are the tool's."""
        text = (ROOT / "port/linux/src/hud_hires.c").read_text()
        start = text.index("static unsigned long big_endian_long")
        end = text.index("#ifdef HALO_WIIU\nunsigned char *hud_hires_png_pixels")
        limit = [line for line in text.splitlines() if line.startswith("#define MAXIMUM_DECODED_SIZE")]
        self.assertEqual(len(limit), 1)
        entries = json.loads(sheets.SHEETS_JSON.read_text())["assets"]
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            source = folder / "decode.c"
            source.write_text("#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <zlib.h>\n" + limit[0] + "\n"
                              "#define platform_log(...) fprintf(stderr, __VA_ARGS__)\n" + text[start:end] +
                              "int main(int argc, char **argv){ FILE *f = fopen(argv[1], \"rb\"); static unsigned char data[1 << 24]; unsigned long size = fread(data, 1, sizeof(data), f), w = 0, h = 0;\n"
                              "unsigned char *pixels = png_decode(data, size, &w, &h); if (!pixels) return 1; printf(\"%lu %lu\\n\", w, h); fwrite(pixels, 4, w * h, stdout); return 0; }\n")
            decode = c_compile(folder, source, "decode", extra=["-lz"])
            for entry in entries:
                path = sheets.WIIU_ASSETS / f"{entry['name']}.png"
                out = subprocess.run([str(decode), str(path)], capture_output=True)
                self.assertEqual(out.returncode, 0, entry["name"] + out.stderr.decode())
                header, _, body = out.stdout.partition(b"\n")
                self.assertEqual(header.decode().split(), [str(entry["width"]), str(entry["height"])], entry["name"])
                self.assertTrue(np.array_equal(np.frombuffer(body, dtype=np.uint8).reshape(entry["height"], entry["width"], 4), sheets.load_rgba(path)), entry["name"])

    def test_the_committed_copies_are_what_the_sheets_make(self):
        self.assertEqual(sheets.build(write=False), [])

    def test_no_sprite_of_the_committed_sheets_reaches_what_was_cut(self):
        layout = sheets.layout_assets_load()
        for entry in json.loads(sheets.SHEETS_JSON.read_text())["assets"]:
            if entry["how"] != "crop":
                continue
            reach = sheets.sprite_rows(entry["name"], layout)
            self.assertLess(reach, entry["height"], entry["name"])
            source = sheets.load_rgba(sheets.HUD_ASSETS / f"{entry['name']}.png")
            self.assertEqual(sheets.used_rows(np.ascontiguousarray(source[entry["height"] - 1:])), 0, entry["name"])
            copy = sheets.load_rgba(sheets.WIIU_ASSETS / f"{entry['name']}.png")
            self.assertTrue(np.array_equal(copy, source[:entry["height"]]), entry["name"])
            self.assertEqual(entry["height"] % 64, 0)
            self.assertTrue(sheets.edges_exact(entry["source_height"], entry["height"]), entry["name"])


class EmbedTest(unittest.TestCase):
    def sheet(self, name="hud_damage_arrows__0"):
        return embed_assets.wiiu_sheets()[name]

    def test_the_list_names_files_that_exist_and_are_the_size_it_says(self):
        for name, sheet in embed_assets.wiiu_sheets().items():
            source = (embed_assets.ROOT / "port/assets/hud" / f"{name}.png").read_bytes()
            data = embed_assets.wiiu_sheet_data(sheet, source, embed_assets.png_size(source, name))
            self.assertEqual(embed_assets.png_size(data, name), (sheet["width"], sheet["height"]))
            self.assertLess(len(data), 400000)

    def test_a_sheet_changed_since_its_copy_was_made_stops_the_build(self):
        sheet = dict(self.sheet())
        source = (embed_assets.ROOT / "port/assets/hud/hud_damage_arrows__0.png").read_bytes()
        size = embed_assets.png_size(source, "x")
        embed_assets.wiiu_sheet_data(sheet, source, size)
        with self.assertRaises(SystemExit):
            embed_assets.wiiu_sheet_data(sheet, source + b"\0", size)
        with self.assertRaises(SystemExit):
            embed_assets.wiiu_sheet_data(dict(sheet, source_sha256=hashlib.sha256(b"other").hexdigest()), source, size)

    def test_a_copy_that_is_not_the_listed_size_stops_the_build(self):
        sheet = dict(self.sheet())
        source = (embed_assets.ROOT / "port/assets/hud/hud_damage_arrows__0.png").read_bytes()
        with self.assertRaises(SystemExit):
            embed_assets.wiiu_sheet_data(dict(sheet, height=sheet["height"] + 64), source, embed_assets.png_size(source, "x"))

    def test_the_wiiu_table_keeps_the_sheets_size_and_embeds_the_copy(self):
        with tempfile.TemporaryDirectory() as folder:
            wiiu, plain = Path(folder) / "wiiu.c", Path(folder) / "plain.c"
            for output, option in ((wiiu, "--wiiu"), (plain, "--big-endian")):
                subprocess.run([sys.executable, "-I", str(ROOT / "tools/embed_assets.py"), str(output), option], check=True, capture_output=True)
            wiiu_text, plain_text = wiiu.read_text(), plain.read_text()
        def line(text):
            return [l for l in text.splitlines() if 'combined\\\\hud_ammo_meters", 0, 2048, 2048, 0xba4441c7u' in l]
        self.assertEqual(len(line(wiiu_text)), 1)
        self.assertEqual(len(line(plain_text)), 1)
        wiiu_size = int(line(wiiu_text)[0].rstrip("},").split(",")[-1])
        plain_size = int(line(plain_text)[0].rstrip("},").split(",")[-1])
        self.assertLess(wiiu_size, plain_size)
        self.assertEqual(wiiu_size, (embed_assets.ROOT / "port/assets/wiiu/hud_ammo_meters__0.png").stat().st_size)
        self.assertEqual(plain_size, (embed_assets.ROOT / "port/assets/hud/hud_ammo_meters__0.png").stat().st_size)
        self.assertLess(len(wiiu_text), len(plain_text))


if __name__ == "__main__":
    unittest.main()
