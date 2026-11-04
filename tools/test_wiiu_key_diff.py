"""tools/wiiu/key_diff.py: which shader keys a run needs that the pack's list lacks (made-up keys, no console)."""
import io
import contextlib
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "wiiu"))
import key_diff  # noqa: E402


def pixel(fill):
    return "P " + ("%02x" % fill) * 256


def vertex(words, mask=0, bad_hash=False):
    program = key_diff.fnv(struct.pack("<I", len(words) // 4) + struct.pack("<%dI" % len(words), *words))
    if bad_hash:
        program ^= 1
    return "V %016x %d %d %s" % (program, mask, len(words) // 4, " ".join("%08x" % word for word in words))


class KeyDiffTest(unittest.TestCase):
    def run_tool(self, needed, have):
        with tempfile.TemporaryDirectory() as folder:
            a, b, out = Path(folder, "needed.txt"), Path(folder, "have.txt"), Path(folder, "missing.txt")
            a.write_text("\n".join(needed) + "\n")
            b.write_text("\n".join(have) + "\n")
            output, errors = io.StringIO(), io.StringIO()
            with contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
                status = key_diff.main([str(a), str(b), "-o", str(out)])
            return status, output.getvalue(), errors.getvalue(), out.read_text().splitlines()

    def test_nothing_missing_is_status_zero(self):
        status, text, _, missing = self.run_tool([pixel(1), vertex([1, 2, 3, 4])], [pixel(1), pixel(2), vertex([1, 2, 3, 4])])
        self.assertEqual((status, missing), (0, []))
        self.assertIn("missing: 0 pixel keys, 0 vertex keys", text)

    def test_missing_keys_are_written_in_the_packs_format(self):
        status, text, _, missing = self.run_tool([pixel(1), pixel(3), vertex([1, 2, 3, 4], 5)], [pixel(1)])
        self.assertEqual(status, 1)
        self.assertEqual(missing, [pixel(3), vertex([1, 2, 3, 4], 5)])
        self.assertIn("missing: 1 pixel keys, 1 vertex keys", text)

    def test_a_vertex_key_is_its_program_and_mask(self):
        status, _, _, missing = self.run_tool([vertex([1, 2, 3, 4], 0), vertex([1, 2, 3, 4], 7)], [vertex([1, 2, 3, 4], 0)])
        self.assertEqual((status, missing), (1, [vertex([1, 2, 3, 4], 7)]))

    def test_case_and_comments_do_not_matter(self):
        status, _, _, missing = self.run_tool(["# a comment", pixel(0xab).upper().replace("P", "P"), ""], [pixel(0xab)])
        self.assertEqual((status, missing), (0, []))

    def test_a_vertex_key_whose_words_do_not_hash_to_its_program_is_refused(self):
        status, text, errors, missing = self.run_tool([vertex([1, 2, 3, 4], bad_hash=True), pixel(9)], [])
        self.assertEqual(missing, [pixel(9)])
        self.assertIn("do not hash", errors)
        self.assertIn("1 lines refused", text)

    def test_a_short_pixel_key_is_refused(self):
        status, _, errors, missing = self.run_tool(["P " + "00" * 255], [])
        self.assertEqual((status, missing), (0, []))
        self.assertIn("512 hex digits", errors)

    def test_the_real_list_reads_clean(self):
        # (a copy of tools/wiiu/shader_keys.txt of the development repository, if this checkout sits in it)
        for folder in ROOT.parents:
            keys = folder / "tools" / "wiiu" / "shader_keys.txt"
            if keys.is_file():
                have, broken, problem = key_diff.read_keys(str(keys))
                self.assertEqual((broken, problem), (0, None))
                self.assertEqual(key_diff.diff(have, have), [])
                return
        self.skipTest("no tools/wiiu/shader_keys.txt above this checkout")


if __name__ == "__main__":
    unittest.main()
