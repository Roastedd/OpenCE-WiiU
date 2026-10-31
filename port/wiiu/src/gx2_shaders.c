/*
GX2_SHADERS.C

The shader side of the GX2 renderer, the game's half. The Wii U's shaders are compiled ahead of time, on a computer (tools/wiiu/shader_pack.py: the GLSL
that OpenCE's NV2A translators write, through CafeGLSL's command-line compiler, into one pack file), because CafeGLSL's compile on the console ends in a null
read (WIIU_PORT_PLAN.md, the GX2 probe). The pack is sd:/wiiu/apps/halo/shaders.pack:

	"HSP1", the number of entries (n), 0, 0, then n entries of six big-endian 32-bit words: stage, the key's hash (high word, low word), the shader's offset
	in the file, its size, 0; sorted by (stage, hash); then the shaders, GFD files as CafeGLSL writes them. Stage 0 is a vertex shader, 1 a pixel shader; 2 and
	3 mark a vertex and a pixel key left out of the pack on purpose (tools/wiiu/shader_exclude.txt: their shaders draw wrongly), with no shader (size 0).

A vertex shader is asked for by its program's hash and the mask of its packed attributes, a pixel shader by its key's bytes (shader_key_wire.h), the same on
the console and on the computer. A pixel key the pack lacks is asked for again as its stand-in, stage 0's texture times its color with its alpha test
(shader_key_fallback), which the pack has whatever the keys (port/wiiu/tools/shader_pack/pack_gen.c); the 5-second log counts the draws made so
(wiiu_gx2.c). A draw whose shader is not found so is skipped: its vertex shader is not in the pack, its key was left out, or the pack is older than the
stand-ins.

Every key the pack lacks as it is, but those left out, is queued once for the main log worker, which writes complete shader-misses-*.txt batches for
shader_pack.py; .part files are incomplete and must not be harvested.
*/

#include "dk_shaders.h"
#include "xgpu.h"
#include "shader_key_wire.h"
#include "wiiu_missing_keys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PACK_FILE "/vol/external01/wiiu/apps/halo/shaders.pack"
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

static struct { uint64_t hash; uint32_t stage; } seen[SEEN_LIMIT];
static unsigned seen_count;

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
	{
		uint32_t i, stages[4] = { 0, 0, 0, 0 };

		for (i = 0; i < pack_count; i++)
			stages[big32(pack + 16 + i * 24) & 3]++;
		platform_log("shaders: the pack has %u vertex and %u pixel shaders, %u keys left out (%u bytes)", (unsigned)stages[0], (unsigned)stages[1],
			(unsigned)(stages[2] + stages[3]), (unsigned)pack_size);
	}
}

/* the programs the game has made, kept by hash, so that a vertex shader the pack lacks can be written out whole */
void dk_shader_program_note(uint64_t program_hash, const uint32_t *words, unsigned long count);

void dk_shader_program_note(uint64_t program_hash, const uint32_t *words, unsigned long count)
{
	unsigned i, count_now = __atomic_load_n(&program_count, __ATOMIC_ACQUIRE);

	if (!words || !count || count > WIIU_MISSING_WORDS / 4u)
		return;
	for (i = 0; i < count_now; i++)
		if (programs[i].hash == program_hash)
			return;
	if (count_now == PROGRAM_LIMIT)
		return;
	programs[count_now].words = malloc(count * 4 * sizeof(uint32_t));
	if (!programs[count_now].words)
		return;
	memcpy(programs[count_now].words, words, count * 4 * sizeof(uint32_t));
	programs[count_now].hash = program_hash;
	programs[count_now].count = count;
	/* One game-side writer publishes immutable process-lifetime program data to the GX2 consumer. */
	__atomic_store_n(&program_count, count_now + 1u, __ATOMIC_RELEASE);
}

/* 1: the key is written down (queued, or found queued before, or there is no room for more: asking again changes nothing); 0: the queue took no key
now, so the next draw of it asks again */
static int missing_note(uint32_t stage, uint64_t hash, const void *key_data)
{
	unsigned i;

	/* The GX2 command consumer is the only producer, including fallback mode. Guard the seen table too. */
	if (!wiiu_missing_keys_observe())
		return 0;
	for (i = 0; i < seen_count; i++)
		if (seen[i].hash == hash && seen[i].stage == stage)
		{
			wiiu_missing_keys_observed();
			return 1;
		}
	if (seen_count == SEEN_LIMIT)
	{
		wiiu_missing_keys_unavailable();
		wiiu_missing_keys_observed();
		return 1;
	}
	/* One attempt per unique key, as before. Accepted and persisted counts remain separate; losses are explicit. */
	seen[seen_count].hash = hash;
	seen[seen_count++].stage = stage;
	if (stage == DK_SHADER_STAGE_VERTEX)
	{
		const struct dk_vertex_key *key = (const struct dk_vertex_key *)key_data;
		unsigned count_now = __atomic_load_n(&program_count, __ATOMIC_ACQUIRE);

		for (i = 0; i < count_now; i++)
			if (programs[i].hash == key->program_hash)
				break;
		if (i < count_now)
			wiiu_missing_keys_vertex((uint32_t)(hash >> 32), (uint32_t)hash,
				(uint32_t)(key->program_hash >> 32), (uint32_t)key->program_hash,
				key->packed_mask, (uint32_t)programs[i].count, programs[i].words);
		else
			wiiu_missing_keys_unavailable();
	}
	else
	{
		unsigned char wire[SHADER_KEY_WIRE_BYTES];

		shader_key_write((const struct nv2a_pixel_shader_key *)key_data, wire);
		wiiu_missing_keys_pixel((uint32_t)(hash >> 32), (uint32_t)hash, wire, sizeof(wire));
	}
	wiiu_missing_keys_observed();
	return 1;
}

/* the handle of the pack's entry for a stage and hash (its number plus 1), or 0 */
static uint32_t find(uint32_t stage, uint64_t hash)
{
	const uint32_t low = (uint32_t)hash, high = (uint32_t)(hash >> 32);
	uint32_t first = 0, last;

	pack_load();
	if (!pack)
		return 0;
	last = pack_count;
	while (first < last)
	{
		const uint32_t middle = (first + last) / 2;
		const unsigned char *entry = pack + 16 + middle * 24;
		const uint32_t entry_stage = big32(entry), entry_high = big32(entry + 4), entry_low = big32(entry + 8);

		if (entry_stage == stage && entry_high == high && entry_low == low)
			return middle + 1;
		if (entry_stage < stage || (entry_stage == stage && (entry_high < high || (entry_high == high && entry_low < low))))
			first = middle + 1;
		else
			last = middle;
	}
	return 0;
}

/* how the last key of each stage was found (DK_SHADER_FOUND_*, dk_shaders.h), and the lines that say which pixel keys were drawn with another's shader */
static uint32_t found[2];
static unsigned found_told;

#define FOUND_TOLD_LIMIT 40

/* a pixel key's shader: its own, else its stand-in's; none for a key left out of the pack. *shader_hash is the hash of the key whose shader it is */
static uint32_t pixel_find(const struct nv2a_pixel_shader_key *key, uint64_t hash, uint32_t *how, uint64_t *shader_hash)
{
	struct nv2a_pixel_shader_key other;
	unsigned char wire[SHADER_KEY_WIRE_BYTES];
	uint32_t handle = find(DK_SHADER_STAGE_PIXEL, hash);

	*shader_hash = hash;
	*how = DK_SHADER_FOUND_EXACT;
	if (handle)
		return handle;
	*how = DK_SHADER_FOUND_LEFT_OUT;
	if (find(DK_SHADER_STAGE_PIXEL + 2, hash))
		return 0;
	*how = DK_SHADER_FOUND_FALLBACK;
	if (shader_key_fallback(key, &other))
	{
		shader_key_write(&other, wire);
		*shader_hash = shader_key_hash(SHADER_KEY_HASH_INIT, wire, sizeof(wire));
		if ((handle = find(DK_SHADER_STAGE_PIXEL, *shader_hash)) != 0)
			return handle;
	}
	*shader_hash = hash;
	*how = DK_SHADER_FOUND_NONE;
	return 0;
}

/* the keys of the last draws, with what they found: a draw of the same state (nearly all of them) is a compare of the key, not a hash of its 256 bytes and a search of the
pack. A key drawn with another's shader stays here, its note retried until the queue takes it; a key with no shader at all does not (it is looked for, and
written down, each time, as before). */
#define MEMO_SIZE 128

static struct
{
	struct nv2a_pixel_shader_key key;
	uint64_t hash;
	uint32_t handle;
	unsigned char used, how, noted;
} pixel_memo[MEMO_SIZE];

/* the handle of the shader (1 on, 0: none, skip the draw); dk_shader_found says how it was found */
uint32_t dk_shader_for_draw(uint32_t stage, const void *key_data)
{
	if (stage == DK_SHADER_STAGE_VERTEX)
	{
		const struct dk_vertex_key *key = (const struct dk_vertex_key *)key_data;
		const uint64_t hash = shader_vertex_key(key->program_hash, key->packed_mask);
		const uint32_t handle = find(stage, hash);

		found[0] = DK_SHADER_FOUND_EXACT;
		if (handle)
			return handle;
		found[0] = DK_SHADER_FOUND_LEFT_OUT;
		if (!find(DK_SHADER_STAGE_VERTEX + 2, hash))
		{
			found[0] = DK_SHADER_FOUND_NONE;
			missing_note(stage, hash, key_data);
		}
		return 0;
	}
	else
	{
		const struct nv2a_pixel_shader_key *key = (const struct nv2a_pixel_shader_key *)key_data;
		const uint32_t *words = (const uint32_t *)key_data;
		uint32_t mix = 2166136261u, slot, i, handle, how;
		unsigned char wire[SHADER_KEY_WIRE_BYTES];
		uint64_t hash, shader_hash;
		int noted = 1;

		for (i = 0; i < sizeof(*key) / 4; i++)
			mix = (mix ^ words[i]) * 16777619u;
		slot = mix % MEMO_SIZE;
		if (pixel_memo[slot].used && !memcmp(&pixel_memo[slot].key, key, sizeof(*key)))
		{
			if (!pixel_memo[slot].noted)
				pixel_memo[slot].noted = (unsigned char)missing_note(stage, pixel_memo[slot].hash, key_data);
			found[1] = pixel_memo[slot].how;
			return pixel_memo[slot].handle;
		}
		shader_key_write(key, wire);
		hash = shader_key_hash(SHADER_KEY_HASH_INIT, wire, sizeof(wire));
		handle = pixel_find(key, hash, &how, &shader_hash);
		found[1] = how;
		if (how != DK_SHADER_FOUND_EXACT && how != DK_SHADER_FOUND_LEFT_OUT)
			noted = missing_note(stage, hash, key_data);
		if (how == DK_SHADER_FOUND_FALLBACK && found_told < FOUND_TOLD_LIMIT)
		{
			found_told++;
			platform_log("shaders: pixel key %08x%08x is not in the pack: drawn with its stand-in, %08x%08x%s", (unsigned)(hash >> 32), (unsigned)hash,
				(unsigned)(shader_hash >> 32), (unsigned)shader_hash, found_told == FOUND_TOLD_LIMIT ? " (no more of these lines)" : "");
		}
		if (how != DK_SHADER_FOUND_NONE)
		{
			memcpy(&pixel_memo[slot].key, key, sizeof(*key));
			pixel_memo[slot].hash = hash;
			pixel_memo[slot].handle = handle;
			pixel_memo[slot].how = (unsigned char)how;
			pixel_memo[slot].noted = (unsigned char)noted;
			pixel_memo[slot].used = 1;
		}
		return handle;
	}
}

/* how dk_shader_for_draw found the last shader it was asked for of a stage */
uint32_t dk_shader_found(uint32_t stage)
{
	return found[stage == DK_SHADER_STAGE_VERTEX ? 0 : 1];
}

/* a shader's GFD file, for the backend: where it is in the pack, its size and its stage (NULL for a handle that is none) */
const void *dk_shader_blob(uint32_t handle, uint32_t *size, uint32_t *stage);

const void *dk_shader_blob(uint32_t handle, uint32_t *size, uint32_t *stage)
{
	const unsigned char *entry;
	uint32_t offset, bytes;

	if (!pack || !handle || handle > pack_count)
		return NULL;
	entry = pack + 16 + (handle - 1) * 24;
	offset = big32(entry + 12);
	bytes = big32(entry + 16);
	/* (a mark of a key left out has no shader; nor has an entry that runs past the file) */
	if (big32(entry) > DK_SHADER_STAGE_PIXEL || !bytes || offset > pack_size || bytes > pack_size - offset)
		return NULL;
	*stage = big32(entry);
	*size = bytes;
	return pack + offset;
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

void halo_stream_endian_forget(void);

void dk_shader_map_loaded(uint32_t map_hash)
{
	(void)map_hash;
	host_gx2_map_changed();
	halo_stream_endian_forget();
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
