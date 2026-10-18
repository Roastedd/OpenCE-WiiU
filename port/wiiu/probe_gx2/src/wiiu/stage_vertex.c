/*
The vertex stream stage: the formats the game's vertex declarations use, as the Xbox stores them (little-endian), fetched
with each byte swap GX2 offers: none, 8_IN_16, 8_IN_32 and DEFAULT (what the system picks for a stream written by a
big-endian CPU). Two triangles, a value to each, the pixel shader shows the value, the CPU reads it back and says which
setting delivered the value the game means. The positions come from a stream of the CPU's own floats with DEFAULT (what
every GX2 program does); if those are wrong nothing is drawn, and the report says "nothing drawn" rather than a wrong
value. See vertex_test.h for the model of what each swap should do.
*/

#include "../vertex_test.h"
#include "stages.h"

#include <stdio.h>
#include <string.h>

#define MODES 5 /* the intended value drawn from floats, then none, 8_IN_16, 8_IN_32, DEFAULT */
#define MODE_REFERENCE 0
#define MODE_DEFAULT 4

typedef struct vertex_case
{
	vtx_format format;
	unsigned mode;
	uint8_t sel[4];
	gx_fetch fetch;
	void *positions;
	void *values;
	unsigned element_bytes;
	int built;
	int drawn;          /* something other than the clear color was found */
	int intended;       /* both pixels are the pixels of the intended values */
	unsigned acts_as;   /* the swaps (bits) the pixels agree with */
	uint8_t found[2][4];
} vertex_case;

static vertex_case cases[VTX_FORMAT_COUNT][MODES];

static GX2AttribFormat gx_format(vtx_format f)
{
	switch (f)
	{
	case VTX_FLOAT32X4: return GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32;
	case VTX_FLOAT32X2: return GX2_ATTRIB_FORMAT_FLOAT_32_32;
	case VTX_UNORM8X4: return GX2_ATTRIB_FORMAT_UNORM_8_8_8_8;
	case VTX_SNORM16X4: return (GX2AttribFormat)(GX2_ATTRIB_TYPE_16_16_16_16 | GX2_ATTRIB_FLAG_SIGNED);
	case VTX_SSCALED16X2: return (GX2AttribFormat)(GX2_ATTRIB_TYPE_16_16 | GX2_ATTRIB_FLAG_SIGNED | GX2_ATTRIB_FLAG_SCALED);
	default: return (GX2AttribFormat)(GX2_ATTRIB_FLAG_INTEGER | GX2_ATTRIB_TYPE_32);
	}
}

static const char *gx_format_name(vtx_format f)
{
	static const char *const names[VTX_FORMAT_COUNT] =
	{
		"FLOAT_32_32_32_32", "FLOAT_32_32", "UNORM_8_8_8_8", "SNORM_16_16_16_16", "SSCALED_16_16", "UINT_32"
	};

	return names[f];
}

static uint32_t selection_mask(const uint8_t sel[4])
{
	uint32_t c[4];
	unsigned i;

	for (i = 0; i < 4; i++)
		c[i] = sel[i] < 4 ? (uint32_t)GX2_SQ_SEL_X + sel[i] : sel[i] == VTX_SEL_ONE ? (uint32_t)GX2_SQ_SEL_1 : (uint32_t)GX2_SQ_SEL_0;
	return GX2_SEL_MASK(c[0], c[1], c[2], c[3]);
}

static GX2EndianSwapMode gx_swap(unsigned mode)
{
	static const GX2EndianSwapMode modes[MODES] =
	{
		GX2_ENDIAN_SWAP_DEFAULT, GX2_ENDIAN_SWAP_NONE, GX2_ENDIAN_SWAP_8_IN_16, GX2_ENDIAN_SWAP_8_IN_32, GX2_ENDIAN_SWAP_DEFAULT
	};

	return modes[mode];
}

static const char *mode_name(unsigned mode)
{
	static const char *const names[MODES] = { "intended", "none", "8_IN_16", "8_IN_32", "DEFAULT" };

	return names[mode];
}

static void draw_case(const vertex_case *c)
{
	gx_session *s = session_current();
	const gx_pipeline *pipeline = c->format == VTX_PACKED3 && c->mode != MODE_REFERENCE ? &s->packed : &s->attr;

	gx_state(0);
	gx_bind(pipeline->group.vertexShader, pipeline->group.pixelShader, &c->fetch.shader, GX2_SHADER_MODE_UNIFORM_REGISTER);
	GX2SetAttribBuffer(0, 6 * 8, 8, c->positions);
	GX2SetAttribBuffer(1, 6 * c->element_bytes, c->element_bytes, c->values);
	GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, 6, 0, 1);
}

static void draw_tile(unsigned index, float width, float height)
{
	tile_background(width, height);
	if (cases[index / MODES][index % MODES].built)
		draw_case(&cases[index / MODES][index % MODES]);
}

static int build_case(gx_session *s, vertex_case *c, vtx_format f, unsigned mode, char *error, size_t error_size)
{
	gx_stream streams[2];
	scene_point points[6];
	float positions[12];
	unsigned i, which;
	uint8_t bytes[VTX_MAX_BYTES * 6];
	uint8_t intended_pixel[2][4];
	float value[4];

	memset(c, 0, sizeof(*c));
	c->format = f;
	c->mode = mode;
	scene_halves(points);
	for (i = 0; i < 6; i++)
	{
		positions[2 * i] = points[i].x;
		positions[2 * i + 1] = points[i].y;
	}
	c->positions = gx_buffer(positions, sizeof(positions), GX2_VERTEX_BUFFER_ALIGNMENT);
	streams[0].name = "a_pos";
	streams[0].buffer = 0;
	streams[0].offset = 0;
	streams[0].format = GX2_ATTRIB_FORMAT_FLOAT_32_32;
	streams[0].mask = GX_MASK_XY01;
	streams[0].swap = GX2_ENDIAN_SWAP_DEFAULT;
	streams[1].buffer = 1;
	streams[1].offset = 0;

	for (which = 0; which < 2; which++)
	{
		vtx_intended(f, which, value);
		vtx_to_pixel(value, intended_pixel[which]);
	}
	if (mode == MODE_REFERENCE)
	{
		/* the intended pixels as plain floats, the CPU's own: the picture the other tiles are meant to equal */
		float values[6][4];

		for (i = 0; i < 6; i++)
		{
			vtx_value_for_pixel(intended_pixel[i / 3], value);
			memcpy(values[i], value, sizeof(value));
		}
		c->element_bytes = 16;
		c->values = gx_buffer(values, sizeof(values), GX2_VERTEX_BUFFER_ALIGNMENT);
		streams[1].name = "a_val";
		streams[1].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32;
		streams[1].mask = GX_MASK_XYZW;
		streams[1].swap = GX2_ENDIAN_SWAP_DEFAULT;
		memset(c->sel, 0, sizeof(c->sel));
		c->sel[1] = 1;
		c->sel[2] = 2;
		c->sel[3] = 3;
	}
	else
	{
		/* the stream as the Xbox holds it: three vertices of the first value, three of the second */
		unsigned element = vtx_format_bytes(f);

		c->element_bytes = element;
		for (i = 0; i < 6; i++)
		{
			uint8_t one[VTX_MAX_BYTES];

			vtx_pack_le(f, i / 3, one);
			memcpy(bytes + i * element, one, element);
		}
		c->values = gx_buffer(bytes, 6 * element, GX2_VERTEX_BUFFER_ALIGNMENT);
		vtx_selection_for(f, mode == MODE_DEFAULT ? VTX_SWAP_NONE : (vtx_swap)(mode - 1), c->sel);
		streams[1].name = f == VTX_PACKED3 ? "a_packed" : "a_val";
		streams[1].format = gx_format(f);
		streams[1].mask = f == VTX_PACKED3 ? GX_MASK_X001 : selection_mask(c->sel);
		streams[1].swap = gx_swap(mode);
	}
	if (!c->positions || !c->values)
	{
		snprintf(error, error_size, "no memory for the vertex buffers");
		return 0;
	}
	{
		const gx_pipeline *pipeline = f == VTX_PACKED3 && mode != MODE_REFERENCE ? &s->packed : &s->attr;

		if (!gx_fetch_create(&c->fetch, pipeline->group.vertexShader, streams, 2, error, error_size))
			return 0;
	}
	c->built = 1;
	return 1;
}

static void observe(gx_session *s, vertex_case *c)
{
	unsigned which;
	uint8_t intended_pixel[2][4];
	float value[4];
	int same = 1, clear = 1;

	for (which = 0; which < 2; which++)
	{
		unsigned x, y;

		scene_halves_sample(which, &x, &y);
		session_pixel(s, x, y, c->found[which]);
		vtx_intended(c->format, which, value);
		vtx_to_pixel(value, intended_pixel[which]);
		if (readback_distance(c->found[which], intended_pixel[which]) > 4)
			same = 0;
		if (readback_distance(c->found[which], scene_clear_rgba) > 3)
			clear = 0;
	}
	c->drawn = !clear;
	c->intended = same;
	c->acts_as = c->mode == MODE_REFERENCE ? 0 : vtx_classify(c->format, c->sel, c->found[0], c->found[1], 4);
}

static void acts_as_text(unsigned bits, char *out, size_t size)
{
	unsigned h;
	size_t used = 0;

	out[0] = 0;
	for (h = 0; h < VTX_SWAP_COUNT; h++)
		if (bits & (1u << h))
			used += (size_t)snprintf(out + used, size - used, "%s%s", used ? "/" : "", vtx_swap_name((vtx_swap)h));
	if (!used)
		snprintf(out, size, "none of the three");
}

void stage_vertex(gx_session *s)
{
	report *r = s->report;
	char error[200], text[80], works[64];
	unsigned f, m;
	page *p;

	report_section(r, "vertex");
	if (!s->ready || !s->readback_ok)
	{
		report_note(r, "skipped: %s", !s->ready ? "the system stage did not finish" : "the target cannot be read back (see control)");
		return;
	}
	p = session_new_page(s, "little-endian vertex streams");
	if (p)
	{
		page_wrap(p, 1, 2, 100, STYLE_NORMAL, "Each format as the Xbox stores it (little-endian), fetched with each byte swap GX2 has. First tile of a row: the value the game means. Green frame: the same value. Grey: another. Red: nothing was drawn.");
		for (m = 0; m < MODES; m++)
			page_text(p, 22 + m * 10, 5, STYLE_HEAD, "%s", mode_name(m));
	}
	for (f = 0; f < VTX_FORMAT_COUNT; f++)
	{
		unsigned works_count = 0;

		works[0] = 0;
		for (m = 0; m < MODES; m++)
		{
			vertex_case *c = &cases[f][m];

			if (!build_case(s, c, (vtx_format)f, m, error, sizeof(error)))
			{
				report_check(r, 0, "vertex case", "%s / %s: %s", vtx_format_name((vtx_format)f), mode_name(m), error);
				continue;
			}
			scene_begin(s);
			draw_case(c);
			scene_end(s);
			observe(s, c);
			acts_as_text(c->acts_as, text, sizeof(text));
			if (m == MODE_REFERENCE)
				report_value(r, "vertex intended", "%s: first %s, second %s (drawn from plain floats)", vtx_format_name((vtx_format)f), pixel_text(c->found[0]),
					pixel_text(c->found[1]));
			else
				report_value(r, "vertex", "%s / %s (%s, mask %#x): first %s, second %s; %s; behaves as %s", vtx_format_name((vtx_format)f), mode_name(m),
					gx_format_name((vtx_format)f), (unsigned)selection_mask(c->sel), pixel_text(c->found[0]), pixel_text(c->found[1]),
					!c->drawn ? "NOTHING DRAWN" : c->intended ? "the intended value" : "not the intended value", text);
			if (m != MODE_REFERENCE && m != MODE_DEFAULT && c->intended)
			{
				snprintf(works + strlen(works), sizeof(works) - strlen(works), "%s%s", works_count ? ", " : "", mode_name(m));
				works_count++;
			}
			if (p)
			{
				int border = !c->drawn ? STYLE_BAD : (m == MODE_REFERENCE || c->intended) ? STYLE_GOOD : STYLE_DIM;

				page_tile_add(p, 22 + m * 10, 6 + f * 5, 8, 4, draw_tile, f * MODES + m, border);
			}
			if (!s->keep_going())
				return;
		}
		{
			vertex_case *d = &cases[f][MODE_DEFAULT];
			char acts[40];

			acts_as_text(d->acts_as, acts, sizeof(acts));
			if (p)
			{
				page_text(p, 1, 6 + f * 5, STYLE_NORMAL, "%s", vtx_format_name((vtx_format)f));
				page_text(p, 1, 7 + f * 5, STYLE_DIM, "%s", gx_format_name((vtx_format)f));
				page_text(p, 72, 6 + f * 5, works_count ? STYLE_GOOD : STYLE_BAD, "works: %s", works_count ? works : "none");
				page_text(p, 72, 7 + f * 5, d->drawn ? STYLE_NORMAL : STYLE_BAD, "DEFAULT %s the game's value", d->intended ? "gives" : "not");
				page_text(p, 72, 8 + f * 5, STYLE_DIM, "DEFAULT acts as %s", acts);
			}
			{
				char key[40];
				static const char *const slugs[VTX_FORMAT_COUNT] = { "float32x4", "float32x2", "d3dcolor", "snorm16x4", "short2", "normpacked3" };

				snprintf(key, sizeof(key), "vertex.%s", slugs[f]);
				answers_set(s->answers, key, "little-endian stream works with swap: %s; DEFAULT behaves as %s", works_count ? works : "NONE OF THEM", acts);
			}
		}
	}
}
