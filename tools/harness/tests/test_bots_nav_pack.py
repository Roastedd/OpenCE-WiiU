"""tools/bots_nav_pack.py against the C reader (port/linux/game/bots_nav.c): a pack the tool merged from two records is read by the game's own functions
(the same areas, links, portals, scripts and places), the tool's verify and reach reports are right on a small graph, and it refuses a bad pack."""

import struct
import subprocess
import sys
import zlib
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import bots_nav_pack as tool  # noqa: E402
import harness  # noqa: E402
from harness import build, read  # noqa: E402


def run(executable, case, *arguments):
    result = subprocess.run([*harness.runner(), str(executable), case, *arguments], capture_output=True, text=True, errors="replace", timeout=60)
    return result.returncode, (result.stdout + result.stderr).strip()


def record(areas, links, scripts, places, bounds=(-4.0, -4.0, 0.0, 40.0, 40.0, 4.0)):
    """A record's bytes by the format's description (areas: (x, y, z, radius); links: (from, to, type, cost, script, exit, entry); places:
    (type, area, x, y, z))."""
    out = bytearray(tool.RECORD_HEADER_BYTES)
    struct.pack_into("<IHHHH", out, 0, 0, len(areas), len(links), len(places), len(scripts))
    struct.pack_into("<6f", out, 12, *bounds)
    struct.pack_into("<3f", out, 36, 2.25, 0.7, 2.4)
    ordered = sorted(range(len(links)), key=lambda k: links[k][0])
    first = {}
    for position, k in enumerate(ordered):
        first.setdefault(links[k][0], [position, 0])
        first[links[k][0]][1] += 1
    for index, (x, y, z, radius) in enumerate(areas):
        begin, count = first.get(index, [0, 0])
        out += struct.pack("<3fHHHHBB", x, y, z, begin, count, int(radius * 16), 0, 0, 0) + b"\0\0"
    for k in ordered:
        source, to, kind, cost, script, exit_p, entry_p = links[k]
        a, b = areas[source], areas[to]
        out += struct.pack("<HBBHH", to, kind, 0, int(cost * 16), script)
        out += struct.pack("<3h", *[round((exit_p[i] - a[i]) * 64) for i in range(3)])
        out += struct.pack("<3h", *[round((entry_p[i] - b[i]) * 64) for i in range(3)])
    for launch, yaw_turns in scripts:
        out += struct.pack("<3fHHHBBB", *launch, int(yaw_turns * 65536) & 0xFFFF, 64, 40, 1, 3, 3) + b"\0\0\0"
    for kind, area, x, y, z in places:
        out += struct.pack("<BBH3fHHHHI", kind, 0, area, x, y, z, 0, 90, 0, 0, 0) + b"\0\0\0\0"
    return bytes(out)


def one_map_pack(name, surfaces, walkable, rec):
    index_end = tool.HEADER_BYTES + tool.INDEX_BYTES
    tail = name.encode().ljust(32, b"\0") + struct.pack("<IIII", surfaces, walkable, index_end, len(rec)) + rec
    header = tool.MAGIC + struct.pack("<HHIIIII", 1, 1, 1, tool.HEADER_BYTES, zlib.crc32(tail) & 0xFFFFFFFF, 1, 0)
    return header + tail


GRID = dict(
    areas=[(0.0, 0.0, 1.0, 5.0), (8.0, 0.0, 1.0, 5.0), (16.0, 0.0, 1.0, 5.0), (24.0, 0.0, 1.0, 5.0)],
    links=[(0, 1, 0, 3.0, 0xFFFF, (4.0, 0.25, 1.0), (4.5, 0.0, 1.0)), (1, 0, 0, 3.0, 0xFFFF, (4.5, 0.0, 1.0), (4.0, 0.25, 1.0)),
           (1, 2, 0, 3.0, 0xFFFF, (12.0, 0.0, 1.0), (12.5, 0.0, 1.0)), (2, 1, 0, 3.0, 0xFFFF, (12.5, 0.0, 1.0), (12.0, 0.0, 1.0)),
           (2, 3, 2, 3.0, 0, (20.0, 0.0, 1.0), (20.5, 0.0, 1.0))],
    scripts=[((19.0, 0.0, 1.0), 0.0)],
    places=[(5, 0, 0.0, 0.0, 1.0), (1, 1, 8.0, 0.0, 1.0), (1, 3, 24.0, 0.0, 1.0), (2, 2, 16.0, 0.0, 1.0)])


def test_the_c_reader_reads_the_tools_merge(tmp_path):
    first = tmp_path / "a.nav"
    second = tmp_path / "b.nav"
    first.write_bytes(one_map_pack("grid", 100, 40, record(**GRID)))
    second.write_bytes(one_map_pack("aaa", 5, 4, record(areas=[(0, 0, 0, 3), (4, 0, 0, 3)], links=[], scripts=[((0, 0, 0), 0.25)],
                                                         places=[(5, 0, 0, 0, 0)])))
    merged = tmp_path / "navs.pack"
    size, names, marked = tool.merge(merged, [first, second])
    assert marked == {}
    assert names == ["aaa", "grid"] and size == merged.stat().st_size
    tool.check_crc(merged.read_bytes())
    code = build("bots_nav", (("bots_nav.h", read("port/linux/game/bots_nav.h")), ("under_test.inc", read("port/linux/game/bots_nav.c"))))
    status, output = run(code, "file", str(merged), "grid")
    assert status == 0, output
    surfaces, walkable, areas, links, scripts, places, x1, exit_y, entry_x, yaw = output.split()
    assert (surfaces, walkable, areas, links, scripts, places) == ("100", "40", "4", "5", "1", "4")
    assert float(x1) == 8.0
    # (area 1's first link: to area 0, exit (4.5, 0, 1), entry (4, 0.25, 1))
    assert abs(float(exit_y)) < 0.02 and abs(float(entry_x) - 4.0) < 0.02 and float(yaw) == 0.0
    status, output = run(code, "file", str(merged), "aaa")
    assert status == 0, output
    assert output.split()[2:6] == ["2", "0", "1", "1"]


def test_verify_and_reach(tmp_path):
    path = tmp_path / "grid.nav"
    path.write_bytes(one_map_pack("grid", 100, 40, record(**GRID)))
    assert tool.main(["verify", str(path)]) == 0
    data = path.read_bytes()
    pack = tool.read_pack(data)
    rec = tool.parse_record(data[pack["entries"][0]["offset"]:pack["entries"][0]["offset"] + pack["entries"][0]["size"]])
    assert len(rec["areas"]) == 4 and len(rec["links"]) == 5 and rec["links"][4]["type"] == 2 and rec["links"][4]["from"] == 2
    # (the jump is one way: from the start in area 0 every place is reached; the place in area 3 reaches nothing back)
    report = tool.reach_report(rec)
    assert report["starts"] == 1 and report["mean"] == 1.0 and report["never"] == []
    assert tool.reach_from(rec, 3) == {3}
    assert tool.reach_from(rec, 0) == {0, 1, 2, 3}
    assert tool.reach_from(rec, 0, allowed=(0,)) == {0, 1, 2}


def test_a_bad_pack_is_refused(tmp_path):
    good = one_map_pack("grid", 100, 40, record(**GRID))
    for name, data in (("magic", b"X" + good[1:]), ("crc", good[:-1] + bytes([good[-1] ^ 1])), ("short", good[:20]),
                       ("version", good[:8] + b"\x09\x00" + good[10:])):
        path = tmp_path / f"{name}.pack"
        path.write_bytes(data)
        assert tool.main(["verify", str(path)]) == 1, name
    duplicate = tmp_path / "d.nav"
    duplicate.write_bytes(good)
    with pytest.raises(tool.PackError):
        tool.merge(tmp_path / "out.pack", [duplicate, duplicate])


def test_a_fragmented_map_is_marked(tmp_path):
    """From the one start the graph reaches the places of its own piece only: marked, unless --no-gate; a sound map is not."""
    broken = dict(GRID)
    # (the link out of area 0 is gone: the start reaches itself and nothing else)
    broken["links"] = [link for link in GRID["links"] if link[0] != 0]
    path = tmp_path / "broken.nav"
    path.write_bytes(one_map_pack("broken", 100, 40, record(**broken)))
    good = tmp_path / "good.nav"
    good.write_bytes(one_map_pack("good", 100, 40, record(**GRID)))
    merged = tmp_path / "navs.pack"
    _, names, marked = tool.merge(merged, [path, good])
    assert names == ["broken", "good"] and list(marked) == ["broken"] and marked["broken"]["mean"] < tool.GATE_MEAN
    data = merged.read_bytes()
    pack = tool.read_pack(data)
    flags = {e["name"]: tool.parse_record(data[e["offset"]:e["offset"] + e["size"]])["flags"] for e in pack["entries"]}
    assert flags == {"broken": tool.FLAG_FRAGMENTED, "good": 0}
    tool.check_crc(data)
    _, _, marked = tool.merge(tmp_path / "plain.pack", [path, good], gate=False)
    assert marked == {}
    data = (tmp_path / "plain.pack").read_bytes()
    pack = tool.read_pack(data)
    assert all(tool.parse_record(data[e["offset"]:e["offset"] + e["size"]])["flags"] == 0 for e in pack["entries"])
    assert tool.main(["info", str(merged)]) == 0


def test_merge_takes_only_the_maps_asked_for(tmp_path):
    """--only: of a pack of several maps, the records named (a map baked again replaces its old record in a pack built so)."""
    first = tmp_path / "a.nav"
    second = tmp_path / "b.nav"
    first.write_bytes(one_map_pack("grid", 100, 40, record(**GRID)))
    second.write_bytes(one_map_pack("other", 5, 4, record(areas=[(0, 0, 0, 3), (4, 0, 0, 3)], links=[], scripts=[], places=[(1, 0, 0, 0, 0)])))
    both = tmp_path / "both.pack"
    tool.merge(both, [first, second])
    only = tmp_path / "only.pack"
    _, names, _ = tool.merge(only, [both], only={"other"})
    assert names == ["other"]
    assert tool.main(["merge", str(tmp_path / "cli.pack"), str(both), "--only", "Grid"]) == 0
    assert [e["name"] for e in tool.read_pack((tmp_path / "cli.pack").read_bytes())["entries"]] == ["grid"]
    # (without it, merging the pack with a record of one of its maps is a duplicate)
    with pytest.raises(tool.PackError):
        tool.merge(tmp_path / "dup.pack", [both, first])
