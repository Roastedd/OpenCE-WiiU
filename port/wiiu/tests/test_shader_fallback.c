/* gx2_shaders.c's pack lookup, its own source compiled with the rest of the game stood in for (tools/test_wiiu_shader_fallback.py): a pixel key the
pack lacks is drawn with its canonical key's shader, else with its stand-in, a key left out of the pack is skipped, and every key the pack lacks is
written down once. The fixtures are keys met on the console and what the pack builder (pack_gen.c, on a little-endian computer) made of them: the
console, big-endian, must find the same hashes. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xgpu.h"
#include "dk_shaders.h"
#include "shader_key_wire.h"
#include "wiiu_missing_keys.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static char last_log[512];
static unsigned log_lines;

void platform_log(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(last_log, sizeof(last_log), format, arguments);
	va_end(arguments);
	log_lines++;
}

/* the capture queue as gx2_shaders.c sees it: whether it takes a key now, and the keys it took */
static int refused;
static unsigned queued_pixel, queued_vertex, unavailable;
static uint64_t last_queued;

int wiiu_missing_keys_observe(void)
{
	return !refused;
}

void wiiu_missing_keys_observed(void)
{
}

int wiiu_missing_keys_pixel(uint32_t high, uint32_t low, const unsigned char *wire, uint32_t bytes)
{
	CHECK(wire && bytes == SHADER_KEY_WIRE_BYTES);
	CHECK(shader_key_hash(SHADER_KEY_HASH_INIT, wire, bytes) == ((uint64_t)high << 32 | low));
	queued_pixel++;
	last_queued = (uint64_t)high << 32 | low;
	return 1;
}

int wiiu_missing_keys_vertex(uint32_t high, uint32_t low, uint32_t program_high, uint32_t program_low, uint32_t packed_mask, uint32_t instructions,
	const uint32_t *words)
{
	CHECK(words && instructions);
	CHECK(shader_vertex_key((uint64_t)program_high << 32 | program_low, packed_mask) == ((uint64_t)high << 32 | low));
	queued_vertex++;
	last_queued = (uint64_t)high << 32 | low;
	return 1;
}

void wiiu_missing_keys_unavailable(void)
{
	unavailable++;
}

void host_gx2_map_changed(void)
{
}

void halo_stream_endian_forget(void)
{
}

#include SHADERS_SOURCE

/* ---------- keys met on the console, and their hashes, canonical keys' hashes and stand-ins' hashes as the pack builder made them */

static const struct fixture
{
	const char *name, *wire;
	uint64_t hash, canonical, stand_in;
} fixtures[] =
{
	/* Blood Gulch, 2026-10-30: missed, its shader in the pack under another key; t0 times C0, faded to white by V0's alpha, alpha tested */
	{ "bg",
		"00000000000000000000000000000000000000003c1c201c000000000000000000200c1400140000000000000000000000000000000000000000000000000000"
		"000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000c0000"
		"00000000000000000000010800000c0c08050c250000140c0b0c0d2c000000000b010b1c0000050c000000000000000000000000c0000000c0000000000c0000"
		"000c0000000c000000000000000c0000000c00000100000000000000000000000000000001000000010000000000000000000000040200000100000000000000"
		, 0x0702c19a9886244aull, 0xce7372d9645436e6ull, 0xba67c6196c856cb2ull },
	/* the pixel key tools/wiiu/shader_exclude.txt leaves out (a bump-mapped cube-map reflection) */
	{ "excluded",
		"000000000000000000000000000000000000000000001d1d0000000000000000000b0f2d00000c0c000000000000000000000000000000000000000000000000"
		"000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000d0000000"
		"000000000000000020010b0b00000c0ccd11cc3120a020cc01110c311d0d150c0000000000000000000000000000000000000000cd000000c0000000000c0300"
		"000c0000000c0000cd10000000000000000000000200000000000000110100000000000021460600010101030000000000000000000000000100000000000000"
		, 0xf770249ae846dc29ull, 0x2547f0f09189a2ceull, 0x74d5c8564437e5fcull },
	{ "untextured",
		"00000000000000000000000000000000000000000000000000000000000000000000000000110000000000000000000000000000000000000000000000000000"
		"00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
		"00000000000000000000000000000c0c15240804010c010c0c04050be1142002090809081d0b040800000000000000000000000000000000d000000045000000"
		"cd100000b4000000000d000000080000000c00000100000000000000000000000000000000000000000000000000000000000000000000000100000000000000"
		, 0x59b024bfa1e6573bull, 0x34ef81132b092d1aull, 0x634d96bea9a99134ull },
	/* stage 0 projected 2D on a cube texture: the stand-in samples the cube */
	{ "projected_cube",
		"000000000000000000000000000000000000000000001d1d000000000000000004000f0500140d0c000000000000000000000000000000000000000000000000"
		"000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000d0000000"
		"00000000000000000000080800000c0c152408040000140c0b0c0d2c1d0d150c0000000000000000000000000000000000000000c0000000d000000045000000"
		"000c0000000c0000cd10000000000000000000000300000000000000000000000000000001000000030000000000000000000000000000000100000000000000"
		, 0x79b8e0a46ddba7b6ull, 0x8a880e4b4473f8f8ull, 0x819b9f2245c713acull },
	{ "cube",
		"000000000000000000000000110c122c0000000000001d1d00000000000000000c000000001c0000000000000000000000000000000000000000000000000000"
		"00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000c000000000000d0000000"
		"0000000000000000110912080000140c0000050c010c022c0b0c0d2c1d0d150c0000000000000000000000000000000000000000000c0000000c0000000c0000"
		"000c0000000c0000cd10000000000000000000000211010000000000000000000000000023000000030100000000000000000000000000000100000000000000"
		, 0x386f761880b63820ull, 0x1f16a7b33583051bull, 0xd2737482f6a0b174ull },
	/* stage 0's alpha kill on: so is the stand-in's */
	{ "kill",
		"081208120000206c20b1200820112012000000000000000000000000000000000000180c001c0000000000000000000000000000000000000000000000000000"
		"00000000000000000000000000000000000000000000000000000000000000000000000000000000000c0200c0000000000c0000004c00000000000000000000"
		"000000000000000020e82011021c013c021c200c2001200c0c04050be1142002090809081d0b0408000000000000000000000000000c0200000c0000000c0000"
		"004c0000b4000000000d000000080000000c00000411010000000000000000000000000001000000010000000100000000000000000000000100000000000000"
		, 0xdaa3af05a00ece20ull, 0x3a5b8e7be001529cull, 0x546d79983f480525ull },
	{ "volume",
		"20a920080c0c1c1c5c5c0000000015140000000000001d1d000000000000000000000f0c00141c1c000000000000000000000000000000000000000000000000"
		"00000000000000000000000000000000000000000000000000000000000000000000000000000000000c0000004c0200004d00004000000000000000d0000000"
		"000000000000000020b820190000000000000000a01d051c0b0c0d2c1d0d150c0000000000000000000000000000000000000000000c00000000000000000000"
		"000c0000000c0000cd10000000000000000000000401000000000000000000000000000042000000020200000000000000000000000000000100000000000000"
		, 0x2b8c63bd67a2f7faull, 0xf14f11dcea0009acull, 0xb4004f0e2ec01868ull },
	/* Blood Gulch's sky (2026-10-31, halo/gx2_trace.txt: drawn first, blended by its alpha): t0, blended toward stage 1's C0 by C0's alpha, and t0's
	alpha; its stand-in is the texture and the texture's alpha */
	{ "sky",
		"00002018000000000000201c000000005c5c00003c1c201c00000000000000000c000000001c0000000000000000000000000000000000000000000000000000"
		"00000000000000000000000000000000000000000000000000000000000000000000000000000000c000000000000000000c000000000000004c0000000c0000"
		"00000000000000000000200801110c31200c200a01110c3100000000000000000b010b1c0000050c000000000000000000000000c0000000000c0000000c0000"
		"000c00000000000000000000000c0000000c00000211010000000000000000000000000001000000010000000000000000000000000000000100000000000000"
		, 0xbcee7733c9e02908ull, 0xaab1da95d772c661ull, 0x7240bdb617108c54ull },
};

/* the stand-ins' hashes in their numbered order (shader_key_fallback_numbered), made by check_fixtures; FNV-1a 64 of them, each as 8 bytes least
significant first, is what the pack builder's came to */
static uint64_t numbered[SHADER_KEY_FALLBACKS];

#define NUMBERED_DIGEST 0xe8a7cde97485754full

static const struct fixture *fixture_named(const char *name)
{
	unsigned i;

	for (i = 0; i < sizeof(fixtures) / sizeof(fixtures[0]); i++)
		if (!strcmp(fixtures[i].name, name))
			return &fixtures[i];
	CHECK(!"no such fixture");
	return NULL;
}

static void key_of(const struct fixture *fixture, struct nv2a_pixel_shader_key *key)
{
	unsigned char wire[SHADER_KEY_WIRE_BYTES], again[SHADER_KEY_WIRE_BYTES];
	unsigned i;

	CHECK(strlen(fixture->wire) == 2 * SHADER_KEY_WIRE_BYTES);
	for (i = 0; i < SHADER_KEY_WIRE_BYTES; i++)
	{
		unsigned value;

		CHECK(sscanf(fixture->wire + 2 * i, "%2x", &value) == 1);
		wire[i] = (unsigned char)value;
	}
	shader_key_read(wire, key);
	shader_key_write(key, again);
	CHECK(!memcmp(wire, again, sizeof(wire)));
}

static uint64_t hash_of(const struct nv2a_pixel_shader_key *key)
{
	unsigned char wire[SHADER_KEY_WIRE_BYTES];

	shader_key_write(key, wire);
	return shader_key_hash(SHADER_KEY_HASH_INIT, wire, sizeof(wire));
}

/* ---------- packs */

struct entry
{
	uint32_t stage;
	uint64_t hash;
	const char *shader; /* its bytes, NULL for a mark of a key left out */
};

static int entry_order(const void *a, const void *b)
{
	const struct entry *x = a, *y = b;

	if (x->stage != y->stage)
		return x->stage < y->stage ? -1 : 1;
	return x->hash < y->hash ? -1 : x->hash > y->hash;
}

static void put32(FILE *file, uint32_t value)
{
	fputc((int)(value >> 24), file);
	fputc((int)(value >> 16) & 0xff, file);
	fputc((int)(value >> 8) & 0xff, file);
	fputc((int)value & 0xff, file);
}

/* the pack at TEST_PACK_FILE, as tools/wiiu/shader_pack.py writes it; bad_entry >= 0 makes that entry's shader run past the end */
static void pack_write(struct entry *entries, unsigned count, int bad_entry)
{
	FILE *file = fopen(TEST_PACK_FILE, "wb");
	uint32_t offset = 16 + 24 * count;
	unsigned i;

	CHECK(file);
	qsort(entries, count, sizeof(*entries), entry_order);
	fwrite("HSP1", 1, 4, file);
	put32(file, count);
	put32(file, 0);
	put32(file, 0);
	for (i = 0; i < count; i++)
	{
		const uint32_t size = entries[i].shader ? (uint32_t)strlen(entries[i].shader) + 1 : 0;

		put32(file, entries[i].stage);
		put32(file, (uint32_t)(entries[i].hash >> 32));
		put32(file, (uint32_t)entries[i].hash);
		put32(file, size ? offset : 0);
		put32(file, (int)i == bad_entry ? 1u << 20 : size);
		put32(file, 0);
		offset += size;
	}
	for (i = 0; i < count; i++)
		if (entries[i].shader)
			fwrite(entries[i].shader, 1, strlen(entries[i].shader) + 1, file);
	CHECK(fclose(file) == 0);
}

/* gx2_shaders.c as when the game starts: no pack read, no key seen, nothing remembered */
static void restart(void)
{
	free(pack);
	pack = NULL;
	pack_size = pack_count = 0;
	pack_tried = 0;
	seen_count = 0;
	memset(pixel_memo, 0, sizeof(pixel_memo));
	found[0] = found[1] = 0;
	found_told = 0;
	refused = 0;
	queued_pixel = queued_vertex = unavailable = 0;
	log_lines = 0;
}

static const char *shader_of(uint32_t handle)
{
	uint32_t size = 0, stage = 9;
	const char *blob = dk_shader_blob(handle, &size, &stage);

	if (blob)
	{
		CHECK(size == strlen(blob) + 1);
		CHECK(stage <= DK_SHADER_STAGE_PIXEL);
	}
	return blob;
}

/* a pixel key's lookup: the shader found (NULL for none) and how */
static const char *pixel(const struct nv2a_pixel_shader_key *key, uint32_t how)
{
	const uint32_t handle = dk_shader_for_draw(DK_SHADER_STAGE_PIXEL, key);

	CHECK(dk_shader_found(DK_SHADER_STAGE_PIXEL) == how);
	CHECK(!handle == (how == DK_SHADER_FOUND_LEFT_OUT || how == DK_SHADER_FOUND_NONE));
	return handle ? shader_of(handle) : NULL;
}

static const char *stand_in_name(uint64_t hash)
{
	static char names[SHADER_KEY_FALLBACKS][32];
	unsigned i;

	for (i = 0; i < SHADER_KEY_FALLBACKS && numbered[i] != hash; i++)
		;
	CHECK(i < SHADER_KEY_FALLBACKS);
	snprintf(names[i], sizeof(names[i]), "stand-in %016llx", (unsigned long long)hash);
	return names[i];
}

/* ---------- random keys (as pack_gen.c --check makes them) */

static uint32_t random_state = 20261030;

static uint32_t random32(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 17;
	random_state ^= random_state << 5;
	return random_state;
}

static void random_key(struct nv2a_pixel_shader_key *key)
{
	static const unsigned long functions[] = { 0, D3DCMP_NEVER, D3DCMP_LESS, D3DCMP_EQUAL, D3DCMP_LESSEQUAL, D3DCMP_GREATER, D3DCMP_NOTEQUAL,
		D3DCMP_GREATEREQUAL, D3DCMP_ALWAYS, 1 };
	unsigned i;

	memset(key, 0, sizeof(*key));
	for (i = 0; i < D3DRS_PS_MAX; i++)
		key->combiner_state[i] = random32() & (random32() % 4 ? 0x3f3f3f3fu : 0xffffffffu);
	key->combiner_state[D3DRS_PSCOMBINERCOUNT] = random32() % 11 | (random32() & 0x11100u);
	if (random32() % 3 == 0)
		key->combiner_state[D3DRS_PSFINALCOMBINERINPUTSABCD] = key->combiner_state[D3DRS_PSFINALCOMBINERINPUTSEFG] = 0;
	for (i = 0; i < 4; i++)
	{
		key->texture_modes |= (DWORD)(random32() % 8 ? random32() % 0x13 : random32() % 32) << (5 * i);
		key->sampler_type[i] = (unsigned char)(random32() % 16 ? random32() % 4 : random32());
		key->alpha_kill[i] = (unsigned char)(random32() % 2 ? 0 : random32());
		key->color_sign[i] = (unsigned char)(random32() % 2 ? 0 : random32());
	}
	key->alpha_test_function = functions[random32() % (sizeof(functions) / sizeof(functions[0]))];
	key->fog_enable = (unsigned char)(random32() % 2);
	key->fog_table_mode = (unsigned char)(random32() % 6);
	key->point_threshold = (unsigned char)(random32() % 3);
	key->per_pixel_lighting = (unsigned char)(random32() % 4);
	key->alpha_test_samples = (unsigned char)(random32() % 9);
	key->source_blend = (unsigned char)(random32() % 6);
}

/* ---------- the checks */

/* the hashes, canonical keys and stand-ins are those the pack builder made, on this machine as on a little-endian one */
static void check_fixtures(void)
{
	uint64_t digest = SHADER_KEY_HASH_INIT;
	unsigned i, j;

	for (i = 0; i < SHADER_KEY_FALLBACKS; i++)
	{
		struct nv2a_pixel_shader_key fallback, canonical;
		unsigned char bytes[8], *at = bytes;

		shader_key_fallback_numbered(i, &fallback);
		numbered[i] = hash_of(&fallback);
		shader_key_put32(&at, (uint32_t)numbered[i]);
		shader_key_put32(&at, (uint32_t)(numbered[i] >> 32));
		digest = shader_key_hash(digest, bytes, sizeof(bytes));
		for (j = 0; j < i; j++)
			CHECK(numbered[j] != numbered[i]);
		shader_key_canonical(&fallback, &canonical);
		CHECK(hash_of(&canonical) == numbered[i]);
	}
	CHECK(digest == NUMBERED_DIGEST);
	for (i = 0; i < sizeof(fixtures) / sizeof(fixtures[0]); i++)
	{
		struct nv2a_pixel_shader_key key, canonical, fallback;

		key_of(&fixtures[i], &key);
		CHECK(hash_of(&key) == fixtures[i].hash);
		shader_key_canonical(&key, &canonical);
		CHECK(hash_of(&canonical) == fixtures[i].canonical);
		CHECK(shader_key_fallback(&key, &fallback));
		CHECK(hash_of(&fallback) == fixtures[i].stand_in);
	}
}

/* what each kind of key gets for its stand-in: what its final color and alpha depend on */
static void check_stand_ins(void)
{
	struct nv2a_pixel_shader_key key, fallback;

	/* the sky: the texture in color (stage 1's C0 only blends toward a color), the texture's alpha in alpha (it blends by it) */
	key_of(fixture_named("sky"), &key);
	CHECK(shader_key_fallback(&key, &fallback) && fallback.texture_modes == SHADER_KEY_MODE_PROJECT2D && fallback.sampler_type[0] == _xgpu_sampler_2d);
	CHECK(fallback.combiner_state[D3DRS_PSRGBINPUTS0] == 0x08200000u && fallback.combiner_state[D3DRS_PSALPHAINPUTS0] == 0x18200000u);
	CHECK(!fallback.alpha_test_function && !fallback.alpha_kill[0]);
	/* Blood Gulch's key, alpha tested: the color blends by V0's alpha (the texture), the alpha is V0's */
	key_of(fixture_named("bg"), &key);
	CHECK(shader_key_fallback(&key, &fallback));
	CHECK(fallback.texture_modes == SHADER_KEY_MODE_PROJECT2D && fallback.sampler_type[0] == _xgpu_sampler_2d);
	CHECK(fallback.alpha_test_function == D3DCMP_GREATER && !fallback.alpha_kill[0]);
	CHECK(fallback.combiner_state[D3DRS_PSRGBINPUTS0] == 0x08200000u && fallback.combiner_state[D3DRS_PSALPHAINPUTS0] == 0x20140000u);
	/* GREATEREQUAL is tested as GREATER; LESS and the rest not at all; NEVER has none */
	key.alpha_test_function = D3DCMP_GREATEREQUAL;
	CHECK(shader_key_fallback(&key, &fallback) && fallback.alpha_test_function == D3DCMP_GREATER);
	key.alpha_test_function = D3DCMP_LESS;
	CHECK(shader_key_fallback(&key, &fallback) && fallback.alpha_test_function == 0);
	key.alpha_test_function = D3DCMP_ALWAYS;
	CHECK(shader_key_fallback(&key, &fallback) && fallback.alpha_test_function == 0);
	key.alpha_test_function = D3DCMP_NEVER;
	CHECK(!shader_key_fallback(&key, &fallback));
	/* a cube texture, sampled by direction, in either mode */
	key_of(fixture_named("cube"), &key);
	CHECK(shader_key_fallback(&key, &fallback) && fallback.texture_modes == SHADER_KEY_MODE_CUBEMAP && fallback.sampler_type[0] == _xgpu_sampler_cube);
	key_of(fixture_named("projected_cube"), &key);
	CHECK(shader_key_fallback(&key, &fallback) && fallback.texture_modes == SHADER_KEY_MODE_CUBEMAP && fallback.sampler_type[0] == _xgpu_sampler_cube);
	key_of(fixture_named("volume"), &key);
	CHECK(shader_key_fallback(&key, &fallback) && fallback.texture_modes == SHADER_KEY_MODE_PROJECT3D && fallback.sampler_type[0] == _xgpu_sampler_3d);
	key_of(fixture_named("kill"), &key);
	CHECK(shader_key_fallback(&key, &fallback) && fallback.alpha_kill[0] == 1);
	key_of(fixture_named("untextured"), &key);
	CHECK(shader_key_fallback(&key, &fallback) && fallback.texture_modes == 0 && fallback.sampler_type[0] == _xgpu_sampler_none && !fallback.alpha_kill[0]);
	/* untextured, depending on nothing: transparent black */
	memset(&key, 0, sizeof(key));
	key.combiner_state[D3DRS_PSCOMBINERCOUNT] = 1;
	CHECK(shader_key_fallback(&key, &fallback));
	CHECK(fallback.combiner_state[D3DRS_PSRGBINPUTS0] == 0x00200000u && fallback.combiner_state[D3DRS_PSALPHAINPUTS0] == 0x00200000u);
	/* a stage 0 that samples nothing (no texture) is untextured, whatever its mode */
	key.texture_modes = SHADER_KEY_MODE_PROJECT2D;
	key.alpha_kill[0] = 1;
	CHECK(shader_key_fallback(&key, &fallback) && fallback.texture_modes == 0 && !fallback.alpha_kill[0]);
	/* textured: R0's alpha is t0's until a stage writes it; V0 counts where its product reaches the result, not where a stage past the count or one
	that writes nowhere reads it */
	key.sampler_type[0] = _xgpu_sampler_2d;
	key.combiner_state[D3DRS_PSRGBINPUTS0 + 1] = 0x04000000u;
	CHECK(shader_key_fallback(&key, &fallback) && fallback.combiner_state[D3DRS_PSRGBINPUTS0] == 0x08200000u &&
		fallback.combiner_state[D3DRS_PSALPHAINPUTS0] == 0x18200000u);
	key.combiner_state[D3DRS_PSCOMBINERCOUNT] = 2;
	CHECK(shader_key_fallback(&key, &fallback) && fallback.combiner_state[D3DRS_PSRGBINPUTS0] == 0x08200000u);
	key.combiner_state[D3DRS_PSRGBOUTPUTS0 + 1] = 0xc0;
	CHECK(shader_key_fallback(&key, &fallback) && fallback.combiner_state[D3DRS_PSRGBINPUTS0] == 0x08040000u);
	/* the color portion's AB written to an alpha as well (its blue) */
	key.combiner_state[D3DRS_PSRGBOUTPUTS0 + 1] = 0x800c0;
	CHECK(shader_key_fallback(&key, &fallback) && fallback.combiner_state[D3DRS_PSALPHAINPUTS0] == 0x20140000u);
	/* a textured key's C0 is left out: it often only tints or blends, and a black C0 must not hide the texture */
	key.combiner_state[D3DRS_PSRGBINPUTS0 + 1] = 0x01000000u;
	key.combiner_state[D3DRS_PSRGBOUTPUTS0 + 1] = 0xc0;
	CHECK(shader_key_fallback(&key, &fallback) && fallback.combiner_state[D3DRS_PSRGBINPUTS0] == 0x08200000u &&
		fallback.combiner_state[D3DRS_PSALPHAINPUTS0] == 0x18200000u);
	/* the final combiner: its C0 is a constant of its own; E and F count where its E times F register is read; G is the alpha */
	key.combiner_state[D3DRS_PSRGBINPUTS0 + 1] = 0;
	key.combiner_state[D3DRS_PSFINALCOMBINERINPUTSABCD] = 0x0100000fu;
	key.combiner_state[D3DRS_PSFINALCOMBINERINPUTSEFG] = 0x00040000u;
	CHECK(shader_key_fallback(&key, &fallback) && fallback.combiner_state[D3DRS_PSRGBINPUTS0] == 0x08040000u &&
		fallback.combiner_state[D3DRS_PSALPHAINPUTS0] == 0x20200000u);
	key.combiner_state[D3DRS_PSFINALCOMBINERINPUTSEFG] = 0x00000004u;
	CHECK(shader_key_fallback(&key, &fallback) && fallback.combiner_state[D3DRS_PSRGBINPUTS0] == 0x08200000u);
	/* (G reading R0's alpha, which no stage wrote: t0's) */
	key.combiner_state[D3DRS_PSFINALCOMBINERINPUTSEFG] = 0x00001c00u;
	CHECK(shader_key_fallback(&key, &fallback) && fallback.combiner_state[D3DRS_PSALPHAINPUTS0] == 0x18200000u);
	key.combiner_state[D3DRS_PSFINALCOMBINERINPUTSEFG] = 0x00001400u;
	CHECK(shader_key_fallback(&key, &fallback) && fallback.combiner_state[D3DRS_PSALPHAINPUTS0] == 0x20140000u);
	key.combiner_state[D3DRS_PSFINALCOMBINERINPUTSEFG] = 0x00003000u;
	CHECK(shader_key_fallback(&key, &fallback) && fallback.combiner_state[D3DRS_PSALPHAINPUTS0] == 0x20200000u);
	/* untextured: C0 in color and alpha; a fade by the final combiner's constant, which the stand-in cannot follow, is nothing */
	memset(&key, 0, sizeof(key));
	key.combiner_state[D3DRS_PSCOMBINERCOUNT] = 1;
	key.combiner_state[D3DRS_PSRGBINPUTS0] = 0x01200000u;
	key.combiner_state[D3DRS_PSRGBOUTPUTS0] = 0xc0;
	key.combiner_state[D3DRS_PSALPHAINPUTS0] = 0x11200000u;
	key.combiner_state[D3DRS_PSALPHAOUTPUTS0] = 0xc0;
	CHECK(shader_key_fallback(&key, &fallback) && fallback.combiner_state[D3DRS_PSRGBINPUTS0] == 0x20010000u &&
		fallback.combiner_state[D3DRS_PSALPHAINPUTS0] == 0x20110000u);
	key.combiner_state[D3DRS_PSFINALCOMBINERINPUTSABCD] = 0x00000001u;
	key.combiner_state[D3DRS_PSFINALCOMBINERINPUTSEFG] = 0x00001100u;
	CHECK(shader_key_fallback(&key, &fallback) && fallback.combiner_state[D3DRS_PSRGBINPUTS0] == 0x00200000u &&
		fallback.combiner_state[D3DRS_PSALPHAINPUTS0] == 0x00200000u);
}

/* the canonical key: what the generator does not read cleared, the rest kept */
static void check_canonical(void)
{
	struct nv2a_pixel_shader_key key, other, canonical, again;
	unsigned i;

	key_of(fixture_named("bg"), &key);
	shader_key_canonical(&key, &canonical);
	/* stale stages past the count, the constants, the unnamed state, the Switch's mode bits: the same canonical key */
	other = key;
	other.combiner_state[D3DRS_PSRGBINPUTS0 + 5] = 0x12345678u;
	other.combiner_state[D3DRS_PSALPHAOUTPUTS0 + 7] = 0xc0u;
	other.combiner_state[D3DRS_PSCONSTANT0_0 + 3] = 0xff00ff00u;
	other.combiner_state[D3DRS_PSFINALCOMBINERCONSTANT1] = 1;
	other.combiner_state[D3DRS_PSCOMBINERCOUNT + 1] = 7;
	other.texture_modes |= 0x50000000u;
	other.combiner_state[D3DRS_PSCOMPAREMODE] = 0xf;
	other.alpha_kill[2] = 1;
	other.color_sign[3] = 0xf;
	other.sampler_type[1] = _xgpu_sampler_2d;
	other.combiner_state[D3DRS_PSCOMBINERCOUNT] |= 0x00f00000u;
	shader_key_canonical(&other, &again);
	CHECK(hash_of(&other) != hash_of(&key) && hash_of(&again) == hash_of(&canonical));
	/* what is read is kept: a stage the count runs, a 3D sampler declared, the alpha test, the fog's mode */
	other = key;
	other.combiner_state[D3DRS_PSRGBINPUTS0] ^= 0x01000000u;
	shader_key_canonical(&other, &again);
	CHECK(hash_of(&again) != hash_of(&canonical));
	other = key;
	other.sampler_type[1] = _xgpu_sampler_3d;
	shader_key_canonical(&other, &again);
	CHECK(hash_of(&again) != hash_of(&canonical));
	other = key;
	other.alpha_test_function = D3DCMP_LESS;
	shader_key_canonical(&other, &again);
	CHECK(hash_of(&again) != hash_of(&canonical));
	other = key;
	other.fog_table_mode = D3DFOG_LINEAR;
	shader_key_canonical(&other, &again);
	CHECK(hash_of(&again) != hash_of(&canonical));
	other = key;
	other.alpha_test_samples = 4;
	shader_key_canonical(&other, &again);
	CHECK(hash_of(&again) != hash_of(&canonical));
	other.alpha_test_function = D3DCMP_EQUAL;
	shader_key_canonical(&other, &again);
	other.alpha_test_samples = 0;
	shader_key_canonical(&other, &canonical);
	CHECK(hash_of(&again) == hash_of(&canonical));
	shader_key_canonical(&key, &canonical);
	/* ALWAYS writes no test, as no test; the fog's mode means nothing without fog */
	other = key;
	other.alpha_test_function = 0;
	shader_key_canonical(&other, &again);
	other.alpha_test_function = D3DCMP_ALWAYS;
	shader_key_canonical(&other, &canonical);
	CHECK(hash_of(&again) == hash_of(&canonical));
	other.fog_enable = 0;
	other.fog_table_mode = D3DFOG_EXP;
	shader_key_canonical(&other, &again);
	other.fog_table_mode = 0;
	shader_key_canonical(&other, &canonical);
	CHECK(hash_of(&again) == hash_of(&canonical));
	/* random keys: canonical once and for all, and their stand-in that of their canonical key */
	for (i = 0; i < 100000; i++)
	{
		struct nv2a_pixel_shader_key fallback, canonical_fallback;
		int has;

		random_key(&key);
		shader_key_canonical(&key, &canonical);
		shader_key_canonical(&canonical, &again);
		CHECK(hash_of(&again) == hash_of(&canonical));
		has = shader_key_fallback(&key, &fallback);
		CHECK(has == shader_key_fallback(&canonical, &canonical_fallback));
		CHECK(has == (key.alpha_test_function != D3DCMP_NEVER));
		if (has)
		{
			CHECK(hash_of(&fallback) == hash_of(&canonical_fallback));
			stand_in_name(hash_of(&fallback));
		}
	}
}

/* the lookup: exact, canonical, stand-in, left out, none; and what is written down */
static void check_lookup(void)
{
	static const uint32_t words[8] = { 0, 0x0020001b, 0x0836106c, 0x2f100ff8, 0, 0x0020001b, 0x0836106c, 0x2f100ff9 };
	const struct dk_vertex_key vertex_found = { 0x1111222233334444ull, 0 }, vertex_left_out = { 0x5555666677778888ull, 4 };
	struct dk_vertex_key vertex_missing = { 0, 0 };
	struct entry entries[8 + SHADER_KEY_FALLBACKS];
	struct nv2a_pixel_shader_key key, other;
	unsigned count = 0, i, queued_before;
	char equivalent_name[64];

	vertex_missing.program_hash = shader_vertex_program_hash(words, 2);
	entries[count++] = (struct entry){ DK_SHADER_STAGE_VERTEX, shader_vertex_key(vertex_found.program_hash, vertex_found.packed_mask), "vertex" };
	entries[count++] = (struct entry){ DK_SHADER_STAGE_VERTEX + 2, shader_vertex_key(vertex_left_out.program_hash, vertex_left_out.packed_mask), NULL };
	entries[count++] = (struct entry){ DK_SHADER_STAGE_PIXEL, fixture_named("cube")->hash, "cube" };
	entries[count++] = (struct entry){ DK_SHADER_STAGE_PIXEL, fixture_named("bg")->canonical, "bg's canonical key" };
	entries[count++] = (struct entry){ DK_SHADER_STAGE_PIXEL + 2, fixture_named("excluded")->hash, NULL };
	entries[count++] = (struct entry){ DK_SHADER_STAGE_PIXEL, fixture_named("excluded")->canonical, "the left-out key's canonical key" };
	for (i = 0; i < SHADER_KEY_FALLBACKS; i++)
	{
		unsigned j;

		for (j = 0; j < i && numbered[j] != numbered[i]; j++)
			;
		if (j == i)
			entries[count++] = (struct entry){ DK_SHADER_STAGE_PIXEL, numbered[i], stand_in_name(numbered[i]) };
	}
	CHECK(count == 6 + SHADER_KEY_FALLBACKS);
	restart();
	pack_write(entries, count, -1);

	/* vertex keys: there, left out (not written down), missing (written down once, with its program) */
	CHECK(!strcmp(shader_of(dk_shader_for_draw(DK_SHADER_STAGE_VERTEX, &vertex_found)), "vertex"));
	CHECK(dk_shader_found(DK_SHADER_STAGE_VERTEX) == DK_SHADER_FOUND_EXACT);
	CHECK(strstr(last_log, "the pack has 1 vertex and 117 pixel shaders, 2 keys left out"));
	CHECK(!dk_shader_for_draw(DK_SHADER_STAGE_VERTEX, &vertex_left_out));
	CHECK(dk_shader_found(DK_SHADER_STAGE_VERTEX) == DK_SHADER_FOUND_LEFT_OUT && !queued_vertex && !unavailable);
	dk_shader_program_note(vertex_missing.program_hash, words, 2);
	CHECK(!dk_shader_for_draw(DK_SHADER_STAGE_VERTEX, &vertex_missing));
	CHECK(dk_shader_found(DK_SHADER_STAGE_VERTEX) == DK_SHADER_FOUND_NONE && queued_vertex == 1);
	CHECK(last_queued == shader_vertex_key(vertex_missing.program_hash, 0));
	CHECK(!dk_shader_for_draw(DK_SHADER_STAGE_VERTEX, &vertex_missing) && queued_vertex == 1);

	/* a pixel key in the pack: its own shader, nothing written down */
	key_of(fixture_named("cube"), &key);
	CHECK(!strcmp(pixel(&key, DK_SHADER_FOUND_EXACT), "cube") && !queued_pixel);
	/* Blood Gulch's key: its canonical key's shader, written down once, said once in the log */
	key_of(fixture_named("bg"), &key);
	log_lines = 0;
	CHECK(!strcmp(pixel(&key, DK_SHADER_FOUND_EQUIVALENT), "bg's canonical key"));
	CHECK(queued_pixel == 1 && last_queued == fixture_named("bg")->hash && log_lines == 1);
	snprintf(equivalent_name, sizeof(equivalent_name), "drawn with the shader of its canonical key, %016llx",
		(unsigned long long)fixture_named("bg")->canonical);
	CHECK(strstr(last_log, "pixel key 0702c19a9886244a is not in the pack") && strstr(last_log, equivalent_name));
	CHECK(!strcmp(pixel(&key, DK_SHADER_FOUND_EQUIVALENT), "bg's canonical key") && queued_pixel == 1 && log_lines == 1);
	/* the same key with another stale stage: another key, the same canonical key's shader */
	other = key;
	other.combiner_state[D3DRS_PSRGBINPUTS0 + 6] = 0x2c0d0c0bu;
	CHECK(!strcmp(pixel(&other, DK_SHADER_FOUND_EQUIVALENT), "bg's canonical key") && queued_pixel == 2 && last_queued == hash_of(&other));
	/* the left-out key: skipped though its canonical key's shader is there, not written down, also from the memo */
	key_of(fixture_named("excluded"), &key);
	CHECK(!pixel(&key, DK_SHADER_FOUND_LEFT_OUT) && queued_pixel == 2);
	CHECK(!pixel(&key, DK_SHADER_FOUND_LEFT_OUT) && queued_pixel == 2);
	/* keys whose shader is nowhere: their stand-in, each written down */
	{
		static const char *const names[] = { "untextured", "projected_cube", "kill", "volume" };

		for (i = 0; i < 4; i++)
		{
			key_of(fixture_named(names[i]), &key);
			CHECK(!strcmp(pixel(&key, DK_SHADER_FOUND_FALLBACK), stand_in_name(fixture_named(names[i])->stand_in)));
			CHECK(queued_pixel == 3 + i && last_queued == fixture_named(names[i])->hash);
			CHECK(strstr(last_log, "drawn with its stand-in"));
		}
	}
	/* an alpha test of NEVER: no stand-in, the draw skipped; written down once, looked for each time */
	key_of(fixture_named("volume"), &key);
	key.alpha_test_function = D3DCMP_NEVER;
	CHECK(!pixel(&key, DK_SHADER_FOUND_NONE) && queued_pixel == 7);
	CHECK(!pixel(&key, DK_SHADER_FOUND_NONE) && queued_pixel == 7);
	/* the queue takes no key now: the stand-in drawn all the same, the key written down by the next draw after the queue takes keys again */
	key_of(fixture_named("volume"), &key);
	key.fog_enable = !key.fog_enable;
	refused = 1;
	CHECK(!strcmp(pixel(&key, DK_SHADER_FOUND_FALLBACK), stand_in_name(fixture_named("volume")->stand_in)) && queued_pixel == 7);
	CHECK(pixel(&key, DK_SHADER_FOUND_FALLBACK) && queued_pixel == 7);
	refused = 0;
	CHECK(pixel(&key, DK_SHADER_FOUND_FALLBACK) && queued_pixel == 8 && last_queued == hash_of(&key));
	CHECK(pixel(&key, DK_SHADER_FOUND_FALLBACK) && queued_pixel == 8);
	/* random keys: each its own stand-in (or none, for NEVER), each written down once; the lines in the log stop at their limit */
	queued_before = queued_pixel;
	for (i = 0; i < 3000; i++)
	{
		struct nv2a_pixel_shader_key fallback;

		random_key(&key);
		if (shader_key_fallback(&key, &fallback))
			CHECK(!strcmp(pixel(&key, DK_SHADER_FOUND_FALLBACK), stand_in_name(hash_of(&fallback))));
		else
			CHECK(!pixel(&key, DK_SHADER_FOUND_NONE));
		CHECK(queued_pixel == queued_before + i + 1 && last_queued == hash_of(&key));
	}
	CHECK(found_told == FOUND_TOLD_LIMIT && log_lines == FOUND_TOLD_LIMIT);
	/* and what was found before is found the same after many other keys */
	key_of(fixture_named("bg"), &key);
	CHECK(!strcmp(pixel(&key, DK_SHADER_FOUND_EQUIVALENT), "bg's canonical key"));
	key_of(fixture_named("cube"), &key);
	CHECK(!strcmp(pixel(&key, DK_SHADER_FOUND_EXACT), "cube"));
}

/* a pack of before the stand-ins (only the keys' own shaders): a key it lacks is skipped and written down, as before */
static void check_old_pack(void)
{
	struct entry entries[2];
	struct nv2a_pixel_shader_key key;

	entries[0] = (struct entry){ DK_SHADER_STAGE_PIXEL, fixture_named("cube")->hash, "cube" };
	entries[1] = (struct entry){ DK_SHADER_STAGE_PIXEL, fixture_named("kill")->hash, "kill" };
	restart();
	pack_write(entries, 2, -1);
	key_of(fixture_named("cube"), &key);
	CHECK(!strcmp(pixel(&key, DK_SHADER_FOUND_EXACT), "cube"));
	key_of(fixture_named("bg"), &key);
	CHECK(!pixel(&key, DK_SHADER_FOUND_NONE) && queued_pixel == 1);
	CHECK(!pixel(&key, DK_SHADER_FOUND_NONE) && queued_pixel == 1);
	key_of(fixture_named("kill"), &key);
	CHECK(!strcmp(pixel(&key, DK_SHADER_FOUND_EXACT), "kill"));
	CHECK(!found_told);
}

/* no pack, a broken one, entries that are no shader */
static void check_bad_packs(void)
{
	struct entry entries[4];
	struct nv2a_pixel_shader_key key;
	FILE *file;
	uint32_t handle;

	restart();
	remove(TEST_PACK_FILE);
	key_of(fixture_named("cube"), &key);
	CHECK(!pixel(&key, DK_SHADER_FOUND_NONE) && strstr(last_log, "no pack"));
	CHECK(!shader_of(1));

	/* more entries than the file has room for */
	restart();
	file = fopen(TEST_PACK_FILE, "wb");
	CHECK(file);
	fwrite("HSP1\0\0\0\x09\0\0\0\0\0\0\0\0", 1, 16, file);
	fclose(file);
	CHECK(!pixel(&key, DK_SHADER_FOUND_NONE) && strstr(last_log, "is not a shader pack"));

	/* a mark has no shader, even one given a size; nor has an entry that runs past the file, or a handle past the entries */
	restart();
	entries[0] = (struct entry){ DK_SHADER_STAGE_PIXEL, 1, "first" };
	entries[1] = (struct entry){ DK_SHADER_STAGE_PIXEL, 2, "second" };
	entries[2] = (struct entry){ DK_SHADER_STAGE_PIXEL + 2, 3, NULL };
	entries[3] = (struct entry){ DK_SHADER_STAGE_VERTEX + 2, 4, "a mark with a size" };
	pack_write(entries, 4, 1);
	CHECK(!pixel(&key, DK_SHADER_FOUND_NONE));
	handle = find(DK_SHADER_STAGE_PIXEL, 1);
	CHECK(handle == 1 && !strcmp(shader_of(handle), "first"));
	CHECK(find(DK_SHADER_STAGE_PIXEL, 2) == 2 && !shader_of(2));
	CHECK(find(DK_SHADER_STAGE_VERTEX + 2, 4) == 3 && !shader_of(3));
	CHECK(find(DK_SHADER_STAGE_PIXEL + 2, 3) == 4 && !shader_of(4));
	CHECK(!find(DK_SHADER_STAGE_PIXEL, 3) && !shader_of(0) && !shader_of(5));
}

int main(void)
{
	check_fixtures();
	check_stand_ins();
	check_canonical();
	check_lookup();
	check_old_pack();
	check_bad_packs();
	remove(TEST_PACK_FILE);
	printf("shader fallback tests passed\n");
	return 0;
}
