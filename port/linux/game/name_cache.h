/*
NAME_CACHE.H

What a lookup of a name in a tag's block (an animation of a graph by name, a node or a marker of a model by name) answered, kept for the next time it is asked:
a name is looked for with a string compare against each entry of the block (a biped's graph has several hundred animations), and the same few names are asked for
by the same few tags every tick. A tag's blocks do not change while a map is loaded, so an answer is good until the next map (halo_name_cache_new_map, from the
objects' start of a map); the answer is exactly what the search gave, so nothing a tick computes changes. Wii U only (HALO_WIIU): the callers ask in an #ifdef.
*/
#ifndef NAME_CACHE_H
#define NAME_CACHE_H

enum halo_name_cache_kind
{
	HALO_NAME_CACHE_ANIMATION, /* animation_graph_get_animation_by_name: the name compared without regard to case */
	HALO_NAME_CACHE_NODE,      /* model_find_node: the name compared exactly */
	HALO_NAME_CACHE_MARKER,    /* model_find_marker: the name compared without regard to case */
	HALO_NAME_CACHE_ENHANCED,  /* units.c's unit_animation_enhanced_unit: the unit's tag (an empty name) against the tag names of the player bipeds */
};

/* TRUE with the answer in *result if this tag (the graph's or the model's tag index) was asked this name this map */
int halo_name_cache_lookup(enum halo_name_cache_kind kind, long tag_index, char const *name, short *result);
void halo_name_cache_store(enum halo_name_cache_kind kind, long tag_index, char const *name, short result);
/* the tags are other ones: nothing asked before is good */
void halo_name_cache_new_map(void);
/* what it has answered and what it has not, since the last map (the log's line) */
void halo_name_cache_counts(unsigned long *hits, unsigned long *misses);

#endif
