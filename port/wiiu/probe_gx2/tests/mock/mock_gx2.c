#include "mock_gx2.h"

#include "../../src/index_test.h"
#include "../../src/scene.h"
#include "../../src/shader_data.h"

#include <cafeglsl/CafeGLSLCompiler.h>
#include <coreinit/cache.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memexpheap.h>
#include <coreinit/memheap.h>
#include <coreinit/thread.h>
#include <gx2/clear.h>
#include <gx2/context.h>
#include <gx2/display.h>
#include <gx2/draw.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/sampler.h>
#include <gx2/shaders.h>
#include <gx2/state.h>
#include <gx2/surface.h>
#include <gx2/utils.h>
#include <gx2/texture.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vpad/input.h>
#include <whb/gfx.h>

mock_config mock;
unsigned mock_draws, mock_scene_draws;

void mock_reset(void)
{
	unsigned i;

	memset(&mock, 0, sizeof(mock));
	for (i = 0; i < 4; i++)
		mock.target_order.source[i] = (uint8_t)i;
	for (i = 0; i < 16; i++)
		mock.bc1_gpu.source[i] = mock.bc2_gpu.source[i] = mock.bc3_gpu.source[i] = (uint8_t)i;
	mock.default_swap = VTX_SWAP_8_IN_32;
	mock_draws = mock_scene_draws = 0;
}

/* ---------- memory and cache */

static void *mock_alloc(uint32_t size, int32_t alignment)
{
	void *memory = NULL;

	if (posix_memalign(&memory, alignment < 8 ? 8 : (size_t)alignment, size ? size : 1))
		return NULL;
	return memory;
}

static void mock_free(void *memory)
{
	free(memory);
}

MEMAllocFromDefaultHeapExFn MEMAllocFromDefaultHeapEx = mock_alloc;
MEMFreeToDefaultHeapFn MEMFreeToDefaultHeap = mock_free;

static uint32_t heap_free = 200u << 20;

static MEMHeapHeader mem2_heap = { .tag = MEM_EXPANDED_HEAP_TAG };

MEMHeapHandle MEMGetBaseHeapHandle(MEMBaseHeapType type)
{
	(void)type;
	return &mem2_heap;
}

uint32_t MEMGetTotalFreeSizeForExpHeap(MEMHeapHandle heap)
{
	(void)heap;
	return heap_free;
}

void DCInvalidateRange(void *addr, uint32_t size)
{
	(void)addr;
	(void)size;
}

void GX2Invalidate(GX2InvalidateMode mode, void *buffer, uint32_t size)
{
	(void)mode;
	(void)buffer;
	(void)size;
}

void GX2Flush(void)
{
}

BOOL GX2DrawDone(void)
{
	return TRUE;
}

/* ---------- surfaces */

static unsigned format_unit_bytes(GX2SurfaceFormat format, int *compressed)
{
	*compressed = 0;
	switch (format)
	{
	case GX2_SURFACE_FORMAT_UNORM_BC1:
		*compressed = 1;
		return 8;
	case GX2_SURFACE_FORMAT_UNORM_BC2:
	case GX2_SURFACE_FORMAT_UNORM_BC3:
		*compressed = 1;
		return 16;
	case GX2_SURFACE_FORMAT_UNORM_R8:
		return 1;
	default:
		return 4;
	}
}

void GX2CalcSurfaceSizeAndAlignment(GX2Surface *s)
{
	int compressed;
	unsigned unit = format_unit_bytes(s->format, &compressed);
	unsigned pitch, size;

	if (compressed)
	{
		if (mock.pitch_in_blocks)
		{
			pitch = ((s->width / 4) + 63) & ~63u;
			size = pitch * (s->height / 4) * unit;
		}
		else
		{
			pitch = (s->width + 63) & ~63u;
			size = (pitch / 4) * (s->height / 4) * unit;
		}
	}
	else
	{
		pitch = (s->width + 63) & ~63u;
		size = pitch * s->height * unit;
	}
	s->pitch = pitch;
	s->imageSize = (size + 255) & ~255u;
	s->alignment = 256;
}

void GX2InitTextureRegs(GX2Texture *texture)
{
	(void)texture;
}

void GX2InitColorBufferRegs(GX2ColorBuffer *colorBuffer)
{
	(void)colorBuffer;
}

void GX2InitSampler(GX2Sampler *sampler, GX2TexClampMode clampMode, GX2TexXYFilterMode minMagFilterMode)
{
	(void)clampMode;
	(void)minMagFilterMode;
	memset(sampler, 0, sizeof(*sampler));
}

/* ---------- state the draws read */

static GX2ColorBuffer *bound_color;
static const GX2Texture *bound_texture;
static const GX2VertexShader *bound_vs;
static const GX2PixelShader *bound_ps;
static const GX2FetchShader *bound_fetch;
static struct
{
	const void *pointer;
	uint32_t size, stride;
} buffers[16];
static float registers[1024];

void GX2SetContextState(GX2ContextState *state)
{
	(void)state;
}

void GX2SetColorBuffer(const GX2ColorBuffer *colorBuffer, GX2RenderTarget target)
{
	(void)target;
	bound_color = (GX2ColorBuffer *)colorBuffer;
}

void GX2SetDepthBuffer(const GX2DepthBuffer *depthBuffer)
{
	(void)depthBuffer;
}

void GX2SetViewport(float x, float y, float width, float height, float nearZ, float farZ)
{
	(void)x; (void)y; (void)width; (void)height; (void)nearZ; (void)farZ;
}

void GX2SetScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
	(void)x; (void)y; (void)width; (void)height;
}

void GX2SetDepthOnlyControl(BOOL depthTest, BOOL depthWrite, GX2CompareFunction depthCompare)
{
	(void)depthTest; (void)depthWrite; (void)depthCompare;
}

void GX2SetCullOnlyControl(GX2FrontFace frontFace, BOOL cullFront, BOOL cullBack)
{
	(void)frontFace; (void)cullFront; (void)cullBack;
}

void GX2SetColorControl(GX2LogicOp rop3, uint8_t targetBlendEnable, BOOL multiWriteEnable, BOOL colorWriteEnable)
{
	(void)rop3; (void)targetBlendEnable; (void)multiWriteEnable; (void)colorWriteEnable;
}

void GX2SetBlendControl(GX2RenderTarget target, GX2BlendMode colorSrcBlend, GX2BlendMode colorDstBlend, GX2BlendCombineMode colorCombine,
	BOOL useAlphaBlend, GX2BlendMode alphaSrcBlend, GX2BlendMode alphaDstBlend, GX2BlendCombineMode alphaCombine)
{
	(void)target; (void)colorSrcBlend; (void)colorDstBlend; (void)colorCombine; (void)useAlphaBlend; (void)alphaSrcBlend; (void)alphaDstBlend;
	(void)alphaCombine;
}

void GX2SetTargetChannelMasks(GX2ChannelMask m0, GX2ChannelMask m1, GX2ChannelMask m2, GX2ChannelMask m3, GX2ChannelMask m4, GX2ChannelMask m5,
	GX2ChannelMask m6, GX2ChannelMask m7)
{
	(void)m0; (void)m1; (void)m2; (void)m3; (void)m4; (void)m5; (void)m6; (void)m7;
}

void GX2SetFetchShader(const GX2FetchShader *shader)
{
	bound_fetch = shader;
}

void GX2SetVertexShader(const GX2VertexShader *shader)
{
	bound_vs = shader;
}

void GX2SetPixelShader(const GX2PixelShader *shader)
{
	bound_ps = shader;
}

void GX2SetShaderModeEx(GX2ShaderMode mode, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f)
{
	(void)mode; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
}

void GX2SetPixelTexture(const GX2Texture *texture, uint32_t unit)
{
	(void)unit;
	bound_texture = texture;
}

void GX2SetPixelSampler(const GX2Sampler *sampler, uint32_t id)
{
	(void)sampler;
	(void)id;
}

void GX2SetAttribBuffer(uint32_t index, uint32_t size, uint32_t stride, const void *buffer)
{
	buffers[index].pointer = buffer;
	buffers[index].size = size;
	buffers[index].stride = stride;
}

void GX2SetVertexUniformReg(uint32_t offset, uint32_t count, const void *data)
{
	if (offset + count <= 1024)
		memcpy(registers + offset, data, count * 4);
}

/* ---------- fetch shaders: the attribute list lives in the "program" */

uint32_t GX2CalcFetchShaderSizeEx(uint32_t attribs, GX2FetchShaderType fetchShaderType, GX2TessellationMode tesellationMode)
{
	(void)fetchShaderType;
	(void)tesellationMode;
	return 16 + attribs * (uint32_t)sizeof(GX2AttribStream);
}

void GX2InitFetchShaderEx(GX2FetchShader *fetchShader, uint8_t *buffer, uint32_t attribCount, const GX2AttribStream *attribs, GX2FetchShaderType type,
	GX2TessellationMode tessMode)
{
	(void)type;
	(void)tessMode;
	memset(fetchShader, 0, sizeof(*fetchShader));
	fetchShader->program = buffer;
	fetchShader->attribCount = attribCount;
	memcpy(buffer, attribs, attribCount * sizeof(GX2AttribStream));
}

/* ---------- the GPU's model of a fetch */

static unsigned attrib_bytes(GX2AttribFormat f)
{
	switch ((unsigned)f)
	{
	case GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32: return 16;
	case GX2_ATTRIB_FORMAT_FLOAT_32_32: return 8;
	case 0x20E: return 8;
	default: return 4;
	}
}

static vtx_format attrib_format(GX2AttribFormat f)
{
	switch ((unsigned)f)
	{
	case GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32: return VTX_FLOAT32X4;
	case GX2_ATTRIB_FORMAT_FLOAT_32_32: return VTX_FLOAT32X2;
	case GX2_ATTRIB_FORMAT_UNORM_8_8_8_8: return VTX_UNORM8X4;
	case 0x20E: return VTX_SNORM16X4;
	case 0xA07: return VTX_SSCALED16X2;
	default: return VTX_PACKED3;
	}
}

static void fetch_attrib(const GX2AttribStream *a, unsigned vertex, float out[4])
{
	uint8_t bytes[VTX_MAX_BYTES] = { 0 };
	unsigned n = attrib_bytes(a->format);
	size_t offset = (size_t)vertex * buffers[a->buffer].stride + a->offset;
	vtx_format format = attrib_format(a->format);
	vtx_swap hypothesis;
	float fetched[4];
	uint8_t sel[4];
	unsigned i;

	/* beyond the buffer's end: zeros (the stages size their buffers so that nothing reads further than they fill) */
	if (offset + n <= buffers[a->buffer].size)
		memcpy(bytes, (const uint8_t *)buffers[a->buffer].pointer + offset, n);
	hypothesis = a->endianSwap == GX2_ENDIAN_SWAP_NONE ? VTX_SWAP_NONE : a->endianSwap == GX2_ENDIAN_SWAP_8_IN_16 ? VTX_SWAP_8_IN_16 :
		a->endianSwap == GX2_ENDIAN_SWAP_8_IN_32 ? VTX_SWAP_8_IN_32 : mock.default_swap;
	vtx_decode(format, hypothesis, bytes, fetched);
	if (format == VTX_PACKED3)
	{
		memcpy(out, fetched, 16); /* (the vertex shader unpacks it) */
		return;
	}
	for (i = 0; i < 4; i++)
		sel[i] = (uint8_t)((a->mask >> (24 - 8 * i)) & 7);
	vtx_select(fetched, sel, out);
}

static const GX2AttribStream *attrib_at(unsigned location)
{
	const GX2AttribStream *attribs = bound_fetch ? bound_fetch->program : NULL;
	unsigned i;

	for (i = 0; attribs && i < bound_fetch->attribCount; i++)
		if (attribs[i].location == location)
			return &attribs[i];
	return NULL;
}

/* ---------- shaders (fakes with the names, locations and registers the real ones have) */

typedef enum shader_kind
{
	KIND_NONE,
	KIND_UI,
	KIND_ATTR,
	KIND_PACKED,
	KIND_GENERATED,
	KIND_OTHER
} shader_kind;

static struct
{
	const void *pointer;
	shader_kind kind;
} tags[256];
static unsigned tag_count;

static void tag_set(const void *pointer, shader_kind kind)
{
	tags[tag_count].pointer = pointer;
	tags[tag_count++].kind = kind;
}

static shader_kind tag_get(const void *pointer)
{
	unsigned i;

	for (i = 0; i < tag_count; i++)
		if (tags[i].pointer == pointer)
			return tags[i].kind;
	return KIND_NONE;
}

static GX2VertexShader *make_vertex_shader(const char *const *names, const unsigned *locations, const GX2ShaderVarType *types, unsigned count,
	int uniforms, const uint32_t *out_ids, unsigned out_words, uint32_t program_size, uint8_t fill)
{
	GX2VertexShader *vs = calloc(1, sizeof(*vs));
	unsigned i;

	vs->attribVarCount = count;
	vs->attribVars = calloc(count, sizeof(GX2AttribVar));
	for (i = 0; i < count; i++)
	{
		vs->attribVars[i].name = names[i];
		vs->attribVars[i].type = types[i];
		vs->attribVars[i].location = locations[i];
	}
	if (uniforms)
	{
		vs->uniformVarCount = 5;
		vs->uniformVars = calloc(5, sizeof(GX2UniformVar));
		vs->uniformVars[0].name = "c"; vs->uniformVars[0].type = GX2_SHADER_VAR_TYPE_FLOAT4; vs->uniformVars[0].count = 60; vs->uniformVars[0].offset = 0;
		vs->uniformVars[1].name = "viewport_scale"; vs->uniformVars[1].type = GX2_SHADER_VAR_TYPE_FLOAT4; vs->uniformVars[1].count = 1; vs->uniformVars[1].offset = 240;
		vs->uniformVars[2].name = "viewport_offset"; vs->uniformVars[2].type = GX2_SHADER_VAR_TYPE_FLOAT4; vs->uniformVars[2].count = 1; vs->uniformVars[2].offset = 244;
		vs->uniformVars[3].name = "point_size"; vs->uniformVars[3].type = GX2_SHADER_VAR_TYPE_FLOAT; vs->uniformVars[3].count = 1; vs->uniformVars[3].offset = 248;
		vs->uniformVars[4].name = "screen_offset"; vs->uniformVars[4].type = GX2_SHADER_VAR_TYPE_FLOAT; vs->uniformVars[4].count = 1; vs->uniformVars[4].offset = 252;
	}
	vs->regs.num_spi_vs_out_id = out_words;
	memcpy(vs->regs.spi_vs_out_id, out_ids, out_words * 4);
	vs->size = program_size;
	vs->program = malloc(program_size);
	memset(vs->program, fill, program_size);
	return vs;
}

static GX2PixelShader *make_pixel_shader(int sampler, const uint32_t *inputs, unsigned input_count, uint32_t program_size, uint8_t fill)
{
	GX2PixelShader *ps = calloc(1, sizeof(*ps));

	if (sampler)
	{
		ps->samplerVarCount = 1;
		ps->samplerVars = calloc(1, sizeof(GX2SamplerVar));
		ps->samplerVars[0].name = "tex0";
		ps->samplerVars[0].type = GX2_SAMPLER_VAR_TYPE_SAMPLER_2D;
		ps->samplerVars[0].location = 0;
	}
	ps->regs.num_spi_ps_input_cntl = input_count;
	memcpy(ps->regs.spi_ps_input_cntls, inputs, input_count * 4);
	ps->size = program_size;
	ps->program = malloc(program_size);
	memset(ps->program, fill, program_size);
	return ps;
}

static const uint32_t ui_vs_ids[1] = { 0xFFFF8B8Au }, ui_ps_inputs[2] = { 0x8A, 0x8B };
static const uint32_t generated_vs_ids[3] = { 0x8D8C8B8Au, 0x91908F8Eu, 0xFFFFFF92u }, generated_ps_inputs[1] = { 0x8C };
static const uint32_t text_ps_inputs[2] = { 0x8C, 0x8F };
static const uint32_t one_vs_id[1] = { 0xFFFFFF8Au };

BOOL WHBGfxLoadGFDShaderGroup(WHBGfxShaderGroup *group, uint32_t index, const void *file)
{
	static const char *const ui_names[3] = { "a_pos", "a_uv", "a_col" };
	static const unsigned ui_locations[3] = { 0, 1, 2 };
	static const GX2ShaderVarType ui_types[3] = { GX2_SHADER_VAR_TYPE_FLOAT2, GX2_SHADER_VAR_TYPE_FLOAT2, GX2_SHADER_VAR_TYPE_FLOAT4 };
	static const char *const attr_names[2] = { "a_pos", "a_val" };
	static const char *const packed_names[2] = { "a_pos", "a_packed" };
	static const unsigned two_locations[2] = { 0, 1 };
	static const GX2ShaderVarType attr_types[2] = { GX2_SHADER_VAR_TYPE_FLOAT2, GX2_SHADER_VAR_TYPE_FLOAT4 };
	static const GX2ShaderVarType packed_types[2] = { GX2_SHADER_VAR_TYPE_FLOAT2, GX2_SHADER_VAR_TYPE_UINT };
	static const char *const gen_names[2] = { "v0_in", "v9_in" };
	static const unsigned gen_locations[2] = { 0, 9 };
	static const GX2ShaderVarType gen_types[2] = { GX2_SHADER_VAR_TYPE_FLOAT4, GX2_SHADER_VAR_TYPE_FLOAT4 };
	unsigned which;

	(void)index;
	memset(group, 0, sizeof(*group));
	for (which = 0; which < GSH_COUNT; which++)
		if (gsh_files[which].data == file)
			break;
	switch (which)
	{
	case GSH_UI:
		group->vertexShader = make_vertex_shader(ui_names, ui_locations, ui_types, 3, 0, ui_vs_ids, 1, 32, 0x11);
		group->pixelShader = make_pixel_shader(1, ui_ps_inputs, 2, 80, 0x12);
		tag_set(group->pixelShader, KIND_UI);
		break;
	case GSH_ATTR:
		group->vertexShader = make_vertex_shader(attr_names, two_locations, attr_types, 2, 0, one_vs_id, 1, 48, 0x21);
		group->pixelShader = make_pixel_shader(0, ui_ps_inputs, 1, 48, 0x22);
		tag_set(group->pixelShader, KIND_ATTR);
		break;
	case GSH_PACKED:
		group->vertexShader = make_vertex_shader(packed_names, two_locations, packed_types, 2, 0, one_vs_id, 1, 168, 0x31);
		group->pixelShader = make_pixel_shader(0, ui_ps_inputs, 1, 48, 0x32);
		tag_set(group->pixelShader, KIND_PACKED);
		break;
	case GSH_GENERATED:
		group->vertexShader = make_vertex_shader(gen_names, gen_locations, gen_types, 2, 1, generated_vs_ids, 3, 568, 0x41);
		group->pixelShader = make_pixel_shader(0, generated_ps_inputs, 1, 48, 0x42);
		tag_set(group->pixelShader, KIND_GENERATED);
		break;
	default:
		return FALSE;
	}
	return TRUE;
}

BOOL WHBGfxInitShaderAttribute(WHBGfxShaderGroup *group, const char *name, uint32_t buffer, uint32_t offset, GX2AttribFormat format)
{
	unsigned i;

	for (i = 0; i < group->vertexShader->attribVarCount; i++)
	{
		if (!strcmp(group->vertexShader->attribVars[i].name, name))
		{
			GX2AttribStream *a = &group->attributes[group->numAttributes++];

			a->location = group->vertexShader->attribVars[i].location;
			a->buffer = buffer;
			a->offset = offset;
			a->format = format;
			a->type = GX2_ATTRIB_INDEX_PER_VERTEX;
			a->aluDivisor = 0;
			a->mask = format == GX2_ATTRIB_FORMAT_FLOAT_32_32 ? GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1) :
				GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W);
			a->endianSwap = GX2_ENDIAN_SWAP_DEFAULT;
			return TRUE;
		}
	}
	return FALSE;
}

BOOL WHBGfxInitFetchShader(WHBGfxShaderGroup *group)
{
	uint32_t size = GX2CalcFetchShaderSizeEx(group->numAttributes, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);

	group->fetchShaderProgram = malloc(size);
	GX2InitFetchShaderEx(&group->fetchShader, group->fetchShaderProgram, group->numAttributes, group->attributes, GX2_FETCH_SHADER_TESSELLATION_NONE,
		GX2_TESSELLATION_MODE_DISCRETE);
	return TRUE;
}

BOOL WHBGfxFreeShaderGroup(WHBGfxShaderGroup *group)
{
	(void)group;
	return TRUE;
}

uint32_t GX2GetVertexShaderGPRs(const GX2VertexShader *shader)
{
	(void)shader;
	return 4;
}

uint32_t GX2GetPixelShaderGPRs(const GX2PixelShader *shader)
{
	(void)shader;
	return 2;
}

/* ---------- CafeGLSL */

static unsigned compile_count;
static int compiler_up;

void InitGLSLCompiler(void)
{
	compiler_up = 1;
	heap_free -= 1800u << 10;
}

void DestroyGLSLCompiler(void)
{
	compiler_up = 0;
	heap_free += 1700u << 10;
}

const char *GetGLSLCompilerVersion(void)
{
	return "v0.3.0";
}

static unsigned source_index(const char *text)
{
	unsigned i;

	for (i = 0; i < GLSL_SOURCE_COUNT; i++)
		if (glsl_sources[i].text == text)
			return i;
	return GLSL_SOURCE_COUNT;
}

GX2ShaderMode CompileShaderPair(const char *vertexSource, const char *pixelSource, GLSLCompileMode requestedMode, GX2VertexShader **vertexOut,
	GX2PixelShader **pixelOut, char *infoLogOut, int infoLogMaxLength, GLSL_COMPILER_FLAG flags)
{
	static const char *const gen_names[2] = { "v0_in", "v9_in" };
	static const unsigned gen_locations[2] = { 0, 9 };
	static const GX2ShaderVarType gen_types[2] = { GX2_SHADER_VAR_TYPE_FLOAT4, GX2_SHADER_VAR_TYPE_FLOAT4 };
	unsigned vi = source_index(vertexSource), pi = source_index(pixelSource);
	int diffuse = vi == GLSL_GEN_VS00 && pi == GLSL_GEN_PS_DIFFUSE;

	(void)requestedMode;
	(void)flags;
	compile_count++;
	*vertexOut = NULL;
	*pixelOut = NULL;
	if (!compiler_up)
		abort();
	if ((mock.fail_pair && compile_count == (unsigned)mock.fail_pair) || (mock.fail_es_dialect && !strncmp(vertexSource, "#version 300 es", 15)))
	{
		snprintf(infoLogOut, (size_t)infoLogMaxLength, "0:1(10): error: mock refuses this shader\n");
		return GLSL_SHADER_MODE_ERROR;
	}
	infoLogOut[0] = 0;
	*vertexOut = make_vertex_shader(gen_names, gen_locations, gen_types, 2, 1, generated_vs_ids, 3, 568, (uint8_t)(diffuse && !mock.different_programs ? 0x41 : 0x61));
	*pixelOut = make_pixel_shader(!diffuse, diffuse ? generated_ps_inputs : text_ps_inputs, diffuse ? 1 : 2, 48, (uint8_t)(diffuse && !mock.different_programs ? 0x42 : 0x62));
	tag_set(*pixelOut, diffuse ? KIND_GENERATED : KIND_OTHER);
	return GX2_SHADER_MODE_UNIFORM_REGISTER;
}

void FreeShaders(GX2VertexShader *vertex, GX2PixelShader *pixel)
{
	(void)vertex;
	(void)pixel;
}

/* ---------- the rasterizer */

static GX2ColorBuffer *scene_target(void)
{
	return bound_color && bound_color->surface.width == SCENE_SIZE && bound_color->surface.height == SCENE_SIZE ? bound_color : NULL;
}

static void put_pixel(GX2ColorBuffer *cb, unsigned x, unsigned y, const uint8_t rgba[4])
{
	uint8_t *p = (uint8_t *)cb->surface.image + ((size_t)y * cb->surface.pitch + x) * 4;
	unsigned i;

	if (mock.target_unreadable)
		return;
	for (i = 0; i < 4; i++)
		p[mock.target_order.source[i]] = rgba[i];
}

void GX2ClearColor(GX2ColorBuffer *colorBuffer, float red, float green, float blue, float alpha)
{
	unsigned x, y;
	uint8_t rgba[4];
	float c[4];
	unsigned i;

	c[0] = red; c[1] = green; c[2] = blue; c[3] = alpha;
	for (i = 0; i < 4; i++)
		rgba[i] = (uint8_t)(c[i] * 255.0f + 0.5f);
	if (!colorBuffer->surface.image || colorBuffer->surface.width != SCENE_SIZE)
		return;
	for (y = 0; y < colorBuffer->surface.height; y++)
		for (x = 0; x < colorBuffer->surface.width; x++)
			put_pixel(colorBuffer, x, y, rgba);
}

typedef struct vertex
{
	float position[4];
	float uv[4];
	float color[4];
} vertex;

static float edge(float ax, float ay, float bx, float by, float px, float py)
{
	return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

static void sample_texture(const GX2Texture *t, float u, float v, uint8_t rgba[4])
{
	const GX2Surface *s = &t->surface;
	int x = (int)(u * (float)s->width), y = (int)(v * (float)s->height);
	int compressed;
	unsigned unit = format_unit_bytes(s->format, &compressed);

	x = x < 0 ? 0 : x >= (int)s->width ? (int)s->width - 1 : x;
	y = y < 0 ? 0 : y >= (int)s->height ? (int)s->height - 1 : y;
	if (mock.rows_flipped)
		y = (int)s->height - 1 - y;
	if (!compressed)
	{
		const uint8_t *p = (const uint8_t *)s->image + ((size_t)y * s->pitch + (unsigned)x) * unit;

		memcpy(rgba, p, 4);
	}
	else
	{
		unsigned row_blocks = mock.pitch_in_blocks ? s->pitch : s->pitch / 4;
		const uint8_t *block = (const uint8_t *)s->image + ((size_t)(y / 4) * row_blocks + (unsigned)(x / 4)) * unit;
		bc_format format = s->format == GX2_SURFACE_FORMAT_UNORM_BC3 ? BC_FORMAT_BC3 : s->format == GX2_SURFACE_FORMAT_UNORM_BC2 ? BC_FORMAT_BC2 : BC_FORMAT_BC1;
		const bc_permutation *gpu = format == BC_FORMAT_BC3 ? &mock.bc3_gpu : format == BC_FORMAT_BC2 ? &mock.bc2_gpu : &mock.bc1_gpu;
		uint8_t logical[16], texels[16][4];
		unsigned i;

		for (i = 0; i < unit; i++)
			logical[i] = block[gpu->source[i]];
		bc_decode_block(format, logical, texels);
		memcpy(rgba, texels[(y & 3) * 4 + (x & 3)], 4);
	}
}

/* a textured, tinted triangle: the ui pipeline */
static void raster_textured(GX2ColorBuffer *cb, const vertex v[3])
{
	unsigned x, y;
	float area = edge(v[0].position[0], v[0].position[1], v[1].position[0], v[1].position[1], v[2].position[0], v[2].position[1]);

	if (area == 0.0f)
		return;
	for (y = 0; y < SCENE_SIZE; y++)
	{
		for (x = 0; x < SCENE_SIZE; x++)
		{
			float px = ((float)x + 0.5f) / (float)SCENE_SIZE * 2.0f - 1.0f, py = 1.0f - ((float)y + 0.5f) / (float)SCENE_SIZE * 2.0f;
			float w0 = edge(v[1].position[0], v[1].position[1], v[2].position[0], v[2].position[1], px, py) / area;
			float w1 = edge(v[2].position[0], v[2].position[1], v[0].position[0], v[0].position[1], px, py) / area;
			float w2 = 1.0f - w0 - w1;
			float u, vv, tint[4];
			uint8_t texel[4], out[4];
			unsigned i;

			if (w0 < -1e-6f || w1 < -1e-6f || w2 < -1e-6f)
				continue;
			u = w0 * v[0].uv[0] + w1 * v[1].uv[0] + w2 * v[2].uv[0];
			vv = w0 * v[0].uv[1] + w1 * v[1].uv[1] + w2 * v[2].uv[1];
			for (i = 0; i < 4; i++)
				tint[i] = w0 * v[0].color[i] + w1 * v[1].color[i] + w2 * v[2].color[i];
			sample_texture(bound_texture, u, vv, texel);
			for (i = 0; i < 4; i++)
				out[i] = (uint8_t)((float)texel[i] * tint[i] + 0.5f);
			put_pixel(cb, x, y, out);
		}
	}
}

/* a flat triangle given in NDC */
static void raster_flat(GX2ColorBuffer *cb, const float x[3], const float y[3], const uint8_t rgba[4])
{
	scene_point t[3];
	unsigned px, py, i;

	for (i = 0; i < 3; i++)
	{
		t[i].x = x[i];
		t[i].y = y[i];
	}
	for (py = 0; py < SCENE_SIZE; py++)
		for (px = 0; px < SCENE_SIZE; px++)
			if (scene_inside(t, px, py))
				put_pixel(cb, px, py, rgba);
}

/* the generator's vs00 as the compiled GLSL computes it, from the uniforms the program set */
static int generated_vertex(const float v0[4], float *x, float *y)
{
	const float *c = registers;
	float clip[4], p[3], scale[3], offset[3];
	unsigned i;

	clip[0] = v0[0] * c[0] + v0[1] * c[1] + v0[2] * c[2] + c[3];
	clip[1] = v0[0] * c[4] + v0[1] * c[5] + v0[2] * c[6] + c[7];
	clip[2] = v0[0] * c[8] + v0[1] * c[9] + v0[2] * c[10] + c[11];
	clip[3] = v0[0] * c[12] + v0[1] * c[13] + v0[2] * c[14] + c[15];
	for (i = 0; i < 3; i++)
	{
		scale[i] = registers[240 + i] != 0.0f ? registers[240 + i] : 1.0f;
		offset[i] = registers[244 + i];
	}
	{
		const float *c58 = registers + 58 * 4, *c59 = registers + 59 * 4;
		float shift[3];

		shift[0] = c59[0] + 0.5f + registers[252] - offset[0];
		shift[1] = c59[1] + 0.5f - offset[1];
		shift[2] = c59[2] - offset[2];
		for (i = 0; i < 3; i++)
			p[i] = (clip[i] * c58[i] + shift[i] * clip[3]) / scale[i];
	}
	if (!(clip[3] > 0.0f || clip[3] < 0.0f))
		return 0;
	*x = p[0] / clip[3];
	*y = -p[1] / clip[3]; /* gl_Position.y = -gl_Position.y */
	return 1;
}

static void draw_triangles(unsigned count, const unsigned *indices, unsigned first)
{
	GX2ColorBuffer *cb = scene_target();
	shader_kind kind = tag_get(bound_ps);
	unsigned t;

	mock_draws++;
	if (!cb || kind == KIND_NONE || kind == KIND_OTHER)
		return;
	mock_scene_draws++;
	for (t = 0; t + 2 < count; t += 3)
	{
		unsigned v[3], k;

		for (k = 0; k < 3; k++)
			v[k] = indices ? indices[t + k] : first + t + k;
		if (kind == KIND_ATTR || kind == KIND_PACKED)
		{
			const GX2AttribStream *position = attrib_at(0), *value = attrib_at(1);
			float x[3], y[3], p[4], val[4];
			uint8_t rgba[4];

			if (!position || !value)
				continue;
			for (k = 0; k < 3; k++)
			{
				fetch_attrib(position, v[k], p);
				x[k] = p[0];
				y[k] = p[1];
			}
			fetch_attrib(value, v[0], val);
			vtx_to_pixel(val, rgba);
			raster_flat(cb, x, y, rgba);
		}
		else if (kind == KIND_UI)
		{
			const GX2AttribStream *position = attrib_at(0), *uv = attrib_at(1), *color = attrib_at(2);
			vertex verts[3];

			if (!position || !uv || !color || !bound_texture)
				continue;
			for (k = 0; k < 3; k++)
			{
				fetch_attrib(position, v[k], verts[k].position);
				fetch_attrib(uv, v[k], verts[k].uv);
				fetch_attrib(color, v[k], verts[k].color);
			}
			raster_textured(cb, verts);
		}
		else if (kind == KIND_GENERATED)
		{
			const GX2AttribStream *position = attrib_at(0), *color = attrib_at(9);
			float x[3], y[3], p[4], c[4];
			uint8_t rgba[4];
			int ok = 1;

			if (!position || !color)
				continue;
			for (k = 0; k < 3; k++)
			{
				fetch_attrib(position, v[k], p);
				ok = ok && generated_vertex(p, &x[k], &y[k]);
			}
			fetch_attrib(color, v[0], c);
			if (!ok)
				continue;
			for (k = 0; k < 4; k++)
				rgba[k] = (uint8_t)((c[k] < 0 ? 0 : c[k] > 1 ? 1 : c[k]) * 255.0f + 0.5f);
			raster_flat(cb, x, y, rgba);
		}
	}
}

void GX2DrawEx(GX2PrimitiveMode mode, uint32_t count, uint32_t offset, uint32_t numInstances)
{
	(void)numInstances;
	if (mode == GX2_PRIMITIVE_MODE_TRIANGLES)
		draw_triangles(count, NULL, offset);
}

void GX2DrawIndexedEx(GX2PrimitiveMode mode, uint32_t count, GX2IndexType indexType, const void *indices, uint32_t offset, uint32_t numInstances)
{
	idx_type type = indexType == GX2_INDEX_TYPE_U16_LE ? IDX_U16_LE : indexType == GX2_INDEX_TYPE_U32_LE ? IDX_U32_LE : indexType == GX2_INDEX_TYPE_U16 ? IDX_U16 : IDX_U32;
	unsigned read[IDX_COUNT];

	(void)offset;
	(void)numInstances;
	if (mock.le_indices_broken)
		type = type == IDX_U16_LE ? IDX_U16 : type == IDX_U32_LE ? IDX_U32 : type;
	if (mode != GX2_PRIMITIVE_MODE_TRIANGLES || count != IDX_COUNT)
		return;
	idx_read(type, indices, read);
	draw_triangles(count, read, 0);
}

/* ---------- the system */

GX2TVScanMode GX2GetSystemTVScanMode(void)
{
	return GX2_TV_SCAN_MODE_720P;
}

GX2AspectRatio GX2GetSystemTVAspectRatio(void)
{
	return GX2_ASPECT_RATIO_16_9;
}

GX2DrcRenderMode GX2GetSystemDRCMode(void)
{
	return GX2_DRC_RENDER_MODE_SINGLE;
}

int32_t GX2GetMainCoreId(void)
{
	return 1;
}

static GX2ColorBuffer tv_color, drc_color;
static GX2DepthBuffer tv_depth, drc_depth;
static GX2ContextState tv_context, drc_context;

static void fake_buffers(void)
{
	tv_color.surface.width = 1280;
	tv_color.surface.height = 720;
	tv_color.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
	drc_color.surface.width = 854;
	drc_color.surface.height = 480;
	drc_color.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
}

GX2ColorBuffer *WHBGfxGetTVColourBuffer(void) { fake_buffers(); return &tv_color; }
GX2ColorBuffer *WHBGfxGetDRCColourBuffer(void) { fake_buffers(); return &drc_color; }
GX2DepthBuffer *WHBGfxGetTVDepthBuffer(void) { return &tv_depth; }
GX2DepthBuffer *WHBGfxGetDRCDepthBuffer(void) { return &drc_depth; }
GX2ContextState *WHBGfxGetTVContextState(void) { return &tv_context; }
GX2ContextState *WHBGfxGetDRCContextState(void) { return &drc_context; }
void WHBGfxBeginRender(void) {}
void WHBGfxFinishRender(void) {}
void WHBGfxFinishRenderTV(void) {}
void WHBGfxFinishRenderDRC(void) {}
void VPADInit(void) {}

/* threads run to the end when they are resumed */

static OSThread main_thread, *created;
static OSThreadEntryPointFn created_entry;

OSThread *OSGetCurrentThread(void)
{
	static char stack[4096];

	main_thread.stackStart = stack + sizeof(stack);
	main_thread.stackEnd = stack;
	return &main_thread;
}

int32_t OSGetThreadPriority(OSThread *thread)
{
	(void)thread;
	return 16;
}

BOOL OSCreateThread(OSThread *thread, OSThreadEntryPointFn entry, int32_t argc, char *argv, void *stack, uint32_t stackSize, int32_t priority,
	OSThreadAttributes attributes)
{
	(void)argc; (void)argv; (void)stack; (void)stackSize; (void)priority; (void)attributes;
	created = thread;
	created_entry = entry;
	return TRUE;
}

void OSSetThreadName(OSThread *thread, const char *name)
{
	(void)thread;
	(void)name;
}

int32_t OSResumeThread(OSThread *thread)
{
	(void)thread;
	created_entry(0, NULL);
	return 0;
}

BOOL OSJoinThread(OSThread *thread, int *threadResult)
{
	(void)thread;
	if (threadResult)
		*threadResult = 0;
	return TRUE;
}
