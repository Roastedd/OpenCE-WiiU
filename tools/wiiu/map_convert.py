#!/usr/bin/env python3
"""An Xbox Halo map (little-endian) rewritten in big-endian byte order, for the
big-endian build (configure.py --linux-target=ppc) and the Wii U.

The map's body is inflated, every multi-byte value of its header, tag header,
tag table, buffers and tags is swapped, and it is deflated again, so that the
game reads it as it reads an Xbox map. What a value is comes from the game:

- tag_schema.json (schema_dump, run on the compiled schemas): each group's root
  definition and each definition's block, data and structure fields, the
  walk the validator makes (port/linux/game/tag_validate.c);
- tag_layouts.json (tag_layouts.py, libclang on the game's declarations): each
  definition's 2-, 4- and 8-byte scalars, the widths a swap needs.

Maps are untrusted input: every block and data is checked to lie in the map
before it is followed, and no byte is swapped twice. What the converter does
not know how to swap (a data field without a handler, a group without a
schema) is left as it is and reported.

    map_convert.py --schema tag_schema.json --layouts tag_layouts.json --symbols schema_dump.nm in.map out.map
"""

import argparse
import array
import json
import struct
import sys
import zlib
from collections import Counter, defaultdict
from pathlib import Path

HEADER_SIZE = 0x800
TAG_CACHE_BASE_ADDRESS = 0x803A6000
TAG_CACHE_SIZE = 0x1600000
SCENARIO_STRUCTURE_BSPS_OFFSET = 0x5A4
BUFFER_SIZE = 12

# the cache file header (cache_files_windows.c): its words and its one short
HEADER_WORDS = (0x00, 0x04, 0x08, 0x10, 0x14, 0x64, 0x7FC)
HEADER_SHORTS = (0x60,)

# how a data field's bytes are swapped: by "definition.field" (the schema's
# names). Bytes need nothing; text and arrays of 2- or 4-byte values swap as
# such. Anything else is left and reported.
DATA_HANDLERS = {
    "scenario.hs_string_constants": "bytes",
    "hs_source_file.source": "bytes",
    "sound_permutation.mouth_data": "bytes",
    "font.pixels": "bytes",
    "hud_message_text.text_data": 2,
    "sound_permutation.subtitle_data": 2,
    # (bit vectors of longs: the clusters each cluster can see)
    "structure_bsp.cluster_data": 4,
    # (a byte for each pair of clusters: its encoded sound distance)
    "structure_bsp.sound_cluster_data": "bytes",
    # (a material's vertices, then its lightmap vertices: structure_vertices)
    "structure_material.compressed_vertex_data": "structure_vertices",
    "string_list_entry.string": "bytes",
    "unicode_string_list_entry.string": 2,
    "scenario.hs_syntax_data": "hs_syntax_data",
    # (by the animation hook, which knows their nodes)
    "animation.data": "hook",
    "animation.default_data": "hook",
    "animation.frame_info": "hook",
    # (by the recorded animation hook, which walks it)
    "recorded_animation.event_stream": "hook",
}


def field(converter, map_: "Map", index: int, offset: int, path: str) -> int:
    """a swapped element's field by its path (tag_layouts.json), as a native number"""
    field_offset, width = converter.fields[index][path]
    value = int.from_bytes(map_.data[offset + field_offset:offset + field_offset + width], "big")
    if path.endswith(".type") and value >= 0x8000:
        value -= 0x10000
    return value


def model_geometry_part(converter, map_: "Map", index: int, offset: int):
    """a model part's triangles' indices (16-bit, in the index buffer its triangle buffer names), which the
    CPU checks and the Wii U's GPU reads big-endian"""
    type_ = field(converter, map_, index, offset, "triangle_buffer.type")
    count = field(converter, map_, index, offset, "triangle_buffer.count")
    descriptor = field(converter, map_, index, offset, "triangle_buffer.hardware_format")
    index_count = 3 * count if type_ == 0 else (count + 2 if type_ == 1 and count > 0 else 0)
    if not descriptor or index_count <= 0:
        return
    descriptor_offset = map_.region_offset(descriptor, BUFFER_SIZE)
    if descriptor_offset is None:
        return
    data = int.from_bytes(map_.data[descriptor_offset + 4:descriptor_offset + 8], "big")
    start = map_.region_offset(data, index_count * 2)
    if start is None:
        map_.unhandled["model_geometry_part indices outside the tags"] += 1
        return
    map_.swap_shared(start, 2, index_count)


def flag_count(converter, map_, index, offset, name, node_count):
    """how many of the first node_count nodes have their bit set in the animation's flag vector name"""
    words = [field(converter, map_, index, offset, f"{name}[{n}]") for n in range(2)]
    return sum(1 for node in range(node_count) if words[node >> 5] >> (node & 31) & 1)


def animation(converter, map_: "Map", index: int, offset: int):
    """an animation's frames, defaults and frame info (model_animations.c's animation_get_node_orientations
    and the compressed readers; tag_schema_models.c's animation_data_problem): node by node, rotations as
    16-bit words (8 bytes, or 6 compressed), translations and scales as floats, the compressed data's
    offsets and node headers as 32-bit words and its frame indices as 16-bit words"""
    value = lambda path: field(converter, map_, index, offset, path)
    node_count = value("node_count")
    if node_count >= 0x8000:
        node_count -= 0x10000
    if not 0 <= node_count <= 64:
        return
    rotation = [value(f"nodes_with_rotation_flags[{n}]") for n in range(2)]
    translation = [value(f"nodes_with_translation_flags[{n}]") for n in range(2)]
    scale = [value(f"nodes_with_scale_flags[{n}]") for n in range(2)]
    has = lambda flags, node: flags[node >> 5] >> (node & 31) & 1
    rotation_count = sum(has(rotation, node) for node in range(node_count))
    translation_count = sum(has(translation, node) for node in range(node_count))
    scale_count = sum(has(scale, node) for node in range(node_count))
    frame_count = value("frame_count")
    frame_size = value("frame_size")
    compressed = value("flags") & 1

    def blob(name):
        size, address = value(f"{name}.size"), value(f"{name}.address")
        if not size or not address:
            return None, 0
        start = map_.region_offset(address, size)
        if start is None:
            raise Refused(f"animation.{name} at {address:#x} is outside the tags")
        return start, size

    def frame(start, wanted):
        """one frame (or the defaults: wanted is whether a node's value is in it, by flag)"""
        at = start
        for node in range(node_count):
            if wanted(rotation, node):
                map_.swap(at, 2, 4)
                at += 8
            if wanted(translation, node):
                map_.swap(at, 4, 3)
                at += 12
            if wanted(scale, node):
                map_.swap(at, 4, 1)
                at += 4
        return at - start

    # (frame info: per frame dx, dy and perhaps dz and dyaw, all floats)
    start, size = blob("frame_info")
    if start is not None:
        map_.swap(start, 4, size // 4)
        map_.swap(start + size // 4 * 4, 1, size % 4)

    start, size = blob("default_data")
    if start is not None:
        default_size = (node_count - rotation_count) * 8 + (node_count - translation_count) * 12 + \
            (node_count - scale_count) * 4
        if default_size <= size:
            frame(start, lambda flags, node: not has(flags, node))
        if default_size != size:
            map_.unhandled["animation.default_data not its nodes' size"] += size

    start, size = blob("data")
    if start is None:
        return
    expected = rotation_count * 8 + translation_count * 12 + scale_count * 4
    uncompressed_end = 0
    if frame_size == expected and frame_size and (not compressed or frame_count * frame_size <= value("compressed_data_offset")):
        if frame_count * frame_size <= size:
            for frame_index in range(frame_count):
                frame(start + frame_index * frame_size, has)
            uncompressed_end = frame_count * frame_size
    if not compressed:
        if uncompressed_end != size:
            map_.unhandled["animation.data not its frames' size"] += size - uncompressed_end
        return
    base_offset = value("compressed_data_offset")
    if base_offset < uncompressed_end or base_offset + 0x2C > size:
        map_.unhandled["animation.data's compressed header outside it"] += size
        return
    base = start + base_offset
    available = size - base_offset
    (rotation_frames, default_rotations, rotation_keyframes, translation_headers, translation_frames,
     default_translations, translation_keyframes, scale_headers, scale_frames, default_scales,
     scale_keyframes) = struct.unpack_from("<11i", map_.data, base)
    map_.swap(base, 4, 11)

    def inside(relative, count, width):
        return 0 <= relative and count >= 0 and relative + count * width <= available

    def keyframes(headers_relative, count, keyframes_relative, frames_relative, keyframe_width, keyframe_words):
        if not inside(headers_relative, count, 4):
            raise Refused("an animation's compressed node headers are outside it")
        headers = struct.unpack_from(f"<{count}I", map_.data, base + headers_relative)
        map_.swap(base + headers_relative, 4, count)
        total = max([(h >> 12) + (h & 0xFFF) for h in headers if h & 0xFFF] or [0])
        if total:
            if not inside(keyframes_relative, total, keyframe_width) or not inside(frames_relative, total, 2):
                raise Refused("an animation's compressed keyframes are outside it")
            map_.swap(base + keyframes_relative, keyframe_width // keyframe_words, total * keyframe_words)
            map_.swap(base + frames_relative, 2, total)

    keyframes(0x2C, rotation_count, rotation_keyframes, rotation_frames, 6, 3)
    keyframes(translation_headers, translation_count, translation_keyframes, translation_frames, 12, 3)
    keyframes(scale_headers, scale_count, scale_keyframes, scale_frames, 4, 1)
    for relative, count, width, words in ((default_rotations, node_count, 6, 3),
                                          (default_translations, node_count, 12, 3),
                                          (default_scales, scale_count, 4, 1)):
        if count and inside(relative, count, width):
            map_.swap_unless_swapped(base + relative, width // words, count * words)


# A recorded animation's event stream (source/cutscene/recorded_animation_playback.c and
# recorded_animation_initialize.c): the unit control's fields by version, the playback state, then
# events, each a header byte (the event type in its upper six bits, a time delta in its lower two:
# 0 and 1 are themselves, 2 is a byte after the header, 3 a 16-bit word) and the event's data.
UNIT_CONTROL_FIELDS = (
    # version 0: two bytes, three shorts (the third is skipped by the game but is in the stream), a
    # real_vector2d and three real_vector3d; versions 1, 2 and 3 add a long, a short and a short
    ((1, 1), (1, 1), (2, 1), (2, 1), (2, 1), (4, 2), (4, 3), (4, 3), (4, 3)),
    ((4, 1),),
    ((2, 1),),
    ((2, 1),),
)
PLAYBACK_STATE_SHORTS = 6  # animation_playback_controller: three direction controllers of two shorts
EVENT_END = 1
# by event type: (data bytes, the data's (width, count) values to swap)
EVENT_DATA = {0: (0, ()), 1: (0, ()), 2: (1, ()), 3: (1, ()), 4: (2, ((2, 1),)), 5: (2, ((2, 1),)),
              6: (8, ((4, 2),))}
EVENT_DATA.update({kind: (2, ()) for kind in range(7, 15)})
EVENT_DATA.update({kind: (4, ((2, 2),)) for kind in range(15, 23)})


def recorded_animation(converter, map_: "Map", index: int, offset: int):
    """a recorded animation's event stream, walked as the game plays it, every value of 2 bytes or more
    swapped; a stream that does not reach its end event is left from where it fails, and said"""
    value = lambda path: field(converter, map_, index, offset, path)
    size, address = value("event_stream.size"), value("event_stream.address")
    if not size or not address:
        return
    start = map_.region_offset(address, size)
    if start is None:
        raise Refused(f"recorded_animation.event_stream at {address:#x} is outside the tags")
    versions = min(max(value("unit_control_data_version"), 1), len(UNIT_CONTROL_FIELDS))
    position = 0

    def take(width, count=1):
        nonlocal position
        if position + width * count > size:
            raise ValueError("the stream ends inside a value")
        map_.swap(start + position, width, count) if width > 1 else None
        position += width * count

    try:
        for version in range(versions):
            for width, count in UNIT_CONTROL_FIELDS[version]:
                take(width, count)
        take(2, PLAYBACK_STATE_SHORTS)
        events = 0
        while True:
            if position >= size:
                raise ValueError("the stream has no end event")
            header = map_.data[start + position]
            delta, kind = header & 3, header >> 2
            position += 1
            if delta == 2:
                position += 1
            elif delta == 3:
                take(2)
            if kind == EVENT_END:
                map_.counts["streams"] += 1
                map_.counts["stream events"] += events
                return
            if kind not in EVENT_DATA:
                raise ValueError(f"event type {kind} is not one the game has")
            length, values = EVENT_DATA[kind]
            if position + length > size:
                raise ValueError("the stream ends inside an event")
            for width, count in values:
                take(width, count)
            position += length - sum(width * count for width, count in values)
            events += 1
    except ValueError as problem:
        map_.unhandled[f"recorded_animation.event_stream not walked to its end ({problem})"] += size - position


# Scalars that are addresses in the tags, which the schema does not type (it knows blocks, data and structures), by the last name of their
# path (tag_layouts.json): a tag reference's name (a reference has name and name_length), a buffer's base address and hardware format, and
# the address of a block or data. The pointer audit (--audit-pointers) lists the words in range that neither this nor the walk names.
POINTER_NAMES = ("base_address", "hardware_format", "address")

# what is done after an element of a definition (by the schema's name) is swapped
HOOKS = {
    "recorded_animation": recorded_animation,
    "model_geometry_part": model_geometry_part,
    "animation": animation,
}


class Refused(Exception):
    pass


class Map:
    def __init__(self, data: bytearray):
        self.data = data
        self.swapped = bytearray(len(data))
        self.regions = []  # (address, size, file offset): where tags and bsps are loaded
        self.unhandled = Counter()
        self.shared = set()
        # (bytes of known one-byte values, and the coverage audit: words no layout covers, by definition and
        # offset, when asked for)
        self.known = bytearray(len(data))
        self.audit = None
        self.counts = Counter()
        # the file offsets of the words that are addresses in the tags (or in a structure bsp), to be moved by a rebase;
        # and, for the pointer audit, the elements walked (offset, size, definition)
        self.pointers = set()
        self.elements = None

    def audit_element(self, name: str, offset: int, size: int):
        """the 4-byte words of an element neither swapped nor a known byte: what no declaration names"""
        for relative in range(0, size - 3, 4):
            at = offset + relative
            if any(self.swapped[at:at + 4]) or any(self.known[at:at + 4]):
                continue
            word = bytes(self.data[at:at + 4])
            entry = self.audit.setdefault((name, relative), Counter())
            entry["seen"] += 1
            if word == b"\0\0\0\0":
                continue
            entry["nonzero"] += 1
            value = struct.unpack("<f", word)[0]
            integer = struct.unpack("<I", word)[0]
            if value == value and 1e-6 < abs(value) < 1e7:
                entry["float"] += 1
            elif integer < 0x10000 or integer >= 0xFFFF0000:
                entry["small"] += 1
            if len(entry) < 12:
                entry[f"e.g. {word.hex()}"] += 1

    def in_region(self, address: int) -> bool:
        """whether address is inside a region the map loads"""
        return any(start <= address < start + length for start, length, _ in self.regions)

    def pointer(self, offset: int):
        """the word at offset is an address (when it is not null and in a region): a rebase moves it"""
        if offset < 0 or offset + 4 > len(self.data):
            return
        address = int.from_bytes(self.data[offset:offset + 4], "big" if self.swapped[offset] else "little")
        if address and self.in_region(address):
            self.pointers.add(offset)

    def region_offset(self, address: int, size: int):
        """the file offset of size bytes at address, or None"""
        for start, length, offset in self.regions:
            if start <= address and address + size <= start + length:
                return offset + (address - start)
        return None

    def u32(self, offset: int, swapped: bool = False) -> int:
        """the word at offset, as the file has it, or as it is once swapped"""
        return struct.unpack_from(">I" if swapped else "<I", self.data, offset)[0]

    def swap_unless_swapped(self, offset: int, width: int, count: int):
        """values of a structure its parent may have swapped already: each one the parent did not"""
        for index in range(count):
            at = offset + index * width
            marks = self.swapped[at:at + width]
            if all(marks):
                continue
            if any(marks):
                raise Refused(f"the value at {at:#x} is half swapped")
            self.swap(at, width)

    def swap_shared(self, offset: int, width: int, count: int):
        """values several elements may name (a buffer's): swapped the first time, then left"""
        if (offset, width, count) in self.shared:
            return
        self.shared.add((offset, width, count))
        self.swap(offset, width, count)

    def swap(self, offset: int, width: int, count: int = 1):
        end = offset + width * count
        if offset < 0 or end > len(self.data):
            raise Refused(f"a swap at {offset:#x} is outside the map")
        if any(self.swapped[offset:end]):
            raise Refused(f"the bytes at {offset:#x} would be swapped twice")
        self.swapped[offset:end] = b"\1" * (end - offset)
        if width == 1:
            return
        values = array.array({2: "H", 4: "I", 8: "Q"}[width], self.data[offset:end])
        values.byteswap()
        self.data[offset:end] = values.tobytes()
        self.counts[width] += count


class Converter:
    def __init__(self, schema: dict, layouts: dict, symbols: dict):
        self.definitions = schema["definitions"]
        self.groups = {g["tag"]: g["definition"] for g in schema["groups"]}
        self.runs = {}
        self.pointer_cache = {}
        self.bytes = {}
        self.fields = {}
        self.extras = layouts.get("extras", {})
        by_variable = defaultdict(list)
        for key, layout in layouts["layouts"].items():
            by_variable[layout["variable"]].append(layout)
        for definition in self.definitions:
            names = symbols.get(definition["address"], [])
            candidates = [l for name in names for l in by_variable.get(name, []) if l["size"] >= definition["size"]]
            exact = [l for l in candidates if l["size"] == definition["size"]]
            chosen = exact or candidates
            # (a definition written alike in several files: any of its layouts)
            if len({json.dumps(l["runs"]) for l in chosen}) == 1:
                chosen = chosen[:1]
            if len(chosen) != 1:
                raise SystemExit(f"definition {definition['name']} ({names}, {definition['size']} bytes): "
                                 f"{len(chosen)} layouts match")
            # (a definition covering the start of its structure keeps the leaves inside its size)
            self.runs[definition["index"]] = [(o, w, c) for o, w, c in chosen[0]["runs"]
                                              if w > 1 and o + w * c <= definition["size"]]
            # (its one-byte values, for the coverage audit)
            self.bytes[definition["index"]] = [(o, c) for o, w, c in chosen[0]["runs"]
                                               if w == 1 and o + c <= definition["size"]]
            self.fields[definition["index"]] = chosen[0]["fields"]

    def pointer_offsets(self, index: int):
        """the offsets, in an element of definition index, of the words that are addresses in the tags"""
        cached = self.pointer_cache.get(index)
        if cached is None:
            fields = self.fields[index]
            cached = []
            for path, (field_offset, width) in fields.items():
                last = path.rsplit(".", 1)[-1]
                if width != 4:
                    continue
                if last in POINTER_NAMES or (last == "name" and "." in path and path[:-4] + "name_length" in fields):
                    cached.append(field_offset)
            self.pointer_cache[index] = cached
        return cached

    def element(self, map_: Map, index: int, offset: int, swap: bool, depth: int = 0):
        """the element of definition index at offset: its scalars swapped (unless a parent's swap already
        covered them), then its blocks, data and structures followed"""
        if depth > 32:
            raise Refused("blocks nest too deep")
        definition = self.definitions[index]
        if offset < 0 or offset + definition["size"] > len(map_.data):
            raise Refused(f"{definition['name']} at {offset:#x} is outside the map")
        # (what to follow, read before the swap turns it: each word as its bytes are now, swapped or not. A
        # structure inside an element its parent swapped may have been declared by the parent as bytes,
        # which the schema reads as a structure of its own: its leaves the parent did not swap are)
        follow = []
        for field in definition["fields"]:
            at = offset + field["offset"]
            if field["kind"] == "block":
                follow.append(("block", field, map_.u32(at, map_.swapped[at]), map_.u32(at + 4, map_.swapped[at + 4])))
            elif field["kind"] == "data":
                follow.append(("data", field, map_.u32(at, map_.swapped[at]), map_.u32(at + 12, map_.swapped[at + 12])))
            elif field["kind"] == "struct":
                follow.append(("struct", field, at, None))
        for run_offset, width, count in self.runs[index]:
            if swap:
                map_.swap(offset + run_offset, width, count)
            else:
                map_.swap_unless_swapped(offset + run_offset, width, count)
        for run_offset, count in self.bytes[index]:
            map_.known[offset + run_offset:offset + run_offset + count] = b"\1" * count
        hook = HOOKS.get(definition["name"])
        if hook:
            hook(self, map_, index, offset)
        audit = swap and map_.audit is not None
        if map_.elements is not None:
            map_.elements.append((offset, definition["size"], definition["name"]))
        for pointer_offset in self.pointer_offsets(index):
            map_.pointer(offset + pointer_offset)
        for kind, field, a, b in follow:
            if kind == "block":
                map_.pointer(offset + field["offset"] + 4)
            elif kind == "data":
                map_.pointer(offset + field["offset"] + 12)
            if kind == "block":
                count, address = a, b
                if not count:
                    continue
                child = self.definitions[field["child"]]
                stride = child["size"]
                if count < 0 or count > TAG_CACHE_SIZE // max(stride, 1):
                    raise Refused(f"{definition['name']}.{field['name']} has {count} elements")
                start = map_.region_offset(address, count * stride)
                if start is None:
                    raise Refused(f"{definition['name']}.{field['name']} at {address:#x} is outside the tags")
                for element in range(count):
                    self.element(map_, field["child"], start + element * stride, True, depth + 1)
            elif kind == "struct":
                for element in range(field["count"]):
                    self.element(map_, field["child"], a + element * field["size"], False, depth + 1)
            else:
                size, address = a, b
                if not size or not address:
                    continue
                name = f"{definition['name']}.{field['name']}"
                start = map_.region_offset(address, size)
                if start is None:
                    raise Refused(f"{name} at {address:#x} is outside the tags")
                handler = DATA_HANDLERS.get(name)
                if handler == "bytes":
                    map_.swap(start, 1, size)
                elif handler in (2, 4):
                    map_.swap(start, handler, size // handler)
                elif handler == "hook":
                    pass
                elif handler == "structure_vertices":
                    self.structure_vertices(map_, index, offset, start, size, name)
                elif handler == "hs_syntax_data":
                    self.data_array(map_, start, size, "hs_syntax_node", name)
                else:
                    map_.unhandled[name] += size
        if audit:
            map_.audit_element(definition["name"], offset, definition["size"])

    def structure_vertices(self, map_: Map, index: int, offset: int, start: int, size: int, name: str):
        """a structure material's compressed vertices (objects/object_lights.c's
        environment_vertex_compressed and environment_lightmap_vertex_compressed, which the lightmap
        sampling reads): 32 bytes each of 8 words (position, three packed vectors, texture coordinates),
        then the lightmap vertices, 8 bytes each of a word and two 16-bit values"""
        vertices = field(self, map_, index, offset, "vertices.count")
        lightmap_vertices = field(self, map_, index, offset, "lightmap_vertices.count")
        if vertices < 0 or lightmap_vertices < 0 or vertices * 32 + lightmap_vertices * 8 != size:
            map_.unhandled[f"{name} (not {vertices} vertices and {lightmap_vertices} lightmap vertices)"] += size
            return
        map_.swap(start, 4, vertices * 8)
        base = start + vertices * 32
        for vertex in range(lightmap_vertices):
            map_.swap(base + vertex * 8, 4, 1)
            map_.swap(base + vertex * 8 + 4, 2, 2)

    def data_array(self, map_: Map, start: int, size: int, element_type: str, name: str):
        """a data array kept in a tag's data (data.h's struct data_array, then its elements): its header,
        then each of its maximum_count elements of size bytes as element_type"""
        header = self.extras["data_array"]
        element = self.extras[element_type]
        maximum_count, element_size = struct.unpack_from("<hh", map_.data, start + header["fields"]["maximum_count"][0])
        if maximum_count < 0 or element_size != element["size"] or \
                header["size"] + maximum_count * element_size > size:
            raise Refused(f"{name} is not a data array of {element_type}")
        for run_offset, width, count in header["runs"]:
            map_.swap(start + run_offset, width, count)
        for index in range(maximum_count):
            base = start + header["size"] + index * element_size
            for run_offset, width, count in element["runs"]:
                map_.swap(base + run_offset, width, count)
        # (what follows the elements, if anything, is left)
        rest = size - header["size"] - maximum_count * element_size
        if rest:
            map_.unhandled[f"{name} past its elements"] += rest

    def tag(self, map_: Map, group: str, offset: int):
        index = self.groups.get(group)
        if index is None or index < 0:
            map_.unhandled[f"group {group}"] += 1
            return
        self.element(map_, index, offset, True)

    def convert(self, data: bytearray, audit: bool = False, rebase: int = None, audit_pointers: bool = False) -> Map:
        """the map in big-endian. rebase: the number to add to every address in the map, so that its window is elsewhere than
        0x80000000 (the Wii U's: halo_port_capacity.h); audit_pointers: also list the words in range that were not moved"""
        map_ = Map(data)
        if audit:
            map_.audit = {}
        if audit_pointers:
            map_.elements = []
        header = struct.unpack_from("<IiiIii", data, 0)
        signature, version, file_length, _, tag_data_offset, tag_data_size = header
        if signature != 0x68656164 or len(data) != file_length:
            raise Refused("not an inflated Xbox map")
        if tag_data_offset < HEADER_SIZE or tag_data_size <= 0 or tag_data_offset + tag_data_size > file_length:
            raise Refused("its tag data is outside it")
        for offset in HEADER_WORDS:
            map_.swap(offset, 4)
        for offset in HEADER_SHORTS:
            map_.swap(offset, 2)
        map_.regions.append((TAG_CACHE_BASE_ADDRESS, tag_data_size, tag_data_offset))

        # the tag header: instances, scenario, checksum, count, vertex buffers' count and address, index
        # buffers' count and address, signature
        tags = tag_data_offset
        (instances, scenario_index, _, tag_count, vertex_count, vertex_buffers, index_count,
         index_buffers, tag_signature) = struct.unpack_from("<IiIiiIiII", data, tags)
        if tag_signature != 0x74616773 or not 0 < tag_count <= 0xFFFF:
            raise Refused("its tag header is damaged")
        map_.swap(tags, 4, 9)
        instance_table = map_.region_offset(instances, tag_count * 0x20)
        if instance_table is None:
            raise Refused("its tag table is outside the tags")
        for word in (0, 20, 28):
            map_.pointer(tags + word)
        for count, address in ((vertex_count, vertex_buffers), (index_count, index_buffers)):
            if count:
                start = map_.region_offset(address, count * BUFFER_SIZE)
                if start is None or count < 0:
                    raise Refused("its buffers are outside the tags")
                map_.swap(start, 4, count * 3)
                for buffer in range(count):
                    map_.pointer(start + buffer * BUFFER_SIZE + 4)

        bsp_references = []
        bsp_base_words = []
        for index in range(tag_count):
            entry = instance_table + index * 0x20
            group, _, _, _, _, base_address, _, _ = struct.unpack_from("<IIIiIIII", data, entry)
            map_.swap(entry, 4, 8)
            map_.pointer(entry + 0x10)
            map_.pointer(entry + 0x14)
            group_text = group.to_bytes(4, "big").decode("latin-1")
            if group_text == "sbsp" or not base_address:
                continue
            start = map_.region_offset(base_address, 4)
            if start is None:
                raise Refused(f"tag {index} ({group_text}) is outside the tags")
            if group_text == "scnr" and index == (scenario_index & 0xFFFF):
                # (its structure bsps, read before the swap: where each loads)
                count, address = struct.unpack_from("<iI", data, start + SCENARIO_STRUCTURE_BSPS_OFFSET)
                table = map_.region_offset(address, count * 0x20) if count else None
                for element in range(count if table is not None else 0):
                    bsp_references.append(struct.unpack_from("<iiI", data, table + element * 0x20))
                    bsp_base_words.append(table + element * 0x20 + 8)
            self.tag(map_, group_text, start)
            map_.counts["tags"] += 1

        # each structure bsp, where it loads: its header (base address, vertex buffers' count and address,
        # index buffers' count and address, signature), then its root
        for file_offset, file_size, base_address in bsp_references:
            if file_offset < 0 or file_size <= 0 or file_offset + file_size > file_length:
                raise Refused("a structure bsp is outside it")
            map_.regions.append((base_address, file_size, file_offset))
            root, vertex_count, vertex_buffers, index_count, index_buffers, bsp_signature = \
                struct.unpack_from("<IiIiII", data, file_offset)
            if bsp_signature != 0x73627370:
                raise Refused("a structure bsp's header is damaged")
            map_.swap(file_offset, 4, 6)
            for word in (0, 8, 16):
                map_.pointer(file_offset + word)
            for count, address in ((vertex_count, vertex_buffers), (index_count, index_buffers)):
                if count:
                    start = map_.region_offset(address, count * BUFFER_SIZE)
                    if start is None or count < 0:
                        raise Refused("a structure bsp's buffers are outside it")
                    map_.swap(start, 4, count * 3)
                    for buffer in range(count):
                        map_.pointer(start + buffer * BUFFER_SIZE + 4)
            start = map_.region_offset(root, 4)
            if start is None:
                raise Refused("a structure bsp's root is outside it")
            self.tag(map_, "sbsp", start)
            map_.regions.pop()
            map_.counts["bsps"] += 1
        # the bsps' own base addresses (words of the scenario, which name where each loads): not in a region once the bsps are popped, and
        # moved with the rest
        for word in bsp_base_words:
            if int.from_bytes(map_.data[word:word + 4], "big"):
                map_.pointers.add(word)
        if audit_pointers:
            self.pointer_audit(map_, tag_data_offset, tag_data_size, [(a, s_, o) for s_, o, a in []])
        if rebase is not None:
            for word in map_.pointers:
                value = int.from_bytes(map_.data[word:word + 4], "big")
                map_.data[word:word + 4] = ((value + rebase) & 0xFFFFFFFF).to_bytes(4, "big")
            map_.counts["pointers moved"] = len(map_.pointers)
        return map_

    def pointer_audit(self, map_: "Map", tag_data_offset: int, tag_data_size: int, bsp_regions):
        """the 4-byte words, in the tag data, whose value is an address in the tag cache and that nothing named as one: unmodelled pointers
        (or numbers that look like them), grouped by the definition of the element they are in and their offset there"""
        import bisect
        ordered = sorted(map_.elements)
        starts = [e[0] for e in ordered]
        found = Counter()
        low, high = TAG_CACHE_BASE_ADDRESS, TAG_CACHE_BASE_ADDRESS + tag_data_size
        end = tag_data_offset + tag_data_size
        for offset in range(tag_data_offset, end - 3, 4):
            if offset in map_.pointers or not map_.swapped[offset]:
                continue
            value = int.from_bytes(map_.data[offset:offset + 4], "big")
            if low <= value < high:
                index = bisect.bisect_right(starts, offset) - 1
                name = "(no element)"
                # (elements nest: the innermost one that holds the word, looking back from the nearest start)
                best = None
                for back in range(index, max(index - 200, -1), -1):
                    start, size, definition = ordered[back]
                    if start <= offset < start + size and (best is None or size < best[1]):
                        best = (start, size, definition)
                if best:
                    name = f"{best[2]}+{offset - best[0]:#x}"
                found[name] += 1
        map_.pointer_audit_result = found


def read_map(path: Path) -> bytearray:
    raw = path.read_bytes()
    file_length = struct.unpack_from("<i", raw, 8)[0]
    if len(raw) == file_length:
        return bytearray(raw)
    return bytearray(raw[:HEADER_SIZE] + zlib.decompress(raw[HEADER_SIZE:]))


def symbol_table(path: Path) -> dict:
    symbols = defaultdict(list)
    for line in path.read_text().splitlines():
        parts = line.split()
        if len(parts) >= 3:
            symbols[int(parts[0], 16)].append(parts[2])
    return symbols


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--schema", required=True, type=Path)
    parser.add_argument("--layouts", required=True, type=Path)
    parser.add_argument("--symbols", required=True, type=Path)
    parser.add_argument("--level", type=int, default=6, help="zlib level of the written map")
    parser.add_argument("--audit", type=Path, help="write the coverage audit (words no layout covers) as JSON")
    parser.add_argument("--rebase", type=lambda text: int(text, 0), help="add this to every address in the map (the window's new base minus 0x80000000)")
    parser.add_argument("--audit-pointers", action="store_true", help="list the words that look like addresses and were not moved")
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    converter = Converter(json.loads(args.schema.read_text()), json.loads(args.layouts.read_text()),
                          symbol_table(args.symbols))
    data = read_map(args.input)
    try:
        map_ = converter.convert(data, audit=args.audit is not None, rebase=args.rebase, audit_pointers=args.audit_pointers)
    except Refused as refusal:
        print(f"{args.input.name}: refused: {refusal}", file=sys.stderr)
        return 1
    compressed = zlib.compress(bytes(map_.data[HEADER_SIZE:]), args.level)
    args.output.write_bytes(bytes(map_.data[:HEADER_SIZE]) + compressed)
    covered = sum(map_.swapped)
    print(f"{args.input.name}: {map_.counts['tags']} tags, {map_.counts['bsps']} bsps; swapped "
          f"{map_.counts[2]} shorts, {map_.counts[4]} words, {map_.counts[8]} doubles; "
          f"{covered} of {len(data)} bytes visited")
    if args.audit:
        rows = [{"definition": name, "offset": offset, **counts} for (name, offset), counts in map_.audit.items()]
        args.audit.write_text(json.dumps(rows))
        gaps = [row for row in rows if row.get("nonzero")]
        print(f"  coverage audit: {len(gaps)} words with values no layout covers "
              f"({sum(1 for row in gaps if row.get('float', 0) * 2 >= row['nonzero'])} mostly float-like)")
    if args.rebase is not None:
        print(f"  rebased by {args.rebase & 0xFFFFFFFF:#x}: {map_.counts['pointers moved']} addresses moved")
    if args.audit_pointers:
        for name, count in map_.pointer_audit_result.most_common(40):
            print(f"  not moved, looks like an address: {name} ({count} words)")
    for name, size in map_.unhandled.most_common():
        print(f"  left as it is: {name} ({size} {'tags' if name.startswith('group') else 'bytes'})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
