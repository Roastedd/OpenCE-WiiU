#!/usr/bin/env python3
"""The bots' navigation pack (port/linux/game/bots_nav.[ch]): merges the bake's one-map packs into navs.pack, checks a pack, and reports what
the bake made of each map (counts, which places the links join, from every start).

    tools/bots_nav_pack.py merge OUT.pack MAP.nav [MAP.nav ...]   one pack with an index; the maps in name order
    tools/bots_nav_pack.py verify PACK                            the crc and the records' bounds; exit status 1 if it is no good
    tools/bots_nav_pack.py info PACK [--reach]                    a table of the maps (with --reach: from every start, the places reached)

The file is little-endian, read byte by byte (bots_nav.c has the layout). It is signed like shaders.pack: tools/wiiu/make_wiiu_release.py puts
it in the release's manifest (its SHA-256 and size) next to the shaders, and the manifest carries the Ed25519 signature the console checks.
The pack's own crc32 (over everything after the 32-byte header) is a check against a card that went bad.
"""

import argparse
import struct
import sys
import zlib
from pathlib import Path

MAGIC = b"HALOBOTN"
PACK_VERSION = 1
NAV_FORMAT = 1
HEADER_BYTES = 32
INDEX_BYTES = 48
RECORD_HEADER_BYTES = 64
AREA_BYTES = 24
LINK_BYTES = 20
SCRIPT_BYTES = 24
PLACE_BYTES = 32

LINK_TYPES = ["walk", "step", "jump", "drop", "teleport"]
PLACE_TYPES = {1: "item", 2: "flag", 3: "ball", 4: "hill", 5: "start", 6: "tele-from", 7: "tele-to", 8: "vehicle", 9: "race"}


class PackError(Exception):
    pass


def read_pack(data):
    """The pack's header and index: a dict with the maps' entries (name, surfaces, walkable, offset, size)."""
    if len(data) < HEADER_BYTES or data[:8] != MAGIC:
        raise PackError("not a navigation pack")
    version, fmt, maps, index_offset, crc, build = struct.unpack_from("<HHIIII", data, 8)
    if version != PACK_VERSION or fmt != NAV_FORMAT:
        raise PackError(f"a pack of version {version} format {fmt}")
    if not 1 <= maps <= 64 or index_offset != HEADER_BYTES:
        raise PackError(f"a pack with {maps} maps and its index at {index_offset}")
    entries = []
    for index in range(maps):
        raw = data[index_offset + index * INDEX_BYTES:index_offset + (index + 1) * INDEX_BYTES]
        if len(raw) < INDEX_BYTES:
            raise PackError("the index is short")
        name = raw[:32].split(b"\0")[0].decode("ascii", "replace")
        surfaces, walkable, offset, size = struct.unpack_from("<IIII", raw, 32)
        if offset + size > len(data) or size < RECORD_HEADER_BYTES:
            raise PackError(f"the record of {name} is outside the pack")
        entries.append({"name": name, "surfaces": surfaces, "walkable": walkable, "offset": offset, "size": size})
    return {"crc": crc, "build": build, "entries": entries}


def check_crc(data):
    crc = zlib.crc32(data[HEADER_BYTES:]) & 0xFFFFFFFF
    stored = struct.unpack_from("<I", data, 20)[0]
    if crc != stored:
        raise PackError(f"the crc is {crc:08x}, the header says {stored:08x}")


def parse_record(record):
    """A map's record: areas, links (with the exit and entry points made absolute), scripts, places, the header numbers."""
    flags, areas, links, places, scripts = struct.unpack_from("<IHHHH", record, 0)
    bounds = struct.unpack_from("<6f", record, 12)
    run_speed, jump_height, drop_height = struct.unpack_from("<3f", record, 36)
    need = RECORD_HEADER_BYTES + areas * AREA_BYTES + links * LINK_BYTES + scripts * SCRIPT_BYTES + places * PLACE_BYTES
    if len(record) < need:
        raise PackError(f"a record of {len(record)} bytes needs {need}")
    at = RECORD_HEADER_BYTES
    area_list = []
    for _ in range(areas):
        x, y, z, first, count, radius, region, aflags, exposure = struct.unpack_from("<3fHHHHBB", record, at)
        area_list.append({"p": (x, y, z), "first": first, "count": count, "radius": radius / 16.0, "flags": aflags})
        at += AREA_BYTES
    link_raw = []
    for _ in range(links):
        to, kind, lflags, cost, script = struct.unpack_from("<HBBHH", record, at)
        exit_p = struct.unpack_from("<3h", record, at + 8)
        entry_p = struct.unpack_from("<3h", record, at + 14)
        link_raw.append({"to": to, "type": kind, "flags": lflags, "cost": cost / 16.0, "script": script,
                         "exit": [v / 64.0 for v in exit_p], "entry": [v / 64.0 for v in entry_p]})
        at += LINK_BYTES
    # (the 'from' area of each link, by the areas' ranges)
    for index, area in enumerate(area_list):
        if area["first"] + area["count"] > links:
            raise PackError("an area's links are outside the table")
        for k in range(area["count"]):
            link_raw[area["first"] + k]["from"] = index
            if link_raw[area["first"] + k]["to"] >= areas:
                raise PackError("a link to an area that is not there")
    script_list = []
    for _ in range(scripts):
        lx, ly, lz, yaw, run_up, ticks, jump, successes, attempts = struct.unpack_from("<3fHHHBBB", record, at)
        script_list.append({"launch": (lx, ly, lz), "yaw": yaw / 65536.0 * 360.0, "run_up": run_up / 64.0, "ticks": ticks, "jump": jump,
                            "successes": successes, "attempts": attempts})
        at += SCRIPT_BYTES
    place_list = []
    for _ in range(places):
        kind, team, area, x, y, z, mask, period, item_class, aux, source = struct.unpack_from("<BBH3fHHHHI", record, at)
        place_list.append({"type": kind, "team": team, "area": area, "p": (x, y, z), "period": period, "item_class": item_class, "aux": aux,
                           "source": source})
        at += PLACE_BYTES
    return {"flags": flags, "bounds": bounds, "run_speed": run_speed, "jump_height": jump_height, "drop_height": drop_height,
            "areas": area_list, "links": link_raw, "scripts": script_list, "places": place_list}


def reach_from(record, start_area, allowed=None):
    """The areas reached from start_area over the links (a one-way link only its way)."""
    seen = {start_area}
    queue = [start_area]
    while queue:
        area = queue.pop()
        a = record["areas"][area]
        for k in range(a["count"]):
            link = record["links"][a["first"] + k]
            if allowed is not None and link["type"] not in allowed:
                continue
            if link["to"] not in seen:
                seen.add(link["to"])
                queue.append(link["to"])
    return seen


def reach_report(record):
    """From every start location, which of the places (items, flag stands, balls, hills, starts) are reached over the links: the fraction of
    them each start reaches, the worst start, and the places no start reaches."""
    places = [p for p in record["places"] if p["area"] != 0xFFFF and p["type"] in (1, 2, 3, 4, 5)]
    starts = [p for p in places if p["type"] == 5]
    report = {"places": len(places), "starts": len(starts), "by_start": []}
    cache = {}
    reached_by_any = set()
    for start in starts:
        if start["area"] not in cache:
            cache[start["area"]] = reach_from(record, start["area"])
        seen = cache[start["area"]]
        hit = [i for i, p in enumerate(places) if p["area"] in seen]
        reached_by_any.update(hit)
        report["by_start"].append(len(hit) / max(len(places), 1))
    report["never"] = [places[i] for i in range(len(places)) if i not in reached_by_any]
    if report["by_start"]:
        report["worst"] = min(report["by_start"])
        report["mean"] = sum(report["by_start"]) / len(report["by_start"])
    return report


def merge(output, sources):
    records = []
    for source in sources:
        data = Path(source).read_bytes()
        pack = read_pack(data)
        check_crc(data)
        for entry in pack["entries"]:
            records.append((entry["name"].lower(), entry["surfaces"], entry["walkable"], data[entry["offset"]:entry["offset"] + entry["size"]]))
    records.sort(key=lambda item: item[0])
    names = [item[0] for item in records]
    if len(set(names)) != len(names):
        raise PackError("two records for one map: " + ", ".join(sorted({n for n in names if names.count(n) > 1})))
    index_end = HEADER_BYTES + INDEX_BYTES * len(records)
    index = bytearray()
    body = bytearray()
    offset = index_end
    for name, surfaces, walkable, record in records:
        index += name.encode("ascii").ljust(32, b"\0") + struct.pack("<IIII", surfaces, walkable, offset, len(record))
        body += record
        offset += len(record)
    tail = bytes(index) + bytes(body)
    crc = zlib.crc32(tail) & 0xFFFFFFFF
    header = MAGIC + struct.pack("<HHIIIII", PACK_VERSION, NAV_FORMAT, len(records), HEADER_BYTES, crc, 1, 0)
    Path(output).write_bytes(header + tail)
    return len(header + tail), names


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    m = sub.add_parser("merge")
    m.add_argument("output")
    m.add_argument("sources", nargs="+")
    v = sub.add_parser("verify")
    v.add_argument("pack")
    i = sub.add_parser("info")
    i.add_argument("pack")
    i.add_argument("--reach", action="store_true")
    args = parser.parse_args(argv)
    try:
        if args.command == "merge":
            size, names = merge(args.output, args.sources)
            print(f"{args.output}: {size} bytes, {len(names)} maps: {', '.join(names)}")
            return 0
        data = Path(args.pack).read_bytes()
        pack = read_pack(data)
        check_crc(data)
        if args.command == "verify":
            for entry in pack["entries"]:
                parse_record(data[entry["offset"]:entry["offset"] + entry["size"]])
            print(f"{args.pack}: {len(data)} bytes, crc {pack['crc']:08x}, {len(pack['entries'])} maps, all records parse")
            return 0
        print(f"{args.pack}: {len(data)} bytes, crc {pack['crc']:08x}")
        for entry in pack["entries"]:
            record = parse_record(data[entry["offset"]:entry["offset"] + entry["size"]])
            kinds = {}
            for link in record["links"]:
                kinds[LINK_TYPES[link["type"]]] = kinds.get(LINK_TYPES[link["type"]], 0) + 1
            types = {}
            for place in record["places"]:
                types[PLACE_TYPES.get(place["type"], str(place["type"]))] = types.get(PLACE_TYPES.get(place["type"], str(place["type"])), 0) + 1
            print(f"  {entry['name']}: {entry['size']} bytes, surfaces {entry['surfaces']} ({entry['walkable']} walkable), {len(record['areas'])} areas, "
                  f"{len(record['links'])} links {kinds}, {len(record['scripts'])} scripts, places {types}")
            if args.reach:
                report = reach_report(record)
                if report["starts"]:
                    print(f"    from each of {report['starts']} starts the places reached (of {report['places']}): mean {report['mean']:.1%}, worst "
                          f"{report['worst']:.1%}, never reached by any start: {len(report['never'])}")
                else:
                    print("    no starts")
        return 0
    except (PackError, OSError, struct.error) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
