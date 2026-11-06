/*
GX2_SHADERS.C

The shader side of the GX2 renderer, the game's half. The Wii U's shaders are compiled ahead of time, on a computer (tools/wiiu/shader_pack.py: the GLSL
that OpenCE's NV2A translators write, through CafeGLSL's command-line compiler, into one pack file), because CafeGLSL's compile on the console ends in a null
read (WIIU_PORT_PLAN.md, the GX2 probe). The pack is sd:/wiiu/apps/halo/shaders.pack:

	"HSP1", the number of entries (n), 0, 0, then n entries of six big-endian 32-bit words: stage, the key's hash (high word, low word), the shader's offset
	in the file, its size, 0; sorted by (stage, hash); then the shaders, GFD files as CafeGLSL writes them. Stage 0 is a vertex shader, 1 a pixel shader; 2 and
	3 mark a vertex and a pixel key left out of the pack on purpose (tools/wiiu/shader_exclude.txt: their shaders draw wrongly), with no shader (size 0).

A vertex shader is asked for by its program's hash and the mask of its packed attributes, a pixel shader by its key's bytes (shader_key_wire.h), the same on
the console and on the computer. A pixel key the pack lacks is asked for again as its canonical key, which has the same shader (the generator does not read
what makes them differ), and then as its stand-in, stage 0's texture times its color with its alpha test (shader_key_fallback): the pack has both for every
key it was built from, and the stand-ins whatever the keys (port/wiiu/tools/shader_pack/pack_gen.c). The 5-second log counts the draws made each way
(wiiu_gx2.c). A draw whose shader is not found so is skipped: its vertex shader is not in the pack, its key was left out, or the pack is older than these.

Every key the pack lacks as it is, but those left out, is queued once for the main log worker, which writes complete shader-misses-*.txt batches for
shader_pack.py; .part files are incomplete and must not be harvested.
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

/* The pixel keys drawn with another key's shader this run, bounded, with the draws each made (the first 40 are told by name above, in the first minute, and the
pack's shader-misses capture can be full or lost: the owner's 2026-11-06 session lost the 41 keys of its stand-in draws). A key drawn with its stand-in (a shader
class the pack has none of) has its 256 bytes written out once, in four log lines `shaders: key P <hash> <n>/4 <hex>` that tools/wiiu/log_keys.py turns back into the
capture's format; each 5 seconds the keys that drew in the window are said, with their counts (nonexact_report, called by the 5-second note). */
#define NONEXACT_LIMIT 64
#define NONEXACT_DUMP_LIMIT 24
#define NONEXACT_NONE 0xffffffffu
#define NONEXACT_LINES_PER_NOTE 8
#define NONEXACT_PART_BYTES 64

static struct
{
	uint64_t hash, shader_hash;
	uint32_t how;
	unsigned long draws, window;
} nonexact[NONEXACT_LIMIT];
static unsigned nonexact_count, nonexact_dumped;
static unsigned long nonexact_full;
/* the entry of the last pixel key asked for (the draws after it are its), or none for a key with its own shader */
static uint32_t nonexact_current = NONEXACT_NONE;
/* halo/debug_standin.txt: a draw with a stand-in is made magenta (dk_shader_debug_standin) */
static int debug_standin;

static uint32_t nonexact_note(uint64_t hash, uint64_t shader_hash, uint32_t how, const unsigned char *wire)
{
	unsigned index;

	for (index = 0; index < nonexact_count; index++)
		if (nonexact[index].hash == hash)
			return index;
	if (nonexact_count == NONEXACT_LIMIT)
	{
		nonexact_full++;
		return NONEXACT_NONE;
	}
	nonexact[nonexact_count].hash = hash;
	nonexact[nonexact_count].shader_hash = shader_hash;
	nonexact[nonexact_count].how = how;
	if (how == DK_SHADER_FOUND_FALLBACK && nonexact_dumped < NONEXACT_DUMP_LIMIT)
	{
		static const char digits[] = "0123456789abcdef";
		const unsigned parts = (SHADER_KEY_WIRE_BYTES + NONEXACT_PART_BYTES - 1) / NONEXACT_PART_BYTES;
		unsigned part, at;

		nonexact_dumped++;
		for (part = 0; part < parts; part++)
		{
			char text[2 * NONEXACT_PART_BYTES + 1];
			unsigned used = 0;

			for (at = part * NONEXACT_PART_BYTES; at < (part + 1) * NONEXACT_PART_BYTES && at < SHADER_KEY_WIRE_BYTES; at++)
			{
				text[used++] = digits[wire[at] >> 4];
				text[used++] = digits[wire[at] & 15];
			}
			text[used] = 0;
			platform_log("shaders: key P %08x%08x %u/%u %s", (unsigned)(hash >> 32), (unsigned)hash, part + 1, parts, text);
		}
	}
	return nonexact_count++;
}

/* a draw was made with the last pixel key asked for (wiiu_gx2.c) */
void dk_shader_note_draw(void);

void dk_shader_note_draw(void)
{
	if (nonexact_current != NONEXACT_NONE)
	{
		nonexact[nonexact_current].draws++;
		nonexact[nonexact_current].window++;
	}
}

/* the 5-second note: the keys that drew with a stand-in in the window, and how many keys are known */
void dk_shader_nonexact_report(void);

void dk_shader_nonexact_report(void)
{
	unsigned index, told = 0, standins = 0;
	unsigned long window = 0;

	for (index = 0; index < nonexact_count; index++)
	{
		if (nonexact[index].how == DK_SHADER_FOUND_FALLBACK)
			standins++;
		if (!nonexact[index].window)
			continue;
		window += nonexact[index].window;
		if (nonexact[index].how == DK_SHADER_FOUND_FALLBACK && told < NONEXACT_LINES_PER_NOTE)
		{
			told++;
			platform_log("shaders: stand-in key %08x%08x drew %lu times in this window (%lu in all)", (unsigned)(nonexact[index].hash >> 32),
				(unsigned)nonexact[index].hash, nonexact[index].window, nonexact[index].draws);
		}
		nonexact[index].window = 0;
	}
	if (told || window)
		platform_log("shaders: %u keys have been drawn with another key's shader this run, %u of them with a stand-in (%u of those written out)%s", nonexact_count,
			standins, nonexact_dumped, nonexact_full ? " (the table is full)" : "");
}

/* halo/debug_standin.txt: 1 makes every draw with a stand-in magenta (the stand-in that has the color of a constant, with the constant magenta: wiiu_gx2.c), so that
a console picture shows where they are */
void dk_shader_debug_standin(int on);

void dk_shader_debug_standin(int on)
{
	debug_standin = on != 0;
}

/* the handle of the stand-in that draws a constant color (wiiu_gx2.c sets the constant), 0 if the pack has none */
static uint32_t tint_handle(void)
{
	struct nv2a_pixel_shader_key key;
	unsigned char wire[SHADER_KEY_WIRE_BYTES];

	/* (the untextured stand-in whose color and alpha are C0's, with no alpha test: index 96 + 1 + 3 * 1) */
	shader_key_fallback_numbered(SHADER_KEY_FALLBACK_TINT, &key);
	shader_key_write(&key, wire);
	return find(DK_SHADER_STAGE_PIXEL, shader_key_hash(SHADER_KEY_HASH_INIT, wire, sizeof(wire)));
}

/* a pixel key's shader: its own, else its canonical key's (the same shader), else its stand-in's; none for a key left out of the pack. *shader_hash
is the hash of the key whose shader it is */
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
	shader_key_canonical(key, &other);
	shader_key_write(&other, wire);
	*shader_hash = shader_key_hash(SHADER_KEY_HASH_INIT, wire, sizeof(wire));
	*how = DK_SHADER_FOUND_EQUIVALENT;
	if (*shader_hash != hash && (handle = find(DK_SHADER_STAGE_PIXEL, *shader_hash)) != 0)
		return handle;
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
	uint32_t handle, nonexact;
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
			nonexact_current = pixel_memo[slot].nonexact;
			return pixel_memo[slot].handle;
		}
		shader_key_write(key, wire);
		hash = shader_key_hash(SHADER_KEY_HASH_INIT, wire, sizeof(wire));
		handle = pixel_find(key, hash, &how, &shader_hash);
		found[1] = how;
		nonexact_current = how == DK_SHADER_FOUND_EQUIVALENT || how == DK_SHADER_FOUND_FALLBACK ? nonexact_note(hash, shader_hash, how, wire) : NONEXACT_NONE;
		if (how == DK_SHADER_FOUND_FALLBACK && debug_standin)
		{
			const uint32_t tint = tint_handle();

			if (tint)
				handle = tint;
		}
		if (how != DK_SHADER_FOUND_EXACT && how != DK_SHADER_FOUND_LEFT_OUT)
			noted = missing_note(stage, hash, key_data);
		if ((how == DK_SHADER_FOUND_EQUIVALENT || how == DK_SHADER_FOUND_FALLBACK) && found_told < FOUND_TOLD_LIMIT)
		{
			found_told++;
			platform_log("shaders: pixel key %08x%08x is not in the pack: drawn with %s, %08x%08x%s", (unsigned)(hash >> 32), (unsigned)hash,
				how == DK_SHADER_FOUND_EQUIVALENT ? "the shader of its canonical key" : "its stand-in", (unsigned)(shader_hash >> 32), (unsigned)shader_hash,
				found_told == FOUND_TOLD_LIMIT ? " (no more of these lines)" : "");
		}
		if (how != DK_SHADER_FOUND_NONE)
		{
			memcpy(&pixel_memo[slot].key, key, sizeof(*key));
			pixel_memo[slot].hash = hash;
			pixel_memo[slot].handle = handle;
			pixel_memo[slot].nonexact = nonexact_current;
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
