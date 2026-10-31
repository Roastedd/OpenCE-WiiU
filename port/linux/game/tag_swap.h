/*
TAG_SWAP.H

A map's tags turned from the little-endian order of its file to big-endian,
the order of the big-endian builds (the Wii U's): a Halo Custom Edition map's
tags, structure BSPs and model data as they load (custom_edition_cache.c,
cache_files.c), which the big-endian builds cannot convert ahead of time (the
map's hash is its network version, and map sharing sends its bytes as they
are). The Xbox maps are converted ahead of time, by tools/wiiu/map_convert.py;
this is that converter's walk, so that both turn the same values:

- what a tag's values are comes from the schemas the validator walks
  (tag_schema.h: each group's root definition, and each definition's blocks,
  data and structures) and from the scalar layouts of their structures
  (tag_swap_layouts.inc, made by tools/wiiu/tag_swap_tables.py from the files
  map_convert.py converts with): every value of 2, 4 or 8 bytes is turned;
- a data field is turned as what it holds (text, 16- or 32-bit values, a
  material's vertices, the scripts' syntax nodes), and an animation's frames,
  a recorded animation's event stream and a model part's indices are walked
  as the game reads them;
- a Custom Edition map's gbxmodels ('mod2', tag_schema_custom_edition_groups)
  are turned by their own schema, and their parts' vertices and strips in the
  map's model data by the parts' fields.

Maps are untrusted: every block and data is checked to lie in the tags (or
the bsp) before it is followed, no root, block or data may overlap another
(as the validator requires), and no byte is turned twice; a map that breaks
any of that is refused, with why, before anything else reads its tags.

The swap turns little-endian into big-endian whatever the host's order: a
big-endian host then reads the tags as its own (the game swaps only there),
and tools/map_validate.c runs it on any host to compare it with
map_convert.py's (--swap).
*/

#ifndef __TAG_SWAP_H
#define __TAG_SWAP_H
#pragma once

/* ---------- constants */

enum
{
	MAXIMUM_TAG_SWAP_PROBLEM_LENGTH = 192,
};

/* ---------- structures */

/* what a swap did, and why it refused when it did */
struct tag_swap_report
{
	long tags;
	long structure_bsps;
	long model_parts;
	/* the values turned, of 2, 4 and 8 bytes */
	unsigned long shorts;
	unsigned long words;
	unsigned long doubles;
	/* the bytes turned or passed as bytes (text, flags) */
	unsigned long bytes_visited;
	/* what is left as it is: tags of groups no schema has, the bytes of data
	no handler knows and of what a hook could not walk to its end */
	long groups_left;
	unsigned long data_bytes_left;
	char problem[MAXIMUM_TAG_SWAP_PROBLEM_LENGTH];
};

/* ---------- prototypes/TAG_SWAP.C */

/* the scratch memory a swap of `bytes` bytes takes (the caller's, for as
long as the swap runs: the swap allocates nothing) */
unsigned long tag_swap_scratch_bytes(
	unsigned long bytes);

/* A Custom Edition map's tags (`loaded_bytes` at tag_cache, which stands for
the tag cache at 0x40440000), as its loader and conversion leave them
(cache_file_formats.c): little-endian, turned to big-endian in place. FALSE
when the tags cannot be walked (report->problem says why): they are then
partly turned, and must not be used. */
boolean tag_swap_custom_edition_tags(
	void *tag_cache,
	unsigned long loaded_bytes,
	void *scratch,
	struct tag_swap_report *report);

/* a Custom Edition map's structure bsp, `bsp_bytes` read to `bsp` from the
map, which loads at `address` */
boolean tag_swap_custom_edition_structure_bsp(
	void *bsp,
	unsigned long bsp_bytes,
	unsigned long address,
	void *scratch,
	struct tag_swap_report *report);

/* A Custom Edition map's model data (`model_data_bytes` at model_data: its
parts' vertices, then from `index_data_offset` their strips), by the parts of
its gbxmodels in its tags (`loaded_bytes` at tag_cache, already turned): each
part's vertices (68 bytes: 14 floats, two node indices, two weights) and
strip (16-bit indices), what several parts name turned once. */
boolean tag_swap_custom_edition_model_data(
	void const *tag_cache,
	unsigned long loaded_bytes,
	void *model_data,
	unsigned long model_data_bytes,
	unsigned long index_data_offset,
	void *scratch,
	struct tag_swap_report *report);

/* An Xbox map, inflated (`map_bytes` at map), turned as
tools/wiiu/map_convert.py turns it (without its --rebase): its header, its
tags, its buffers and its structure bsps. For tools: the game reads the Xbox
maps converted ahead of time. */
boolean tag_swap_xbox_map(
	void *map,
	unsigned long map_bytes,
	void *scratch,
	struct tag_swap_report *report);

#endif // __TAG_SWAP_H
