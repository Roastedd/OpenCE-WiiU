#include "gx.h"

#include <coreinit/cache.h>
#include <coreinit/memdefaultheap.h>
#include <gx2/clear.h>
#include <gx2/context.h>
#include <gx2/event.h>
#include <gx2/registers.h>
#include <gx2/state.h>
#include <gx2/utils.h>
#include <stdio.h>
#include <string.h>

void *gx_alloc(uint32_t size, uint32_t alignment)
{
	void *memory = MEMAllocFromDefaultHeapEx(size, (int32_t)alignment);

	if (memory)
		memset(memory, 0, size);
	return memory;
}

void gx_free(void *memory)
{
	if (memory)
		MEMFreeToDefaultHeap(memory);
}

/* ---------- textures */

int gx_texture_create(gx_texture *t, unsigned width, unsigned height, GX2SurfaceFormat format, unsigned bytes_per_unit, int compressed,
	char *error, size_t error_size)
{
	GX2Surface *s = &t->texture.surface;
	unsigned pitch;

	memset(t, 0, sizeof(*t));
	t->width = width;
	t->height = height;
	s->dim = GX2_SURFACE_DIM_TEXTURE_2D;
	s->width = width;
	s->height = height;
	s->depth = 1;
	s->mipLevels = 1;
	s->format = format;
	s->aa = GX2_AA_MODE1X;
	s->use = GX2_SURFACE_USE_TEXTURE;
	s->tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
	s->swizzle = 0;
	t->texture.viewFirstMip = 0;
	t->texture.viewNumMips = 1;
	t->texture.viewFirstSlice = 0;
	t->texture.viewNumSlices = 1;
	t->texture.compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
	GX2CalcSurfaceSizeAndAlignment(s);
	pitch = s->pitch;
	t->rows = compressed ? height / 4 : height;
	if (compressed)
	{
		/* the pitch of a compressed surface is counted in texels (the first run on a console: rows of blocks 512 bytes apart, the size
		of the padded image, drew block 0 right and the rest black; the GPU reads the rows (pitch / 4) blocks apart). The image is
		larger than that, padded. */
		unsigned a = (pitch / 4) * bytes_per_unit;

		if (a * t->rows > s->imageSize)
		{
			snprintf(error, error_size, "compressed surface %ux%u: pitch %u, image size %u too small for %u rows of %u bytes", width, height, pitch,
				(unsigned)s->imageSize, t->rows, a);
			return 0;
		}
		t->row_bytes = a;
	}
	else
	{
		t->row_bytes = pitch * bytes_per_unit;
		if (t->row_bytes * t->rows > s->imageSize)
		{
			snprintf(error, error_size, "surface %ux%u: pitch %u, image size %u too small for %u rows", width, height, pitch, (unsigned)s->imageSize, t->rows);
			return 0;
		}
	}
	s->image = gx_alloc(s->imageSize, s->alignment ? s->alignment : 256);
	if (!s->image)
	{
		snprintf(error, error_size, "no memory for a %u byte surface", (unsigned)s->imageSize);
		return 0;
	}
	GX2InitTextureRegs(&t->texture);
	t->ok = 1;
	return 1;
}

void gx_texture_upload(gx_texture *t, const uint8_t *data, size_t src_row_bytes)
{
	GX2Surface *s = &t->texture.surface;
	size_t copy = src_row_bytes < t->row_bytes ? src_row_bytes : t->row_bytes;
	unsigned row;

	for (row = 0; row < t->rows; row++)
		memcpy((uint8_t *)s->image + (size_t)row * t->row_bytes, data + (size_t)row * src_row_bytes, copy);
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, s->image, s->imageSize);
}

void gx_texture_free(gx_texture *t)
{
	gx_free(t->texture.surface.image);
	memset(t, 0, sizeof(*t));
}

/* ---------- targets */

int gx_target_create(gx_target *t, unsigned width, unsigned height, char *error, size_t error_size)
{
	GX2Surface *s = &t->color.surface;

	memset(t, 0, sizeof(*t));
	t->width = width;
	t->height = height;
	s->dim = GX2_SURFACE_DIM_TEXTURE_2D;
	s->width = width;
	s->height = height;
	s->depth = 1;
	s->mipLevels = 1;
	s->format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
	s->aa = GX2_AA_MODE1X;
	s->use = (GX2SurfaceUse)(GX2_SURFACE_USE_COLOR_BUFFER | GX2_SURFACE_USE_TEXTURE);
	s->tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
	s->swizzle = 0;
	GX2CalcSurfaceSizeAndAlignment(s);
	t->pitch_bytes = s->pitch * 4;
	if (t->pitch_bytes * height > s->imageSize)
	{
		snprintf(error, error_size, "target %ux%u: pitch %u, image size %u too small", width, height, (unsigned)s->pitch, (unsigned)s->imageSize);
		return 0;
	}
	s->image = gx_alloc(s->imageSize, s->alignment ? s->alignment : 256);
	if (!s->image)
	{
		snprintf(error, error_size, "no memory for a %u byte target", (unsigned)s->imageSize);
		return 0;
	}
	t->color.viewMip = 0;
	t->color.viewFirstSlice = 0;
	t->color.viewNumSlices = 1;
	GX2InitColorBufferRegs(&t->color);
	t->ok = 1;
	return 1;
}

void gx_view(float x, float y, float width, float height)
{
	GX2SetViewport(x, y, width, height, 0.0f, 1.0f);
	GX2SetScissor((uint32_t)x, (uint32_t)y, (uint32_t)width, (uint32_t)height);
}

void gx_state(int alpha_blend)
{
	GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS);
	GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);
	GX2SetColorControl(GX2_LOGIC_OP_COPY, alpha_blend ? 1 : 0, FALSE, TRUE);
	GX2SetBlendControl(GX2_RENDER_TARGET_0, GX2_BLEND_MODE_SRC_ALPHA, GX2_BLEND_MODE_INV_SRC_ALPHA, GX2_BLEND_COMBINE_MODE_ADD, TRUE,
		GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_INV_SRC_ALPHA, GX2_BLEND_COMBINE_MODE_ADD);
	GX2SetTargetChannelMasks(GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA,
		GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA);
}

void gx_target_begin(gx_target *t)
{
	GX2SetContextState(WHBGfxGetTVContextState());
	GX2SetColorBuffer(&t->color, GX2_RENDER_TARGET_0);
	gx_view(0.0f, 0.0f, (float)t->width, (float)t->height);
	gx_state(0);
}

void gx_target_clear(gx_target *t, const uint8_t rgba[4])
{
	GX2ClearColor(&t->color, (float)rgba[0] / 255.0f, (float)rgba[1] / 255.0f, (float)rgba[2] / 255.0f, (float)rgba[3] / 255.0f);
	/* (the clear is a draw of its own: put the state this target's draws need back) */
	GX2SetContextState(WHBGfxGetTVContextState());
	GX2SetColorBuffer(&t->color, GX2_RENDER_TARGET_0);
	gx_view(0.0f, 0.0f, (float)t->width, (float)t->height);
	gx_state(0);
}

void gx_target_finish(gx_target *t)
{
	GX2Flush();
	GX2DrawDone();
	DCInvalidateRange(t->color.surface.image, t->color.surface.imageSize);
}

const uint8_t *gx_target_pixels(const gx_target *t)
{
	return t->color.surface.image;
}

/* ---------- shaders */

int gx_pipeline_load(gx_pipeline *p, const gsh_entry *file)
{
	memset(p, 0, sizeof(*p));
	p->loaded = WHBGfxLoadGFDShaderGroup(&p->group, 0, file->data) ? 1 : 0;
	return p->loaded;
}

int gx_fetch_create(gx_fetch *f, const GX2VertexShader *vs, const gx_stream *streams, unsigned count, char *error, size_t error_size)
{
	GX2AttribStream attribs[16];
	uint32_t size;
	unsigned i, j;

	memset(f, 0, sizeof(*f));
	if (count > 16)
	{
		snprintf(error, error_size, "%u streams (16 at most)", count);
		return 0;
	}
	for (i = 0; i < count; i++)
	{
		int location = -1;

		for (j = 0; j < vs->attribVarCount; j++)
		{
			if (!strcmp(vs->attribVars[j].name, streams[i].name))
			{
				location = (int)vs->attribVars[j].location;
				break;
			}
		}
		if (location < 0)
		{
			snprintf(error, error_size, "the vertex shader has no input %s", streams[i].name);
			return 0;
		}
		attribs[i].location = (uint32_t)location;
		attribs[i].buffer = streams[i].buffer;
		attribs[i].offset = streams[i].offset;
		attribs[i].format = streams[i].format;
		attribs[i].type = GX2_ATTRIB_INDEX_PER_VERTEX;
		attribs[i].aluDivisor = 0;
		attribs[i].mask = streams[i].mask;
		attribs[i].endianSwap = streams[i].swap;
	}
	size = GX2CalcFetchShaderSizeEx(count, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
	f->program = gx_alloc(size, GX2_SHADER_PROGRAM_ALIGNMENT);
	if (!f->program)
	{
		snprintf(error, error_size, "no memory for a %u byte fetch shader", (unsigned)size);
		return 0;
	}
	GX2InitFetchShaderEx(&f->shader, f->program, count, attribs, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, f->program, size);
	f->ok = 1;
	return 1;
}

void gx_fetch_free(gx_fetch *f)
{
	gx_free(f->program);
	memset(f, 0, sizeof(*f));
}

void gx_bind(const GX2VertexShader *vs, const GX2PixelShader *ps, const GX2FetchShader *fetch, GX2ShaderMode mode)
{
	GX2SetFetchShader(fetch);
	GX2SetVertexShader(vs);
	GX2SetPixelShader(ps);
	GX2SetShaderMode(mode);
}

void *gx_buffer(const void *data, size_t size, uint32_t alignment)
{
	void *memory = gx_alloc((uint32_t)size, alignment);

	if (!memory)
		return NULL;
	memcpy(memory, data, size);
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, memory, (uint32_t)size);
	return memory;
}

void gx_sampler_point(GX2Sampler *sampler)
{
	GX2InitSampler(sampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_POINT);
}
