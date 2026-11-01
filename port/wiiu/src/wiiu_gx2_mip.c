/* The fallback mip filter stays on the GPU: an exact source-level copy, a
2 x 2 filter into a separate colour image, then a copy into the destination
level. Keeping the filter's input at level zero avoids relying on the
texture view's interpretation of an explicit fetch LOD. The two scratch
images are reused in command order, and replaced storage is retired by the
renderer rather than freed while the GPU can still read it. */

#include "wiiu_gx2_mip.h"

#include <coreinit/memdefaultheap.h>
#include <gx2/context.h>
#include <gx2/draw.h>
#include <gx2/enum.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/sampler.h>
#include <gx2/shaders.h>
#include <gx2/state.h>
#include <gx2/texture.h>
#include <gx2/utils.h>
#include <whb/gfx.h>

#include <stdint.h>
#include <string.h>

static const unsigned char mip_gsh[] __attribute__((aligned(64))) =
{
#include "wiiu_gx2_mip_gsh.inc"
};

struct mip_vertex
{
	float x, y, u, v;
};

/* Never rewritten: queued copies and draws can retain these addresses. */
static const struct mip_vertex mip_quad[6] __attribute__((aligned(64))) =
{
	{ -1.0f,  1.0f, 0.0f, 0.0f },
	{  1.0f,  1.0f, 1.0f, 0.0f },
	{ -1.0f, -1.0f, 0.0f, 1.0f },
	{  1.0f,  1.0f, 1.0f, 0.0f },
	{  1.0f, -1.0f, 1.0f, 1.0f },
	{ -1.0f, -1.0f, 0.0f, 1.0f }
};

static GX2ContextState mip_context __attribute__((aligned(256)));
static struct
{
	int ready, failed, context_ready;
	WHBGfxShaderGroup shader;
	GX2Sampler sampler;
	uint32_t sampler_location;
	struct
	{
		void *memory;
		uint32_t bytes, alignment;
	} scratch[2];
} mip;

static uint32_t mip_extent(uint32_t size, unsigned level)
{
	return size >> level ? size >> level : 1;
}

static int mip_initialize(void)
{
	unsigned i;
	int sampler_found = 0;

	if (mip.ready)
		return 1;
	if (mip.failed)
		return 0;
	mip.failed = 1;
	if (!WHBGfxLoadGFDShaderGroup(&mip.shader, 0, mip_gsh) ||
		!WHBGfxInitShaderAttribute(&mip.shader, "a_pos", 0, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32) ||
		!WHBGfxInitShaderAttribute(&mip.shader, "a_uv", 0, 8, GX2_ATTRIB_FORMAT_FLOAT_32_32) ||
		!WHBGfxInitFetchShader(&mip.shader))
		return 0;
	for (i = 0; i < mip.shader.pixelShader->samplerVarCount; i++)
		if (!strcmp(mip.shader.pixelShader->samplerVars[i].name, "tex0"))
		{
			mip.sampler_location = mip.shader.pixelShader->samplerVars[i].location;
			sampler_found = 1;
		}
	if (!sampler_found)
		return 0;
	GX2InitSampler(&mip.sampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_POINT);
	GX2InitSamplerZMFilter(&mip.sampler, GX2_TEX_Z_FILTER_MODE_POINT, GX2_TEX_MIP_FILTER_MODE_NONE);
	GX2InitSamplerLOD(&mip.sampler, 0.0f, 0.0f, 0.0f);
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, (void *)mip_quad, sizeof(mip_quad));
	mip.failed = 0;
	mip.ready = 1;
	return 1;
}

static int scratch_prepare(unsigned slot, uint32_t width, uint32_t height, GX2Surface *surface, void (*retire)(void *))
{
	uint32_t alignment;

	memset(surface, 0, sizeof(*surface));
	surface->dim = GX2_SURFACE_DIM_TEXTURE_2D;
	surface->width = width;
	surface->height = height;
	surface->depth = 1;
	surface->mipLevels = 1;
	surface->format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
	surface->aa = GX2_AA_MODE1X;
	surface->use = (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | (slot ? GX2_SURFACE_USE_COLOR_BUFFER : 0));
	surface->tileMode = GX2_TILE_MODE_DEFAULT;
	GX2CalcSurfaceSizeAndAlignment(surface);
	alignment = surface->alignment ? surface->alignment : 256;
	if (!alignment || (alignment & (alignment - 1)) || !surface->imageSize)
		return 0;
	if (surface->imageSize > mip.scratch[slot].bytes || alignment > mip.scratch[slot].alignment)
	{
		const uint32_t bytes = surface->imageSize > mip.scratch[slot].bytes ? surface->imageSize : mip.scratch[slot].bytes;
		void *memory;

		if (alignment < mip.scratch[slot].alignment)
			alignment = mip.scratch[slot].alignment;
		memory = MEMAllocFromDefaultHeapEx(bytes, (int32_t)alignment);

		if (!memory)
		{
			/* Do not retry a failed allocation on every material draw. The
			caller's existing CPU path remains available for this run. */
			mip.failed = 1;
			mip.ready = 0;
			return 0;
		}
		if (mip.scratch[slot].memory)
			retire(mip.scratch[slot].memory);
		mip.scratch[slot].memory = memory;
		mip.scratch[slot].bytes = bytes;
		mip.scratch[slot].alignment = alignment;
	}
	surface->image = mip.scratch[slot].memory;
	return surface->image != NULL;
}

int wiiu_gx2_mip_derive(const GX2Surface *source, unsigned source_level,
	GX2Surface *destination, unsigned destination_level, void (*retire)(void *))
{
	GX2Surface input;
	GX2Texture texture;
	GX2ColorBuffer output;
	uint32_t width, height, sw, sh;

	if (!source || !destination || !retire || !source->image || !destination->image ||
		!source->width || !source->height || !destination->width || !destination->height ||
		source_level >= source->mipLevels || destination_level >= destination->mipLevels ||
		source_level >= 31 || destination_level >= 31 || source->depth != 1 || destination->depth != 1 ||
		source->dim != GX2_SURFACE_DIM_TEXTURE_2D || destination->dim != GX2_SURFACE_DIM_TEXTURE_2D ||
		source->format != GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8 || destination->format != source->format ||
		source->aa != GX2_AA_MODE1X || destination->aa != GX2_AA_MODE1X ||
		(source_level && !source->mipmaps) || (destination_level && !destination->mipmaps))
		return 0;
	sw = mip_extent(source->width, source_level);
	sh = mip_extent(source->height, source_level);
	width = sw > 1 ? sw / 2 : 1;
	height = sh > 1 ? sh / 2 : 1;
	if (width != mip_extent(destination->width, destination_level) || height != mip_extent(destination->height, destination_level))
		return 0;
	if (!mip_initialize())
		return 0;
	memset(&output, 0, sizeof(output));
	if (!scratch_prepare(0, sw, sh, &input, retire) || !scratch_prepare(1, width, height, &output.surface, retire))
		return 0;
	output.viewNumSlices = 1;
	GX2InitColorBufferRegs(&output);
	memset(&texture, 0, sizeof(texture));
	texture.surface = input;
	texture.viewNumMips = texture.viewNumSlices = 1;
	texture.compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
	GX2InitTextureRegs(&texture);

	/* Everything that can fail is above this point. In particular, neither a
	resize nor shader reflection can fail after changing the GPU context. */
	if (!mip.context_ready)
	{
		GX2SetupContextStateEx(&mip_context, TRUE);
		mip.context_ready = 1;
	}
	GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_COLOR_BUFFER | GX2_INVALIDATE_MODE_TEXTURE), source->image,
		source->imageSize);
	if (source->mipmapSize && source->mipmaps)
		GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_COLOR_BUFFER | GX2_INVALIDATE_MODE_TEXTURE), source->mipmaps, source->mipmapSize);
	GX2CopySurface(source, source_level, 0, &input, 0, 0);
	GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_COLOR_BUFFER | GX2_INVALIDATE_MODE_TEXTURE), input.image, input.imageSize);
	GX2SetContextState(&mip_context);
	GX2SetColorBuffer(&output, GX2_RENDER_TARGET_0);
	GX2SetViewport(0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f);
	GX2SetScissor(0, 0, width, height);
	GX2SetRasterizerClipControlEx(TRUE, TRUE, FALSE);
	GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS);
	GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);
	GX2SetColorControl(GX2_LOGIC_OP_COPY, 0, FALSE, TRUE);
	GX2SetTargetChannelMasks(GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA,
		GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA);
	GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
	GX2SetFetchShader(&mip.shader.fetchShader);
	GX2SetVertexShader(mip.shader.vertexShader);
	GX2SetPixelShader(mip.shader.pixelShader);
	GX2SetAttribBuffer(0, sizeof(mip_quad), sizeof(mip_quad[0]), mip_quad);
	GX2SetPixelTexture(&texture, mip.sampler_location);
	GX2SetPixelSampler(&mip.sampler, mip.sampler_location);
	GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, 6, 0, 1);
	GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_COLOR_BUFFER | GX2_INVALIDATE_MODE_TEXTURE), output.surface.image, output.surface.imageSize);
	GX2CopySurface(&output.surface, 0, 0, destination, destination_level, 0);
	GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_COLOR_BUFFER | GX2_INVALIDATE_MODE_TEXTURE), destination->image,
		destination->imageSize);
	if (destination->mipmapSize && destination->mipmaps)
		GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_COLOR_BUFFER | GX2_INVALIDATE_MODE_TEXTURE), destination->mipmaps, destination->mipmapSize);
	return 1;
}
