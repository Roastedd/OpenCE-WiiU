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

#endif
