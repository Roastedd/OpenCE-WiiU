/*
GX2_SHADERS.C

The shader side of the GX2 renderer, the game's half. The Wii U's shaders are compiled ahead of time, on a computer (tools/wiiu/shader_pack.py: the GLSL
that OpenCE's NV2A translators write, through CafeGLSL's command-line compiler, into one pack file), because CafeGLSL's compile on the console ends in a null
read (WIIU_PORT_PLAN.md, the GX2 probe). The pack is sd:/wiiu/apps/halo/shaders.pack:

	"HSP1", the number of shaders (n), 0, 0, then n entries of six big-endian 32-bit words: stage (0 vertex, 1 pixel), the key's hash (high word, low word),
	the shader's offset in the file, its size, 0; sorted by (stage, hash); then the shaders, GFD files as CafeGLSL writes them.

A draw whose shader is not in the pack is skipped, and the key is written to shaders_missing.txt, once, for the next pack: the file is the input of
shader_pack.py. A vertex shader is asked for by its program's hash and the mask of its packed attributes, a pixel shader by its key's bytes
(shader_key_wire.h), the same on the console and on the computer.
*/

#include "dk_shaders.h"
#include "xgpu.h"
#include "shader_key_wire.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PACK_FILE "/vol/external01/wiiu/apps/halo/shaders.pack"
#define MISSING_FILE "/vol/external01/wiiu/apps/halo/shaders_missing.txt"
#define PROGRAM_LIMIT 512
#define SEEN_LIMIT 4096

struct pack_entry
{
	uint32_t stage, hash_high, hash_low, offset, size, reserved;
};

static unsigned char *pack;
static uint32_t pack_size;
static uint32_t pack_count;
static int pack_tried;

static struct
{
	uint64_t hash;
	uint32_t *words;
	unsigned long count;
} programs[PROGRAM_LIMIT];
static unsigned program_count;

static uint64_t seen[SEEN_LIMIT];
static unsigned seen_count;
static unsigned long misses;

static uint32_t big32(const unsigned char *at)
{
	return (uint32_t)at[0] << 24 | (uint32_t)at[1] << 16 | (uint32_t)at[2] << 8 | (uint32_t)at[3];
}

static void pack_load(void)
{
	FILE *file;
	long size;

	if (pack_tried)
		return;
	pack_tried = 1;
	file = fopen(PACK_FILE, "rb");
	if (!file)
	{
		platform_log("shaders: no pack at %s: no draw will be made", PACK_FILE);
		return;
	}
	if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 16 || fseek(file, 0, SEEK_SET) != 0)
	{
		fclose(file);
		return;
	}
	pack = malloc((size_t)size);
	if (!pack || fread(pack, 1, (size_t)size, file) != (size_t)size)
	{
		free(pack);
		pack = NULL;
		fclose(file);
		platform_log("shaders: the pack could not be read");
		return;
	}
	fclose(file);
	pack_size = (uint32_t)size;
	if (memcmp(pack, "HSP1", 4) != 0 || 16 + (uint64_t)big32(pack + 4) * 24 > pack_size)
	{
		platform_log("shaders: %s is not a shader pack", PACK_FILE);
		free(pack);
		pack = NULL;
		return;
	}
	pack_count = big32(pack + 4);
	platform_log("shaders: the pack has %u shaders (%u bytes)", (unsigned)pack_count, (unsigned)pack_size);
}

/* the programs the game has made, kept by hash, so that a vertex shader the pack lacks can be written out whole */
void dk_shader_program_note(uint64_t program_hash, const uint32_t *words, unsigned long count);

void dk_shader_program_note(uint64_t program_hash, const uint32_t *words, unsigned long count)
{
	unsigned i;

	for (i = 0; i < program_count; i++)
		if (programs[i].hash == program_hash)
			return;
	if (program_count == PROGRAM_LIMIT)
		return;
	programs[program_count].words = malloc(count * 4 * sizeof(uint32_t));
	if (!programs[program_count].words)
		return;
	memcpy(programs[program_count].words, words, count * 4 * sizeof(uint32_t));
	programs[program_count].hash = program_hash;
	programs[program_count].count = count;
	program_count++;
}

static void missing_note(uint32_t stage, uint64_t hash, const void *key_data)
{
	unsigned i;
	FILE *file;

	for (i = 0; i < seen_count; i++)
		if (seen[i] == (hash ^ stage))
			return;
	if (seen_count == SEEN_LIMIT)
		return;
	seen[seen_count++] = hash ^ stage;
	file = fopen(MISSING_FILE, "a");
	if (!file)
		return;
	if (stage == DK_SHADER_STAGE_VERTEX)
	{
		const struct dk_vertex_key *key = (const struct dk_vertex_key *)key_data;

		for (i = 0; i < program_count; i++)
			if (programs[i].hash == key->program_hash)
				break;
		if (i < program_count)
		{
			unsigned long word;

			fprintf(file, "V %08x%08x %u %lu", (unsigned)(key->program_hash >> 32), (unsigned)key->program_hash, (unsigned)key->packed_mask,
				programs[i].count);
			for (word = 0; word < programs[i].count * 4; word++)
				fprintf(file, " %08x", (unsigned)programs[i].words[word]);
			fputc('\n', file);
		}
	}
	else
	{
		unsigned char wire[SHADER_KEY_WIRE_BYTES];
		unsigned byte;

		shader_key_write((const struct nv2a_pixel_shader_key *)key_data, wire);
		fputs("P ", file);
		for (byte = 0; byte < sizeof(wire); byte++)
			fprintf(file, "%02x", wire[byte]);
		fputc('\n', file);
	}
	fclose(file);
	misses++;
}

/* the handle of the shader (1 on, 0: none, skip the draw) */
uint32_t dk_shader_for_draw(uint32_t stage, const void *key_data)
{
	uint64_t hash;
	uint32_t low, high, count;
	uint32_t first = 0, last;
	const unsigned char *entries;

	if (stage == DK_SHADER_STAGE_VERTEX)
	{
		const struct dk_vertex_key *key = (const struct dk_vertex_key *)key_data;

		hash = shader_vertex_key(key->program_hash, key->packed_mask);
	}
	else
	{
		unsigned char wire[SHADER_KEY_WIRE_BYTES];

		shader_key_write((const struct nv2a_pixel_shader_key *)key_data, wire);
		hash = shader_key_hash(SHADER_KEY_HASH_INIT, wire, sizeof(wire));
	}
	pack_load();
	high = (uint32_t)(hash >> 32);
	low = (uint32_t)hash;
	count = pack ? pack_count : 0;
	entries = pack ? pack + 16 : NULL;
	last = count;
	while (first < last)
	{
		uint32_t middle = (first + last) / 2;
		const unsigned char *entry = entries + middle * 24;
		uint32_t entry_stage = big32(entry), entry_high = big32(entry + 4), entry_low = big32(entry + 8);

		if (entry_stage == stage && entry_high == high && entry_low == low)
			return middle + 1;
		if (entry_stage < stage || (entry_stage == stage && (entry_high < high || (entry_high == high && entry_low < low))))
			first = middle + 1;
		else
			last = middle;
	}
	missing_note(stage, hash, key_data);
	return 0;
}

/* a shader's GFD file, for the backend: where it is in the pack, its size and its stage (NULL for a handle that is none) */
const void *dk_shader_blob(uint32_t handle, uint32_t *size, uint32_t *stage);

const void *dk_shader_blob(uint32_t handle, uint32_t *size, uint32_t *stage)
{
	const unsigned char *entry;

	if (!pack || !handle || handle > pack_count)
		return NULL;
	entry = pack + 16 + (handle - 1) * 24;
	*stage = big32(entry);
	*size = big32(entry + 16);
	return pack + big32(entry + 12);
}

/* FNV-1a 64, with which the key hashes were made before the pack (dk_shaders.h): kept for the callers that mix other things in */
uint64_t dk_shader_hash_init(void)
{
	return SHADER_KEY_HASH_INIT;
}

uint64_t dk_shader_hash_mix(uint64_t hash, const void *data, unsigned long size)
{
	return shader_key_hash(hash, (const unsigned char *)data, size);
}

void dk_shader_start(void)
{
}

void dk_shader_frame(void)
{
}

/* wiiu_gx2.c: the map's data, which the backend may have kept copies of, is another map's now */
void host_gx2_map_changed(void);

void dk_shader_map_loaded(uint32_t map_hash)
{
	(void)map_hash;
	host_gx2_map_changed();
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
