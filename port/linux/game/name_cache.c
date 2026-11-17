/*
NAME_CACHE.C

See name_cache.h. A table of 512 entries, an entry for each (kind, tag, name) by a hash of them: an entry another name has taken is asked again of the block and
takes it back. Names over 47 characters are not kept. The game thread asks, and the tick thread (one at a time: the tick overlap hands the game over, never both at once).
*/

#include "name_cache.h"

#include <string.h>

#define ENTRIES 512
#define NAME_LENGTH 48

static struct
{
	unsigned long generation;
	long tag_index;
	short result;
	unsigned char kind;
	unsigned char used;
	char name[NAME_LENGTH];
} table[ENTRIES];

static unsigned long generation = 1;
static unsigned long hit_count, miss_count;

static unsigned char fold(unsigned char c)
{
	return c >= 'A' && c <= 'Z' ? (unsigned char)(c + 32) : c;
}

/* the hash of the name (folded for the kinds that compare without case), and whether it fits an entry */
static int hash_of(enum halo_name_cache_kind kind, long tag_index, char const *name, unsigned long *hash)
{
	unsigned long h = 2166136261ul ^ (unsigned long)kind;
	size_t length = 0;
	const int folded = kind != HALO_NAME_CACHE_NODE;

	h = (h * 16777619ul) ^ (unsigned long)tag_index;
	for (; name[length]; length++)
	{
		if (length >= NAME_LENGTH - 1)
			return 0;
		h = (h * 16777619ul) ^ (folded ? fold((unsigned char)name[length]) : (unsigned char)name[length]);
	}
	*hash = h ^ (h >> 15);
	return 1;
}

static int same_name(enum halo_name_cache_kind kind, char const *a, char const *b)
{
	if (kind == HALO_NAME_CACHE_NODE)
		return !strcmp(a, b);
	for (; *a && *b; a++, b++)
	{
		if (fold((unsigned char)*a) != fold((unsigned char)*b))
			return 0;
	}
	return *a == *b;
}

int halo_name_cache_lookup(enum halo_name_cache_kind kind, long tag_index, char const *name, short *result)
{
	unsigned long hash;

	if (name && hash_of(kind, tag_index, name, &hash))
	{
		const unsigned long slot = hash % ENTRIES;

		if (table[slot].used && table[slot].generation == generation && table[slot].kind == kind && table[slot].tag_index == tag_index &&
			same_name(kind, table[slot].name, name))
		{
			*result = table[slot].result;
			hit_count++;
			return 1;
		}
	}
	miss_count++;
	return 0;
}

void halo_name_cache_store(enum halo_name_cache_kind kind, long tag_index, char const *name, short result)
{
	unsigned long hash;

	if (name && hash_of(kind, tag_index, name, &hash))
	{
		const unsigned long slot = hash % ENTRIES;

		table[slot].used = 1;
		table[slot].generation = generation;
		table[slot].kind = (unsigned char)kind;
		table[slot].tag_index = tag_index;
		table[slot].result = result;
		strncpy(table[slot].name, name, NAME_LENGTH - 1);
		table[slot].name[NAME_LENGTH - 1] = 0;
	}
}

void halo_name_cache_new_map(void)
{
	generation++;
	hit_count = miss_count = 0;
}

void halo_name_cache_counts(unsigned long *hits, unsigned long *misses)
{
	*hits = hit_count;
	*misses = miss_count;
}
