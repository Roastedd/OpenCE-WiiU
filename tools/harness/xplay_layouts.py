#!/usr/bin/env python3
"""The scalars of the structures the game sends raw, from its own declarations (libclang), for tests/xplay_layouts_*.inc.

Each structure is flattened to its leaves, (offset, width) for every scalar of 2, 4 or 8 bytes (bytes are the same in every
byte order and are left out), with the structure's size. The test (tests/test_xplay_distributed.py) fills every leaf with a
value, runs the send path and expects each value's bytes little-endian at its offset: a scalar the send path does not turn is a
byte-order bug, and a scalar this list does not have is caught by regenerating it (test_layouts_current) and comparing.

Run in the opence-ppc-be image (python3-clang) from the OpenCE root:

    python3 tools/harness/xplay_layouts.py            write tests/xplay_layouts_*.inc
    python3 tools/harness/xplay_layouts.py --check    exit 1 when they are out of date
"""

import argparse
import re
import shlex
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(Path(__file__).resolve().parent))

# (name of the .inc, the translation unit, the structures: a prefix 'distributed_' takes all of that name in the unit's own file)
UNITS = {
    "distributed": ("port/linux/game/network_distributed.c", ["distributed_"]),
    "objects": ("port/linux/game/network_objects.c", ["distributed_"]),
    "damage": ("port/linux/game/network_damage.c", ["distributed_"]),
    "actors": ("port/linux/game/network_actors.c", ["distributed_"]),
    "coop": ("port/linux/game/network_coop.c", ["distributed_", "rasterizer_screen_effect_port_state"]),
    "voice": ("port/linux/game/network_voice.c", ["distributed_"]),
    "chat": ("port/linux/game/network_chat.c", ["distributed_"]),
    "votekick": ("port/linux/game/network_votekick.c", ["distributed_"]),
    "map_vote": ("port/linux/game/network_map_vote.c", ["distributed_"]),
    "shield": ("port/linux/game/network_shield_effects.c", ["distributed_"]),
    "map_share": ("port/linux/game/map_share.c", ["distributed_"]),
    "infection": ("port/linux/game/infection.c", ["distributed_"]),
    "raw": ("port/linux/game/network_wire_order.c", ["network_wire_advertisement", "network_game"]),
}


# (the headers whose structures a unit owns)
HEADERS = {"distributed": ("network_distributed.h",), "infection": ("network_infection.h",)}


def parse_flags():
    import xplay

    class Target:
        big_endian = False
        target_flags = ["--target=i686-linux-gnu", "-m32"]

    flags = xplay._game_flags(Target)
    return [flag for flag in flags if flag not in ("-O1",)]


def flatten(ci, type_, base, path, leaves, problems):
    type_ = type_.get_canonical()
    kind = type_.kind
    if kind == ci.TypeKind.CONSTANTARRAY:
        element = type_.element_type.get_canonical()
        count = type_.element_count
        if element.kind in (ci.TypeKind.RECORD, ci.TypeKind.CONSTANTARRAY):
            for index in range(count):
                flatten(ci, element, base + index * element.get_size(), f"{path}[{index}]", leaves, problems)
        else:
            width = element.get_size()
            if width > 1:
                for index in range(count):
                    leaves.append((base + index * width, width, f"{path}[{index}]"))
        return
    if kind == ci.TypeKind.RECORD:
        declaration = type_.get_declaration()
        if declaration.kind == ci.CursorKind.UNION_DECL:
            # (the member that fills the union, its leaves, when every other member's agree where they overlap: a union of one
            # width, as the vectors' (reals as an array or as x y z) and the statistics' of shorts)
            alternatives = []
            for field in type_.get_fields():
                members = []
                flatten(ci, field.type, base, f"{path}.{field.spelling}", members, problems)
                alternatives.append((field.type.get_canonical().get_size(), members))
            alternatives.sort(key=lambda alternative: -alternative[0])
            if alternatives:
                chosen = alternatives[0][1]
                widths = {offset: width for offset, width, _ in chosen}
                for _, members in alternatives[1:]:
                    for offset, width, _ in members:
                        if widths.get(offset, width) != width:
                            problems.append(f"union at {path} (+{base}) with leaves of different widths at +{offset}")
                leaves.extend(chosen)
            return
        for field in type_.get_fields():
            # (padding is no scalar: the senders clear it and no receiver reads it)
            if re.fullmatch(r"_?pad\d*", field.spelling):
                continue
            if field.is_bitfield():
                problems.append(f"bit field {path}.{field.spelling}")
                continue
            flatten(ci, field.type, base + field.get_field_offsetof() // 8, f"{path}.{field.spelling}" if path else field.spelling,
                    leaves, problems)
        return
    if kind in (ci.TypeKind.POINTER,):
        problems.append(f"pointer at {path}")
        return
    size = type_.get_size()
    if size > 1:
        leaves.append((base, size, path))
    elif size < 0:
        problems.append(f"unknown type {type_.spelling} at {path}")


def generate(name, source, wanted):
    import clang.cindex as ci

    index = ci.Index.create()
    unit = index.parse(str(ROOT / source), args=parse_flags(), options=ci.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES)
    errors = [d for d in unit.diagnostics if d.severity >= ci.Diagnostic.Error]
    if errors:
        raise SystemExit(f"{source}: " + "; ".join(str(e) for e in errors[:5]))
    found = {}
    for cursor in unit.cursor.walk_preorder():
        if cursor.kind == ci.CursorKind.STRUCT_DECL and cursor.is_definition() and cursor.spelling:
            if cursor.spelling.endswith("_message"):
                continue
            if any(cursor.spelling.startswith(prefix) for prefix in wanted) and (
                    cursor.spelling.startswith("distributed_") is False or Path(cursor.location.file.name).name in
                    (Path(source).name, *HEADERS.get(name, ()))):
                found.setdefault(cursor.spelling, cursor)
    lines = [f"/* generated by tools/harness/xplay_layouts.py from {source}: do not edit */"]
    report = []
    names = []
    checks = []
    sizes = {}
    for struct_name in sorted(found):
        leaves, problems = [], []
        flatten(ci, found[struct_name].type, 0, "", leaves, problems)
        leaves.sort()
        merged = []
        for offset, width, path in leaves:
            if merged and merged[-1][1] == width and merged[-1][0] + width * merged[-1][3] == offset:
                merged[-1][3] += 1
            else:
                merged.append([offset, width, path, 1])
        leaves = merged
        size = found[struct_name].type.get_size()
        names.append(struct_name)
        sizes[struct_name] = size
        lines.append(f"static const struct xplay_leaf xplay_leaves_{struct_name}[] = {{")
        for offset, width, path, count in leaves:
            lines.append(f"\t{{ {offset}, {width}, {count} }}, /* {path} */")
        if not leaves:
            lines.append("\t{ 0, 0, 0 },")
        lines.append("};")
        checks.append(f"typedef char xplay_size_{struct_name}[sizeof(struct {struct_name}) == {size} ? 1 : -1];")
        for number, (offset, width, path, count) in enumerate(leaves):
            checks.append(f"typedef char xplay_offset_{struct_name}_{number}[offsetof(struct {struct_name}, {path}) == {offset} "
                          f"? 1 : -1];")
        if problems:
            report.append((struct_name, problems))
    lines.append(f"static const struct xplay_structure xplay_structures_{name}[] = {{")
    for struct_name in names:
        size = sizes[struct_name]
        lines.append(f'\t{{ "{struct_name}", {size}, xplay_leaves_{struct_name}, '
                     f"sizeof(xplay_leaves_{struct_name}) / sizeof(xplay_leaves_{struct_name}[0]) }},")
    lines.append("};")
    lines.append("")
    lines.append("/* what ties the table to the declarations on each leg: the sizes and the offsets of the leaves */")
    lines.append("#ifdef XPLAY_LAYOUT_CHECK")
    lines.extend(checks)
    lines.append("#endif")
    return "\n".join(lines) + "\n", report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("only", nargs="*")
    args = parser.parse_args()
    status = 0
    for name, (source, wanted) in UNITS.items():
        if args.only and name not in args.only:
            continue
        text, report = generate(name, source, wanted)
        path = ROOT / "tools" / "harness" / "tests" / f"xplay_layouts_{name}.inc"
        for struct_name, problems in report:
            for problem in problems:
                print(f"{name}: {struct_name}: {problem}")
        if args.check:
            if not path.exists() or path.read_text() != text:
                print(f"{path.name} is out of date")
                status = 1
        else:
            path.write_text(text)
            print(f"{path.name}: {text.count('xplay_leaves_') // 3} structures")
    return status


if __name__ == "__main__":
    sys.exit(main())
