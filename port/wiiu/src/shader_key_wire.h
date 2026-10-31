/*
SHADER_KEY_WIRE.H

The pixel shader key (xgpu.h's struct nv2a_pixel_shader_key) as bytes that mean the same on every machine: the big-endian console writes the keys it
meets and a computer (little-endian) generates and compiles the shaders for them, so the key travels as a fixed field order with every 32-bit value
written least significant byte first. The same header is included by the game (gx2_shaders.c) and by the shader pack generator (tools/wiiu/shader_pack).
*/

#ifndef HALO_WIIU_SHADER_KEY_WIRE_H
#define HALO_WIIU_SHADER_KEY_WIRE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SHADER_KEY_WIRE_BYTES ((D3DRS_PS_MAX + 1 + 1) * 4 + 12 + 8)

static inline void shader_key_put32(unsigned char **at, uint32_t value)
{
	(*at)[0] = (unsigned char)value;
	(*at)[1] = (unsigned char)(value >> 8);
	(*at)[2] = (unsigned char)(value >> 16);
	(*at)[3] = (unsigned char)(value >> 24);
	*at += 4;
}

static inline uint32_t shader_key_get32(const unsigned char **at)
{
	uint32_t value = (uint32_t)(*at)[0] | (uint32_t)(*at)[1] << 8 | (uint32_t)(*at)[2] << 16 | (uint32_t)(*at)[3] << 24;

	*at += 4;
	return value;
}

/* the key as SHADER_KEY_WIRE_BYTES bytes */
static inline void shader_key_write(const struct nv2a_pixel_shader_key *key, unsigned char *out)
{
	unsigned char *at = out;
	unsigned int i;

	for (i = 0; i < D3DRS_PS_MAX; i++)
		shader_key_put32(&at, key->combiner_state[i]);
	shader_key_put32(&at, key->texture_modes);
	for (i = 0; i < 4; i++)
		*at++ = key->sampler_type[i];
	for (i = 0; i < 4; i++)
		*at++ = key->alpha_kill[i];
	for (i = 0; i < 4; i++)
		*at++ = key->color_sign[i];
	shader_key_put32(&at, (uint32_t)key->alpha_test_function);
	*at++ = key->fog_enable;
	*at++ = key->fog_table_mode;
	*at++ = key->count_samples;
	*at++ = key->coverage_alpha;
	*at++ = key->point_threshold;
	*at++ = key->per_pixel_lighting;
	*at++ = key->alpha_test_samples;
	*at++ = key->source_blend;
}

static inline void shader_key_read(const unsigned char *in, struct nv2a_pixel_shader_key *key)
{
	const unsigned char *at = in;
	unsigned int i;

	memset(key, 0, sizeof(*key));
	for (i = 0; i < D3DRS_PS_MAX; i++)
		key->combiner_state[i] = shader_key_get32(&at);
	key->texture_modes = shader_key_get32(&at);
	for (i = 0; i < 4; i++)
		key->sampler_type[i] = *at++;
	for (i = 0; i < 4; i++)
		key->alpha_kill[i] = *at++;
	for (i = 0; i < 4; i++)
		key->color_sign[i] = *at++;
	key->alpha_test_function = shader_key_get32(&at);
	key->fog_enable = *at++;
	key->fog_table_mode = *at++;
	key->count_samples = *at++;
	key->coverage_alpha = *at++;
	key->point_threshold = *at++;
	key->per_pixel_lighting = *at++;
	key->alpha_test_samples = *at++;
	key->source_blend = *at++;
}

/* FNV-1a 64 over bytes (the pack's keys: the same function on the console and on the computer) */
static inline uint64_t shader_key_hash(uint64_t hash, const unsigned char *bytes, size_t size)
{
	size_t i;

	for (i = 0; i < size; i++)
	{
		hash ^= bytes[i];
		hash *= 0x100000001b3ull;
	}
	return hash;
}

#define SHADER_KEY_HASH_INIT 0xcbf29ce484222325ull

/* a vertex shader's key: its program (the hash of the instruction count and words, each written as 4 bytes, least significant first) and the mask of
attributes fed as packed integers */
static inline uint64_t shader_vertex_program_hash(const uint32_t *words, unsigned long count)
{
	uint64_t hash = SHADER_KEY_HASH_INIT;
	unsigned char bytes[4];
	unsigned long i;
	unsigned char *at;

	at = bytes;
	shader_key_put32(&at, (uint32_t)count);
	hash = shader_key_hash(hash, bytes, 4);
	for (i = 0; i < count * 4; i++)
	{
		at = bytes;
		shader_key_put32(&at, words[i]);
		hash = shader_key_hash(hash, bytes, 4);
	}
	return hash;
}

static inline uint64_t shader_vertex_key(uint64_t program_hash, uint32_t packed_mask)
{
	unsigned char bytes[12];
	unsigned char *at = bytes;

	shader_key_put32(&at, (uint32_t)program_hash);
	shader_key_put32(&at, (uint32_t)(program_hash >> 32));
	shader_key_put32(&at, packed_mask);
	return shader_key_hash(SHADER_KEY_HASH_INIT, bytes, 12);
}

/* ---------- pixel keys the pack has no shader for (gx2_shaders.c dk_shader_for_draw; pack_gen.c writes the shaders these name) */

/* the texture stage modes (D3DRS_PSTEXTUREMODES, five bits a stage) that matter here, as nv2a_psh.c numbers them */
#define SHADER_KEY_MODE_NONE 0x00
#define SHADER_KEY_MODE_PROJECT2D 0x01
#define SHADER_KEY_MODE_PROJECT3D 0x02
#define SHADER_KEY_MODE_CUBEMAP 0x03
#define SHADER_KEY_MODE_PASSTHRU 0x04
#define SHADER_KEY_MODE_CLIPPLANE 0x05
#define SHADER_KEY_MODE_DOT_ZW 0x0a
#define SHADER_KEY_MODE_DOT_PRODUCT 0x11

/* a texture stage's mode as nv2a_psh.c's texture_stage takes it: a stage with no texture samples nothing */
static inline unsigned shader_key_stage_mode(const struct nv2a_pixel_shader_key *key, unsigned stage)
{
	const unsigned mode = (unsigned)(key->texture_modes >> (5 * stage)) & 0x1f;

	if (key->sampler_type[stage] == _xgpu_sampler_none && mode != SHADER_KEY_MODE_PASSTHRU && mode != SHADER_KEY_MODE_CLIPPLANE &&
		mode != SHADER_KEY_MODE_DOT_PRODUCT && mode != SHADER_KEY_MODE_DOT_ZW)
		return SHADER_KEY_MODE_NONE;
	return mode;
}

/* The stand-in for a pixel key the pack has no shader for (the fallback). What the key's final color and alpha depend on is followed
through its combiner program (shader_key_sources), and the stand-in keeps the part of that it can: stage 0's texture times the diffuse color, each
where the key's color or alpha depends on them; with no texture, the diffuse color, else C0, else nothing (a fade or a flash the stand-in cannot
follow is better transparent than a black or white screen); with the key's alpha test and stage 0's alpha kill, and nothing more (no other stage, no
fog). Models, the first-person weapon, the sky and the HUD
are drawn so, roughly, rather than not at all, until the key, written down as before (shader-misses-*.txt), has a shader of its own in the next pack.
Each stand-in is a key like any other, its shader written by the same generator (pack_gen.c writes all SHADER_KEY_FALLBACKS of them): its varyings,
uniforms and samplers are those of every pixel shader, so it meets every vertex shader.

(Blood Gulch's sky, 2026-10-31: its color reads stage 1's constant only to blend toward it, and the texture's alpha blends it over the screen; a
stand-in of the texture times stage 0's constant, black there, in color and alpha drew nothing.) */
#define SHADER_KEY_FALLBACKS 114

/* what a stand-in samples (stage 0's sampler), and the sources of its color and alpha */
enum { SHADER_KEY_FALLBACK_UNTEXTURED, SHADER_KEY_FALLBACK_2D, SHADER_KEY_FALLBACK_3D, SHADER_KEY_FALLBACK_CUBE };
enum { SHADER_KEY_FALLBACK_DIFFUSE, SHADER_KEY_FALLBACK_CONSTANT, SHADER_KEY_FALLBACK_ONE, SHADER_KEY_FALLBACK_ZERO };

/* what a pixel key's final color and alpha depend on (shader_key_sources) */
#define SHADER_KEY_FROM_T0 1u /* stage 0's texel, when it samples a texture */
#define SHADER_KEY_FROM_V0 2u /* the diffuse color's red, green or blue */
#define SHADER_KEY_FROM_V0_ALPHA 4u
#define SHADER_KEY_FROM_C0 8u /* a C0 constant's color */
#define SHADER_KEY_FROM_C0_ALPHA 16u
#define SHADER_KEY_FROM_OTHER 32u /* the other stages, V1, C1, the fog, the final combiner's constants */

/* the sources a combiner input reads, from[register][0 color, 1 alpha]: a register general combiner stages have not (6, 7, 14 and 15) is zero there;
an input of the alpha portion without the alpha's bit reads the blue, a color's */
static inline unsigned shader_key_input_sources(unsigned input, unsigned from[16][2])
{
	const unsigned reg = input & 0x0f;

	if (reg == 6 || reg == 7 || reg >= 14)
		return 0;
	return from[reg][(input & 0x10) ? 1 : 0];
}

/* and of the final combiner, whose C0 and C1 are constants of its own, and which has V1 plus R0 (14) and E times F (15), whose alpha is 0 */
static inline unsigned shader_key_final_sources(unsigned input, uint32_t efg, unsigned from[16][2])
{
	const unsigned reg = input & 0x0f;

	if (reg == 1 || reg == 2)
		return SHADER_KEY_FROM_OTHER;
	if (reg == 14)
		return (input & 0x10) ? 0 : from[5][0] | from[12][0];
	if (reg == 15)
		return (input & 0x10) ? 0 : shader_key_input_sources((efg >> 24) & 0xff, from) | shader_key_input_sources((efg >> 16) & 0xff, from);
	return shader_key_input_sources(input, from);
}

/* What a key's final color and alpha depend on (SHADER_KEY_FROM_*), following its program as nv2a_psh.c writes it: the stages' texels (stage 0's
counted apart when it samples a texture, a stage that samples nothing zero), R0's alpha first stage 0's, each general combiner stage the count runs
writing its products and sums, the mux reading R0's alpha, then the final combiner, or R0 when there is none. */
static inline void shader_key_sources(const struct nv2a_pixel_shader_key *key, unsigned *color, unsigned *alpha)
{
	const DWORD *state = key->combiner_state;
	const uint32_t abcd = state[D3DRS_PSFINALCOMBINERINPUTSABCD], efg = state[D3DRS_PSFINALCOMBINERINPUTSEFG];
	unsigned from[16][2], next[16][2], count = state[D3DRS_PSCOMBINERCOUNT] & 0xff, stage, portion, i;

	memset(from, 0, sizeof(from));
	from[1][0] = SHADER_KEY_FROM_C0;
	from[1][1] = SHADER_KEY_FROM_C0_ALPHA;
	from[2][0] = from[2][1] = from[3][0] = from[3][1] = SHADER_KEY_FROM_OTHER;
	from[4][0] = SHADER_KEY_FROM_V0;
	from[4][1] = SHADER_KEY_FROM_V0_ALPHA;
	from[5][0] = from[5][1] = SHADER_KEY_FROM_OTHER;
	for (stage = 0; stage < 4; stage++)
	{
		const unsigned mode = shader_key_stage_mode(key, stage);

		if (mode != SHADER_KEY_MODE_NONE)
			from[8 + stage][0] = from[8 + stage][1] = stage == 0 && (mode == SHADER_KEY_MODE_PROJECT2D || mode == SHADER_KEY_MODE_PROJECT3D ||
				mode == SHADER_KEY_MODE_CUBEMAP) ? SHADER_KEY_FROM_T0 : SHADER_KEY_FROM_OTHER;
	}
	from[12][1] = from[8][1];
	if (count > 8)
		count = 8;
	for (stage = 0; stage < count; stage++)
	{
		memcpy(next, from, sizeof(from));
		/* (both portions read before either writes; the color's writes first, the alpha's after, as the generator orders them) */
		for (portion = 0; portion < 2; portion++)
		{
			const uint32_t inputs = state[(portion ? D3DRS_PSALPHAINPUTS0 : D3DRS_PSRGBINPUTS0) + stage];
			const uint32_t outputs = state[(portion ? D3DRS_PSALPHAOUTPUTS0 : D3DRS_PSRGBOUTPUTS0) + stage];
			const unsigned flags = outputs >> 12;
			const unsigned ab = shader_key_input_sources((inputs >> 24) & 0xff, from) | shader_key_input_sources((inputs >> 16) & 0xff, from);
			const unsigned cd = shader_key_input_sources((inputs >> 8) & 0xff, from) | shader_key_input_sources(inputs & 0xff, from);
			const unsigned sum = ab | cd | ((flags & 0x04) ? from[12][1] : 0);
			const unsigned ab_register = (outputs >> 4) & 0x0f, cd_register = outputs & 0x0f, sum_register = (outputs >> 8) & 0x0f;
			const unsigned written[3] = { ab_register, cd_register, sum_register }, value[3] = { ab, cd, sum };

			for (i = 0; i < 3; i++)
			{
				const unsigned reg = written[i];

				if (reg != 4 && reg != 5 && (reg < 8 || reg > 13))
					continue;
				next[reg][portion] = value[i];
				/* (the color portion's AB or CD may write its blue to the register's alpha as well) */
				if (!portion && ((i == 0 && (flags & 0x80)) || (i == 1 && (flags & 0x40))))
					next[reg][1] = value[i];
			}
		}
		memcpy(from, next, sizeof(from));
	}
	if (!abcd && !efg)
	{
		*color = from[12][0];
		*alpha = from[12][1];
		return;
	}
	*color = 0;
	for (i = 0; i < 4; i++)
		*color |= shader_key_final_sources((abcd >> (8 * i)) & 0xff, efg, from);
	*alpha = shader_key_final_sources((efg >> 8) & 0xff, efg, from);
}

/* the stand-in of a kind: textured, stage 0's texture times color (the diffuse color or one) in color, and in alpha the texture's alpha
(texture_alpha) times alpha (the diffuse color's or one); untextured, color in color and alpha in alpha (the diffuse color's, C0's, one or zero) */
static inline void shader_key_fallback_make(unsigned kind, unsigned color, int texture_alpha, unsigned alpha, int alpha_test, int alpha_kill,
	struct nv2a_pixel_shader_key *out)
{
	/* combiner inputs: a register (zero 0, C0 1, V0 4, T0 8), 0x10 for its alpha, 0x20 inverted (zero inverted is one) */
	static const unsigned char color_input[4] = { 0x04, 0x01, 0x20, 0x20 }, alpha_input[4] = { 0x14, 0x11, 0x20, 0x20 };
	static const unsigned char modes[4] = { SHADER_KEY_MODE_NONE, SHADER_KEY_MODE_PROJECT2D, SHADER_KEY_MODE_PROJECT3D, SHADER_KEY_MODE_CUBEMAP };
	static const unsigned char samplers[4] = { _xgpu_sampler_none, _xgpu_sampler_2d, _xgpu_sampler_3d, _xgpu_sampler_cube };

	kind &= 3;
	color &= 3;
	alpha &= 3;
	memset(out, 0, sizeof(*out));
	out->texture_modes = modes[kind];
	out->sampler_type[0] = samplers[kind];
	/* one general combiner stage, r0 = A * B in both portions; no final combiner, so the result is r0 */
	out->combiner_state[D3DRS_PSCOMBINERCOUNT] = 1;
	out->combiner_state[D3DRS_PSRGBINPUTS0] = (uint32_t)(kind ? 0x08 : color == SHADER_KEY_FALLBACK_ZERO ? 0x00 : 0x20) << 24 |
		(uint32_t)color_input[color] << 16;
	out->combiner_state[D3DRS_PSALPHAINPUTS0] = (uint32_t)(kind && texture_alpha ? 0x18 : alpha == SHADER_KEY_FALLBACK_ZERO ? 0x00 : 0x20) << 24 |
		(uint32_t)alpha_input[alpha] << 16;
	out->combiner_state[D3DRS_PSRGBOUTPUTS0] = 0xc0;
	out->combiner_state[D3DRS_PSALPHAOUTPUTS0] = 0xc0;
	out->alpha_kill[0] = kind && alpha_kill;
	out->alpha_test_function = alpha_test ? D3DCMP_GREATER : 0;
}

/* the stand-in numbered index, 0 to SHADER_KEY_FALLBACKS - 1: 32 for each textured kind (the color the diffuse color or one, the texture's alpha or
not, the alpha the diffuse color's or one, the alpha test, the alpha kill), then 18 untextured (the color and the alpha each the diffuse color's,
C0's or zero, the alpha test) */
static inline void shader_key_fallback_numbered(unsigned index, struct nv2a_pixel_shader_key *out)
{
	static const unsigned char untextured[3] = { SHADER_KEY_FALLBACK_DIFFUSE, SHADER_KEY_FALLBACK_CONSTANT, SHADER_KEY_FALLBACK_ZERO };

	if (index < 96)
		shader_key_fallback_make(1 + index / 32, (index & 1) ? SHADER_KEY_FALLBACK_ONE : SHADER_KEY_FALLBACK_DIFFUSE, !((index >> 1) & 1),
			((index >> 2) & 1) ? SHADER_KEY_FALLBACK_ONE : SHADER_KEY_FALLBACK_DIFFUSE, (index >> 3) & 1, (index >> 4) & 1, out);
	else
		shader_key_fallback_make(SHADER_KEY_FALLBACK_UNTEXTURED, untextured[(index - 96) % 3], 0, untextured[(index - 96) / 3 % 3], (index - 96) / 9 % 2, 0,
			out);
}

/* the stand-in for a key; 0 for none (an alpha test of NEVER: its draws draw nothing) */
static inline int shader_key_fallback(const struct nv2a_pixel_shader_key *key, struct nv2a_pixel_shader_key *out)
{
	const unsigned mode = shader_key_stage_mode(key, 0);
	const int alpha_test = key->alpha_test_function == D3DCMP_GREATER || key->alpha_test_function == D3DCMP_GREATEREQUAL;
	unsigned color, alpha;

	if (key->alpha_test_function == D3DCMP_NEVER)
		return 0;
	shader_key_sources(key, &color, &alpha);
	if (mode == SHADER_KEY_MODE_PROJECT2D || mode == SHADER_KEY_MODE_PROJECT3D || mode == SHADER_KEY_MODE_CUBEMAP)
		shader_key_fallback_make(key->sampler_type[0] == _xgpu_sampler_cube ? SHADER_KEY_FALLBACK_CUBE : key->sampler_type[0] == _xgpu_sampler_3d ?
			SHADER_KEY_FALLBACK_3D : SHADER_KEY_FALLBACK_2D, (color & SHADER_KEY_FROM_V0) ? SHADER_KEY_FALLBACK_DIFFUSE : SHADER_KEY_FALLBACK_ONE,
			(alpha & SHADER_KEY_FROM_T0) != 0, (alpha & (SHADER_KEY_FROM_V0 | SHADER_KEY_FROM_V0_ALPHA)) ? SHADER_KEY_FALLBACK_DIFFUSE :
			SHADER_KEY_FALLBACK_ONE, alpha_test, key->alpha_kill[0] != 0, out);
	else
		shader_key_fallback_make(SHADER_KEY_FALLBACK_UNTEXTURED, (color & SHADER_KEY_FROM_V0) ? SHADER_KEY_FALLBACK_DIFFUSE : (color &
			SHADER_KEY_FROM_C0) ? SHADER_KEY_FALLBACK_CONSTANT : SHADER_KEY_FALLBACK_ZERO, 0, (alpha & (SHADER_KEY_FROM_V0 | SHADER_KEY_FROM_V0_ALPHA)) ?
			SHADER_KEY_FALLBACK_DIFFUSE : (alpha & (SHADER_KEY_FROM_C0 | SHADER_KEY_FROM_C0_ALPHA)) ? SHADER_KEY_FALLBACK_CONSTANT : SHADER_KEY_FALLBACK_ZERO,
			alpha_test, 0, out);
	return 1;
}

#endif
