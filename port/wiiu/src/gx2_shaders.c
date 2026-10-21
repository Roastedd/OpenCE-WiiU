/*
GX2_SHADERS.C

The shader side of the GX2 renderer, as far as it is written: nothing is ready, so no draw is made (d3d8_gx2.c skips a draw whose shader is
not ready, as the Switch's does while a shader compiles). The Wii U's shaders are compiled ahead of time on a computer (the GLSL the Linux
port's NV2A translators write, through CafeGLSL: tools/wiiu/gx2_probe_shaders.sh), not on the console, where CafeGLSL's first compile ends in a
null read (WIIU_PORT_PLAN.md, the GX2 probe).
*/

#include "dk_shaders.h"
#include "xgpu.h"

#include <stddef.h>

/* FNV-1a 64, with which the vertex programs' and keys' hashes are made (dk_shaders.h) */
uint64_t dk_shader_hash_init(void)
{
	return 0xcbf29ce484222325ull;
}

uint64_t dk_shader_hash_mix(uint64_t hash, const void *data, unsigned long size)
{
	const unsigned char *bytes = (const unsigned char *)data;
	unsigned long i;

	for (i = 0; i < size; i++)
	{
		hash ^= bytes[i];
		hash *= 0x100000001b3ull;
	}
	return hash;
}

void dk_shader_start(void)
{
}

void dk_shader_frame(void)
{
}

void dk_shader_map_loaded(uint32_t map_hash)
{
	(void)map_hash;
}

uint32_t dk_shader_for_draw(uint32_t stage, const void *key_data)
{
	(void)stage;
	(void)key_data;
	return 0;
}

char *nv2a_dk_vertex_shader_to_glsl(const uint32_t *instructions, unsigned long instruction_count, unsigned long packed_attribute_mask)
{
	(void)instructions;
	(void)instruction_count;
	(void)packed_attribute_mask;
	return NULL;
}

char *nv2a_dk_pixel_shader_to_glsl(const struct nv2a_pixel_shader_key *key)
{
	(void)key;
	return NULL;
}
