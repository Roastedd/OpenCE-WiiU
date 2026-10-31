/*
PACK_GEN.C

The GLSL for the shader keys the console wrote to shaders_missing.txt (gx2_shaders.c), by OpenCE's own NV2A translators (port/linux/src/nv2a_vsh.c, nv2a_psh.c: the
ES path, as gen_shaders/gen.c uses them). tools/wiiu/shader_pack.sh builds it with the Linux port's flags and runs it under qemu-i386.

	pack_gen <keys.txt> <directory>

keys.txt, one key a line:
	V <program hash, 16 hex digits> <packed attribute mask> <instruction count> <the words, hex>      a vertex program
	P <the pixel key, shader_key_wire.h, in hex>                                                    a pixel shader
The files are named by the key's hash (shader_key_wire.h: the hash gx2_shaders.c looks the shader up by): vs_<hash>.vert and ps_<hash>.frag; one that is there already is left.
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

int main(int argc, char **argv)
{
	static char line[1 << 16];
	FILE *keys;
	unsigned long vertex_count = 0, pixel_count = 0, line_number = 0;

	if (argc != 3 || !(keys = fopen(argv[1], "r")))
	{
		fprintf(stderr, "usage: pack_gen <keys.txt> <directory>\n");
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
				write_file(path, source);
				vertex_count++;
			}
			free(words);
		}
		else if (line[0] == 'P' && line[1] == ' ')
		{
			unsigned char wire[SHADER_KEY_WIRE_BYTES];
			struct nv2a_pixel_shader_key key;
			unsigned i;
			char *source;

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
			snprintf(path, sizeof(path), "%s/ps_%016llx.frag", argv[2],
				(unsigned long long)shader_key_hash(SHADER_KEY_HASH_INIT, wire, sizeof(wire)));
			if (!exists(path))
			{
				shader_key_read(wire, &key);
				source = nv2a_pixel_shader_to_glsl(&key);
				if (!source)
				{
					fprintf(stderr, "line %lu: no GLSL for the pixel key\n", line_number);
					return 1;
				}
				write_file(path, source);
				pixel_count++;
			}
		}
	}
	fclose(keys);
	printf("pack_gen: %lu vertex shaders, %lu pixel shaders written\n", vertex_count, pixel_count);
	return 0;
}
