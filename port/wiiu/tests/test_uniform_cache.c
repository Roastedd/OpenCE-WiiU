#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(test) do { if (!(test)) { fprintf(stderr, "uniform check failed at line %u: %s\n", \
	(unsigned)__LINE__, #test); exit(1); } } while (0)

static unsigned touched[76];
#define WIIU_UNIFORM_CACHE_TEST_TOUCH(first) (touched[first]++)
#include "wiiu_uniform_cache.h"
#include "dk_commands.h"

typedef uint32_t DWORD;
typedef uint32_t D3DCOLOR;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define D3DTSS_MAXSTAGES 4
#define DRAW_UNIFORM_INPUT_COUNT 76
#define UI_OFFSET ui_offset
enum
{
	D3DRS_POINTSIZE,
	D3DRS_PSCONSTANT0_0 = 1,
	D3DRS_PSCONSTANT1_0 = 9,
	D3DRS_PSFINALCOMBINERCONSTANT0 = 17,
	D3DRS_PSFINALCOMBINERCONSTANT1,
	D3DRS_FOGCOLOR, D3DRS_FOGSTART, D3DRS_FOGEND,
	D3DRS_FOGDENSITY, D3DRS_ALPHAREF
};
enum { D3DTSS_BUMPENVMAT00, D3DTSS_BUMPENVMAT01, D3DTSS_BUMPENVMAT10,
	D3DTSS_BUMPENVMAT11, D3DTSS_BUMPENVLSCALE, D3DTSS_BUMPENVLOFFSET };
static struct { float viewport_scale[4], viewport_offset[4]; } device;
static DWORD D3D__RenderState[64], D3D__TextureState[4][6];
static float texture_clip[3];
static int32_t ui_offset;

struct packet
{
	union { double alignment; unsigned char bytes[600]; } payload;
	unsigned size;
};
struct recording { struct packet packet[1024]; unsigned count; };
static struct recording reference_record, candidate_record;

static void *record(struct recording *recording, uint32_t type, unsigned long size)
{
	struct packet *packet;
	struct dk_command_header *header;
	assert(recording->count < 1024 && size <= sizeof(recording->packet[0].payload.bytes));
	packet = &recording->packet[recording->count++];
	memset(packet, 0, sizeof(*packet));
	packet->size = (unsigned)size;
	header = (struct dk_command_header *)(void *)packet->payload.bytes;
	header->type = type; header->size = (uint32_t)size;
	return header;
}
static void *reference_command(uint32_t type, unsigned long size) { return record(&reference_record, type, size); }
static void *candidate_command(uint32_t type, unsigned long size) { return record(&candidate_record, type, size); }
static int words_equal(const void *a, const void *b, size_t size) { return !memcmp(a, b, size); }
static float dword_to_float(DWORD word) { float value; memcpy(&value, &word, 4); return value; }
static void color_to_vec4(D3DCOLOR color, float *out)
{
	out[0] = ((color >> 16) & 0xff) / 255.0f;
	out[1] = ((color >> 8) & 0xff) / 255.0f;
	out[2] = (color & 0xff) / 255.0f;
	out[3] = ((color >> 24) & 0xff) / 255.0f;
}

static DWORD legacy_inputs[76];
static BOOL legacy_inputs_known, legacy_parameters_known;
static struct dk_vertex_parameters legacy_vertex;
static struct dk_pixel_parameters legacy_pixel;
#define draw_parameters_send reference_send
#define draw_uniform_inputs legacy_inputs
#define draw_uniform_inputs_known legacy_inputs_known
#define parameters_known legacy_parameters_known
#define vertex_parameters_told legacy_vertex
#define pixel_parameters_told legacy_pixel
#define stream_command reference_command
#include "uniform_legacy.inc"
#undef draw_parameters_send
#undef draw_uniform_inputs
#undef draw_uniform_inputs_known
#undef parameters_known
#undef vertex_parameters_told
#undef pixel_parameters_told
#undef stream_command

/* the current derivation (dk_derive.c: parameter_inputs and derive_parameters, which the GX2 thread runs before a draw; wiiu_gx2.c's derive_apply
takes the changed blocks as the commands did), its inputs taken from the same globals the legacy oracle reads */
#include "dk_derive.h"
struct dk_derive
{
	DWORD rs[64];
	DWORD ts[4][6];
	struct dk_draw_facts facts;
	struct wiiu_uniform_cache uniforms;
};
static struct dk_derive derive_for_test;
#define draw_uniform_cache derive_for_test.uniforms
static BOOL parameters_known;
#include "current_uniform.inc"
static void candidate_send(float texture_scale[4][4])
{
	unsigned changed;

	memcpy(derive_for_test.rs, D3D__RenderState, sizeof(derive_for_test.rs));
	memcpy(derive_for_test.ts, D3D__TextureState, sizeof(derive_for_test.ts));
	memcpy(derive_for_test.facts.viewport_scale, device.viewport_scale, sizeof(device.viewport_scale));
	memcpy(derive_for_test.facts.viewport_offset, device.viewport_offset, sizeof(device.viewport_offset));
	derive_for_test.facts.ui_offset = ui_offset;
	memcpy(derive_for_test.facts.texture_clip, texture_clip, sizeof(texture_clip));
	changed = derive_parameters(&derive_for_test, texture_scale, !parameters_known);
	if (changed & DK_DERIVED_VERTEX_PARAMETERS)
	{
		struct dk_command_vertex_parameters *command = candidate_command(DK_COMMAND_VERTEX_PARAMETERS, sizeof(*command));

		command->parameters = derive_for_test.uniforms.vertex;
	}
	if (changed & DK_DERIVED_PIXEL_PARAMETERS)
	{
		struct dk_command_pixel_parameters *command = candidate_command(DK_COMMAND_PIXEL_PARAMETERS, sizeof(*command));

		command->parameters = derive_for_test.uniforms.pixel;
	}
	parameters_known = TRUE;
}

static uint32_t float_bits(float value) { uint32_t bits; memcpy(&bits, &value, 4); return bits; }
static void input_defaults(uint32_t *input)
{
	unsigned i;
	for (i = 0; i < 76; i++) input[i] = float_bits((float)(i + 1) * 0.125f);
	input[24] = 0;
	for (i = 25; i <= 43; i++) input[i] = UINT32_C(0xa3456789) ^ (i * 65537u);
	input[47] = 127;
	input[48] = (uint32_t)-32;
}
static void input_put(const uint32_t *input, float scale[4][4])
{
	unsigned stage;
	memcpy(device.viewport_scale, input, 16); memcpy(device.viewport_offset, input + 4, 16);
	memcpy(scale, input + 8, 64);
	D3D__RenderState[D3DRS_POINTSIZE] = input[24];
	for (stage = 0; stage < 8; stage++)
	{
		D3D__RenderState[D3DRS_PSCONSTANT0_0 + stage] = input[25 + stage * 2];
		D3D__RenderState[D3DRS_PSCONSTANT1_0 + stage] = input[26 + stage * 2];
	}
	D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT0] = input[41];
	D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT1] = input[42];
	D3D__RenderState[D3DRS_FOGCOLOR] = input[43];
	D3D__RenderState[D3DRS_FOGSTART] = input[44];
	D3D__RenderState[D3DRS_FOGEND] = input[45];
	D3D__RenderState[D3DRS_FOGDENSITY] = input[46];
	D3D__RenderState[D3DRS_ALPHAREF] = input[47];
	memcpy(&ui_offset, input + 48, 4); memcpy(texture_clip, input + 49, 12);
	for (stage = 0; stage < 4; stage++) memcpy(D3D__TextureState[stage], input + 52 + stage * 6, 24);
}
static void check_packets(void)
{
	unsigned i;
	assert(reference_record.count == candidate_record.count);
	for (i = 0; i < reference_record.count; i++)
	{
		assert(reference_record.packet[i].size == candidate_record.packet[i].size);
		assert(!memcmp(reference_record.packet[i].payload.bytes, candidate_record.packet[i].payload.bytes,
			reference_record.packet[i].size));
	}
}
static void step(const uint32_t *input)
{
	float scale[4][4];
	input_put(input, scale);
	memset(touched, 0, sizeof(touched));
	reference_send(scale); candidate_send(scale);
	check_packets();
	assert(!memcmp(draw_uniform_cache.input, input, 76 * 4));
	assert(!memcmp(&draw_uniform_cache.vertex, &legacy_vertex, sizeof(legacy_vertex)));
	assert(!memcmp(&draw_uniform_cache.pixel, &legacy_pixel, sizeof(legacy_pixel)));
}
static unsigned touches(void)
{
	unsigned i, total = 0;
	for (i = 0; i < 76; i++) total += touched[i];
	return total;
}

int main(void)
{
	uint32_t original[76], input[76];
	unsigned i, n, count;
	/* Unknown GPU state needs both blocks even when the entire pixel block
	   converts to zero and the CPU cache starts as static zero storage. */
	memset(input, 0, sizeof(input)); step(input);
	assert(candidate_record.count == 2);
	reference_record.count = candidate_record.count = 0;
	input_defaults(original); memcpy(input, original, sizeof(input));
	step(input); assert(candidate_record.count == 2 && touches() > 0);
	step(input); assert(candidate_record.count == 2 && !touches());
	for (i = 0; i < 76; i++)
	{
		input[i] ^= UINT32_C(0x00000100);
		step(input); assert(touches() == 1);
		step(input); assert(!touches());
		input[i] = original[i]; step(input); assert(touches() == 1);
	}
	/* Same GPU result despite a different raw input: no redundant command. */
	count = candidate_record.count;
	input[47] ^= UINT32_C(0x80000000); step(input); assert(candidate_record.count == count);
	input[47] = original[47]; step(input); assert(candidate_record.count == count);
	input[24] = float_bits(1.0f); step(input); assert(candidate_record.count == count);
	input[24] = 0; step(input); assert(candidate_record.count == count);
	input[48] = UINT32_C(0x01000000); step(input); count = candidate_record.count;
	input[48]++; step(input); assert(candidate_record.count == count);
	input[48] = original[48]; step(input);
	/* Bitwise equality preserves zeros and NaN payloads. */
	{
		static const uint32_t values[] = { 0, 0x80000000u, 0x7fc12345u, 0x7fc12346u, 0xffc12345u,
			0x7f812345u, 0xff812345u,
			0x3f800000u, 0xbf800000u, 0x00000001u };
		static const unsigned words[] = { 0, 4, 8, 24, 44, 49, 52, 56 };
		for (n = 0; n < sizeof(words) / sizeof(words[0]); n++)
			for (i = 0; i < sizeof(values) / sizeof(values[0]); i++)
			{
				input[words[n]] = values[i]; step(input);
				step(input); assert(!touches());
			}
	}
	memcpy(input, original, sizeof(input)); step(input);
	/* All 256 byte values and asymmetric channels keep legacy division. */
	for (i = 0; i < 256; i++)
	{
		input[25] = i << 24 | ((i + 37) & 255u) << 16 | ((i + 91) & 255u) << 8 | (255u - i);
		step(input); assert(touches() <= 1);
	}
	/* CPU-only key forgetting can rebuild without sending identical data. */
	count = candidate_record.count;
	draw_uniform_cache.valid = 0; legacy_inputs_known = FALSE;
	step(input); assert(candidate_record.count == count && touches() > 0);
	/* A discarded/forgotten GPU batch must resend even on a CPU cache hit.
	   The old coupled oracle is explicitly invalidated to request a rebuild. */
	parameters_known = legacy_parameters_known = FALSE; legacy_inputs_known = FALSE;
	count = candidate_record.count;
	step(input); assert(candidate_record.count == count + 2 && !touches());
	/* The old queued command bytes own their values across later mutation. */
	{
		struct packet saved = candidate_record.packet[0];
		input[25] ^= UINT32_C(0x0f0f0f0f); input[4] = float_bits(-72.0f);
		step(input);
		assert(!memcmp(&saved, &candidate_record.packet[0], sizeof(saved)));
	}
	/* Frame/command-arena reset alone does not forget known GPU state. */
	reference_record.count = candidate_record.count = 0;
	step(input); assert(!candidate_record.count && !touches());
	puts("uniform cache: 76-word mutations, exact payloads, group reuse, aliases, NaN/zero, CPU/GPU resets and owned packets passed");
	return 0;
}
