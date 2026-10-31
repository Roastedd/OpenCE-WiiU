#!/usr/bin/env python3
"""Makes a Wii U release's manifest and signs it: the files the Wii U's updater (port/wiiu/src/wiiu_update.c) downloads.

    python3 tools/wiiu/make_wiiu_release.py FOLDER --build N --key KEY.pem [--public-key HEX] [--repository OWNER/REPO]
    python3 tools/wiiu/make_wiiu_release.py --print-public-key KEY.pem

FOLDER holds the release's files: halo.wuhb (the build made with HALO_BUILD_NUMBER=N, HALO_UPDATE_REPOSITORY and
HALO_UPDATE_PUBLIC_KEY), shaders.pack (its shaders) and brokers.txt. The script writes, in FOLDER:

    halo-wiiu-manifest.txt      "halo-wiiu-manifest 1", "build N", "tag wiiu-N", "maps <format>", and "file <name> <size> <sha256>"
                                for each file, halo.wuhb last
    halo-wiiu-manifest.txt.sig  the manifest's Ed25519 signature (openssl pkeyutl -sign -rawin), 64 bytes

and checks what it wrote before it says so: the signature with openssl, the manifest with the same rules the console reads it by,
each file's size and SHA-256 again, and, inside halo.wuhb (its RPX, its compressed sections), the build's own record of its build
number, maps' format, repository and key, so that a release cannot be signed for a build that would not accept it, or that would
take it for another build. Upload the files and the two it wrote to a release tagged wiiu-N of the releases' repository; the
consoles find it as the latest release.

KEY.pem is the owner's Ed25519 private key (`openssl genpkey -algorithm ed25519`), kept off the machine that builds; nothing here
writes it anywhere. Needs Python 3 and OpenSSL 3 (for -rawin).
"""

import argparse
import hashlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
BUNDLE = "halo.wuhb"
FILES = ["shaders.pack", "brokers.txt", BUNDLE]
MANIFEST = "halo-wiiu-manifest.txt"
SIGNATURE = MANIFEST + ".sig"
# the console's bounds (wiiu_update_core.h)
MANIFEST_MOST = 4096
FILES_MOST = 8
NAME_BYTES = 48
FILE_MOST = 256 * 1024 * 1024
TOTAL_MOST = 512 * 1024 * 1024
BUILD_MOST = 999999999
MARKER = re.compile(rb"OpenCE Wii U update: build=(\d+) maps=([0-9a-z-]*) repository=([A-Za-z0-9_./-]*) key=([0-9a-f]*);")


class Refused(Exception):
    pass


def maps_format() -> str:
    """the maps' format a build reads: tools/wiiu/convert_maps.py's rebase, as wiiu_update.c's MAPS_FORMAT"""
    text = (HERE / "convert_maps.py").read_text()
    match = re.search(r"WIIU_REBASE = \((0x[0-9A-Fa-f]+) - 0x80000000\) & 0xFFFFFFFF", text)
    if not match:
        raise Refused("tools/wiiu/convert_maps.py has no WIIU_REBASE")
    return f"{(int(match.group(1), 16) - 0x80000000) & 0xFFFFFFFF:08x}"


def name_allowed(name: str) -> bool:
    """wiiu_update_name_allowed: the bundle, the brokers' list, and packs; one dot; lowercase letters, digits, . _ -"""
    if not name or len(name) >= NAME_BYTES or not re.fullmatch(r"[a-z0-9][a-z0-9._-]*", name):
        return False
    if name.count(".") > 1 or name.endswith("."):
        return False
    return name in (BUNDLE, "brokers.txt") or name.endswith(".pack")


def parse_manifest(data: bytes) -> dict:
    """the manifest as the console reads it (wiiu_update_manifest_parse): Refused if it would refuse it"""
    if not 0 < len(data) <= MANIFEST_MOST or not data.endswith(b"\n"):
        raise Refused("the manifest's size or end")
    if any(byte != 0x0A and not 0x20 <= byte <= 0x7E for byte in data):
        raise Refused("the manifest has a byte outside printable ASCII")
    lines = data.decode("ascii")[:-1].split("\n")
    for line in lines:
        if not line or line != line.strip(" ") or "  " in line:
            raise Refused(f"a line with spaces out of place: {line!r}")
    if len(lines) < 5 or lines[0] != "halo-wiiu-manifest 1":
        raise Refused("the first line, or too few lines")
    build = re.fullmatch(r"build ([1-9][0-9]{0,8})", lines[1])
    if not build:
        raise Refused("line 2 is not the build")
    number = int(build.group(1))
    if lines[2] != f"tag wiiu-{number}":
        raise Refused("line 3 is not the tag of the build")
    maps = re.fullmatch(r"maps ([0-9a-z-]{1,32})", lines[3])
    if not maps:
        raise Refused("line 4 is not the maps' format")
    files = {}
    for line in lines[4:]:
        match = re.fullmatch(r"file (\S+) ([1-9][0-9]*) ([0-9a-f]{64})", line)
        if not match:
            raise Refused(f"not a file line: {line!r}")
        name, size, digest = match.group(1), int(match.group(2)), match.group(3)
        if not name_allowed(name) or name in files or size > FILE_MOST:
            raise Refused(f"the file {name!r}")
        files[name] = (size, digest)
    if len(files) > FILES_MOST or sum(size for size, _ in files.values()) > TOTAL_MOST or BUNDLE not in files:
        raise Refused("the files: too many, too large, or no halo.wuhb")
    return {"build": number, "maps": maps.group(1), "files": files}


def manifest_text(build: int, maps: str, folder: Path, names: list) -> bytes:
    lines = ["halo-wiiu-manifest 1", f"build {build}", f"tag wiiu-{build}", f"maps {maps}"]
    for name in sorted(names, key=lambda name: name == BUNDLE):
        data = (folder / name).read_bytes()
        lines.append(f"file {name} {len(data)} {hashlib.sha256(data).hexdigest()}")
    return ("\n".join(lines) + "\n").encode("ascii")


# ---------- what the bundle says of itself

def bundle_rpx(data: bytes) -> bytes:
    """the RPX in a WUHB (wuhbtool's RomFS: a header of 64-bit offsets, a table of files, code/root.rpx)"""
    if data[:4] != b"WUHB" or len(data) < 0x50:
        raise Refused(f"{BUNDLE} is not a WUHB")
    (_, _, _, _, _, _, file_table, file_table_size, file_data) = struct.unpack_from(">9Q", data, 8)
    at = 0
    while at + 32 <= file_table_size:
        _, _, offset, size, _, name_length = struct.unpack_from(">IIQQII", data, file_table + at)
        name = data[file_table + at + 32:file_table + at + 32 + name_length]
        if name.endswith(b".rpx"):
            return data[file_data + offset:file_data + offset + size]
        at += 32 + ((name_length + 3) & ~3)
    raise Refused(f"{BUNDLE} has no RPX")


def rpx_sections(rpx: bytes):
    """each section's bytes, inflated where elf2rpl compressed it (RPL_ZLIB: its size, then a zlib stream)"""
    if rpx[:4] != b"\x7fELF" or rpx[4] != 1 or rpx[5] != 2:
        raise Refused("the RPX is not a big-endian 32-bit ELF")
    section_offset, = struct.unpack_from(">I", rpx, 0x20)
    entry_size, count = struct.unpack_from(">HH", rpx, 0x2E)
    for index in range(count):
        _, kind, flags, _, offset, size = struct.unpack_from(">6I", rpx, section_offset + index * entry_size)
        if kind in (0, 8) or not size:
            continue
        body = rpx[offset:offset + size]
        if flags & 0x08000000:
            body = zlib.decompress(body[4:])
        yield body


def bundle_marker(path: Path) -> dict:
    """the build's record of itself in the bundle (wiiu_update.c's build_marker)"""
    for section in rpx_sections(bundle_rpx(path.read_bytes())):
        match = MARKER.search(section)
        if match:
            build, maps, repository, key = (part.decode() for part in match.groups())
            return {"build": int(build), "maps": maps, "repository": repository, "key": key}
    raise Refused(f"{path.name} has no record of its updater: is it a build of this port?")


# ---------- the key

def openssl() -> str:
    found = shutil.which("openssl")
    if not found:
        raise Refused("openssl is not installed")
    return found


def public_key_of(key: Path) -> str:
    """the raw Ed25519 public key (64 hexadecimal digits) of a private key: what a build is given as HALO_UPDATE_PUBLIC_KEY"""
    result = subprocess.run([openssl(), "pkey", "-in", str(key), "-pubout", "-outform", "DER"], capture_output=True)
    # (an Ed25519 SubjectPublicKeyInfo is 44 bytes: 12 of algorithm, then the key)
    if result.returncode or len(result.stdout) != 44 or result.stdout[:12] != bytes.fromhex("302a300506032b6570032100"):
        raise Refused(f"{key} is not an Ed25519 private key openssl can read")
    return result.stdout[12:].hex()


def sign(key: Path, manifest: Path, signature: Path):
    result = subprocess.run([openssl(), "pkeyutl", "-sign", "-rawin", "-inkey", str(key), "-in", str(manifest), "-out", str(signature)],
                            capture_output=True, text=True)
    if result.returncode:
        raise Refused(f"openssl could not sign: {result.stderr.strip()}")


def verify(key: Path, manifest: Path, signature: Path):
    with tempfile.TemporaryDirectory(prefix="wiiu-release-") as folder:
        public = Path(folder) / "public.pem"
        made = subprocess.run([openssl(), "pkey", "-in", str(key), "-pubout", "-out", str(public)], capture_output=True, text=True)
        checked = subprocess.run([openssl(), "pkeyutl", "-verify", "-rawin", "-pubin", "-inkey", str(public), "-in", str(manifest), "-sigfile",
                                  str(signature)], capture_output=True, text=True)
    if made.returncode or checked.returncode or "Signature Verified Successfully" not in checked.stdout:
        raise Refused(f"the signature does not verify: {checked.stdout.strip()} {checked.stderr.strip()}")


# ---------- the release

def make_release(folder: Path, build: int, key: Path, expected_key: str = "", repository: str = "", names=None, maps: str = "",
                 check_bundle: bool = True) -> str:
    names = list(names or FILES)
    maps = maps or maps_format()
    if not 1 <= build <= BUILD_MOST:
        raise Refused(f"the build number {build} is not 1 to {BUILD_MOST}")
    if BUNDLE not in names:
        raise Refused(f"a release has {BUNDLE}")
    for name in names:
        if not name_allowed(name):
            raise Refused(f"a release may not own a file named {name!r}")
        path = folder / name
        if not path.is_file():
            raise Refused(f"{path} is not there")
        if not 0 < path.stat().st_size <= FILE_MOST:
            raise Refused(f"{path} is empty or larger than {FILE_MOST >> 20} MB")
    public = public_key_of(key)
    if expected_key and expected_key != public:
        raise Refused(f"{key} is not the key the builds trust ({expected_key}): its public key is {public}")
    if check_bundle:
        marker = bundle_marker(folder / BUNDLE)
        if marker["build"] != build:
            raise Refused(f"{BUNDLE} is build {marker['build']}, not {build} (HALO_BUILD_NUMBER when it was configured)")
        if marker["maps"] != maps:
            raise Refused(f"{BUNDLE} reads maps {marker['maps']}, not {maps}")
        if marker["key"] != public:
            raise Refused(f"{BUNDLE} trusts the key {marker['key'] or '(none)'}, not this one ({public}): HALO_UPDATE_PUBLIC_KEY")
        if not marker["repository"]:
            raise Refused(f"{BUNDLE} names no releases' repository (HALO_UPDATE_REPOSITORY)")
        if repository and marker["repository"] != repository:
            raise Refused(f"{BUNDLE} looks for releases in {marker['repository']}, not {repository}")
        repository = marker["repository"]
    text = manifest_text(build, maps, folder, names)
    parsed = parse_manifest(text)
    manifest = folder / MANIFEST
    signature = folder / SIGNATURE
    manifest.write_bytes(text)
    sign(key, manifest, signature)
    # (what was written, read back and checked as the console will)
    if signature.stat().st_size != 64:
        raise Refused(f"the signature is {signature.stat().st_size} bytes, not 64")
    verify(key, manifest, signature)
    reread = parse_manifest(manifest.read_bytes())
    if reread != parsed or reread["build"] != build:
        raise Refused("the manifest read back is not the one written")
    for name, (size, digest) in reread["files"].items():
        data = (folder / name).read_bytes()
        if len(data) != size or hashlib.sha256(data).hexdigest() != digest:
            raise Refused(f"{name} changed while the release was made")
    return repository


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("folder", type=Path, nargs="?", help="the folder of the release's files")
    parser.add_argument("--build", type=int, help="the build number (HALO_BUILD_NUMBER of the build in it)")
    parser.add_argument("--key", type=Path, help="the owner's Ed25519 private key (PEM)")
    parser.add_argument("--public-key", default="", help="the key the builds trust (HALO_UPDATE_PUBLIC_KEY), checked against --key")
    parser.add_argument("--repository", default="", help="the releases' repository (HALO_UPDATE_REPOSITORY), checked against the bundle")
    parser.add_argument("--files", nargs="+", default=FILES, help=f"the release's files (default: {' '.join(FILES)})")
    parser.add_argument("--print-public-key", type=Path, metavar="KEY", help="print the public key of a private key and stop")
    args = parser.parse_args()
    try:
        if args.print_public_key:
            print(public_key_of(args.print_public_key))
            return 0
        if not args.folder or not args.build or not args.key:
            parser.error("FOLDER, --build and --key are needed")
        repository = make_release(args.folder, args.build, args.key, args.public_key, args.repository, args.files)
    except Refused as refusal:
        print(f"refused: {refusal}", file=sys.stderr)
        return 1
    print(f"{args.folder / MANIFEST} and {SIGNATURE}: build {args.build}, signed and checked.")
    print(f"Publish a release tagged wiiu-{args.build} in {repository} with these assets:")
    for name in [MANIFEST, SIGNATURE, *args.files]:
        print(f"  {args.folder / name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
