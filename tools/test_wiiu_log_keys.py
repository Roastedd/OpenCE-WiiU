"""tools/wiiu/log_keys.py: the stand-in keys a Wii U log writes out in four lines each come back as the capture's lines.

python3 -m unittest discover -s tools -p test_wiiu_log_keys.py
"""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location("log_keys", ROOT / "tools/wiiu/log_keys.py")
log_keys = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(log_keys)


def parts(key, wire, drop=None):
    return [f"12.5 shaders: key P {key} {n + 1}/4 {wire[n * 128:(n + 1) * 128]}" for n in range(4) if n + 1 != drop]


class LogKeysTest(unittest.TestCase):
    def run_logs(self, *texts):
        with tempfile.TemporaryDirectory() as folder:
            paths = []
            for index, text in enumerate(texts):
                path = Path(folder) / f"log{index}.txt"
                path.write_text(text)
                paths.append(path)
            return log_keys.read(paths)

    def test_the_four_parts_join_into_the_key(self):
        wire = "".join(f"{n:02x}" for n in range(256))
        keys, draws = self.run_logs("\n".join(["1.0 other", *parts("00112233aabbccdd", wire), "13.0 shaders: stand-in key 00112233aabbccdd drew 233 times in this window (233 in all)"]))
        self.assertEqual(keys, {"00112233aabbccdd": wire})
        self.assertEqual(draws, {"00112233aabbccdd": 233})

    def test_a_key_with_a_part_missing_is_left_out(self):
        wire = "ab" * 256
        keys, _ = self.run_logs("\n".join(parts("0000000000000001", wire, drop=3)))
        self.assertEqual(keys, {})

    def test_a_key_of_the_wrong_length_is_left_out(self):
        keys, _ = self.run_logs("\n".join(parts("0000000000000002", "cd" * 256)[:3] + ["1.0 shaders: key P 0000000000000002 4/4 abcd"]))
        self.assertEqual(keys, {})

    def test_a_key_in_two_logs_is_once(self):
        wire = "ef" * 256
        text = "\n".join(parts("0000000000000003", wire))
        keys, _ = self.run_logs(text, text)
        self.assertEqual(list(keys), ["0000000000000003"])


if __name__ == "__main__":
    unittest.main()
