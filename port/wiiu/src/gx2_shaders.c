/*
GX2_SHADERS.C

The shader side of the GX2 renderer, the game's half. The Wii U's shaders are compiled ahead of time, on a computer (tools/wiiu/shader_pack.py: the GLSL
that OpenCE's NV2A translators write, through CafeGLSL's command-line compiler, into one pack file), because CafeGLSL's compile on the console ends in a null
read (WIIU_PORT_PLAN.md, the GX2 probe). The pack is sd:/wiiu/apps/halo/shaders.pack:

	"HSP1", the number of shaders (n), 0, 0, then n entries of six big-endian 32-bit words: stage (0 vertex, 1 pixel), the key's hash (high word, low word),
	the shader's offset in the file, its size, 0; sorted by (stage, hash); then the shaders, GFD files as CafeGLSL writes them.

A draw whose shader is not in the pack is skipped. Its owned key is queued once for the main log worker, which writes complete
shader-misses-*.txt batches for shader_pack.py; .part files are incomplete and must not be harvested. A vertex shader is asked for by its program's hash and the mask of its packed attributes, a pixel shader by its key's bytes
(shader_key_wire.h), the same on the console and on the computer.
*/

#include "dk_shaders.h"
#include "xgpu.h"
#include "shader_key_wire.h"
#include "wiiu_missing_keys.h"
#include "halo_startup.h"

void wiiu_thread_off_game_core(void);

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

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

/* the pack's file read whole (2.7 MB at the time of writing, which the card gives in a fifth of a second): the buffer, or NULL (said in the log). It is read
by the GX2 thread when the first draw asks for a shader, or before that by a thread of its own (dk_shader_pack_prefetch). */
static unsigned char *pack_read(uint32_t *size_out)
{
	off_t length;
	size_t done = 0;
	unsigned char *buffer;
	int file;
	STARTUP_TIMER(started);

	/* (open and read, not fopen and fread: the C library's stdio reads a file through its own small buffer unless it is asked for a lot at once, and the
	card gives most when it is asked for a lot at once) */
	file = open(PACK_FILE, O_RDONLY);
	if (file < 0)
	{
		platform_log("shaders: no pack at %s: no draw will be made", PACK_FILE);
		return NULL;
	}
	length = lseek(file, 0, SEEK_END);
	if (length < 16 || lseek(file, 0, SEEK_SET) != 0)
	{
		close(file);
		return NULL;
	}
	buffer = malloc((size_t)length);
	while (buffer && done < (size_t)length)
	{
		const ssize_t got = read(file, buffer + done, (size_t)length - done);

		if (got <= 0)
			break;
		done += (size_t)got;
	}
	close(file);
	if (!buffer || done != (size_t)length)
	{
		free(buffer);
		platform_log("shaders: the pack could not be read");
		return NULL;
	}
	STARTUP_ADD("shader_pack_read", started, 1, done);
	*size_out = (uint32_t)done;
	return buffer;
}

/* the pack read ahead (halo/startup.txt bit 8): 0 not started, 1 being read, 2 read (or failed: prefetched is NULL) */
static pthread_mutex_t prefetch_lock;
static pthread_cond_t prefetch_done;
static volatile int prefetch_state;
static unsigned char *prefetched;
static uint32_t prefetched_size;

static void *prefetch_main(void *unused)
{
	uint32_t size = 0;
	unsigned char *buffer;

	(void)unused;
	wiiu_thread_off_game_core();
	buffer = pack_read(&size);
	pthread_mutex_lock(&prefetch_lock);
	prefetched = buffer;
	prefetched_size = size;
	prefetch_state = 2;
	pthread_cond_broadcast(&prefetch_done);
	pthread_mutex_unlock(&prefetch_lock);
	STARTUP_NOTE("shader_pack_ready");
	return NULL;
}

/* (from the game's main, once its memory is set up) the pack read by a thread of its own, where the GX2 thread would read it at the first draw */
void dk_shader_pack_prefetch(void);

void dk_shader_pack_prefetch(void)
{
	pthread_attr_t attributes;
	pthread_t thread;

	if (prefetch_state || !STARTUP_OPTION(STARTUP_PREFETCH_SHADER_PACK))
		return;
	if (pthread_mutex_init(&prefetch_lock, NULL) != 0 || pthread_cond_init(&prefetch_done, NULL) != 0)
		return;
	prefetch_state = 1;
	pthread_attr_init(&attributes);
	pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
	pthread_attr_setstacksize(&attributes, 128 * 1024);
	if (pthread_create(&thread, &attributes, prefetch_main, NULL) != 0)
		prefetch_state = 0; /* (the GX2 thread reads it itself) */
	pthread_attr_destroy(&attributes);
}

static void pack_load(void)
{
	STARTUP_TIMER(started);
	uint32_t size = 0;

	if (pack_tried)
		return;
	pack_tried = 1;
	if (prefetch_state)
	{
		pthread_mutex_lock(&prefetch_lock);
		while (prefetch_state != 2)
			pthread_cond_wait(&prefetch_done, &prefetch_lock);
		pack = prefetched;
		size = prefetched_size;
		prefetched = NULL;
		pthread_mutex_unlock(&prefetch_lock);
		STARTUP_ADD("shader_pack_wait", started, 1, size);
	}
	else
	{
		pack = pack_read(&size);
	}
	if (!pack)
		return;
	pack_size = size;
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

static void missing_note(uint32_t stage, uint64_t hash, const void *key_data)
{
	unsigned i;

	/* The GX2 command consumer is the only producer, including fallback mode. Guard the seen table too. */
	if (!wiiu_missing_keys_observe())
		return;
	for (i = 0; i < seen_count; i++)
		if (seen[i].hash == hash && seen[i].stage == stage)
		{
			wiiu_missing_keys_observed();
			return;
		}
	if (seen_count == SEEN_LIMIT)
	{
		wiiu_missing_keys_unavailable();
		wiiu_missing_keys_observed();
		return;
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
}

/* the keys of the last draws, with what they found: a draw of the same state (nearly all of them) is a compare of the key, not a hash of its 256 bytes and a search of the
pack */
#define MEMO_SIZE 128

static struct
{
	struct nv2a_pixel_shader_key key;
	uint32_t handle;
	int used;
} pixel_memo[MEMO_SIZE];

static uint32_t lookup(uint32_t stage, uint64_t hash, const void *key_data)
{
	uint32_t low = (uint32_t)hash, high = (uint32_t)(hash >> 32);
	uint32_t first = 0, last;
	const unsigned char *entries;

	pack_load();
	entries = pack ? pack + 16 : NULL;
	last = pack ? pack_count : 0;
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

/* the handle of the shader (1 on, 0: none, skip the draw) */
uint32_t dk_shader_for_draw(uint32_t stage, const void *key_data)
{
	if (stage == DK_SHADER_STAGE_VERTEX)
	{
		const struct dk_vertex_key *key = (const struct dk_vertex_key *)key_data;

		return lookup(stage, shader_vertex_key(key->program_hash, key->packed_mask), key_data);
	}
	else
	{
		const struct nv2a_pixel_shader_key *key = (const struct nv2a_pixel_shader_key *)key_data;
		const uint32_t *words = (const uint32_t *)key_data;
		uint32_t mix = 2166136261u, slot, i;
		unsigned char wire[SHADER_KEY_WIRE_BYTES];
		uint32_t handle;

		for (i = 0; i < sizeof(*key) / 4; i++)
			mix = (mix ^ words[i]) * 16777619u;
		slot = mix % MEMO_SIZE;
		if (pixel_memo[slot].used && !memcmp(&pixel_memo[slot].key, key, sizeof(*key)))
			return pixel_memo[slot].handle;
		shader_key_write(key, wire);
		handle = lookup(stage, shader_key_hash(SHADER_KEY_HASH_INIT, wire, sizeof(wire)), key_data);
		if (handle)
		{
			/* (not a miss: it is looked for, and written down, each time) */
			memcpy(&pixel_memo[slot].key, key, sizeof(*key));
			pixel_memo[slot].handle = handle;
			pixel_memo[slot].used = 1;
		}
		return handle;
	}
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
