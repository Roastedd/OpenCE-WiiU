#include "ui.h"

#include <stdio.h>
#include <string.h>

#define UI_VERTEX_CAPACITY 24576
#define UI_BATCH_CAPACITY 256

typedef struct ui_vertex
{
	float x, y;
	float u, v;
	float r, g, b, a;
} ui_vertex;

typedef struct ui_batch
{
	const GX2Texture *texture;
	int blend;
	unsigned first, count;
} ui_batch;

static gx_pipeline pipeline;
static GX2Sampler sampler;
static gx_texture white;
static ui_vertex *vertices;
static unsigned used;
static unsigned flushed;
static ui_batch batches[UI_BATCH_CAPACITY];
static unsigned batch_count;
static float view_w = 1.0f, view_h = 1.0f;
static int ready;
static uint32_t sampler_location;

int ui_init(char *error, size_t error_size)
{
	static const uint8_t pixel[4] = { 255, 255, 255, 255 };
	unsigned i;

	if (!gx_pipeline_load(&pipeline, &gsh_files[GSH_UI]))
	{
		snprintf(error, error_size, "the system's loader refused the ui shaders");
		return 0;
	}
	if (!WHBGfxInitShaderAttribute(&pipeline.group, "a_pos", 0, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32) ||
		!WHBGfxInitShaderAttribute(&pipeline.group, "a_uv", 0, 8, GX2_ATTRIB_FORMAT_FLOAT_32_32) ||
		!WHBGfxInitShaderAttribute(&pipeline.group, "a_col", 0, 16, GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32))
	{
		snprintf(error, error_size, "the ui vertex shader lacks an input the program expects");
		return 0;
	}
	if (!WHBGfxInitFetchShader(&pipeline.group))
	{
		snprintf(error, error_size, "no fetch shader for the ui streams");
		return 0;
	}
	sampler_location = 0;
	for (i = 0; i < pipeline.group.pixelShader->samplerVarCount; i++)
		if (!strcmp(pipeline.group.pixelShader->samplerVars[i].name, "tex0"))
			sampler_location = pipeline.group.pixelShader->samplerVars[i].location;
	gx_sampler_point(&sampler);
	if (!gx_texture_create(&white, 64, 1, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, 4, 0, error, error_size))
		return 0;
	{
		uint8_t row[64 * 4];

		for (i = 0; i < 64; i++)
			memcpy(row + i * 4, pixel, 4);
		gx_texture_upload(&white, row, sizeof(row));
	}
	vertices = gx_alloc(UI_VERTEX_CAPACITY * sizeof(ui_vertex), GX2_VERTEX_BUFFER_ALIGNMENT);
	if (!vertices)
	{
		snprintf(error, error_size, "no memory for the ui's vertices");
		return 0;
	}
	ready = 1;
	return 1;
}

void ui_shutdown(void)
{
	ready = 0;
	gx_free(vertices);
	vertices = NULL;
	gx_texture_free(&white);
	if (pipeline.loaded)
		WHBGfxFreeShaderGroup(&pipeline.group);
	pipeline.loaded = 0;
}

int ui_ready(void)
{
	return ready;
}

const GX2VertexShader *ui_vertex_shader(void)
{
	return pipeline.group.vertexShader;
}

const GX2PixelShader *ui_pixel_shader(void)
{
	return pipeline.group.pixelShader;
}

void ui_frame(void)
{
	GX2DrawDone();
	used = 0;
	flushed = 0;
	batch_count = 0;
}

void ui_begin(float width, float height)
{
	view_w = width;
	view_h = height;
}

void ui_flush(void)
{
	unsigned i;

	if (!ready || !batch_count)
		return;
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, vertices + flushed, (used - flushed) * (uint32_t)sizeof(ui_vertex));
	gx_bind(pipeline.group.vertexShader, pipeline.group.pixelShader, &pipeline.group.fetchShader, GX2_SHADER_MODE_UNIFORM_REGISTER);
	GX2SetAttribBuffer(0, UI_VERTEX_CAPACITY * (uint32_t)sizeof(ui_vertex), (uint32_t)sizeof(ui_vertex), vertices);
	for (i = 0; i < batch_count; i++)
	{
		gx_state(batches[i].blend);
		GX2SetPixelTexture(batches[i].texture, sampler_location);
		GX2SetPixelSampler(&sampler, sampler_location);
		GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, batches[i].count, batches[i].first, 1);
	}
	flushed = used;
	batch_count = 0;
}

void ui_quad(float x, float y, float w, float h, const GX2Texture *texture, float u0, float v0, float u1, float v1, const float tint[4], int blend)
{
	float x0, y0, x1, y1;
	ui_vertex *v;
	ui_batch *batch;
	unsigned i;

	if (!ready)
		return;
	if (!texture)
		texture = &white.texture;
	if (used + 6 > UI_VERTEX_CAPACITY || batch_count >= UI_BATCH_CAPACITY)
	{
		/* the frame has more than the buffer holds: draw what there is, wait for it, start over */
		ui_flush();
		GX2DrawDone();
		used = 0;
		flushed = 0;
	}
	x0 = x / view_w * 2.0f - 1.0f;
	x1 = (x + w) / view_w * 2.0f - 1.0f;
	y0 = 1.0f - y / view_h * 2.0f;
	y1 = 1.0f - (y + h) / view_h * 2.0f;
	v = vertices + used;
	{
		static const unsigned corner_x[6] = { 0, 1, 0, 1, 1, 0 };
		static const unsigned corner_y[6] = { 0, 0, 1, 0, 1, 1 };

		for (i = 0; i < 6; i++)
		{
			v[i].x = corner_x[i] ? x1 : x0;
			v[i].y = corner_y[i] ? y1 : y0;
			v[i].u = corner_x[i] ? u1 : u0;
			v[i].v = corner_y[i] ? v1 : v0;
			v[i].r = tint[0];
			v[i].g = tint[1];
			v[i].b = tint[2];
			v[i].a = tint[3];
		}
	}
	batch = batch_count ? &batches[batch_count - 1] : NULL;
	if (batch && batch->texture == texture && batch->blend == blend && batch->first + batch->count == used)
	{
		batch->count += 6;
	}
	else
	{
		batch = &batches[batch_count++];
		batch->texture = texture;
		batch->blend = blend;
		batch->first = used;
		batch->count = 6;
	}
	used += 6;
}

void ui_rect(float x, float y, float w, float h, const float color[4])
{
	ui_quad(x, y, w, h, NULL, 0.0f, 0.0f, 1.0f, 1.0f, color, color[3] < 1.0f);
}
