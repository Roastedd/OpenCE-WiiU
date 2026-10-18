#include "stages.h"

#include "../iface_check.h"

#include <stdio.h>
#include <string.h>

static gx_session *current;

gx_session *session_current(void)
{
	return current;
}

void session_set_current(gx_session *s)
{
	current = s;
}

page *session_new_page(gx_session *s, const char *title)
{
	page *p;

	if (s->page_count >= SESSION_PAGES)
		return NULL;
	p = &s->pages[s->page_count++];
	page_clear(p);
	page_text(p, 1, 0, STYLE_HEAD, "OpenCE Wii U GX2 probe: %s", title);
	page_text(p, 1, 39, STYLE_DIM, "A next page   B previous page   HOME leave   report: /vol/external01/wiiu/apps/opence-probe-gx2/");
	return p;
}

void session_pixel(const gx_session *s, unsigned x, unsigned y, uint8_t rgba[4])
{
	readback_pixel(gx_target_pixels(&s->scene), s->scene.pitch_bytes, x, y, &s->order, rgba);
}

const char *pixel_text(const uint8_t rgba[4])
{
	static char text[4][20];
	static unsigned next;
	char *t = text[next++ & 3];

	snprintf(t, 20, "%u,%u,%u,%u", rgba[0], rgba[1], rgba[2], rgba[3]);
	return t;
}

void scene_begin(gx_session *s)
{
	ui_frame();
	gx_target_begin(&s->scene);
	gx_target_clear(&s->scene, scene_clear_rgba);
	ui_begin((float)SCENE_SIZE, (float)SCENE_SIZE);
}

void scene_end(gx_session *s)
{
	ui_flush();
	gx_target_finish(&s->scene);
}

void tile_background(float width, float height)
{
	float c[4];
	unsigned i;

	for (i = 0; i < 4; i++)
		c[i] = (float)scene_clear_rgba[i] / 255.0f;
	c[3] = 1.0f;
	ui_rect(0.0f, 0.0f, width, height, c);
	ui_flush();
}

void draw_texture_fill(const gx_session *s, const GX2Texture *texture, float width, float height)
{
	static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	float v_top = s->rows_flipped ? 1.0f : 0.0f, v_bottom = s->rows_flipped ? 0.0f : 1.0f;

	ui_quad(0.0f, 0.0f, width, height, texture, 0.0f, v_top, 1.0f, v_bottom, white, 0);
}

/* one line of "name(count at offset)" for the uniforms that are not elements of an array */
static void uniform_list(char *out, size_t size, const GX2UniformVar *vars, unsigned count)
{
	size_t used = 0;
	unsigned i;

	out[0] = 0;
	for (i = 0; i < count && used + 24 < size; i++)
	{
		if (strchr(vars[i].name, '['))
			continue;
		used += (size_t)snprintf(out + used, size - used, "%s(%u at %u) ", vars[i].name, (unsigned)vars[i].count, (unsigned)vars[i].offset);
	}
}

void report_shader_pair(report *r, const char *name, const GX2VertexShader *vs, const GX2PixelShader *ps)
{
	char line[200];
	size_t used;
	unsigned i;
	int first;

	if (vs)
	{
		used = (size_t)snprintf(line, sizeof(line), "inputs:");
		for (i = 0; i < vs->attribVarCount && used < sizeof(line) - 24; i++)
			used += (size_t)snprintf(line + used, sizeof(line) - used, " %s@%u", vs->attribVars[i].name, (unsigned)vs->attribVars[i].location);
		report_note(r, "shader %s vertex: %u bytes, mode %d, %u registers; %s", name, (unsigned)vs->size, (int)vs->mode,
			(unsigned)GX2GetVertexShaderGPRs(vs), line);
		uniform_list(line, sizeof(line), vs->uniformVars, vs->uniformVarCount);
		if (line[0] || vs->uniformBlockCount)
			report_note(r, "shader %s vertex uniforms: %s%s", name, line, vs->uniformBlockCount ? "and uniform blocks" : "");
	}
	if (ps)
	{
		used = (size_t)snprintf(line, sizeof(line), "samplers:");
		for (i = 0; i < ps->samplerVarCount && used < sizeof(line) - 24; i++)
			used += (size_t)snprintf(line + used, sizeof(line) - used, " %s@%u", ps->samplerVars[i].name, (unsigned)ps->samplerVars[i].location);
		report_note(r, "shader %s pixel: %u bytes, mode %d, %u registers; %s", name, (unsigned)ps->size, (int)ps->mode,
			(unsigned)GX2GetPixelShaderGPRs(ps), line);
		uniform_list(line, sizeof(line), ps->uniformVars, ps->uniformVarCount);
		if (line[0] || ps->uniformBlockCount)
			report_note(r, "shader %s pixel uniforms: %s%s", name, line, ps->uniformBlockCount ? "and uniform blocks" : "");
	}
	if (vs && ps)
	{
		unsigned missing = iface_missing(vs->regs.spi_vs_out_id, vs->regs.num_spi_vs_out_id, ps->regs.spi_ps_input_cntls, ps->regs.num_spi_ps_input_cntl, &first);

		report_check(r, missing == 0, "interpolants", "%s: %u pixel inputs, %u without a vertex output%s", name, (unsigned)ps->regs.num_spi_ps_input_cntl, missing,
			missing ? " (the pixel shader reads another varying than the vertex shader wrote)" : "");
	}
}

void session_overview(const gx_session *s, page *overview, const char *flavour, const char *build)
{
	const answers *a = s->answers;
	unsigned i, row = 2;

	page_clear(overview);
	page_text(overview, 1, 0, STYLE_HEAD, "OpenCE Wii U GX2 probe (%s, build %s): the answers", flavour, build);
	page_text(overview, 1, 39, STYLE_DIM, "A next page   B previous page   HOME leave   report: /vol/external01/wiiu/apps/opence-probe-gx2/");
	for (i = 0; i < a->count && row < 38; i++)
	{
		const char *text = a->text[i];
		int bad = strstr(text, "NOT") || strstr(text, "WRONG") || strstr(text, "REFUSED") || strstr(text, "NONE") || strstr(text, "NOTHING");

		page_text(overview, 1, row, STYLE_HEAD, "%s", a->key[i]);
		row += page_wrap(overview, 26, row, 78, bad ? STYLE_BAD : STYLE_NORMAL, "%s", text);
	}
	if (s->report->failures)
		page_text(overview, 1, 38, STYLE_BAD, "%u checks failed: see the report", s->report->failures);
}
