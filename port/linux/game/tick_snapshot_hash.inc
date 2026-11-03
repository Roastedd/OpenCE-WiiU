/*
TICK_SNAPSHOT_HASH.INC

A test aid, included at the end of tick_snapshot.c (mode bit 8192 of halo/tick_snapshot.txt, HALO_TICK_SNAPSHOT on the host): each frame drawn from the ring says a hash of what the frame
can read of the two captures it is drawn from, the object blocks (without the words the renderer's side owns, the marker and the cached render state, and without the orientation
blocks, which only the ticks read), the objects' function inputs, the header array, the cluster lists and the lights. Two builds that copy differently, or the same build twice,
that say the same hash for a tick have given the frame the same world; the pictures cannot show that where the two threads draw from one random seed (a frame drawn beside the
tick thread differs from run to run in its particles), and this does.
*/

#define HASH_START 0xcbf29ce484222325ULL
#define HASH_PRIME 0x100000001b3ULL

static unsigned long long tick_snapshot_hash_word(unsigned long long hash, unsigned long word)
{
	return (hash ^ (unsigned long long)word) * HASH_PRIME;
}

static unsigned long long tick_snapshot_hash_bytes(unsigned long long hash, void const *data, long bytes)
{
	unsigned long const *words = (unsigned long const *)data;
	long index;

	for (index = 0; index < bytes / 4; index++)
		hash = tick_snapshot_hash_word(hash, words[index]);
	return hash;
}

/* the parts: the header array, the objects' structures, the type's own data, the node matrices, the function inputs, the cluster lists, the lights */
enum { HASH_HEADERS, HASH_STRUCTURES, HASH_TYPE_DATA, HASH_MATRICES, HASH_INPUTS, HASH_CLUSTERS, HASH_LIGHTS, HASH_PARTS };

static void tick_snapshot_hash_slot(struct slot const *slot, int slot_index, unsigned long long parts[HASH_PARTS])
{
	long index;
	int part;

	for (part = 0; part < HASH_PARTS; part++)
		parts[part] = HASH_START;
	parts[HASH_HEADERS] = tick_snapshot_hash_word(parts[HASH_HEADERS], (unsigned long)slot->array.count);
	for (index = 0; index < slot->array.count && index < MAXIMUM_OBJECTS_PER_MAP; index++)
	{
		struct object_header_datum const *header = &slot->headers[index];
		unsigned long long *hash = &parts[HASH_HEADERS];

		*hash = tick_snapshot_hash_word(*hash, (unsigned long)(unsigned short)header->identifier);
		if (!header->identifier)
			continue;
		*hash = tick_snapshot_hash_word(*hash, ((unsigned long)header->flags << 8) | header->type);
		*hash = tick_snapshot_hash_word(*hash, (unsigned long)(unsigned short)header->cluster_index);
		*hash = tick_snapshot_hash_word(*hash, (unsigned long)(unsigned short)header->data_size);
		*hash = tick_snapshot_hash_word(*hash, slot->copied[index] ? 1UL : 0UL);
		if (slot->copied[index] && header->datum)
		{
			unsigned long const *words = (unsigned long const *)header->datum;
			struct object_datum const *block = header->datum;
			const long size = header->data_size;
			const long marker = (long)offsetof(struct object_datum, object.magic_number);
			const long cached = (long)offsetof(struct object_datum, object.cached_render_state_index);
			const long matrices_offset = block->object.node_matrices.offset, matrices_size = block->object.node_matrices.size;
			long word;

			for (word = 0; word < size / 4; word++)
			{
				const long offset = word * 4;
				int which;
				boolean left_out = offset == marker || offset == cached;

				for (which = 0; which < 2 && !left_out; which++)
				{
					struct object_header_block_reference const *reference = which ? &block->object.original_node_orientations : &block->object.node_orientations;

					if (reference->size > 0 && reference->offset >= (short)sizeof(struct object_datum) && offset >= reference->offset &&
						offset < (long)reference->offset + reference->size)
					{
						left_out = TRUE;
					}
				}
				if (!left_out)
				{
					const int which_part = offset < (long)sizeof(struct object_datum) ? HASH_STRUCTURES :
						(matrices_size > 0 && offset >= matrices_offset && offset < matrices_offset + matrices_size) ? HASH_MATRICES : HASH_TYPE_DATA;

					parts[which_part] = tick_snapshot_hash_word(parts[which_part], words[word]);
				}
			}
			parts[HASH_INPUTS] = tick_snapshot_hash_word(parts[HASH_INPUTS], object_function_inputs_checksum(slot_index, index));
		}
	}
	for (index = 0; index < PARTITION_COUNT; index++)
	{
		struct partition_copy const *copy = &slot->partitions[index];
		unsigned long long *hash = &parts[HASH_CLUSTERS];

		*hash = tick_snapshot_hash_word(*hash, copy->valid ? 1UL : 0UL);
		if (!copy->valid)
			continue;
		*hash = tick_snapshot_hash_bytes(*hash, copy->first_references, sizeof(copy->first_references));
		*hash = tick_snapshot_hash_word(*hash, (unsigned long)copy->references.count);
		*hash = tick_snapshot_hash_bytes(*hash, copy->entries, (long)copy->references.count * (long)sizeof(copy->entries[0]));
		*hash = tick_snapshot_hash_word(*hash, (unsigned long)copy->cluster_references.count);
		*hash = tick_snapshot_hash_bytes(*hash, copy->cluster_entries, (long)copy->cluster_references.count * (long)sizeof(copy->cluster_entries[0]));
	}
	parts[HASH_LIGHTS] = tick_snapshot_hash_word(parts[HASH_LIGHTS], slot->lights_valid ? 1UL : 0UL);
	if (slot->lights_valid)
		parts[HASH_LIGHTS] = tick_snapshot_hash_bytes(parts[HASH_LIGHTS], slot->light_bytes, (long)slot->lights.count * (long)slot->lights.size);
}

/* HALO_HASH_DETAIL=tick: for the frame drawn from the capture of that tick, a line for each object with its block's hashes by part, and for a unit-sized block the words of the type's data */
static void tick_snapshot_hash_detail(struct slot const *slot, long tick)
{
	long index;

	for (index = 0; index < slot->array.count && index < MAXIMUM_OBJECTS_PER_MAP; index++)
	{
		struct object_header_datum const *header = &slot->headers[index];
		unsigned long const *words;
		long word, size;
		unsigned long long fixed = HASH_START, type = HASH_START;
		const long marker = (long)offsetof(struct object_datum, object.magic_number);
		const long cached = (long)offsetof(struct object_datum, object.cached_render_state_index);

		if (!header->identifier || !slot->copied[index] || !header->datum)
			continue;
		words = (unsigned long const *)header->datum;
		size = header->data_size;
		for (word = 0; word < size / 4; word++)
		{
			const long offset = word * 4;
			struct object_datum const *block = header->datum;
			boolean left_out = offset == marker || offset == cached;
			int which;

			for (which = 0; which < 2 && !left_out; which++)
			{
				struct object_header_block_reference const *reference = which ? &block->object.original_node_orientations : &block->object.node_orientations;

				if (reference->size > 0 && reference->offset >= (short)sizeof(struct object_datum) && offset >= reference->offset && offset < (long)reference->offset + reference->size)
					left_out = TRUE;
			}
			if (left_out)
				continue;
			if (offset < (long)sizeof(struct object_datum))
				fixed = tick_snapshot_hash_word(fixed, words[word]);
			else if (!(block->object.node_matrices.size > 0 && offset >= block->object.node_matrices.offset && offset < block->object.node_matrices.offset + block->object.node_matrices.size))
				type = tick_snapshot_hash_word(type, words[word]);
		}
		platform_log("HASHDETAIL tick %ld object %ld type %d size %ld structure %08llx typedata %08llx", tick, index, (int)header->type, size, fixed & 0xffffffffULL, type & 0xffffffffULL);
		if ((int)header->type <= 1 || (int)header->type == 2)
		{
			/* (a unit or weapon: the type's own words, to say which differ) */
			for (word = (long)sizeof(struct object_datum) / 4; word < size / 4; word++)
			{
				struct object_datum const *block = header->datum;
				const long offset = word * 4;

				if ((block->object.node_matrices.size > 0 && offset >= block->object.node_matrices.offset && offset < block->object.node_matrices.offset + block->object.node_matrices.size) ||
					(block->object.node_orientations.size > 0 && offset >= block->object.node_orientations.offset && offset < block->object.node_orientations.offset + block->object.node_orientations.size) ||
					(block->object.original_node_orientations.size > 0 && offset >= block->object.original_node_orientations.offset && offset < block->object.original_node_orientations.offset + block->object.original_node_orientations.size))
					continue;
				platform_log("HASHWORD %ld %ld +%ld %08lx", tick, index, offset, words[word]);
			}
		}
	}
}

/* the frame's view has begun on a pair of the ring: say the hashes of both, by part (the parts of the latest, then the previous's whole) */
static void tick_snapshot_hash_log(void)
{
	if ((s.mode & MODE_HASH) && s.view)
	{
		const int latest = (int)(s.view - slots);
		const int previous = s.previous ? (int)(s.previous - slots) : -1;
		unsigned long long latest_parts[HASH_PARTS], previous_parts[HASH_PARTS];
		unsigned long long previous_all = 0;
		int part;

		tick_snapshot_hash_slot(s.view, latest, latest_parts);
		{
			static long detail = -2;

			if (detail == -2)
			{
				char const *variable = getenv("HALO_HASH_DETAIL");

				detail = variable && *variable ? strtol(variable, NULL, 10) : -1;
			}
			if (detail >= 0 && s.ring.tick[latest] == detail)
				tick_snapshot_hash_detail(s.view, detail);
		}
		if (previous >= 0)
		{
			tick_snapshot_hash_slot(s.previous, previous, previous_parts);
			for (part = 0; part < HASH_PARTS; part++)
				previous_all = previous_all * HASH_PRIME ^ previous_parts[part];
		}
		platform_log("tick snapshot HASH tick %ld: latest headers %08llx structures %08llx type %08llx matrices %08llx inputs %08llx clusters %08llx lights %08llx previous %08llx",
			s.ring.tick[latest], latest_parts[0] & 0xffffffffULL, latest_parts[1] & 0xffffffffULL, latest_parts[2] & 0xffffffffULL, latest_parts[3] & 0xffffffffULL,
			latest_parts[4] & 0xffffffffULL, latest_parts[5] & 0xffffffffULL, latest_parts[6] & 0xffffffffULL, previous_all & 0xffffffffULL);
	}
}
