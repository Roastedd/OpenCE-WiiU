"""tools/wiiu/instancing_census.py on traces made up for it: the counts of repeats, of adjacent repeats, of repeated blocks (the parts of one model drawn again for a second object), of
draws hoisted within a window, and what it does not count (a draw that differs in its mesh or in its state is not a repeat). Each of the census's rules is also broken on purpose and
the test then fails.

python3 -m unittest tools.test_wiiu_instancing_census
"""
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "wiiu"))
import instancing_census  # noqa: E402


def draw(frame, number, mesh="m0", state="s0", constants="c0", pixel="p0", vertex="v0"):
    return (f"draw {frame}.{number}: prim 5 count 74 offset 0 indices {mesh} s0@6481e0+1184:{mesh}; targets t; state {state} vertex key k pixel key q; textures i1:a:b - - -; "
            f"vertex parameters {vertex} pixel parameters {pixel} constants {constants} format f")


def trace(frames):
    lines = []
    for index, draws in enumerate(frames):
        lines.append(f"present {index}: t")
        lines.append(f"# frame {index} tick 1")
        lines.extend(draw(index, n, *spec) for n, spec in enumerate(draws))
    lines.append(f"present {len(frames)}: t")
    return "\n".join(lines) + "\n"


def census(frames, rule=None):
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder) / "trace.txt"
        path.write_text(trace(frames))
        return instancing_census.census(path)


def frame_of(specs, pad=20):
    """a frame of the given (mesh, state, constants) draws, padded with draws of their own to the census's 20 draws a frame"""
    return list(specs) + [(f"pad{i}", "s0", f"c{i}") for i in range(max(0, pad - len(specs)))]


class CensusTest(unittest.TestCase):
    def test_adjacent_repeats_and_blocks(self):
        # A A A (three of one group in a row), B C B C B C (a block of two three times), D alone
        specs = [("A", "s0", "c1"), ("A", "s0", "c2"), ("A", "s0", "c3"), ("B", "s0", "c1"), ("C", "s0", "c1"), ("B", "s0", "c2"), ("C", "s0", "c2"), ("B", "s0", "c3"), ("C", "s0", "c3"),
                 ("D", "s0", "c1")]
        total = census([frame_of(specs)] * 3)
        self.assertEqual(total["frames"], 3)
        self.assertEqual(total["repeat"] / 3, 2 + 4)  # A twice more; B twice, C twice
        self.assertEqual(total["adjacent"] / 3, 2)  # A A, A A
        self.assertEqual(total["blocks"] / 3, 2 + 4)  # A A A: a block of 1 repeated twice; B C B C B C: a block of 2 repeated twice (4 draws)
        self.assertEqual(total["window2"] / 3, 2 + 4)  # the two adjacent and the four of the block (a gap of 2)
        self.assertTrue(total["window2"] <= total["window3"] <= total["window8"] <= total["repeat"])

    def test_a_change_of_state_or_mesh_is_not_a_repeat(self):
        specs = [("A", "s0", "c1"), ("A", "s1", "c1"), ("A2", "s0", "c1"), ("A", "s0", "c1")]
        total = census([frame_of(specs)])
        self.assertEqual(total["repeat"], 1)  # only the last A (mesh A, state s0) repeats the first
        self.assertEqual(total["adjacent"], 0)
        self.assertEqual(total["same_mesh_other_state"], 1)

    def test_the_per_instance_inputs_are_told_apart(self):
        total = census([frame_of([("A", "s0", "c1"), ("A", "s0", "c2")])])
        self.assertEqual((total["repeat"], total["repeat_constants_differ"], total["repeat_identical"]), (1, 1, 0))
        same = census([frame_of([("A", "s0", "c1"), ("A", "s0", "c1")])])
        self.assertEqual((same["repeat"], same["repeat_constants_differ"], same["repeat_identical"]), (1, 0, 1))

    def test_a_short_frame_is_a_menu_and_is_left_out(self):
        self.assertEqual(census([[("A", "s0", "c1")] * 5])["frames"], 0)

    def test_a_group_with_other_draws_between_is_not_a_block(self):
        total = census([frame_of([("A", "s0", "c1"), ("B", "s0", "c1"), ("A", "s0", "c2"), ("C", "s0", "c1"), ("A", "s0", "c3"), ("D", "s0", "c1")])])
        self.assertEqual((total["repeat"], total["blocks"], total["adjacent"]), (2, 0, 0))

    def test_the_window_counts_the_gap(self):
        specs = [("A", "s0", "c1"), ("B", "s0", "c1"), ("C", "s0", "c1"), ("D", "s0", "c1"), ("A", "s0", "c2")]
        total = census([frame_of(specs)])
        self.assertEqual((total["window3"], total["window4"]), (0, 1))  # the repeat of A is 4 draws after

    def test_each_rule_broken_is_caught(self):
        source = (ROOT / "tools/wiiu/instancing_census.py").read_text()
        specs = [("A", "s0", "c1"), ("A", "s0", "c2"), ("B", "s0", "c1"), ("C", "s0", "c1"), ("B", "s0", "c2"), ("C", "s0", "c2")]
        frames = [frame_of(specs), frame_of([("A", "s0", "c1"), ("A", "s1", "c1")]), frame_of([("A", "s0", "c1"), ("B", "s0", "c1"), ("A", "s0", "c2"), ("C", "s0", "c1"), ("A", "s0", "c3"),
                                                                                               ("D", "s0", "c1")])]
        good = census(frames)
        broken = {
            "the state is left out of the group": ("group = (mesh, state)", "group = (mesh,)"),
            "an adjacent draw is not told": ("if gap == 1:", "if gap == 0:"),
            "a block is a repeat of any distance": ("run = run + 1 if ids[i] == ids[i - period] else 0", "run = run + 1 if ids[i] in ids[:i] else 0"),
        }
        for name, (before, after) in broken.items():
            with self.subTest(name):
                self.assertEqual(source.count(before), 1, name)
                with tempfile.TemporaryDirectory() as folder:
                    module = Path(folder) / "instancing_census.py"
                    module.write_text(source.replace(before, after, 1))
                    namespace = {"__name__": "mutated"}
                    exec(compile(module.read_text(), str(module), "exec"), namespace)
                    path = Path(folder) / "trace.txt"
                    path.write_text(trace(frames))
                    self.assertNotEqual(dict(namespace["census"](path)), dict(good), name)


if __name__ == "__main__":
    unittest.main()
