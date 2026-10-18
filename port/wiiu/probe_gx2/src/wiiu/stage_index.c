/*
The index stage: GX2_INDEX_TYPE_U16_LE, U32_LE, U16 and U32 each given index data in each byte order (the 32-bit
types only with the order they read: a mismatch would index 150 million vertices in a buffer of 4096). What is drawn
says which types read the indices they were given: four triangles, one to a corner of the target, picked by nine indices
that leave the top right one out. See index_test.h.
*/

#include "../index_test.h"
#include "../vertex_test.h"
#include "stages.h"

#include <stdio.h>
#include <string.h>

#define VERTEX_STRIDE 24u
#define VERTEX_COUNT 4096u

typedef struct index_case
{
	idx_type type;
	int big_endian;      /* the byte order of the data */
	int drawn;           /* the case was drawn (not skipped as unsafe) */
	void *indices;
	unsigned observed;   /* corners found drawn */
	unsigned predicted;  /* corners the model says the type reads from this data */
	unsigned other;      /* corners whose pixel is neither the corner's color nor the clear color */
} index_case;

static index_case cases[IDX_TYPE_COUNT * 2];
static gx_fetch fetch;
static void *vertex_buffer;

static const GX2IndexType gx_types[IDX_TYPE_COUNT] = { GX2_INDEX_TYPE_U16_LE, GX2_INDEX_TYPE_U32_LE, GX2_INDEX_TYPE_U16, GX2_INDEX_TYPE_U32 };

static void draw_case(const index_case *c)
{
	gx_session *s = session_current();

	gx_state(0);
	gx_bind(s->attr.group.vertexShader, s->attr.group.pixelShader, &fetch.shader, GX2_SHADER_MODE_UNIFORM_REGISTER);
	GX2SetAttribBuffer(0, VERTEX_COUNT * VERTEX_STRIDE, VERTEX_STRIDE, vertex_buffer);
	GX2DrawIndexedEx(GX2_PRIMITIVE_MODE_TRIANGLES, IDX_COUNT, gx_types[c->type], c->indices, 0, 1);
}

static void draw_tile(unsigned index, float width, float height)
{
	tile_background(width, height);
	if (cases[index].drawn)
		draw_case(&cases[index]);
}

static int build_vertices(void)
{
	static uint8_t bytes[VERTEX_COUNT * VERTEX_STRIDE];
	scene_point points[12];
	unsigned i, k;

	scene_corners(points);
	memset(bytes, 0, sizeof(bytes));
	for (i = 0; i < 12; i++)
	{
		float values[6];
		uint8_t rgba[4];
		float value[4];

		scene_corner_color(i / 3, rgba);
		vtx_value_for_pixel(rgba, value);
		values[0] = points[i].x;
		values[1] = points[i].y;
		for (k = 0; k < 4; k++)
			values[2 + k] = value[k];
		memcpy(bytes + i * VERTEX_STRIDE, values, sizeof(values)); /* big-endian floats: the CPU's own */
	}
	vertex_buffer = gx_buffer(bytes, sizeof(bytes), GX2_VERTEX_BUFFER_ALIGNMENT);
	return vertex_buffer != NULL;
}

static int near_pixel(const uint8_t a[4], const uint8_t b[4], unsigned tolerance)
{
	return readback_distance(a, b) <= tolerance;
}

void stage_index(gx_session *s)
{
	report *r = s->report;
	static const gx_stream streams[2] =
	{
		{ "a_pos", 0, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32, GX_MASK_XY01, GX2_ENDIAN_SWAP_DEFAULT },
		{ "a_val", 0, 8, GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32, GX_MASK_XYZW, GX2_ENDIAN_SWAP_DEFAULT },
	};
	char error[160];
	unsigned t, big, n = 0;
	int control_ok = 1;
	page *p;

	report_section(r, "index");
	if (!s->ready || !s->readback_ok)
	{
		report_note(r, "skipped: %s", !s->ready ? "the system stage did not finish" : "the target cannot be read back (see control)");
		return;
	}
	if (!build_vertices() || !gx_fetch_create(&fetch, s->attr.group.vertexShader, streams, 2, error, sizeof(error)))
	{
		report_check(r, 0, "index vertices", "%s", vertex_buffer ? error : "no memory for the vertex buffer");
		return;
	}
	p = session_new_page(s, "index buffers");
	if (p)
	{
		unsigned top = 2;

		top += page_wrap(p, 1, top, 100, STYLE_NORMAL, "Four triangles, one to a corner, picked by nine indices that leave the top right one out. Three triangles in a tile: the index buffer was read as written.");
		page_wrap(p, 1, top, 100, STYLE_DIM, "Frame: green = read as written, red = not, grey = a mismatch that failed as the model says. Numbers: report [index].");
	}
	for (t = 0; t < IDX_TYPE_COUNT; t++)
	{
		for (big = 0; big < 2; big++, n++)
		{
			index_case *c = &cases[n];
			uint8_t data[IDX_MAX_BYTES];
			unsigned width = idx_type_width((idx_type)t), corner, read[IDX_COUNT];
			int matched = idx_type_swaps((idx_type)t) == (int)big;
			char name[40];

			c->type = (idx_type)t;
			c->big_endian = (int)big;
			snprintf(name, sizeof(name), "%s / %s data", idx_type_name(c->type), big ? "BE" : "LE");
			idx_pack(width, (int)big, data);
			idx_read(c->type, data, read);
			c->predicted = idx_corners(read);
			if (width == 4 && !matched)
			{
				report_note(r, "%s: not drawn (the type would read indices of %u and more: past a vertex buffer of %u)", name, 9u << 24, VERTEX_COUNT);
				if (p)
				{
					unsigned col = 2 + t * 26, row = 6 + big * 14;

					page_text(p, col, row + 9, STYLE_DIM, "%s", name);
					page_text(p, col, row + 10, STYLE_DIM, "not drawn: unsafe");
				}
				continue;
			}
			c->indices = gx_buffer(data, width * IDX_COUNT, GX2_INDEX_BUFFER_ALIGNMENT);
			if (!c->indices)
			{
				report_check(r, 0, "index buffer", "%s: no memory", name);
				continue;
			}
			scene_begin(s);
			draw_case(c);
			scene_end(s);
			c->drawn = 1;
			for (corner = 0; corner < 4; corner++)
			{
				unsigned x, y;
				uint8_t found[4], color[4];

				scene_corner_sample(corner, &x, &y);
				scene_corner_color(corner, color);
				session_pixel(s, x, y, found);
				if (near_pixel(found, color, 6))
					c->observed |= 1u << corner;
				else if (!near_pixel(found, scene_clear_rgba, 3))
					c->other |= 1u << corner;
			}
			{
				int right = c->observed == 0xD && !c->other;

				if (matched)
				{
					static const char *const keys[IDX_TYPE_COUNT] = { "index.u16_le", "index.u32_le", "index.u16", "index.u32" };

					report_check(r, right, "index", "%s: corners drawn %#x (right is 0xD)%s", name, c->observed, c->other ? ", something else drawn too" : "");
					answers_set(s->answers, keys[t], "%s", right ? "reads the indices as written" : "DOES NOT read them as written");
				}
				else
				{
					int as_modelled = c->observed == c->predicted && !c->other;

					report_value(r, "index (mismatch control)", "%s: corners drawn %#x, model says %#x%s", name, c->observed, c->predicted,
						as_modelled ? " (as modelled)" : " (NOT as modelled)");
					control_ok = control_ok && as_modelled;
				}
				if (p)
				{
					unsigned col = 2 + t * 26, row = 6 + big * 14;
					int border = matched ? (right ? STYLE_GOOD : STYLE_BAD) : (c->observed == c->predicted ? STYLE_DIM : STYLE_BAD);

					page_tile_add(p, col, row, 12, 8, draw_tile, n, border);
					page_text(p, col, row + 9, STYLE_NORMAL, "%s", name);
					page_text(p, col, row + 10, right || !matched ? STYLE_NORMAL : STYLE_BAD, "corners %#x%s", c->observed, matched ? (right ? " right" : " WRONG") : "");
				}
			}
			if (!s->keep_going())
				return;
		}
	}
	answers_set(s->answers, "index.control", "16-bit data in the other byte order %s", control_ok ? "fails as modelled (so a pass above means something)" : "does NOT fail as modelled");
}
