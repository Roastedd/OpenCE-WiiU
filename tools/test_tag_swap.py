"""Tests for the byte swap the big-endian builds turn Halo Custom Edition maps with as they load
(port/linux/game/tag_swap.c; docs/custom_edition_caches.md, "Big-endian builds").

- Its tables (port/linux/game/tag_swap_layouts.inc) are what tools/wiiu/tag_swap_tables.py makes of the schema files
  tools/wiiu/map_convert.py converts the Xbox maps with.
- It makes the same bytes as map_convert.py's own walk: of synthetic Custom Edition caches (tools/test_cache_file_formats.py's
  builder) as cache_file_report --dump-tags leaves their tags, with their structure bsps and model data
  (map_validate --swap against map_convert.py --custom-edition), and of the Xbox maps when they are present (assets/maps:
  map_validate --swap-xbox against map_convert.py).

The swap turns little-endian into big-endian on any host, so map_validate is the build's own (build/linux/map_validate, or
MAP_VALIDATE), run as it is or under MAP_VALIDATE_RUNNER (qemu-ppc for the big-endian build's, say).
"""
import os
import shlex
import shutil
import struct
import subprocess
import sys
import zlib
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools/wiiu"))
import map_convert  # noqa: E402
import tag_swap_tables  # noqa: E402
import test_cache_file_formats as caches  # noqa: E402

MAP_VALIDATE = Path(os.environ.get("MAP_VALIDATE", ROOT / "build/linux/map_validate"))
RUNNER = shlex.split(os.environ.get("MAP_VALIDATE_RUNNER", ""))


def test_tables_are_what_the_schema_files_make():
    assert Path(tag_swap_tables.OUTPUT).read_text() == tag_swap_tables.generate(), \
        "run tools/wiiu/tag_swap_tables.py: port/linux/game/tag_swap_layouts.inc is not what tools/wiiu/map_schema makes"


@pytest.fixture(scope="module")
def schema(tmp_path_factory):
    """the schema files, as map_convert.py takes them"""
    folder = tmp_path_factory.mktemp("map-schema")
    for name in ("tag_schema.json", "tag_layouts.json", "schema_dump.nm"):
        (folder / name).write_bytes(zlib.decompress((ROOT / "tools/wiiu/map_schema" / f"{name}.gz").read_bytes(), 31))
    return folder


def map_convert_command(schema, *arguments):
    return [sys.executable, str(ROOT / "tools/wiiu/map_convert.py"), "--schema", str(schema / "tag_schema.json"),
            "--layouts", str(schema / "tag_layouts.json"), "--symbols", str(schema / "schema_dump.nm"), *map(str, arguments)]


def map_validate(*arguments):
    if not MAP_VALIDATE.is_file():
        pytest.skip(f"needs the build's map_validate ({MAP_VALIDATE}: ninja linux)")
    result = subprocess.run([*RUNNER, str(MAP_VALIDATE), *map(str, arguments)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout


# (the builder's weapon is a stub of 0x100 bytes and its shaders of 0x28, which the loader's tests need no more of: the
# swap walks a tag whole, so the weapon is given a group no schema has, and left as it is)
UNWALKED_GROUP = caches.code("zzzz")

SYNTHETIC = {
    "minimal": {},
    "model": {"model": {}},
    "model with local nodes": {"model": {"local_node_count": 2}},
    "structure bsp material": {"bsp_material": {}},
    "several structure bsps": {"bsp_sizes": (0x1000, 0x800)},
    "pitch ranges": {"pitch_ranges": 3},
    "animation graph": {"animation_overlay": 0},
    "weapon HUD": {"weapon_hud": [((1.0, 1.0), 4), ((0.5, 0.5), 0), ((1.0, 1.0), 4)]},
    "everything": {"model": {}, "bsp_material": {}, "animation_overlay": 0, "pitch_ranges": 2,
                   "weapon_hud": [((1.0, 1.0), 4), ((0.5, 0.5), 0), ((1.0, 1.0), 4)]},
}


@pytest.mark.parametrize("case", sorted(SYNTHETIC))
def test_custom_edition_swap_is_map_converts(report_tool, schema, tmp_path, case):
    cache = caches.Map(**SYNTHETIC[case])
    path = cache.write(tmp_path)
    data = bytearray(path.read_bytes())
    struct.pack_into("<I", data, cache.where["instances"] + cache.tag_indices["test\\weapon"] * 0x20, UNWALKED_GROUP)
    path.write_bytes(data)
    returncode, report = caches.report_one(report_tool, path, "--dump-tags", tmp_path / "tags")
    assert returncode == 0 and report["convert"] == "ok", report
    map_validate("--swap", tmp_path / "tags", tmp_path / "c", path)
    subprocess.run(map_convert_command(schema, "--custom-edition", "--map", path, tmp_path / "tags", tmp_path / "python"),
                   check=True, capture_output=True)
    outputs = sorted(name.name[len("python"):] for name in tmp_path.glob("python*"))
    assert ".bsp0" in outputs and ".model" in outputs, outputs
    for suffix in outputs:
        assert (tmp_path / f"c{suffix}").read_bytes() == (tmp_path / f"python{suffix}").read_bytes(), suffix
    # (and turned: the tag index's signature reads 'tags' big-endian)
    assert (tmp_path / "c").read_bytes()[0x24:0x28] == b"tags"


def swap_status(*arguments):
    """map_validate's exit status and what it said (the swap refused, or not)"""
    if not MAP_VALIDATE.is_file():
        pytest.skip(f"needs the build's map_validate ({MAP_VALIDATE}: ninja linux)")
    result = subprocess.run([*RUNNER, str(MAP_VALIDATE), *map(str, arguments)], capture_output=True, text=True)
    return result.returncode, result.stdout + result.stderr


def weapon_hud_dump(report_tool, tmp_path, statics_count, slack):
    """a weapon HUD map's tags (as the report tool dumps them), its statics block given statics_count elements, at the start of `slack` zero bytes
    added to the end of the tags; (the map's path, the tags' path, where the block is in the tags)"""
    cache = caches.Map(weapon_hud=[((1.0, 1.0), 4), ((0.5, 0.5), 0), ((1.0, 1.0), 4)])
    path = cache.write(tmp_path)
    data = bytearray(path.read_bytes())
    struct.pack_into("<I", data, cache.where["instances"] + cache.tag_indices["test\\weapon"] * 0x20, UNWALKED_GROUP)
    path.write_bytes(data)
    returncode, report = caches.report_one(report_tool, path, "--dump-tags", tmp_path / "tags")
    assert returncode == 0 and report["convert"] == "ok", report
    tags = bytearray((tmp_path / "tags").read_bytes())
    block = cache.addresses["test\\weapon hud"] - caches.BASE + 0x60
    struct.pack_into("<II", tags, block, statics_count, caches.BASE + len(tags))
    tags += bytes(slack)
    (tmp_path / "tags").write_bytes(tags)
    return path, tmp_path / "tags", block


def test_a_block_past_the_games_maximum_is_walked_only_as_far_as_the_validator_cuts_it(report_tool, tmp_path):
    """The validator cuts a block's elements to the game's maximum (a weapon HUD's statics: 16) before it follows the block, so a count of a protected
    map's that is garbage is played on the desktop builds; the swap used to follow the count it was given and refuse the map ('has N elements'). Here the
    statics have 0x40000000 elements at 16 elements' worth of zero bytes: cut to 16, the swap goes through; the count is kept (turned), for the validator
    to cut again. A negative count is not cut, as the validator refuses it."""
    path, tags, block = weapon_hud_dump(report_tool, tmp_path, 0x40000000, 16 * 0xB4)
    returncode, said = swap_status("--swap", tags, tmp_path / "c", path)
    assert returncode == 0, said
    assert (tmp_path / "c").read_bytes()[block:block + 4] == struct.pack(">I", 0x40000000)
    refused = tmp_path / "refused"
    path, tags, block = weapon_hud_dump(report_tool, refused, 0x80000000, 16 * 0xB4)
    returncode, said = swap_status("--swap", tags, refused / "c", path)
    assert returncode != 0 and "elements" in said, said


def test_a_block_is_cut_to_the_games_maximum_and_no_further(report_tool, tmp_path):
    """16 elements of a block that may have 16 are followed whole (15 elements' room is too little), and 17 are cut to 16, which 15 elements' room is too
    little for as well: the cut is the validator's, no more lenient."""
    path, tags, _ = weapon_hud_dump(report_tool, tmp_path / "full", 16, 16 * 0xB4)
    returncode, said = swap_status("--swap", tags, tmp_path / "full" / "c", path)
    assert returncode == 0, said
    for count in (16, 17):
        path, tags, _ = weapon_hud_dump(report_tool, tmp_path / f"short{count}", count, 15 * 0xB4)
        returncode, said = swap_status("--swap", tags, tmp_path / f"short{count}" / "c", path)
        assert returncode != 0 and "outside the tags" in said, said


def xbox_maps():
    folder = ROOT / "assets/maps"
    return sorted(folder.glob("*.map"), key=lambda path: path.stat().st_size)[:2] if folder.is_dir() else []


@pytest.mark.parametrize("path", xbox_maps(), ids=lambda path: path.name)
def test_xbox_swap_is_map_converts(schema, tmp_path, path):
    map_validate("--swap-xbox", path, tmp_path / "c.map")
    subprocess.run(map_convert_command(schema, "--level", "1", path, tmp_path / "python.map"), check=True, capture_output=True)
    converted = (tmp_path / "python.map").read_bytes()
    inflated = converted[:map_convert.HEADER_SIZE] + zlib.decompress(converted[map_convert.HEADER_SIZE:])
    assert (tmp_path / "c.map").read_bytes() == inflated


@pytest.fixture(scope="module")
def report_tool(tmp_path_factory):
    clang = shutil.which("clang")
    if clang is None:
        pytest.skip("clang is needed to build the report tool")
    output = tmp_path_factory.mktemp("cache-file-report") / "cache_file_report"
    result = subprocess.run([clang, "-std=c99", "-O1", f"-I{caches.MODULE.parent}", str(caches.MODULE), str(caches.TOOL),
                             "-o", str(output)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return output
