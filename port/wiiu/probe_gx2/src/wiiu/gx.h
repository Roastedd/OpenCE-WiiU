/*
GX.H

The probe's thin layer over GX2: aligned memory, linear textures and render targets the CPU can read back, fetch shaders
built from a list of streams (the part the tests vary), and the state every draw sets for itself. Nothing here is
cleverer than wut's own samples; it is split out so that the stages read as what they test.

Every draw sets all the state it depends on (viewport, scissor, blending, depth, culling, shader mode) instead of trusting
what the last one left: the targets of the pages and the targets of the readback share one context state.
*/

#ifndef HALO_WIIU_GX2_GX_H
#define HALO_WIIU_GX2_GX_H

#include "../readback.h"
#include "../shader_data.h"

#include <gx2/draw.h>
#include <gx2/enum.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/sampler.h>
#include <gx2/shaders.h>
#include <gx2/state.h>
#include <gx2/surface.h>
#include <gx2/texture.h>
#include <gx2/utils.h>
#include <stddef.h>
#include <stdint.h>
#include <whb/gfx.h>

/* memory the GPU reads or writes: zero-filled, aligned as asked; NULL when there is none */
void *gx_alloc(uint32_t size, uint32_t alignment);
void gx_free(void *memory);

/* ---------- textures: linear, one level */

typedef struct gx_texture
{
	GX2Texture texture;
	unsigned width, height;
	unsigned row_bytes; /* bytes from one row of texels (of blocks, for compressed formats) to the next */
	unsigned rows;      /* rows the data has: height, or height / 4 for the compressed formats */
	int ok;
} gx_texture;

/* bytes_per_unit: bytes a texel (a block, if compressed) takes. Returns 0 with the reason in error. */
int gx_texture_create(gx_texture *t, unsigned width, unsigned height, GX2SurfaceFormat format, unsigned bytes_per_unit, int compressed,
	char *error, size_t error_size);
/* data holds rows of src_row_bytes; as many rows as the texture has. Makes the GPU see them. */
void gx_texture_upload(gx_texture *t, const uint8_t *data, size_t src_row_bytes);
void gx_texture_free(gx_texture *t);

/* ---------- targets the CPU can read: linear RGBA8 color buffers */

typedef struct gx_target
{
	GX2ColorBuffer color;
	unsigned width, height;
	unsigned pitch_bytes;
	int ok;
} gx_target;

int gx_target_create(gx_target *t, unsigned width, unsigned height, char *error, size_t error_size);
/* binds the target for drawing (the TV context state, this color buffer, the whole of it as viewport) and sets the default state */
void gx_target_begin(gx_target *t);
void gx_target_clear(gx_target *t, const uint8_t rgba[4]);
/* waits for the GPU and drops the CPU's stale copy of what it drew */
void gx_target_finish(gx_target *t);
const uint8_t *gx_target_pixels(const gx_target *t);

/* ---------- state */

/* the viewport and scissor, in pixels of the bound target (top left origin) */
void gx_view(float x, float y, float width, float height);
/* opaque or alpha-blended draws; no depth test, no culling */
void gx_state(int alpha_blend);

/* ---------- shaders */

typedef struct gx_pipeline
{
	WHBGfxShaderGroup group; /* the vertex and pixel shader from the GSH file; the fetch shader of the UI's streams */
	int loaded;
} gx_pipeline;

int gx_pipeline_load(gx_pipeline *p, const gsh_entry *file);

/* One attribute of a vertex stream: the shader input's name (looked up in the vertex shader for its location), the
buffer index it is read from (GX2SetAttribBuffer) and the offset in a vertex, its format, the component selection and
the byte swap the fetch applies. */
typedef struct gx_stream
{
	const char *name;
	uint32_t buffer;
	uint32_t offset;
	GX2AttribFormat format;
	uint32_t mask;
	GX2EndianSwapMode swap;
} gx_stream;

typedef struct gx_fetch
{
	GX2FetchShader shader;
	void *program;
	int ok;
} gx_fetch;

#define GX_MASK_XYZW GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W)
#define GX_MASK_XY01 GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1)
#define GX_MASK_X001 GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_0, GX2_SQ_SEL_0, GX2_SQ_SEL_1)

int gx_fetch_create(gx_fetch *f, const GX2VertexShader *vs, const gx_stream *streams, unsigned count, char *error, size_t error_size);
void gx_fetch_free(gx_fetch *f);

/* makes these the shaders of the following draws (and the uniform mode they were compiled for) */
void gx_bind(const GX2VertexShader *vs, const GX2PixelShader *ps, const GX2FetchShader *fetch, GX2ShaderMode mode);

/* a vertex or index buffer the GPU can read: a copy of data, flushed; NULL when out of memory. Free with gx_free. */
void *gx_buffer(const void *data, size_t size, uint32_t alignment);

void gx_sampler_point(GX2Sampler *sampler);

#endif
