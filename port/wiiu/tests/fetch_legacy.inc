/* Frozen own-source descriptor oracle, OpenCE b5af9cf. */
#define ATTRIB_INTEGER_32 ((GX2AttribFormat)(GX2_ATTRIB_FLAG_INTEGER | GX2_ATTRIB_TYPE_32))
#define ATTRIB_SSCALED_16 ((GX2AttribFormat)(GX2_ATTRIB_FLAG_SIGNED | GX2_ATTRIB_FLAG_SCALED | GX2_ATTRIB_TYPE_16))
#define ATTRIB_SSCALED_16_16 ((GX2AttribFormat)(GX2_ATTRIB_FLAG_SIGNED | GX2_ATTRIB_FLAG_SCALED | GX2_ATTRIB_TYPE_16_16))
#define ATTRIB_SSCALED_16x4 ((GX2AttribFormat)(GX2_ATTRIB_FLAG_SIGNED | GX2_ATTRIB_FLAG_SCALED | GX2_ATTRIB_TYPE_16_16_16_16))
#define ATTRIB_SNORM_16 ((GX2AttribFormat)(GX2_ATTRIB_FLAG_SIGNED | GX2_ATTRIB_TYPE_16))
#define ATTRIB_SNORM_16_16 ((GX2AttribFormat)(GX2_ATTRIB_FLAG_SIGNED | GX2_ATTRIB_TYPE_16_16))
#define ATTRIB_SNORM_16x4 ((GX2AttribFormat)(GX2_ATTRIB_FLAG_SIGNED | GX2_ATTRIB_TYPE_16_16_16_16))

#define SELECT(x, y, z, w) GX2_SEL_MASK(GX2_SQ_SEL_##x, GX2_SQ_SEL_##y, GX2_SQ_SEL_##z, GX2_SQ_SEL_##w)



struct baseline_fetch
{
	uint64_t key;
	GX2FetchShader shader;
	void *program;
};

static int baseline_attribute_spec(uint32_t dk, int little_endian, struct attribute_spec *spec)
{
	const GX2EndianSwapMode word = little_endian ? GX2_ENDIAN_SWAP_NONE : GX2_ENDIAN_SWAP_8_IN_32;
	const GX2EndianSwapMode half = little_endian ? GX2_ENDIAN_SWAP_NONE : GX2_ENDIAN_SWAP_8_IN_16;

	switch (dk)
	{
	case DK_ATTRIBUTE_FLOAT1: *spec = (struct attribute_spec){ GX2_ATTRIB_FORMAT_FLOAT_32, SELECT(X, 0, 0, 1), word }; return 1;
	case DK_ATTRIBUTE_FLOAT2: *spec = (struct attribute_spec){ GX2_ATTRIB_FORMAT_FLOAT_32_32, SELECT(X, Y, 0, 1), word }; return 1;
	case DK_ATTRIBUTE_FLOAT3: *spec = (struct attribute_spec){ GX2_ATTRIB_FORMAT_FLOAT_32_32_32, SELECT(X, Y, Z, 1), word }; return 1;
	case DK_ATTRIBUTE_FLOAT4: *spec = (struct attribute_spec){ GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32, SELECT(X, Y, Z, W), word }; return 1;
	case DK_ATTRIBUTE_COLOR: *spec = (struct attribute_spec){ GX2_ATTRIB_FORMAT_UNORM_8_8_8_8, SELECT(Z, Y, X, W), word }; return 1;
	case DK_ATTRIBUTE_SHORT1: *spec = (struct attribute_spec){ ATTRIB_SSCALED_16, SELECT(X, 0, 0, 1), half }; return 1;
	case DK_ATTRIBUTE_SHORT2: *spec = (struct attribute_spec){ ATTRIB_SSCALED_16_16, SELECT(X, Y, 0, 1), half }; return 1;
	case DK_ATTRIBUTE_SHORT3: *spec = (struct attribute_spec){ ATTRIB_SSCALED_16x4, SELECT(X, Y, Z, 1), half }; return 1;
	case DK_ATTRIBUTE_SHORT4: *spec = (struct attribute_spec){ ATTRIB_SSCALED_16x4, SELECT(X, Y, Z, W), half }; return 1;
	case DK_ATTRIBUTE_NORMSHORT1: *spec = (struct attribute_spec){ ATTRIB_SNORM_16, SELECT(X, 0, 0, 1), half }; return 1;
	case DK_ATTRIBUTE_NORMSHORT2: *spec = (struct attribute_spec){ ATTRIB_SNORM_16_16, SELECT(X, Y, 0, 1), half }; return 1;
	case DK_ATTRIBUTE_NORMSHORT3: *spec = (struct attribute_spec){ ATTRIB_SNORM_16x4, SELECT(X, Y, Z, 1), half }; return 1;
	case DK_ATTRIBUTE_NORMSHORT4: *spec = (struct attribute_spec){ ATTRIB_SNORM_16x4, SELECT(X, Y, Z, W), half }; return 1;
	case DK_ATTRIBUTE_BYTE1: *spec = (struct attribute_spec){ GX2_ATTRIB_FORMAT_UNORM_8, SELECT(X, 0, 0, 1), GX2_ENDIAN_SWAP_NONE }; return 1;
	case DK_ATTRIBUTE_BYTE2: *spec = (struct attribute_spec){ GX2_ATTRIB_FORMAT_UNORM_8_8, SELECT(X, Y, 0, 1), GX2_ENDIAN_SWAP_NONE }; return 1;
	case DK_ATTRIBUTE_BYTE3: *spec = (struct attribute_spec){ GX2_ATTRIB_FORMAT_UNORM_8_8_8_8, SELECT(X, Y, Z, 1), GX2_ENDIAN_SWAP_NONE }; return 1;
	case DK_ATTRIBUTE_BYTE4: *spec = (struct attribute_spec){ GX2_ATTRIB_FORMAT_UNORM_8_8_8_8, SELECT(X, Y, Z, W), GX2_ENDIAN_SWAP_NONE }; return 1;
	case DK_ATTRIBUTE_PACKED: *spec = (struct attribute_spec){ ATTRIB_INTEGER_32, SELECT(X, 0, 0, 1), word }; return 1;
	default: return 0;
	}
}

static uint64_t baseline_hash_words(uint64_t hash, const uint32_t *words, unsigned count)
{
	unsigned i;

	for (i = 0; i < count; i++)
	{
		hash ^= words[i];
		hash *= 0x100000001b3ull;
	}
	return hash;
}

static unsigned baseline_attributes_make(const struct shader *vs, GX2AttribStream *out, uint64_t *key)
{
	unsigned location, count = 0;
	uint64_t hash = 0xcbf29ce484222325ull;

	for (location = 0; location < DK_ATTRIBUTE_COUNT; location++)
	{
		const struct dk_attribute *attribute = &gx.format.attributes[location];
		struct attribute_spec spec;
		GX2AttribStream *stream;
		uint32_t words[4];

		if (!(vs->attribute_used & (1u << location)))
			continue;
		stream = &out[count++];
		memset(stream, 0, sizeof(*stream));
		stream->location = location;
		stream->type = GX2_ATTRIB_INDEX_PER_VERTEX;
		if (baseline_attribute_spec(attribute->format, (gx.format.little_endian >> attribute->stream) & 1, &spec))
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
				baseline_attribute_spec(DK_ATTRIBUTE_PACKED, 0, &spec);
			else
				baseline_attribute_spec(DK_ATTRIBUTE_FLOAT4, 0, &spec);
		}
		stream->format = spec.format;
		stream->mask = spec.mask;
		stream->endianSwap = spec.swap;
		words[0] = stream->location | (stream->buffer << 8);
		words[1] = stream->offset;
		words[2] = (uint32_t)stream->format;
		words[3] = spec.mask ^ (uint32_t)spec.swap;
		hash = baseline_hash_words(hash, words, 4);
	}
	*key = baseline_hash_words(hash, (const uint32_t *)(const void *)&vs, sizeof(vs) / 4);
	return count;
}
