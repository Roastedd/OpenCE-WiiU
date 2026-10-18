/*
GEN.C

Writes the GLSL that OpenCE's NV2A translators (port/linux/src/nv2a_vsh.c, nv2a_psh.c) produce for the shaders the
game itself ships, on the ES path (HALO_GLES, "#version 300 es") that Android, the web and the Switch use and the
Wii U will use. It is built by tools/wiiu/gx2_probe_shaders.sh with the Linux port's own flags and runs under
qemu-i386; the translators are the real ones, only the GL header and the configuration are stubbed.

	gen <directory>          the shaders the GX2 probe embeds (shaders listed in probe_gx2/README.md)
	gen --all <directory>    every vertex program the game has (67, each plain, with the NORMPACKED3 normal, and
	                         lit where the game's lighting is recognised) and the pixel shaders below

Vertex programs: rasterizer_xbox_vertex_shaders_data.inc, entries as rasterizer_xbox_vertex_shaders.c lists them
(table.inc, written from that file by the script). Pixel shaders: the state rasterizer_xbox_text.c sets for text
(two combiners, texture 0), the same with fog and an alpha test, with a second texture (3D) and per-pixel lighting,
and a pass-through of the diffuse color (one combiner and the final combiner reading it), which the draw test uses
so that the generated vertex shader's output is what the screen shows.
*/

#include "xgpu.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct xgpu_capabilities xgpu_capabilities = { 0, 0, 0, 0, 0, 0, "300 es" };
int config_boolean(const char *name) { (void)name; return 0; }
const char *config_string(const char *name) { (void)name; return ""; }

static const unsigned long code[] =
{
#include "rasterizer_xbox_vertex_shaders_data.inc"
};
static const struct { unsigned long offset, bytes; } table[] =
{
#include "table.inc"
};
#define PROGRAM_COUNT (sizeof(table) / sizeof(table[0]))

static void write_file(const char *directory, const char *name, char *text)
{
	char path[256];
	FILE *file;

	snprintf(path, sizeof(path), "%s/%s", directory, name);
	file = fopen(path, "w");
	if (!file)
	{
		perror(path);
		exit(1);
	}
	fputs(text, file);
	fclose(file);
	free(text);
}

static void vertex(const char *directory, unsigned index, int packed, int lit)
{
	const DWORD *function = (const DWORD *)((const unsigned char *)code + table[index].offset);
	unsigned long count = function[0] >> 16;
	struct nv2a_vertex_lighting lighting;
	char name[64];

	if (lit && !nv2a_vertex_shader_lighting(function + 1, count, &lighting))
		return;
	snprintf(name, sizeof(name), "vs%02u%s%s.vert", index, packed ? "_packed" : "", lit ? "_lit" : "");
	/* the NORMPACKED3 normal is attribute 2 as a 32-bit integer (the game's model programs) */
	write_file(directory, name, nv2a_vertex_shader_to_glsl(function + 1, count, packed ? 1UL << 2 : 0, lit ? &lighting : NULL));
}

static void text_key(struct nv2a_pixel_shader_key *key)
{
	memset(key, 0, sizeof(*key));
	key->combiner_state[D3DRS_PSALPHAOUTPUTS0] = 0x89;
	key->combiner_state[D3DRS_PSRGBOUTPUTS0] = 0x89;
	key->combiner_state[D3DRS_PSRGBINPUTS0] = 0x08010902;
	key->combiner_state[D3DRS_PSALPHAINPUTS0] = 0x18111912;
	key->combiner_state[D3DRS_PSRGBINPUTS0 + 1] = 0x0A010804;
	key->combiner_state[D3DRS_PSRGBOUTPUTS0 + 1] = 0xAC;
	key->combiner_state[D3DRS_PSALPHAINPUTS0 + 1] = 0x1A111814;
	key->combiner_state[D3DRS_PSALPHAOUTPUTS0 + 1] = 0xAC;
	key->combiner_state[D3DRS_PSCOMBINERCOUNT] = 0x11102;
	key->combiner_state[D3DRS_PSFINALCOMBINERINPUTSABCD] = 12;
	key->combiner_state[D3DRS_PSFINALCOMBINERINPUTSEFG] = 0x1C00;
	key->texture_modes = 1;
	key->sampler_type[0] = _xgpu_sampler_2d;
}

int main(int argc, char **argv)
{
	int all = argc > 2 && !strcmp(argv[1], "--all");
	const char *out = argc > 1 ? argv[argc - 1] : ".";
	struct nv2a_pixel_shader_key key;
	unsigned index;

	if (all)
	{
		for (index = 0; index < PROGRAM_COUNT; index++)
		{
			vertex(out, index, 0, 0);
			vertex(out, index, 1, 0);
			vertex(out, index, 0, 1);
		}
	}
	else
	{
		static const struct { unsigned index; int packed, lit; } chosen[] =
		{
			{ 0, 0, 0 }, { 0, 1, 0 }, { 9, 0, 0 }, { 9, 0, 1 }, { 31, 0, 0 }, { 31, 0, 1 },
		};
		unsigned i;

		for (i = 0; i < sizeof(chosen) / sizeof(chosen[0]); i++)
			vertex(out, chosen[i].index, chosen[i].packed, chosen[i].lit);
	}

	/* text drawing */
	text_key(&key);
	write_file(out, "ps_text.frag", nv2a_pixel_shader_to_glsl(&key));
	key.fog_enable = 1;
	key.fog_table_mode = D3DFOG_LINEAR;
	key.alpha_test_function = D3DCMP_GREATER;
	write_file(out, "ps_text_fog_alphatest.frag", nv2a_pixel_shader_to_glsl(&key));
	key.texture_modes = 1 | (1 << 5);
	key.sampler_type[1] = _xgpu_sampler_3d;
	key.per_pixel_lighting = 1;
	write_file(out, "ps_text_lit_3d.frag", nv2a_pixel_shader_to_glsl(&key));

	/* the diffuse color, as it comes: AB = v0 * 1 into r0, the final combiner's D = r0 (alpha as well) */
	memset(&key, 0, sizeof(key));
	key.combiner_state[D3DRS_PSRGBINPUTS0] = 0x04200000;
	key.combiner_state[D3DRS_PSALPHAINPUTS0] = 0x14300000;
	key.combiner_state[D3DRS_PSRGBOUTPUTS0] = 0xC0;
	key.combiner_state[D3DRS_PSALPHAOUTPUTS0] = 0xC0;
	key.combiner_state[D3DRS_PSCOMBINERCOUNT] = 1;
	key.combiner_state[D3DRS_PSFINALCOMBINERINPUTSABCD] = 0x0C;
	key.combiner_state[D3DRS_PSFINALCOMBINERINPUTSEFG] = 0x1C00;
	write_file(out, "ps_diffuse.frag", nv2a_pixel_shader_to_glsl(&key));
	return 0;
}
