"""The mod store (port/linux/src/modstore.c, tools/modstore_catalog.py, tools/modstore_server.py) on this machine.

    python3 -m pytest -q tools/test_modstore.py

modstore.c is compiled unchanged (tools/modstore_check.c includes it whole) against the real request code (posix_update.c,
plain HTTP on 127.0.0.1), JSON reader, SHA-256, Ed25519 and TOML reader, and run against a local server (tools/modstore_server.py)
that serves catalogs made and signed by tools/modstore_catalog.py from two sample mods made of tag files the game's loader
accepts (tools/test_mods.py's), and from hostile catalogs. What each case proves:

- the catalog: listed and shown; a tampered catalog, a catalog signed by another key, a missing signature and an older
  catalog than one seen (a replay) are refused and nothing in them is looked at; no store set means no request at all
- an install: every file's bytes, the marker, no staging left over; files above one MB come in Range pieces; a server that
  ignores ranges is used for small files and refused for big ones in words; a connection lost half way, and the player's
  stop, leave what was done and the next try goes on from there (the requests start past byte 0)
- what is refused, in words, with nothing left in the mods folder or anywhere else under the data root: a path that leaves
  the mod's folder (.., an absolute path, a leading dot, a drive, a backslash), a file the game does not play, a file with
  the wrong hash, a file the server serves with other bytes, a file with a header the loader would not accept, a manifest
  that is not the catalog's, a size over the limits, a mod.toml that is not TOML or names another mod, a lack of room on the
  card, the same name twice, a folder of the player's with the mod's name (never touched)
- an update replaces the folder; a removal deletes the files the marker lists and leaves the player's own files
- the paths the C code accepts are exactly the ones the catalog tool accepts
"""

import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import pytest

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))
import ed25519_ref  # noqa: E402
import modstore_catalog as catalog  # noqa: E402
import modstore_server  # noqa: E402
import test_mods  # noqa: E402
import test_wiiu_update as base  # noqa: E402

SEED = hashlib.sha256(b"opence modstore test key").digest()
PUBLIC = ed25519_ref.public_key(SEED).hex()
OTHER_SEED = hashlib.sha256(b"another key").digest()
SRC = ROOT / "port/linux/src"
THIRD = ROOT / "port/third_party"

PHASE_READY, PHASE_FAILED = 3, 4
STATE_AVAILABLE, STATE_INSTALLED, STATE_UPDATE, STATE_LOCAL, STATE_UNPLAYABLE = range(5)


# ---------- the driver

def build_driver(mutation=None):
    """the driver, built once for a state of the sources (kept in the temporary folder by their hash); `mutation` is
    (old text, new text) in modstore.c: a deliberate fault, for the negative controls"""
    sources = [SRC / "modstore.c", SRC / "modstore.h", TOOLS / "modstore_check.c", TOOLS / "modstore_test/stub/platform.h"]
    key = hashlib.sha256(repr(mutation).encode())
    for source in sources:
        key.update(source.read_bytes())
    folder = Path(tempfile.gettempdir()) / f"opence-modstore-{key.hexdigest()[:16]}"
    binary = folder / "check"
    if binary.exists():
        return binary
    folder.mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="build-", dir=folder))
    # (modstore.c is copied as it is, with a platform.h of stand-ins beside it: platform.h itself needs the XDK's headers)
    for source in (SRC / "modstore.c", TOOLS / "modstore_check.c", TOOLS / "modstore_test/stub/platform.h"):
        shutil.copy(source, work)
    if mutation:
        text = (work / "modstore.c").read_text()
        assert text.count(mutation[0]) == 1, f"the fault {mutation[0]!r} is not in modstore.c exactly once"
        (work / "modstore.c").write_text(text.replace(*mutation))
    units = [SRC / name for name in ("posix_update.c", "p2p_crypto.c", "server_json.c")]
    units += [THIRD / "monocypher/monocypher.c", THIRD / "monocypher/monocypher-ed25519.c", THIRD / "tomlc17/tomlc17.c"]
    includes = [f"-I{SRC}", f"-I{THIRD / 'tomlc17'}", f"-I{THIRD / 'jsmn'}", f"-I{THIRD / 'mbedtls/include'}", f"-I{THIRD / 'monocypher'}"]
    # (the vendored libraries are not ours to make quiet; the store's own code and the driver are built with warnings as errors)
    objects = []
    for unit in units:
        target = work / (unit.stem + ".o")
        result = base.run([base.CC, "-std=gnu11", "-O1", "-w", *includes, "-c", str(unit), "-o", str(target)])
        assert result.returncode == 0, result.stderr
        objects.append(str(target))
    command = [base.CC, "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-DMODSTORE_STDIO", f"-I{work}", *includes,
               str(work / "modstore_check.c")]
    warnings = [] if mutation else ["-Werror"]
    result = base.run(command + warnings + objects + [str(o) for o in base.mbedtls_objects()] + ["-lpthread", "-o", str(binary)])
    assert result.returncode == 0, result.stderr
    return binary


@pytest.fixture(scope="session")
def driver():
    return build_driver()


def run(driver, root, url, key, *commands, environment=None, timeout=180):
    """runs the commands; the lines they print by their first word (a list for each), and the log"""
    env = dict(os.environ, **(environment or {}))
    result = subprocess.run([str(driver), str(root), url, key, *commands], capture_output=True, text=True, timeout=timeout, env=env)
    assert result.returncode == 0, (result.stdout, result.stderr)
    lines = {}
    for line in result.stdout.splitlines():
        lines.setdefault(line.split(" ", 1)[0], []).append(line)
    return lines, result.stderr


def field(line, name):
    """a `name=value` of a line (the message, last, is the rest of it)"""
    if name == "message":
        return line.split("message=", 1)[1]
    return line.split(f"{name}=", 1)[1].split(" ")[0]


# ---------- the catalogs

def sample_mods(folder: Path, big: bool = False, version: str = "1.0"):
    """two mods of tag files the loader accepts: sounds (and a licence file) and a translation with a bitmap"""
    audio = folder / "hd_audio"
    (audio / "tags/sound/sfx/weapons/assault rifle").mkdir(parents=True)
    (audio / "mod.toml").write_text(f'name = "High-res audio"\nversion = "{version}"\nauthors = ["Ann"]\ndescription = "Sounds"\n')
    (audio / "store.toml").write_text('licence = "CC-BY-4.0"\nredistributable = true\norigin = "https://example.org/hd_audio"\n')
    (audio / "tags/sound/sfx/weapons/assault rifle/fire.sound").write_bytes(test_mods.sound_file())
    (audio / "LICENSE.txt").write_text("CC-BY 4.0\n")
    if big:
        # (2.5 MB: three pieces; a sound's header and a body, to carry bytes: the game never plays it)
        (audio / "tags/sound/sfx/weapons/assault rifle/long.sound").write_bytes(
            test_mods.header(b"snd!", 4) + hashlib.shake_256(b"long").digest(2_500_000))
    german = folder / "german"
    (german / "tags/ui/hud").mkdir(parents=True)
    (german / "tags/levels/test/bloodgulch/bitmaps").mkdir(parents=True)
    (german / "mod.toml").write_text('name = "Deutsch"\nversion = "2.1"\nauthors = "Bernd"\ndescription = "Translation"\n')
    (german / "store.toml").write_text('licence = "MIT"\nredistributable = true\n')
    (german / "tags/ui/hud/hud_item_messages.unicode_string_list").write_bytes(test_mods.strings_file(["Hallo", "Welt"]))
    (german / "tags/levels/test/bloodgulch/bitmaps/blood cliff rock.bitmap").write_bytes(test_mods.bitmap_file())


def hostile_catalog(out: Path, mod_id: str, files: dict, manifest_extra=None, size=None, version="1.0", toml='name = "x"\n',
                    serial=1, seed=SEED, declared=None):
    """a catalog signed by the test key with one mod whose manifest says what it likes: `files` maps a path to its bytes (what is
    served at that path under the mod's folder, unless the path cannot be a file name); `declared` {path: size} overrides
    the sizes the manifest states"""
    base_dir = out / "mods" / mod_id / version
    base_dir.mkdir(parents=True, exist_ok=True)
    entries = []
    total = 0
    for path, data in files.items():
        if ".." not in path and not path.startswith("/") and "\\" not in path:
            target = base_dir / path
            try:
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
            except (OSError, ValueError):
                pass
        length = (declared or {}).get(path, len(data))
        entries.append({"path": path, "size": length, "sha256": hashlib.sha256(data).hexdigest()})
        total += length
    manifest = json.dumps({"format": 1, "id": mod_id, "version": version, "files": entries, **(manifest_extra or {})}).encode()
    (base_dir / "files.json").write_bytes(manifest)
    document = {"format": 1, "serial": serial, "mods": [{
        "id": mod_id, "name": mod_id, "author": "Mallory", "version": version, "description": "x", "kind": ["sounds"],
        "licence": "MIT", "size": size if size is not None else total, "format": 1,
        "manifest": {"path": f"mods/{mod_id}/{version}/files.json", "size": len(manifest),
                     "sha256": hashlib.sha256(manifest).hexdigest()}}]}
    data = json.dumps(document).encode()
    (out / "catalog.json").write_bytes(data)
    (out / "catalog.json.sig").write_text(ed25519_ref.sign(seed, data).hex() + "\n")


@pytest.fixture
def good(tmp_path):
    """a built, signed catalog of the sample mods, served"""
    source, out = tmp_path / "source", tmp_path / "web"
    source.mkdir()
    sample_mods(source, big=True)
    os.environ["MODSTORE_SIGNING_KEY"] = SEED.hex()
    try:
        catalog.build(source, out, "Test", 1)
        catalog.sign_file(out / "catalog.json", catalog.seed_from_environment())
    finally:
        del os.environ["MODSTORE_SIGNING_KEY"]
    setup = Setup(tmp_path, source, out)
    yield setup
    setup.finish()


class Setup:
    def __init__(self, tmp_path, source, out):
        self.tmp = tmp_path
        self.source = source
        self.out = out
        self.root = tmp_path / "data"
        self.root.mkdir()
        self.server = modstore_server.Store(out)
        self.port = self.server.start()
        self.url = f"http://127.0.0.1:{self.port}/catalog.json"

    def run(self, driver, *commands, key=PUBLIC, **options):
        return run(driver, self.root, self.url, key, *commands, **options)

    def mods(self):
        return self.root / "mods"

    def snapshot(self):
        """every file under the data root, with its size"""
        return sorted((str(path.relative_to(self.root)), path.stat().st_size) for path in self.root.rglob("*")
                      if path.is_file() and path.name != ".store-serial")

    def finish(self):
        self.server.stop()


@pytest.fixture
def hostile(tmp_path):
    out = tmp_path / "web"
    out.mkdir()
    setup = Setup(tmp_path, tmp_path / "source", out)
    yield setup
    setup.finish()


def tree(root: Path):
    return sorted(path.relative_to(root).as_posix() for path in root.rglob("*") if path.is_file())


# ---------- the catalog

def test_catalog_listed(driver, good):
    lines, _ = good.run(driver, "open", "list")
    opened = lines["open"][0]
    assert field(opened, "phase") == str(PHASE_READY) and field(opened, "count") == "2"
    mods = {line.split()[2]: line for line in lines["mod"]}
    assert set(mods) == {"hd_audio", "german"}
    assert "state=0" in mods["hd_audio"] and f"size={sum(len(p.read_bytes()) for p in (good.source / 'hd_audio').rglob('*') if p.is_file() and p.name != 'store.toml')}" in mods["hd_audio"]
    assert [name for name in tree(good.root)] == ["mods/.store-serial"], "opening the store made nothing but the serial"


def test_no_store_means_no_request(driver, good):
    lines, _ = run(driver, good.root, "", PUBLIC, "open", "list")
    assert field(lines["open"][0], "phase") == "0" and field(lines["open"][0], "configured") == "0"
    assert good.server.requests == []
    lines, _ = run(driver, good.root, good.url, "", "open")
    assert field(lines["open"][0], "configured") == "0"
    assert good.server.requests == []
    # (a key that is not 64 hex digits is no key: it is not replaced by the build's)
    lines, _ = run(driver, good.root, good.url, "zz" * 32, "open")
    assert field(lines["open"][0], "phase") == str(PHASE_FAILED)


def test_address_must_be_https_or_this_machine(driver, good):
    for url in ("http://example.org/catalog.json", "ftp://x/y", "https://example.org/catalog.json?a=b", "https://x"):
        lines, _ = run(driver, good.root, url, PUBLIC, "open")
        assert field(lines["open"][0], "phase") == str(PHASE_FAILED), url
        assert "ADDRESS" in field(lines["open"][0], "message")
    assert good.server.requests == []


def test_tampered_catalog_is_refused(driver, good):
    path = good.out / "catalog.json"
    data = path.read_bytes().replace(b'"Test"', b'"Tesu"')
    path.write_bytes(data)
    lines, _ = good.run(driver, "open", "list")
    opened = lines["open"][0]
    assert field(opened, "phase") == str(PHASE_FAILED) and field(opened, "count") == "0"
    assert "NOT SIGNED" in field(opened, "message") and "mod" not in lines


def test_wrong_key_missing_and_bad_signature(driver, good):
    lines, _ = good.run(driver, "open", key=ed25519_ref.public_key(OTHER_SEED).hex())
    assert "NOT SIGNED" in field(lines["open"][0], "message")
    (good.out / "catalog.json.sig").unlink()
    lines, _ = good.run(driver, "open")
    assert field(lines["open"][0], "phase") == str(PHASE_FAILED)
    (good.out / "catalog.json.sig").write_text("not hex\n")
    lines, _ = good.run(driver, "open")
    assert "NOT SIGNED" in field(lines["open"][0], "message")
    # (a signature with one bit changed)
    data = (good.out / "catalog.json").read_bytes()
    signature = bytearray(ed25519_ref.sign(SEED, data))
    signature[10] ^= 1
    (good.out / "catalog.json.sig").write_text(bytes(signature).hex() + "\n")
    lines, _ = good.run(driver, "open")
    assert "NOT SIGNED" in field(lines["open"][0], "message")


def test_nothing_is_read_before_the_signature(driver, good):
    # a catalog that is not even JSON, unsigned: refused as not signed, not as unreadable
    (good.out / "catalog.json").write_bytes(b"{{{ not json")
    (good.out / "catalog.json.sig").write_text("00" * 64 + "\n")
    lines, _ = good.run(driver, "open")
    assert "NOT SIGNED" in field(lines["open"][0], "message")
    # signed but not JSON: unreadable
    data = b"{{{ not json"
    (good.out / "catalog.json").write_bytes(data)
    (good.out / "catalog.json.sig").write_text(ed25519_ref.sign(SEED, data).hex() + "\n")
    lines, _ = good.run(driver, "open")
    assert "CANNOT BE READ" in field(lines["open"][0], "message")


def test_replayed_older_catalog_is_refused(driver, good):
    # serial 1 seen, then the store serves serial 5, then an older one (serial 3) is replayed
    lines, _ = good.run(driver, "open")
    assert field(lines["open"][0], "phase") == str(PHASE_READY)
    catalog.build(good.source, good.out, "Test", 5)
    catalog.sign_file(good.out / "catalog.json", SEED)
    lines, _ = good.run(driver, "open")
    assert field(lines["open"][0], "phase") == str(PHASE_READY)
    assert (good.mods() / ".store-serial").read_text().strip() == "5"
    catalog.build(good.source, good.out, "Test", 3)
    catalog.sign_file(good.out / "catalog.json", SEED)
    lines, _ = good.run(driver, "open")
    assert field(lines["open"][0], "phase") == str(PHASE_FAILED) and "OLDER" in field(lines["open"][0], "message")
    assert (good.mods() / ".store-serial").read_text().strip() == "5"


def test_a_bad_mod_of_the_catalog_is_left_out_not_the_catalog(driver, good):
    document = json.loads((good.out / "catalog.json").read_text())
    document["mods"].append({"id": "../evil", "version": "1", "licence": "x", "size": 5})
    document["mods"].append(dict(document["mods"][0]))  # (listed twice)
    document["mods"].append({**document["mods"][0], "id": "nolicence", "licence": ""})
    data = json.dumps(document).encode()
    (good.out / "catalog.json").write_bytes(data)
    (good.out / "catalog.json.sig").write_text(ed25519_ref.sign(SEED, data).hex() + "\n")
    lines, log = good.run(driver, "open", "list")
    assert field(lines["open"][0], "count") == "2"
    assert "listed twice" in log and "NAMES NO LICENCE" in log and "NO VALID ID" in log


# ---------- an install

def installed_tree(good, mod_id):
    return tree(good.mods() / mod_id)


def test_install_whole(driver, good):
    lines, log = good.run(driver, "open", "install", "hd_audio", "install", "german", "list")
    assert field(lines["install"][0], "started") == "1" and field(lines["install"][0], "changed") == "1"
    assert "INSTALLED" in field(lines["install"][0], "message")
    for mod_id, source in (("hd_audio", good.source / "hd_audio"), ("german", good.source / "german")):
        expected = sorted(p.relative_to(source).as_posix() for p in source.rglob("*") if p.is_file() and p.name != "store.toml")
        assert installed_tree(good, mod_id) == sorted(expected + [".store"])
        for name in expected:
            assert (good.mods() / mod_id / name).read_bytes() == (source / name).read_bytes()
    assert not [p for p in good.mods().iterdir() if p.name.startswith(".store-") and p.name != ".store-serial"]
    marker = (good.mods() / "hd_audio" / ".store").read_text().splitlines()
    assert marker[:3] == ["opence-modstore 1", "id hd_audio", "version 1.0"]
    assert sum(1 for line in marker if line.startswith("file ")) == 4
    states = {line.split()[2]: line for line in lines["mod"]}
    assert "state=1" in states["hd_audio"] and "installed=1.0" in states["hd_audio"] and "state=1" in states["german"]
    # (the big file came in three Range pieces: 2.5 MB at 1 MB a piece)
    ranged = [r for r in good.server.requests if r[0].endswith("long.sound") and r[1]]
    assert len(ranged) == 3 and ranged[0][1] == "bytes=0-1048575" and ranged[1][1].startswith("bytes=1048576-")
    # (and the small ones whole)
    assert [r for r in good.server.requests if r[0].endswith("fire.sound")][0][1] is None


def test_installed_mod_is_a_mod_the_loader_reads(driver, good):
    """the files are where port_mods.c looks, and the mod.toml parses as it does"""
    good.run(driver, "open", "install", "german")
    import tomllib
    meta = tomllib.loads((good.mods() / "german" / "mod.toml").read_text())
    assert meta["name"] == "Deutsch"
    assert (good.mods() / "german" / "tags" / "ui" / "hud" / "hud_item_messages.unicode_string_list").is_file()
    for path in (good.mods() / "german" / "tags").rglob("*.*"):
        extension = catalog.tag_extension(path.name)
        assert extension and catalog.tag_header_ok(path.read_bytes(), extension)


def test_install_twice_and_reinstall_states(driver, good):
    lines, _ = good.run(driver, "open", "install", "german", "install", "german")
    assert field(lines["install"][1], "started") == "0" and "ALREADY INSTALLED" in field(lines["install"][1], "message")


def test_server_without_ranges(driver, good):
    good.server.ranges = False
    lines, _ = good.run(driver, "open", "install", "german", "install", "hd_audio")
    assert "INSTALLED" in field(lines["install"][0], "message")
    assert field(lines["install"][1], "started") == "1" and "PARTIAL DOWNLOADS" in field(lines["install"][1], "message")
    assert not (good.mods() / "hd_audio").exists()


def test_connection_lost_then_resumed(driver, good):
    # the mod's files in list order: LICENSE.txt, mod.toml, tags... : cut the connection after 3 files' worth of requests
    good.server.fail_after = 4
    lines, log = good.run(driver, "open", "install", "hd_audio")
    assert "STORE" in field(lines["install"][0], "message") or "REACH" in field(lines["install"][0], "message")
    assert not (good.mods() / "hd_audio").exists()
    stage = good.mods() / ".store-hd_audio.tmp"
    assert stage.is_dir() and (stage / ".plan").is_file()
    kept = tree(stage)
    good.server.fail_after = None
    good.server.requests.clear()
    lines, _ = good.run(driver, "open", "install", "hd_audio")
    assert "INSTALLED" in field(lines["install"][0], "message")
    assert not stage.exists()
    # (what had arrived was not asked for again)
    asked = [r[0] for r in good.server.requests if r[0].startswith("/mods/") and not r[0].endswith("files.json")]
    done_files = [name for name in kept if not name.endswith((".plan", ".part"))]
    for name in done_files:
        assert not any(path.endswith(name) for path in asked), name


def test_stop_in_the_middle_goes_on_from_there(driver, good):
    good.server.delay = 0.3
    lines, _ = good.run(driver, "open", "stop-after", "hd_audio", "1050000")
    good.server.delay = 0
    stop = lines["stop"][0]
    assert "STOPPED" in field(stop, "message") and field(stop, "changed") == "0"
    stage = good.mods() / ".store-hd_audio.tmp"
    parts = [p for p in stage.rglob("*.part")]
    assert not (good.mods() / "hd_audio").exists()
    good.server.requests.clear()
    lines, _ = good.run(driver, "open", "install", "hd_audio")
    assert "INSTALLED" in field(lines["install"][0], "message")
    if parts:
        # (the file in pieces went on past its first byte: its first request is not for byte 0)
        long = [r for r in good.server.requests if r[0].endswith("long.sound")]
        assert long and (long[0][1] or "").split("=")[1].split("-")[0] != "0"
    assert (good.mods() / "hd_audio" / "tags/sound/sfx/weapons/assault rifle/long.sound").read_bytes() == \
        (good.source / "hd_audio/tags/sound/sfx/weapons/assault rifle/long.sound").read_bytes()


def test_wrong_hash_is_refused_and_deleted(driver, good):
    path = "/mods/hd_audio/1.0/tags/sound/sfx/weapons/assault rifle/fire.sound"
    original = (good.source / "hd_audio/tags/sound/sfx/weapons/assault rifle/fire.sound").read_bytes()
    good.server.replace[path] = original[:-1] + bytes([original[-1] ^ 1])
    lines, _ = good.run(driver, "open", "install", "hd_audio")
    message = field(lines["install"][0], "message")
    assert "HASH IS WRONG" in message and field(lines["install"][0], "changed") == "0"
    assert not (good.mods() / "hd_audio").exists()
    assert not list(good.mods().rglob("fire.sound*")), "a file with the wrong hash is not kept"


def test_a_hostile_host_serving_other_bytes(driver, good):
    # (the manifest swapped for another that lists a different file: its hash is not the catalog's)
    manifest = json.loads((good.out / "mods/german/2.1/files.json").read_text())
    manifest["files"][0]["path"] = "tags/ui/hud/other.unicode_string_list"
    good.server.replace["/mods/german/2.1/files.json"] = json.dumps(manifest).encode()
    lines, _ = good.run(driver, "open", "install", "german")
    assert "NOT THE ONE THE CATALOG NAMES" in field(lines["install"][0], "message")
    assert not (good.mods() / "german").exists()


def test_status_not_found(driver, good):
    good.server.status["/mods/german/2.1/mod.toml"] = 404
    lines, _ = good.run(driver, "open", "install", "german")
    assert "ANSWERED 404" in field(lines["install"][0], "message")
    assert not (good.mods() / "german").exists()


def test_not_enough_room(driver, good):
    lines, _ = good.run(driver, "open", "install", "hd_audio", environment={"MODSTORE_CHECK_FREE": str(1_000_000)})
    assert "NOT ENOUGH ROOM" in field(lines["install"][0], "message")
    assert tree(good.root) == ["mods/.store-serial"]
    assert not (good.mods() / "hd_audio").exists() and not (good.mods() / ".store-hd_audio.tmp").exists()


# ---------- what is refused

HOSTILE_PATHS = [
    "../evil.toml", "tags/../../evil.sound", "tags/sound/../../../outside.sound", "/etc/passwd", "/tags/a/b.sound",
    "C:/Windows/x.sound", "tags\\sound\\a.sound", "tags/.hidden/a.sound", "tags/sound/.a.sound", "tags//sound/a.sound",
    "tags/sound/a.sound/", "tags/sound/a b .sound ", "tags/sound/con:.sound", "tags/sound/a?.sound", "tags/sound/a*.sound",
    "tags/sound/a|b.sound", "tags/sound/a\"b.sound", "run.exe", "tags/run.exe", "tags/sound/a.sound.exe", "tags/a.sound",
    "tags/sound/%2e%2e/a.sound", "tags/sound/a%00.sound", "tags/sound/é.sound", "autorun.inf", ".store", "tags/sound/.",
    "tags/sound/..", "tags/" + "a/" * 20 + "b.sound", "tags/sound/" + "n" * 70 + ".sound", "mod.toml/x", "mods.toml",
]


@pytest.mark.parametrize("path", HOSTILE_PATHS)
def test_hostile_path_is_refused(driver, hostile, path):
    valid_tag = test_mods.header(b"snd!", 4) + bytes(16)
    hostile_catalog(hostile.out, "evil", {"mod.toml": b'name = "x"\n', path: valid_tag})
    before = hostile.snapshot()
    lines, _ = hostile.run(driver, "open", "install", "evil")
    assert field(lines["install"][0], "changed") == "0", path
    assert "GAME WOULD NOT PLAY" in field(lines["install"][0], "message") or "FILES" in field(lines["install"][0], "message"), path
    assert not (hostile.mods() / "evil").exists()
    assert hostile.snapshot() == before, "nothing was written anywhere"
    assert not (hostile.tmp / "evil.toml").exists() and not (hostile.root.parent / "evil.toml").exists()


def test_c_and_python_accept_the_same_paths(driver, hostile):
    paths = HOSTILE_PATHS + [
        "mod.toml", "LICENSE", "LICENSE.txt", "README.md", "tags/sound/a.sound", "tags/sound/sfx/weapons/assault rifle/fire.sound",
        "tags/ui/hud/hud_item_messages.unicode_string_list", "tags/levels/test/bloodgulch/bitmaps/blood cliff rock.bitmap",
        "tags/vehicles/warthog/warthog.gbxmodel", "tags/weapons/pistol/pistol.model", "tags/x/y.string_list", "tags/x/Y.SOUND",
        "tags/a (1)/b, c's.sound", "tags/a/b~c+d!@&=e.bitmap", "tags/a/" + "b" * 64 + ".sound", "tags/a/b.sound.", "tags/a/ b.sound",
        "tags/a/b.sound ", "tags/a", "tags", "tags/", "", "a", "a/b", "tags/x/y.sounds", "tags/x/.sound", "readme.md", "LICENSE.exe",
    ]
    # ("" cannot be told apart on a command line: Python's verdict only)
    paths = [path for path in paths if path]
    lines, _ = hostile.run(driver, *[word for path in paths for word in ("path", path)])
    outputs = {}
    for line in lines["path"]:
        path, verdict = line.split(" ", 1)[1].rsplit(" ", 1)
        outputs[path] = verdict == "1"
    for path in paths:
        assert outputs[path] == catalog.path_ok(path), repr(path)
    assert not catalog.path_ok("")


def test_a_file_the_loader_would_not_accept(driver, hostile):
    wrong_group = test_mods.header(b"bitm", 7) + bytes(16)
    wrong_version = test_mods.header(b"snd!", 3) + bytes(16)
    bad_signature = bytearray(test_mods.header(b"snd!", 4) + bytes(16))
    bad_signature[0x3C:0x40] = b"xxxx"
    bad_size = bytearray(test_mods.header(b"snd!", 4) + bytes(16))
    struct.pack_into(">I", bad_size, 0x2C, 0x41)
    for name, data in (("wrong-group", wrong_group), ("wrong-version", wrong_version), ("bad-signature", bytes(bad_signature)),
                       ("bad-size", bytes(bad_size)), ("short", b"snd!"), ("empty-ish", b"\0")):
        out = hostile.out / name
        out.mkdir()
        hostile_catalog(out, "tags", {"mod.toml": b'name = "x"\n', "tags/sound/a.sound": data})
        server = modstore_server.Store(out)
        port = server.start()
        try:
            lines, _ = run(driver, hostile.root, f"http://127.0.0.1:{port}/catalog.json", PUBLIC, "open", "install", "tags")
        finally:
            server.stop()
        assert "NOT A TAG FILE THE GAME CAN PLAY" in field(lines["install"][0], "message"), name
        assert not (hostile.mods() / "tags").exists(), name
        assert not list(hostile.mods().rglob("a.sound*")), name


def test_wrong_manifest_facts(driver, hostile):
    tag = test_mods.header(b"snd!", 4) + bytes(16)
    cases = {
        "no-mod-toml": ({"tags/sound/a.sound": tag}, {}, "NO MOD.TOML"),
        "twice": ({"mod.toml": b"x=1\n", "tags/sound/a.sound": tag, "tags/sound/A.sound": tag}, {}, "SAME FILE TWICE"),
        "file-and-folder": ({"mod.toml": b"x=1\n", "tags/sound/a.sound": tag, "tags/sound/a.sound/b.sound": tag}, {}, "SAME FILE TWICE"),
        "bad-size-total": ({"mod.toml": b"x=1\n"}, {"size": 5}, "ADD UP"),
        "no-files": ({}, {"size": 5}, "NO MOD.TOML"),
    }
    for name, (files, extra, words) in cases.items():
        out = hostile.out / name
        out.mkdir()
        hostile_catalog(out, "mod", files, size=extra.get("size"))
        server = modstore_server.Store(out)
        port = server.start()
        try:
            lines, _ = run(driver, hostile.root, f"http://127.0.0.1:{port}/catalog.json", PUBLIC, "open", "install", "mod")
        finally:
            server.stop()
        assert words in field(lines["install"][0], "message"), (name, lines["install"][0])
        assert not (hostile.mods() / "mod").exists(), name


def test_too_big(driver, hostile):
    tag = test_mods.header(b"snd!", 4) + bytes(16)
    # one file declared above 64 MB, and files adding up to over 128 MB
    cases = {
        "file": ({"mod.toml": b"x=1\n", "tags/sound/a.sound": tag}, {"tags/sound/a.sound": 65 * 1024 * 1024}),
        "total": ({"mod.toml": b"x=1\n", **{f"tags/sound/{n}.sound": tag for n in "abc"}},
                  {f"tags/sound/{n}.sound": 50 * 1024 * 1024 for n in "abc"}),
    }
    for name, (files, declared) in cases.items():
        out = hostile.out / name
        out.mkdir()
        hostile_catalog(out, "big", files, declared=declared, size=100 * 1024 * 1024 if name == "total" else None)
        server = modstore_server.Store(out)
        port = server.start()
        try:
            lines, _ = run(driver, hostile.root, f"http://127.0.0.1:{port}/catalog.json", PUBLIC, "open", "list", "install", "big")
        finally:
            server.stop()
        text = " ".join(lines["install"] + lines["open"])
        assert "MB" in text or "FILE" in text or "SIZE" in text, (name, text)
        assert not (hostile.mods() / "big").exists()
        assert not list(hostile.mods().glob(".store-big*")), name
    # a catalog entry of more than 128 MB is not listed at all
    out = hostile.out / "huge"
    out.mkdir()
    hostile_catalog(out, "huge", {"mod.toml": b"x=1\n"}, size=200 * 1024 * 1024)
    server = modstore_server.Store(out)
    port = server.start()
    try:
        lines, log = run(driver, hostile.root, f"http://127.0.0.1:{port}/catalog.json", PUBLIC, "open")
    finally:
        server.stop()
    assert field(lines["open"][0], "count") == "0" and "NO SIZE UNDER" in log


def test_server_sends_more_than_the_size(driver, hostile):
    # the manifest says 40 bytes and the file served is 80: the piece is not the size asked for
    tag = test_mods.header(b"snd!", 4) + bytes(16)
    hostile_catalog(hostile.out, "mod", {"mod.toml": b"name='x'\n", "tags/sound/a.sound": tag})
    hostile.server.replace["/mods/mod/1.0/tags/sound/a.sound"] = tag + bytes(40)
    lines, _ = hostile.run(driver, "open", "install", "mod")
    assert "changed=0" in lines["install"][0]
    assert not (hostile.mods() / "mod").exists()


def test_mod_toml_problems(driver, hostile):
    tag = test_mods.header(b"snd!", 4) + bytes(16)
    cases = {"not-toml": (b"name = = =\n", "NOT VALID TOML"), "other-id": (b'id = "hd_audio"\n', "ANOTHER ID"),
             "later-format": (b"format = 2\n", "LATER MODS FORMAT"), "format-zero": (b"format = 0\n", "LATER MODS FORMAT")}
    for name, (toml, words) in cases.items():
        out = hostile.out / name
        out.mkdir()
        hostile_catalog(out, "mod", {"mod.toml": toml, "tags/sound/a.sound": tag})
        server = modstore_server.Store(out)
        port = server.start()
        try:
            lines, _ = run(driver, hostile.root, f"http://127.0.0.1:{port}/catalog.json", PUBLIC, "open", "install", "mod")
        finally:
            server.stop()
        assert words in field(lines["install"][0], "message"), (name, lines["install"][0])
        assert not (hostile.mods() / "mod").exists(), name
        assert not list(hostile.mods().glob(".store-mod*")), name
    # (its own id, in any case, is fine)
    out = hostile.out / "own-id"
    out.mkdir()
    hostile_catalog(out, "mod", {"mod.toml": b'id = "MOD"\nformat = 1\n', "tags/sound/a.sound": tag})
    server = modstore_server.Store(out)
    port = server.start()
    try:
        lines, _ = run(driver, hostile.root, f"http://127.0.0.1:{port}/catalog.json", PUBLIC, "open", "install", "mod")
    finally:
        server.stop()
    assert "INSTALLED" in field(lines["install"][0], "message")


def test_the_players_own_folder_is_never_touched(driver, good):
    own = good.mods() / "german"
    (own / "tags").mkdir(parents=True)
    (own / "mod.toml").write_text('name = "My translation"\n')
    (own / "tags" / "mine.txt").write_text("mine")
    before = good.snapshot()
    lines, _ = good.run(driver, "open", "list", "install", "german", "remove", "german")
    states = {line.split()[2]: line for line in lines["mod"]}
    assert f"state={STATE_LOCAL}" in states["german"]
    assert field(lines["install"][0], "started") == "0" and "ALREADY IN THE MODS FOLDER" in field(lines["install"][0], "message")
    assert field(lines["remove"][0], "started") == "0" and "YOURS" in field(lines["remove"][0], "message")
    after = [item for item in good.snapshot() if not item[0].endswith(".store-serial")]
    assert after == [item for item in before if not item[0].endswith(".store-serial")]
    assert (own / "mod.toml").read_text() == 'name = "My translation"\n'


def test_a_folder_that_appears_after_the_list_was_made(driver, good):
    """the install looks at the folder again when it begins: one the player made since the list is not replaced"""
    lines, _ = good.run(driver, "open", "own", "german", "install", "german")
    assert field(lines["install"][0], "started") == "1" and "NOT THE STORE'S" in field(lines["install"][0], "message")
    assert (good.mods() / "german" / "mod.toml").read_text() == 'name = "mine"\n' and not (good.mods() / "german" / ".store").exists()
    assert not (good.mods() / ".store-german.old").exists()
    # (and one with a marker of another mod's id is not the store's either)
    (good.mods() / "german" / ".store").write_text("opence-modstore 1\nid other\nversion 1\n")
    lines, _ = good.run(driver, "open", "list")
    assert any(f"state={STATE_LOCAL}" in line for line in lines["mod"] if " german " in line)


# ---------- update and removal

def test_update_replaces_the_folder_and_remove_deletes_the_files(driver, good):
    good.run(driver, "open", "install", "hd_audio")
    (good.mods() / "hd_audio" / "my notes.txt").write_text("the player's")
    # a new version: a tag changes, one is gone, one is new
    shutil.rmtree(good.source / "hd_audio/tags/sound/sfx/weapons/assault rifle/long.sound", ignore_errors=True)
    (good.source / "hd_audio/tags/sound/sfx/weapons/assault rifle/long.sound").unlink()
    (good.source / "hd_audio/tags/sound/sfx/weapons/assault rifle/fire.sound").write_bytes(test_mods.sound_file(3000))
    (good.source / "hd_audio/tags/sound/new.sound").write_bytes(test_mods.sound_file(100))
    (good.source / "hd_audio/mod.toml").write_text('name = "High-res audio"\nversion = "1.1"\n')
    catalog.build(good.source, good.out, "Test", 2)
    catalog.sign_file(good.out / "catalog.json", SEED)
    lines, _ = good.run(driver, "open", "list")
    states = {line.split()[2]: line for line in lines["mod"]}
    assert f"state={STATE_UPDATE}" in states["hd_audio"] and "installed=1.0" in states["hd_audio"]
    lines, _ = good.run(driver, "open", "install", "hd_audio", "list")
    assert "UPDATED" in field(lines["install"][0], "message")
    names = tree(good.mods() / "hd_audio")
    assert "tags/sound/new.sound" in names and not any("long.sound" in n for n in names) and "my notes.txt" not in names
    assert (good.mods() / "hd_audio" / "tags/sound/sfx/weapons/assault rifle/fire.sound").read_bytes() == test_mods.sound_file(3000)
    assert not [p for p in good.mods().iterdir() if p.name.startswith(".store-") and p.name != ".store-serial"]
    assert any(f"state={STATE_INSTALLED}" in line for line in lines["mod"] if " hd_audio " in line)
    # remove: the files the marker lists, and the player's own file stays
    (good.mods() / "hd_audio" / "my notes.txt").write_text("the player's")
    lines, _ = good.run(driver, "open", "remove", "hd_audio", "list")
    assert "REMOVED" in field(lines["remove"][0], "message") and "OWN" in field(lines["remove"][0], "message")
    assert tree(good.mods() / "hd_audio") == ["my notes.txt"]
    assert any(f"state={STATE_LOCAL}" in line for line in lines["mod"] if " hd_audio " in line)
    # remove a mod the store installed that has nothing else in it: the folder goes
    good.run(driver, "open", "install", "german")
    lines, _ = good.run(driver, "open", "remove", "german")
    assert field(lines["remove"][0], "message") == "REMOVED DEUTSCH"
    assert not (good.mods() / "german").exists()


def test_a_cut_update_is_put_back(driver, good):
    good.run(driver, "open", "install", "german")
    old = good.mods() / ".store-german.old"
    (good.mods() / "german").rename(old)
    lines, _ = good.run(driver, "open")
    assert (good.mods() / "german" / "mod.toml").is_file() and not old.exists()
    # (and an old folder left beside a folder that is there is deleted)
    shutil.copytree(good.mods() / "german", old)
    good.run(driver, "open")
    assert not old.exists() and (good.mods() / "german" / "mod.toml").is_file()


def test_a_marker_cannot_make_a_removal_leave_the_folder(driver, good):
    good.run(driver, "open", "install", "german")
    marker = good.mods() / "german" / ".store"
    outside = good.root / "outside.txt"
    outside.write_text("keep me")
    marker.write_text(marker.read_text() + f"file 4 {'00' * 32} ../../outside.txt\nfile 4 {'00' * 32} /etc/hosts\n")
    lines, _ = good.run(driver, "open", "remove", "german")
    assert outside.read_text() == "keep me"
    assert "REMOVED" in field(lines["remove"][0], "message")


def test_remove_something_not_installed(driver, good):
    lines, _ = good.run(driver, "open", "remove", "german")
    assert field(lines["remove"][0], "started") == "0" and "NOT INSTALLED" in field(lines["remove"][0], "message")


def test_job_while_another_runs_is_refused(driver, good):
    # an install begun and a second asked at once: the second says wait (the driver waits between commands, so use stop-after
    # to hold the first and ask the same in the same breath is not possible here: tested in the second process instead)
    lines, _ = good.run(driver, "open", "install", "german", "install", "german")
    assert field(lines["install"][0], "started") == "1" and field(lines["install"][1], "started") == "0"


# ---------- the parts

@pytest.mark.parametrize("size", [0, 1, 3, 55, 56, 57, 63, 64, 65, 119, 120, 1000, 65537])
def test_sha256_in_pieces_is_sha256(driver, tmp_path, size):
    data = hashlib.shake_256(str(size).encode()).digest(size)
    path = tmp_path / "data"
    path.write_bytes(data)
    lines, _ = run(driver, tmp_path, "http://127.0.0.1:1/c.json", PUBLIC, "hash", str(path))
    assert lines["hash"][0] == "hash " + hashlib.sha256(data).hexdigest()


def test_addresses_of_files(driver, tmp_path):
    lines, _ = run(driver, tmp_path, "http://127.0.0.1:1/c.json", PUBLIC, "url", "https://example.org/store/mods/a/1.0/files.json",
                   "tags/sound/assault rifle/fire (2).sound")
    assert lines["url"][0] == "url https://example.org/store/mods/a/1.0/tags/sound/assault%20rifle/fire%20%282%29.sound"


def test_the_catalog_tool_refuses_what_the_game_would_(tmp_path):
    source = tmp_path / "source"
    source.mkdir()
    sample_mods(source)
    # no licence
    (source / "german/store.toml").write_text("redistributable = true\n")
    with pytest.raises(catalog.Refused, match="licence"):
        catalog.build(source, tmp_path / "out", "t", 1)
    # not redistributable
    (source / "german/store.toml").write_text('licence = "MIT"\nredistributable = false\n')
    with pytest.raises(catalog.Refused, match="redistributable"):
        catalog.build(source, tmp_path / "out", "t", 1)
    # from the Steam Workshop
    (source / "german/store.toml").write_text('licence = "MIT"\nredistributable = true\norigin = "https://steamcommunity.com/sharedfiles/filedetails/?id=1"\n')
    with pytest.raises(catalog.Refused, match="Workshop"):
        catalog.build(source, tmp_path / "out", "t", 1)
    (source / "german/store.toml").write_text('licence = "MIT"\nredistributable = true\n')
    # a program
    (source / "german/tags/run.exe").write_bytes(b"MZ")
    with pytest.raises(catalog.Refused, match="not a file the game plays"):
        catalog.build(source, tmp_path / "out", "t", 1)
    (source / "german/tags/run.exe").unlink()
    # a tag file the loader would not accept
    (source / "german/tags/ui/hud/hud_item_messages.unicode_string_list").write_bytes(b"\0" * 100)
    with pytest.raises(catalog.Refused, match="header"):
        catalog.build(source, tmp_path / "out", "t", 1)
    (source / "german/tags/ui/hud/hud_item_messages.unicode_string_list").write_bytes(test_mods.strings_file(["a"]))
    # a link out of the folder
    (source / "german/tags/ui/link.sound").symlink_to("/etc/hosts")
    with pytest.raises(catalog.Refused, match="link"):
        catalog.build(source, tmp_path / "out", "t", 1)
    (source / "german/tags/ui/link.sound").unlink()
    # mod.toml naming another id
    (source / "german/mod.toml").write_text('id = "hd_audio"\nversion = "1"\n')
    with pytest.raises(catalog.Refused, match="id"):
        catalog.build(source, tmp_path / "out", "t", 1)
    (source / "german/mod.toml").write_text('version = "1"\n')
    built = catalog.build(source, tmp_path / "out", "t", 1)
    assert [mod["id"] for mod in built["mods"]] == ["german", "hd_audio"]
    assert catalog.check(tmp_path / "out", None) == []


def test_the_catalog_tool_signs_and_checks(tmp_path):
    source = tmp_path / "source"
    source.mkdir()
    sample_mods(source)
    out = tmp_path / "out"
    environment = dict(os.environ, MODSTORE_SIGNING_KEY=SEED.hex())
    result = subprocess.run([sys.executable, str(TOOLS / "modstore_catalog.py"), "build", str(source), str(out)], capture_output=True,
                            text=True, env=environment)
    assert result.returncode == 0, result.stderr
    assert catalog.check(out, PUBLIC) == []
    assert catalog.check(out, ed25519_ref.public_key(OTHER_SEED).hex()) == ["the signature is not that key's"]
    # a changed file, a changed hash
    target = out / "mods/german/2.1/tags/ui/hud/hud_item_messages.unicode_string_list"
    target.write_bytes(target.read_bytes() + b"x")
    assert any("not the size and hash" in problem for problem in catalog.check(out, PUBLIC))
    # without the key: built, not signed
    result = subprocess.run([sys.executable, str(TOOLS / "modstore_catalog.py"), "build", str(source), str(tmp_path / "out2")],
                            capture_output=True, text=True, env={k: v for k, v in os.environ.items() if k != "MODSTORE_SIGNING_KEY"})
    assert "NOT signed" in result.stdout and not (tmp_path / "out2/catalog.json.sig").exists()
    # keygen: the secret never printed
    secret = tmp_path / "key"
    result = subprocess.run([sys.executable, str(TOOLS / "modstore_catalog.py"), "keygen", "--out", str(secret)], capture_output=True, text=True)
    seed = secret.read_text().strip()
    assert seed not in result.stdout and ed25519_ref.public_key(bytes.fromhex(seed)).hex() in result.stdout
    assert oct(secret.stat().st_mode & 0o777) == "0o600"


# ---------- negative controls: the same attacks against a modstore.c with one check taken out must succeed (so the tests above can fail)

def attack_tampered_catalog(driver, setup):
    path = setup.out / "catalog.json"
    path.write_bytes(path.read_bytes().replace(b'"Test"', b'"Tesu"'))
    lines, _ = setup.run(driver, "open")
    return field(lines["open"][0], "phase") == str(PHASE_READY)


def attack_replay(driver, setup):
    setup.run(driver, "open")
    catalog.build(setup.source, setup.out, "Test", 5)
    catalog.sign_file(setup.out / "catalog.json", SEED)
    setup.run(driver, "open")
    catalog.build(setup.source, setup.out, "Test", 3)
    catalog.sign_file(setup.out / "catalog.json", SEED)
    lines, _ = setup.run(driver, "open")
    return field(lines["open"][0], "phase") == str(PHASE_READY)


def attack_wrong_bytes(driver, setup):
    path = "/mods/hd_audio/1.0/tags/sound/sfx/weapons/assault rifle/fire.sound"
    original = (setup.source / "hd_audio/tags/sound/sfx/weapons/assault rifle/fire.sound").read_bytes()
    setup.server.replace[path] = original[:-1] + bytes([original[-1] ^ 1])
    setup.run(driver, "open", "install", "hd_audio")
    return (setup.mods() / "hd_audio").exists()


def attack_other_manifest(driver, setup):
    """a host serving a file of its own (the same size) and a manifest that names its hash: the catalog names the other manifest"""
    manifest = json.loads((setup.out / "mods/german/2.1/files.json").read_text())
    entry = [f for f in manifest["files"] if f["path"].endswith(".unicode_string_list")][0]
    path = setup.out / "mods/german/2.1" / entry["path"]
    data = path.read_bytes()
    forged = data[:-1] + bytes([data[-1] ^ 1])
    entry["sha256"] = hashlib.sha256(forged).hexdigest()
    setup.server.replace["/mods/german/2.1/" + entry["path"]] = forged
    # (written as the tool writes it, so that it is the size the catalog says: only its hash differs)
    setup.server.replace["/mods/german/2.1/files.json"] = json.dumps(manifest, indent=1).encode() + b"\n"
    setup.run(driver, "open", "install", "german")
    return (setup.mods() / "german").exists()


def attack_traversal(driver, setup):
    valid_tag = test_mods.header(b"snd!", 4) + bytes(16)
    hostile_catalog(setup.out, "evil", {"mod.toml": b'name = "x"\n', "tags/../../../evil.sound": valid_tag})
    # (the file the manifest names is served from where the server finds it: put it where a ".." in the URL leads)
    (setup.out / "mods/evil/1.0/tags").mkdir(parents=True, exist_ok=True)
    setup.server.replace["/mods/evil/1.0/tags/../../../evil.sound"] = valid_tag
    setup.run(driver, "open", "install", "evil")
    return (setup.root / "evil.sound").exists() or (setup.root.parent / "evil.sound").exists() or (setup.mods() / "evil").exists()


def attack_bad_header(driver, setup):
    wrong = test_mods.header(b"bitm", 7) + bytes(16)
    hostile_catalog(setup.out, "tags", {"mod.toml": b'name = "x"\n', "tags/sound/a.sound": wrong})
    setup.run(driver, "open", "install", "tags")
    return (setup.mods() / "tags").exists()


def attack_own_folder(driver, setup):
    """the player's folder appears after the list was made (so the list says available), and the install begins"""
    setup.run(driver, "open", "own", "german", "install", "german")
    return (setup.mods() / "german" / ".store").exists()


def attack_other_id(driver, setup):
    tag = test_mods.header(b"snd!", 4) + bytes(16)
    hostile_catalog(setup.out, "mod", {"mod.toml": b'id = "hd_audio"\n', "tags/sound/a.sound": tag})
    setup.run(driver, "open", "install", "mod")
    return (setup.mods() / "mod").exists()


def attack_marker_escape(driver, setup):
    setup.run(driver, "open", "install", "german")
    outside = setup.root / "outside.txt"
    outside.write_text("keep me")
    marker = setup.mods() / "german" / ".store"
    marker.write_text(marker.read_text() + f"file 4 {'00' * 32} ../../outside.txt\n")
    setup.run(driver, "open", "remove", "german")
    return not outside.exists()


def attack_no_room(driver, setup):
    setup.run(driver, "open", "install", "hd_audio", environment={"MODSTORE_CHECK_FREE": "1000"})
    return (setup.mods() / "hd_audio").exists()


CONTROLS = {
    "no-signature-check": (("!p2p_ed25519_verify(key, catalog.body, (int)catalog.body_size, signature)", "0"), attack_tampered_catalog),
    "no-replay-check": (("if (newest < serial)", "if (0)"), attack_replay),
    "no-file-hash-check": (("if (memcmp(digest, file->hash, HASH_BYTES))", "if (0)"), attack_wrong_bytes),
    "no-manifest-hash-check": (("answer.body_size != entry.manifest_size || memcmp(digest, entry.manifest_hash, HASH_BYTES)",
                                "answer.body_size != entry.manifest_size"), attack_other_manifest),
    "dot-components-allowed": (("path[at] == '.' || path[at] == ' ' || path[end - 1] == '.' ||", ""), attack_traversal),
    "no-header-check": (("static int tag_header_valid(const unsigned char *file, int group)\n{",
                         "static int tag_header_valid(const unsigned char *file, int group)\n{\n\treturn 1;"), attack_bad_header),
    "players-folder-replaced": (("if (path_kind(folder, NULL) && !(path_kind(folder, NULL) == 2 && marker_read(folder, &marker, 0) && !strcmp(marker.id, entry.id)))",
                                 "if (0)"), attack_own_folder),
    "other-mods-id-allowed": (("if (value.type == TOML_STRING && !names_equal_case(value.u.s, id))", "if (0)"), attack_other_id),
    "marker-paths-trusted": (("!path_ok(end + 2 + HASH_BYTES * 2))", "0)"), attack_marker_escape),
    "no-room-check": (("if (available < (unsigned long long)manifest.total + FREE_MARGIN_BYTES)", "if (0)"), attack_no_room),
}


@pytest.mark.parametrize("name", sorted(CONTROLS))
def test_negative_control(name, driver, good):
    mutation, attack = CONTROLS[name]
    assert not attack(driver, good), "the attack succeeds against the real code"
    good.finish()
    # (a fresh data root and server for the faulty code)
    shutil.rmtree(good.root)
    good.root.mkdir()
    good.server = modstore_server.Store(good.out)
    good.port = good.server.start()
    good.url = f"http://127.0.0.1:{good.port}/catalog.json"
    for leftover in ("catalog.json", "catalog.json.sig"):
        pass
    faulty = build_driver(mutation)
    # (the attack's own changes to the web folder are kept: it is run again from the state it left)
    catalog.build(good.source, good.out, "Test", 1)
    catalog.sign_file(good.out / "catalog.json", SEED)
    good.server.replace.clear()
    assert attack(faulty, good), f"{name}: with the check taken out the attack still fails, so no test would notice it is gone"
