/*
DK_DERIVE.C

A draw's GPU state, shader keys, samplers and uniforms from Direct3D's render and texture stage states and the draw's facts (dk_derive.h). This is the
derivation d3d8_gx2.c's draw_prepare made on the game thread for every draw, moved as it was: draw_state_make, pixel_key_make without the textures'
lookup (which stays with the game thread, which owns the texture cache: the facts carry what it found), sampler_make and draw_parameters_send's inputs.
The GX2 thread runs it before a draw when a state or the facts have changed, and applies what differs from the last run.
*/

#include "xgpu.h"
#include "dk_derive.h"
#include "dk_textures.h"
#include "wiiu_uniform_cache.h"

#include <math.h>
#include <string.h>

typedef char dk_derive_render_state_count_check[DK_DERIVE_RENDER_STATES == D3DRS_MAX ? 1 : -1];
typedef char dk_derive_texture_state_count_check[DK_DERIVE_TEXTURE_STATES == D3DTSS_MAX ? 1 : -1];
typedef char dk_derive_stage_count_check[DK_DERIVE_STAGES == D3DTSS_MAXSTAGES ? 1 : -1];

struct dk_derive
{
	DWORD rs[D3DRS_MAX];
	DWORD ts[D3DTSS_MAXSTAGES][D3DTSS_MAX];
	struct dk_draw_facts facts;
	int pending;
	int run;

	struct dk_draw_state state;
	struct dk_vertex_key vertex_key;
	struct nv2a_pixel_shader_key pixel_key;
	struct dk_command_textures textures;
	struct wiiu_uniform_cache uniforms;
};

static struct dk_derive derive_instance;

/* (a word at a time: the structs compared are words throughout, and the library's memcmp goes a byte at a time) */
static int words_differ(const void *a, const void *b, unsigned long size)
{
	const uint32_t *x = (const uint32_t *)a, *y = (const uint32_t *)b;
	uint32_t different = 0;
	unsigned long words = size / 4, i;

	for (i = 0; i < words; i++)
		different |= x[i] ^ y[i];
	return different != 0;
}

struct dk_derive *dk_derive_get(void)
{
	return &derive_instance;
}

unsigned long dk_derive_size(void)
{
	return sizeof(struct dk_derive);
}

void dk_derive_writes(struct dk_derive *derive, const struct dk_command_state_writes *command)
{
	uint32_t index, count = command->count;

	if (sizeof(*command) + (unsigned long)count * 8 > command->header.size)
		return;
	for (index = 0; index < count; index++)
	{
		const uint32_t which = command->writes[index * 2], value = command->writes[index * 2 + 1];

		if (which < D3DRS_MAX)
			derive->rs[which] = value;
		else if (which >= DK_DERIVE_TEXTURE_STATE_INDEX(0, 0) && which < DK_DERIVE_TEXTURE_STATE_INDEX(D3DTSS_MAXSTAGES, 0))
		{
			const uint32_t offset = which - DK_DERIVE_TEXTURE_STATE_INDEX(0, 0);

			derive->ts[offset / D3DTSS_MAX][offset % D3DTSS_MAX] = value;
		}
	}
	derive->pending = 1;
}

void dk_derive_facts(struct dk_derive *derive, const struct dk_draw_facts *facts)
{
	derive->facts = *facts;
	derive->pending = 1;
}

int dk_derive_pending(const struct dk_derive *derive)
{
	return derive->pending;
}

/* ---------- the derivation (d3d8_gx2.c's, as it was) */

static float dword_to_float(DWORD value)
{
	union { DWORD d; float f; } u;

	u.d = value;
	return u.f;
}

static void color_to_vec4(D3DCOLOR color, float *out)
{
	out[0] = ((color >> 16) & 0xff) / 255.0f;
	out[1] = ((color >> 8) & 0xff) / 255.0f;
	out[2] = (color & 0xff) / 255.0f;
	out[3] = ((color >> 24) & 0xff) / 255.0f;
}

static int target_pixel(const struct dk_draw_facts *facts, float coordinate, int axis)
{
	return (int)floorf(coordinate * facts->target_scale[axis] + 0.5f);
}

static uint32_t blend_equation(DWORD operation)
{
	switch (operation)
	{
	case D3DBLENDOP_SUBTRACT: return DK_BLEND_SUBTRACT;
	case D3DBLENDOP_REVSUBTRACT:
	case D3DBLENDOP_REVSUBTRACTSIGNED: return DK_BLEND_REVERSE_SUBTRACT;
	case D3DBLENDOP_MIN: return DK_BLEND_MIN;
	case D3DBLENDOP_MAX: return DK_BLEND_MAX;
	default: return DK_BLEND_ADD;
	}
}

/* d3d8_gl.c's apply_raster_state, as a description of what it would set */
static void draw_state_make(const struct dk_derive *derive, struct dk_draw_state *state)
{
	const DWORD *rs = derive->rs;
	const struct dk_draw_facts *facts = &derive->facts;
	DWORD write = rs[D3DRS_COLORWRITEENABLE];
	BOOL depth_test = facts->has_depth && rs[D3DRS_ZENABLE];

	memset(state, 0, sizeof(*state));
	state->viewport[0] = target_pixel(facts, (float)facts->viewport_x, 0);
	state->viewport[1] = target_pixel(facts, (float)facts->viewport_y, 1);
	state->viewport[2] = target_pixel(facts, (float)(facts->viewport_x + facts->viewport_width), 0) - state->viewport[0];
	state->viewport[3] = target_pixel(facts, (float)(facts->viewport_y + facts->viewport_height), 1) - state->viewport[1];
	state->depth_range[0] = facts->viewport_min_z;
	state->depth_range[1] = facts->viewport_max_z;
	/* the game never issues a scissor rectangle, and the NV2A scissor register
	defaults to the viewport, so fragment clipping follows the viewport: this is
	what keeps a split-screen window's geometry from bleeding across the divider */
	memcpy(state->scissor, state->viewport, sizeof(state->scissor));

	state->depth_test = depth_test;
	if (depth_test)
	{
		state->depth_function = rs[D3DRS_ZFUNC] ? rs[D3DRS_ZFUNC] : D3DCMP_NEVER;
		state->depth_write = rs[D3DRS_ZWRITEENABLE] != 0;
	}

	state->stencil_test = facts->has_depth && rs[D3DRS_STENCILENABLE];
	if (state->stencil_test)
	{
		state->stencil_function = rs[D3DRS_STENCILFUNC] ? rs[D3DRS_STENCILFUNC] : D3DCMP_NEVER;
		state->stencil_reference = rs[D3DRS_STENCILREF];
		state->stencil_mask = rs[D3DRS_STENCILMASK];
		state->stencil_write_mask = rs[D3DRS_STENCILWRITEMASK];
		state->stencil_fail = rs[D3DRS_STENCILFAIL];
		state->stencil_depth_fail = rs[D3DRS_STENCILZFAIL];
		state->stencil_pass = rs[D3DRS_STENCILPASS];
	}

	state->blend = rs[D3DRS_ALPHABLENDENABLE] != 0;
	if (state->blend)
	{
		state->blend_source = rs[D3DRS_SRCBLEND];
		state->blend_destination = rs[D3DRS_DESTBLEND];
		state->blend_equation = blend_equation(rs[D3DRS_BLENDOP]);
		color_to_vec4(rs[D3DRS_BLENDCOLOR], state->blend_color);
	}
	state->color_mask = ((write & D3DCOLORWRITEENABLE_RED) ? 1 : 0) | ((write & D3DCOLORWRITEENABLE_GREEN) ? 2 : 0) |
		((write & D3DCOLORWRITEENABLE_BLUE) ? 4 : 0) | ((write & D3DCOLORWRITEENABLE_ALPHA) ? 8 : 0);

	/* the cull mode names the winding to discard; FRONTFACE names the front
	winding */
	if (rs[D3DRS_CULLMODE] != D3DCULL_NONE)
		state->cull = rs[D3DRS_CULLMODE] == rs[D3DRS_FRONTFACE] ? DK_CULL_FRONT : DK_CULL_BACK;
	state->front_face_ccw = rs[D3DRS_FRONTFACE] == D3DFRONT_CCW;
	state->fill_mode = rs[D3DRS_FILLMODE] == D3DFILL_WIREFRAME ? DK_FILL_LINE :
		rs[D3DRS_FILLMODE] == D3DFILL_POINT ? DK_FILL_POINT : DK_FILL_SOLID;

	/* D3DRS_ZBIAS is expressed in these states (D3DDevice_SetRenderState_ZBias) */
	state->offset_enable = rs[D3DRS_SOLIDOFFSETENABLE] != 0;
	if (state->offset_enable)
	{
		state->offset_slope = dword_to_float(rs[D3DRS_POLYGONOFFSETZSLOPESCALE]);
		state->offset_units = dword_to_float(rs[D3DRS_POLYGONOFFSETZOFFSET]);
	}
}

static uint32_t address_mode(DWORD mode)
{
	switch (mode)
	{
	case D3DTADDRESS_MIRROR: return DK_WRAP_MIRROR;
	case D3DTADDRESS_CLAMP:
	case D3DTADDRESS_CLAMPTOEDGE: return DK_WRAP_CLAMP;
	case D3DTADDRESS_BORDER: return DK_WRAP_BORDER;
	default: return DK_WRAP_REPEAT;
	}
}

/* what d3d8_gl.c's configure_sampler decides for a stage, as values for the
host's sampler. mipmapped: the texture has more than one level. hires: it is
a high-res replacement (xbox_textures_dk.c), drawn smaller than it is, so
filtered and from its mip levels whatever the game asks. */
static void sampler_make(const DWORD *state, BOOL mipmapped, BOOL hires, struct dk_sampler *sampler)
{
	DWORD min_filter = hires ? D3DTEXF_LINEAR : state[D3DTSS_MINFILTER];
	DWORD mip_filter = hires ? D3DTEXF_LINEAR : mipmapped ? state[D3DTSS_MIPFILTER] : D3DTEXF_NONE;

	memset(sampler, 0, sizeof(*sampler));
	sampler->min_linear = min_filter != D3DTEXF_POINT;
	sampler->mag_linear = hires || state[D3DTSS_MAGFILTER] != D3DTEXF_POINT;
	sampler->mip_filter = mip_filter == D3DTEXF_NONE ? DK_MIP_NONE : mip_filter == D3DTEXF_POINT ? DK_MIP_NEAREST :
		DK_MIP_LINEAR;
	sampler->wrap[0] = address_mode(state[D3DTSS_ADDRESSU]);
	sampler->wrap[1] = address_mode(state[D3DTSS_ADDRESSV]);
	sampler->wrap[2] = address_mode(state[D3DTSS_ADDRESSW]);
	sampler->lod_bias = hires ? 0.0f : dword_to_float(state[D3DTSS_MIPMAPLODBIAS]);
	sampler->lod_minimum = hires ? 0.0f : (float)state[D3DTSS_MAXMIPLEVEL];
	sampler->anisotropy = (min_filter == D3DTEXF_ANISOTROPIC && state[D3DTSS_MAXANISOTROPY] > 1) ?
		(float)state[D3DTSS_MAXANISOTROPY] : 1.0f;
	color_to_vec4(state[D3DTSS_BORDERCOLOR], sampler->border);
}

/* The pixel shader's key as prepare_draw builds it, and what the stages
sample: the stages' textures as the game thread found them (facts). */
static void pixel_key_make(const struct dk_derive *derive, struct nv2a_pixel_shader_key *key, float texture_scale[4][4],
	struct dk_command_textures *textures)
{
	const DWORD *rs = derive->rs;
	const struct dk_draw_facts *facts = &derive->facts;
	int stage;

	memset(key, 0, sizeof(*key));
	memset(textures, 0, sizeof(*textures));
	textures->header.type = DK_COMMAND_TEXTURES;
	textures->header.size = sizeof(*textures);
	memcpy(key->combiner_state, rs, sizeof(key->combiner_state));
	/* constants are uniforms, not part of the program */
	memset(&key->combiner_state[D3DRS_PSCONSTANT0_0], 0, 16 * sizeof(DWORD));
	key->combiner_state[D3DRS_PSFINALCOMBINERCONSTANT0] = 0;
	key->combiner_state[D3DRS_PSFINALCOMBINERCONSTANT1] = 0;
	key->texture_modes = rs[D3DRS_PSTEXTUREMODES];
	if (facts->texture_clip[2] != 0.0f)
		key->texture_modes |= NV2A_PIXEL_KEY_CLIP;
	if (facts->warp_blend)
		key->texture_modes |= NV2A_PIXEL_KEY_WARP_BLEND;
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		const struct dk_stage_facts *found = &facts->stages[stage];
		unsigned long mode = (rs[D3DRS_PSTEXTUREMODES] >> (5 * stage)) & 0x1f;

		texture_scale[stage][0] = texture_scale[stage][1] = 1.0f;
		texture_scale[stage][2] = texture_scale[stage][3] = 1.0f;
		key->alpha_kill[stage] = derive->ts[stage][D3DTSS_ALPHAKILL] == D3DTALPHAKILL_ENABLE;
		key->color_sign[stage] = (unsigned char)((derive->ts[stage][D3DTSS_COLORSIGN] >> 28) & 0xf);
		if (!found->present)
		{
			key->sampler_type[stage] = mode == 0x11 ? _xgpu_sampler_2d : _xgpu_sampler_none;
			continue;
		}
		textures->stages[stage].id = found->id;
		textures->stages[stage].target = found->target;
		textures->stages[stage].composite = found->composite;
		texture_scale[stage][0] = found->scale[0];
		texture_scale[stage][1] = found->scale[1];
		if (stage == 0)
		{
			key->coverage_alpha = found->hires_coverage != FALSE;
			/* (the meters' discrete thresholds in red, read unfiltered: d3d8_gl.c's bind_textures has it from upstream's "Read discrete health thresholds
			independently of filtered HUD artwork"; the key's field was in the wire format, but nothing set it) */
			key->point_threshold = found->hires_point_threshold != FALSE;
		}
		key->sampler_type[stage] = found->kind == DK_TEXTURE_CUBE ? _xgpu_sampler_cube : found->kind == DK_TEXTURE_3D ?
			_xgpu_sampler_3d : _xgpu_sampler_2d;
		sampler_make(derive->ts[stage], found->mipmapped, found->hires, &textures->stages[stage].sampler);
	}
	/* (only with the meter's blend: hud_hires.h, nv2a_pixel_shader_key) */
	key->coverage_alpha = key->coverage_alpha && rs[D3DRS_ALPHABLENDENABLE] &&
		rs[D3DRS_SRCBLEND] == D3DBLEND_CONSTANTCOLOR &&
		rs[D3DRS_DESTBLEND] == D3DBLEND_SRCALPHA;
	key->point_threshold = key->point_threshold && key->coverage_alpha;
	key->alpha_test_function = rs[D3DRS_ALPHATESTENABLE] ? rs[D3DRS_ALPHAFUNC] : 0;
	key->fog_enable = rs[D3DRS_FOGENABLE] != 0;
	key->fog_table_mode = (unsigned char)rs[D3DRS_FOGTABLEMODE];
}

/* draw_parameters_send's inputs (wiiu_uniform_cache.h: the 76 words, in its order) */
static void parameter_inputs(const struct dk_derive *derive, float texture_scale[4][4], DWORD *inputs)
{
	const DWORD *rs = derive->rs;
	const struct dk_draw_facts *facts = &derive->facts;
	unsigned long count = 0;
	int stage;

	memcpy(&inputs[count], facts->viewport_scale, sizeof(facts->viewport_scale));
	count += 4;
	memcpy(&inputs[count], facts->viewport_offset, sizeof(facts->viewport_offset));
	count += 4;
	memcpy(&inputs[count], texture_scale, 16 * sizeof(float));
	count += 16;
	inputs[count++] = rs[D3DRS_POINTSIZE];
	for (stage = 0; stage < 8; stage++)
	{
		inputs[count++] = rs[D3DRS_PSCONSTANT0_0 + stage];
		inputs[count++] = rs[D3DRS_PSCONSTANT1_0 + stage];
	}
	inputs[count++] = rs[D3DRS_PSFINALCOMBINERCONSTANT0];
	inputs[count++] = rs[D3DRS_PSFINALCOMBINERCONSTANT1];
	inputs[count++] = rs[D3DRS_FOGCOLOR];
	inputs[count++] = rs[D3DRS_FOGSTART];
	inputs[count++] = rs[D3DRS_FOGEND];
	inputs[count++] = rs[D3DRS_FOGDENSITY];
	inputs[count++] = rs[D3DRS_ALPHAREF];
	inputs[count++] = (DWORD)facts->ui_offset;
	memcpy(&inputs[count], facts->texture_clip, 3 * sizeof(float));
	count += 3;
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		const DWORD *state = derive->ts[stage];

		inputs[count++] = state[D3DTSS_BUMPENVMAT00];
		inputs[count++] = state[D3DTSS_BUMPENVMAT01];
		inputs[count++] = state[D3DTSS_BUMPENVMAT10];
		inputs[count++] = state[D3DTSS_BUMPENVMAT11];
		inputs[count++] = state[D3DTSS_BUMPENVLSCALE];
		inputs[count++] = state[D3DTSS_BUMPENVLOFFSET];
	}
}

/* the uniforms' part of a run (draw_parameters_send's, as it was): which of the vertex and pixel parameters changed (DK_DERIVED_*); the first run,
both (tools/test_wiiu_uniform_cache.py checks it against the frozen legacy conversion) */
static unsigned derive_parameters(struct dk_derive *derive, float texture_scale[4][4], int first)
{
	DWORD inputs[WIIU_UNIFORM_INPUT_WORDS];
	unsigned changed = 0, uniforms;

	parameter_inputs(derive, texture_scale, inputs);
	uniforms = wiiu_uniform_cache_update(&derive->uniforms, inputs);
	if (first || (uniforms & WIIU_UNIFORM_VERTEX_CHANGED))
		changed |= DK_DERIVED_VERTEX_PARAMETERS;
	if (first || (uniforms & WIIU_UNIFORM_PIXEL_CHANGED))
		changed |= DK_DERIVED_PIXEL_PARAMETERS;
	return changed;
}

unsigned dk_derive_run(struct dk_derive *derive)
{
	struct dk_draw_state state;
	struct dk_vertex_key vertex_key;
	struct nv2a_pixel_shader_key pixel_key;
	struct dk_command_textures textures;
	float texture_scale[4][4];
	unsigned changed = 0;
	const int first = !derive->run;

	derive->pending = 0;
	derive->run = 1;
	draw_state_make(derive, &state);
	if (first || words_differ(&state, &derive->state, sizeof(state)))
	{
		derive->state = state;
		changed |= DK_DERIVED_STATE;
	}
	pixel_key_make(derive, &pixel_key, texture_scale, &textures);
	memset(&vertex_key, 0, sizeof(vertex_key));
	vertex_key.program_hash = (uint64_t)derive->facts.program_hash[0] << 32 | derive->facts.program_hash[1];
	vertex_key.packed_mask = derive->facts.packed_mask;
	if (first || words_differ(&vertex_key, &derive->vertex_key, sizeof(vertex_key)) || words_differ(&pixel_key, &derive->pixel_key, sizeof(pixel_key)))
	{
		derive->vertex_key = vertex_key;
		derive->pixel_key = pixel_key;
		changed |= DK_DERIVED_KEYS;
	}
	if (first || words_differ(textures.stages, derive->textures.stages, sizeof(textures.stages)))
	{
		derive->textures = textures;
		changed |= DK_DERIVED_TEXTURES;
	}
	changed |= derive_parameters(derive, texture_scale, first);
	return changed;
}

const struct dk_draw_state *dk_derive_state(const struct dk_derive *derive)
{
	return &derive->state;
}

const void *dk_derive_vertex_key(const struct dk_derive *derive)
{
	return &derive->vertex_key;
}

const void *dk_derive_pixel_key(const struct dk_derive *derive)
{
	return &derive->pixel_key;
}

const struct dk_command_textures *dk_derive_textures(const struct dk_derive *derive)
{
	return &derive->textures;
}

const struct dk_vertex_parameters *dk_derive_vertex_parameters(const struct dk_derive *derive)
{
	return &derive->uniforms.vertex;
}

const struct dk_pixel_parameters *dk_derive_pixel_parameters(const struct dk_derive *derive)
{
	return &derive->uniforms.pixel;
}
