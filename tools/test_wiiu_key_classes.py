"""tools/wiiu/key_classes.py: pixel keys grouped by canonical key, against a key list (made-up keys, the game's own shader_key_canonical, no console)."""
import contextlib
import io
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "wiiu"))
sys.path.insert(0, str(ROOT / "tools"))
import key_classes  # noqa: E402

# the byte offsets of shader_key_wire.h's order: 57 combiner words and the texture modes (232 bytes), 4 sampler types, 4 alpha kills, 4 color signs,
# the alpha test function (4 bytes), then fog_enable, fog_table_mode, count_samples, coverage_alpha, point_threshold, per_pixel_lighting, alpha_test_samples, source_blend
COMBINER_COUNT = 53 * 4
ALPHA_INPUTS_5 = 5 * 4
FOG_ENABLE = 248
FOG_MODE = 249


def key(**changes):
    """a pixel key of one general combiner stage (count 1), with bytes changed: {offset: value}"""
    wire = bytearray(256)
    wire[COMBINER_COUNT] = 1
    for offset, value in changes.items():
        wire[int(offset[1:])] = value
    return "P " + wire.hex()


def vertex(words, mask=0):
    program = key_classes.key_diff.fnv(struct.pack("<I", len(words) // 4) + struct.pack("<%dI" % len(words), *words))
    return "V %016x %d %d %s" % (program, mask, len(words) // 4, " ".join("%08x" % word for word in words))


class KeyClassesTest(unittest.TestCase):
    def run_tool(self, needed, have):
        with tempfile.TemporaryDirectory() as folder:
            a, b, out, classes = Path(folder, "needed.txt"), Path(folder, "have.txt"), Path(folder, "lacked.txt"), Path(folder, "classes.txt")
            a.write_text("\n".join(needed) + "\n")
            b.write_text("\n".join(have) + "\n")
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                status = key_classes.main([str(a), "--have", str(b), "-o", str(out), "--classes", str(classes)])
            return status, output.getvalue(), out.read_text().splitlines(), classes.read_text().splitlines()

    def test_a_key_that_differs_where_the_generator_does_not_look_is_in_the_class(self):
        # (a stage past the count: its inputs are never read; the list's key and the needed one have one shader)
        base = key()
        past_count = key(**{"o%d" % ALPHA_INPUTS_5: 0x44})
        status, text, lacked, classes = self.run_tool([past_count], [base])
        self.assertEqual(status, 0, text)
        self.assertEqual(lacked, [past_count])
        self.assertEqual(classes, [])
        self.assertIn("1 of them in classes it has", text)

    def test_a_key_the_generator_reads_is_a_new_class(self):
        # (fog on is another shader than fog off: nv2a_psh.c writes the fog)
        status, text, lacked, classes = self.run_tool([key(**{"o%d" % FOG_ENABLE: 1})], [key()])
        self.assertEqual(status, 1, text)
        self.assertEqual(len(lacked), 1)
        self.assertEqual(len(classes), 1)
        self.assertIn("1 in 1 new classes", text)

    def test_the_fog_mode_without_fog_is_not_read(self):
        status, text, _, classes = self.run_tool([key(**{"o%d" % FOG_MODE: 3})], [key()])
        self.assertEqual((status, classes), (0, []), text)

    def test_nothing_lacked_and_vertex_keys_counted(self):
        status, text, lacked, classes = self.run_tool([key(), vertex([1, 2, 3, 4])], [key(), vertex([1, 2, 3, 4]), vertex([5, 6, 7, 8])])
        self.assertEqual((status, lacked, classes), (0, [], []), text)
        lacked_vertex = self.run_tool([key(), vertex([1, 2, 3, 4], 14)], [key(), vertex([1, 2, 3, 4])])
        self.assertEqual(lacked_vertex[0], 0)
        self.assertEqual(len(lacked_vertex[2]), 1)
        self.assertTrue(lacked_vertex[2][0].startswith("V "))


if __name__ == "__main__":
    unittest.main()
