#!/usr/bin/env python3
"""The tables port/linux/game/tag_swap.c turns a Custom Edition map's tags to big-endian by:
port/linux/game/tag_swap_layouts.inc, made from the schema files map_convert.py converts the Xbox maps with
(tools/wiiu/map_schema: the big-endian build's tag schemas, schema_dump.c, and the scalar layouts of their structures,
tag_layouts.py), so that the game's swap and the converter's are of the same values.

For each definition of the schemas (a tag's root, a block's element, a structure in one), by its name and size:

- its 2-, 4- and 8-byte values, as runs of one width (map_convert.py's Converter takes the same runs: only those
  inside the definition);
- what its data fields hold, as map_convert.py's DATA_HANDLERS has it (bytes, 16- or 32-bit values, a structure's
  vertices, the scripts' syntax nodes, or what a hook swaps);
- the hook that swaps what follows its element (map_convert.py's HOOKS: an animation's frames, a recorded animation's
  event stream, a model part's indices);
- its block, data and structure fields as the schemas have them, which the game checks its own schemas against before
  it swaps anything, so that tables made from other schemas than the build's are refused, not followed.

And the fields the hooks read, the data array of the scripts' syntax nodes, and the gbxmodel fields that say where a
part's vertices and strip are in the model data.

    python3 tools/wiiu/tag_swap_tables.py            write port/linux/game/tag_swap_layouts.inc
    python3 tools/wiiu/tag_swap_tables.py --check    fail if it is not what the schema files make
"""

import argparse
import gzip
import json
import sys
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SCHEMA = HERE / "map_schema"
OUTPUT = ROOT / "port/linux/game/tag_swap_layouts.inc"

sys.path.insert(0, str(HERE))
import map_convert  # noqa: E402

DATA_KINDS = {"bytes": "_tag_swap_data_bytes", 2: "_tag_swap_data_shorts", 4: "_tag_swap_data_words",
              "hook": "_tag_swap_data_hook", "structure_vertices": "_tag_swap_data_structure_vertices",
              "hs_syntax_data": "_tag_swap_data_hs_syntax_data"}
HOOK_KINDS = {"animation": "_tag_swap_hook_animation", "recorded_animation": "_tag_swap_hook_recorded_animation",
              "model_geometry_part": "_tag_swap_hook_model_geometry_part"}
FIELD_KINDS = {"block": "_tag_swap_field_block", "data": "_tag_swap_field_data", "file_data": "_tag_swap_field_file_data",
               "struct": "_tag_swap_field_struct"}

# the fields the hooks and the model data's swap read: (definition, path, width), and the name the C code knows it by
HOOK_FIELDS = [
    ("animation", "node_count", 2, "ANIMATION_NODE_COUNT"),
    ("animation", "nodes_with_rotation_flags[0]", 4, "ANIMATION_ROTATION_FLAGS"),
    ("animation", "nodes_with_translation_flags[0]", 4, "ANIMATION_TRANSLATION_FLAGS"),
    ("animation", "nodes_with_scale_flags[0]", 4, "ANIMATION_SCALE_FLAGS"),
    ("animation", "frame_count", 2, "ANIMATION_FRAME_COUNT"),
    ("animation", "frame_size", 2, "ANIMATION_FRAME_SIZE"),
    ("animation", "flags", 2, "ANIMATION_FLAGS"),
    ("animation", "compressed_data_offset", 4, "ANIMATION_COMPRESSED_DATA_OFFSET"),
    ("animation", "frame_info.size", 4, "ANIMATION_FRAME_INFO_SIZE"),
    ("animation", "frame_info.address", 4, "ANIMATION_FRAME_INFO_ADDRESS"),
    ("animation", "default_data.size", 4, "ANIMATION_DEFAULT_DATA_SIZE"),
    ("animation", "default_data.address", 4, "ANIMATION_DEFAULT_DATA_ADDRESS"),
    ("animation", "data.size", 4, "ANIMATION_DATA_SIZE"),
    ("animation", "data.address", 4, "ANIMATION_DATA_ADDRESS"),
    ("recorded_animation", "unit_control_data_version", 1, "RECORDED_ANIMATION_VERSION"),
    ("recorded_animation", "event_stream.size", 4, "RECORDED_ANIMATION_EVENT_STREAM_SIZE"),
    ("recorded_animation", "event_stream.address", 4, "RECORDED_ANIMATION_EVENT_STREAM_ADDRESS"),
    ("model_geometry_part", "triangle_buffer.type", 2, "MODEL_PART_TRIANGLE_BUFFER_TYPE"),
    ("model_geometry_part", "triangle_buffer.count", 4, "MODEL_PART_TRIANGLE_BUFFER_COUNT"),
    ("model_geometry_part", "triangle_buffer.hardware_format", 4, "MODEL_PART_TRIANGLE_BUFFER_HARDWARE_FORMAT"),
    ("structure_material", "vertices.count", 4, "STRUCTURE_MATERIAL_VERTEX_COUNT"),
    ("structure_material", "lightmap_vertices.count", 4, "STRUCTURE_MATERIAL_LIGHTMAP_VERTEX_COUNT"),
    ("gbxmodel_geometry_part", "strip_triangle_count", 4, "GBXMODEL_PART_STRIP_TRIANGLE_COUNT"),
    ("gbxmodel_geometry_part", "strip_offset", 4, "GBXMODEL_PART_STRIP_OFFSET"),
    ("gbxmodel_geometry_part", "vertex_count", 4, "GBXMODEL_PART_VERTEX_COUNT"),
    ("gbxmodel_geometry_part", "vertex_offset", 4, "GBXMODEL_PART_VERTEX_OFFSET"),
]
# the blocks the model data's swap walks to a gbxmodel's parts: (definition, field, the C name)
BLOCK_FIELDS = [
    ("gbxmodel", "geometries", "GBXMODEL_GEOMETRIES"),
    ("gbxmodel_geometry", "parts", "GBXMODEL_GEOMETRY_PARTS"),
]


def schema_text(name: str) -> str:
    return gzip.decompress((SCHEMA / f"{name}.gz").read_bytes()).decode("utf-8")


def converter():
    """map_convert.py's Converter of the schema files, as convert_maps.py makes it: its definitions' runs and fields"""
    symbols = defaultdict(list)
    for line in schema_text("schema_dump.nm").splitlines():
        parts = line.split()
        if len(parts) >= 3:
            symbols[int(parts[0], 16)].append(parts[2])
    schema = json.loads(schema_text("tag_schema.json"))
    layouts = json.loads(schema_text("tag_layouts.json"))
    if "custom_edition_groups" not in schema:
        raise SystemExit("tools/wiiu/map_schema/tag_schema.json has no custom_edition_groups: "
                         "dump it with tools/wiiu/schema_dump.c")
    return schema, layouts, map_convert.Converter(schema, layouts, symbols)


def c_runs(runs):
    return [(offset, width, count) for offset, width, count in runs]


def generate() -> str:
    schema, layouts, convert = converter()
    definitions = schema["definitions"]
    by_key = {}
    for definition in definitions:
        index = definition["index"]
        key = (definition["name"], definition["size"])
        fields = [(FIELD_KINDS[field["kind"]], field["offset"], field["count"], field["size"], field["name"])
                  for field in definition["fields"]]
        handlers = []
        for field in definition["fields"]:
            if field["kind"] != "data":
                continue
            handler = map_convert.DATA_HANDLERS.get(f"{definition['name']}.{field['name']}")
            if handler is not None:
                handlers.append((field["offset"], DATA_KINDS[handler], field["name"]))
        entry = {"runs": c_runs(convert.runs[index]), "fields": fields, "handlers": handlers,
                 "pointers": sorted(set(convert.pointer_offsets(index))),
                 "hook": HOOK_KINDS.get(definition["name"], "_tag_swap_hook_none")}
        if key in by_key and by_key[key] != entry:
            raise SystemExit(f"two definitions {key} differ: the game could not tell them apart")
        by_key[key] = entry
    # (the game finds a definition by its name, then its size)
    keys = sorted(by_key, key=lambda key: (key[0].encode("ascii"), key[1]))
    fields_of = {}
    for definition in definitions:
        fields_of.setdefault(definition["name"], convert.fields[definition["index"]])

    lines = [
        "/*",
        "TAG_SWAP_LAYOUTS.INC",
        "",
        "Made by tools/wiiu/tag_swap_tables.py from tools/wiiu/map_schema (the big-endian",
        "build's tag schemas and its structures' scalar layouts, as map_convert.py converts",
        "the Xbox maps with them): run it again when they change, rather than editing this.",
        "*/",
        "",
        "/* each definition's 2-, 4- and 8-byte values, as runs of one width: offset,",
        "width, count */",
        "static struct tag_swap_run const tag_swap_runs[] =",
        "{",
    ]
    run_first = {}
    run_index = 0
    for key in keys:
        entry = by_key[key]
        run_first[key] = run_index
        if entry["runs"]:
            lines.append(f"\t/* {key[0]} ({key[1]} bytes) */")
        for offset, width, count in entry["runs"]:
            lines.append(f"\t{{ 0x{offset:04X}, {width}, {count} }},")
            run_index += 1
    if not run_index:
        lines.append("\t{ 0, 0, 0 },")
    lines += ["};", "",
              "/* each definition's block, data and structure fields as the schemas have them:",
              "kind, offset, count, stride (the game's schema must be the same) */",
              "static struct tag_swap_field const tag_swap_fields[] =", "{"]
    field_first = {}
    field_index = 0
    for key in keys:
        field_first[key] = field_index
        for kind, offset, count, size, name in by_key[key]["fields"]:
            lines.append(f"\t{{ {kind}, 0x{offset:04X}, {count}, {size} }}, /* {key[0]}.{name} */")
            field_index += 1
    lines += ["};", "",
              "/* what each definition's data fields hold, by their offset (map_convert.py's",
              "DATA_HANDLERS; a data field not here is left as it is) */",
              "static struct tag_swap_data_handler const tag_swap_data_handlers[] =", "{"]
    handler_first = {}
    handler_index = 0
    for key in keys:
        handler_first[key] = handler_index
        for offset, kind, name in by_key[key]["handlers"]:
            lines.append(f"\t{{ 0x{offset:04X}, {kind} }}, /* {key[0]}.{name} */")
            handler_index += 1
    lines += ["};", "", "/* pointer fields from the same layouts as map_convert.py */",
              "static unsigned short const tag_swap_pointer_offsets[] =", "{"]
    pointer_first = {}
    pointer_index = 0
    for key in keys:
        pointer_first[key] = pointer_index
        for offset in by_key[key]["pointers"]:
            lines.append(f"\t0x{offset:04X}, /* {key[0]} */")
            pointer_index += 1
    lines += ["};", "",
              "/* the definitions, by name and then size: their runs, fields and data",
              "handlers (first, count) and their hook */",
              "static struct tag_swap_layout const tag_swap_layouts[] =", "{"]
    for key in keys:
        entry = by_key[key]
        lines.append(f"\t{{ \"{key[0]}\", 0x{key[1]:X}, {run_first[key]}, {len(entry['runs'])}, "
                     f"{field_first[key]}, {len(entry['fields'])}, {handler_first[key]}, {len(entry['handlers'])}, "
                     f"{pointer_first[key]}, {len(entry['pointers'])}, {entry['hook']} }},")
    lines += ["};", ""]

    lines += ["/* the fields the hooks and the model data's swap read, and their widths */"]
    for definition_name, path, width, name in HOOK_FIELDS:
        found = fields_of.get(definition_name, {}).get(path)
        if not found or found[1] != width:
            raise SystemExit(f"{definition_name}.{path} is not a {width}-byte field of the layouts ({found})")
        lines.append(f"#define TAG_SWAP_{name}_OFFSET 0x{found[0]:02X}")
    model = next(d for d in definitions if d["name"] == "gbxmodel")
    lines.append(f"#define TAG_SWAP_GBXMODEL_BYTES 0x{model['size']:02X}")
    for definition_name, field_name, name in BLOCK_FIELDS:
        definition = next(d for d in definitions if d["name"] == definition_name)
        field = next(f for f in definition["fields"] if f["name"] == field_name and f["kind"] == "block")
        child = definitions[field["child"]]
        lines.append(f"#define TAG_SWAP_{name}_OFFSET 0x{field['offset']:02X}")
        lines.append(f"#define TAG_SWAP_{name}_ELEMENT_BYTES 0x{child['size']:02X}")
    lines.append("")

    # the scripts' syntax nodes: a data array (data.h) whose elements are hs_syntax_node
    extras = layouts["extras"]
    for name in ("data_array", "hs_syntax_node"):
        runs = extras[name]["runs"]
        if any(width < 1 or width not in (1, 2, 4, 8) for _, width, _ in runs):
            raise SystemExit(f"the layout of {name} has a value of no single width: {runs}")
    header = extras["data_array"]
    node = extras["hs_syntax_node"]
    maximum_count, maximum_count_width = header["fields"]["maximum_count"]
    element_size, element_size_width = header["fields"]["size"]
    if maximum_count_width != 2 or element_size_width != 2 or element_size != maximum_count + 2:
        raise SystemExit("data_array's maximum_count and size are not two shorts")
    lines += ["/* a data array kept in a tag's data (data.h's struct data_array, then its",
              "elements): its header's values (one-byte ones too: they are marked, not turned)",
              "and the scripts' syntax nodes' */",
              f"#define TAG_SWAP_DATA_ARRAY_BYTES 0x{header['size']:02X}",
              f"#define TAG_SWAP_DATA_ARRAY_MAXIMUM_COUNT_OFFSET 0x{maximum_count:02X}",
              f"#define TAG_SWAP_DATA_ARRAY_ELEMENT_SIZE_OFFSET 0x{element_size:02X}",
              f"#define TAG_SWAP_HS_SYNTAX_NODE_BYTES 0x{node['size']:02X}",
              "static struct tag_swap_run const tag_swap_data_array_runs[] =", "{"]
    lines += [f"\t{{ 0x{offset:02X}, {width}, {count} }}," for offset, width, count in header["runs"]]
    lines += ["};", "static struct tag_swap_run const tag_swap_hs_syntax_node_runs[] =", "{"]
    lines += [f"\t{{ 0x{offset:02X}, {width}, {count} }}," for offset, width, count in node["runs"]]
    lines += ["};", ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="fail if the tables are not what the schema files make")
    args = parser.parse_args()
    text = generate()
    if args.check:
        if not OUTPUT.is_file() or OUTPUT.read_text() != text:
            print(f"{OUTPUT.relative_to(ROOT)} is not what tools/wiiu/map_schema makes: "
                  "run tools/wiiu/tag_swap_tables.py", file=sys.stderr)
            return 1
        print(f"{OUTPUT.relative_to(ROOT)}: as the schema files make it")
        return 0
    OUTPUT.write_text(text)
    print(f"wrote {OUTPUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
