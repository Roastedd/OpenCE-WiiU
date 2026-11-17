"""The actual native reader/converter under MAP_VALIDATE_RUNNER, plus the
computer helper. Synthetic disc images contain only test strings. Optional
retail comparisons use HALO_XBOX_MAPS locally and never publish game data.
"""
import os
import shlex
import struct
import subprocess
import sys
import zlib
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/wiiu"))
from disc_image import DiscImage, MAGIC, RETAIL_MAPS, SetupError
from setup_assistant import prepare_maps
import setup_assistant
import disc_image
import convert_maps
import map_convert

TOOL = Path(os.environ.get("MAP_VALIDATE", ROOT / "build/linux-ppc/map_validate"))
RUNNER = shlex.split(os.environ.get("MAP_VALIDATE_RUNNER", ""))


def native(*arguments):
    if not TOOL.exists():
        pytest.skip("build map_validate and set MAP_VALIDATE_RUNNER for a cross build")
    return subprocess.run([*RUNNER, str(TOOL), *map(str, arguments)], capture_output=True, timeout=120)


def table(entries):
    rows = []
    for name, sector, size, directory in entries:
        encoded = name.encode("ascii")
        row = bytearray(struct.pack("<HHIIBB", 0, 0, sector, size, 0x10 if directory else 0, len(encoded)) + encoded)
        row.extend(bytes((-len(row)) % 4))
        rows.append(row)
    at = 0
    for index, row in enumerate(rows[:-1]):
        at += len(row)
        struct.pack_into("<H", row, 2, at // 4)
    return b"".join(rows)


def disc(path, files=None, partition=0, high_file=False):
    files = files or {"ui.map": b"test map payload"}
    sector = 0x200000 if high_file else 36
    members, writes = [], []
    for name, data in files.items():
        members.append((name, sector, len(data), False))
        writes.append((partition + sector * 2048, data))
        sector += (len(data) + 2047) // 2048
    maps = table(members)
    root = table([("maps", 34, len(maps), True)])
    volume = bytearray(2048)
    volume[:20] = volume[0x7EC:0x800] = MAGIC
    struct.pack_into("<II", volume, 0x14, 33, len(root))
    with path.open("wb") as output:
        for offset, data in [(partition + 0x10000, volume), (partition + 33 * 2048, root),
                             (partition + 34 * 2048, maps), *writes]:
            output.seek(offset)
            output.write(data)
    return files


@pytest.mark.parametrize("partition", [0, 0x18300000])
def test_native_and_computer_read_the_same_partition(tmp_path, partition):
    path = tmp_path / "halo.iso"
    files = disc(path, partition=partition)
    with DiscImage(path) as image:
        entries = image.maps(require_complete=False)
        assert image.read(entries["ui.map"].offset, entries["ui.map"].size) == files["ui.map"]
    output = tmp_path / "output"
    output.mkdir()
    result = native("--extract-xiso", path, output)
    assert result.returncode == 0, result.stderr.decode()
    assert (output / "maps/ui.map").read_bytes() == files["ui.map"]


def test_split_read_crosses_the_volume_descriptor_and_file(tmp_path):
    original = tmp_path / "halo.iso"
    files = disc(original, {"ui.map": b"a" * 7000})
    raw = original.read_bytes()
    # The descriptor and the map each cross a part boundary.
    boundaries = [0, 0x10000 + 17, 36 * 2048 + 31, len(raw)]
    for part, (start, end) in enumerate(zip(boundaries, boundaries[1:])):
        (tmp_path / f"halo.iso.{part}").write_bytes(raw[start:end])
    first = tmp_path / "halo.iso.0"
    with DiscImage(first) as image:
        entry = image.maps(require_complete=False)["ui.map"]
        assert image.read(entry.offset, entry.size) == files["ui.map"]
    output = tmp_path / "output"
    output.mkdir()
    result = native("--extract-xiso", first, output)
    assert result.returncode == 0, result.stderr.decode()
    assert (output / "maps/ui.map").read_bytes() == files["ui.map"]


def test_a_map_beyond_four_gib_uses_64_bit_offsets(tmp_path):
    path = tmp_path / "full-disc.iso"
    files = disc(path, high_file=True)  # sparse; only kilobytes are allocated
    with DiscImage(path) as image:
        entry = image.maps(require_complete=False)["ui.map"]
        assert entry.offset >= 2**32
        assert image.read(entry.offset, entry.size) == files["ui.map"]
    output = tmp_path / "output"
    output.mkdir()
    result = native("--extract-xiso", path, output)
    assert result.returncode == 0, result.stderr.decode()
    assert (output / "maps/ui.map").read_bytes() == files["ui.map"]


def test_truncated_disc_is_refused_before_installation(tmp_path):
    path = tmp_path / "halo.iso"
    disc(path)
    with path.open("r+b") as file:
        file.truncate(path.stat().st_size - 1)
    with DiscImage(path) as image, pytest.raises(SetupError, match="incomplete"):
        image.maps(require_complete=False)
    output = tmp_path / "output"
    output.mkdir()
    assert native("--extract-xiso", path, output).returncode != 0
    assert not (output / "maps").exists()


def test_traversal_name_cannot_escape_extraction(tmp_path):
    path = tmp_path / "halo.iso"
    disc(path, {"../ui.map": b"unsafe"})
    with DiscImage(path) as image, pytest.raises(SetupError):
        image.maps(require_complete=False)
    output = tmp_path / "output"
    output.mkdir()
    assert native("--extract-xiso", path, output).returncode != 0
    assert not (tmp_path / "ui.map").exists()


def test_setup_refuses_to_replace_existing_maps_or_saves(tmp_path):
    source = tmp_path / "source"
    source.mkdir()
    target = tmp_path / "card/wiiu/apps/halo/data"
    (target / "maps").mkdir(parents=True)
    (target / "maps/ui.map").write_bytes(b"existing")
    (target / "save.bin").write_bytes(b"save")
    with pytest.raises(SetupError, match="already exists"):
        prepare_maps(source, tmp_path / "card")
    assert (target / "maps/ui.map").read_bytes() == b"existing"
    assert (target / "save.bin").read_bytes() == b"save"


def test_computer_can_stage_an_iso_without_uploading_or_converting(tmp_path):
    path = tmp_path / "halo.iso"
    disc(path, {name: b"local fixture" for name in RETAIL_MAPS})
    card = tmp_path / "card"
    card.mkdir()
    installed = setup_assistant.stage_image(path, card)
    assert installed.read_bytes() == path.read_bytes()
    assert not (card / "wiiu/apps/halo/data/maps").exists()


def test_computer_splits_an_iso_and_the_reader_reassembles_it(tmp_path, monkeypatch):
    path = tmp_path / "halo.iso"
    disc(path, {name: b"local fixture" for name in RETAIL_MAPS})
    monkeypatch.setattr(setup_assistant, "FAT32_FILE_LIMIT", 32 * 1024)
    monkeypatch.setattr(setup_assistant, "ISO_PART_BYTES", 16 * 1024)
    card = tmp_path / "card"
    card.mkdir()
    first = setup_assistant.stage_image(path, card)
    assert first.name == "halo.iso.0"
    parts = sorted(first.parent.glob("halo.iso.*"), key=lambda p: int(p.suffix[1:]))
    assert b"".join(p.read_bytes() for p in parts) == path.read_bytes()
    with DiscImage(first) as image:
        assert set(RETAIL_MAPS) <= image.maps().keys()


def test_a_cancelled_iso_copy_leaves_no_partial_launchable_image(tmp_path):
    path = tmp_path / "halo.iso"
    disc(path, {name: b"local fixture" for name in RETAIL_MAPS})
    card = tmp_path / "card"
    card.mkdir()
    def cancel(*_):
        raise SetupError("cancelled")
    with pytest.raises(SetupError, match="cancelled"):
        setup_assistant.stage_image(path, card, cancel)
    assert not list(card.rglob("halo.iso*"))


@pytest.mark.parametrize("from_iso,cancel_write", [(False, False), (True, False), (False, True)])
def test_computer_setup_with_a_real_local_ui_map(tmp_path, monkeypatch, from_iso, cancel_write):
    folder = os.environ.get("HALO_XBOX_MAPS")
    if not folder or not (Path(folder) / "ui.map").exists():
        pytest.skip("set HALO_XBOX_MAPS to local original Xbox maps")
    monkeypatch.setattr(setup_assistant, "RETAIL_MAPS", ("ui.map",))
    monkeypatch.setattr(disc_image, "RETAIL_MAPS", ("ui.map",))
    source = Path(folder)
    if from_iso:
        source = tmp_path / "halo.iso"
        disc(source, {"ui.map": (Path(folder) / "ui.map").read_bytes()})
    card = tmp_path / "card"
    data = card / "wiiu/apps/halo/data"
    data.mkdir(parents=True)
    (data / "save.bin").write_bytes(b"saved game")
    def progress(text, *_):
        if cancel_write and text.startswith("Writing"):
            raise SetupError("cancelled")
    if cancel_write:
        with pytest.raises(SetupError, match="cancelled"):
            prepare_maps(source, card, progress)
        assert not (data / "maps").exists()
    else:
        target = prepare_maps(source, card, progress)
        native_output = tmp_path / "native.map"
        result = native("--import-wiiu-map", Path(folder) / "ui.map", native_output)
        assert result.returncode == 0, result.stderr.decode()
        def inflate(path):
            raw = path.read_bytes()
            return raw[:0x800] + zlib.decompress(raw[0x800:])
        assert inflate(target / "ui.map") == inflate(native_output)
    assert (data / "save.bin").read_bytes() == b"saved game"
    assert not list(data.glob("iso-import-*"))


@pytest.mark.parametrize("contents", [b"short", b"daeh" + bytes(2044),
    struct.pack("<III", 0x68656164, 5, 0x7FFFFFFF) + bytes(2036)])
def test_bad_map_cannot_damage_an_existing_destination(tmp_path, contents):
    source, target = tmp_path / "bad.map", tmp_path / "existing.map"
    source.write_bytes(contents)
    target.write_bytes(b"keep")
    assert native("--import-wiiu-map", source, target).returncode != 0
    assert target.read_bytes() == b"keep"
    assert not (tmp_path / "existing.map.part").exists()


@pytest.mark.parametrize("name", RETAIL_MAPS)
def test_native_rebased_map_matches_the_computer_converter(tmp_path, name):
    folder = os.environ.get("HALO_XBOX_MAPS")
    if not folder or not (Path(folder) / name).exists():
        pytest.skip("set HALO_XBOX_MAPS to local original Xbox maps")
    source, output = Path(folder) / name, tmp_path / "converted.map"
    result = native("--import-wiiu-map", source, output)
    assert result.returncode == 0, result.stderr.decode()
    raw = output.read_bytes()
    actual = raw[:0x800] + zlib.decompress(raw[0x800:])
    expected = convert_maps.converter().convert(map_convert.read_map(source), rebase=convert_maps.WIIU_REBASE).data
    assert actual == expected, name
