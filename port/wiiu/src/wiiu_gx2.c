/*
WIIU_GX2.C

The Wii U's half of the renderer: runs the command stream the Direct3D 8 device (d3d8_gx2.c) writes, on the GPU through GX2. The device and the
stream's definition (dk_commands.h) come from the Switch port's deko3d renderer (port/switch/DEKO3D.md), whose host half this takes the place of;
there is no guest/host split here, only the two ABIs (the device is built with the Xbox's, this with the console's), and every field of the stream
is a 32 bit integer or float, so that they agree.

What it does so far (phase 2 of that design): render targets, clearing them, and presenting the back buffer to the TV and the GamePad, letterboxed.
Draws, textures, state, shaders and visibility tests are accepted and counted but not made (the log says how many, once a second).

It also answers the platform layer's few calls about the screen (wiiu_platform below): the display is the TV's, and the game's back buffer is drawn at
the TV's size, so that presenting it is a blit.

Not run on a console.
*/

#include "dk_commands.h"
#include "wiiu_shell.h"
#include "wiiu_tile.h"

#include <coreinit/cache.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/time.h>
#include <gx2/clear.h>
#include <gx2/context.h>
#include <gx2/enum.h>
#include <gx2/draw.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/sampler.h>
#include <gx2/shaders.h>
#include <gx2/state.h>
#include <gx2/surface.h>
#include <gx2/texture.h>
#include <gx2/utils.h>
#include <whb/gfx.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const unsigned char wiiu_gx2_blit_gsh[];
extern const unsigned long wiiu_gx2_blit_gsh_size;

#define TARGET_LIMIT 64
#define SHADER_LIMIT 8192
#define FETCH_LIMIT 2048
#define RING_SEGMENTS 2
#define RING_SEGMENT_BYTES (24u * 1024u * 1024u)
#define DEFERRED_LIMIT 512
/* the attribute buffer that holds the registers the declaration does not feed (stride 0) */
#define CONSTANTS_BUFFER 15
#define SURFACE_PIXEL_WIDTH(surface) ((surface)->pixel_width ? (surface)->pixel_width : (surface)->width)
#define SURFACE_PIXEL_HEIGHT(surface) ((surface)->pixel_height ? (surface)->pixel_height : (surface)->height)

struct target
{
	struct dk_surface surface;
	GX2ColorBuffer color;
	GX2DepthBuffer depth;
	GX2Texture texture; /* a color target as the blit samples it */
	unsigned last_used;
};

struct blit_vertex
{
	float x, y, u, v, r, g, b, a;
};

static struct
{
	int ready;
	int failed;
	struct target targets[TARGET_LIMIT];
	unsigned target_count;
	struct target *color;
	struct target *depth;
	WHBGfxShaderGroup blit;
	GX2Sampler sampler;
	uint32_t sampler_location;
	struct blit_vertex *quad;
	unsigned frames;
	OSTime told;
	unsigned counts[32];
	unsigned counted_draws;
	unsigned partial_clears;

	/* what the stream has set, for the draws that follow */
	struct dk_draw_state state;
	int state_dirty;
	float constants[192][4];
	unsigned constants_lo, constants_hi;
	struct dk_vertex_parameters vertex_parameters;
	struct dk_pixel_parameters pixel_parameters;
	int vertex_parameters_dirty, pixel_parameters_dirty;
	uint32_t vs_handle, ps_handle;
	struct dk_vertex_format format;
	int format_set;
	struct dk_stage_texture stages[4];
	GX2Sampler samplers[4];
	GX2Texture white;
	/* what the GPU's registers hold */
	const struct shader *regs_vs, *regs_ps, *cur_vs, *cur_ps;
	const struct fetch *cur_fetch;
	unsigned use_serial;

	struct ring_segment
	{
		uint8_t *base;
		uint32_t offset;
		OSTime stamp;
	} ring[RING_SEGMENTS];
	unsigned ring_current;
	struct
	{
		void *memory;
		unsigned frame;
	} deferred[DEFERRED_LIMIT];
	unsigned deferred_count;

	unsigned shaders_loaded, fetch_full;
	unsigned draws_made, skipped_target, skipped_state, skipped_shader, skipped_ring, ring_overflows;
	unsigned textures_made, textures_unsupported;
	OSTime started;
	double exit_after;
	int trace_frame; /* halo/gx2_trace.txt: the frame whose draws are logged (-1: none) */
	int debug_mode; /* and, after it, a mode: 1 draws without blending */
	unsigned traced;
} gx;

static void *gx_alloc_raw(uint32_t size, uint32_t alignment)
{
	return MEMAllocFromDefaultHeapEx(size, (int32_t)alignment);
}

static void *gx_alloc(uint32_t size, uint32_t alignment)
{
	void *memory = MEMAllocFromDefaultHeapEx(size, (int32_t)alignment);

	if (memory)
		memset(memory, 0, size);
	return memory;
}

/* ---------- shaders: the pack's, loaded when a draw first needs one */

/* gx2_shaders.c: where in the pack the shader of a handle is */
const void *dk_shader_blob(uint32_t handle, uint32_t *size, uint32_t *stage);

enum
{
	VS_C,
	VS_VIEWPORT_SCALE,
	VS_VIEWPORT_OFFSET,
	VS_POINT_SIZE,
	VS_SCREEN_OFFSET,
	VS_UNIFORMS,
};

enum
{
	PS_C0,
	PS_C1,
	PS_FINAL_C0,
	PS_FINAL_C1,
	PS_FOG_COLOR,
	PS_FOG_PARAMETERS,
	PS_ALPHA_REFERENCE,
	PS_BUMP_MATRIX,
	PS_BUMP_LUMINANCE,
	PS_TEXTURE_SCALE,
	PS_TEXTURE_LOD_BIAS,
	PS_UNIFORMS,
};

static const char *const vertex_uniform_names[VS_UNIFORMS] = { "c", "viewport_scale", "viewport_offset", "point_size", "screen_offset" };
static const char *const pixel_uniform_names[PS_UNIFORMS] = { "ps_c0", "ps_c1", "ps_final_c0", "ps_final_c1", "fog_color", "fog_parameters",
	"alpha_reference", "bump_matrix", "bump_luminance", "texture_scale", "texture_lod_bias" };

struct shader
{
	int state; /* 0: not tried, 1: ready, 2: failed */
	uint32_t stage;
	GX2VertexShader *vertex;
	GX2PixelShader *pixel;
	int uniform[PS_UNIFORMS]; /* the register offset (in floats) of each, -1: the shader does not use it */
	uint32_t uniform_floats[PS_UNIFORMS]; /* and how many floats it has room for */
	int sampler[4]; /* the location of tex0..3, -1: not sampled */
	uint32_t attribute_used; /* bit n: the vertex shader reads input location n */
};

static struct shader *shader_table[SHADER_LIMIT];

static uint32_t type_floats(GX2ShaderVarType type)
{
	switch (type)
	{
	case GX2_SHADER_VAR_TYPE_FLOAT2: return 2;
	case GX2_SHADER_VAR_TYPE_FLOAT3: return 3;
	case GX2_SHADER_VAR_TYPE_FLOAT: return 1;
	default: return 4;
	}
}

static void uniforms_resolve(struct shader *shader, const GX2UniformVar *vars, uint32_t count, const char *const *names, unsigned name_count)
{
	unsigned n;
	uint32_t i;

	for (n = 0; n < name_count; n++)
	{
		const size_t length = strlen(names[n]);

		shader->uniform[n] = -1;
		for (i = 0; i < count; i++)
		{
			const char *name = vars[i].name;

			if (!strncmp(name, names[n], length) && (name[length] == 0 || name[length] == '['))
			{
				shader->uniform[n] = (int)vars[i].offset;
				shader->uniform_floats[n] = vars[i].count * type_floats(vars[i].type);
				break;
			}
		}
	}
}

static struct shader *shader_get(uint32_t handle)
{
	struct shader *shader;
	const void *blob;
	uint32_t size = 0, stage = 0, i;

	if (!handle || handle >= SHADER_LIMIT)
		return NULL;
	shader = shader_table[handle];
	if (shader)
		return shader->state == 1 ? shader : NULL;
	shader = calloc(1, sizeof(*shader));
	if (!shader)
		return NULL;
	shader_table[handle] = shader;
	shader->state = 2;
	for (i = 0; i < 4; i++)
		shader->sampler[i] = -1;
	blob = dk_shader_blob(handle, &size, &stage);
	if (!blob)
		return NULL;
	shader->stage = stage;
	if (stage == DK_SHADER_STAGE_VERTEX)
	{
		GX2VertexShader *vertex = WHBGfxLoadGFDVertexShader(0, blob);

		if (!vertex)
		{
			wiiu_log("GX2: vertex shader %u (%u bytes) did not load", (unsigned)handle, (unsigned)size);
			return NULL;
		}
		shader->vertex = vertex;
		uniforms_resolve(shader, vertex->uniformVars, vertex->uniformVarCount, vertex_uniform_names, VS_UNIFORMS);
		for (i = 0; i < vertex->attribVarCount; i++)
			if (vertex->attribVars[i].location < 32)
				shader->attribute_used |= 1u << vertex->attribVars[i].location;
		if (gx.shaders_loaded < 4)
		{
			wiiu_log("GX2: vertex shader %u: mode %u, %u uniforms, %u attributes (mask %08x), c at %d (%u floats), scale %d, offset %d, point %d, screen %d",
				(unsigned)handle, (unsigned)vertex->mode, (unsigned)vertex->uniformVarCount, (unsigned)vertex->attribVarCount,
				(unsigned)shader->attribute_used, shader->uniform[VS_C], (unsigned)shader->uniform_floats[VS_C], shader->uniform[VS_VIEWPORT_SCALE],
				shader->uniform[VS_VIEWPORT_OFFSET], shader->uniform[VS_POINT_SIZE], shader->uniform[VS_SCREEN_OFFSET]);
			for (i = 0; i < vertex->uniformVarCount && i < 8; i++)
				wiiu_log("GX2:   uniform %s: type %u, count %u, offset %u, block %d", vertex->uniformVars[i].name,
					(unsigned)vertex->uniformVars[i].type, (unsigned)vertex->uniformVars[i].count, (unsigned)vertex->uniformVars[i].offset,
					(int)vertex->uniformVars[i].block);
		}
	}
	else
	{
		GX2PixelShader *pixel = WHBGfxLoadGFDPixelShader(0, blob);

		if (!pixel)
		{
			wiiu_log("GX2: pixel shader %u (%u bytes) did not load", (unsigned)handle, (unsigned)size);
			return NULL;
		}
		shader->pixel = pixel;
		uniforms_resolve(shader, pixel->uniformVars, pixel->uniformVarCount, pixel_uniform_names, PS_UNIFORMS);
		for (i = 0; i < pixel->samplerVarCount; i++)
		{
			const char *name = pixel->samplerVars[i].name;

			if (name[0] == 't' && name[1] == 'e' && name[2] == 'x' && name[3] >= '0' && name[3] <= '3')
				shader->sampler[name[3] - '0'] = (int)pixel->samplerVars[i].location;
		}
		if (gx.shaders_loaded < 4)
		{
			wiiu_log("GX2: pixel shader %u: mode %u, %u uniforms, %u samplers", (unsigned)handle, (unsigned)pixel->mode,
				(unsigned)pixel->uniformVarCount, (unsigned)pixel->samplerVarCount);
			for (i = 0; i < pixel->uniformVarCount && i < 12; i++)
				wiiu_log("GX2:   uniform %s: type %u, count %u, offset %u, block %d", pixel->uniformVars[i].name, (unsigned)pixel->uniformVars[i].type,
					(unsigned)pixel->uniformVars[i].count, (unsigned)pixel->uniformVars[i].offset, (int)pixel->uniformVars[i].block);
		}
	}
	gx.shaders_loaded++;
	shader->state = 1;
	return shader;
}

/* ---------- vertex formats: a fetch shader for each (vertex shader, format) */

struct attribute_spec
{
	GX2AttribFormat format;
	uint32_t mask;
	GX2EndianSwapMode swap;
};

#define ATTRIB_INTEGER_32 ((GX2AttribFormat)(GX2_ATTRIB_FLAG_INTEGER | GX2_ATTRIB_TYPE_32))
#define ATTRIB_SSCALED_16 ((GX2AttribFormat)(GX2_ATTRIB_FLAG_SIGNED | GX2_ATTRIB_FLAG_SCALED | GX2_ATTRIB_TYPE_16))
#define ATTRIB_SSCALED_16_16 ((GX2AttribFormat)(GX2_ATTRIB_FLAG_SIGNED | GX2_ATTRIB_FLAG_SCALED | GX2_ATTRIB_TYPE_16_16))
#define ATTRIB_SSCALED_16x4 ((GX2AttribFormat)(GX2_ATTRIB_FLAG_SIGNED | GX2_ATTRIB_FLAG_SCALED | GX2_ATTRIB_TYPE_16_16_16_16))
#define ATTRIB_SNORM_16 ((GX2AttribFormat)(GX2_ATTRIB_FLAG_SIGNED | GX2_ATTRIB_TYPE_16))
#define ATTRIB_SNORM_16_16 ((GX2AttribFormat)(GX2_ATTRIB_FLAG_SIGNED | GX2_ATTRIB_TYPE_16_16))
#define ATTRIB_SNORM_16x4 ((GX2AttribFormat)(GX2_ATTRIB_FLAG_SIGNED | GX2_ATTRIB_TYPE_16_16_16_16))

#define SELECT(x, y, z, w) GX2_SEL_MASK(GX2_SQ_SEL_##x, GX2_SQ_SEL_##y, GX2_SQ_SEL_##z, GX2_SQ_SEL_##w)

/* Big-endian data (the CPU's, the converted BSPs'): 32 bit values are swapped as words, 16 bit ones as halves, and four bytes (a D3DCOLOR, a word 0xAARRGGBB)
as a word, which puts blue first. Little-endian data (the models' vertices, as the Xbox had them): no swap, and a D3DCOLOR's bytes are blue first as they are. */
static int attribute_spec(uint32_t dk, int little_endian, struct attribute_spec *spec)
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

struct fetch
{
	uint64_t key;
	GX2FetchShader shader;
	void *program;
};

static struct fetch *fetch_table[FETCH_LIMIT];
static unsigned fetch_count;

static uint64_t hash_words(uint64_t hash, const uint32_t *words, unsigned count)
{
	unsigned i;

	for (i = 0; i < count; i++)
	{
		hash ^= words[i];
		hash *= 0x100000001b3ull;
	}
	return hash;
}

/* the attributes the vertex shader reads, fed as the format says (an unfed one, and one the format leaves out, from the constants' stream) */
static unsigned attributes_make(const struct shader *vs, GX2AttribStream *out, uint64_t *key)
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
		words[0] = stream->location | (stream->buffer << 8);
		words[1] = stream->offset;
		words[2] = (uint32_t)stream->format;
		words[3] = spec.mask ^ (uint32_t)spec.swap;
		hash = hash_words(hash, words, 4);
	}
	*key = hash_words(hash, (const uint32_t *)(const void *)&vs, sizeof(vs) / 4);
	return count;
}

static struct fetch *fetch_get(const struct shader *vs)
{
	GX2AttribStream attributes[DK_ATTRIBUTE_COUNT];
	uint64_t key;
	unsigned count = attributes_make(vs, attributes, &key), i;
	struct fetch *fetch;
	uint32_t size;

	for (i = 0; i < fetch_count; i++)
		if (fetch_table[i]->key == key)
			return fetch_table[i];
	if (fetch_count == FETCH_LIMIT)
	{
		if (!gx.fetch_full)
			wiiu_log("GX2: more than %d fetch shaders", FETCH_LIMIT);
		gx.fetch_full = 1;
		return NULL;
	}
	fetch = calloc(1, sizeof(*fetch));
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
	fetch_table[fetch_count++] = fetch;
	return fetch;
}

/* ---------- vertex data: copied into a ring the GPU reads, a segment a frame */

static void *ring_alloc(uint32_t size)
{
	const uint32_t aligned = (size + 63u) & ~63u;
	struct ring_segment *segment = &gx.ring[gx.ring_current];

	if (!segment->base || segment->offset + aligned > RING_SEGMENT_BYTES)
	{
		gx.ring_overflows++;
		return NULL;
	}
	segment->offset += aligned;
	return segment->base + segment->offset - aligned;
}

static void *ring_copy(const void *source, uint32_t size)
{
	void *memory = ring_alloc(size);

	if (!memory)
		return NULL;
	memcpy(memory, source, size);
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, memory, size);
	return memory;
}

/* the frame is over: the GPU will read this segment's data until its stamp, and the next segment is free when its own is past */
static void ring_next(void)
{
	gx.ring[gx.ring_current].stamp = GX2GetLastSubmittedTimeStamp();
	gx.ring_current = (gx.ring_current + 1) % RING_SEGMENTS;
	if (gx.ring[gx.ring_current].stamp)
		GX2WaitTimeStamp(gx.ring[gx.ring_current].stamp);
	gx.ring[gx.ring_current].offset = 0;
}

/* ---------- state */

static GX2CompareFunction compare_of(uint32_t gl)
{
	return gl >= 0x200 && gl <= 0x207 ? (GX2CompareFunction)(gl - 0x200) : GX2_COMPARE_FUNC_ALWAYS;
}

static GX2StencilFunction stencil_op_of(uint32_t gl)
{
	switch (gl)
	{
	case 0: return GX2_STENCIL_FUNCTION_ZERO;
	case 0x1E01: return GX2_STENCIL_FUNCTION_REPLACE;
	case 0x1E02: return GX2_STENCIL_FUNCTION_INCR_CLAMP;
	case 0x1E03: return GX2_STENCIL_FUNCTION_DECR_CLAMP;
	case 0x150A: return GX2_STENCIL_FUNCTION_INV;
	case 0x8507: return GX2_STENCIL_FUNCTION_INCR_WRAP;
	case 0x8508: return GX2_STENCIL_FUNCTION_DECR_WRAP;
	default: return GX2_STENCIL_FUNCTION_KEEP;
	}
}

static GX2BlendMode blend_of(uint32_t gl)
{
	switch (gl)
	{
	case 0: return GX2_BLEND_MODE_ZERO;
	case 1: return GX2_BLEND_MODE_ONE;
	case 0x300: return GX2_BLEND_MODE_SRC_COLOR;
	case 0x301: return GX2_BLEND_MODE_INV_SRC_COLOR;
	case 0x302: return GX2_BLEND_MODE_SRC_ALPHA;
	case 0x303: return GX2_BLEND_MODE_INV_SRC_ALPHA;
	case 0x304: return GX2_BLEND_MODE_DST_ALPHA;
	case 0x305: return GX2_BLEND_MODE_INV_DST_ALPHA;
	case 0x306: return GX2_BLEND_MODE_DST_COLOR;
	case 0x307: return GX2_BLEND_MODE_INV_DST_COLOR;
	case 0x308: return GX2_BLEND_MODE_SRC_ALPHA_SAT;
	case 0x8001: return GX2_BLEND_MODE_BLEND_FACTOR;
	case 0x8002: return GX2_BLEND_MODE_INV_BLEND_FACTOR;
	case 0x8003: return GX2_BLEND_MODE_CONSTANT_ALPHA;
	case 0x8004: return GX2_BLEND_MODE_INV_CONSTANT_ALPHA;
	default: return GX2_BLEND_MODE_ONE;
	}
}

static GX2BlendCombineMode combine_of(uint32_t equation)
{
	switch (equation)
	{
	case DK_BLEND_SUBTRACT: return GX2_BLEND_COMBINE_MODE_SUB;
	case DK_BLEND_REVERSE_SUBTRACT: return GX2_BLEND_COMBINE_MODE_REV_SUB;
	case DK_BLEND_MIN: return GX2_BLEND_COMBINE_MODE_MIN;
	case DK_BLEND_MAX: return GX2_BLEND_COMBINE_MODE_MAX;
	default: return GX2_BLEND_COMBINE_MODE_ADD;
	}
}

static void state_apply(void)
{
	const struct dk_draw_state *s = &gx.state;
	const GX2FrontFace front = s->front_face_ccw ? GX2_FRONT_FACE_CCW : GX2_FRONT_FACE_CW;
	const GX2PolygonMode mode = s->fill_mode == DK_FILL_LINE ? GX2_POLYGON_MODE_LINE : s->fill_mode == DK_FILL_POINT ? GX2_POLYGON_MODE_POINT :
		GX2_POLYGON_MODE_TRIANGLE;
	const GX2StencilFunction keep = GX2_STENCIL_FUNCTION_KEEP;
	GX2CompareFunction stencil_function = compare_of(s->stencil_function);

	GX2SetRasterizerClipControlEx(TRUE, TRUE, FALSE);
	/* The generated vertex shaders put the Xbox's top at clip y = -1, as the OpenGL port's: there row 0 of a framebuffer is clip y = -1, and a render target's rows
	are the Xbox's (top first). GX2's viewport puts clip y = +1 at row 0, so it is given a negative height, which turns it over: rows and winding as OpenGL's. */
	GX2SetViewport((float)s->viewport[0], (float)(s->viewport[1] + s->viewport[3]), (float)s->viewport[2], -(float)s->viewport[3], s->depth_range[0],
		s->depth_range[1]);
	GX2SetScissor((uint32_t)s->scissor[0], (uint32_t)s->scissor[1], (uint32_t)s->scissor[2], (uint32_t)s->scissor[3]);
	GX2SetPolygonControl(front, s->cull == DK_CULL_FRONT, s->cull == DK_CULL_BACK, mode != GX2_POLYGON_MODE_TRIANGLE, mode, mode, s->offset_enable,
		s->offset_enable, s->offset_enable);
	if (s->offset_enable)
		GX2SetPolygonOffset(s->offset_units, s->offset_slope, s->offset_units, s->offset_slope, 0.0f);
	if (s->stencil_test)
	{
		GX2SetDepthStencilControl(s->depth_test, s->depth_write, compare_of(s->depth_function), TRUE, FALSE, stencil_function, stencil_op_of(s->stencil_pass),
			stencil_op_of(s->stencil_depth_fail), stencil_op_of(s->stencil_fail), stencil_function, stencil_op_of(s->stencil_pass),
			stencil_op_of(s->stencil_depth_fail), stencil_op_of(s->stencil_fail));
		GX2SetStencilMask((uint8_t)s->stencil_mask, (uint8_t)s->stencil_write_mask, (uint8_t)s->stencil_reference, (uint8_t)s->stencil_mask,
			(uint8_t)s->stencil_write_mask, (uint8_t)s->stencil_reference);
	}
	else
	{
		GX2SetDepthStencilControl(s->depth_test, s->depth_write, compare_of(s->depth_function), FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS, keep, keep, keep,
			GX2_COMPARE_FUNC_ALWAYS, keep, keep, keep);
	}
	if (s->blend)
	{
		GX2SetBlendControl(GX2_RENDER_TARGET_0, blend_of(s->blend_source), blend_of(s->blend_destination), combine_of(s->blend_equation), TRUE,
			blend_of(s->blend_source), blend_of(s->blend_destination), combine_of(s->blend_equation));
		GX2SetBlendConstantColor(s->blend_color[0], s->blend_color[1], s->blend_color[2], s->blend_color[3]);
	}
	GX2SetColorControl(GX2_LOGIC_OP_COPY, s->blend ? 1 : 0, FALSE, TRUE);
	GX2SetTargetChannelMasks((GX2ChannelMask)s->color_mask, GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA,
		GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA);
	gx.state_dirty = 0;
}

/* ---------- uniforms: the stream's copies, written to the registers of the shader a draw uses when they differ from what the registers hold */

static void vertex_uniform(const struct shader *vs, unsigned which, const float *values, uint32_t floats)
{
	if (vs->uniform[which] < 0)
		return;
	if (floats > vs->uniform_floats[which])
		floats = vs->uniform_floats[which];
	GX2SetVertexUniformReg((uint32_t)vs->uniform[which], floats, values);
}

static void pixel_uniform(const struct shader *ps, unsigned which, const float *values, uint32_t floats)
{
	if (ps->uniform[which] < 0)
		return;
	if (floats > ps->uniform_floats[which])
		floats = ps->uniform_floats[which];
	GX2SetPixelUniformReg((uint32_t)ps->uniform[which], floats, values);
}

static void uniforms_apply(const struct shader *vs, const struct shader *ps)
{
	if (vs != gx.regs_vs)
	{
		vertex_uniform(vs, VS_C, &gx.constants[0][0], 192 * 4);
		gx.constants_lo = 192;
		gx.constants_hi = 0;
		gx.vertex_parameters_dirty = 1;
		gx.regs_vs = vs;
	}
	else if (gx.constants_lo <= gx.constants_hi && vs->uniform[VS_C] >= 0)
	{
		const uint32_t floats = (gx.constants_hi - gx.constants_lo + 1) * 4;

		if (gx.constants_lo * 4 < vs->uniform_floats[VS_C])
			GX2SetVertexUniformReg((uint32_t)vs->uniform[VS_C] + gx.constants_lo * 4,
				gx.constants_lo * 4 + floats > vs->uniform_floats[VS_C] ? vs->uniform_floats[VS_C] - gx.constants_lo * 4 : floats,
				&gx.constants[gx.constants_lo][0]);
		gx.constants_lo = 192;
		gx.constants_hi = 0;
	}
	if (gx.vertex_parameters_dirty)
	{
		vertex_uniform(vs, VS_VIEWPORT_SCALE, gx.vertex_parameters.viewport_scale, 4);
		vertex_uniform(vs, VS_VIEWPORT_OFFSET, gx.vertex_parameters.viewport_offset, 4);
		vertex_uniform(vs, VS_POINT_SIZE, &gx.vertex_parameters.point_and_screen[0], 1);
		vertex_uniform(vs, VS_SCREEN_OFFSET, &gx.vertex_parameters.point_and_screen[1], 1);
		gx.vertex_parameters_dirty = 0;
	}
	if (ps != gx.regs_ps || gx.pixel_parameters_dirty)
	{
		static const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		const struct dk_pixel_parameters *p = &gx.pixel_parameters;

		pixel_uniform(ps, PS_C0, &p->ps_c0[0][0], 32);
		pixel_uniform(ps, PS_C1, &p->ps_c1[0][0], 32);
		pixel_uniform(ps, PS_FINAL_C0, p->ps_final_c0, 4);
		pixel_uniform(ps, PS_FINAL_C1, p->ps_final_c1, 4);
		pixel_uniform(ps, PS_FOG_COLOR, p->fog_color, 4);
		pixel_uniform(ps, PS_FOG_PARAMETERS, p->fog_parameters, 4);
		pixel_uniform(ps, PS_ALPHA_REFERENCE, p->alpha_reference, 1);
		pixel_uniform(ps, PS_BUMP_MATRIX, &p->bump_matrix[0][0], 16);
		pixel_uniform(ps, PS_BUMP_LUMINANCE, &p->bump_luminance[0][0], 16);
		pixel_uniform(ps, PS_TEXTURE_SCALE, &p->texture_scale[0][0], 16);
		pixel_uniform(ps, PS_TEXTURE_LOD_BIAS, zero, 4);
		gx.pixel_parameters_dirty = 0;
		gx.regs_ps = ps;
	}
}

/* ---------- textures */

struct image
{
	int used;
	int supported;
	uint32_t kind, format, width, height;
	GX2Texture texture;
};

static struct image *images;

static void deferred_flush(int everything)
{
	unsigned i, kept = 0;

	for (i = 0; i < gx.deferred_count; i++)
	{
		if (everything || gx.frames >= gx.deferred[i].frame + 3)
			MEMFreeToDefaultHeap(gx.deferred[i].memory);
		else
			gx.deferred[kept++] = gx.deferred[i];
	}
	gx.deferred_count = kept;
}

/* memory the GPU may still be reading goes back to the heap a few frames on */
static void free_later(void *memory)
{
	if (!memory)
		return;
	if (gx.deferred_count == DEFERRED_LIMIT)
	{
		GX2DrawDone();
		deferred_flush(1);
	}
	gx.deferred[gx.deferred_count].memory = memory;
	gx.deferred[gx.deferred_count].frame = gx.frames;
	gx.deferred_count++;
}

static void image_release(struct image *image)
{
	if (image->used && image->texture.surface.image)
		free_later(image->texture.surface.image);
	memset(image, 0, sizeof(*image));
}

static int format_of(uint32_t dk, GX2SurfaceFormat *format, unsigned *block_bytes)
{
	switch (dk)
	{
	case DK_TEXTURE_BC1: *format = GX2_SURFACE_FORMAT_UNORM_BC1; *block_bytes = 8; return 1;
	case DK_TEXTURE_BC2: *format = GX2_SURFACE_FORMAT_UNORM_BC2; *block_bytes = 16; return 1;
	case DK_TEXTURE_BC3: *format = GX2_SURFACE_FORMAT_UNORM_BC3; *block_bytes = 16; return 1;
	default: *format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8; *block_bytes = 4; return 0;
	}
}

/* makes the texture's surface: 2D, level 0 only. BC in the 1D tiled layout (wiiu_tile.c), BGRA linear; its texels are the DDS file's bytes / a Direct3D
color's bytes (blue first), which the compMap puts right */
static int image_surface(struct image *image, uint32_t kind, uint32_t format, uint32_t width, uint32_t height)
{
	GX2Surface *surface = &image->texture.surface;
	GX2SurfaceFormat gx_format;
	unsigned block_bytes;
	const int compressed = format_of(format, &gx_format, &block_bytes);

	memset(image, 0, sizeof(*image));
	image->used = 1;
	image->kind = kind;
	image->format = format;
	image->width = width;
	image->height = height;
	if (kind != DK_TEXTURE_2D || !width || !height)
		return 0;
	surface->dim = GX2_SURFACE_DIM_TEXTURE_2D;
	surface->width = width;
	surface->height = height;
	surface->depth = 1;
	surface->mipLevels = 1;
	surface->format = gx_format;
	surface->aa = GX2_AA_MODE1X;
	surface->use = GX2_SURFACE_USE_TEXTURE;
	surface->tileMode = compressed ? GX2_TILE_MODE_TILED_1D_THIN1 : GX2_TILE_MODE_LINEAR_ALIGNED;
	GX2CalcSurfaceSizeAndAlignment(surface);
	surface->image = gx_alloc_raw(surface->imageSize, surface->alignment ? surface->alignment : 256);
	if (!surface->image)
	{
		wiiu_log("GX2: no memory for a %ux%u texture (%u bytes)", (unsigned)width, (unsigned)height, (unsigned)surface->imageSize);
		return 0;
	}
	memset(surface->image, 0, surface->imageSize);
	image->texture.viewFirstMip = 0;
	image->texture.viewNumMips = 1;
	image->texture.viewFirstSlice = 0;
	image->texture.viewNumSlices = 1;
	image->texture.compMap = compressed ? GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A) :
		GX2_COMP_MAP(GX2_SQ_SEL_B, GX2_SQ_SEL_G, GX2_SQ_SEL_R, GX2_SQ_SEL_A);
	GX2InitTextureRegs(&image->texture);
	image->supported = 1;
	return 1;
}

static void image_write_level0(struct image *image, const uint8_t *source, uint32_t bytes)
{
	GX2Surface *surface = &image->texture.surface;
	GX2SurfaceFormat gx_format;
	unsigned block_bytes;

	if (format_of(image->format, &gx_format, &block_bytes))
	{
		const unsigned across = (image->width + 3) / 4, down = (image->height + 3) / 4;

		if (bytes < (uint32_t)across * down * block_bytes || surface->pitch < across)
			return;
		wiiu_tile_1d(source, across, down, block_bytes, surface->pitch, (uint8_t *)surface->image);
	}
	else
	{
		unsigned y;

		if (bytes < image->width * image->height * 4)
			return;
		for (y = 0; y < image->height; y++)
			memcpy((uint8_t *)surface->image + (size_t)y * surface->pitch * 4, source + (size_t)y * image->width * 4, (size_t)image->width * 4);
	}
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, surface->image, surface->imageSize);
}

static void texture_make(const struct dk_command_texture *command)
{
	struct image *image;

	if (!images || !command->id || command->id >= DK_TEXTURE_LIMIT)
		return;
	image = &images[command->id];
	if (!(image->used && image->supported && image->kind == command->kind && image->format == command->format && image->width == command->width &&
		image->height == command->height))
	{
		image_release(image);
		image_surface(image, command->kind, command->format, command->width, command->height);
		gx.textures_made++;
	}
	if (image->supported && command->source_bytes)
		image_write_level0(image, (const uint8_t *)(uintptr_t)command->source, command->source_bytes);
	else if (!image->supported)
		gx.textures_unsupported++;
}

static void texture_rows(const struct dk_command_texture_rows *command)
{
	struct image *image;
	GX2Surface *surface;
	unsigned row;

	if (!images || !command->id || command->id >= DK_TEXTURE_LIMIT || command->level)
		return;
	image = &images[command->id];
	surface = &image->texture.surface;
	if (!image->used || !image->supported || image->format != DK_TEXTURE_BGRA || command->top + command->rows > image->height ||
		command->source_bytes < command->rows * image->width * 4)
		return;
	for (row = 0; row < command->rows; row++)
		memcpy((uint8_t *)surface->image + (size_t)(command->top + row) * surface->pitch * 4,
			(const uint8_t *)(uintptr_t)command->source + (size_t)row * image->width * 4, (size_t)image->width * 4);
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, surface->image, surface->imageSize);
}

static void texture_free(const struct dk_command_texture_free *command)
{
	if (images && command->id && command->id < DK_TEXTURE_LIMIT)
		image_release(&images[command->id]);
}

static GX2TexClampMode wrap_of(uint32_t wrap)
{
	switch (wrap)
	{
	case DK_WRAP_MIRROR: return GX2_TEX_CLAMP_MODE_MIRROR;
	case DK_WRAP_CLAMP: return GX2_TEX_CLAMP_MODE_CLAMP;
	case DK_WRAP_BORDER: return GX2_TEX_CLAMP_MODE_CLAMP_BORDER;
	default: return GX2_TEX_CLAMP_MODE_WRAP;
	}
}

static void sampler_set(GX2Sampler *sampler, const struct dk_sampler *s)
{
	GX2InitSampler(sampler, GX2_TEX_CLAMP_MODE_WRAP, GX2_TEX_XY_FILTER_MODE_POINT);
	GX2InitSamplerClamping(sampler, wrap_of(s->wrap[0]), wrap_of(s->wrap[1]), wrap_of(s->wrap[2]));
	GX2InitSamplerXYFilter(sampler, s->mag_linear ? GX2_TEX_XY_FILTER_MODE_LINEAR : GX2_TEX_XY_FILTER_MODE_POINT,
		s->min_linear ? GX2_TEX_XY_FILTER_MODE_LINEAR : GX2_TEX_XY_FILTER_MODE_POINT, GX2_TEX_ANISO_RATIO_NONE);
	/* (only level 0 is uploaded so far: the mip filter is none) */
	GX2InitSamplerZMFilter(sampler, GX2_TEX_Z_FILTER_MODE_NONE, GX2_TEX_MIP_FILTER_MODE_NONE);
	GX2InitSamplerLOD(sampler, 0.0f, 0.0f, 0.0f);
	GX2InitSamplerBorderType(sampler, GX2_TEX_BORDER_TYPE_TRANSPARENT_BLACK);
}

static void textures_set(const struct dk_command_textures *command)
{
	unsigned stage;

	for (stage = 0; stage < 4; stage++)
	{
		gx.stages[stage] = command->stages[stage];
		sampler_set(&gx.samplers[stage], &command->stages[stage].sampler);
	}
}

/* the texture a stage samples: its image, the render target it names, or a white texel */
static const GX2Texture *stage_texture(const struct dk_stage_texture *stage)
{
	if (stage->id && stage->id < DK_TEXTURE_LIMIT && images && images[stage->id].used && images[stage->id].supported)
		return &images[stage->id].texture;
	if (!stage->id && stage->target)
	{
		struct target *found = NULL;
		unsigned i;

		for (i = 0; i < gx.target_count; i++)
			if (gx.targets[i].surface.kind == DK_SURFACE_COLOR && gx.targets[i].surface.data == stage->target &&
				(!found || gx.targets[i].last_used >= found->last_used))
				found = &gx.targets[i];
		if (found)
		{
			GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_COLOR_BUFFER | GX2_INVALIDATE_MODE_TEXTURE), found->color.surface.image,
				found->color.surface.imageSize);
			return &found->texture;
		}
	}
	return &gx.white;
}

static void textures_bind(const struct shader *ps)
{
	unsigned stage;

	for (stage = 0; stage < 4; stage++)
	{
		if (ps->sampler[stage] < 0)
			continue;
		GX2SetPixelTexture(stage_texture(&gx.stages[stage]), (uint32_t)ps->sampler[stage]);
		GX2SetPixelSampler(&gx.samplers[stage], (uint32_t)ps->sampler[stage]);
	}
}

static int white_make(void)
{
	GX2Surface *surface = &gx.white.surface;

	surface->dim = GX2_SURFACE_DIM_TEXTURE_2D;
	surface->width = 1;
	surface->height = 1;
	surface->depth = 1;
	surface->mipLevels = 1;
	surface->format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
	surface->aa = GX2_AA_MODE1X;
	surface->use = GX2_SURFACE_USE_TEXTURE;
	surface->tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
	GX2CalcSurfaceSizeAndAlignment(surface);
	surface->image = gx_alloc_raw(surface->imageSize, surface->alignment ? surface->alignment : 256);
	if (!surface->image)
		return 0;
	memset(surface->image, 0xff, surface->imageSize);
	gx.white.viewNumMips = 1;
	gx.white.viewNumSlices = 1;
	gx.white.compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
	GX2InitTextureRegs(&gx.white);
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, surface->image, surface->imageSize);
	return 1;
}

/* ---------- draws */

static GX2PrimitiveMode primitive_of(uint32_t primitive)
{
	switch (primitive)
	{
	case DK_PRIMITIVE_POINTS: return GX2_PRIMITIVE_MODE_POINTS;
	case DK_PRIMITIVE_LINES: return GX2_PRIMITIVE_MODE_LINES;
	case DK_PRIMITIVE_LINE_LOOP: return GX2_PRIMITIVE_MODE_LINE_LOOP;
	case DK_PRIMITIVE_LINE_STRIP: return GX2_PRIMITIVE_MODE_LINE_STRIP;
	case DK_PRIMITIVE_TRIANGLE_STRIP: return GX2_PRIMITIVE_MODE_TRIANGLE_STRIP;
	case DK_PRIMITIVE_TRIANGLE_FAN: return GX2_PRIMITIVE_MODE_TRIANGLE_FAN;
	case DK_PRIMITIVE_QUADS: return GX2_PRIMITIVE_MODE_QUADS;
	default: return GX2_PRIMITIVE_MODE_TRIANGLES;
	}
}

/* the draws of the frame halo/gx2_trace.txt names, one line each, to see what the game asked of the GPU */
static void draw_trace(const struct dk_command_draw *command, const struct shader *vs, const struct shader *ps)
{
	const struct dk_draw_state *s = &gx.state;
	const struct dk_vertex_format *f = &gx.format;
	unsigned location, slot = 0, stream;
	char text[512];
	size_t used = 0;
	float first[4] = { 0, 0, 0, 0 };
	char bytes[96] = "";

	for (location = 0; location < DK_ATTRIBUTE_COUNT; location++)
		if (vs->attribute_used & (1u << location))
			used += (size_t)snprintf(text + used, sizeof(text) - used, " %u:f%u@s%u+%u", location, (unsigned)f->attributes[location].format,
				(unsigned)f->attributes[location].stream, (unsigned)f->attributes[location].offset);
	for (stream = 0; stream < DK_STREAM_COUNT; stream++)
	{
		if (!((f->stream_mask >> stream) & 1))
			continue;
		if (slot < command->stream_count && stream == f->attributes[0].stream && f->attributes[0].format >= DK_ATTRIBUTE_FLOAT1 &&
			f->attributes[0].format <= DK_ATTRIBUTE_FLOAT4 && !command->inline_bytes)
		{
			const unsigned char *at = (const unsigned char *)(uintptr_t)command->streams[slot][0];
			unsigned b;

			memcpy(first, at + f->attributes[0].offset, sizeof(float) * 3);
			for (b = 0; b < 32; b++)
				snprintf(bytes + b * 2 + b / 4, sizeof(bytes) - b * 2 - b / 4, "%02x%s", at[b], b % 4 == 3 ? " " : "");
		}
		slot++;
	}
	wiiu_log("GX2 draw %u: vs %u ps %u prim %u count %u idx %s off %d streams %u (%08x, %u bytes, stride %u); attrs%s; v0 %g %g %g bytes %s", gx.traced, (unsigned)gx.vs_handle, (unsigned)gx.ps_handle,
		(unsigned)command->primitive, (unsigned)command->count, command->index_address ? "yes" : "no", (int)command->vertex_offset,
		(unsigned)command->stream_count, command->stream_count ? (unsigned)command->streams[0][0] : 0, command->stream_count ? (unsigned)command->streams[0][1] : 0,
		(unsigned)f->strides[0], text, first[0], first[1], first[2], bytes);
	if (command->inline_bytes)
	{
		const float *vertices = (const float *)(const void *)&command->streams[command->stream_count];
		unsigned vertex;

		for (vertex = 0; vertex < command->count && vertex < 4; vertex++)
		{
			const float *v = vertices + vertex * 64;

			wiiu_log("GX2   vertex %u: r0 %g %g %g %g | r4 %g %g %g %g | r9 %g %g %g %g", vertex, v[0], v[1], v[2], v[3], v[16], v[17], v[18], v[19], v[36], v[37],
				v[38], v[39]);
		}
	}
	wiiu_log("GX2   state: vp %d,%d %dx%d z %g..%g cull %u ccw %u depth %u/%u/%x stencil %u blend %u %x->%x mask %x; tex %u/%u %u/%u %u/%u %u/%u",
		(int)s->viewport[0], (int)s->viewport[1], (int)s->viewport[2], (int)s->viewport[3], s->depth_range[0], s->depth_range[1], (unsigned)s->cull,
		(unsigned)s->front_face_ccw, (unsigned)s->depth_test, (unsigned)s->depth_write, (unsigned)s->depth_function, (unsigned)s->stencil_test,
		(unsigned)s->blend, (unsigned)s->blend_source, (unsigned)s->blend_destination, (unsigned)s->color_mask, (unsigned)gx.stages[0].id,
		(unsigned)gx.stages[0].target, (unsigned)gx.stages[1].id, (unsigned)gx.stages[1].target, (unsigned)gx.stages[2].id, (unsigned)gx.stages[2].target,
		(unsigned)gx.stages[3].id, (unsigned)gx.stages[3].target);
	wiiu_log("GX2   vp params: scale %g %g %g %g offset %g %g %g %g; c[0..3] %g %g %g %g | %g %g %g %g", gx.vertex_parameters.viewport_scale[0],
		gx.vertex_parameters.viewport_scale[1], gx.vertex_parameters.viewport_scale[2], gx.vertex_parameters.viewport_scale[3],
		gx.vertex_parameters.viewport_offset[0], gx.vertex_parameters.viewport_offset[1], gx.vertex_parameters.viewport_offset[2],
		gx.vertex_parameters.viewport_offset[3], gx.constants[0][0], gx.constants[0][1], gx.constants[0][2], gx.constants[0][3], gx.constants[1][0],
		gx.constants[1][1], gx.constants[1][2], gx.constants[1][3]);
}

static void draw(const struct dk_command_draw *command)
{
	struct shader *vs, *ps;
	struct fetch *fetch;
	const unsigned char *inline_data = (const unsigned char *)&command->streams[command->stream_count];
	void *buffers[DK_STREAM_COUNT];
	uint32_t sizes[DK_STREAM_COUNT];
	void *indices = NULL;
	void *constants;
	unsigned stream, slot = 0;

	if (!gx.color && !gx.depth)
	{
		gx.skipped_target++;
		return;
	}
	if (!gx.vs_handle || !gx.ps_handle || !gx.format_set)
	{
		gx.skipped_state++;
		return;
	}
	vs = shader_get(gx.vs_handle);
	ps = shader_get(gx.ps_handle);
	if (!vs || !ps)
	{
		gx.skipped_shader++;
		return;
	}
	fetch = fetch_get(vs);
	if (!fetch)
	{
		gx.skipped_shader++;
		return;
	}
	memset(buffers, 0, sizeof(buffers));
	for (stream = 0; stream < DK_STREAM_COUNT; stream++)
	{
		const unsigned char *source;

		if (!((gx.format.stream_mask >> stream) & 1))
			continue;
		if (slot >= command->stream_count)
			break;
		sizes[stream] = command->streams[slot][1];
		source = command->inline_bytes ? inline_data : (const unsigned char *)(uintptr_t)command->streams[slot][0];
		buffers[stream] = ring_copy(source, sizes[stream]);
		if (!buffers[stream])
		{
			gx.skipped_ring++;
			return;
		}
		slot++;
	}
	constants = ring_copy(gx.format.constants, sizeof(gx.format.constants));
	if (command->index_address)
	{
		const unsigned char *source;

		if (command->index_address == DK_INDEX_INLINE)
			source = inline_data + ((command->inline_bytes + 3u) & ~3u);
		else
			source = (const unsigned char *)(uintptr_t)command->index_address;
		indices = ring_copy(source, command->count * 2);
		if (!indices)
		{
			gx.skipped_ring++;
			return;
		}
	}
	if (!constants)
	{
		gx.skipped_ring++;
		return;
	}
	if (gx.trace_frame >= 0 && (int)gx.frames == gx.trace_frame && gx.traced < 48)
	{
		draw_trace(command, vs, ps);
		gx.traced++;
	}
	if (gx.debug_mode == 1 && gx.state.blend)
	{
		gx.state.blend = 0;
		gx.state_dirty = 1;
	}
	if (gx.debug_mode == 2 && gx.state.cull)
	{
		gx.state.cull = 0;
		gx.state_dirty = 1;
	}
	if (gx.state_dirty)
		state_apply();
	if (gx.cur_fetch != fetch)
	{
		GX2SetFetchShader(&fetch->shader);
		gx.cur_fetch = fetch;
	}
	if (gx.cur_vs != vs)
	{
		GX2SetVertexShader(vs->vertex);
		gx.cur_vs = vs;
	}
	if (gx.cur_ps != ps)
	{
		GX2SetPixelShader(ps->pixel);
		gx.cur_ps = ps;
	}
	uniforms_apply(vs, ps);
	textures_bind(ps);
	for (stream = 0; stream < DK_STREAM_COUNT; stream++)
		if (buffers[stream])
			GX2SetAttribBuffer(stream, sizes[stream], gx.format.strides[stream], buffers[stream]);
	GX2SetAttribBuffer(CONSTANTS_BUFFER, sizeof(gx.format.constants), 0, constants);
	if (gx.color)
		gx.color->last_used = ++gx.use_serial;
	if (indices)
		GX2DrawIndexedEx(primitive_of(command->primitive), command->count, GX2_INDEX_TYPE_U16, indices, (uint32_t)command->vertex_offset, 1);
	else
		GX2DrawEx(primitive_of(command->primitive), command->count, 0, 1);
	gx.draws_made++;
}

/* ---------- starting */

static int initialize(void)
{
	uint32_t i;

	if (gx.ready)
		return 1;
	if (gx.failed)
		return 0;
	gx.failed = 1;
	if (!WHBGfxInit())
	{
		wiiu_log("GX2: WHBGfxInit failed");
		return 0;
	}
	if (!WHBGfxLoadGFDShaderGroup(&gx.blit, 0, wiiu_gx2_blit_gsh))
	{
		wiiu_log("GX2: the blit shaders did not load");
		return 0;
	}
	if (!WHBGfxInitShaderAttribute(&gx.blit, "a_pos", 0, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32) ||
		!WHBGfxInitShaderAttribute(&gx.blit, "a_uv", 0, 8, GX2_ATTRIB_FORMAT_FLOAT_32_32) ||
		!WHBGfxInitShaderAttribute(&gx.blit, "a_col", 0, 16, GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32) || !WHBGfxInitFetchShader(&gx.blit))
	{
		wiiu_log("GX2: the blit shaders lack an input");
		return 0;
	}
	for (i = 0; i < gx.blit.pixelShader->samplerVarCount; i++)
		if (!strcmp(gx.blit.pixelShader->samplerVars[i].name, "tex0"))
			gx.sampler_location = gx.blit.pixelShader->samplerVars[i].location;
	GX2InitSampler(&gx.sampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_LINEAR);
	gx.quad = gx_alloc(6 * sizeof(struct blit_vertex), GX2_VERTEX_BUFFER_ALIGNMENT);
	if (!gx.quad)
	{
		wiiu_log("GX2: no memory for the blit's quad");
		return 0;
	}
	images = calloc(DK_TEXTURE_LIMIT, sizeof(*images));
	if (!images || !white_make())
	{
		wiiu_log("GX2: no memory for the texture table");
		return 0;
	}
	for (i = 0; i < RING_SEGMENTS; i++)
	{
		gx.ring[i].base = gx_alloc_raw(RING_SEGMENT_BYTES, GX2_VERTEX_BUFFER_ALIGNMENT);
		if (!gx.ring[i].base)
		{
			wiiu_log("GX2: no memory for the vertex ring (%u bytes)", (unsigned)RING_SEGMENT_BYTES);
			return 0;
		}
	}
	/* a note in halo/exit_after.txt (seconds) ends the run by itself that long after the first frame: for runs nobody is at the console for */
	{
		FILE *file = fopen(WIIU_DATA_DIRECTORY "/exit_after.txt", "r");

		if (file)
		{
			if (fscanf(file, "%lf", &gx.exit_after) != 1)
				gx.exit_after = 0.0;
			fclose(file);
		}
		gx.trace_frame = -1;
		file = fopen(WIIU_DATA_DIRECTORY "/gx2_trace.txt", "r");
		if (file)
		{
			if (fscanf(file, "%d %d", &gx.trace_frame, &gx.debug_mode) < 1)
				gx.trace_frame = -1;
			fclose(file);
		}
		if (gx.exit_after > 0.0)
			wiiu_log("GX2: the run ends %.0f seconds after its first frame (exit_after.txt)", gx.exit_after);
	}
	gx.state_dirty = 1;
	gx.constants_lo = 192;
	gx.constants_hi = 0;
	gx.failed = 0;
	gx.ready = 1;
	wiiu_log("GX2: started; the TV is %ux%u, the GamePad %ux%u", (unsigned)WHBGfxGetTVColourBuffer()->surface.width,
		(unsigned)WHBGfxGetTVColourBuffer()->surface.height, (unsigned)WHBGfxGetDRCColourBuffer()->surface.width,
		(unsigned)WHBGfxGetDRCColourBuffer()->surface.height);
	return 1;
}

/* ---------- the platform layer's calls about the screen (sdl_platform.h's, as the Wii U answers them) */

/* the display's size: the TV's, in pixels */
int platform_screen_mode(long *width, long *height);

int platform_screen_mode(long *width, long *height)
{
	if (!initialize())
		return 0;
	*width = (long)WHBGfxGetTVColourBuffer()->surface.width;
	*height = (long)WHBGfxGetTVColourBuffer()->surface.height;
	return 1;
}

/* "creates the window and makes its OpenGL context current": the GX2 context was made by initialize */
int platform_video_initialize(unsigned long width, unsigned long height);

int platform_video_initialize(unsigned long width, unsigned long height)
{
	(void)width;
	(void)height;
	return initialize();
}

/* the swap is the present command's */
void platform_video_swap(void);

void platform_video_swap(void)
{
}

/* the window's and controllers' events: ProcUI is the shell's, and the input layer is not written */
void platform_pump_events(void);

void platform_pump_events(void)
{
}

void platform_menus_set_active(int active);

void platform_menus_set_active(int active)
{
	(void)active;
}

/* ---------- render targets */

static struct target *target_get(const struct dk_surface *surface)
{
	struct target *target;
	GX2Surface *s;
	unsigned i;
	const int depth = surface->kind == DK_SURFACE_DEPTH;

	if (surface->kind == DK_SURFACE_NONE || !surface->width || !surface->height)
		return NULL;
	for (i = 0; i < gx.target_count; i++)
		if (!memcmp(&gx.targets[i].surface, surface, sizeof(*surface)))
			return &gx.targets[i];
	if (gx.target_count == TARGET_LIMIT)
	{
		wiiu_log("GX2: more than %d render targets; %08x is not drawn into", TARGET_LIMIT, (unsigned)surface->data);
		return NULL;
	}
	target = &gx.targets[gx.target_count];
	memset(target, 0, sizeof(*target));
	s = depth ? &target->depth.surface : &target->color.surface;
	s->dim = GX2_SURFACE_DIM_TEXTURE_2D;
	s->width = SURFACE_PIXEL_WIDTH(surface);
	s->height = SURFACE_PIXEL_HEIGHT(surface);
	s->depth = 1;
	s->mipLevels = 1;
	s->format = depth ? GX2_SURFACE_FORMAT_FLOAT_D24_S8 : GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
	s->aa = GX2_AA_MODE1X;
	s->use = (GX2SurfaceUse)((depth ? GX2_SURFACE_USE_DEPTH_BUFFER : GX2_SURFACE_USE_COLOR_BUFFER) | GX2_SURFACE_USE_TEXTURE);
	s->tileMode = GX2_TILE_MODE_DEFAULT;
	GX2CalcSurfaceSizeAndAlignment(s);
	s->image = gx_alloc(s->imageSize, s->alignment ? s->alignment : 256);
	if (!s->image)
	{
		wiiu_log("GX2: no memory for the %ux%u %s target at %08x", (unsigned)s->width, (unsigned)s->height, depth ? "depth" : "color",
			(unsigned)surface->data);
		return NULL;
	}
	if (depth)
	{
		target->depth.viewNumSlices = 1;
		target->depth.depthClear = 1.0f;
		GX2InitDepthBufferRegs(&target->depth);
	}
	else
	{
		target->color.viewNumSlices = 1;
		GX2InitColorBufferRegs(&target->color);
		target->texture.surface = target->color.surface;
		target->texture.viewFirstMip = 0;
		target->texture.viewNumMips = 1;
		target->texture.viewFirstSlice = 0;
		target->texture.viewNumSlices = 1;
		target->texture.compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
		GX2InitTextureRegs(&target->texture);
	}
	target->surface = *surface;
	gx.target_count++;
	wiiu_log("GX2: %s target %08x, %ux%u (%ux%u pixels)", depth ? "depth" : "color", (unsigned)surface->data, (unsigned)surface->width,
		(unsigned)surface->height, (unsigned)s->width, (unsigned)s->height);
	return target;
}

static void context_begin(void)
{
	GX2SetContextState(WHBGfxGetTVContextState());
}

/* after a clear or a frame's presenting, which used the context for something else: the game's targets back, and everything a draw needs set again */
static void context_resume(void)
{
	context_begin();
	if (gx.color)
		GX2SetColorBuffer(&gx.color->color, GX2_RENDER_TARGET_0);
	if (gx.depth)
		GX2SetDepthBuffer(&gx.depth->depth);
	GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
	gx.state_dirty = 1;
	gx.regs_vs = gx.regs_ps = gx.cur_vs = gx.cur_ps = NULL;
	gx.cur_fetch = NULL;
	gx.vertex_parameters_dirty = gx.pixel_parameters_dirty = 1;
}

static void targets_bind(const struct dk_command_targets *command)
{
	gx.color = target_get(&command->color);
	gx.depth = target_get(&command->depth);
	context_resume();
	if (gx.color || gx.depth)
	{
		const uint32_t width = gx.color ? gx.color->color.surface.width : gx.depth->depth.surface.width;
		const uint32_t height = gx.color ? gx.color->color.surface.height : gx.depth->depth.surface.height;

		GX2SetViewport(0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f);
		GX2SetScissor(0, 0, width, height);
	}
}

static void clear(const struct dk_command_clear *command)
{
	const uint32_t color_flags = DK_CLEAR_RED | DK_CLEAR_GREEN | DK_CLEAR_BLUE | DK_CLEAR_ALPHA;
	const int whole_color = (command->flags & color_flags) == color_flags;

	/* GX2 clears a whole buffer: a clear of part of a target (a split-screen window), or of some channels (the fog screen's alpha only), is
	made as a whole one for now, and counted */
	if (command->rectangle_count)
	{
		const struct target *size_from = gx.color ? gx.color : gx.depth;

		if (size_from)
		{
			const uint32_t *rectangle = command->rectangles[0];

			if (command->rectangle_count != 1 || rectangle[0] || rectangle[1] || rectangle[2] != SURFACE_PIXEL_WIDTH(&size_from->surface) ||
				rectangle[3] != SURFACE_PIXEL_HEIGHT(&size_from->surface))
				gx.partial_clears++;
		}
	}
	if (gx.color && (command->flags & color_flags))
	{
		if (!whole_color)
			gx.partial_clears++;
		else
			GX2ClearColor(&gx.color->color, command->color[0], command->color[1], command->color[2], command->color[3]);
	}
	if (gx.depth && (command->flags & (DK_CLEAR_DEPTH | DK_CLEAR_STENCIL)))
	{
		GX2ClearFlags flags = (GX2ClearFlags)0;

		if (command->flags & DK_CLEAR_DEPTH)
			flags = (GX2ClearFlags)(flags | GX2_CLEAR_FLAGS_DEPTH);
		if (command->flags & DK_CLEAR_STENCIL)
			flags = (GX2ClearFlags)(flags | GX2_CLEAR_FLAGS_STENCIL);
		GX2ClearDepthStencilEx(&gx.depth->depth, command->depth, (uint8_t)command->stencil, flags);
	}
	/* (a clear is a draw of its own: the state the draws need goes back) */
	context_resume();
}

/* ---------- presenting */

static void blit_state(void)
{
	GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS);
	GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);
	GX2SetColorControl(GX2_LOGIC_OP_COPY, 0, FALSE, TRUE);
	GX2SetTargetChannelMasks(GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA,
		GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA);
}

/* the back buffer, letterboxed, onto the screen whose color buffer and context state are given */
static void blit_to(struct target *source, GX2ColorBuffer *screen, GX2ContextState *context)
{
	const float source_width = (float)source->color.surface.width, source_height = (float)source->color.surface.height;
	const float screen_width = (float)screen->surface.width, screen_height = (float)screen->surface.height;
	float width = screen_width, height = screen_width * source_height / source_width;
	float x0, y0, x1, y1;
	static const unsigned corner_x[6] = { 0, 1, 0, 1, 1, 0 };
	static const unsigned corner_y[6] = { 0, 0, 1, 0, 1, 1 };
	unsigned i;

	if (height > screen_height)
	{
		height = screen_height;
		width = screen_height * source_width / source_height;
	}
	x0 = ((screen_width - width) / 2.0f) / screen_width * 2.0f - 1.0f;
	x1 = x0 + width / screen_width * 2.0f;
	y1 = 1.0f - ((screen_height - height) / 2.0f) / screen_height * 2.0f;
	y0 = y1 - height / screen_height * 2.0f;
	for (i = 0; i < 6; i++)
	{
		struct blit_vertex *v = &gx.quad[i];

		v->x = corner_x[i] ? x1 : x0;
		v->y = corner_y[i] ? y0 : y1;
		v->u = (float)corner_x[i];
		v->v = (float)corner_y[i];
		v->r = v->g = v->b = v->a = 1.0f;
	}
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, gx.quad, 6 * sizeof(struct blit_vertex));
	GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_COLOR_BUFFER | GX2_INVALIDATE_MODE_TEXTURE), source->color.surface.image,
		source->color.surface.imageSize);
	GX2SetContextState(context);
	GX2ClearColor(screen, 0.0f, 0.0f, 0.0f, 1.0f);
	GX2SetContextState(context);
	GX2SetColorBuffer(screen, GX2_RENDER_TARGET_0);
	GX2SetViewport(0.0f, 0.0f, screen_width, screen_height, 0.0f, 1.0f);
	GX2SetScissor(0, 0, (uint32_t)screen_width, (uint32_t)screen_height);
	blit_state();
	GX2SetFetchShader(&gx.blit.fetchShader);
	GX2SetVertexShader(gx.blit.vertexShader);
	GX2SetPixelShader(gx.blit.pixelShader);
	GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
	GX2SetAttribBuffer(0, 6 * sizeof(struct blit_vertex), sizeof(struct blit_vertex), gx.quad);
	GX2SetPixelTexture(&source->texture, gx.sampler_location);
	GX2SetPixelSampler(&gx.sampler, gx.sampler_location);
	GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, 6, 0, 1);
}

/* the back buffer's pixels, R G B A in memory, for debug.screenshot_every (d3d8_gx2.c writes the file): copied by the GPU into a linear surface, which the
CPU can read */
static void screenshot_take(struct target *source, uint32_t address)
{
	GX2Surface linear = source->color.surface;
	uint8_t *out = (uint8_t *)(uintptr_t)address;
	unsigned y;

	linear.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
	linear.use = GX2_SURFACE_USE_TEXTURE;
	linear.image = NULL;
	linear.mipmaps = NULL;
	linear.imageSize = 0;
	linear.pitch = 0;
	linear.alignment = 0;
	GX2CalcSurfaceSizeAndAlignment(&linear);
	linear.image = gx_alloc_raw(linear.imageSize, linear.alignment ? linear.alignment : 256);
	if (!linear.image)
		return;
	GX2DrawDone();
	GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_COLOR_BUFFER | GX2_INVALIDATE_MODE_TEXTURE), source->color.surface.image,
		source->color.surface.imageSize);
	GX2CopySurface(&source->color.surface, 0, 0, &linear, 0, 0);
	GX2DrawDone();
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU, linear.image, linear.imageSize);
	for (y = 0; y < linear.height; y++)
		memcpy(out + (size_t)y * linear.width * 4, (uint8_t *)linear.image + (size_t)y * linear.pitch * 4, (size_t)linear.width * 4);
	MEMFreeToDefaultHeap(linear.image);
	context_resume();
}

static void present(const struct dk_command_present *command)
{
	struct target *back = target_get(&command->back_buffer);
	const OSTime now = OSGetSystemTime();

	if (back && command->screenshot)
		screenshot_take(back, command->screenshot);
	/* (before the frame is drawn, as the screenshot above is: the screens' buffers then hold the frame before) */
	WHBGfxBeginRender();
	WHBGfxBeginRenderTV();
	if (back)
		blit_to(back, WHBGfxGetTVColourBuffer(), WHBGfxGetTVContextState());
	WHBGfxFinishRenderTV();
	WHBGfxBeginRenderDRC();
	if (back)
		blit_to(back, WHBGfxGetDRCColourBuffer(), WHBGfxGetDRCContextState());
	WHBGfxFinishRenderDRC();
	WHBGfxFinishRender();
	gx.frames++;
	if (gx.frames == 1)
	{
		wiiu_log("GX2: first frame presented");
		gx.started = now;
	}
	if (gx.exit_after > 0.0 && (double)(now - gx.started) / (double)OSSecondsToTicks(1) >= gx.exit_after)
	{
		wiiu_log("GX2: the run's time is up");
		gx.exit_after = 0.0;
		wiiu_request_exit();
	}
	if (!gx.told || now - gx.told >= OSSecondsToTicks(5))
	{
		unsigned i;

		gx.told = now;
		wiiu_log("GX2: %u frames; commands since the last note, by type (target 1, clear 2, present 3, state 4, shaders 5 ...): ", gx.frames);
		for (i = 1; i < 18; i++)
			if (gx.counts[i])
				wiiu_log("GX2:   command %u: %u", i, gx.counts[i]);
		wiiu_log("GX2:   %u draws made; skipped: %u no target, %u no shader or format yet, %u shader not in the pack or not loaded, %u ring full; %u clears "
			"of part of a target or of some channels made whole or skipped; %u textures made (%u not 2D), ring overflows %u", gx.draws_made,
			gx.skipped_target, gx.skipped_state, gx.skipped_shader, gx.skipped_ring, gx.partial_clears, gx.textures_made, gx.textures_unsupported,
			gx.ring_overflows);
		memset(gx.counts, 0, sizeof(gx.counts));
		gx.draws_made = gx.skipped_target = gx.skipped_state = gx.skipped_shader = gx.skipped_ring = 0;
		gx.partial_clears = 0;
		gx.textures_made = gx.textures_unsupported = 0;
	}
	ring_next();
	deferred_flush(0);
	context_resume();
}

/* ---------- the stream */

void host_gx2_submit(uint32_t commands, uint32_t size);
uint32_t host_gx2_retired(void);
void host_gx2_idle(void);
uint32_t host_gx2_visibility(uint32_t index);
unsigned int host_gx2_output_height(void);

/* the shell's (wiiu_shell.c): held here while the app is in the background */
void wiiu_foreground_gate(int is_game_thread);

void host_gx2_submit(uint32_t commands, uint32_t size)
{
	const unsigned char *at = (const unsigned char *)(uintptr_t)commands;
	const unsigned char *end = at + size;

	wiiu_foreground_gate(1);
	if (!initialize())
		return;
	while (at + sizeof(struct dk_command_header) <= end)
	{
		const struct dk_command_header *header = (const struct dk_command_header *)at;

		if (header->size < sizeof(*header) || at + header->size > end)
		{
			wiiu_log("GX2: a command of %u bytes runs past the stream's end; the rest is dropped", (unsigned)header->size);
			break;
		}
		if (header->type < 32)
			gx.counts[header->type]++;
		switch (header->type)
		{
		case DK_COMMAND_TARGETS:
			targets_bind((const struct dk_command_targets *)header);
			break;
		case DK_COMMAND_CLEAR:
			clear((const struct dk_command_clear *)header);
			break;
		case DK_COMMAND_PRESENT:
			present((const struct dk_command_present *)header);
			break;
		case DK_COMMAND_STATE:
			gx.state = ((const struct dk_command_state *)header)->state;
			gx.state_dirty = 1;
			break;
		case DK_COMMAND_SHADERS:
			gx.vs_handle = ((const struct dk_command_shaders *)header)->vertex;
			gx.ps_handle = ((const struct dk_command_shaders *)header)->pixel;
			break;
		case DK_COMMAND_CONSTANTS:
		{
			const struct dk_command_constants *constants = (const struct dk_command_constants *)header;

			if (constants->first < 192 && constants->count && constants->first + constants->count <= 192)
			{
				memcpy(gx.constants[constants->first], constants->data, constants->count * 4 * sizeof(float));
				if (constants->first < gx.constants_lo)
					gx.constants_lo = constants->first;
				if (constants->first + constants->count - 1 > gx.constants_hi || gx.constants_lo > gx.constants_hi)
					gx.constants_hi = constants->first + constants->count - 1;
			}
			break;
		}
		case DK_COMMAND_VERTEX_PARAMETERS:
			gx.vertex_parameters = ((const struct dk_command_vertex_parameters *)header)->parameters;
			gx.vertex_parameters_dirty = 1;
			break;
		case DK_COMMAND_PIXEL_PARAMETERS:
			gx.pixel_parameters = ((const struct dk_command_pixel_parameters *)header)->parameters;
			gx.pixel_parameters_dirty = 1;
			break;
		case DK_COMMAND_VERTEX_FORMAT:
			gx.format = ((const struct dk_command_vertex_format *)header)->format;
			gx.format_set = 1;
			break;
		case DK_COMMAND_TEXTURE:
			texture_make((const struct dk_command_texture *)header);
			break;
		case DK_COMMAND_TEXTURE_ROWS:
			texture_rows((const struct dk_command_texture_rows *)header);
			break;
		case DK_COMMAND_TEXTURE_FREE:
			texture_free((const struct dk_command_texture_free *)header);
			break;
		case DK_COMMAND_TEXTURES:
			textures_set((const struct dk_command_textures *)header);
			break;
		case DK_COMMAND_DRAW:
			draw((const struct dk_command_draw *)header);
			break;
		default:
			break;
		}
		at += header->size;
	}
}

/* the highest submission the GPU has finished: everything handed over is waited for at its present, so all of it */
uint32_t host_gx2_retired(void)
{
	return 0xffffffffu;
}

void host_gx2_idle(void)
{
	GX2DrawDone();
}

uint32_t host_gx2_visibility(uint32_t index)
{
	(void)index;
	return 0;
}

/* the lines the screen's targets are drawn at, at most: the TV's */
unsigned int host_gx2_output_height(void)
{
	return initialize() ? (unsigned int)WHBGfxGetTVColourBuffer()->surface.height : 480u;
}
