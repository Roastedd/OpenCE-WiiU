/*
TAG_SWAP.C

A map's tags turned from little-endian to big-endian (tag_swap.h): the walk
tools/wiiu/map_convert.py's Converter makes, over the schemas the game
compiled (tag_schema.h) with the scalar layouts tag_swap_layouts.inc gives
each of their definitions.

The bytes being turned are one buffer (the tag cache, a bsp, the model data,
an Xbox map file), and addresses in them are found by regions: where a range
of addresses is in the buffer. Two bits a byte, in the caller's scratch
memory, keep what has been turned (never turned twice; a value read as its
bytes are now, turned or not) and what roots, blocks and data hold (none may
hold another's bytes, which also bounds the walk by the buffer's size).
*/

/* ---------- headers */

#include "cseries.h"
#include "tag_schema.h"
#include "cache_file_formats.h"
#include "tag_swap.h"

#include <stdarg.h>
#include <stdio.h>

/* ---------- constants */

enum
{
	/* how deep blocks go (map_convert.py's limit; the deepest are 6 or 7) */
	MAXIMUM_TAG_SWAP_DEPTH = 32,
	MAXIMUM_TAG_SWAP_REGIONS = 4,
	TAG_SWAP_BITS = 32,
	/* the index ranges model parts share, turned once (an Xbox map's parts
	name their index buffers'): a table of this many in the scratch */
	TAG_SWAP_SHARED_SLOTS = 0x8000,
	/* the definitions whose layouts have been found */
	TAG_SWAP_DEFINITION_SLOTS = 0x400,

	TAG_INDEX_SIGNATURE = 'tags',
	STRUCTURE_BSP_SIGNATURE = 'sbsp',
	CACHE_HEADER_SIGNATURE = 'head',
	STRUCTURE_BSP_GROUP_TAG = 'sbsp',
	SCENARIO_GROUP_TAG = 'scnr',
	GBXMODEL_GROUP_TAG = 'mod2',

	/* a Custom Edition map's tag index: the tag table, the scenario, the
	checksum, the count, where its model data is in the file (cache_file_formats.c) */
	CUSTOM_EDITION_TAG_INDEX_BYTES = 0x28,
	CUSTOM_EDITION_TAG_INDEX_WORDS = 10,
	TAG_INDEX_INSTANCES_OFFSET = 0x00,
	TAG_INDEX_COUNT_OFFSET = 0x0C,
	CUSTOM_EDITION_TAG_INDEX_SIGNATURE_OFFSET = 0x24,
	TAG_INSTANCE_BYTES = 0x20,
	TAG_INSTANCE_WORDS = 8,
	TAG_INSTANCE_ADDRESS_OFFSET = 0x14,
	MAXIMUM_TAG_COUNT = 0xFFFF,

	/* a structure bsp's header: its root, two buffer arrays, its signature */
	STRUCTURE_BSP_HEADER_BYTES = 0x18,
	STRUCTURE_BSP_HEADER_WORDS = 6,
	/* a vertex or index buffer (a D3DResource: Common, Data, Lock) */
	BUFFER_BYTES = 12,
	BUFFER_WORDS = 3,

	/* an Xbox map's header (cache_files_windows.c) and tag header: the tag
	table, the scenario, the checksum, the count, the vertex and index buffers,
	the signature */
	XBOX_HEADER_BYTES = 0x800,
	XBOX_TAG_HEADER_WORDS = 9,
	XBOX_TAG_HEADER_SIGNATURE_OFFSET = 0x20,
	SCENARIO_STRUCTURE_BSPS_OFFSET = 0x5A4,
	STRUCTURE_BSP_REFERENCE_BYTES = 0x20,
	MAXIMUM_XBOX_STRUCTURE_BSPS = 64,

	/* a gbxmodel part's vertex in the model data: position, normal, binormal,
	tangent and texture coordinates (14 floats), two node indices (shorts),
	two node weights (floats) */
	GBXMODEL_VERTEX_BYTES = 68,

	/* a structure material's compressed vertices (object_lights.c): 32 bytes
	of 8 words, then its lightmap vertices, 8 bytes of a word and two shorts */
	COMPRESSED_ENVIRONMENT_VERTEX_BYTES = 32,
	COMPRESSED_LIGHTMAP_VERTEX_BYTES = 8,

	/* a compressed animation's header: 11 offsets into the data */
	ANIMATION_COMPRESSED_HEADER_BYTES = 0x2C,
	MAXIMUM_ANIMATION_NODES = 64,

	/* a recorded animation's stream (recorded_animation_playback.c): an
	event's header byte has its type in its upper six bits */
	RECORDED_ANIMATION_END_EVENT = 1,
	NUMBER_OF_RECORDED_ANIMATION_EVENTS = 23,
	NUMBER_OF_UNIT_CONTROL_VERSIONS = 4,
	PLAYBACK_STATE_SHORTS = 6,
};

/* an Xbox map's tags and Custom Edition map's (cache_file_formats.h) */
#define XBOX_TAG_CACHE_ADDRESS 0x803A6000UL
#define XBOX_TAG_CACHE_BYTES 0x01600000UL

enum tag_swap_field_kind
{
	_tag_swap_field_block,
	_tag_swap_field_data,
	_tag_swap_field_file_data,
	_tag_swap_field_struct,
};

enum tag_swap_data_kind
{
	_tag_swap_data_bytes,
	_tag_swap_data_shorts,
	_tag_swap_data_words,
	/* swapped by its element's hook */
	_tag_swap_data_hook,
	_tag_swap_data_structure_vertices,
	_tag_swap_data_hs_syntax_data,
};

enum tag_swap_hook
{
	_tag_swap_hook_none,
	_tag_swap_hook_animation,
	_tag_swap_hook_recorded_animation,
	_tag_swap_hook_model_geometry_part,
};

/* ---------- structures */

struct tag_swap_run
{
	unsigned short offset;
	unsigned short width;
	unsigned short count;
};

struct tag_swap_field
{
	short kind;
	unsigned short offset;
	short count;
	short size;
};

struct tag_swap_data_handler
{
	unsigned short offset;
	short kind;
};

struct tag_swap_layout
{
	char const *name;
	long size;
	short first_run;
	short run_count;
	short first_field;
	short field_count;
	short first_handler;
	short handler_count;
	short hook;
};

/* where size bytes of addresses from address are in the buffer */
struct tag_swap_region
{
	unsigned long address;
	unsigned long size;
	unsigned long offset;
};

/* an index range a model part names, once turned */
struct tag_swap_shared
{
	unsigned long offset;
	unsigned long width;
	unsigned long count;
};

struct tag_swap
{
	byte *bytes;
	unsigned long size;
	/* a bit a byte: turned (or passed as a byte), and held by a root, block
	or data */
	unsigned long *swapped;
	unsigned long *claimed;
	struct tag_swap_shared *shared;
	unsigned long shared_count;
	struct tag_swap_region regions[MAXIMUM_TAG_SWAP_REGIONS];
	short region_count;
	/* the most bytes a block may hold */
	unsigned long tag_cache_size;
	/* a Custom Edition map's: its gbxmodels' groups come first */
	boolean custom_edition;
	boolean refused;
	struct tag_swap_report *report;
};

/* ---------- globals */

#include "tag_swap_layouts.inc"

/* the definitions whose layouts have been found and checked (by their
address: they are the schemas' constants) */
static struct
{
	struct tag_schema_definition const *definition;
	struct tag_swap_layout const *layout;
} tag_swap_definitions[TAG_SWAP_DEFINITION_SLOTS];

/* a recorded animation's unit control fields, by version (version 0: two
bytes, three shorts, a real_vector2d and three real_vector3d; versions 1, 2
and 3 add a long, a short and a short): width, count; and each event type's
data bytes and the values in them (map_convert.py's EVENT_DATA) */
static byte const recorded_animation_unit_control[NUMBER_OF_UNIT_CONTROL_VERSIONS][9][2] =
{
	{ { 1, 1 }, { 1, 1 }, { 2, 1 }, { 2, 1 }, { 2, 1 }, { 4, 2 }, { 4, 3 }, { 4, 3 }, { 4, 3 } },
	{ { 4, 1 } },
	{ { 2, 1 } },
	{ { 2, 1 } },
};

/* ---------- private code */

static unsigned long little_u32(
	byte const *bytes)
{
	return (unsigned long)bytes[0] | (unsigned long)bytes[1] << 8 | (unsigned long)bytes[2] << 16 |
		(unsigned long)bytes[3] << 24;
}

static unsigned long big_u32(
	byte const *bytes)
{
	return (unsigned long)bytes[3] | (unsigned long)bytes[2] << 8 | (unsigned long)bytes[1] << 16 |
		(unsigned long)bytes[0] << 24;
}

static unsigned short little_u16(
	byte const *bytes)
{
	return (unsigned short)(bytes[0] | bytes[1] << 8);
}

static unsigned short big_u16(
	byte const *bytes)
{
	return (unsigned short)(bytes[1] | bytes[0] << 8);
}

/* a 32-bit value read as two's complement */
static long signed_u32(
	unsigned long value)
{
	return value <= 0x7FFFFFFFUL ? (long)value : -(long)(0xFFFFFFFFUL - value) - 1;
}

static char *group_tag_text(
	unsigned long group_tag,
	char text[5])
{
	short index;

	for (index = 0; index < 4; index++)
	{
		char character = (char)(group_tag >> (24 - 8 * index));

		text[index] = character >= ' ' && character < 0x7F ? character : '?';
	}
	text[4] = 0;

	return text;
}

static void swap_refuse(
	struct tag_swap *swap,
	char const *format,
	...)
{
	va_list arguments;

	if (swap->refused)
		return;
	swap->refused = TRUE;
	va_start(arguments, format);
	vsnprintf(swap->report->problem, sizeof(swap->report->problem), format, arguments);
	va_end(arguments);

	return;
}

/* ---------- the bits */

static boolean bits_any(
	unsigned long const *bits,
	unsigned long first,
	unsigned long count)
{
	unsigned long bit = first;
	unsigned long end = first + count;

	while (bit < end)
	{
		if (bit % TAG_SWAP_BITS == 0 && end - bit >= TAG_SWAP_BITS)
		{
			if (bits[bit / TAG_SWAP_BITS])
				return TRUE;
			bit += TAG_SWAP_BITS;
		}
		else
		{
			if ((bits[bit / TAG_SWAP_BITS] >> (bit % TAG_SWAP_BITS)) & 1)
				return TRUE;
			bit++;
		}
	}

	return FALSE;
}

static boolean bits_all(
	unsigned long const *bits,
	unsigned long first,
	unsigned long count)
{
	unsigned long bit;

	for (bit = first; bit < first + count; bit++)
	{
		if (!((bits[bit / TAG_SWAP_BITS] >> (bit % TAG_SWAP_BITS)) & 1))
			return FALSE;
	}

	return TRUE;
}

static void bits_set(
	unsigned long *bits,
	unsigned long first,
	unsigned long count)
{
	unsigned long bit = first;
	unsigned long end = first + count;

	while (bit < end)
	{
		if (bit % TAG_SWAP_BITS == 0 && end - bit >= TAG_SWAP_BITS)
		{
			bits[bit / TAG_SWAP_BITS] = 0xFFFFFFFFUL;
			bit += TAG_SWAP_BITS;
		}
		else
		{
			bits[bit / TAG_SWAP_BITS] |= 1UL << (bit % TAG_SWAP_BITS);
			bit++;
		}
	}

	return;
}

static unsigned long bit_words(
	unsigned long bytes)
{
	return bytes / TAG_SWAP_BITS + 1;
}

/* ---------- the buffer */

static void swap_begin(
	struct tag_swap *swap,
	void *bytes,
	unsigned long size,
	void *scratch,
	boolean custom_edition,
	struct tag_swap_report *report)
{
	unsigned long words = bit_words(size);

	csmemset(swap, 0, sizeof(*swap));
	csmemset(report, 0, sizeof(*report));
	swap->bytes = bytes;
	swap->size = size;
	swap->swapped = scratch;
	swap->claimed = swap->swapped + words;
	swap->shared = (struct tag_swap_shared *)(swap->claimed + words);
	csmemset(scratch, 0, tag_swap_scratch_bytes(size));
	swap->custom_edition = custom_edition;
	swap->tag_cache_size = custom_edition ? CUSTOM_EDITION_TAG_CACHE_BYTES : XBOX_TAG_CACHE_BYTES;
	swap->report = report;

	return;
}

static void region_add(
	struct tag_swap *swap,
	unsigned long address,
	unsigned long size,
	unsigned long offset)
{
	if (swap->region_count < MAXIMUM_TAG_SWAP_REGIONS)
	{
		swap->regions[swap->region_count].address = address;
		swap->regions[swap->region_count].size = size;
		swap->regions[swap->region_count].offset = offset;
		swap->region_count++;
	}
	else
	{
		swap_refuse(swap, "too many regions");
	}

	return;
}

/* the buffer's offset of the size bytes at address, if a region has them */
static boolean region_offset(
	struct tag_swap const *swap,
	unsigned long address,
	unsigned long size,
	unsigned long *offset)
{
	short index;

	for (index = 0; index < swap->region_count; index++)
	{
		struct tag_swap_region const *region = &swap->regions[index];

		if (address >= region->address && size <= region->size && address - region->address <= region->size - size)
		{
			*offset = region->offset + (address - region->address);
			return TRUE;
		}
	}

	return FALSE;
}

/* the 4-byte value at offset, as its bytes are now: turned or not */
static unsigned long value_u32(
	struct tag_swap const *swap,
	unsigned long offset)
{
	if (offset > swap->size || swap->size - offset < 4)
		return 0;

	return (swap->swapped[offset / TAG_SWAP_BITS] >> (offset % TAG_SWAP_BITS)) & 1 ?
		big_u32(swap->bytes + offset) :
		little_u32(swap->bytes + offset);
}

/* whether size bytes at offset are in the buffer */
static boolean in_buffer(
	struct tag_swap const *swap,
	unsigned long offset,
	unsigned long size)
{
	return offset <= swap->size && size <= swap->size - offset;
}

/* count values of width bytes at offset turned (one-byte values only marked):
refused if any of their bytes are outside the buffer or turned already */
static void values_swap(
	struct tag_swap *swap,
	unsigned long offset,
	unsigned long width,
	unsigned long count)
{
	unsigned long bytes;
	unsigned long index;

	if (swap->refused)
		return;
	if (offset > swap->size || count > (swap->size - offset) / width)
	{
		swap_refuse(swap, "a swap at %08lx is outside the map", offset);
		return;
	}
	bytes = width * count;
	if (bits_any(swap->swapped, offset, bytes))
	{
		swap_refuse(swap, "the bytes at %08lx would be swapped twice", offset);
		return;
	}
	bits_set(swap->swapped, offset, bytes);
	swap->report->bytes_visited += bytes;
	if (width == 1)
		return;
	for (index = 0; index < count; index++)
	{
		byte *value = swap->bytes + offset + index * width;
		unsigned long low;

		for (low = 0; low < width / 2; low++)
		{
			byte kept = value[low];

			value[low] = value[width - 1 - low];
			value[width - 1 - low] = kept;
		}
	}
	if (width == 2)
		swap->report->shorts += count;
	else if (width == 4)
		swap->report->words += count;
	else
		swap->report->doubles += count;

	return;
}

/* values of a structure its parent may have turned already: each one the
parent did not */
static void values_swap_unless_swapped(
	struct tag_swap *swap,
	unsigned long offset,
	unsigned long width,
	unsigned long count)
{
	unsigned long index;

	for (index = 0; index < count && !swap->refused; index++)
	{
		unsigned long at = offset + index * width;

		if (!in_buffer(swap, at, width))
		{
			swap_refuse(swap, "a swap at %08lx is outside the map", at);
			return;
		}
		if (bits_all(swap->swapped, at, width))
			continue;
		if (bits_any(swap->swapped, at, width))
		{
			swap_refuse(swap, "the value at %08lx is half swapped", at);
			return;
		}
		values_swap(swap, at, width, 1);
	}

	return;
}

/* values several elements may name (a buffer's indices): turned the first
time, then left */
static void values_swap_shared(
	struct tag_swap *swap,
	unsigned long offset,
	unsigned long width,
	unsigned long count)
{
	unsigned long slot = (offset * 2654435761UL ^ count * 40503UL ^ width) % TAG_SWAP_SHARED_SLOTS;
	unsigned long probe;

	for (probe = 0; probe < TAG_SWAP_SHARED_SLOTS; probe++, slot = (slot + 1) % TAG_SWAP_SHARED_SLOTS)
	{
		struct tag_swap_shared *shared = &swap->shared[slot];

		if (!shared->width)
		{
			shared->offset = offset;
			shared->width = width;
			shared->count = count;
			swap->shared_count++;
			values_swap(swap, offset, width, count);
			return;
		}
		if (shared->offset == offset && shared->width == width && shared->count == count)
			return;
	}
	swap_refuse(swap, "more than %d index ranges are shared", TAG_SWAP_SHARED_SLOTS);

	return;
}

/* size bytes at offset held by a root, block or data: FALSE if any already are */
static boolean claim(
	struct tag_swap *swap,
	unsigned long offset,
	unsigned long size)
{
	if (!in_buffer(swap, offset, size) || bits_any(swap->claimed, offset, size))
		return FALSE;
	bits_set(swap->claimed, offset, size);

	return TRUE;
}

/* ---------- definitions */

static struct tag_swap_layout const *layout_find(
	char const *name,
	long size)
{
	long low = 0;
	long high = NUMBEROF(tag_swap_layouts);

	while (low < high)
	{
		long middle = (low + high) / 2;
		struct tag_swap_layout const *layout = &tag_swap_layouts[middle];
		long order = strcmp(layout->name, name);

		if (!order)
			order = layout->size < size ? -1 : layout->size > size ? 1 : 0;
		if (!order)
			return layout;
		if (order < 0)
			low = middle + 1;
		else
			high = middle;
	}

	return NULL;
}

/* whether the definition's blocks, data and structures are where the layout
says the schemas had them (the tables are of this build's schemas) */
static boolean layout_matches(
	struct tag_schema_definition const *definition,
	struct tag_swap_layout const *layout)
{
	struct tag_schema_field const *field;
	short index = 0;

	for (field = definition->fields; field->type != _tag_schema_terminator; field++)
	{
		struct tag_swap_field const *expected;
		short kind;

		switch (field->type)
		{
		case _tag_schema_block: kind = _tag_swap_field_block; break;
		case _tag_schema_data: kind = _tag_swap_field_data; break;
		case _tag_schema_file_data: kind = _tag_swap_field_file_data; break;
		case _tag_schema_struct: kind = _tag_swap_field_struct; break;
		default: continue;
		}
		if (index >= layout->field_count)
			return FALSE;
		expected = &tag_swap_fields[layout->first_field + index++];
		if (expected->kind != kind || expected->offset != field->offset || expected->count != field->count ||
			expected->size != field->size)
		{
			return FALSE;
		}
	}

	return index == layout->field_count;
}

static struct tag_swap_layout const *layout_get(
	struct tag_swap *swap,
	struct tag_schema_definition const *definition)
{
	unsigned long slot = ((unsigned long)definition >> 2) % TAG_SWAP_DEFINITION_SLOTS;
	unsigned long probe;
	struct tag_swap_layout const *layout;

	for (probe = 0; probe < TAG_SWAP_DEFINITION_SLOTS; probe++, slot = (slot + 1) % TAG_SWAP_DEFINITION_SLOTS)
	{
		if (tag_swap_definitions[slot].definition == definition)
			return tag_swap_definitions[slot].layout;
		if (!tag_swap_definitions[slot].definition)
			break;
	}
	layout = layout_find(definition->name, definition->size);
	if (!layout || !layout_matches(definition, layout))
	{
		swap_refuse(swap, "the swap's tables have no layout of %s (%ld bytes) as this build has it "
			"(tools/wiiu/tag_swap_tables.py)", definition->name, definition->size);
		return NULL;
	}
	if (probe < TAG_SWAP_DEFINITION_SLOTS)
	{
		tag_swap_definitions[slot].definition = definition;
		tag_swap_definitions[slot].layout = layout;
	}

	return layout;
}

static struct tag_schema_group const *group_get(
	boolean custom_edition,
	unsigned long group_tag)
{
	struct tag_schema_group const *const *list;
	struct tag_schema_group const *group;

	/* (a Custom Edition map's groups laid out otherwise come first, as the
	validator takes them) */
	if (custom_edition)
	{
		for (group = tag_schema_custom_edition_groups; group->group_tag; group++)
		{
			if (group->group_tag == group_tag)
				return group;
		}
	}
	for (list = tag_schema_group_lists; *list; list++)
	{
		for (group = *list; group->group_tag; group++)
		{
			if (group->group_tag == group_tag)
				return group;
		}
	}

	return NULL;
}

/* ---------- hooks */

static boolean node_flag(
	unsigned long const flags[2],
	long node)
{
	return (flags[node >> 5] >> (node & 31)) & 1;
}

static long node_flag_count(
	unsigned long const flags[2],
	long node_count)
{
	long count = 0;
	long node;

	for (node = 0; node < node_count; node++)
		count += node_flag(flags, node);

	return count;
}

/* the size bytes at the address a tag data's value says, in the element at
offset, or none (FALSE, no problem) when it has none */
static boolean element_blob(
	struct tag_swap *swap,
	unsigned long offset,
	unsigned long size_offset,
	unsigned long address_offset,
	char const *name,
	unsigned long *start,
	unsigned long *size)
{
	unsigned long address = big_u32(swap->bytes + offset + address_offset);

	*size = big_u32(swap->bytes + offset + size_offset);
	if (!*size || !address)
		return FALSE;
	if (!region_offset(swap, address, *size, start))
	{
		swap_refuse(swap, "%s at %08lx is outside the tags", name, address);
		return FALSE;
	}

	return TRUE;
}

/* one frame of an animation (or its defaults, a node's value in them when
its flag is clear): node by node, its rotation (four shorts), translation
(three floats), scale (a float) */
static unsigned long animation_frame_swap(
	struct tag_swap *swap,
	unsigned long at,
	long node_count,
	unsigned long const rotation[2],
	unsigned long const translation[2],
	unsigned long const scale[2],
	boolean defaults)
{
	unsigned long start = at;
	long node;

	for (node = 0; node < node_count && !swap->refused; node++)
	{
		if (node_flag(rotation, node) != defaults)
		{
			values_swap(swap, at, 2, 4);
			at += 8;
		}
		if (node_flag(translation, node) != defaults)
		{
			values_swap(swap, at, 4, 3);
			at += 12;
		}
		if (node_flag(scale, node) != defaults)
		{
			values_swap(swap, at, 4, 1);
			at += 4;
		}
	}

	return at - start;
}

/* whether count values of width bytes at relative lie in the available bytes */
static boolean animation_inside(
	long relative,
	long count,
	unsigned long width,
	unsigned long available)
{
	return relative >= 0 && count >= 0 && (unsigned long)relative <= available &&
		(unsigned long)count <= (available - (unsigned long)relative) / width;
}

/* a compressed animation's keyframes of one kind: their node headers (each
a keyframe count and first keyframe, read before they are turned), keyframes
(of keyframe_words values) and frame indices */
static void animation_keyframes_swap(
	struct tag_swap *swap,
	unsigned long base,
	unsigned long available,
	long headers_relative,
	long count,
	long keyframes_relative,
	long frames_relative,
	unsigned long keyframe_width,
	unsigned long keyframe_words)
{
	unsigned long total = 0;
	long index;

	if (!animation_inside(headers_relative, count, 4, available))
	{
		swap_refuse(swap, "an animation's compressed node headers are outside it");
		return;
	}
	for (index = 0; index < count; index++)
	{
		unsigned long header = little_u32(swap->bytes + base + (unsigned long)headers_relative + (unsigned long)index * 4);

		if (header & 0xFFF)
			total = MAX(total, (header >> 12) + (header & 0xFFF));
	}
	values_swap(swap, base + (unsigned long)headers_relative, 4, (unsigned long)count);
	if (!total)
		return;
	if (!animation_inside(keyframes_relative, (long)total, keyframe_width, available) ||
		!animation_inside(frames_relative, (long)total, 2, available))
	{
		swap_refuse(swap, "an animation's compressed keyframes are outside it");
		return;
	}
	values_swap(swap, base + (unsigned long)keyframes_relative, keyframe_width / keyframe_words, total * keyframe_words);
	values_swap(swap, base + (unsigned long)frames_relative, 2, total);

	return;
}

/* an animation's frame info (floats), defaults and frames, node by node, and
its compressed data's header, node headers, keyframes and frame indices
(model_animations.c's readers; tag_schema_models.c's animation_data_problem) */
static void animation_swap(
	struct tag_swap *swap,
	unsigned long offset)
{
	byte const *animation = swap->bytes + offset;
	long node_count = (short)big_u16(animation + TAG_SWAP_ANIMATION_NODE_COUNT_OFFSET);
	unsigned long rotation[2];
	unsigned long translation[2];
	unsigned long scale[2];
	long rotation_count, translation_count, scale_count;
	unsigned long frame_count, frame_size, compressed_data_offset;
	boolean compressed;
	unsigned long start, size;
	unsigned long expected, uncompressed_end = 0;
	unsigned long base, available;
	long header[11];
	short index;

	if (node_count < 0 || node_count > MAXIMUM_ANIMATION_NODES)
		return;
	for (index = 0; index < 2; index++)
	{
		rotation[index] = big_u32(animation + TAG_SWAP_ANIMATION_ROTATION_FLAGS_OFFSET + index * 4);
		translation[index] = big_u32(animation + TAG_SWAP_ANIMATION_TRANSLATION_FLAGS_OFFSET + index * 4);
		scale[index] = big_u32(animation + TAG_SWAP_ANIMATION_SCALE_FLAGS_OFFSET + index * 4);
	}
	rotation_count = node_flag_count(rotation, node_count);
	translation_count = node_flag_count(translation, node_count);
	scale_count = node_flag_count(scale, node_count);
	frame_count = big_u16(animation + TAG_SWAP_ANIMATION_FRAME_COUNT_OFFSET);
	frame_size = big_u16(animation + TAG_SWAP_ANIMATION_FRAME_SIZE_OFFSET);
	compressed = big_u16(animation + TAG_SWAP_ANIMATION_FLAGS_OFFSET) & 1;
	compressed_data_offset = big_u32(animation + TAG_SWAP_ANIMATION_COMPRESSED_DATA_OFFSET_OFFSET);

	/* (frame info: per frame dx, dy and perhaps dz and dyaw, all floats) */
	if (element_blob(swap, offset, TAG_SWAP_ANIMATION_FRAME_INFO_SIZE_OFFSET, TAG_SWAP_ANIMATION_FRAME_INFO_ADDRESS_OFFSET,
		"animation.frame_info", &start, &size))
	{
		values_swap(swap, start, 4, size / 4);
		values_swap(swap, start + size / 4 * 4, 1, size % 4);
	}
	if (swap->refused)
		return;
	if (element_blob(swap, offset, TAG_SWAP_ANIMATION_DEFAULT_DATA_SIZE_OFFSET,
		TAG_SWAP_ANIMATION_DEFAULT_DATA_ADDRESS_OFFSET, "animation.default_data", &start, &size))
	{
		unsigned long default_size = (unsigned long)(node_count - rotation_count) * 8 +
			(unsigned long)(node_count - translation_count) * 12 + (unsigned long)(node_count - scale_count) * 4;

		if (default_size <= size)
			animation_frame_swap(swap, start, node_count, rotation, translation, scale, TRUE);
		if (default_size != size)
			swap->report->data_bytes_left += size;
	}
	if (swap->refused ||
		!element_blob(swap, offset, TAG_SWAP_ANIMATION_DATA_SIZE_OFFSET, TAG_SWAP_ANIMATION_DATA_ADDRESS_OFFSET,
			"animation.data", &start, &size))
	{
		return;
	}
	expected = (unsigned long)rotation_count * 8 + (unsigned long)translation_count * 12 + (unsigned long)scale_count * 4;
	if (frame_size == expected && frame_size && (!compressed || frame_count * frame_size <= compressed_data_offset) &&
		frame_count * frame_size <= size)
	{
		unsigned long frame;

		for (frame = 0; frame < frame_count && !swap->refused; frame++)
			animation_frame_swap(swap, start + frame * frame_size, node_count, rotation, translation, scale, FALSE);
		uncompressed_end = frame_count * frame_size;
	}
	if (!compressed)
	{
		if (uncompressed_end != size)
			swap->report->data_bytes_left += size - uncompressed_end;
		return;
	}
	if (compressed_data_offset < uncompressed_end || compressed_data_offset > size ||
		size - compressed_data_offset < ANIMATION_COMPRESSED_HEADER_BYTES)
	{
		swap->report->data_bytes_left += size;
		return;
	}
	base = start + compressed_data_offset;
	available = size - compressed_data_offset;
	/* (rotation frames, default rotations, rotation keyframes, translation
	headers, frames, defaults and keyframes, scale headers, frames, defaults
	and keyframes) */
	for (index = 0; index < 11; index++)
		header[index] = signed_u32(little_u32(swap->bytes + base + index * 4));
	values_swap(swap, base, 4, 11);
	animation_keyframes_swap(swap, base, available, ANIMATION_COMPRESSED_HEADER_BYTES, rotation_count, header[2],
		header[0], 6, 3);
	if (!swap->refused)
		animation_keyframes_swap(swap, base, available, header[3], translation_count, header[6], header[4], 12, 3);
	if (!swap->refused)
		animation_keyframes_swap(swap, base, available, header[7], scale_count, header[10], header[8], 4, 1);
	if (!swap->refused && node_count && animation_inside(header[1], node_count, 6, available))
		values_swap_unless_swapped(swap, base + (unsigned long)header[1], 2, (unsigned long)node_count * 3);
	if (!swap->refused && node_count && animation_inside(header[5], node_count, 12, available))
		values_swap_unless_swapped(swap, base + (unsigned long)header[5], 4, (unsigned long)node_count * 3);
	if (!swap->refused && scale_count && animation_inside(header[9], scale_count, 4, available))
		values_swap_unless_swapped(swap, base + (unsigned long)header[9], 4, (unsigned long)scale_count);

	return;
}

/* the walk of a recorded animation's stream */
struct recorded_animation_stream
{
	unsigned long start;
	unsigned long size;
	unsigned long position;
};

/* count values of width bytes at the stream's position turned (one-byte
ones passed), FALSE when they run past its end */
static boolean recorded_animation_take(
	struct tag_swap *swap,
	struct recorded_animation_stream *stream,
	unsigned long width,
	unsigned long count)
{
	if (stream->position > stream->size || width * count > stream->size - stream->position)
		return FALSE;
	if (width > 1)
		values_swap(swap, stream->start + stream->position, width, count);
	stream->position += width * count;

	return TRUE;
}

/* a recorded animation's event stream, walked as the game plays it
(recorded_animation_playback.c): its unit control's fields by version, the
playback state, then events, each a header byte (its type in its upper six
bits, a time delta in its lower two: 0 and 1 are themselves, 2 a byte after
the header, 3 a short) and the event's data; a stream that does not reach its
end event is left from where it fails */
static void recorded_animation_swap(
	struct tag_swap *swap,
	unsigned long offset)
{
	struct recorded_animation_stream stream;
	short versions = swap->bytes[offset + TAG_SWAP_RECORDED_ANIMATION_VERSION_OFFSET];
	short version;

	if (!element_blob(swap, offset, TAG_SWAP_RECORDED_ANIMATION_EVENT_STREAM_SIZE_OFFSET,
		TAG_SWAP_RECORDED_ANIMATION_EVENT_STREAM_ADDRESS_OFFSET, "recorded_animation.event_stream", &stream.start,
		&stream.size))
	{
		return;
	}
	stream.position = 0;
	versions = MIN(MAX(versions, 1), NUMBER_OF_UNIT_CONTROL_VERSIONS);
	for (version = 0; version < versions; version++)
	{
		short field;

		for (field = 0; field < 9 && recorded_animation_unit_control[version][field][0]; field++)
		{
			if (!recorded_animation_take(swap, &stream, recorded_animation_unit_control[version][field][0],
				recorded_animation_unit_control[version][field][1]))
			{
				goto unwalked;
			}
		}
	}
	if (!recorded_animation_take(swap, &stream, 2, PLAYBACK_STATE_SHORTS))
		goto unwalked;
	while (!swap->refused)
	{
		byte header;
		short delta, kind;
		unsigned long length, values_width = 0, values_count = 0;

		if (stream.position >= stream.size)
			goto unwalked;
		header = swap->bytes[stream.start + stream.position];
		delta = header & 3;
		kind = header >> 2;
		stream.position++;
		if (delta == 2)
			stream.position++;
		else if (delta == 3 && !recorded_animation_take(swap, &stream, 2, 1))
			goto unwalked;
		if (kind == RECORDED_ANIMATION_END_EVENT)
			return;
		if (kind >= NUMBER_OF_RECORDED_ANIMATION_EVENTS)
			goto unwalked;
		/* (types 0 and 1 have no data; 2 and 3 a byte; 4 and 5 a short; 6 two
		longs; 7 to 14 two bytes; 15 to 22 two shorts) */
		if (kind <= 1)
			length = 0;
		else if (kind <= 3)
			length = 1;
		else if (kind <= 5)
			length = 2, values_width = 2, values_count = 1;
		else if (kind == 6)
			length = 8, values_width = 4, values_count = 2;
		else if (kind <= 14)
			length = 2;
		else
			length = 4, values_width = 2, values_count = 2;
		if (stream.position > stream.size || length > stream.size - stream.position)
			goto unwalked;
		if (values_count)
			recorded_animation_take(swap, &stream, values_width, values_count);
		stream.position += length - values_width * values_count;
	}

	return;

unwalked:
	if (stream.position < stream.size)
		swap->report->data_bytes_left += stream.size - stream.position;

	return;
}

/* a model part's triangle indices (16-bit, in the index buffer its triangle
buffer names), which the CPU checks and a big-endian GPU reads big-endian */
static void model_geometry_part_swap(
	struct tag_swap *swap,
	unsigned long offset)
{
	byte const *part = swap->bytes + offset;
	short type = (short)big_u16(part + TAG_SWAP_MODEL_PART_TRIANGLE_BUFFER_TYPE_OFFSET);
	unsigned long count = big_u32(part + TAG_SWAP_MODEL_PART_TRIANGLE_BUFFER_COUNT_OFFSET);
	unsigned long descriptor = big_u32(part + TAG_SWAP_MODEL_PART_TRIANGLE_BUFFER_HARDWARE_FORMAT_OFFSET);
	unsigned long index_count;
	unsigned long descriptor_offset;
	unsigned long start;

	if (type == 0 && count <= 0x7FFFFFFFUL / 3)
		index_count = 3 * count;
	else if (type == 1 && count > 0 && count <= 0x7FFFFFFFUL - 2)
		index_count = count + 2;
	else
		index_count = 0;
	if (!descriptor || !index_count || !region_offset(swap, descriptor, BUFFER_BYTES, &descriptor_offset))
		return;
	if (!region_offset(swap, big_u32(swap->bytes + descriptor_offset + 4), index_count * 2, &start))
	{
		swap->report->data_bytes_left += index_count * 2;
		return;
	}
	values_swap_shared(swap, start, 2, index_count);

	return;
}

/* ---------- data */

/* a structure material's compressed vertices (object_lights.c's
environment_vertex_compressed and environment_lightmap_vertex_compressed):
32 bytes of 8 words each, then the lightmap vertices, 8 bytes each of a word
and two shorts */
static void structure_vertices_swap(
	struct tag_swap *swap,
	unsigned long element_offset,
	unsigned long start,
	unsigned long size)
{
	unsigned long vertex_count = big_u32(swap->bytes + element_offset + TAG_SWAP_STRUCTURE_MATERIAL_VERTEX_COUNT_OFFSET);
	unsigned long lightmap_vertex_count =
		big_u32(swap->bytes + element_offset + TAG_SWAP_STRUCTURE_MATERIAL_LIGHTMAP_VERTEX_COUNT_OFFSET);
	unsigned long base;
	unsigned long vertex;

	if (vertex_count > size / COMPRESSED_ENVIRONMENT_VERTEX_BYTES ||
		lightmap_vertex_count > size / COMPRESSED_LIGHTMAP_VERTEX_BYTES ||
		vertex_count * COMPRESSED_ENVIRONMENT_VERTEX_BYTES + lightmap_vertex_count * COMPRESSED_LIGHTMAP_VERTEX_BYTES != size)
	{
		swap->report->data_bytes_left += size;
		return;
	}
	values_swap(swap, start, 4, vertex_count * 8);
	base = start + vertex_count * COMPRESSED_ENVIRONMENT_VERTEX_BYTES;
	for (vertex = 0; vertex < lightmap_vertex_count && !swap->refused; vertex++)
	{
		values_swap(swap, base + vertex * COMPRESSED_LIGHTMAP_VERTEX_BYTES, 4, 1);
		values_swap(swap, base + vertex * COMPRESSED_LIGHTMAP_VERTEX_BYTES + 4, 2, 2);
	}

	return;
}

/* the scripts' syntax nodes: a data array (data.h's struct data_array) and
each of its maximum_count elements */
static void hs_syntax_data_swap(
	struct tag_swap *swap,
	char const *name,
	unsigned long start,
	unsigned long size)
{
	long maximum_count;
	long element_size;
	long element;
	short index;

	if (size < TAG_SWAP_DATA_ARRAY_BYTES)
	{
		swap_refuse(swap, "%s.hs_syntax_data is not a data array of hs_syntax_node", name);
		return;
	}
	maximum_count = (short)little_u16(swap->bytes + start + TAG_SWAP_DATA_ARRAY_MAXIMUM_COUNT_OFFSET);
	element_size = (short)little_u16(swap->bytes + start + TAG_SWAP_DATA_ARRAY_ELEMENT_SIZE_OFFSET);
	if (maximum_count < 0 || element_size != TAG_SWAP_HS_SYNTAX_NODE_BYTES ||
		(unsigned long)maximum_count > (size - TAG_SWAP_DATA_ARRAY_BYTES) / TAG_SWAP_HS_SYNTAX_NODE_BYTES)
	{
		swap_refuse(swap, "%s.hs_syntax_data is not a data array of hs_syntax_node", name);
		return;
	}
	for (index = 0; index < NUMBEROF(tag_swap_data_array_runs); index++)
	{
		values_swap(swap, start + tag_swap_data_array_runs[index].offset, tag_swap_data_array_runs[index].width,
			tag_swap_data_array_runs[index].count);
	}
	for (element = 0; element < maximum_count && !swap->refused; element++)
	{
		unsigned long base = start + TAG_SWAP_DATA_ARRAY_BYTES + (unsigned long)element * TAG_SWAP_HS_SYNTAX_NODE_BYTES;

		for (index = 0; index < NUMBEROF(tag_swap_hs_syntax_node_runs); index++)
		{
			values_swap(swap, base + tag_swap_hs_syntax_node_runs[index].offset, tag_swap_hs_syntax_node_runs[index].width,
				tag_swap_hs_syntax_node_runs[index].count);
		}
	}
	/* (what follows the elements, if anything, is left) */
	swap->report->data_bytes_left +=
		size - TAG_SWAP_DATA_ARRAY_BYTES - (unsigned long)maximum_count * TAG_SWAP_HS_SYNTAX_NODE_BYTES;

	return;
}

/* ---------- elements */

static void element_swap(
	struct tag_swap *swap,
	struct tag_schema_definition const *definition,
	unsigned long offset,
	boolean whole,
	short depth);

/* The validator cuts a block's elements and a data's bytes to the game's maximum before it follows them (tag_validate.c, validate_block_extent and
validate_data_extent), except a Custom Edition block whose maximum is the editing kit's; the elements and bytes past the cut are never looked at. A
Custom Edition map's block or data is walked as far as the validator walks it, so that one the desktop builds cut and play (a count of a protected map's
that is garbage, whose elements would run out of the tags) is not refused here. A negative count or size is not cut: the validator refuses it. The Xbox
maps are walked as map_convert.py walks them, whole. */
static unsigned long maximum_cut(
	struct tag_swap const *swap,
	struct tag_schema_field const *field,
	unsigned long value,
	boolean tool_maximum_exempt)
{
	if (swap->custom_edition && field->maximum > 0 && value <= 0x7FFFFFFFUL && value > (unsigned long)field->maximum &&
		!(tool_maximum_exempt && TEST_FLAG(field->flags, _tag_schema_tool_maximum_bit)))
	{
		return (unsigned long)field->maximum;
	}

	return value;
}

static void block_follow(
	struct tag_swap *swap,
	struct tag_schema_definition const *definition,
	struct tag_schema_field const *field,
	unsigned long at,
	short depth)
{
	struct tag_schema_definition const *child = field->definition;
	unsigned long count = maximum_cut(swap, field, value_u32(swap, at), TRUE);
	unsigned long address = value_u32(swap, at + 4);
	unsigned long stride = (unsigned long)MAX(child->size, 1);
	unsigned long start;
	unsigned long element;

	if (!count)
		return;
	if (count > swap->tag_cache_size / stride)
	{
		swap_refuse(swap, "%s.%s has %lu elements", definition->name, field->name, count);
		return;
	}
	if (!region_offset(swap, address, count * stride, &start))
	{
		swap_refuse(swap, "%s.%s at %08lx is outside the tags", definition->name, field->name, address);
		return;
	}
	if (!claim(swap, start, count * stride))
	{
		swap_refuse(swap, "%s.%s at %08lx overlaps another's", definition->name, field->name, address);
		return;
	}
	for (element = 0; element < count && !swap->refused; element++)
		element_swap(swap, child, start + element * stride, TRUE, (short)(depth + 1));

	return;
}

static void data_follow(
	struct tag_swap *swap,
	struct tag_schema_definition const *definition,
	struct tag_swap_layout const *layout,
	struct tag_schema_field const *field,
	unsigned long element_offset,
	unsigned long at)
{
	unsigned long size = maximum_cut(swap, field, value_u32(swap, at), FALSE);
	unsigned long address = value_u32(swap, at + 12);
	unsigned long start;
	short kind = NONE;
	short index;

	if (!size || !address)
		return;
	if (!region_offset(swap, address, size, &start))
	{
		swap_refuse(swap, "%s.%s at %08lx is outside the tags", definition->name, field->name, address);
		return;
	}
	if (!claim(swap, start, size))
	{
		swap_refuse(swap, "%s.%s at %08lx overlaps another's", definition->name, field->name, address);
		return;
	}
	for (index = 0; index < layout->handler_count; index++)
	{
		if (tag_swap_data_handlers[layout->first_handler + index].offset == field->offset)
			kind = tag_swap_data_handlers[layout->first_handler + index].kind;
	}
	switch (kind)
	{
	case _tag_swap_data_bytes:
		values_swap(swap, start, 1, size);
		break;
	case _tag_swap_data_shorts:
		values_swap(swap, start, 2, size / 2);
		break;
	case _tag_swap_data_words:
		values_swap(swap, start, 4, size / 4);
		break;
	case _tag_swap_data_hook:
		break;
	case _tag_swap_data_structure_vertices:
		structure_vertices_swap(swap, element_offset, start, size);
		break;
	case _tag_swap_data_hs_syntax_data:
		hs_syntax_data_swap(swap, definition->name, start, size);
		break;
	default:
		swap->report->data_bytes_left += size;
		break;
	}

	return;
}

/* the element of definition at offset: its values turned (whole: all of
them; else those a parent that holds it as a structure did not), then its
hook, then its blocks, data and structures followed in their order */
static void element_swap(
	struct tag_swap *swap,
	struct tag_schema_definition const *definition,
	unsigned long offset,
	boolean whole,
	short depth)
{
	struct tag_swap_layout const *layout;
	struct tag_schema_field const *field;
	short index;

	if (swap->refused)
		return;
	if (depth > MAXIMUM_TAG_SWAP_DEPTH)
	{
		swap_refuse(swap, "blocks nest too deep");
		return;
	}
	layout = layout_get(swap, definition);
	if (!layout)
		return;
	if (!in_buffer(swap, offset, (unsigned long)definition->size))
	{
		swap_refuse(swap, "%s at %08lx is outside the map", definition->name, offset);
		return;
	}
	for (index = 0; index < layout->run_count && !swap->refused; index++)
	{
		struct tag_swap_run const *run = &tag_swap_runs[layout->first_run + index];

		if (whole)
			values_swap(swap, offset + run->offset, run->width, run->count);
		else
			values_swap_unless_swapped(swap, offset + run->offset, run->width, run->count);
	}
	if (swap->refused)
		return;
	switch (layout->hook)
	{
	case _tag_swap_hook_animation:
		animation_swap(swap, offset);
		break;
	case _tag_swap_hook_recorded_animation:
		recorded_animation_swap(swap, offset);
		break;
	case _tag_swap_hook_model_geometry_part:
		model_geometry_part_swap(swap, offset);
		break;
	}
	/* (each word read as its bytes are now, turned or not: a structure's
	parent may have held its values as bytes, which it turns itself) */
	for (field = definition->fields; field->type != _tag_schema_terminator && !swap->refused; field++)
	{
		unsigned long at = offset + (unsigned long)field->offset;

		switch (field->type)
		{
		case _tag_schema_block:
			block_follow(swap, definition, field, at, depth);
			break;
		case _tag_schema_data:
			data_follow(swap, definition, layout, field, offset, at);
			break;
		case _tag_schema_struct:
			for (index = 0; index < field->count && !swap->refused; index++)
			{
				element_swap(swap, field->definition, at + (unsigned long)index * (unsigned long)field->size, FALSE,
					(short)(depth + 1));
			}
			break;
		}
	}

	return;
}

/* the tag of group_tag whose root is at offset, by its group's schema; a tag
of a group no schema has is left */
static void tag_swap_tag(
	struct tag_swap *swap,
	unsigned long group_tag,
	unsigned long offset)
{
	struct tag_schema_group const *group = group_get(swap->custom_edition, group_tag);
	char text[5];

	if (!group || !group->definition)
	{
		swap->report->groups_left++;
		return;
	}
	if (!in_buffer(swap, offset, (unsigned long)group->definition->size))
	{
		swap_refuse(swap, "%s at %08lx is outside the map", group->definition->name, offset);
		return;
	}
	if (!claim(swap, offset, (unsigned long)group->definition->size))
	{
		swap_refuse(swap, "a '%s' tag at %08lx overlaps another's", group_tag_text(group_tag, text), offset);
		return;
	}
	element_swap(swap, group->definition, offset, TRUE, 0);

	return;
}

/* count vertex or index buffers (D3DResources) at address, turned */
static void buffers_swap(
	struct tag_swap *swap,
	long count,
	unsigned long address)
{
	unsigned long start;

	if (!count)
		return;
	if (count < 0 || (unsigned long)count > swap->size / BUFFER_BYTES ||
		!region_offset(swap, address, (unsigned long)count * BUFFER_BYTES, &start))
	{
		swap_refuse(swap, "its buffers are outside the tags");
		return;
	}
	values_swap(swap, start, 4, (unsigned long)count * BUFFER_WORDS);

	return;
}

/* the structure bsp whose header is at offset (its root, its buffers, then
its tags) */
static void structure_bsp_swap(
	struct tag_swap *swap,
	unsigned long offset)
{
	unsigned long root;
	unsigned long start;

	if (!in_buffer(swap, offset, STRUCTURE_BSP_HEADER_BYTES) ||
		little_u32(swap->bytes + offset + 0x14) != STRUCTURE_BSP_SIGNATURE)
	{
		swap_refuse(swap, "a structure bsp's header is damaged");
		return;
	}
	root = little_u32(swap->bytes + offset);
	values_swap(swap, offset, 4, STRUCTURE_BSP_HEADER_WORDS);
	if (!claim(swap, offset, STRUCTURE_BSP_HEADER_BYTES))
		swap_refuse(swap, "a structure bsp's header overlaps its tags");
	buffers_swap(swap, signed_u32(big_u32(swap->bytes + offset + 0x04)), big_u32(swap->bytes + offset + 0x08));
	buffers_swap(swap, signed_u32(big_u32(swap->bytes + offset + 0x0C)), big_u32(swap->bytes + offset + 0x10));
	if (swap->refused)
		return;
	if (!region_offset(swap, root, 4, &start))
	{
		swap_refuse(swap, "a structure bsp's root is outside it");
		return;
	}
	tag_swap_tag(swap, STRUCTURE_BSP_GROUP_TAG, start);
	swap->report->structure_bsps++;

	return;
}

/* the tag table of tag_count tags at address: each entry turned, and each
tag but structure bsps (which load later), the scenario's structure bsp
references kept for an Xbox map's walk */
static void tag_table_swap(
	struct tag_swap *swap,
	unsigned long address,
	long tag_count,
	long scenario_index,
	unsigned long *bsp_references,
	long *bsp_count)
{
	unsigned long table;
	long index;

	if (!region_offset(swap, address, (unsigned long)tag_count * TAG_INSTANCE_BYTES, &table))
	{
		swap_refuse(swap, "its tag table is outside the tags");
		return;
	}
	if (!claim(swap, table, (unsigned long)tag_count * TAG_INSTANCE_BYTES))
	{
		swap_refuse(swap, "its tag table overlaps its tag index");
		return;
	}
	for (index = 0; index < tag_count && !swap->refused; index++)
	{
		unsigned long entry = table + (unsigned long)index * TAG_INSTANCE_BYTES;
		unsigned long group_tag = little_u32(swap->bytes + entry);
		unsigned long base_address = little_u32(swap->bytes + entry + TAG_INSTANCE_ADDRESS_OFFSET);
		unsigned long start;
		char text[5];

		values_swap(swap, entry, 4, TAG_INSTANCE_WORDS);
		if (group_tag == STRUCTURE_BSP_GROUP_TAG || !base_address)
			continue;
		if (!region_offset(swap, base_address, 4, &start))
		{
			swap_refuse(swap, "tag %ld ('%s') is outside the tags", index, group_tag_text(group_tag, text));
			return;
		}
		/* (an Xbox map's scenario's structure bsps, read before it is turned:
		where each is in the file and loads) */
		if (bsp_references && group_tag == SCENARIO_GROUP_TAG && index == (scenario_index & 0xFFFF) &&
			in_buffer(swap, start + SCENARIO_STRUCTURE_BSPS_OFFSET, 8))
		{
			long count = signed_u32(little_u32(swap->bytes + start + SCENARIO_STRUCTURE_BSPS_OFFSET));
			unsigned long references;

			if (count > 0 && count <= MAXIMUM_XBOX_STRUCTURE_BSPS &&
				region_offset(swap, little_u32(swap->bytes + start + SCENARIO_STRUCTURE_BSPS_OFFSET + 4),
					(unsigned long)count * STRUCTURE_BSP_REFERENCE_BYTES, &references))
			{
				long reference;

				for (reference = 0; reference < count; reference++)
				{
					byte const *bytes = swap->bytes + references + (unsigned long)reference * STRUCTURE_BSP_REFERENCE_BYTES;

					bsp_references[reference * 3] = little_u32(bytes);
					bsp_references[reference * 3 + 1] = little_u32(bytes + 4);
					bsp_references[reference * 3 + 2] = little_u32(bytes + 8);
				}
				*bsp_count = count;
			}
		}
		tag_swap_tag(swap, group_tag, start);
		swap->report->tags++;
	}

	return;
}

/* ---------- public code */

unsigned long tag_swap_scratch_bytes(
	unsigned long bytes)
{
	return 2 * bit_words(bytes) * sizeof(unsigned long) + TAG_SWAP_SHARED_SLOTS * sizeof(struct tag_swap_shared);
}

boolean tag_swap_custom_edition_tags(
	void *tag_cache,
	unsigned long loaded_bytes,
	void *scratch,
	struct tag_swap_report *report)
{
	struct tag_swap swap;
	byte const *bytes = tag_cache;
	long tag_count;

	swap_begin(&swap, tag_cache, loaded_bytes, scratch, TRUE, report);
	if (loaded_bytes < CUSTOM_EDITION_TAG_INDEX_BYTES || loaded_bytes > CUSTOM_EDITION_TAG_CACHE_BYTES)
	{
		swap_refuse(&swap, "not a Custom Edition map's tags");
		return FALSE;
	}
	region_add(&swap, CUSTOM_EDITION_TAG_CACHE_ADDRESS, loaded_bytes, 0);
	tag_count = signed_u32(little_u32(bytes + TAG_INDEX_COUNT_OFFSET));
	if (little_u32(bytes + CUSTOM_EDITION_TAG_INDEX_SIGNATURE_OFFSET) != TAG_INDEX_SIGNATURE ||
		tag_count <= 0 || tag_count > MAXIMUM_TAG_COUNT)
	{
		swap_refuse(&swap, "its tag index is damaged");
		return FALSE;
	}
	values_swap(&swap, 0, 4, CUSTOM_EDITION_TAG_INDEX_WORDS);
	claim(&swap, 0, CUSTOM_EDITION_TAG_INDEX_BYTES);
	tag_table_swap(&swap, big_u32(bytes + TAG_INDEX_INSTANCES_OFFSET), tag_count, NONE, NULL, NULL);

	return !swap.refused;
}

boolean tag_swap_custom_edition_structure_bsp(
	void *bsp,
	unsigned long bsp_bytes,
	unsigned long address,
	void *scratch,
	struct tag_swap_report *report)
{
	struct tag_swap swap;

	swap_begin(&swap, bsp, bsp_bytes, scratch, TRUE, report);
	region_add(&swap, address, bsp_bytes, 0);
	structure_bsp_swap(&swap, 0);

	return !swap.refused;
}

/* the count elements of element_bytes at the address of the block at
block_offset in the tags (already turned), as offsets in them, or FALSE */
static boolean turned_block(
	byte const *tags,
	unsigned long loaded_bytes,
	unsigned long block_offset,
	unsigned long element_bytes,
	unsigned long *count,
	unsigned long *elements_offset)
{
	unsigned long address;
	unsigned long offset;

	*count = big_u32(tags + block_offset);
	address = big_u32(tags + block_offset + 4);
	*elements_offset = 0;
	if (!*count)
		return TRUE;
	offset = address - CUSTOM_EDITION_TAG_CACHE_ADDRESS;
	if (*count > CUSTOM_EDITION_TAG_CACHE_BYTES / element_bytes || address < CUSTOM_EDITION_TAG_CACHE_ADDRESS ||
		offset > loaded_bytes || *count * element_bytes > loaded_bytes - offset)
	{
		return FALSE;
	}
	*elements_offset = offset;

	return TRUE;
}

boolean tag_swap_custom_edition_model_data(
	void const *tag_cache,
	unsigned long loaded_bytes,
	void *model_data,
	unsigned long model_data_bytes,
	unsigned long index_data_offset,
	void *scratch,
	struct tag_swap_report *report)
{
	struct tag_swap swap;
	byte const *tags = tag_cache;
	unsigned long table;
	long tag_count;
	long index;

	swap_begin(&swap, model_data, model_data_bytes, scratch, TRUE, report);
	tag_count = loaded_bytes >= CUSTOM_EDITION_TAG_INDEX_BYTES ? signed_u32(big_u32(tags + TAG_INDEX_COUNT_OFFSET)) : 0;
	table = big_u32(tags + TAG_INDEX_INSTANCES_OFFSET) - CUSTOM_EDITION_TAG_CACHE_ADDRESS;
	if (tag_count <= 0 || tag_count > MAXIMUM_TAG_COUNT ||
		big_u32(tags + CUSTOM_EDITION_TAG_INDEX_SIGNATURE_OFFSET) != TAG_INDEX_SIGNATURE ||
		table > loaded_bytes || (unsigned long)tag_count * TAG_INSTANCE_BYTES > loaded_bytes - table ||
		index_data_offset > model_data_bytes)
	{
		swap_refuse(&swap, "its tags are not turned, or have no tag table");
		return FALSE;
	}
	for (index = 0; index < tag_count && !swap.refused; index++)
	{
		byte const *entry = tags + table + (unsigned long)index * TAG_INSTANCE_BYTES;
		unsigned long root = big_u32(entry + TAG_INSTANCE_ADDRESS_OFFSET) - CUSTOM_EDITION_TAG_CACHE_ADDRESS;
		unsigned long geometry_count, geometries, geometry;

		if (big_u32(entry) != GBXMODEL_GROUP_TAG || !big_u32(entry + TAG_INSTANCE_ADDRESS_OFFSET))
			continue;
		if (root > loaded_bytes || loaded_bytes - root < TAG_SWAP_GBXMODEL_BYTES ||
			!turned_block(tags, loaded_bytes, root + TAG_SWAP_GBXMODEL_GEOMETRIES_OFFSET,
				TAG_SWAP_GBXMODEL_GEOMETRIES_ELEMENT_BYTES, &geometry_count, &geometries))
		{
			swap_refuse(&swap, "model %ld's geometries are outside the tags", index);
			return FALSE;
		}
		for (geometry = 0; geometry < geometry_count && !swap.refused; geometry++)
		{
			unsigned long part_count, parts, part;

			if (!turned_block(tags, loaded_bytes,
				geometries + geometry * TAG_SWAP_GBXMODEL_GEOMETRIES_ELEMENT_BYTES + TAG_SWAP_GBXMODEL_GEOMETRY_PARTS_OFFSET,
				TAG_SWAP_GBXMODEL_GEOMETRY_PARTS_ELEMENT_BYTES, &part_count, &parts))
			{
				swap_refuse(&swap, "model %ld's parts are outside the tags", index);
				return FALSE;
			}
			for (part = 0; part < part_count && !swap.refused; part++)
			{
				byte const *fields = tags + parts + part * TAG_SWAP_GBXMODEL_GEOMETRY_PARTS_ELEMENT_BYTES;
				unsigned long strip_triangle_count = big_u32(fields + TAG_SWAP_GBXMODEL_PART_STRIP_TRIANGLE_COUNT_OFFSET);
				unsigned long strip_offset = big_u32(fields + TAG_SWAP_GBXMODEL_PART_STRIP_OFFSET_OFFSET);
				unsigned long vertex_count = big_u32(fields + TAG_SWAP_GBXMODEL_PART_VERTEX_COUNT_OFFSET);
				unsigned long vertex_offset = big_u32(fields + TAG_SWAP_GBXMODEL_PART_VERTEX_OFFSET_OFFSET);
				unsigned long strip_start = index_data_offset + strip_offset;
				unsigned long vertex;

				if (vertex_offset > model_data_bytes ||
					vertex_count > (model_data_bytes - vertex_offset) / GBXMODEL_VERTEX_BYTES ||
					strip_offset > model_data_bytes - index_data_offset ||
					strip_triangle_count > 0xFFFFFFFFUL - 2 ||
					strip_triangle_count + 2 > (model_data_bytes - strip_start) / 2)
				{
					swap_refuse(&swap, "part %lu of model %ld lies outside the model data", part, index);
					return FALSE;
				}
				for (vertex = 0; vertex < vertex_count && !swap.refused; vertex++)
				{
					unsigned long at = vertex_offset + vertex * GBXMODEL_VERTEX_BYTES;

					values_swap_unless_swapped(&swap, at, 4, 14);
					values_swap_unless_swapped(&swap, at + 56, 2, 2);
					values_swap_unless_swapped(&swap, at + 60, 4, 2);
				}
				values_swap_unless_swapped(&swap, strip_start, 2, strip_triangle_count + 2);
				report->model_parts++;
			}
		}
	}

	return !swap.refused;
}

boolean tag_swap_xbox_map(
	void *map,
	unsigned long map_bytes,
	void *scratch,
	struct tag_swap_report *report)
{
	static unsigned long const header_words[] = { 0x00, 0x04, 0x08, 0x10, 0x14, 0x64, 0x7FC };
	struct tag_swap swap;
	byte const *bytes = map;
	unsigned long bsp_references[MAXIMUM_XBOX_STRUCTURE_BSPS * 3];
	long bsp_count = 0;
	unsigned long tag_data_offset, tag_data_size, tags;
	long tag_count, bsp;
	short index;

	swap_begin(&swap, map, map_bytes, scratch, FALSE, report);
	if (map_bytes < XBOX_HEADER_BYTES || little_u32(bytes) != CACHE_HEADER_SIGNATURE ||
		little_u32(bytes + 0x08) != map_bytes)
	{
		swap_refuse(&swap, "not an inflated Xbox map");
		return FALSE;
	}
	tag_data_offset = little_u32(bytes + 0x10);
	tag_data_size = little_u32(bytes + 0x14);
	if (tag_data_offset < XBOX_HEADER_BYTES || !tag_data_size || tag_data_size > 0x7FFFFFFFUL ||
		!in_buffer(&swap, tag_data_offset, tag_data_size))
	{
		swap_refuse(&swap, "its tag data is outside it");
		return FALSE;
	}
	for (index = 0; index < NUMBEROF(header_words); index++)
		values_swap(&swap, header_words[index], 4, 1);
	values_swap(&swap, 0x60, 2, 1);
	region_add(&swap, XBOX_TAG_CACHE_ADDRESS, tag_data_size, tag_data_offset);

	tags = tag_data_offset;
	tag_count = signed_u32(little_u32(bytes + tags + TAG_INDEX_COUNT_OFFSET));
	if (tag_data_size < XBOX_TAG_HEADER_WORDS * 4 ||
		little_u32(bytes + tags + XBOX_TAG_HEADER_SIGNATURE_OFFSET) != TAG_INDEX_SIGNATURE ||
		tag_count <= 0 || tag_count > MAXIMUM_TAG_COUNT)
	{
		swap_refuse(&swap, "its tag header is damaged");
		return FALSE;
	}
	values_swap(&swap, tags, 4, XBOX_TAG_HEADER_WORDS);
	claim(&swap, tags, XBOX_TAG_HEADER_WORDS * 4);
	/* (the tag table first, then the buffers, as map_convert.py checks them) */
	{
		unsigned long table;

		if (!region_offset(&swap, big_u32(bytes + tags), (unsigned long)tag_count * TAG_INSTANCE_BYTES, &table))
		{
			swap_refuse(&swap, "its tag table is outside the tags");
			return FALSE;
		}
	}
	buffers_swap(&swap, signed_u32(big_u32(bytes + tags + 0x10)), big_u32(bytes + tags + 0x14));
	buffers_swap(&swap, signed_u32(big_u32(bytes + tags + 0x18)), big_u32(bytes + tags + 0x1C));
	if (!swap.refused)
	{
		tag_table_swap(&swap, big_u32(bytes + tags), tag_count, signed_u32(big_u32(bytes + tags + 0x04)),
			bsp_references, &bsp_count);
	}

	/* each structure bsp where it loads */
	for (bsp = 0; bsp < bsp_count && !swap.refused; bsp++)
	{
		long file_offset = signed_u32(bsp_references[bsp * 3]);
		long file_size = signed_u32(bsp_references[bsp * 3 + 1]);

		if (file_offset < 0 || file_size <= 0 || !in_buffer(&swap, (unsigned long)file_offset, (unsigned long)file_size))
		{
			swap_refuse(&swap, "a structure bsp is outside it");
			break;
		}
		region_add(&swap, bsp_references[bsp * 3 + 2], (unsigned long)file_size, (unsigned long)file_offset);
		structure_bsp_swap(&swap, (unsigned long)file_offset);
		swap.region_count--;
	}

	return !swap.refused;
}
