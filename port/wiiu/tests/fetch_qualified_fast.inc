struct fetch_attribute_key
{
	uint32_t words[8];
};

/* The public WUT stream has eight contiguous 32-bit fields. Keys own all
of those bytes; no compiler padding, shader identity or borrowed data enters
identity. These checks deliberately reject a changed input ABI. */
typedef char fetch_key_size[(sizeof(struct fetch_attribute_key)==32 && sizeof(GX2AttribStream)==32)?1:-1];
typedef char fetch_stream_offsets[(offsetof(GX2AttribStream,location)==0 && offsetof(GX2AttribStream,buffer)==4 &&
 offsetof(GX2AttribStream,offset)==8 && offsetof(GX2AttribStream,format)==12 && offsetof(GX2AttribStream,type)==16 &&
 offsetof(GX2AttribStream,aluDivisor)==20 && offsetof(GX2AttribStream,mask)==24 && offsetof(GX2AttribStream,endianSwap)==28)?1:-1];

struct fetch
{
	uint32_t key;
	uint16_t next, count; /* table slot plus one; zero ends a bucket */
	GX2FetchShader shader;
	void *program;
	struct fetch_attribute_key attributes[];
};

#define FETCH_BUCKETS 256
typedef char fetch_bucket_count_is_power_of_two[(FETCH_BUCKETS & (FETCH_BUCKETS - 1)) == 0 ? 1 : -1];
typedef char fetch_slots_fit[FETCH_LIMIT < 65536 ? 1 : -1];

static struct fetch *fetch_table[FETCH_LIMIT];
static uint16_t fetch_heads[FETCH_BUCKETS];
static unsigned fetch_count;

static void fetch_attribute_key_make(struct fetch_attribute_key *out, const GX2AttribStream *attribute)
{
	memcpy(out, attribute, sizeof(*out));
}

/* A word hash narrows the search; collisions never identify a layout. */
static uint32_t fetch_layout_hash(const GX2AttribStream *attributes, unsigned count)
{
	uint32_t hash = (2166136261u ^ count) * 16777619u;
	unsigned index;

	/* Only a bucket filter: type/divisor and separate mask/swap identity are
	checked by the complete owned key even when this filter collides. */
	for (index = 0; index < count; index++)
	{
		const GX2AttribStream *attribute = &attributes[index];

		hash = (hash ^ (attribute->location | (attribute->buffer << 8))) * 16777619u;
		hash = (hash ^ attribute->offset) * 16777619u;
		hash = (hash ^ (uint32_t)attribute->format) * 16777619u;
		hash = (hash ^ (attribute->mask ^ (uint32_t)attribute->endianSwap)) * 16777619u;
	}
	hash ^= hash >> 16;
	hash ^= hash >> 8;
	return hash;
}

static int fetch_layout_equal(const struct fetch *entry, const GX2AttribStream *attributes, unsigned count)
{
	unsigned index;

	if (entry->count != count)
		return 0;
	for (index = 0; index < count; index++)
	{
		const uint32_t *words = entry->attributes[index].words;
		const GX2AttribStream *attribute = &attributes[index];

		if (words[0] != attribute->location || words[1] != attribute->buffer || words[2] != attribute->offset ||
			words[3] != (uint32_t)attribute->format || words[4] != (uint32_t)attribute->type || words[5] != attribute->aluDivisor ||
			words[6] != attribute->mask || words[7] != (uint32_t)attribute->endianSwap)
			return 0;
	}
	return 1;
}

static unsigned attributes_make(const struct shader *vs, GX2AttribStream *out)
{
	unsigned location, count = 0;

	for (location = 0; location < DK_ATTRIBUTE_COUNT; location++)
	{
		const struct dk_attribute *attribute = &gx.format.attributes[location];
		struct attribute_spec spec;
		GX2AttribStream *stream;

		if (!(vs->attribute_used & (1u << location)))
			continue;
		stream = &out[count++];
		memset(stream, 0, sizeof(*stream));
		stream->location = location;
		stream->type = GX2_ATTRIB_INDEX_PER_VERTEX;
		if (attribute_spec(attribute->format, (gx.format.little_endian >> attribute->stream) & 1, &spec))
		{
			stream->buffer = attribute->stream;
			stream->offset = attribute->offset;
		}
		else
		{
			/* a register the declaration does not feed: its current value, from the constants' stream (stride 0) */
			stream->buffer = CONSTANTS_BUFFER;
			stream->offset = location * 16;
			if (attribute->format == DK_ATTRIBUTE_UNFED_PACKED)
				attribute_spec(DK_ATTRIBUTE_PACKED, 0, &spec);
			else
				attribute_spec(DK_ATTRIBUTE_FLOAT4, 0, &spec);
		}
		stream->format = spec.format;
		stream->mask = spec.mask;
		stream->endianSwap = spec.swap;
	}
	return count;
}

static struct fetch *fetch_get_layout(const GX2AttribStream *attributes, unsigned count)
{
	struct fetch *fetch;
	uint32_t key, bucket, size;
	unsigned slot, remaining = FETCH_LIMIT, index;

	if (count > DK_ATTRIBUTE_COUNT)
		return NULL;
	key = fetch_layout_hash(attributes, count);
	bucket = key & (FETCH_BUCKETS - 1);
	for (slot = fetch_heads[bucket]; slot && remaining; slot = fetch->next, remaining--)
	{
		/* Links are private to this consumer and published only after a
		complete allocation. Keep even a damaged chain within the table. */
		if (slot > fetch_count)
			return NULL;
		fetch = fetch_table[slot - 1];
		if (fetch->key == key && fetch_layout_equal(fetch, attributes, count))
			return fetch;
	}
	if (slot) /* a cycle cannot be produced by this cache */
		return NULL;
	if (fetch_count == FETCH_LIMIT)
	{
		if (!gx.fetch_full)
			wiiu_log("GX2: more than %d fetch shaders", FETCH_LIMIT);
		gx.fetch_full = 1;
		return NULL;
	}
	fetch = calloc(1, sizeof(*fetch) + count * sizeof(fetch->attributes[0]));
	if (!fetch)
		return NULL;
	size = GX2CalcFetchShaderSizeEx(count, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
	fetch->program = gx_alloc(size, GX2_SHADER_PROGRAM_ALIGNMENT);
	if (!fetch->program)
	{
		free(fetch);
		return NULL;
	}
	GX2InitFetchShaderEx(&fetch->shader, (uint8_t *)fetch->program, count, attributes, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, fetch->program, size);
	fetch->key = key;
	fetch->count = (uint16_t)count;
	for (index = 0; index < count; index++)
		fetch_attribute_key_make(&fetch->attributes[index], &attributes[index]);
	fetch->next = fetch_heads[bucket];
	fetch_table[fetch_count++] = fetch;
	fetch_heads[bucket] = (uint16_t)fetch_count;
	return fetch;
}

static struct fetch *fetch_get(const struct shader *vs)
{
	GX2AttribStream attributes[DK_ATTRIBUTE_COUNT];
	unsigned count = attributes_make(vs, attributes);

	return fetch_get_layout(attributes, count);
}



/* QUALIFIED_FETCH_BINDING */
	if (gx.cur_fetch != fetch || gx.cur_vs != vs)
	{
		gx.counts_fetch++;
		GX2SetFetchShader(&fetch->shader);
		gx.cur_fetch = fetch;
	}
	if (gx.cur_vs != vs)
	{
		gx.counts_vs++;
		GX2SetVertexShader(vs->vertex);
		gx.cur_vs = vs;
	}
