#!/usr/bin/env python3
"""The mod store's catalog: made from folders of mods, signed, and checked (port/linux/src/modstore.c reads it).

    modstore_catalog.py keygen [--out FILE]        a new signing key: its public half is printed (that is what players'
                                                   mods.store_key, or the build's HALO_MODSTORE_PUBLIC_KEY, holds); with
                                                   --out the secret half (the seed, 64 hex digits) is written to FILE,
                                                   readable by you alone, and never printed
    modstore_catalog.py build SOURCE OUT [--name TEXT] [--serial N]
                                                   SOURCE has one folder for each mod (mod.toml, tags/, and a store.toml
                                                   saying what the store needs to know); OUT gets the files to put on a
                                                   web server (catalog.json, mods/<id>/<version>/...); signed if
                                                   MODSTORE_SIGNING_KEY (the seed) is set
    modstore_catalog.py sign FILE                  FILE.sig: the Ed25519 signature of FILE's bytes (128 hex digits), with
                                                   the key MODSTORE_SIGNING_KEY holds (the seed, 64 hex digits)
    modstore_catalog.py check OUT [--key HEX]      everything a player's game checks, on a built folder: the signature (if
                                                   a key is given), every hash and size, every path, every tag file's header

store.toml, in a mod's folder in SOURCE (it is not published):

    licence = "CC-BY-4.0"           # required: the licence the mod's files are distributed under
    redistributable = true          # required, and true: you may give these files to anyone who asks
    origin = "https://..."          # where the files come from (the author's page); never a Steam Workshop address
    author = "..."                  # else mod.toml's authors

The mod's id is its folder's name (lower case letters, digits, _ and -). Its name, version and description are mod.toml's.
This tool refuses a mod with no licence, one not marked redistributable, one with a file the game would not play, and one
whose origin is the Steam Workshop (those items are not ours to give: docs/mod_store.md).

Python 3.11 or later (tomllib). The signature is made with tools/ed25519_ref.py, which is slow (a second) and not constant-time:
fine for a catalog signed on your own machine, not for a server that signs for strangers.
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import sys
import tomllib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ed25519_ref  # noqa: E402

CATALOG = "catalog.json"
MAXIMUM_FILES = 256
MAXIMUM_FILE_BYTES = 64 * 1024 * 1024
MAXIMUM_MOD_BYTES = 128 * 1024 * 1024
MAXIMUM_MANIFEST_BYTES = 128 * 1024
MAXIMUM_CATALOG_BYTES = 512 * 1024
MAXIMUM_CATALOG_MODS = 128
MAXIMUM_PATH = 180
MAXIMUM_COMPONENT = 64
MAXIMUM_DEPTH = 16
MODS_FORMAT = 1

# the groups of tag files the game plays (port/linux/game/mod_*.c): extension, group, the version of the group
TAG_GROUPS = {
    ".sound": (b"snd!", 4),
    ".bitmap": (b"bitm", 7),
    ".unicode_string_list": (b"ustr", 1),
    ".string_list": (b"str#", 1),
    ".model": (b"mode", 4),
    ".gbxmodel": (b"mod2", 5),
}
KIND_WORDS = {".sound": "sounds", ".bitmap": "bitmaps", ".unicode_string_list": "text", ".string_list": "text",
              ".model": "models", ".gbxmodel": "models"}
TOP_FILES = ("mod.toml", "LICENSE", "LICENSE.txt", "LICENSE.md", "README.md", "README.txt")
COMPONENT_CHARACTERS = re.compile(r"[A-Za-z0-9 _\-.(),'+!@&=~]+")
ID = re.compile(r"[a-z0-9][a-z0-9_-]{0,39}")
VERSION = re.compile(r"[A-Za-z0-9._+-]{1,16}")
HEX64 = re.compile(r"[0-9a-f]{64}")


class Refused(Exception):
    pass


# ---------- what the game accepts (mirrors modstore.c's path_ok and tag_header_valid)

def path_ok(path: str) -> bool:
    """whether a file of a mod may be named so (modstore.c path_ok)"""
    if not path or len(path) > MAXIMUM_PATH or path.endswith("/"):
        return False
    parts = path.split("/")
    if len(parts) > MAXIMUM_DEPTH:
        return False
    for part in parts:
        if (not part or len(part) > MAXIMUM_COMPONENT or not COMPONENT_CHARACTERS.fullmatch(part) or part[0] in ". " or
                part[-1] in ". "):
            return False
    if len(parts) == 1:
        return path in TOP_FILES
    return parts[0] == "tags" and len(parts) >= 3 and tag_extension(path) is not None


def tag_extension(path: str):
    lowered = path.lower()
    for extension in TAG_GROUPS:
        if lowered.endswith(extension) and len(lowered) > len(extension):
            return extension
    return None


def tag_header_ok(data: bytes, extension: str) -> bool:
    """whether the first bytes of a tag file are a header the game accepts for the group (mods.c mods_tag_file_header_valid)"""
    if len(data) < 0x40:
        return False
    group, version = TAG_GROUPS[extension]
    return (data[0x24:0x28] == group and data[0x3C:0x40] == b"blam" and struct.unpack_from(">I", data, 0x2C)[0] == 0x40 and
            struct.unpack_from(">h", data, 0x38)[0] == version)


# ---------- signing

def seed_from_environment() -> bytes:
    text = os.environ.get("MODSTORE_SIGNING_KEY", "")
    if not HEX64.fullmatch(text.lower()):
        raise Refused("MODSTORE_SIGNING_KEY is not a seed of 64 hex digits (make one with: modstore_catalog.py keygen --out FILE)")
    return bytes.fromhex(text)


def sign_file(path: Path, seed: bytes) -> None:
    signature = ed25519_ref.sign(seed, path.read_bytes())
    Path(str(path) + ".sig").write_text(signature.hex() + "\n")


# ---------- building

def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_toml(path: Path) -> dict:
    try:
        return tomllib.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, tomllib.TOMLDecodeError) as error:
        raise Refused(f"{path}: {error}")


def mod_files(folder: Path) -> list:
    """(path, bytes) of a mod folder's files that the store carries; anything else in the folder (but store.toml) is refused"""
    files = []
    for path in sorted(folder.rglob("*")):
        relative = path.relative_to(folder).as_posix()
        if path.is_dir():
            continue
        if path.is_symlink():
            raise Refused(f"{folder.name}/{relative} is a link")
        if relative == "store.toml":
            continue
        if not path_ok(relative):
            raise Refused(f"{folder.name}/{relative} is not a file the game plays (only mod.toml, a licence or readme, and "
                          f"tags/**/*{{{','.join(TAG_GROUPS)}}} with letters, digits, spaces and _-.(),'+!@&=~ in the names)")
        data = path.read_bytes()
        if not data or len(data) > MAXIMUM_FILE_BYTES:
            raise Refused(f"{folder.name}/{relative} is empty or larger than {MAXIMUM_FILE_BYTES >> 20} MB")
        extension = tag_extension(relative)
        if extension and not tag_header_ok(data, extension):
            raise Refused(f"{folder.name}/{relative} has not the header of a {extension} tag file (the game would not play it)")
        files.append((relative, data))
    names = [path.lower() for path, _ in files]
    if len(set(names)) != len(names):
        raise Refused(f"{folder.name}: two files differ only in case")
    for name in names:
        if any(other.startswith(name + "/") for other in names):
            raise Refused(f"{folder.name}: {name} is a file and a folder")
    if not files or len(files) > MAXIMUM_FILES or "mod.toml" not in [path for path, _ in files]:
        raise Refused(f"{folder.name}: it needs a mod.toml and at most {MAXIMUM_FILES} files")
    if sum(len(data) for _, data in files) > MAXIMUM_MOD_BYTES:
        raise Refused(f"{folder.name}: more than {MAXIMUM_MOD_BYTES >> 20} MB")
    return files


def build_mod(folder: Path, out: Path) -> dict:
    mod_id = folder.name
    if not ID.fullmatch(mod_id):
        raise Refused(f"{folder}: the folder's name is the mod's id: lower case letters, digits, _ and - (40 at most)")
    store_path = folder / "store.toml"
    if not store_path.is_file():
        raise Refused(f"{folder}: there is no store.toml (the licence, and that you may redistribute the mod)")
    store = read_toml(store_path)
    licence = str(store.get("licence", "")).strip()
    if not licence or store.get("redistributable") is not True:
        raise Refused(f"{mod_id}: store.toml needs a licence and redistributable = true (only mods that may be given to anyone are listed)")
    origin = str(store.get("origin", ""))
    if re.search(r"steamcommunity|steam.?workshop|workshop", origin, re.IGNORECASE):
        raise Refused(f"{mod_id}: the Steam Workshop's items are not redistributable here (docs/mod_store.md)")
    meta = read_toml(folder / "mod.toml")
    if "format" in meta and meta["format"] != MODS_FORMAT:
        raise Refused(f"{mod_id}: mod.toml says format {meta['format']}; this tool and the game know {MODS_FORMAT}")
    if "id" in meta and str(meta["id"]).lower() != mod_id:
        raise Refused(f"{mod_id}: mod.toml's id is {meta['id']!r}; the game refuses an install whose id is not its folder's")
    version = str(meta.get("version", ""))
    if not VERSION.fullmatch(version):
        raise Refused(f"{mod_id}: mod.toml needs a version of letters, digits and ._+- (16 at most)")
    authors = meta.get("authors", "")
    author = str(store.get("author") or (", ".join(authors) if isinstance(authors, list) else authors))
    files = mod_files(folder)
    total = sum(len(data) for _, data in files)
    kinds = []
    for path, _ in files:
        extension = tag_extension(path)
        if extension and KIND_WORDS[extension] not in kinds:
            kinds.append(KIND_WORDS[extension])
    base = out / "mods" / mod_id / version
    if base.exists():
        shutil.rmtree(base)
    for path, data in files:
        target = base / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    manifest = json.dumps({"format": 1, "id": mod_id, "version": version,
                           "files": [{"path": path, "size": len(data), "sha256": sha256(data)} for path, data in files]},
                          indent=1, sort_keys=False).encode() + b"\n"
    if len(manifest) > MAXIMUM_MANIFEST_BYTES:
        raise Refused(f"{mod_id}: the list of files is larger than {MAXIMUM_MANIFEST_BYTES >> 10} KB")
    (base / "files.json").write_bytes(manifest)
    return {
        "id": mod_id,
        "name": str(meta.get("name") or mod_id),
        "author": author,
        "version": version,
        "description": str(meta.get("description", "")),
        "kind": kinds,
        "licence": licence,
        "format": int(meta.get("format", MODS_FORMAT)),
        "size": total,
        "manifest": {"path": f"mods/{mod_id}/{version}/files.json", "size": len(manifest), "sha256": sha256(manifest)},
    }


def build(source: Path, out: Path, name: str, serial) -> dict:
    out.mkdir(parents=True, exist_ok=True)
    previous = 0
    try:
        previous = int(json.loads((out / CATALOG).read_text())["serial"])
    except (OSError, ValueError, KeyError):
        pass
    mods = [build_mod(folder, out) for folder in sorted(source.iterdir()) if folder.is_dir()]
    if not mods or len(mods) > MAXIMUM_CATALOG_MODS:
        raise Refused(f"{source}: it needs between 1 and {MAXIMUM_CATALOG_MODS} mod folders")
    catalog = {"format": 1, "name": name, "serial": serial if serial is not None else previous + 1, "mods": mods}
    data = json.dumps(catalog, indent=1).encode() + b"\n"
    if len(data) > MAXIMUM_CATALOG_BYTES:
        raise Refused("the catalog is larger than 512 KB")
    (out / CATALOG).write_bytes(data)
    for stale in (out / "mods").iterdir():
        if stale.name not in {mod["id"] for mod in mods}:
            shutil.rmtree(stale)
    return catalog


# ---------- checking

def check(out: Path, key_hex) -> list:
    """the problems of a built folder, as a player's game would find them (empty: none)"""
    problems = []
    try:
        data = (out / CATALOG).read_bytes()
    except OSError as error:
        return [str(error)]
    if key_hex:
        try:
            signature = bytes.fromhex((out / (CATALOG + ".sig")).read_text().strip())
        except (OSError, ValueError):
            return ["the catalog has no readable signature"]
        if not ed25519_ref.verify(bytes.fromhex(key_hex), data, signature):
            return ["the signature is not that key's"]
    catalog = json.loads(data)
    if catalog.get("format") != 1 or not isinstance(catalog.get("serial"), int):
        problems.append("the catalog has no format 1 or no serial")
    seen = set()
    for mod in catalog.get("mods", []):
        mod_id = mod.get("id", "")
        if not ID.fullmatch(mod_id) or mod_id in seen:
            problems.append(f"{mod_id!r}: not a valid unique id")
            continue
        seen.add(mod_id)
        if not mod.get("licence"):
            problems.append(f"{mod_id}: no licence")
        manifest_path = out / mod["manifest"]["path"]
        manifest = manifest_path.read_bytes() if manifest_path.is_file() else b""
        if len(manifest) != mod["manifest"]["size"] or sha256(manifest) != mod["manifest"]["sha256"]:
            problems.append(f"{mod_id}: the list of files is not the catalog's")
            continue
        listing = json.loads(manifest)
        total = 0
        for entry in listing["files"]:
            path = entry["path"]
            file = manifest_path.parent / path
            data = file.read_bytes() if file.is_file() else b""
            total += entry["size"]
            if not path_ok(path):
                problems.append(f"{mod_id}/{path}: not a path the game accepts")
            elif len(data) != entry["size"] or sha256(data) != entry["sha256"]:
                problems.append(f"{mod_id}/{path}: not the size and hash the list gives")
            elif tag_extension(path) and not tag_header_ok(data, tag_extension(path)):
                problems.append(f"{mod_id}/{path}: not a tag file the game accepts")
        if total != mod["size"]:
            problems.append(f"{mod_id}: the files add up to {total}, the catalog says {mod['size']}")
    return problems


# ---------- the command line

def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    commands = parser.add_subparsers(dest="command", required=True)
    keygen = commands.add_parser("keygen")
    keygen.add_argument("--out")
    build_parser = commands.add_parser("build")
    build_parser.add_argument("source", type=Path)
    build_parser.add_argument("out", type=Path)
    build_parser.add_argument("--name", default="OpenCE-U mods")
    build_parser.add_argument("--serial", type=int)
    sign_parser = commands.add_parser("sign")
    sign_parser.add_argument("file", type=Path)
    check_parser = commands.add_parser("check")
    check_parser.add_argument("out", type=Path)
    check_parser.add_argument("--key")
    arguments = parser.parse_args(argv)
    try:
        if arguments.command == "keygen":
            seed = os.urandom(32)
            if arguments.out:
                descriptor = os.open(arguments.out, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
                with os.fdopen(descriptor, "w") as handle:
                    handle.write(seed.hex() + "\n")
                print(f"the secret key (seed) is in {arguments.out}: keep it safe, and set MODSTORE_SIGNING_KEY from it to sign")
            else:
                print("no --out: the secret key is not shown; run again with --out FILE", file=sys.stderr)
                return 2
            print("public key (players' mods.store_key):", ed25519_ref.public_key(seed).hex())
        elif arguments.command == "build":
            catalog = build(arguments.source, arguments.out, arguments.name, arguments.serial)
            path = arguments.out / CATALOG
            if os.environ.get("MODSTORE_SIGNING_KEY"):
                sign_file(path, seed_from_environment())
                print(f"{path} and {path}.sig written: {len(catalog['mods'])} mods, serial {catalog['serial']}")
            else:
                print(f"{path} written, NOT signed (set MODSTORE_SIGNING_KEY and run: modstore_catalog.py sign {path})")
        elif arguments.command == "sign":
            sign_file(arguments.file, seed_from_environment())
            print(f"{arguments.file}.sig written")
        elif arguments.command == "check":
            problems = check(arguments.out, arguments.key.lower() if arguments.key else None)
            for problem in problems:
                print("PROBLEM:", problem)
            print("ok" if not problems else f"{len(problems)} problems")
            return 1 if problems else 0
    except Refused as error:
        print(f"refused: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
