/* Host-only mock of the installed WUT public input contract. No GPU work. */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t GX2AttribFormat;
typedef uint32_t GX2EndianSwapMode;
typedef struct GX2AttribStream
{
	uint32_t location, buffer, offset;
	GX2AttribFormat format;
	uint32_t type, aluDivisor, mask;
	GX2EndianSwapMode endianSwap;
} GX2AttribStream;
typedef struct GX2FetchShader
{
	uint32_t type;
	struct { uint32_t sq_pgm_resources_fs; } regs;
	uint32_t size;
	void *program;
	uint32_t attribCount, numDivisors, divisors[2];
} GX2FetchShader;

#define GX2_ATTRIB_FLAG_INTEGER 0x100u
#define GX2_ATTRIB_FLAG_SIGNED 0x200u
#define GX2_ATTRIB_FLAG_SCALED 0x800u
#define GX2_ATTRIB_TYPE_32 5u
#define GX2_ATTRIB_TYPE_16 2u
#define GX2_ATTRIB_TYPE_16_16 7u
#define GX2_ATTRIB_TYPE_16_16_16_16 14u
#define GX2_ATTRIB_FORMAT_FLOAT_32 0x806u
#define GX2_ATTRIB_FORMAT_FLOAT_32_32 0x80du
#define GX2_ATTRIB_FORMAT_FLOAT_32_32_32 0x811u
#define GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32 0x813u
#define GX2_ATTRIB_FORMAT_UNORM_8 0u
#define GX2_ATTRIB_FORMAT_UNORM_8_8 4u
#define GX2_ATTRIB_FORMAT_UNORM_8_8_8_8 10u
#define GX2_ENDIAN_SWAP_NONE 0u
#define GX2_ENDIAN_SWAP_8_IN_16 1u
#define GX2_ENDIAN_SWAP_8_IN_32 2u
#define GX2_ATTRIB_INDEX_PER_VERTEX 0u
#define GX2_ATTRIB_INDEX_PER_INSTANCE 1u
#define GX2_SQ_SEL_X 0u
#define GX2_SQ_SEL_Y 1u
#define GX2_SQ_SEL_Z 2u
#define GX2_SQ_SEL_W 3u
#define GX2_SQ_SEL_0 4u
#define GX2_SQ_SEL_1 5u
#define GX2_SEL_MASK(x,y,z,w) (((x)<<24)|((y)<<16)|((z)<<8)|(w))
#define GX2_FETCH_SHADER_TESSELLATION_NONE 0u
#define GX2_TESSELLATION_MODE_DISCRETE 0u
#define GX2_SHADER_PROGRAM_ALIGNMENT 256u
#define GX2_INVALIDATE_MODE_CPU_SHADER 16u
#define FETCH_LIMIT 2048
#define CONSTANTS_BUFFER 15

#include "source-types.inc"

struct shader { uint32_t attribute_used, identity; void *vertex; };
struct attribute_spec { GX2AttribFormat format; uint32_t mask; GX2EndianSwapMode swap; };
struct fetch;
static struct
{
	struct dk_vertex_format format;
	int fetch_full;
	struct fetch *cur_fetch;
	const struct shader *cur_vs;
	unsigned counts_fetch, counts_vs;
} gx;
static uint32_t test_hash_mask = UINT32_MAX;
static unsigned allocations, frees, program_allocations, init_calls, invalidations, log_calls;
static int fail_entry, fail_program;
static GX2AttribStream initialized[DK_ATTRIBUTE_COUNT];
static unsigned initialized_count;
static unsigned mock_fetch_binds, mock_vertex_binds;

static void *mock_calloc(size_t count, size_t size)
{
	if (fail_entry) { fail_entry = 0; return NULL; }
	allocations++;
	return calloc(count, size);
}
static void mock_free(void *data) { frees++; free(data); }
static void *gx_alloc(uint32_t size, uint32_t alignment)
{
	assert(alignment == GX2_SHADER_PROGRAM_ALIGNMENT);
	if (fail_program) { fail_program = 0; return NULL; }
	program_allocations++;
	return calloc(1, size);
}
static uint32_t GX2CalcFetchShaderSizeEx(unsigned count, unsigned type, unsigned tess)
{
	assert(count <= DK_ATTRIBUTE_COUNT && type == 0 && tess == 0);
	return 64 + count * 16;
}
static void GX2InitFetchShaderEx(GX2FetchShader *shader, uint8_t *program, unsigned count,
	const GX2AttribStream *attributes, unsigned type, unsigned tess)
{
	assert(count <= DK_ATTRIBUTE_COUNT && program && type == 0 && tess == 0);
	init_calls++;
	initialized_count = count;
	memcpy(initialized, attributes, count * sizeof(*attributes));
	shader->program = program;
	shader->attribCount = count;
	shader->type = type;
	shader->size = 64 + count * 16;
}
static void GX2Invalidate(unsigned mode, void *program, uint32_t size)
{
	assert(mode == GX2_INVALIDATE_MODE_CPU_SHADER && program && size >= 64);
	invalidations++;
}
static void GX2SetFetchShader(const GX2FetchShader *shader)
{
	assert(shader && shader->program);
	mock_fetch_binds++;
}
static void GX2SetVertexShader(const void *shader)
{
	assert(shader);
	mock_vertex_binds++;
}
static void wiiu_log(const char *format, int limit)
{
	(void)format;
	assert(limit == FETCH_LIMIT);
	log_calls++;
}

#define calloc mock_calloc
#define free mock_free
#include "source-extracted.inc"
#undef calloc
#undef free

static void reset(void)
{
	unsigned i;
	for (i = 0; i < fetch_count; i++)
	{
		free(fetch_table[i]->program);
		free(fetch_table[i]);
	}
	memset(fetch_table, 0, sizeof(fetch_table));
	memset(fetch_heads, 0, sizeof(fetch_heads));
	memset(&gx, 0, sizeof(gx));
	fetch_count = 0;
	allocations = frees = program_allocations = init_calls = invalidations = log_calls = 0;
	fail_entry = fail_program = 0;
	mock_fetch_binds = mock_vertex_binds = 0;
	test_hash_mask = UINT32_MAX;
}

static GX2AttribStream sample(void)
{
	GX2AttribStream out = { 2, 3, 16, GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32,
		GX2_ATTRIB_INDEX_PER_VERTEX, 0, GX2_SEL_MASK(0,1,2,3), GX2_ENDIAN_SWAP_8_IN_32 };
	return out;
}

static void identity_cases(void)
{
	GX2AttribStream a[2], b[2], snapshot[2];
	struct fetch *first, *second;
	unsigned field;

	reset();
	a[0] = sample(); a[1] = sample(); a[1].location = 7;
	memcpy(snapshot, a, sizeof(a));
	first = fetch_get_layout(a, 2);
	assert(first && fetch_count == 1 && initialized_count == 2);
	assert(!memcmp(initialized, a, sizeof(a)) && init_calls == invalidations);
	memset(a, 0xa5, sizeof(a)); /* descriptors are not borrowed */
	assert(fetch_get_layout(snapshot, 2) == first && init_calls == 1 && allocations == 1);
	reset();
	test_hash_mask = 0; /* every digest collides */
	first = fetch_get_layout(snapshot, 2);
	second = fetch_get_layout(snapshot, 1);
	assert(first && second && first != second && fetch_count == 2);
	assert(fetch_get_layout(snapshot, 2) == first && fetch_get_layout(snapshot, 1) == second);
	for (field = 0; field < 8; field++)
	{
		reset(); test_hash_mask = 0;
		memcpy(a, snapshot, sizeof(a)); memcpy(b, snapshot, sizeof(b));
		switch (field)
		{
		case 0: b[0].location = 6; break;
		case 1: b[0].buffer = 4; break;
		case 2: b[0].offset = 32; break;
		case 3: b[0].format = GX2_ATTRIB_FORMAT_FLOAT_32; break;
		case 4: b[0].type = GX2_ATTRIB_INDEX_PER_INSTANCE; break;
		case 5: b[0].aluDivisor = 2; break;
		case 6: b[0].mask = GX2_SEL_MASK(3,2,1,0); break;
		case 7: b[0].endianSwap = GX2_ENDIAN_SWAP_NONE; break;
		}
		first = fetch_get_layout(a, 2); second = fetch_get_layout(b, 2);
		assert(first && second && first != second && fetch_count == 2);
		assert(fetch_get_layout(a, 2) == first && fetch_get_layout(b, 2) == second);
		assert(init_calls == 2 && invalidations == 2);
	}
	reset();
	first = fetch_get_layout(NULL, 0);
	assert(first && !first->count && fetch_get_layout(NULL, 0) == first && init_calls == 1);
	assert(fetch_get_layout(snapshot, DK_ATTRIBUTE_COUNT + 1) == NULL);
}

static uint32_t random_word(uint32_t *seed)
{
	*seed ^= *seed << 13; *seed ^= *seed >> 17; *seed ^= *seed << 5;
	return *seed;
}

static void producer_cases(void)
{
	struct shader vs[2] = { { 0, 101, NULL }, { 0, 202, NULL } };
	GX2AttribStream old[DK_ATTRIBUTE_COUNT], fresh[DK_ATTRIBUTE_COUNT];
	uint64_t old_key;
	uint32_t seed = 0x1267ee9du;
	unsigned trial, index;
	reset();
	for (trial = 0; trial < 10000; trial++)
	{
		unsigned old_count, fresh_count;
		vs[0].attribute_used = random_word(&seed) & 0xffff;
		for (index = 0; index < DK_ATTRIBUTE_COUNT; index++)
		{
			gx.format.attributes[index].format = random_word(&seed) % (DK_ATTRIBUTE_PACKED + 2);
			gx.format.attributes[index].stream = random_word(&seed) % DK_STREAM_COUNT;
			gx.format.attributes[index].offset = random_word(&seed) & 0xfffc;
		}
		gx.format.little_endian = random_word(&seed) & 0xffff;
		memset(old, 0xcc, sizeof(old)); memset(fresh, 0xdd, sizeof(fresh));
		old_count = baseline_attributes_make(&vs[0], old, &old_key);
		fresh_count = attributes_make(&vs[0], fresh);
		assert(old_count == fresh_count && old_count <= DK_ATTRIBUTE_COUNT);
		assert(!memcmp(old, fresh, old_count * sizeof(old[0])));
	}
	memset(&gx, 0, sizeof(gx));
	for (index = 0; index < DK_ATTRIBUTE_COUNT; index++)
	{
		gx.format.attributes[index].format = DK_ATTRIBUTE_FLOAT4;
		gx.format.attributes[index].stream = index;
		gx.format.attributes[index].offset = index * 16;
	}
	vs[0].attribute_used = vs[1].attribute_used = 0xa55a;
	{
		struct fetch *first = fetch_get(&vs[0]);
		struct fetch *second = fetch_get(&vs[1]);
		assert(first == second && fetch_count == 1); /* different VS object, same actual input */
		vs[1].attribute_used ^= 1;
		assert(fetch_get(&vs[1]) != first && fetch_count == 2);
		vs[1].attribute_used = vs[0].attribute_used;
		gx.format.constants[1][0] = 99; /* runtime values do not change the fetch layout */
		gx.format.strides[1] = 24; /* GX2SetAttribBuffer, not InitFetchShaderEx input */
		assert(fetch_get(&vs[1]) == first);
		gx.format.attributes[1].offset += 4;
		assert(fetch_get(&vs[1]) != first);
	}
}

static void allocation_and_capacity(void)
{
	GX2AttribStream a = sample();
	struct fetch *oldest;
	unsigned i;
	reset();
	fail_entry = 1;
	assert(fetch_get_layout(&a, 1) == NULL && fetch_count == 0 && init_calls == 0);
	fail_program = 1;
	assert(fetch_get_layout(&a, 1) == NULL && fetch_count == 0 && frees == 1 && init_calls == 0);
	oldest = fetch_get_layout(&a, 1);
	assert(oldest && fetch_count == 1 && init_calls == 1);
	reset(); test_hash_mask = 0;
	oldest = NULL;
	for (i = 0; i < FETCH_LIMIT; i++)
	{
		struct fetch *entry;
		a.offset = i * 4;
		entry = fetch_get_layout(&a, 1);
		assert(entry && fetch_count == i + 1);
		if (!i) oldest = entry;
	}
	a.offset = 0;
	assert(fetch_get_layout(&a, 1) == oldest && fetch_count == FETCH_LIMIT);
	a.offset = FETCH_LIMIT * 4;
	assert(fetch_get_layout(&a, 1) == NULL && gx.fetch_full == 1 && log_calls == 1);
	assert(fetch_get_layout(&a, 1) == NULL && log_calls == 1);
	assert(init_calls == FETCH_LIMIT && program_allocations == FETCH_LIMIT);
	/* A corrupted link must not turn a missing layout into an infinite loop. */
	reset(); test_hash_mask = 0;
	a = sample(); oldest = fetch_get_layout(&a, 1);
	oldest->next = 1; a.offset += 4;
	assert(fetch_get_layout(&a, 1) == NULL && fetch_count == 1);
	oldest->next = 0;
	reset();
}

static void high_bit_buckets(void)
{
	GX2AttribStream a = sample();
	unsigned index, populated = 0;
	reset();
	for (index = 0; index < 256; index++)
	{
		a.offset = index << 8;
		assert(fetch_get_layout(&a, 1));
	}
	for (index = 0; index < FETCH_BUCKETS; index++)
		populated += fetch_heads[index] != 0;
	assert(populated >= 128); /* not a distribution promise for actual game keys */
	reset();
}

static void binding_cases(void)
{
	struct shader vs[2] = { { 1, 1, (void *)(uintptr_t)1 }, { 1, 2, (void *)(uintptr_t)2 } };
	struct fetch *shared, *changed;
	reset();
	gx.format.attributes[0].format = DK_ATTRIBUTE_FLOAT3;
	shared = fetch_get(&vs[0]);
	assert(shared == fetch_get(&vs[1]) && fetch_count == 1);
	extracted_draw_bind(shared, &vs[0]);
	assert(mock_fetch_binds == 1 && mock_vertex_binds == 1);
	extracted_draw_bind(shared, &vs[0]);
	assert(mock_fetch_binds == 1 && mock_vertex_binds == 1);
	extracted_draw_bind(shared, &vs[1]);
	assert(mock_fetch_binds == 2 && mock_vertex_binds == 2); /* old VS-switch behavior retained */
	extracted_draw_bind(shared, &vs[0]);
	assert(mock_fetch_binds == 3 && mock_vertex_binds == 3);
	gx.format.attributes[0].offset = 4;
	changed = fetch_get(&vs[0]);
	assert(changed != shared);
	extracted_draw_bind(changed, &vs[0]);
	assert(mock_fetch_binds == 4 && mock_vertex_binds == 3);
	gx.cur_fetch = NULL; gx.cur_vs = NULL; /* forgotten GPU state, owned CPU entry survives */
	extracted_draw_bind(shared, &vs[0]);
	assert(mock_fetch_binds == 5 && mock_vertex_binds == 4);
	assert(gx.counts_fetch == mock_fetch_binds && gx.counts_vs == mock_vertex_binds);
	reset();
}

int main(void)
{
	assert(sizeof(GX2AttribStream) == 32);
	assert(sizeof(struct fetch_attribute_key) == 32);
	assert(offsetof(struct fetch, shader) == offsetof(struct baseline_fetch, shader));
	assert(offsetof(struct fetch, program) == offsetof(struct baseline_fetch, program));
	identity_cases(); producer_cases(); allocation_and_capacity(); high_bit_buckets(); binding_cases();
	printf("fetch fixture: 10000 extracted-producer cases; all 8 descriptor fields/count, collisions, sharing, ownership and capacity passed\n");
	printf("sizes: original_base=%lu candidate_base=%lu key_per_attribute=%lu heads=%lu table=%lu shader=%lu pointer=%lu\n",
		(unsigned long)sizeof(struct baseline_fetch), (unsigned long)sizeof(struct fetch), (unsigned long)sizeof(struct fetch_attribute_key),
		(unsigned long)sizeof(fetch_heads), (unsigned long)sizeof(fetch_table),
		(unsigned long)sizeof(GX2FetchShader), (unsigned long)sizeof(void *));
	return 0;
}
