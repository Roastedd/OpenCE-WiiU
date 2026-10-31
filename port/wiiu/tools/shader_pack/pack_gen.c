/*
PACK_GEN.C

The GLSL for the shader keys the console wrote to shaders_missing.txt (gx2_shaders.c), by OpenCE's own NV2A translators (port/linux/src/nv2a_vsh.c, nv2a_psh.c: the
ES path, as gen_shaders/gen.c uses them). tools/wiiu/shader_pack.sh builds it with the Linux port's flags and runs it under qemu-i386.

	pack_gen <keys.txt> <directory>
	pack_gen --check <count> <seed>      shader_key_wire.h's canonical keys and stand-ins checked on that many random pixel keys; nothing written

keys.txt, one key a line:
	V <program hash, 16 hex digits> <packed attribute mask> <instruction count> <the words, hex>      a vertex program
	P <the pixel key, shader_key_wire.h, in hex>                                                    a pixel shader
The files are named by the key's hash (shader_key_wire.h: the hash gx2_shaders.c looks the shader up by): vs_<hash>.vert and ps_<hash>.frag; one that is there already is left.
A pixel key's shader is written under its canonical key's hash too (shader_key_canonical: the console asks for that when the pack lacks the key), once
the two keys' GLSL is found to be the same text; a key whose canonical key has other GLSL stops the run. The stand-ins' shaders (shader_key_fallback)
are written whatever the keys.
*/

#include "xgpu.h"
#include "shader_key_wire.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* (named: upstream's merge added astc and color_buffer_half_float before the shading language) */
struct xgpu_capabilities xgpu_capabilities = { .shading_language = "300 es" };
/* (upstream's display.reversed_depth is desktop GL's; the Wii U's depth is Direct3D's) */
BOOL xgpu_reversed_depth = FALSE;
int config_boolean(const char *name) { (void)name; return 0; }
const char *config_string(const char *name) { (void)name; return ""; }

static int exists(const char *path)
{
	FILE *file = fopen(path, "r");

	if (file)
		fclose(file);
	return file != NULL;
}

/* The vertex shaders' height fade (nv2a_vsh.c's "uniform vec4 height_fade", which multiplies the vertex colours while height_fade.w is not zero) is the OpenGL
renderer's (d3d8_gl_set_height_fade); d3d8_gx2.c's is empty, so nothing sets the uniform and it held whatever the GPU's register held: the colours of the draws
that vertex colours tint (the HUD's bitmaps and text, the menus') were multiplied away (2026-11-01: the first-person HUD's crosshair, ammo and radar and the pause
menu's text were gone from the upstream merge on). Here it is a constant zero, which the compiler removes. */
static char *no_height_fade(char *text)
{
	static const char declaration[] = "uniform vec4 height_fade;";
	static const char constant[] = "const vec4 height_fade = vec4(0.0);";
	char *at = strstr(text, declaration), *result;
	size_t before;

	if (!at)
		return text;
	result = malloc(strlen(text) + sizeof(constant));
	if (!result)
	{
		perror("malloc");
		exit(1);
	}
	before = (size_t)(at - text);
	memcpy(result, text, before);
	strcpy(result + before, constant);
	strcpy(result + before + sizeof(constant) - 1, at + sizeof(declaration) - 1);
	free(text);
	return result;
}

static void write_file(const char *path, char *text)
{
	FILE *file = fopen(path, "w");

	if (!file)
	{
		perror(path);
		exit(1);
	}
	fputs(text, file);
	fclose(file);
	free(text);
}

static int hex_digit(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static uint64_t pixel_hash(const struct nv2a_pixel_shader_key *key)
{
	unsigned char wire[SHADER_KEY_WIRE_BYTES];

	shader_key_write(key, wire);
	return shader_key_hash(SHADER_KEY_HASH_INIT, wire, sizeof(wire));
}

static void pixel_print(const char *what, const struct nv2a_pixel_shader_key *key)
{
	unsigned char wire[SHADER_KEY_WIRE_BYTES];
	unsigned i;

	shader_key_write(key, wire);
	fprintf(stderr, "%s: P ", what);
	for (i = 0; i < SHADER_KEY_WIRE_BYTES; i++)
		fprintf(stderr, "%02x", wire[i]);
	fprintf(stderr, "\n");
}

static char *pixel_glsl(const struct nv2a_pixel_shader_key *key, const char *what)
{
	char *source = nv2a_pixel_shader_to_glsl(key);

	if (!source)
	{
		fprintf(stderr, "%s: no GLSL for the pixel key\n", what);
		exit(1);
	}
	return source;
}

/* a pixel shader's file, unless it is there already (1 if written); the text is freed */
static int pixel_write(const char *directory, uint64_t hash, char *source)
{
	char path[512];

	snprintf(path, sizeof(path), "%s/ps_%016llx.frag", directory, (unsigned long long)hash);
	if (exists(path))
	{
		free(source);
		return 0;
	}
	write_file(path, source);
	return 1;
}

/* ---------- the check: random keys, with each field's values that the generator tells apart likely */

static uint32_t random_state;

static uint32_t random32(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 17;
	random_state ^= random_state << 5;
	return random_state;
}

static unsigned random_below(unsigned limit)
{
	return random32() % limit;
}

static void random_key(struct nv2a_pixel_shader_key *key)
{
	static const unsigned long functions[] = { 0, D3DCMP_NEVER, D3DCMP_LESS, D3DCMP_EQUAL, D3DCMP_LESSEQUAL, D3DCMP_GREATER, D3DCMP_NOTEQUAL,
		D3DCMP_GREATEREQUAL, D3DCMP_ALWAYS, 1, 0x208 };
	unsigned i;

	memset(key, 0, sizeof(*key));
	for (i = 0; i < D3DRS_PS_MAX; i++)
	{
		/* an input byte's register 0 to 15, its alpha and its mapping, or a word of anything */
		key->combiner_state[i] = random32();
		if (random_below(4) == 0)
			key->combiner_state[i] &= 0x3f3f3f3f;
	}
	key->combiner_state[D3DRS_PSCOMBINERCOUNT] = random_below(11) | (random32() & 0x11100u);
	if (random_below(4) == 0)
		key->combiner_state[D3DRS_PSCOMBINERCOUNT] |= random32() & 0xfffe0000u;
	if (random_below(3) == 0)
		key->combiner_state[D3DRS_PSFINALCOMBINERINPUTSABCD] = key->combiner_state[D3DRS_PSFINALCOMBINERINPUTSEFG] = 0;
	for (i = 0; i < 4; i++)
	{
		const unsigned mode = random_below(8) ? random_below(0x13) : random_below(32);

		key->texture_modes |= (DWORD)mode << (5 * i);
		key->sampler_type[i] = (unsigned char)(random_below(16) ? random_below(4) : random32());
		key->alpha_kill[i] = (unsigned char)(random_below(2) ? 0 : random_below(4) ? 1 : random32());
		key->color_sign[i] = (unsigned char)(random_below(2) ? 0 : random32());
	}
	if (random_below(4) == 0)
		key->texture_modes |= random32() & 0xfff00000u;
	key->alpha_test_function = random_below(8) ? functions[random_below(sizeof(functions) / sizeof(functions[0]))] : random32();
	key->fog_enable = (unsigned char)(random_below(4) ? random_below(2) : random32());
	key->fog_table_mode = (unsigned char)random_below(6);
	key->count_samples = (unsigned char)(random_below(4) ? 0 : random32());
	key->coverage_alpha = (unsigned char)(random_below(2) ? 0 : random_below(3));
	key->point_threshold = (unsigned char)(random_below(2) ? 0 : random_below(3));
	key->per_pixel_lighting = (unsigned char)(random_below(2) ? 0 : random_below(4));
	key->alpha_test_samples = (unsigned char)(random_below(2) ? 0 : random_below(9));
	key->source_blend = (unsigned char)(random_below(2) ? 0 : random32());
}

static int check(unsigned long count, uint32_t seed)
{
	uint64_t numbered[SHADER_KEY_FALLBACKS];
	unsigned long n, collapsed = 0, stood_in = 0;
	unsigned i;

	for (i = 0; i < SHADER_KEY_FALLBACKS; i++)
	{
		struct nv2a_pixel_shader_key fallback;

		shader_key_fallback_numbered(i, &fallback);
		numbered[i] = pixel_hash(&fallback);
	}
	random_state = seed ? seed : 1;
	for (n = 0; n < count; n++)
	{
		struct nv2a_pixel_shader_key key, canonical, twice, fallback, canonical_fallback;
		char *text, *canonical_text;
		int has, canonical_has;

		random_key(&key);
		shader_key_canonical(&key, &canonical);
		shader_key_canonical(&canonical, &twice);
		if (pixel_hash(&twice) != pixel_hash(&canonical))
		{
			pixel_print("key", &key);
			fprintf(stderr, "check %lu: the canonical key of a canonical key is another\n", n);
			return 1;
		}
		text = pixel_glsl(&key, "check");
		canonical_text = pixel_glsl(&canonical, "check");
		if (strcmp(text, canonical_text) != 0)
		{
			pixel_print("key", &key);
			pixel_print("canonical", &canonical);
			fprintf(stderr, "check %lu: the key and its canonical key have different GLSL:\n----\n%s----\n%s----\n", n, text, canonical_text);
			return 1;
		}
		collapsed += pixel_hash(&canonical) != pixel_hash(&key);
		free(text);
		free(canonical_text);
		/* the stand-in: one of the numbered ones, the same for the canonical key, none only for an alpha test that passes nothing */
		has = shader_key_fallback(&key, &fallback);
		canonical_has = shader_key_fallback(&canonical, &canonical_fallback);
		if (has != canonical_has || has != (key.alpha_test_function != D3DCMP_NEVER) ||
			(has && pixel_hash(&fallback) != pixel_hash(&canonical_fallback)))
		{
			pixel_print("key", &key);
			fprintf(stderr, "check %lu: the stand-ins of the key and of its canonical key differ\n", n);
			return 1;
		}
		if (has)
		{
			for (i = 0; i < SHADER_KEY_FALLBACKS && numbered[i] != pixel_hash(&fallback); i++)
				;
			if (i == SHADER_KEY_FALLBACKS)
			{
				pixel_print("key", &key);
				fprintf(stderr, "check %lu: the key's stand-in is none of the numbered ones\n", n);
				return 1;
			}
			stood_in++;
		}
	}
	printf("pack_gen: %lu random pixel keys have the GLSL of their canonical keys (%lu of them another key), and %lu a numbered stand-in\n", count,
		collapsed, stood_in);
	return 0;
}

static int compare_hashes(const void *a, const void *b)
{
	const uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;

	return x < y ? -1 : x > y;
}

int main(int argc, char **argv)
{
	static char line[1 << 16];
	FILE *keys;
	unsigned long vertex_count = 0, pixel_count = 0, canonical_count = 0, fallback_count = 0, line_number = 0, pixel_keys = 0, n, distinct;
	uint64_t *canonical_hashes = NULL;

	if (argc == 4 && !strcmp(argv[1], "--check"))
		return check(strtoul(argv[2], NULL, 10), (uint32_t)strtoul(argv[3], NULL, 10));
	if (argc != 3 || !(keys = fopen(argv[1], "r")))
	{
		fprintf(stderr, "usage: pack_gen <keys.txt> <directory> | pack_gen --check <count> <seed>\n");
		return 1;
	}
	while (fgets(line, sizeof(line), keys))
	{
		char path[512];

		line_number++;
		if (line[0] == 'V' && line[1] == ' ')
		{
			unsigned long long program_hash;
			unsigned mask, count, i;
			unsigned long long recomputed, key_hash;
			char *at;
			uint32_t *words;
			char *source;

			program_hash = strtoull(line + 2, &at, 16);
			mask = (unsigned)strtoul(at, &at, 10);
			count = (unsigned)strtoul(at, &at, 10);
			if (!count || count > 136)
			{
				fprintf(stderr, "line %lu: %u instructions\n", line_number, count);
				return 1;
			}
			words = malloc(count * 4 * sizeof(uint32_t));
			for (i = 0; i < count * 4; i++)
				words[i] = (uint32_t)strtoul(at, &at, 16);
			recomputed = shader_vertex_program_hash(words, count);
			if (recomputed != program_hash)
			{
				fprintf(stderr, "line %lu: the program hash is %016llx, the words hash to %016llx\n", line_number, program_hash, recomputed);
				return 1;
			}
			key_hash = shader_vertex_key(program_hash, mask);
			snprintf(path, sizeof(path), "%s/vs_%016llx.vert", argv[2], key_hash);
			if (!exists(path))
			{
				source = nv2a_vertex_shader_to_glsl((const DWORD *)words, count, mask, NULL);
				if (!source)
				{
					fprintf(stderr, "line %lu: no GLSL for the vertex program\n", line_number);
					return 1;
				}
				write_file(path, no_height_fade(source));
				vertex_count++;
			}
			free(words);
		}
		else if (line[0] == 'P' && line[1] == ' ')
		{
			unsigned char wire[SHADER_KEY_WIRE_BYTES];
			struct nv2a_pixel_shader_key key, canonical;
			uint64_t hash, canonical_hash;
			unsigned i;
			char what[64], *source;

			for (i = 0; i < SHADER_KEY_WIRE_BYTES; i++)
			{
				int high = hex_digit(line[2 + i * 2]), low = hex_digit(line[3 + i * 2]);

				if (high < 0 || low < 0)
				{
					fprintf(stderr, "line %lu: the pixel key is short\n", line_number);
					return 1;
				}
				wire[i] = (unsigned char)(high << 4 | low);
			}
			shader_key_read(wire, &key);
			hash = shader_key_hash(SHADER_KEY_HASH_INIT, wire, sizeof(wire));
			shader_key_canonical(&key, &canonical);
			canonical_hash = pixel_hash(&canonical);
			snprintf(what, sizeof(what), "line %lu", line_number);
			source = pixel_glsl(&key, what);
			if (canonical_hash != hash)
			{
				char *same = pixel_glsl(&canonical, what);

				if (strcmp(source, same) != 0)
				{
					fprintf(stderr, "line %lu: the pixel key %016llx and its canonical key %016llx have different GLSL: shader_key_canonical "
						"(shader_key_wire.h) clears something nv2a_psh.c reads\n", line_number, (unsigned long long)hash,
						(unsigned long long)canonical_hash);
					return 1;
				}
				canonical_count += pixel_write(argv[2], canonical_hash, same);
			}
			pixel_count += pixel_write(argv[2], hash, source);
			canonical_hashes = realloc(canonical_hashes, (pixel_keys + 1) * sizeof(*canonical_hashes));
			if (!canonical_hashes)
			{
				fprintf(stderr, "no memory\n");
				return 1;
			}
			canonical_hashes[pixel_keys++] = canonical_hash;
		}
	}
	fclose(keys);
	for (n = 0; n < SHADER_KEY_FALLBACKS; n++)
	{
		struct nv2a_pixel_shader_key fallback, canonical;

		shader_key_fallback_numbered((unsigned)n, &fallback);
		shader_key_canonical(&fallback, &canonical);
		if (pixel_hash(&canonical) != pixel_hash(&fallback))
		{
			fprintf(stderr, "stand-in %lu is not a canonical key (shader_key_fallback_make)\n", n);
			return 1;
		}
		fallback_count += pixel_write(argv[2], pixel_hash(&fallback), pixel_glsl(&fallback, "a stand-in"));
	}
	distinct = 0;
	if (pixel_keys)
	{
		qsort(canonical_hashes, pixel_keys, sizeof(*canonical_hashes), compare_hashes);
		for (n = 0; n < pixel_keys; n++)
			distinct += !n || canonical_hashes[n] != canonical_hashes[n - 1];
	}
	free(canonical_hashes);
	printf("pack_gen: %lu vertex shaders, %lu pixel shaders written; %lu more under canonical keys (%lu pixel keys, %lu canonical keys), %lu stand-ins\n",
		vertex_count, pixel_count, canonical_count, pixel_keys, distinct, fallback_count);
	return 0;
}
