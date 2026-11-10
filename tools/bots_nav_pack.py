#!/usr/bin/env python3
"""The bots' navigation pack (port/linux/game/bots_nav.[ch]): merges the bake's one-map packs into navs.pack, checks a pack, and reports what
the bake made of each map (counts, which places the links join, from every start).

    tools/bots_nav_pack.py merge OUT.pack MAP.nav [MAP.nav ...] [--only MAP,MAP]   one pack with an index; the maps in name order; a map whose graph reaches too few of
                                                                  the places from the starts is marked FRAGMENTED (the game then keeps the engine's
                                                                  surfaces for it; --no-gate leaves the marks as the bake made them)
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


# a map's graph is marked fragmented (BOTS_NAV_FLAG_FRAGMENTED: the game walks the map by the engine's surfaces) when from the starts it reaches less than this
# share of the places on average, or from the worst start less than the second
GATE_MEAN = 0.85
GATE_WORST = 0.40
FLAG_FRAGMENTED = 1


def fragmented(record, mean_floor=GATE_MEAN, worst_floor=GATE_WORST):
    """The reach report of a record that is too fragmented to walk, else None (a record with no starts is not judged)."""
    report = reach_report(record)
    if not report["starts"]:
        return None
    if report["mean"] < mean_floor or report["worst"] < worst_floor:
        return report
    return None


def merge(output, sources, gate=True, only=None):
    """One pack of the one-map packs. Returns (size, names, marked): marked maps the names of the records marked fragmented to their reach report."""
    records = []
    marked = {}
    for source in sources:
        data = Path(source).read_bytes()
        pack = read_pack(data)
        check_crc(data)
        for entry in pack["entries"]:
            if only is not None and entry["name"].lower() not in only:
                continue
            records.append((entry["name"].lower(), entry["surfaces"], entry["walkable"], data[entry["offset"]:entry["offset"] + entry["size"]]))
    if gate:
        gated = []
        for name, surfaces, walkable, rec in records:
            report = fragmented(parse_record(rec))
            if report is not None:
                flags = struct.unpack_from("<I", rec, 0)[0] | FLAG_FRAGMENTED
                rec = struct.pack("<I", flags) + rec[4:]
                marked[name] = report
            gated.append((name, surfaces, walkable, rec))
        records = gated
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
    return len(header + tail), names, marked


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    m = sub.add_parser("merge")
    m.add_argument("output")
    m.add_argument("sources", nargs="+")
    m.add_argument("--no-gate", action="store_true", help="keep the records' fragmented marks as the bake made them")
    m.add_argument("--only", help="take only the records of these maps (comma separated) from the sources (a pack of several maps)")
    v = sub.add_parser("verify")
    v.add_argument("pack")
    w = sub.add_parser("show", help="the areas within RADIUS of a point, with their links (for finding why two places do not join)")
    w.add_argument("pack")
    w.add_argument("map")
    w.add_argument("x", type=float)
    w.add_argument("y", type=float)
    w.add_argument("z", type=float)
    w.add_argument("--radius", type=float, default=6.0)
    i = sub.add_parser("info")
    i.add_argument("pack")
    i.add_argument("--reach", action="store_true")
    args = parser.parse_args(argv)
    try:
        if args.command == "merge":
            size, names, marked = merge(args.output, args.sources, gate=not args.no_gate, only={n.strip().lower() for n in args.only.split(',')} if args.only else None)
            print(f"{args.output}: {size} bytes, {len(names)} maps: {', '.join(names)}")
            for name, report in sorted(marked.items()):
                print(f"  {name}: marked FRAGMENTED: from the starts the graph reaches {report['mean']:.1%} of the places (worst start {report['worst']:.1%}); the game "
                      f"walks it by the engine's surfaces")
            return 0
        data = Path(args.pack).read_bytes()
        pack = read_pack(data)
        check_crc(data)
        if args.command == "verify":
            for entry in pack["entries"]:
                parse_record(data[entry["offset"]:entry["offset"] + entry["size"]])
            print(f"{args.pack}: {len(data)} bytes, crc {pack['crc']:08x}, {len(pack['entries'])} maps, all records parse")
            return 0
        if args.command == "show":
            entry = next(e for e in pack["entries"] if e["name"] == args.map)
            record = parse_record(data[entry["offset"]:entry["offset"] + entry["size"]])
            for index, area in enumerate(record["areas"]):
                dx, dy, dz = area["p"][0] - args.x, area["p"][1] - args.y, area["p"][2] - args.z
                if (dx * dx + dy * dy) ** 0.5 > args.radius or abs(dz) > 3.0:
                    continue
                print(f"area {index} at ({area['p'][0]:.1f} {area['p'][1]:.1f} {area['p'][2]:.1f}) radius {area['radius']:.1f}, {area['count']} links:")
                for k in range(area["count"]):
                    link = record["links"][area["first"] + k]
                    target = record["areas"][link["to"]]["p"]
                    print(f"    {LINK_TYPES[link['type']]:8s} to {link['to']} ({target[0]:.1f} {target[1]:.1f} {target[2]:.1f}) cost {link['cost']:.1f}s "
                          f"exit ({link['exit'][0]+area['p'][0]:.1f} {link['exit'][1]+area['p'][1]:.1f} {link['exit'][2]+area['p'][2]:.1f})")
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
            print(f"  {entry['name']}: {'FRAGMENTED, ' if record['flags'] & FLAG_FRAGMENTED else ''}{entry['size']} bytes, surfaces {entry['surfaces']} ({entry['walkable']} walkable), {len(record['areas'])} areas, "
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
