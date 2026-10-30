"""Actual fetch-cache source against the frozen original descriptor producer.

FETCH_TARGET='--target=powerpc-linux-gnu -fsigned-char -fuse-ld=lld -no-pie'
FETCH_RUNNER='qemu-ppc -L /usr/powerpc-linux-gnu' runs the same cases big-endian.
FETCH_SOURCE_PATH selects an off-production candidate while staging this test.
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

try:
    import resource
except ImportError:
    resource = None

ROOT = Path(__file__).resolve().parent.parent
OPENCE = ROOT
if not (OPENCE / "port/wiiu/src/wiiu_gx2.c").is_file():
    OPENCE = next(parent / "upstream/OpenCE" for parent in ROOT.parents
                  if (parent / "upstream/OpenCE/tools/harness").is_dir())
sys.path.insert(0, str(OPENCE / "tools"))
from harness import function, enum_with, structure

SOURCE = Path(os.environ.get("FETCH_SOURCE_PATH", OPENCE / "port/wiiu/src/wiiu_gx2.c"))
COMMANDS = OPENCE / "port/wiiu/src/dk_commands.h"
TEST = ROOT / "port/wiiu/tests/test_fetch_cache.c"
LEGACY = ROOT / "port/wiiu/tests/fetch_legacy.inc"
QUALIFIED = ROOT / "port/wiiu/tests/fetch_qualified_fast.inc"
PROVENANCE = ROOT / "port/wiiu/tests/fetch_qualification.json"
LEGACY_SHA256 = "4fbbafc2c544f22aec8dafd49b6d00daa744794c6059e45201787430259abe57"
QUALIFIED_SHA256 = "0e683dcebdef3ab905e749a40521ae0c67292430746f5fa7a3c898d3dbabfbb8"


def current_code(source):
    """Extract actual declarations/implementation; only force the test hash."""
    start = source.index("struct fetch_attribute_key\n{")
    end = source.index("/* ---------- vertex data:", start)
    region = source[start:end]
    body = function(region, "fetch_layout_hash")
    if body.count("return hash;") != 1:
        raise ValueError("Unexpected hash-return shape; review the extraction")
    region = region.replace(body, body.replace("return hash;", "return hash & test_hash_mask;"), 1)
    draw = function(source, "draw")
    begin = draw.index("\tif (gx.cur_fetch != fetch")
    stop = draw.index("\tif (gx.cur_ps != ps)", begin)
    binding = "static void extracted_draw_bind(struct fetch *fetch, const struct shader *vs)\n{\n" + draw[begin:stop] + "}\n"
    return "\n\n".join([LEGACY.read_text(), function(source, "attribute_spec"), region, binding])


class FetchCacheTest(unittest.TestCase):
    def run_fixture(self, mutation=None, sanitizer=False, source_mutation=None):
        source = SOURCE.read_text()
        if source_mutation:
            before, after = source_mutation
            self.assertEqual(source.count(before), 1, before)
            source = source.replace(before, after, 1)
        code = current_code(source)
        if mutation:
            before, after = mutation
            self.assertEqual(code.count(before), 1, before)
            code = code.replace(before, after, 1)
        header = COMMANDS.read_text()
        types = enum_with(header, "DK_ATTRIBUTE_UNFED") + "\n#define DK_ATTRIBUTE_COUNT 16\n#define DK_STREAM_COUNT 16\n"
        types += structure(header, "dk_attribute") + "\n" + structure(header, "dk_vertex_format")
        with tempfile.TemporaryDirectory(prefix="opence-fetch-cache-") as folder:
            folder = Path(folder)
            (folder / "source-extracted.inc").write_text(code)
            (folder / "source-types.inc").write_text(types)
            # Keep quoted includes beside this exact mutation, not the fixture.
            (folder / "test_fetch_cache.c").write_text(TEST.read_text())
            command = [os.environ.get("CC", "cc"), *shlex.split(os.environ.get("FETCH_TARGET", "")),
                       "-std=gnu11", "-O2", "-g", "-Wall", "-Wextra", "-Werror", "-ffp-contract=off",
                       "-fno-strict-aliasing", "-fwrapv", "-I" + str(folder), str(folder / "test_fetch_cache.c"),
                       "-o", str(folder / "check")]
            if sanitizer:
                command[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            # Expected-abort controls must not leave host/emulated core files.
            def no_core():
                resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
            options = {"preexec_fn": no_core} if resource is not None else {}
            return subprocess.run([*shlex.split(os.environ.get("FETCH_RUNNER", "")), str(folder / "check")],
                                  capture_output=True, text=True, timeout=60, **options)

    def test_actual_cache_producer_binding_ownership_and_limits(self):
        result = self.run_fixture()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("10000 extracted-producer cases", result.stdout)

    @unittest.skipIf(bool(os.environ.get("FETCH_TARGET")), "sanitizer uses the native host")
    def test_native_address_and_undefined_sanitizers(self):
        result = self.run_fixture(sanitizer=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_original_descriptor_oracle_and_capacity_are_pinned(self):
        metadata = json.loads(PROVENANCE.read_text())
        self.assertEqual(hashlib.sha256(LEGACY.read_bytes()).hexdigest(), LEGACY_SHA256)
        self.assertEqual(metadata["legacy_oracle_sha256"], LEGACY_SHA256)
        self.assertRegex(SOURCE.read_text(), r"(?m)^#define FETCH_LIMIT 2048$")
        self.assertNotIn("test_hash_mask", SOURCE.read_text())

    def test_cache_and_binding_match_the_native_qualified_input(self):
        metadata = json.loads(PROVENANCE.read_text())
        frozen = QUALIFIED.read_text()
        self.assertEqual(hashlib.sha256(QUALIFIED.read_bytes()).hexdigest(), QUALIFIED_SHA256)
        self.assertEqual(metadata["qualified_cache_input_sha256"], QUALIFIED_SHA256)
        source = SOURCE.read_text()
        start = source.index("struct fetch_attribute_key\n{")
        end = source.index("/* ---------- vertex data:", start)
        region, binding = frozen.split("\n/* QUALIFIED_FETCH_BINDING */\n")
        self.assertEqual(source[start:end], region)
        draw = function(source, "draw")
        begin = draw.index("\tif (gx.cur_fetch != fetch")
        stop = draw.index("\tif (gx.cur_ps != ps)", begin)
        self.assertEqual(draw[begin:stop], binding)
        self.assertEqual(hashlib.sha256(function(source, "attribute_spec").encode()).hexdigest(),
                         metadata["qualified_attribute_spec_sha256"])

    def assert_fault(self, before, after):
        result = self.run_fixture((before, after))
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue("Assertion" in result.stderr or "assertion" in result.stderr,
                        result.stdout + result.stderr)

    def test_omitted_offset_is_detected(self):
        self.assert_fault("memcpy(out, attribute, sizeof(*out));",
                          "memcpy(out, attribute, sizeof(*out)); out->words[2] = 0;")

    def test_omitted_mask_is_detected(self):
        self.assert_fault("memcpy(out, attribute, sizeof(*out));",
                          "memcpy(out, attribute, sizeof(*out)); out->words[6] = 0;")

    def test_omitted_endian_is_detected(self):
        self.assert_fault("memcpy(out, attribute, sizeof(*out));",
                          "memcpy(out, attribute, sizeof(*out)); out->words[7] = 0;")

    def test_omitted_equality_endian_is_detected(self):
        self.assert_fault("words[7] != (uint32_t)attribute->endianSwap", "0")

    def test_omitted_count_is_detected(self):
        self.assert_fault("if (entry->count != count)", "if (0)")

    def test_hash_only_hit_is_detected(self):
        self.assert_fault("fetch->key == key && fetch_layout_equal(fetch, attributes, count)",
                          "fetch->key == key || fetch_layout_equal(fetch, attributes, count)")

    def test_omitted_bucket_high_bits_is_detected(self):
        self.assert_fault("hash ^= hash >> 16;\n\thash ^= hash >> 8;", "hash ^= 0;")

    def test_omitted_vs_switch_rebind_is_detected(self):
        self.assert_fault("if (gx.cur_fetch != fetch || gx.cur_vs != vs)", "if (gx.cur_fetch != fetch)")

    def test_changed_current_producer_mapping_is_detected(self):
        result = self.run_fixture(source_mutation=(
            "GX2_ATTRIB_FORMAT_FLOAT_32_32_32, SELECT(X, Y, Z, 1)",
            "GX2_ATTRIB_FORMAT_FLOAT_32_32_32, SELECT(X, Y, Z, 0)"))
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue("Assertion" in result.stderr or "assertion" in result.stderr,
                        result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
