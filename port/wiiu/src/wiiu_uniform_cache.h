/* Producer-owned uniform conversion cache. The 76 input words are captured
   from live state at every draw; no setter serial or borrowed data is used.
   The caller still copies changed blocks into owned command payloads. */
#ifndef WIIU_UNIFORM_CACHE_H
#define WIIU_UNIFORM_CACHE_H

#include "dk_shaders.h"
#include <stdint.h>
#include <string.h>

#define WIIU_UNIFORM_INPUT_WORDS 76u
#define WIIU_UNIFORM_VERTEX_CHANGED 1u
#define WIIU_UNIFORM_PIXEL_CHANGED 2u

struct wiiu_uniform_cache
{
	uint32_t input[WIIU_UNIFORM_INPUT_WORDS];
	unsigned valid;
	struct dk_vertex_parameters vertex;
	struct dk_pixel_parameters pixel;
};

/* Zero-initialize the cache, including the unused std140 components. Clearing
   valid later forgets only the CPU input key, retaining owned converted data. */
#define WIIU_UNIFORM_INLINE static __inline__ __attribute__((always_inline))
#ifndef WIIU_UNIFORM_CACHE_TEST_TOUCH
#define WIIU_UNIFORM_CACHE_TEST_TOUCH(first) ((void)(first))
#endif

WIIU_UNIFORM_INLINE uint32_t wiiu_uniform_word(const void *address)
{
	uint32_t value;
	memcpy(&value, address, sizeof(value));
	return value;
}

WIIU_UNIFORM_INLINE unsigned wiiu_uniform_group(struct wiiu_uniform_cache *cache,
	const unsigned char *input, unsigned first, unsigned count)
{
	unsigned i;
	uint32_t different = !cache->valid;
	for (i = 0; i < count; i++)
		different |= cache->input[first + i] ^ wiiu_uniform_word(input + (first + i) * 4u);
	if (!different)
		return 0;
	for (i = 0; i < count; i++)
		cache->input[first + i] = wiiu_uniform_word(input + (first + i) * 4u);
	WIIU_UNIFORM_CACHE_TEST_TOUCH(first);
	return 1;
}

WIIU_UNIFORM_INLINE unsigned wiiu_uniform_store(void *destination, const void *source, unsigned count)
{
	unsigned i;
	uint32_t different = 0;
	for (i = 0; i < count; i++)
		different |= wiiu_uniform_word((const unsigned char *)destination + i * 4u) ^
			wiiu_uniform_word((const unsigned char *)source + i * 4u);
	if (different)
		memcpy(destination, source, count * 4u);
	return different != 0;
}

/* Exactly color_to_vec4's existing arithmetic, including division by 255. */
WIIU_UNIFORM_INLINE unsigned wiiu_uniform_color(float *destination, uint32_t color)
{
	float value[4];
	value[0] = ((color >> 16) & 0xff) / 255.0f;
	value[1] = ((color >> 8) & 0xff) / 255.0f;
	value[2] = (color & 0xff) / 255.0f;
	value[3] = ((color >> 24) & 0xff) / 255.0f;
	return wiiu_uniform_store(destination, value, 4);
}

WIIU_UNIFORM_INLINE unsigned wiiu_uniform_cache_update(struct wiiu_uniform_cache *cache, const void *words)
{
	const unsigned char *input = (const unsigned char *)words;
	unsigned changed = 0, stage;
	float value;
	if (wiiu_uniform_group(cache, input, 0, 4))
		changed |= wiiu_uniform_store(cache->vertex.viewport_scale, input, 4) * WIIU_UNIFORM_VERTEX_CHANGED;
	if (wiiu_uniform_group(cache, input, 4, 4))
		changed |= wiiu_uniform_store(cache->vertex.viewport_offset, input + 16, 4) * WIIU_UNIFORM_VERTEX_CHANGED;
	if (wiiu_uniform_group(cache, input, 8, 16))
		changed |= wiiu_uniform_store(cache->pixel.texture_scale, input + 32, 16) * WIIU_UNIFORM_PIXEL_CHANGED;
	if (wiiu_uniform_group(cache, input, 24, 1))
	{
		uint32_t size = cache->input[24] ? cache->input[24] : UINT32_C(0x3f800000);
		changed |= wiiu_uniform_store(&cache->vertex.point_and_screen[0], &size, 1) * WIIU_UNIFORM_VERTEX_CHANGED;
	}
	for (stage = 0; stage < 8; stage++)
	{
		if (wiiu_uniform_group(cache, input, 25 + stage * 2, 1))
			changed |= wiiu_uniform_color(cache->pixel.ps_c0[stage], cache->input[25 + stage * 2]) * WIIU_UNIFORM_PIXEL_CHANGED;
		if (wiiu_uniform_group(cache, input, 26 + stage * 2, 1))
			changed |= wiiu_uniform_color(cache->pixel.ps_c1[stage], cache->input[26 + stage * 2]) * WIIU_UNIFORM_PIXEL_CHANGED;
	}
	if (wiiu_uniform_group(cache, input, 41, 1))
		changed |= wiiu_uniform_color(cache->pixel.ps_final_c0, cache->input[41]) * WIIU_UNIFORM_PIXEL_CHANGED;
	if (wiiu_uniform_group(cache, input, 42, 1))
		changed |= wiiu_uniform_color(cache->pixel.ps_final_c1, cache->input[42]) * WIIU_UNIFORM_PIXEL_CHANGED;
	if (wiiu_uniform_group(cache, input, 43, 1))
		changed |= wiiu_uniform_color(cache->pixel.fog_color, cache->input[43]) * WIIU_UNIFORM_PIXEL_CHANGED;
	if (wiiu_uniform_group(cache, input, 44, 3))
		changed |= wiiu_uniform_store(cache->pixel.fog_parameters, input + 44 * 4u, 3) * WIIU_UNIFORM_PIXEL_CHANGED;
	if (wiiu_uniform_group(cache, input, 47, 1))
	{
		value = (float)(cache->input[47] & 0xff);
		changed |= wiiu_uniform_store(&cache->pixel.alpha_reference[0], &value, 1) * WIIU_UNIFORM_PIXEL_CHANGED;
	}
	if (wiiu_uniform_group(cache, input, 48, 1))
	{
		/* UI_OFFSET is a signed 32-bit GLint, captured as a DWORD above. */
		value = (float)(int32_t)cache->input[48];
		changed |= wiiu_uniform_store(&cache->vertex.point_and_screen[1], &value, 1) * WIIU_UNIFORM_VERTEX_CHANGED;
	}
	if (wiiu_uniform_group(cache, input, 49, 3))
		changed |= wiiu_uniform_store(&cache->pixel.alpha_reference[1], input + 49 * 4u, 3) * WIIU_UNIFORM_PIXEL_CHANGED;
	for (stage = 0; stage < 4; stage++)
	{
		if (wiiu_uniform_group(cache, input, 52 + stage * 6, 4))
			changed |= wiiu_uniform_store(cache->pixel.bump_matrix[stage], input + (52 + stage * 6) * 4u, 4) * WIIU_UNIFORM_PIXEL_CHANGED;
		if (wiiu_uniform_group(cache, input, 56 + stage * 6, 2))
			changed |= wiiu_uniform_store(cache->pixel.bump_luminance[stage], input + (56 + stage * 6) * 4u, 2) * WIIU_UNIFORM_PIXEL_CHANGED;
	}
	cache->valid = 1;
	return changed;
}

#undef WIIU_UNIFORM_INLINE
#endif
