"""Actual Wii U IMA decoder against its native-qualified input and legacy path.

ADPCM_TARGET='--target=powerpc-linux-gnu -fuse-ld=lld -no-pie'
ADPCM_RUNNER='qemu-ppc -L /usr/powerpc-linux-gnu' runs the same cases big-endian.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from harness import function

SOURCE = ROOT / "port/linux/src/dsound_sdl.c"
FROZEN = ROOT / "port/wiiu/tests/adpcm_qualified.inc"
PROVENANCE = ROOT / "port/wiiu/tests/adpcm_qualification.json"
TEST = ROOT / "port/wiiu/tests/test_adpcm.c"
QUALIFIED_SHA256 = "7aa9696357c0a07dcb264c4dea02bfdf5a4ad195bd4196f1d0f983e42ad5414b"


def declaration(text, name):
    """One named static array declaration, retaining the real initializer."""
    match = re.search(r"^static const [^;\n]+\b" + re.escape(name) + r"\[[^;=]+?=\s*\{", text, re.M)
    if not match:
        raise ValueError("missing array: " + name)
    end = text.index("{", match.start()) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[match.start():text.index(";", end) + 1]


def table(text):
    match = re.search(r"^struct ima_decode_entry\s*\{[^}]+\};", text, re.M)
    if not match:
        raise ValueError("missing IMA entry definition")
    return match.group(0) + "\n" + declaration(text, "ima_decode_table")


def current_code(text):
    code = "\n\n".join([declaration(text, "ima_index_table"), declaration(text, "ima_step_table"),
                            table(text), function(text, "ima_expand"), function(text, "decode_adpcm")])
    pieces = ["#define packet_allocate malloc\n"]
    names = {"ima_decode_entry": "entry", "ima_decode_table": "table", "ima_index_table": "index_table",
             "ima_step_table": "step_table", "ima_expand": "expand", "decode_adpcm": "decode"}
    for mode in ("fast", "legacy"):
        prefix = "production_" + mode + "_"
        pieces.append("#define HALO_WIIU\n" if mode == "fast" else "#undef HALO_WIIU\n")
        pieces.extend("#define " + name + " " + prefix + suffix + "\n" for name, suffix in names.items())
        pieces.append(code + "\n")
        pieces.extend("#undef " + name + "\n" for name in names)
        pieces.append("#undef HALO_WIIU\n")
    return "".join(pieces)


def selected(code, wiiu):
    command = [os.environ.get("CC", "cc"), *shlex.split(os.environ.get("ADPCM_TARGET", "")),
               "-E", "-P", "-x", "c", *(["-DHALO_WIIU"] if wiiu else []), "-"]
    return subprocess.run(command, input=code, capture_output=True, text=True, check=True).stdout.split()


class AdpcmTest(unittest.TestCase):
    def run_fixture(self, mutate=None, sanitizer=False):
        text = SOURCE.read_text()
        code = current_code(text)
        if mutate:
            before, after = mutate
            self.assertIn(before, code)
            code = code.replace(before, after, 1)
        with tempfile.TemporaryDirectory(prefix="opence-adpcm-") as folder:
            folder = Path(folder)
            (folder / "adpcm_current.inc").write_text(code)
            command = [os.environ.get("CC", "cc"), *shlex.split(os.environ.get("ADPCM_TARGET", "")),
                       "-std=gnu11", "-O2", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                       "-Wno-unused-variable", "-ffp-contract=off", "-fno-strict-aliasing", "-fwrapv",
                       "-I" + str(folder), str(TEST), "-o", str(folder / "check")]
            if sanitizer:
                command[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return subprocess.run([*shlex.split(os.environ.get("ADPCM_RUNNER", "")), str(folder / "check")],
                                  capture_output=True, text=True, timeout=120)

    def test_production_table_and_kernel_match_the_native_qualified_input(self):
        metadata = json.loads(PROVENANCE.read_text())
        self.assertEqual(hashlib.sha256(FROZEN.read_bytes()).hexdigest(), QUALIFIED_SHA256)
        self.assertEqual(metadata["qualified_input_sha256"], QUALIFIED_SHA256)
        self.assertEqual(hashlib.sha256((FROZEN.parent / metadata["native_probe_source"]).read_bytes()).hexdigest(),
                         metadata["native_probe_source_sha256"])
        source, frozen = SOURCE.read_text(), FROZEN.read_text()
        self.assertEqual(table(source), table(frozen))
        self.assertEqual(selected(function(source, "ima_expand"), True),
                         selected(function(frozen, "ima_expand_cached").replace("ima_expand_cached(", "ima_expand(", 1), True))
        self.assertEqual(function(source, "decode_adpcm").replace("packet_allocate(", "malloc(", 1),
                         function(frozen, "decode_original").replace("decode_original(", "decode_adpcm(", 1))

    def test_other_platform_arrays_and_kernel_are_unchanged(self):
        source, frozen = SOURCE.read_text(), FROZEN.read_text()
        for name in ("ima_index_table", "ima_step_table"):
            self.assertEqual(declaration(source, name), declaration(frozen, name))
        self.assertEqual(selected(function(source, "ima_expand"), False), selected(function(frozen, "ima_expand"), False))

    def test_actual_decoder_exact_transitions_and_complete_packets(self):
        result = self.run_fixture()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("256 complete mono/stereo packets", result.stdout)

    @unittest.skipIf(bool(os.environ.get("ADPCM_TARGET")), "sanitizer uses the native host")
    def test_native_address_and_undefined_sanitizers(self):
        result = self.run_fixture(sanitizer=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_changed_difference_is_detected(self):
        result = self.run_fixture(("{0,0}", "{1,0}"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ADPCM check failed", result.stderr)

    def test_changed_next_index_is_detected(self):
        result = self.run_fixture(("{0,0}", "{0,1}"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ADPCM check failed", result.stderr)

    def test_changed_predictor_clamp_is_detected(self):
        result = self.run_fixture(("if (*predictor > 32767) *predictor = 32767;", "if (*predictor > 32767) *predictor = 32766;"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ADPCM check failed", result.stderr)


if __name__ == "__main__":
    unittest.main()
