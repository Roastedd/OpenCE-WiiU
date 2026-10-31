"""The Wii U release script (tools/wiiu/make_wiiu_release.py), with a signing key made for the test and thrown away after it.

    python3 -m pytest -q tools/test_wiiu_release.py

A release is made from a folder whose halo.wuhb is a stand-in WUHB (an RPX with a compressed section holding the build's record of
itself, as wiiu_update.c's), signed, and checked three ways: by the script, by openssl, and by the console's own code
(port/wiiu/tests/test_update.c's verify and parse, over wiiu_update_core.c and Monocypher). Each thing the script must refuse is
refused: the bundle of another build, or trusting another key, or looking in another repository, or reading other maps; a key that
is not the one the builds trust; a file missing, empty or one a release may not own. The build's real bundle, if one is built,
must hold its record.
"""

import hashlib
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/wiiu"))
import make_wiiu_release as release  # noqa: E402

OPENSSL = shutil.which("openssl")
CC = "cc"


def fake_rpx(record: bytes, compressed: bool = True) -> bytes:
    """a big-endian ELF with one section of data holding record (elf2rpl's compression: its size, then zlib)"""
    data = b"\x00some rodata\x00" + record + b"\x00more\x00"
    body = struct.pack(">I", len(data)) + zlib.compress(data) if compressed else data
    section_offset = 0x40
    body_offset = section_offset + 2 * 40
    header = b"\x7fELF" + bytes([1, 2, 1, 0xCA]) + bytes(8)
    header += struct.pack(">HHIIIIIHHHHHH", 0xFE01, 20, 1, 0, 0, section_offset, 0, 52, 0, 0, 40, 2, 0)
    header += bytes(section_offset - len(header))
    sections = struct.pack(">10I", *([0] * 10))
    sections += struct.pack(">10I", 0, 1, 0x08000002 if compressed else 2, 0x10000000, body_offset, len(body), 0, 0, 4, 0)
    return header + sections + body


def fake_wuhb(rpx: bytes) -> bytes:
    """wuhbtool's container, as far as the script reads it: the header, the files' data, a table of files with code/root.rpx"""
    file_data = 0x200
    table = struct.pack(">IIQQII", 0, 0xFFFFFFFF, 0, len(rpx), 0xFFFFFFFF, 8) + b"root.rpx"
    table_offset = file_data + len(rpx)
    header = b"WUHB" + struct.pack(">I", 0x50) + struct.pack(">9Q", 0, 0, 0, 0, 0, 0, table_offset, len(table), file_data)
    return header + bytes(file_data - len(header)) + rpx + table


def record(build=124, maps=None, repository="owner/halo-wiiu-releases", key=""):
    maps = maps or release.maps_format()
    return f"OpenCE Wii U update: build={build} maps={maps} repository={repository} key={key};".encode()


@unittest.skipUnless(OPENSSL, "needs openssl")
class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.folder = Path(tempfile.mkdtemp(prefix="opence-release-"))
        self.key = self.folder / "throwaway-key.pem"
        made = subprocess.run([OPENSSL, "genpkey", "-algorithm", "ed25519", "-out", str(self.key)], capture_output=True)
        self.assertEqual(made.returncode, 0, made.stderr)
        self.public = release.public_key_of(self.key)
        self.files = self.folder / "files"
        self.files.mkdir()
        (self.files / "shaders.pack").write_bytes(b"HSP1" + bytes(range(256)) * 40)
        (self.files / "brokers.txt").write_text("broker.example:1883\n")
        self.bundle(record(key=self.public))

    def tearDown(self):
        shutil.rmtree(self.folder, ignore_errors=True)

    def bundle(self, text: bytes, compressed=True):
        (self.files / "halo.wuhb").write_bytes(fake_wuhb(fake_rpx(text, compressed)))

    def make(self, build=124, **options):
        return release.make_release(self.files, build, self.key, **options)

    def refused(self, needle, build=124, **options):
        with self.assertRaises(release.Refused) as caught:
            self.make(build, **options)
        self.assertIn(needle, str(caught.exception))
        return str(caught.exception)

    def console(self):
        """the console's code, built here: test_update verify KEY MANIFEST SIGNATURE and test_update parse MANIFEST"""
        executable = self.folder / "test_update"
        source = ROOT / "port/wiiu/tests/test_update.c"
        built = subprocess.run([CC, "-std=gnu11", "-O1", f"-I{ROOT / 'port/wiiu/src'}", f"-I{ROOT / 'port/third_party/monocypher'}", str(source),
                                str(ROOT / "port/wiiu/src/wiiu_update_core.c"), str(ROOT / "port/third_party/monocypher/monocypher.c"),
                                str(ROOT / "port/third_party/monocypher/monocypher-ed25519.c"), "-o", str(executable)], capture_output=True, text=True)
        self.assertEqual(built.returncode, 0, built.stderr)
        return executable

    def test_a_release_is_made_signed_and_checked_three_ways(self):
        self.assertEqual(self.make(expected_key=self.public, repository="owner/halo-wiiu-releases"), "owner/halo-wiiu-releases")
        manifest = (self.files / release.MANIFEST).read_bytes()
        lines = manifest.decode().splitlines()
        self.assertEqual(lines[:4], ["halo-wiiu-manifest 1", "build 124", "tag wiiu-124", f"maps {release.maps_format()}"])
        self.assertEqual([line.split()[1] for line in lines[4:]], ["shaders.pack", "brokers.txt", "halo.wuhb"])
        for line in lines[4:]:
            _, name, size, digest = line.split()
            data = (self.files / name).read_bytes()
            self.assertEqual((int(size), digest), (len(data), hashlib.sha256(data).hexdigest()))
        signature = self.files / release.SIGNATURE
        self.assertEqual(signature.stat().st_size, 64)
        console = self.console()
        checked = subprocess.run([str(console), "verify", self.public, str(self.files / release.MANIFEST), str(signature)], capture_output=True, text=True)
        self.assertEqual(checked.stdout.strip(), "valid")
        parsed = subprocess.run([str(console), "parse", str(self.files / release.MANIFEST)], capture_output=True, text=True)
        self.assertEqual(parsed.stdout.strip(), "ok build 124 files 3")
        # (a byte of the manifest changed after signing, and the console refuses it)
        (self.files / release.MANIFEST).write_bytes(manifest.replace(b"build 124", b"build 125"))
        checked = subprocess.run([str(console), "verify", self.public, str(self.files / release.MANIFEST), str(signature)], capture_output=True, text=True)
        self.assertEqual(checked.stdout.strip(), "invalid")

    def test_the_bundle_of_another_build_is_refused(self):
        self.refused("is build 124, not 125", build=125)

    def test_a_bundle_trusting_another_key_is_refused(self):
        self.bundle(record(key="ab" * 32))
        self.refused("trusts the key abab")
        self.bundle(record(key=""))
        self.refused("trusts the key (none)")

    def test_a_signing_key_the_builds_do_not_trust_is_refused(self):
        self.refused("is not the key the builds trust", expected_key="cd" * 32)

    def test_a_bundle_of_another_repository_or_none_is_refused(self):
        self.refused("looks for releases in owner/halo-wiiu-releases, not someone/else", repository="someone/else")
        self.bundle(record(key=self.public, repository=""))
        self.refused("names no releases' repository")

    def test_a_bundle_reading_other_maps_is_refused(self):
        self.bundle(record(key=self.public, maps="80000000"))
        self.refused("reads maps 80000000")

    def test_a_bundle_without_its_record_or_not_a_bundle_is_refused(self):
        self.bundle(b"nothing of the sort")
        self.refused("has no record of its updater")
        (self.files / "halo.wuhb").write_bytes(b"not a wuhb at all" * 10)
        self.refused("is not a WUHB")

    def test_an_uncompressed_section_is_read_too(self):
        self.bundle(record(key=self.public), compressed=False)
        self.make()

    def test_files_missing_empty_or_not_a_releases_are_refused(self):
        (self.files / "brokers.txt").unlink()
        self.refused("brokers.txt is not there")
        (self.files / "brokers.txt").write_bytes(b"")
        self.refused("is empty")
        (self.files / "config.toml").write_text("x = 1\n")
        self.refused("may not own a file named 'config.toml'", names=["config.toml", "halo.wuhb"])
        self.refused("a release has halo.wuhb", names=["shaders.pack"])
        self.refused("is not 1 to", build=0)

    def test_a_release_without_brokers_is_allowed_when_named(self):
        self.make(names=["shaders.pack", "halo.wuhb"])
        self.assertNotIn(b"brokers.txt", (self.files / release.MANIFEST).read_bytes())

    def test_the_command_line(self):
        script = ROOT / "tools/wiiu/make_wiiu_release.py"
        printed = subprocess.run([sys.executable, "-I", str(script), "--print-public-key", str(self.key)], capture_output=True, text=True)
        self.assertEqual(printed.stdout.strip(), self.public)
        made = subprocess.run([sys.executable, "-I", str(script), str(self.files), "--build", "124", "--key", str(self.key), "--public-key", self.public],
                              capture_output=True, text=True)
        self.assertEqual(made.returncode, 0, made.stderr)
        self.assertIn("Publish a release tagged wiiu-124 in owner/halo-wiiu-releases", made.stdout)
        refused = subprocess.run([sys.executable, "-I", str(script), str(self.files), "--build", "7", "--key", str(self.key)], capture_output=True, text=True)
        self.assertEqual(refused.returncode, 1)
        self.assertIn("refused:", refused.stderr)

    def test_the_scripts_reading_of_a_manifest_is_the_consoles(self):
        """the script's parser and wiiu_update_manifest_parse agree, line for line"""
        console = self.console()
        good = "halo-wiiu-manifest 1\nbuild 9\ntag wiiu-9\nmaps c1ea0000\nfile shaders.pack 10 " + "0" * 64 + "\nfile halo.wuhb 5 " + "f" * 64 + "\n"
        cases = [good, good.replace("build 9", "build 09"), good.replace("tag wiiu-9", "tag wiiu-8"), good.replace("shaders.pack", "Shaders.pack"),
                 good.replace("shaders.pack", "config.toml"), good.replace("file halo.wuhb", "file other.wuhb"), good.replace(" 10 ", " 0 "),
                 good.replace("maps c1ea0000", "maps C1EA0000"), good.replace("0" * 64, "0" * 63), good.replace("\n", "\r\n"), good + "\n",
                 good + "file shaders.pack 1 " + "a" * 64 + "\n", good.replace(" 10 ", "  10 ")]
        for index, text in enumerate(cases):
            path = self.folder / f"case{index}.txt"
            path.write_bytes(text.encode())
            try:
                release.parse_manifest(text.encode())
                script = True
            except release.Refused:
                script = False
            verdict = subprocess.run([str(console), "parse", str(path)], capture_output=True, text=True).stdout
            self.assertEqual(script, verdict.startswith("ok"), (index, text, verdict))

    def test_the_built_bundle_holds_its_record(self):
        built = ROOT / "build/linux-wiiu/halo.wuhb"
        if not built.is_file():
            self.skipTest("no Wii U build (build/linux-wiiu/halo.wuhb)")
        found = release.bundle_marker(built)
        self.assertEqual(found["maps"], release.maps_format())
        self.assertGreaterEqual(found["build"], 0)


if __name__ == "__main__":
    unittest.main()
