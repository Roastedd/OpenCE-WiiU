/*
WIIU_GX2.C

The Wii U's half of the renderer: runs the command stream the Direct3D 8 device (d3d8_gx2.c) writes, on the GPU through GX2. The device and the
stream's definition (dk_commands.h) come from the Switch port's deko3d renderer (port/switch/DEKO3D.md), whose host half this takes the place of;
there is no guest/host split here, only the two ABIs (the device is built with the Xbox's, this with the console's), and every field of the stream
is a 32 bit integer or float, so that they agree.

What it does so far (phase 2 of that design): render targets, clearing them, and presenting the back buffer to the TV and the GamePad, letterboxed.
Draws, textures, state, shaders and visibility tests are accepted and counted but not made (the log says how many, once a second).

It also answers the platform layer's few calls about the screen (wiiu_platform below): the display is the TV's, and the game's back buffer is drawn at
the TV's size, so that presenting it is a blit.

Not run on a console.
*/

#include "dk_commands.h"
#include "dk_derive.h"
#include "wiiu_debug.h"
#include "wiiu_shell.h"
#include "port_config.h"
#include "wiiu_indices.h"
#include "wiiu_mip.h"
#include "wiiu_overlay.h"
#include "wiiu_stream_queue.h"

/* d3d8_gx2.c: the queue of streams and its buffers (halo/gx2_thread.txt 7) */
extern struct stream_queue dk_stream_queue;
uint32_t *dk_stream_buffer(unsigned slot);
#include "wiiu_tile.h"

#include <coreinit/cache.h>
#include <coreinit/core.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/semaphore.h>
#include <proc_ui/procui.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <gx2/clear.h>
#include <gx2/context.h>
#include <gx2/enum.h>
#include <gx2/draw.h>
#include <gx2/display.h>
#include <gx2/event.h>
#include <gx2/swap.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/sampler.h>
#include <gx2/shaders.h>
#include <gx2/state.h>
#include <gx2/surface.h>
#include <gx2r/surface.h>
#include <gx2/texture.h>
#include <gx2/utils.h>
#include <whb/gfx.h>

#include <stdint.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const unsigned char wiiu_gx2_blit_gsh[];
extern const unsigned long wiiu_gx2_blit_gsh_size;

/* the shell's (wiiu_shell.c): holds the game thread while the app is in the background */
void wiiu_foreground_gate(int is_game_thread);

#define TARGET_LIMIT 64
#define SHADER_LIMIT 8192
#define FETCH_LIMIT 2048
#define RING_SEGMENTS 2
#define RING_SEGMENT_BYTES (24u * 1024u * 1024u)
#define DEFERRED_LIMIT 512
/* the attribute buffer that holds the registers the declaration does not feed (stride 0) */
#define CONSTANTS_BUFFER 15
#define SURFACE_PIXEL_WIDTH(surface) ((surface)->pixel_width ? (surface)->pixel_width : (surface)->width)
#define SURFACE_PIXEL_HEIGHT(surface) ((surface)->pixel_height ? (surface)->pixel_height : (surface)->height)
/* the part of a target the frame draws into, from its top left: all of it, unless dynamic resolution has lowered the screen's lines (dk_surface) */
#define SURFACE_ACTIVE_WIDTH(surface) \
	((surface)->active_width && (surface)->active_width < SURFACE_PIXEL_WIDTH(surface) ? (surface)->active_width : SURFACE_PIXEL_WIDTH(surface))
#define SURFACE_ACTIVE_HEIGHT(surface) \
	((surface)->active_height && (surface)->active_height < SURFACE_PIXEL_HEIGHT(surface) ? (surface)->active_height : SURFACE_PIXEL_HEIGHT(surface))

struct target
{
	struct dk_surface surface;
	GX2ColorBuffer color;
	GX2DepthBuffer depth;
	GX2Texture texture; /* a color target as the blit samples it */
	unsigned last_used;
	int in_mem1; /* its image is in the eDRAM (MEM1), which the system takes while the app is in the background: made again by mem1_restore */
	int unusable; /* no memory for it after HOME: nothing is drawn into it or from it (target_get and target_latest leave it out) */
	OSTime remade_at;
};

struct blit_vertex
{
	float x, y, u, v, r, g, b, a;
};

/* frames whose GPU time is being measured at once (the samples of one are read a frame or two after it is handed over) */
#define GPU_SAMPLE_SLOTS 4

static struct
{
	int ready;
	int failed;
	struct target targets[TARGET_LIMIT];
	unsigned target_count;
	struct target *color;
	struct target *depth;
	WHBGfxShaderGroup blit;
	GX2Sampler sampler;
	uint32_t sampler_location;
	struct blit_vertex *quad;
	unsigned frames;
	OSTime told;
	unsigned counts[32];
	unsigned counted_draws;
	unsigned partial_clears, rectangle_clears;

	/* what the stream has set, for the draws that follow */
	struct dk_draw_state state;
	int state_dirty;
	float constants[192][4];
	unsigned constants_lo, constants_hi;
	struct dk_vertex_parameters vertex_parameters;
	struct dk_pixel_parameters pixel_parameters;
	int vertex_parameters_dirty, pixel_parameters_dirty;
	uint32_t vs_handle, ps_handle;
	unsigned long shader_key_misses; /* key changes whose shader the pack has not (the draws after it skipped until the next change) */
	struct dk_vertex_format format;
	int format_set;
	struct dk_stage_texture stages[4];
	GX2Sampler samplers[4];
	GX2Texture white;
	/* what the GPU's registers hold */
	const struct shader *regs_vs, *regs_ps, *cur_vs, *cur_ps;
	const struct fetch *cur_fetch;
	unsigned use_serial;

	struct ring_segment
	{
		uint8_t *base;
		uint32_t offset;
		OSTime stamp;
	} ring[RING_SEGMENTS];
	unsigned ring_current;
	struct
	{
		void *memory;
		unsigned frame;
	} deferred[DEFERRED_LIMIT];
	unsigned deferred_count;

	unsigned long ring_bytes, ring_calls, ring_largest, index_bytes, dynamic_streams;
	unsigned long frame_buckets[4]; /* frames since the last log note that took one refresh of the display, two, three, more */
	OSTime draw_started, section_now;
	OSTime section_ticks[7];
	unsigned long counts_state, counts_fetch, counts_vs, counts_ps, counts_vertex_regs, counts_pixel_regs, counts_texture_sets, counts_draw_calls; /* of the draw's: its copies of data, state, shaders, uniforms, textures and attribute buffers, the draw call */
	unsigned ring_what; /* what the next ring_copy copies: 0 a stream, 1 immediate mode's vertices, 2 constants, 3 indices */
	unsigned long ring_kind_bytes[4], ring_kind_calls[4];
	void *ring_dirty_from, *ring_dirty_to;
	unsigned shaders_loaded, fetch_full;
	unsigned draws_made, skipped_target, skipped_state, skipped_shader, skipped_ring, ring_overflows;
	unsigned skipped_missing; /* of skipped_state, the draws whose shader (vertex or pixel) the pack has not: a key in the shader-misses files */
	unsigned textures_made, textures_unsupported, texture_short, layouts_logged;
	unsigned texture_log;
	unsigned sampler_levels[4];
	struct { const void *buffer; uint32_t size, stride; } attrib[16]; /* what the attribute buffer registers hold now */
	const GX2Texture *bound_texture[4]; /* what the stages hold now (static images only: a render target is bound, and flushed, each draw) */
	int bound_location[4];
	int stage_dynamic;
	int samplers_changed[4];
	unsigned format_serial, memo_serial;
	const struct shader *memo_vs;
	struct fetch *memo_fetch;
	void *constants_ring;
	unsigned constants_frame, constants_serial;
	int sampler_valid[4];
	OSTime started;
	OSTime shader_ticks, blit_ticks, finish_ticks, wait_ticks;
	int screens_enabled;
	/* the performance overlay (halo/overlay.txt: 0 is off): its numbers, the texture of its text and graph, and the GPU's time of a frame, found by when the timestamp of the work
	submitted for it is retired */
	struct overlay overlay;
	int overlay_on; /* its numbers are taken (and logged every 5 s) */
	volatile int overlay_shown; /* and it is drawn: halo/overlay.txt 1, or L + R + - on the GamePad (wiiu_overlay_toggle) */
	GX2Texture overlay_texture;
	GX2Sampler overlay_sampler;
	struct blit_vertex *overlay_quad;
	OSTime overlay_previous_end, overlay_view_time;
	unsigned long ring_total, draw_total, overlay_ring_previous, overlay_draw_previous;
	/* the GPU's own time for a frame's draws (gpu_frame_begin, gpu_frame_end, gpu_poll): the slot of samples in use, each slot's state (0 free, 1 begun,
	2 ended and not yet read) and when it ended; the frame now drawn is measured (1) or not (2: no free slot); the five seconds' sum and longest */
	unsigned gpu_slot;
	int gpu_frame_open;
	uint8_t gpu_slot_state[GPU_SAMPLE_SLOTS];
	OSTime gpu_slot_ended[GPU_SAMPLE_SLOTS];
	OSTime gpu_window_ticks, gpu_window_longest;
	unsigned gpu_window_frames;
	float gpu_ms;
	unsigned overlay_frame;
	/* the GPU test (halo/gpu_test.txt: 1): a frame's streams kept (test_armed 2), run again at its present, timed */
	int test_armed, replaying;
	uint8_t *test_log;
	uint32_t test_log_used;
	uint32_t test_ring_start;
	unsigned last_frame_draws;
	unsigned long test_draw_mark;
	OSTime submit_ticks, present_ticks, texture_ticks, draw_ticks;
	unsigned window_frames;
	OSTime last_present, longest_frame;
	unsigned slow_frames; /* frames of the window that took more than 40 ms */
	double exit_after;
	int hiz; /* halo/hiz.txt: 1, the big depth target has hierarchical Z */
	unsigned fail_allocations; /* halo/mem1_fail.txt: that many allocations of targets made again after HOME fail (a test) */
	int gpu_test_wanted; /* halo/gpu_test.txt "1 [DRAWS]", read at the start: the GPU test runs once the game draws DRAWS (250) or more a frame */
	int gpu_test_draws;
	volatile uint32_t mem1_restoring; /* a thread is making the targets again (the acquire callback, or the GX2 thread after 3 s): one at a time */
	volatile int target_missing; /* target_get answered NULL for a target that has no memory (targets_bind then binds neither) */
	int mem1; /* halo/mem1.txt: 1, the big render targets are made in the eDRAM (MEM1) instead of the DDR3 */
	volatile int mem1_lost; /* HOME: the eDRAM's targets are gone until mem1_restore */
	volatile int resume_needed;
	unsigned render_height; /* the lines the game draws at (halo/render_height.txt, 540 if none): the TV's 1080 lines cost 27 ms a frame in the menu */
	/* Dynamic resolution: the lines the game thread draws its next frames at (host_gx2_drs_lines; render_height, the size of the screen's targets,
	or fewer), the lines halo/drs_lines.txt forces for the run (0: none), and the targets cleared whole because a step changed (target_step) */
	volatile unsigned drs_lines;
	unsigned drs_forced;
	unsigned drs_clears;
	int trace_frame; /* halo/gx2_trace.txt: the frame whose draws are logged (-1: none) */
	int debug_mode; /* and, after it, a mode: 1 draws without blending, 2 without culling */
	int features_off; /* and a third number: bit 0 no mip levels, bit 1 no 3D and cube textures, bit 2 no mip composites, bit 3 visibility tests on, bit 4 samplers ignore mips,
	bit 5 samplers without anisotropy (these two take effect when the file is read again), bit 6 no mip levels made for composites' unrendered levels */
	unsigned traced;
	int trace_serial; /* the fourth number: a new one traces the frame after the file is read again (every 300 frames) */
} gx;

static void *gx_alloc_raw(uint32_t size, uint32_t alignment)
{
	return MEMAllocFromDefaultHeapEx(size, (int32_t)alignment);
}

static void *gx_alloc(uint32_t size, uint32_t alignment)
{
	void *memory = MEMAllocFromDefaultHeapEx(size, (int32_t)alignment);

	if (memory)
		memset(memory, 0, size);
	return memory;
}

/* ---------- shaders: the pack's, loaded when a draw first needs one */

/* gx2_shaders.c: where in the pack the shader of a handle is */
const void *dk_shader_blob(uint32_t handle, uint32_t *size, uint32_t *stage);

enum
{
	VS_C,
	VS_VIEWPORT_SCALE,
	VS_VIEWPORT_OFFSET,
	VS_POINT_SIZE,
	VS_SCREEN_OFFSET,
	VS_UNIFORMS,
};

enum
{
	PS_C0,
	PS_C1,
	PS_FINAL_C0,
	PS_FINAL_C1,
	PS_FOG_COLOR,
	PS_FOG_PARAMETERS,
	PS_ALPHA_REFERENCE,
	PS_BUMP_MATRIX,
	PS_BUMP_LUMINANCE,
	PS_TEXTURE_SCALE,
	PS_TEXTURE_LOD_BIAS,
	PS_UNIFORMS,
};

static const char *const vertex_uniform_names[VS_UNIFORMS] = { "c", "viewport_scale", "viewport_offset", "point_size", "screen_offset" };
static const char *const pixel_uniform_names[PS_UNIFORMS] = { "ps_c0", "ps_c1", "ps_final_c0", "ps_final_c1", "fog_color", "fog_parameters",
	"alpha_reference", "bump_matrix", "bump_luminance", "texture_scale", "texture_lod_bias" };

struct shader
{
	int state; /* 0: not tried, 1: ready, 2: failed */
	uint32_t stage;
	GX2VertexShader *vertex;
	GX2PixelShader *pixel;
	int uniform[PS_UNIFORMS]; /* the register offset (in floats) of each, -1: the shader does not use it */
	uint32_t uniform_floats[PS_UNIFORMS]; /* and how many floats it has room for */
	int16_t *elements[PS_UNIFORMS]; /* for an array, where each element is (-1: none): the compiler gives the elements it uses registers of their own, not always in a row */
	int sampler[4]; /* the location of tex0..3, -1: not sampled */
	uint32_t attribute_used; /* bit n: the vertex shader reads input location n */
};

static struct shader *shader_table[SHADER_LIMIT];

static uint32_t type_floats(GX2ShaderVarType type)
{
	switch (type)
	{
	case GX2_SHADER_VAR_TYPE_FLOAT2: return 2;
	case GX2_SHADER_VAR_TYPE_FLOAT3: return 3;
	case GX2_SHADER_VAR_TYPE_FLOAT: return 1;
	default: return 4;
	}
}

static void uniforms_resolve(struct shader *shader, const GX2UniformVar *vars, uint32_t count, const char *const *names, unsigned name_count)
{
	unsigned n;
	uint32_t i;

	for (n = 0; n < name_count; n++)
	{
		const size_t length = strlen(names[n]);

		shader->uniform[n] = -1;
		for (i = 0; i < count; i++)
		{
			const char *name = vars[i].name;

			if (strncmp(name, names[n], length) != 0)
				continue;
			if (name[length] == 0)
			{
				if (shader->uniform[n] < 0)
				{
					shader->uniform[n] = (int)vars[i].offset;
					shader->uniform_floats[n] = vars[i].count * type_floats(vars[i].type);
				}
			}
			else if (name[length] == '[')
			{
				const unsigned element = (unsigned)strtoul(name + length + 1, NULL, 10);

				if (element < 192)
				{
					if (!shader->elements[n])
					{
						unsigned e;

						shader->elements[n] = malloc(192 * sizeof(int16_t));
						if (!shader->elements[n])
							continue;
						for (e = 0; e < 192; e++)
							shader->elements[n][e] = -1;
					}
					shader->elements[n][element] = (int16_t)vars[i].offset;
				}
			}
		}
	}
}

static struct shader *shader_get(uint32_t handle)
{
	struct shader *shader;
	const void *blob;
	uint32_t size = 0, stage = 0, i;

	if (!handle || handle >= SHADER_LIMIT)
		return NULL;
	shader = shader_table[handle];
	if (shader)
		return shader->state == 1 ? shader : NULL;
	shader = calloc(1, sizeof(*shader));
	if (!shader)
		return NULL;
	shader_table[handle] = shader;
	shader->state = 2;
	for (i = 0; i < 4; i++)
		shader->sampler[i] = -1;
	blob = dk_shader_blob(handle, &size, &stage);
	if (!blob)
		return NULL;
	shader->stage = stage;
	if (stage == DK_SHADER_STAGE_VERTEX)
	{
		GX2VertexShader *vertex = WHBGfxLoadGFDVertexShader(0, blob);

		if (!vertex)
		{
			wiiu_log("GX2: vertex shader %u (%u bytes) did not load", (unsigned)handle, (unsigned)size);
			return NULL;
		}
		shader->vertex = vertex;
		uniforms_resolve(shader, vertex->uniformVars, vertex->uniformVarCount, vertex_uniform_names, VS_UNIFORMS);
		for (i = 0; i < vertex->attribVarCount; i++)
			if (vertex->attribVars[i].location < 32)
				shader->attribute_used |= 1u << vertex->attribVars[i].location;
		if (gx.shaders_loaded < 4)
		{
			wiiu_log("GX2: vertex shader %u: mode %u, %u uniforms, %u attributes (mask %08x), c at %d (%u floats), scale %d, offset %d, point %d, screen %d",
				(unsigned)handle, (unsigned)vertex->mode, (unsigned)vertex->uniformVarCount, (unsigned)vertex->attribVarCount,
				(unsigned)shader->attribute_used, shader->uniform[VS_C], (unsigned)shader->uniform_floats[VS_C], shader->uniform[VS_VIEWPORT_SCALE],
				shader->uniform[VS_VIEWPORT_OFFSET], shader->uniform[VS_POINT_SIZE], shader->uniform[VS_SCREEN_OFFSET]);
			for (i = 0; i < vertex->uniformVarCount && i < 8; i++)
				wiiu_log("GX2:   uniform %s: type %u, count %u, offset %u, block %d", vertex->uniformVars[i].name,
					(unsigned)vertex->uniformVars[i].type, (unsigned)vertex->uniformVars[i].count, (unsigned)vertex->uniformVars[i].offset,
					(int)vertex->uniformVars[i].block);
		}
	}
	else
	{
		GX2PixelShader *pixel = WHBGfxLoadGFDPixelShader(0, blob);

		if (!pixel)
		{
			wiiu_log("GX2: pixel shader %u (%u bytes) did not load", (unsigned)handle, (unsigned)size);
			return NULL;
		}
		shader->pixel = pixel;
		uniforms_resolve(shader, pixel->uniformVars, pixel->uniformVarCount, pixel_uniform_names, PS_UNIFORMS);
		for (i = 0; i < pixel->samplerVarCount; i++)
		{
			const char *name = pixel->samplerVars[i].name;

			if (name[0] == 't' && name[1] == 'e' && name[2] == 'x' && name[3] >= '0' && name[3] <= '3')
				shader->sampler[name[3] - '0'] = (int)pixel->samplerVars[i].location;
		}
		if (gx.shaders_loaded < 4)
		{
			wiiu_log("GX2: pixel shader %u: mode %u, %u uniforms, %u samplers", (unsigned)handle, (unsigned)pixel->mode,
				(unsigned)pixel->uniformVarCount, (unsigned)pixel->samplerVarCount);
			for (i = 0; i < pixel->uniformVarCount && i < 12; i++)
				wiiu_log("GX2:   uniform %s: type %u, count %u, offset %u, block %d", pixel->uniformVars[i].name, (unsigned)pixel->uniformVars[i].type,
					(unsigned)pixel->uniformVars[i].count, (unsigned)pixel->uniformVars[i].offset, (int)pixel->uniformVars[i].block);
		}
	}
	gx.shaders_loaded++;
	shader->state = 1;
	return shader;
}

/* ---------- vertex formats: a fetch shader for each (vertex shader, format) */

struct attribute_spec
{
	GX2AttribFormat format;
	uint32_t mask;
	GX2EndianSwapMode swap;
};

#define ATTRIB_INTEGER_32 ((GX2AttribFormat)(GX2_ATTRIB_FLAG_INTEGER | GX2_ATTRIB_TYPE_32))
#define ATTRIB_SSCALED_16 ((GX2AttribFormat)(GX2_ATTRIB_FLAG_SIGNED | GX2_ATTRIB_FLAG_SCALED | GX2_ATTRIB_TYPE_16))
#define ATTRIB_SSCALED_16_16 ((GX2AttribFormat)(GX2_ATTRIB_FLAG_SIGNED | GX2_ATTRIB_FLAG_SCALED | GX2_ATTRIB_TYPE_16_16))
#define ATTRIB_SSCALED_16x4 ((GX2AttribFormat)(GX2_ATTRIB_FLAG_SIGNED | GX2_ATTRIB_FLAG_SCALED | GX2_ATTRIB_TYPE_16_16_16_16))
#define ATTRIB_SNORM_16 ((GX2AttribFormat)(GX2_ATTRIB_FLAG_SIGNED | GX2_ATTRIB_TYPE_16))
#define ATTRIB_SNORM_16_16 ((GX2AttribFormat)(GX2_ATTRIB_FLAG_SIGNED | GX2_ATTRIB_TYPE_16_16))
#define ATTRIB_SNORM_16x4 ((GX2AttribFormat)(GX2_ATTRIB_FLAG_SIGNED | GX2_ATTRIB_TYPE_16_16_16_16))

#define SELECT(x, y, z, w) GX2_SEL_MASK(GX2_SQ_SEL_##x, GX2_SQ_SEL_##y, GX2_SQ_SEL_##z, GX2_SQ_SEL_##w)

/* Big-endian data (the CPU's, the converted BSPs'): 32 bit values are swapped as words, 16 bit ones as halves, and four bytes (a D3DCOLOR, a word 0xAARRGGBB)
as a word, which puts blue first. Little-endian data (the models' vertices, as the Xbox had them): no swap, and a D3DCOLOR's bytes are blue first as they are. */
static int attribute_spec(uint32_t dk, int little_endian, struct attribute_spec *spec)
{
	const GX2EndianSwapMode word = little_endian ? GX2_ENDIAN_SWAP_NONE : GX2_ENDIAN_SWAP_8_IN_32;
	const GX2EndianSwapMode half = little_endian ? GX2_ENDIAN_SWAP_NONE : GX2_ENDIAN_SWAP_8_IN_16;

	switch (dk)
	{
	case DK_ATTRIBUTE_FLOAT1: *spec = (struct attribute_spec){ GX2_ATTRIB_FORMAT_FLOAT_32, SELECT(X, 0, 0, 1), word }; return 1;
	case DK_ATTRIBUTE_FLOAT2: *spec = (struct attribute_spec){ GX2_ATTRIB_FORMAT_FLOAT_32_32, SELECT(X, Y, 0, 1), word }; return 1;
	case DK_ATTRIBUTE_FLOAT3: *spec = (struct attribute_spec){ GX2_ATTRIB_FORMAT_FLOAT_32_32_32, SELECT(X, Y, Z, 1), word }; return 1;
	case DK_ATTRIBUTE_FLOAT4: *spec = (struct attribute_spec){ GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32, SELECT(X, Y, Z, W), word }; return 1;
	case DK_ATTRIBUTE_COLOR: *spec = (struct attribute_spec){ GX2_ATTRIB_FORMAT_UNORM_8_8_8_8, SELECT(Z, Y, X, W), word }; return 1;
	case DK_ATTRIBUTE_SHORT1: *spec = (struct attribute_spec){ ATTRIB_SSCALED_16, SELECT(X, 0, 0, 1), half }; return 1;
	case DK_ATTRIBUTE_SHORT2: *spec = (struct attribute_spec){ ATTRIB_SSCALED_16_16, SELECT(X, Y, 0, 1), half }; return 1;
	case DK_ATTRIBUTE_SHORT3: *spec = (struct attribute_spec){ ATTRIB_SSCALED_16x4, SELECT(X, Y, Z, 1), half }; return 1;
	case DK_ATTRIBUTE_SHORT4: *spec = (struct attribute_spec){ ATTRIB_SSCALED_16x4, SELECT(X, Y, Z, W), half }; return 1;
	case DK_ATTRIBUTE_NORMSHORT1: *spec = (struct attribute_spec){ ATTRIB_SNORM_16, SELECT(X, 0, 0, 1), half }; return 1;
	case DK_ATTRIBUTE_NORMSHORT2: *spec = (struct attribute_spec){ ATTRIB_SNORM_16_16, SELECT(X, Y, 0, 1), half }; return 1;
	case DK_ATTRIBUTE_NORMSHORT3: *spec = (struct attribute_spec){ ATTRIB_SNORM_16x4, SELECT(X, Y, Z, 1), half }; return 1;
	case DK_ATTRIBUTE_NORMSHORT4: *spec = (struct attribute_spec){ ATTRIB_SNORM_16x4, SELECT(X, Y, Z, W), half }; return 1;
	case DK_ATTRIBUTE_BYTE1: *spec = (struct attribute_spec){ GX2_ATTRIB_FORMAT_UNORM_8, SELECT(X, 0, 0, 1), GX2_ENDIAN_SWAP_NONE }; return 1;
	case DK_ATTRIBUTE_BYTE2: *spec = (struct attribute_spec){ GX2_ATTRIB_FORMAT_UNORM_8_8, SELECT(X, Y, 0, 1), GX2_ENDIAN_SWAP_NONE }; return 1;
	case DK_ATTRIBUTE_BYTE3: *spec = (struct attribute_spec){ GX2_ATTRIB_FORMAT_UNORM_8_8_8_8, SELECT(X, Y, Z, 1), GX2_ENDIAN_SWAP_NONE }; return 1;
	case DK_ATTRIBUTE_BYTE4: *spec = (struct attribute_spec){ GX2_ATTRIB_FORMAT_UNORM_8_8_8_8, SELECT(X, Y, Z, W), GX2_ENDIAN_SWAP_NONE }; return 1;
	case DK_ATTRIBUTE_PACKED: *spec = (struct attribute_spec){ ATTRIB_INTEGER_32, SELECT(X, 0, 0, 1), word }; return 1;
	default: return 0;
	}
}

/* Independent experiment: identity is the complete input to the fixed
fetch-shader initializer, not the identity of the vertex program. */
struct fetch_attribute_key
{
	uint32_t words[8];
};

/* The public WUT stream has eight contiguous 32-bit fields. Keys own all
of those bytes; no compiler padding, shader identity or borrowed data enters
identity. These checks deliberately reject a changed input ABI. */
typedef char fetch_key_size[(sizeof(struct fetch_attribute_key)==32 && sizeof(GX2AttribStream)==32)?1:-1];
typedef char fetch_stream_offsets[(offsetof(GX2AttribStream,location)==0 && offsetof(GX2AttribStream,buffer)==4 &&
 offsetof(GX2AttribStream,offset)==8 && offsetof(GX2AttribStream,format)==12 && offsetof(GX2AttribStream,type)==16 &&
 offsetof(GX2AttribStream,aluDivisor)==20 && offsetof(GX2AttribStream,mask)==24 && offsetof(GX2AttribStream,endianSwap)==28)?1:-1];

struct fetch
{
	uint32_t key;
	uint16_t next, count; /* table slot plus one; zero ends a bucket */
	GX2FetchShader shader;
	void *program;
	struct fetch_attribute_key attributes[];
};

#define FETCH_BUCKETS 256
typedef char fetch_bucket_count_is_power_of_two[(FETCH_BUCKETS & (FETCH_BUCKETS - 1)) == 0 ? 1 : -1];
typedef char fetch_slots_fit[FETCH_LIMIT < 65536 ? 1 : -1];

static struct fetch *fetch_table[FETCH_LIMIT];
static uint16_t fetch_heads[FETCH_BUCKETS];
static unsigned fetch_count;

static void fetch_attribute_key_make(struct fetch_attribute_key *out, const GX2AttribStream *attribute)
{
	memcpy(out, attribute, sizeof(*out));
}

/* A word hash narrows the search; collisions never identify a layout. */
static uint32_t fetch_layout_hash(const GX2AttribStream *attributes, unsigned count)
{
	uint32_t hash = (2166136261u ^ count) * 16777619u;
	unsigned index;

	/* Only a bucket filter: type/divisor and separate mask/swap identity are
	checked by the complete owned key even when this filter collides. */
	for (index = 0; index < count; index++)
	{
		const GX2AttribStream *attribute = &attributes[index];

		hash = (hash ^ (attribute->location | (attribute->buffer << 8))) * 16777619u;
		hash = (hash ^ attribute->offset) * 16777619u;
		hash = (hash ^ (uint32_t)attribute->format) * 16777619u;
		hash = (hash ^ (attribute->mask ^ (uint32_t)attribute->endianSwap)) * 16777619u;
	}
	hash ^= hash >> 16;
	hash ^= hash >> 8;
	return hash;
}

static int fetch_layout_equal(const struct fetch *entry, const GX2AttribStream *attributes, unsigned count)
{
	unsigned index;

	if (entry->count != count)
		return 0;
	for (index = 0; index < count; index++)
	{
		const uint32_t *words = entry->attributes[index].words;
		const GX2AttribStream *attribute = &attributes[index];

		if (words[0] != attribute->location || words[1] != attribute->buffer || words[2] != attribute->offset ||
			words[3] != (uint32_t)attribute->format || words[4] != (uint32_t)attribute->type || words[5] != attribute->aluDivisor ||
			words[6] != attribute->mask || words[7] != (uint32_t)attribute->endianSwap)
			return 0;
	}
	return 1;
}

static unsigned attributes_make(const struct shader *vs, GX2AttribStream *out)
{
	unsigned location, count = 0;

	for (location = 0; location < DK_ATTRIBUTE_COUNT; location++)
	{
		const struct dk_attribute *attribute = &gx.format.attributes[location];
		struct attribute_spec spec;
		GX2AttribStream *stream;

		if (!(vs->attribute_used & (1u << location)))
			continue;
		stream = &out[count++];
		memset(stream, 0, sizeof(*stream));
		stream->location = location;
		stream->type = GX2_ATTRIB_INDEX_PER_VERTEX;
		if (attribute_spec(attribute->format, (gx.format.little_endian >> attribute->stream) & 1, &spec))
		{
			stream->buffer = attribute->stream;
			stream->offset = attribute->offset;
		}
		else
		{
			/* a register the declaration does not feed: its current value, from the constants' stream (stride 0) */
			stream->buffer = CONSTANTS_BUFFER;
			stream->offset = location * 16;
			if (attribute->format == DK_ATTRIBUTE_UNFED_PACKED)
				attribute_spec(DK_ATTRIBUTE_PACKED, 0, &spec);
			else
				attribute_spec(DK_ATTRIBUTE_FLOAT4, 0, &spec);
		}
		stream->format = spec.format;
		stream->mask = spec.mask;
		stream->endianSwap = spec.swap;
	}
	return count;
}

static struct fetch *fetch_get_layout(const GX2AttribStream *attributes, unsigned count)
{
	struct fetch *fetch;
	uint32_t key, bucket, size;
	unsigned slot, remaining = FETCH_LIMIT, index;

	if (count > DK_ATTRIBUTE_COUNT)
		return NULL;
	key = fetch_layout_hash(attributes, count);
	bucket = key & (FETCH_BUCKETS - 1);
	for (slot = fetch_heads[bucket]; slot && remaining; slot = fetch->next, remaining--)
	{
		/* Links are private to this consumer and published only after a
		complete allocation. Keep even a damaged chain within the table. */
		if (slot > fetch_count)
			return NULL;
		fetch = fetch_table[slot - 1];
		if (fetch->key == key && fetch_layout_equal(fetch, attributes, count))
			return fetch;
	}
	if (slot) /* a cycle cannot be produced by this cache */
		return NULL;
	if (fetch_count == FETCH_LIMIT)
	{
		if (!gx.fetch_full)
			wiiu_log("GX2: more than %d fetch shaders", FETCH_LIMIT);
		gx.fetch_full = 1;
		return NULL;
	}
	fetch = calloc(1, sizeof(*fetch) + count * sizeof(fetch->attributes[0]));
	if (!fetch)
		return NULL;
	size = GX2CalcFetchShaderSizeEx(count, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
	fetch->program = gx_alloc(size, GX2_SHADER_PROGRAM_ALIGNMENT);
	if (!fetch->program)
	{
		free(fetch);
		return NULL;
	}
	GX2InitFetchShaderEx(&fetch->shader, (uint8_t *)fetch->program, count, attributes, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, fetch->program, size);
	fetch->key = key;
	fetch->count = (uint16_t)count;
	for (index = 0; index < count; index++)
		fetch_attribute_key_make(&fetch->attributes[index], &attributes[index]);
	fetch->next = fetch_heads[bucket];
	fetch_table[fetch_count++] = fetch;
	fetch_heads[bucket] = (uint16_t)fetch_count;
	return fetch;
}

static struct fetch *fetch_get(const struct shader *vs)
{
	GX2AttribStream attributes[DK_ATTRIBUTE_COUNT];
	unsigned count = attributes_make(vs, attributes);

	return fetch_get_layout(attributes, count);
}


/* ---------- vertex data: copied into a ring the GPU reads, a segment a frame */

static void *ring_alloc(uint32_t size)
{
	const uint32_t aligned = (size + 63u) & ~63u;
	struct ring_segment *segment = &gx.ring[gx.ring_current];

	if (!segment->base || segment->offset + aligned > RING_SEGMENT_BYTES)
	{
		gx.ring_overflows++;
		return NULL;
	}
	segment->offset += aligned;
	return segment->base + segment->offset - aligned;
}

/* What the CPU wrote into the ring is flushed from its cache for the GPU once for a whole run of copies (ring_flush: at the end of each stream and before anything waits for the
GPU), not for each: a flush of 300 small ranges a frame was a fifth of the frame in the system's code. The commands are not run by the GPU before the frame's end (GX2Flush in
the present), so it reads the data after the flush. */
static void ring_flush(void)
{
	if (gx.ring_dirty_to > gx.ring_dirty_from)
		GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, gx.ring_dirty_from, (uint32_t)((uint8_t *)gx.ring_dirty_to - (uint8_t *)gx.ring_dirty_from));
	gx.ring_dirty_from = gx.ring_dirty_to = NULL;
}

#define TEST_LOG_BYTES (8 * 1024 * 1024)

static void run_stream(const unsigned char *at, const unsigned char *end);
static void gx_gate(void);
static void gt_log(unsigned frames);
static void gpu_poll(void);
static void gpu_frame_begin(void);
static int overlay_make(void);

/* room in the ring, noted as written to (the caller writes it before it is flushed, which is at the end of the stream or the present) */
static void *ring_reserve(uint32_t size)
{
	void *memory = ring_alloc(size);

	if (!memory)
		return NULL;
	gx.ring_bytes += size;
	gx.ring_total += size;
	gx.ring_calls++;
	gx.ring_kind_bytes[gx.ring_what] += size;
	gx.ring_kind_calls[gx.ring_what]++;
	if (size > gx.ring_largest)
		gx.ring_largest = size;
	if (!gx.ring_dirty_from || (uint8_t *)memory < (uint8_t *)gx.ring_dirty_from || (uint8_t *)memory > (uint8_t *)gx.ring_dirty_to + 4 * 1024 * 1024)
	{
		ring_flush();
		gx.ring_dirty_from = memory;
		gx.ring_dirty_to = (uint8_t *)memory + size;
	}
	else if ((uint8_t *)memory + size > (uint8_t *)gx.ring_dirty_to)
		gx.ring_dirty_to = (uint8_t *)memory + size;
	return memory;
}

/* the streams copied into the ring that are big, by address: which memory is copied each frame (in the log with the notes) */
static struct { uint32_t address, size, count, in_window; unsigned long bytes; } big_copies[24];

static void big_copy_note(uint32_t address, uint32_t size, uint32_t in_window)
{
	unsigned i;

	for (i = 0; i < 24 && big_copies[i].address; i++)
		if (big_copies[i].address == address)
			break;
	if (i == 24)
		return;
	big_copies[i].address = address;
	if (size > big_copies[i].size)
		big_copies[i].size = size;
	big_copies[i].count++;
	big_copies[i].in_window = in_window;
	big_copies[i].bytes += size;
}

static void big_copies_log(void)
{
	unsigned i, shown, best;
	unsigned char done[24] = { 0 };

	for (shown = 0; shown < 6; shown++)
	{
		best = 24;
		for (i = 0; i < 24 && big_copies[i].address; i++)
			if (!done[i] && (best == 24 || big_copies[i].bytes > big_copies[best].bytes))
				best = i;
		if (best == 24)
			break;
		done[best] = 1;
		wiiu_log("GX2:   big stream copy: %08x, up to %u KB, %u times (%lu KB in all), the Direct3D layer %s it static", (unsigned)big_copies[best].address, (unsigned)(big_copies[best].size / 1024),
			(unsigned)big_copies[best].count, big_copies[best].bytes / 1024, big_copies[best].in_window ? "calls" : "does not call");
	}
	memset(big_copies, 0, sizeof(big_copies));
}

static void *ring_copy(const void *source, uint32_t size)
{
	void *memory = ring_reserve(size);

	if (memory)
		memcpy(memory, source, size);
	return memory;
}

/* the frame is over: the GPU will read this segment's data until its stamp, and the next segment is free when its own is past */
static void ring_next(void)
{
	gx.ring[gx.ring_current].stamp = GX2GetLastSubmittedTimeStamp();
	gx.ring_current = (gx.ring_current + 1) % RING_SEGMENTS;
	if (gx.ring[gx.ring_current].stamp && wiiu_in_foreground() && !wiiu_exit_requested())
		GX2WaitTimeStamp(gx.ring[gx.ring_current].stamp); /* (not once HOME is pressed: the GPU may be taken, and the wait never ends) */
	gx.ring[gx.ring_current].offset = 0;
}

/* ---------- the map's own vertex and index data, copied once: it is where the Xbox window says it cannot change (dk_vertex_format.static_streams), so a draw that
reads it again finds its copy in memory the GPU can read, aligned, instead of copying it into the ring again */

static void free_later(void *memory);

#define STATIC_TABLE 4096
#define STATIC_CHUNK_BYTES (16u * 1024u * 1024u)
#define STATIC_CHUNKS 8

static struct
{
	struct static_entry
	{
		uint32_t address, size;
		unsigned long serial; /* the memory watch's serial when the copy was made: a write to the range announced after it makes the copy stale */
		void *memory;
	} table[STATIC_TABLE];
	uint8_t *chunks[STATIC_CHUNKS];
	uint32_t chunk_used;
	unsigned chunk_count;
	unsigned entries, hits, misses, full;
} cache;

/* The map's vertex streams, by block of the window: a draw reads its own range of a buffer, and the ranges differ from frame to frame as what is visible does, so a copy of each range made
the table full (4096 of them in a few seconds in some views) and then every draw copied its vertices into the ring again, 2 MB and 20 ms a frame. Now the window is cut in blocks of 128 KB, a
block copied whole with the 256 KB after it (any range of up to 256 KB is inside one block's copy) the first time a draw reads from it, and a draw is given a pointer into the copy. The window
(its place and size, from the Direct3D layer) is the only memory a range may be in. */
#define BLOCK_UNIT (128u * 1024u)
#define BLOCK_RANGE (2u * BLOCK_UNIT)
#define BLOCK_SPAN (BLOCK_UNIT + BLOCK_RANGE)
#define BLOCK_SLOTS 2048 /* (a window of 256 MB) */

static struct
{
	uint32_t window_base, window_size;
	struct { uint8_t *memory; uint32_t length; unsigned long serial; unsigned refreshes; } block[BLOCK_SLOTS];
	unsigned copies, hits, misses_range;
} blocks;

/* the memory watch (memory_watch_wiiu.c): a copy is good for a range while no write to it has been announced since the copy was made (dynamic vertex buffers are in the window too: a copy of
one would be shown for ever). A block whose range was written is copied again (to new memory: the GPU may still be reading the old) up to twice; after that, or for an exact range, the draw
copies the range into the ring */
unsigned long memory_watch_generation_raw(unsigned long address, unsigned long size);
unsigned long memory_watch_serial(void);

static int copy_is_current(uint32_t address, uint32_t size, unsigned long serial)
{
	return memory_watch_generation_raw(address, size) <= serial;
}

static void cache_flush(void)
{
	unsigned i;

	for (i = 0; i < cache.chunk_count; i++)
		free_later(cache.chunks[i]);
	memset(&cache, 0, sizeof(cache));
	memset(blocks.block, 0, sizeof(blocks.block));
}

/* for d3d8_gx2.c: the Xbox window's place and size */
void host_gx2_window(uint32_t base, uint32_t size);

void host_gx2_window(uint32_t base, uint32_t size)
{
	blocks.window_base = base;
	blocks.window_size = size;
}

void host_gx2_map_changed(void);

/* room in the chunks of the static cache (aligned, 16 MB chunks, up to 8): NULL when full */
static void *cache_alloc(uint32_t bytes)
{
	const uint32_t aligned = (bytes + 63u) & ~63u;
	uint8_t *memory;

	if (aligned > STATIC_CHUNK_BYTES)
		return NULL;
	if (!cache.chunk_count || cache.chunk_used + aligned > STATIC_CHUNK_BYTES)
	{
		if (cache.chunk_count == STATIC_CHUNKS)
		{
			cache.full++;
			return NULL;
		}
		cache.chunks[cache.chunk_count] = gx_alloc_raw(STATIC_CHUNK_BYTES, GX2_VERTEX_BUFFER_ALIGNMENT);
		if (!cache.chunks[cache.chunk_count])
		{
			cache.full++;
			return NULL;
		}
		cache.chunk_count++;
		cache.chunk_used = 0;
	}
	memory = cache.chunks[cache.chunk_count - 1] + cache.chunk_used;
	cache.chunk_used += aligned;
	return memory;
}

/* a pointer into the copy of the block [address, address + size) is in, NULL if the range is not in the window, is more than BLOCK_RANGE, or the cache has no room. The pointer is not aligned as a
copy made for the range would be (a draw's pointer is a vertex's): see draw() */
static void *block_get(uint32_t address, uint32_t size)
{
	uint32_t relative, block, offset;

	if (!blocks.window_size || address < blocks.window_base || size > BLOCK_RANGE || (uint64_t)address + size > (uint64_t)blocks.window_base + blocks.window_size)
	{
		blocks.misses_range++;
		return NULL;
	}
	relative = address - blocks.window_base;
	block = relative / BLOCK_UNIT;
	offset = relative % BLOCK_UNIT;
	if (block >= BLOCK_SLOTS)
		return NULL;
	if (blocks.block[block].memory && !copy_is_current(address, size, blocks.block[block].serial))
	{
		if (blocks.block[block].refreshes >= 2)
		{
			gx.dynamic_streams++;
			return NULL;
		}
		blocks.block[block].refreshes++;
		blocks.block[block].memory = NULL; /* (copied again below, to new memory) */
	}
	if (!blocks.block[block].memory)
	{
		const uint32_t start = block * BLOCK_UNIT;
		uint32_t length = BLOCK_SPAN;
		uint8_t *memory;
		const unsigned long serial = memory_watch_serial(); /* (before the copy: a write during it is newer) */

		if (start + length > blocks.window_size)
			length = blocks.window_size - start;
		memory = cache_alloc(BLOCK_SPAN);
		if (!memory)
			return NULL;
		blocks.block[block].serial = serial;
		memcpy(memory, (const void *)(uintptr_t)(blocks.window_base + start), length);
		GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, memory, length);
		blocks.block[block].memory = memory;
		blocks.block[block].length = length;
		blocks.copies++;
	}
	if (offset + size > blocks.block[block].length)
		return NULL;
	blocks.hits++;
	return blocks.block[block].memory + offset;
}

static void *cache_get(uint32_t address, uint32_t size)
{
	unsigned slot = (address * 2654435761u + size) % STATIC_TABLE, probes;
	const uint32_t aligned = (size + 63u) & ~63u;
	uint8_t *memory;

	/* (only the Xbox window's memory, whose writes the memory watch sees: a list in the C heap - a copy d3d8_gx2.c made of a long dynamic index list, freed once its stream
	is retired - may be another at the same address and size the next time) */
	if (!blocks.window_size || address < blocks.window_base || (uint64_t)address + size > (uint64_t)blocks.window_base + blocks.window_size)
		return NULL;

	for (probes = 0; probes < 32; probes++, slot = (slot + 1) % STATIC_TABLE)
	{
		struct static_entry *entry = &cache.table[slot];

		if (!entry->memory)
			break;
		if (entry->address == address && entry->size == size)
		{
			if (!copy_is_current(address, size, entry->serial))
			{
				gx.dynamic_streams++;
				return NULL;
			}
			cache.hits++;
			return entry->memory;
		}
	}
	if (probes == 32 || aligned > STATIC_CHUNK_BYTES)
		return NULL;
	memory = cache_alloc(aligned);
	if (!memory)
		return NULL;
	cache.table[slot].serial = memory_watch_serial();
	memcpy(memory, (const void *)(uintptr_t)address, size);
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, memory, size);
	cache.table[slot].address = address;
	cache.table[slot].size = size;
	cache.table[slot].memory = memory;
	cache.entries++;
	cache.misses++;
	return memory;
}

/* ---------- state */

static GX2CompareFunction compare_of(uint32_t gl)
{
	return gl >= 0x200 && gl <= 0x207 ? (GX2CompareFunction)(gl - 0x200) : GX2_COMPARE_FUNC_ALWAYS;
}

static GX2StencilFunction stencil_op_of(uint32_t gl)
{
	switch (gl)
	{
	case 0: return GX2_STENCIL_FUNCTION_ZERO;
	case 0x1E01: return GX2_STENCIL_FUNCTION_REPLACE;
	case 0x1E02: return GX2_STENCIL_FUNCTION_INCR_CLAMP;
	case 0x1E03: return GX2_STENCIL_FUNCTION_DECR_CLAMP;
	case 0x150A: return GX2_STENCIL_FUNCTION_INV;
	case 0x8507: return GX2_STENCIL_FUNCTION_INCR_WRAP;
	case 0x8508: return GX2_STENCIL_FUNCTION_DECR_WRAP;
	default: return GX2_STENCIL_FUNCTION_KEEP;
	}
}

static GX2BlendMode blend_of(uint32_t gl)
{
	switch (gl)
	{
	case 0: return GX2_BLEND_MODE_ZERO;
	case 1: return GX2_BLEND_MODE_ONE;
	case 0x300: return GX2_BLEND_MODE_SRC_COLOR;
	case 0x301: return GX2_BLEND_MODE_INV_SRC_COLOR;
	case 0x302: return GX2_BLEND_MODE_SRC_ALPHA;
	case 0x303: return GX2_BLEND_MODE_INV_SRC_ALPHA;
	case 0x304: return GX2_BLEND_MODE_DST_ALPHA;
	case 0x305: return GX2_BLEND_MODE_INV_DST_ALPHA;
	case 0x306: return GX2_BLEND_MODE_DST_COLOR;
	case 0x307: return GX2_BLEND_MODE_INV_DST_COLOR;
	case 0x308: return GX2_BLEND_MODE_SRC_ALPHA_SAT;
	case 0x8001: return GX2_BLEND_MODE_BLEND_FACTOR;
	case 0x8002: return GX2_BLEND_MODE_INV_BLEND_FACTOR;
	case 0x8003: return GX2_BLEND_MODE_CONSTANT_ALPHA;
	case 0x8004: return GX2_BLEND_MODE_INV_CONSTANT_ALPHA;
	default: return GX2_BLEND_MODE_ONE;
	}
}

static GX2BlendCombineMode combine_of(uint32_t equation)
{
	switch (equation)
	{
	case DK_BLEND_SUBTRACT: return GX2_BLEND_COMBINE_MODE_SUB;
	case DK_BLEND_REVERSE_SUBTRACT: return GX2_BLEND_COMBINE_MODE_REV_SUB;
	case DK_BLEND_MIN: return GX2_BLEND_COMBINE_MODE_MIN;
	case DK_BLEND_MAX: return GX2_BLEND_COMBINE_MODE_MAX;
	default: return GX2_BLEND_COMBINE_MODE_ADD;
	}
}

static void state_apply(void)
{
	gx.counts_state++;
	const struct dk_draw_state *s = &gx.state;
	const GX2FrontFace front = s->front_face_ccw ? GX2_FRONT_FACE_CCW : GX2_FRONT_FACE_CW;
	const GX2PolygonMode mode = s->fill_mode == DK_FILL_LINE ? GX2_POLYGON_MODE_LINE : s->fill_mode == DK_FILL_POINT ? GX2_POLYGON_MODE_POINT :
		GX2_POLYGON_MODE_TRIANGLE;
	const GX2StencilFunction keep = GX2_STENCIL_FUNCTION_KEEP;
	GX2CompareFunction stencil_function = compare_of(s->stencil_function);

	GX2SetRasterizerClipControlEx(TRUE, TRUE, FALSE);
	/* The generated vertex shaders put the Xbox's top at clip y = -1, as the OpenGL port's: there row 0 of a framebuffer is clip y = -1, and a render target's rows
	are the Xbox's (top first). GX2's viewport puts clip y = +1 at row 0, so it is given a negative height, which turns it over: rows and winding as OpenGL's. */
	GX2SetViewport((float)s->viewport[0], (float)(s->viewport[1] + s->viewport[3]), (float)s->viewport[2], -(float)s->viewport[3], s->depth_range[0],
		s->depth_range[1]);
	if (gx.debug_mode == 4)
		GX2SetScissor(0, 0, 1, 1); /* (no pixels: what the frame costs without filling any) */
	else
		GX2SetScissor((uint32_t)s->scissor[0], (uint32_t)s->scissor[1], (uint32_t)s->scissor[2], (uint32_t)s->scissor[3]);
	GX2SetPolygonControl(front, s->cull == DK_CULL_FRONT, s->cull == DK_CULL_BACK, mode != GX2_POLYGON_MODE_TRIANGLE, mode, mode, s->offset_enable,
		s->offset_enable, s->offset_enable);
	if (s->offset_enable)
		GX2SetPolygonOffset(s->offset_units, s->offset_slope, s->offset_units, s->offset_slope, 0.0f);
	if (s->stencil_test)
	{
		GX2SetDepthStencilControl(s->depth_test, s->depth_write, compare_of(s->depth_function), TRUE, FALSE, stencil_function, stencil_op_of(s->stencil_pass),
			stencil_op_of(s->stencil_depth_fail), stencil_op_of(s->stencil_fail), stencil_function, stencil_op_of(s->stencil_pass),
			stencil_op_of(s->stencil_depth_fail), stencil_op_of(s->stencil_fail));
		GX2SetStencilMask((uint8_t)s->stencil_mask, (uint8_t)s->stencil_write_mask, (uint8_t)s->stencil_reference, (uint8_t)s->stencil_mask,
			(uint8_t)s->stencil_write_mask, (uint8_t)s->stencil_reference);
	}
	else
	{
		GX2SetDepthStencilControl(s->depth_test, s->depth_write, compare_of(s->depth_function), FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS, keep, keep, keep,
			GX2_COMPARE_FUNC_ALWAYS, keep, keep, keep);
	}
	if (s->blend)
	{
		GX2SetBlendControl(GX2_RENDER_TARGET_0, blend_of(s->blend_source), blend_of(s->blend_destination), combine_of(s->blend_equation), TRUE,
			blend_of(s->blend_source), blend_of(s->blend_destination), combine_of(s->blend_equation));
		GX2SetBlendConstantColor(s->blend_color[0], s->blend_color[1], s->blend_color[2], s->blend_color[3]);
	}
	GX2SetColorControl(GX2_LOGIC_OP_COPY, s->blend ? 1 : 0, FALSE, TRUE);
	GX2SetTargetChannelMasks((GX2ChannelMask)s->color_mask, GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA,
		GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA);
	gx.state_dirty = 0;
}

/* ---------- uniforms: the stream's copies, written to the registers of the shader a draw uses when they differ from what the registers hold */

static void vertex_uniform(const struct shader *vs, unsigned which, const float *values, uint32_t floats)
{
	if (vs->uniform[which] < 0)
		return;
	if (floats > vs->uniform_floats[which])
		floats = vs->uniform_floats[which];
	if (floats < 4)
	{
		/* (a register is written whole: GX2 ignores a write of fewer than four floats, which left the menus' screen offset and the alpha test's reference at zero) */
		float whole[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

		memcpy(whole, values, floats * sizeof(float));
		GX2SetVertexUniformReg((uint32_t)vs->uniform[which], 4, whole);
		return;
	}
	GX2SetVertexUniformReg((uint32_t)vs->uniform[which], floats, values);
}

static void pixel_uniform(const struct shader *ps, unsigned which, const float *values, uint32_t floats)
{
	if (ps->uniform[which] < 0)
		return;
	if (floats > ps->uniform_floats[which])
		floats = ps->uniform_floats[which];
	if (floats < 4)
	{
		float whole[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

		memcpy(whole, values, floats * sizeof(float));
		GX2SetPixelUniformReg((uint32_t)ps->uniform[which], 4, whole);
		return;
	}
	GX2SetPixelUniformReg((uint32_t)ps->uniform[which], floats, values);
}

/* rows [first, first + rows) of an array of vec4s: each element at its own register where the compiler gave them, else in a row from the array's */
static void vertex_array(const struct shader *vs, unsigned which, const float (*values)[4], unsigned first, unsigned rows)
{
	unsigned row;

	if (vs->uniform[which] < 0 && !vs->elements[which])
		return;
	if (!vs->elements[which])
	{
		const uint32_t room = vs->uniform_floats[which] / 4;

		if (first < room)
			gx.counts_vertex_regs++;
		if (first < room)
			GX2SetVertexUniformReg((uint32_t)vs->uniform[which] + first * 4, (first + rows > room ? room - first : rows) * 4, values[first]);
		return;
	}
	for (row = first; row < first + rows;)
	{
		unsigned run = 1;

		if (vs->elements[which][row] < 0)
		{
			row++;
			continue;
		}
		while (row + run < first + rows && vs->elements[which][row + run] == vs->elements[which][row] + (int)run * 4)
			run++;
		gx.counts_vertex_regs++;
		GX2SetVertexUniformReg((uint32_t)vs->elements[which][row], run * 4, values[row]);
		row += run;
	}
}

static void pixel_array(const struct shader *ps, unsigned which, const float (*values)[4], unsigned rows)
{
	unsigned row;

	if (ps->uniform[which] < 0 && !ps->elements[which])
		return;
	if (!ps->elements[which])
	{
		const uint32_t room = ps->uniform_floats[which] / 4;

		gx.counts_pixel_regs++;
		GX2SetPixelUniformReg((uint32_t)ps->uniform[which], (rows > room ? room : rows) * 4, values[0]);
		return;
	}
	for (row = 0; row < rows;)
	{
		unsigned run = 1;

		if (ps->elements[which][row] < 0)
		{
			row++;
			continue;
		}
		while (row + run < rows && ps->elements[which][row + run] == ps->elements[which][row] + (int)run * 4)
			run++;
		gx.counts_pixel_regs++;
		GX2SetPixelUniformReg((uint32_t)ps->elements[which][row], run * 4, values[row]);
		row += run;
	}
}

static void uniforms_apply(const struct shader *vs, const struct shader *ps)
{
	if (vs != gx.regs_vs)
	{
		vertex_array(vs, VS_C, gx.constants, 0, 192);
		gx.constants_lo = 192;
		gx.constants_hi = 0;
		gx.vertex_parameters_dirty = 1;
		gx.regs_vs = vs;
	}
	else if (gx.constants_lo <= gx.constants_hi)
	{
		vertex_array(vs, VS_C, gx.constants, gx.constants_lo, gx.constants_hi - gx.constants_lo + 1);
		gx.constants_lo = 192;
		gx.constants_hi = 0;
	}
	if (gx.vertex_parameters_dirty)
	{
		vertex_uniform(vs, VS_VIEWPORT_SCALE, gx.vertex_parameters.viewport_scale, 4);
		vertex_uniform(vs, VS_VIEWPORT_OFFSET, gx.vertex_parameters.viewport_offset, 4);
		{
			/* (a scalar is in a register of its own: written as the whole register, as the GPU's constants are four at a time) */
			const float point[4] = { gx.vertex_parameters.point_and_screen[0], 0.0f, 0.0f, 0.0f };
			const float screen[4] = { gx.vertex_parameters.point_and_screen[1], 0.0f, 0.0f, 0.0f };

			if (vs->uniform[VS_POINT_SIZE] >= 0)
				GX2SetVertexUniformReg((uint32_t)vs->uniform[VS_POINT_SIZE], 4, point);
			if (vs->uniform[VS_SCREEN_OFFSET] >= 0)
				GX2SetVertexUniformReg((uint32_t)vs->uniform[VS_SCREEN_OFFSET], 4, screen);
		}
		gx.vertex_parameters_dirty = 0;
	}
	if (ps != gx.regs_ps || gx.pixel_parameters_dirty)
	{
		static const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		const struct dk_pixel_parameters *p = &gx.pixel_parameters;

		pixel_array(ps, PS_C0, p->ps_c0, 8);
		pixel_array(ps, PS_C1, p->ps_c1, 8);
		pixel_uniform(ps, PS_FINAL_C0, p->ps_final_c0, 4);
		pixel_uniform(ps, PS_FINAL_C1, p->ps_final_c1, 4);
		pixel_uniform(ps, PS_FOG_COLOR, p->fog_color, 4);
		pixel_uniform(ps, PS_FOG_PARAMETERS, p->fog_parameters, 4);
		pixel_uniform(ps, PS_ALPHA_REFERENCE, p->alpha_reference, 1);
		pixel_array(ps, PS_BUMP_MATRIX, p->bump_matrix, 4);
		pixel_array(ps, PS_BUMP_LUMINANCE, p->bump_luminance, 4);
		pixel_array(ps, PS_TEXTURE_SCALE, p->texture_scale, 4);
		pixel_uniform(ps, PS_TEXTURE_LOD_BIAS, zero, 4);
		gx.pixel_parameters_dirty = 0;
		gx.regs_ps = ps;
	}
}

/* ---------- textures */

#define MIP_LIMIT 13

struct image
{
	int used;
	int supported;
	uint32_t kind, format, width, height, depth, levels, requested_levels;
	/* a cube's six faces and a 3D texture's slices are slices of one surface */
	uint32_t slices;
	unsigned block_bytes;
	int compressed;
	uint8_t *base;
	/* where each level is, as the surface says (its first slice), the pitch of its rows (blocks, for the compressed formats) and the size of a slice of it */
	uint32_t level_offset[MIP_LIMIT], level_pitch[MIP_LIMIT], level_slice_bytes[MIP_LIMIT];
	GX2Texture texture;
};

static struct image *images;

static void deferred_flush(int everything)
{
	unsigned i, kept = 0;

	for (i = 0; i < gx.deferred_count; i++)
	{
		if (everything || gx.frames >= gx.deferred[i].frame + 3)
			MEMFreeToDefaultHeap(gx.deferred[i].memory);
		else
			gx.deferred[kept++] = gx.deferred[i];
	}
	gx.deferred_count = kept;
}

/* memory the GPU may still be reading goes back to the heap a few frames on */
static void free_later(void *memory)
{
	if (!memory)
		return;
	if (gx.deferred_count == DEFERRED_LIMIT)
	{
		GX2DrawDone();
		deferred_flush(1);
	}
	gx.deferred[gx.deferred_count].memory = memory;
	gx.deferred[gx.deferred_count].frame = gx.frames;
	gx.deferred_count++;
}

static void image_release(struct image *image)
{
	if (image->used && image->base)
		free_later(image->base);
	memset(image, 0, sizeof(*image));
}

static int format_of(uint32_t dk, GX2SurfaceFormat *format, unsigned *block_bytes)
{
	switch (dk)
	{
	case DK_TEXTURE_BC1: *format = GX2_SURFACE_FORMAT_UNORM_BC1; *block_bytes = 8; return 1;
	case DK_TEXTURE_BC2: *format = GX2_SURFACE_FORMAT_UNORM_BC2; *block_bytes = 16; return 1;
	case DK_TEXTURE_BC3: *format = GX2_SURFACE_FORMAT_UNORM_BC3; *block_bytes = 16; return 1;
	default: *format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8; *block_bytes = 4; return 0;
	}
}

static uint32_t level_size(uint32_t size, unsigned level)
{
	size >>= level;
	return size ? size : 1;
}

/* the pitch and size of a level (one slice) on its own: what the surface's calculation says of a surface that size */
static void level_layout(const struct image *image, GX2SurfaceFormat format, GX2TileMode mode, unsigned level, uint32_t *pitch, uint32_t *bytes)
{
	GX2Surface surface;

	memset(&surface, 0, sizeof(surface));
	surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
	surface.width = level_size(image->width, level);
	surface.height = level_size(image->height, level);
	surface.depth = 1;
	surface.mipLevels = 1;
	surface.format = format;
	surface.aa = GX2_AA_MODE1X;
	surface.use = GX2_SURFACE_USE_TEXTURE;
	surface.tileMode = mode;
	GX2CalcSurfaceSizeAndAlignment(&surface);
	*pitch = surface.pitch;
	*bytes = surface.imageSize;
}

/* where level `level`, slice `slice` is in memory */
static uint8_t *image_level(const struct image *image, unsigned level, unsigned slice)
{
	return image->base + image->level_offset[level] + (size_t)slice * image->level_slice_bytes[level];
}

/* makes the texture's surface with all its levels. The compressed formats are in the 1D tiled layout (wiiu_tile.c), BGRA linear; the texels are the DDS file's bytes / a
Direct3D color's bytes (blue first), which the component map puts right */
static int image_surface(struct image *image, uint32_t kind, uint32_t format, uint32_t width, uint32_t height, uint32_t depth, uint32_t levels)
{
	GX2Surface *surface = &image->texture.surface;
	GX2SurfaceFormat gx_format;
	GX2TileMode mode;
	unsigned block_bytes, level;
	uint32_t largest = width > height ? width : height;
	uint32_t total;

	memset(image, 0, sizeof(*image));
	image->used = 1;
	image->kind = kind;
	image->format = format;
	image->width = width;
	image->height = height;
	image->depth = depth ? depth : 1;
	image->requested_levels = levels;
	image->compressed = format_of(format, &gx_format, &block_bytes);
	image->block_bytes = block_bytes;
	if (!width || !height || kind > DK_TEXTURE_CUBE || (kind != DK_TEXTURE_2D && (gx.features_off & 2)))
		return 0;
	if (gx.features_off & 1)
		levels = 1;
	if (kind == DK_TEXTURE_3D && image->depth > largest)
		largest = image->depth;
	if (levels < 1)
		levels = 1;
	while (levels > 1 && (largest >> (levels - 1)) == 0)
		levels--;
	if (levels > MIP_LIMIT)
		levels = MIP_LIMIT;
	image->levels = levels;
	image->slices = kind == DK_TEXTURE_CUBE ? 6 : kind == DK_TEXTURE_3D ? image->depth : 1;
	mode = image->compressed ? GX2_TILE_MODE_TILED_1D_THIN1 : GX2_TILE_MODE_LINEAR_ALIGNED;
	surface->dim = kind == DK_TEXTURE_CUBE ? GX2_SURFACE_DIM_TEXTURE_CUBE : kind == DK_TEXTURE_3D ? GX2_SURFACE_DIM_TEXTURE_3D : GX2_SURFACE_DIM_TEXTURE_2D;
	surface->width = width;
	surface->height = height;
	surface->depth = kind == DK_TEXTURE_CUBE ? 6 : kind == DK_TEXTURE_3D ? image->depth : 1;
	surface->mipLevels = levels;
	surface->format = gx_format;
	surface->aa = GX2_AA_MODE1X;
	surface->use = GX2_SURFACE_USE_TEXTURE;
	surface->tileMode = mode;
	GX2CalcSurfaceSizeAndAlignment(surface);
	total = surface->imageSize + (levels > 1 ? surface->mipmapSize : 0);
	image->base = gx_alloc_raw(total, surface->alignment ? surface->alignment : 256);
	if (!image->base)
	{
		wiiu_log("GX2: no memory for a %ux%u texture (%u bytes)", (unsigned)width, (unsigned)height, (unsigned)total);
		return 0;
	}
	memset(image->base, 0, total);
	for (level = 0; level < levels; level++)
	{
		level_layout(image, gx_format, mode, level, &image->level_pitch[level], &image->level_slice_bytes[level]);
		/* (GX2's mipLevelOffset[0] is where level 1 is from the surface's start, and mipLevelOffset[n] for n > 0 where level n + 1 is from the first mip level's:
		the console's numbers for a 64 x 64 BC1 texture are 2048 512 1024) */
		image->level_offset[level] = level == 0 ? 0 : surface->mipLevelOffset[0] + (level == 1 ? 0 : surface->mipLevelOffset[level - 1]);
	}
	for (level = 0; level < levels; level++)
		if ((uint64_t)image->level_offset[level] + (uint64_t)image->level_slice_bytes[level] * image->slices > total)
		{
			if (gx.layouts_logged < 12)
				wiiu_log("GX2: texture %ux%u kind %u: level %u (%u + %u x %u) does not fit its %u bytes; the texture is its first level only", (unsigned)width,
					(unsigned)height, (unsigned)kind, level, (unsigned)image->level_offset[level], (unsigned)image->level_slice_bytes[level],
					(unsigned)image->slices, (unsigned)total);
			gx.layouts_logged++;
			if (level == 0 || kind != DK_TEXTURE_2D)
			{
				MEMFreeToDefaultHeap(image->base);
				image->base = NULL;
				return 0;
			}
			levels = level;
			break;
		}
	image->levels = levels;
	surface->mipLevels = levels;
	surface->image = image->base;
	surface->mipmaps = levels > 1 ? image->base + image->level_offset[1] : NULL;
	if (gx.layouts_logged < 6 && (levels > 1 || kind != DK_TEXTURE_2D))
	{
		char text[200];
		size_t used = 0;

		for (level = 0; level < levels && used + 24 < sizeof(text); level++)
			used += (size_t)snprintf(text + used, sizeof(text) - used, " %u:%u/%u/%u", level, (unsigned)image->level_offset[level],
				(unsigned)image->level_pitch[level], (unsigned)image->level_slice_bytes[level]);
		wiiu_log("GX2: texture %ux%ux%u kind %u format %u, %u levels, image %u mip %u align %u pitch %u, raw level offsets %u %u %u %u %u; level offset/pitch/slice:%s",
			(unsigned)width, (unsigned)height, (unsigned)image->depth, (unsigned)kind, (unsigned)format, (unsigned)levels, (unsigned)surface->imageSize,
			(unsigned)surface->mipmapSize, (unsigned)surface->alignment, (unsigned)surface->pitch, (unsigned)surface->mipLevelOffset[0],
			(unsigned)surface->mipLevelOffset[1], (unsigned)surface->mipLevelOffset[2], (unsigned)surface->mipLevelOffset[3],
			(unsigned)surface->mipLevelOffset[4], text);
		gx.layouts_logged++;
	}
	image->texture.viewFirstMip = 0;
	image->texture.viewNumMips = levels;
	image->texture.viewFirstSlice = 0;
	image->texture.viewNumSlices = kind == DK_TEXTURE_CUBE ? 6 : kind == DK_TEXTURE_3D ? image->depth : 1;
	image->texture.compMap = image->compressed ? GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A) :
		GX2_COMP_MAP(GX2_SQ_SEL_B, GX2_SQ_SEL_G, GX2_SQ_SEL_R, GX2_SQ_SEL_A);
	GX2InitTextureRegs(&image->texture);
	image->supported = 1;
	return 1;
}

/* one slice of one level, from rows of texels or blocks (no padding) */
static void image_write_slice(const struct image *image, unsigned level, unsigned slice, const uint8_t *source)
{
	const uint32_t width = level_size(image->width, level), height = level_size(image->height, level);
	uint8_t *destination = image_level(image, level, slice);

	if (image->compressed)
		wiiu_tile_1d(source, (width + 3) / 4, (height + 3) / 4, image->block_bytes, image->level_pitch[level], destination);
	else
	{
		unsigned y;

		for (y = 0; y < height; y++)
			memcpy(destination + (size_t)y * image->level_pitch[level] * 4, source + (size_t)y * width * 4, (size_t)width * 4);
	}
}

/* the bytes of one slice of a level in the source */
static uint32_t level_source_bytes(const struct image *image, unsigned level)
{
	const uint32_t width = level_size(image->width, level), height = level_size(image->height, level);

	return image->compressed ? ((width + 3) / 4) * ((height + 3) / 4) * image->block_bytes : width * height * 4;
}

/* the texels of every level (and each cube face's) as dk_command_texture lays them out */
static void image_write(struct image *image, const uint8_t *source, uint32_t source_bytes, uint32_t face_bytes)
{
	const unsigned faces = image->kind == DK_TEXTURE_CUBE ? 6 : 1;
	unsigned face, level, slice;

	for (face = 0; face < faces; face++)
	{
		uint32_t at = face * face_bytes;

		for (level = 0; level < image->levels; level++)
		{
			const uint32_t slices = image->kind == DK_TEXTURE_3D ? level_size(image->depth, level) : 1;
			const uint32_t bytes = level_source_bytes(image, level);

			if ((uint64_t)at + (uint64_t)bytes * slices > source_bytes)
			{
				gx.texture_short++;
				goto done;
			}
			for (slice = 0; slice < slices; slice++)
				image_write_slice(image, level, image->kind == DK_TEXTURE_CUBE ? face : slice, source + at + (size_t)slice * bytes);
			at += bytes * slices;
		}
	}
done:
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, image->base, image->texture.surface.imageSize + (image->levels > 1 ? image->texture.surface.mipmapSize : 0));
}

static void texture_make(const struct dk_command_texture *command)
{
	struct image *image;

	if (!images || !command->id || command->id >= DK_TEXTURE_LIMIT)
		return;
	image = &images[command->id];
	if (!(image->used && image->supported && image->kind == command->kind && image->format == command->format && image->width == command->width &&
		image->height == command->height && image->depth == (command->depth ? command->depth : 1) && image->requested_levels == command->levels))
	{
		image_release(image);
		image_surface(image, command->kind, command->format, command->width, command->height, command->depth, command->levels);
		gx.textures_made++;
	}
	if (gx.texture_log < 80 && command->format == DK_TEXTURE_BGRA && command->kind == DK_TEXTURE_2D && command->source_bytes)
	{
		const uint8_t *texels = (const uint8_t *)(uintptr_t)command->source;
		unsigned long i, clear = 0, count = (unsigned long)command->width * command->height;

		for (i = 0; i < count; i++)
			if (texels[i * 4 + 3] < 128)
				clear++;
		wiiu_log("GX2: texture %u made/updated: %ux%u, %u levels, %lu of %lu texels transparent (byte 3), first texel %02x%02x%02x%02x", (unsigned)command->id,
			(unsigned)command->width, (unsigned)command->height, (unsigned)command->levels, clear, count, texels[0], texels[1], texels[2], texels[3]);
		gx.texture_log++;
	}
	if (image->supported && command->source_bytes)
		image_write(image, (const uint8_t *)(uintptr_t)command->source, command->source_bytes, command->face_bytes);
	else if (!image->supported)
		gx.textures_unsupported++;
}

static void texture_rows(const struct dk_command_texture_rows *command)
{
	struct image *image;
	unsigned row;
	uint32_t width, height;

	if (!images || !command->id || command->id >= DK_TEXTURE_LIMIT)
		return;
	image = &images[command->id];
	if (!image->used || !image->supported || image->format != DK_TEXTURE_BGRA || image->kind != DK_TEXTURE_2D || command->level >= image->levels)
		return;
	width = level_size(image->width, command->level);
	height = level_size(image->height, command->level);
	if (command->top + command->rows > height || command->source_bytes < command->rows * width * 4)
		return;
	for (row = 0; row < command->rows; row++)
		memcpy(image_level(image, command->level, 0) + (size_t)(command->top + row) * image->level_pitch[command->level] * 4,
			(const uint8_t *)(uintptr_t)command->source + (size_t)row * width * 4, (size_t)width * 4);
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, image->base, image->texture.surface.imageSize + (image->levels > 1 ? image->texture.surface.mipmapSize : 0));
}

static void texture_free(const struct dk_command_texture_free *command)
{
	if (images && command->id && command->id < DK_TEXTURE_LIMIT)
		image_release(&images[command->id]);
}

static GX2TexClampMode wrap_of(uint32_t wrap)
{
	switch (wrap)
	{
	case DK_WRAP_MIRROR: return GX2_TEX_CLAMP_MODE_MIRROR;
	case DK_WRAP_CLAMP: return GX2_TEX_CLAMP_MODE_CLAMP;
	case DK_WRAP_BORDER: return GX2_TEX_CLAMP_MODE_CLAMP_BORDER;
	default: return GX2_TEX_CLAMP_MODE_WRAP;
	}
}

static GX2TexAnisoRatio aniso_of(float anisotropy)
{
	return anisotropy >= 16.0f ? GX2_TEX_ANISO_RATIO_16_TO_1 : anisotropy >= 8.0f ? GX2_TEX_ANISO_RATIO_8_TO_1 : anisotropy >= 4.0f ? GX2_TEX_ANISO_RATIO_4_TO_1 :
		anisotropy >= 2.0f ? GX2_TEX_ANISO_RATIO_2_TO_1 : GX2_TEX_ANISO_RATIO_NONE;
}

/* levels: those of the texture the sampler is used with */
static void sampler_set(GX2Sampler *sampler, const struct dk_sampler *s, unsigned levels)
{
	const GX2TexMipFilterMode mip = (gx.features_off & 16) ? GX2_TEX_MIP_FILTER_MODE_NONE : levels > 1 && s->mip_filter == DK_MIP_LINEAR ?
		GX2_TEX_MIP_FILTER_MODE_LINEAR : levels > 1 && s->mip_filter == DK_MIP_NEAREST ? GX2_TEX_MIP_FILTER_MODE_POINT : GX2_TEX_MIP_FILTER_MODE_NONE;
	const float maximum = mip == GX2_TEX_MIP_FILTER_MODE_NONE ? 0.0f : (float)(levels - 1);
	float minimum = s->lod_minimum;

	if (minimum > maximum)
		minimum = maximum;
	if (minimum < 0.0f)
		minimum = 0.0f;
	GX2InitSampler(sampler, GX2_TEX_CLAMP_MODE_WRAP, GX2_TEX_XY_FILTER_MODE_POINT);
	GX2InitSamplerClamping(sampler, wrap_of(s->wrap[0]), wrap_of(s->wrap[1]), wrap_of(s->wrap[2]));
	GX2InitSamplerXYFilter(sampler, s->mag_linear ? GX2_TEX_XY_FILTER_MODE_LINEAR : GX2_TEX_XY_FILTER_MODE_POINT,
		s->min_linear ? GX2_TEX_XY_FILTER_MODE_LINEAR : GX2_TEX_XY_FILTER_MODE_POINT, (gx.features_off & 32) ? GX2_TEX_ANISO_RATIO_NONE : aniso_of(s->anisotropy));
	GX2InitSamplerZMFilter(sampler, s->min_linear ? GX2_TEX_Z_FILTER_MODE_LINEAR : GX2_TEX_Z_FILTER_MODE_POINT, mip);
	GX2InitSamplerLOD(sampler, minimum, maximum, mip == GX2_TEX_MIP_FILTER_MODE_NONE ? 0.0f : s->lod_bias);
	GX2InitSamplerBorderType(sampler, s->border[3] > 0.5f && s->border[0] > 0.5f ? GX2_TEX_BORDER_TYPE_WHITE : s->border[3] > 0.5f ? GX2_TEX_BORDER_TYPE_BLACK :
		GX2_TEX_BORDER_TYPE_TRANSPARENT_BLACK);
}

static void textures_set(const struct dk_command_textures *command)
{
	unsigned stage;

	for (stage = 0; stage < 4; stage++)
	{
		gx.stages[stage] = command->stages[stage];
		gx.sampler_valid[stage] = 0;
	}
}

/* ---------- textures rendered a level at a time (the water's ripples): the levels' targets are copied into one texture with all its levels */

#define COMPOSITE_LIMIT 32

struct composite
{
	int used;
	uint32_t data, width, height, levels;
	uint32_t level_data[DK_COMPOSITE_LEVELS];
	unsigned level_written[DK_COMPOSITE_LEVELS];
	unsigned level_derived[DK_COMPOSITE_LEVELS]; /* for a level the game did not render: the stamp of the level it was made from */
	uint8_t *base;
	GX2Texture texture;
};

static struct composite composites[COMPOSITE_LIMIT];

static void composite_receive(const struct dk_command_composite *command)
{
	struct composite *composite = NULL;
	unsigned i, level;
	GX2Surface *surface;

	if (!command->levels || command->levels > DK_COMPOSITE_LEVELS || (gx.features_off & 4))
		return;
	for (i = 0; i < COMPOSITE_LIMIT; i++)
		if (composites[i].used && composites[i].data == command->data && composites[i].width == command->width && composites[i].height == command->height &&
			composites[i].levels == command->levels)
			composite = &composites[i];
	if (composite)
	{
		memcpy(composite->level_data, command->level_data, sizeof(composite->level_data));
		memset(composite->level_written, 0, sizeof(composite->level_written));
		memset(composite->level_derived, 0, sizeof(composite->level_derived));
		return;
	}
	for (i = 0; i < COMPOSITE_LIMIT && composites[i].used; i++)
		;
	if (i == COMPOSITE_LIMIT)
	{
		wiiu_log("GX2: more than %d mip composites", COMPOSITE_LIMIT);
		return;
	}
	composite = &composites[i];
	memset(composite, 0, sizeof(*composite));
	composite->used = 1;
	composite->data = command->data;
	composite->width = command->width;
	composite->height = command->height;
	composite->levels = command->levels;
	memcpy(composite->level_data, command->level_data, sizeof(composite->level_data));
	surface = &composite->texture.surface;
	surface->dim = GX2_SURFACE_DIM_TEXTURE_2D;
	surface->width = command->width;
	surface->height = command->height;
	surface->depth = 1;
	surface->mipLevels = command->levels;
	surface->format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
	surface->aa = GX2_AA_MODE1X;
	surface->use = GX2_SURFACE_USE_TEXTURE;
	surface->tileMode = GX2_TILE_MODE_DEFAULT;
	GX2CalcSurfaceSizeAndAlignment(surface);
	composite->base = gx_alloc_raw(surface->imageSize + surface->mipmapSize, surface->alignment ? surface->alignment : 256);
	if (!composite->base)
	{
		memset(composite, 0, sizeof(*composite));
		return;
	}
	memset(composite->base, 0, surface->imageSize + surface->mipmapSize);
	surface->image = composite->base;
	surface->mipmaps = command->levels > 1 ? composite->base + surface->mipLevelOffset[0] : NULL;
	composite->texture.viewNumMips = command->levels;
	composite->texture.viewNumSlices = 1;
	composite->texture.compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
	GX2InitTextureRegs(&composite->texture);
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, composite->base, surface->imageSize + surface->mipmapSize);
	(void)level;
}

static struct target *target_latest(uint32_t data)
{
	struct target *found = NULL;
	unsigned i;

	for (i = 0; i < gx.target_count; i++)
		if (gx.targets[i].surface.kind == DK_SURFACE_COLOR && gx.targets[i].surface.data == data && !gx.targets[i].unusable &&
			(!found || gx.targets[i].last_used >= found->last_used))
			found = &gx.targets[i];
	return found;
}

/* a level the game did not render, made from the level before it (the Switch's renderer does the same): copied by the GPU to a linear surface, averaged by the CPU and copied back.
It waits for the GPU (once, and again only when the level before it changes), which a texture that is mostly rendered at every level never has to. */
static void *mip_scratch[2];
static uint32_t mip_scratch_bytes;

static int composite_derive(struct composite *composite, unsigned level)
{
	const uint32_t source_width = level_size(composite->width, level - 1), source_height = level_size(composite->height, level - 1);
	const uint32_t width = level_size(composite->width, level), height = level_size(composite->height, level);
	GX2Surface linear[2];
	unsigned i;

	for (i = 0; i < 2; i++)
	{
		memset(&linear[i], 0, sizeof(linear[i]));
		linear[i].dim = GX2_SURFACE_DIM_TEXTURE_2D;
		linear[i].width = i ? width : source_width;
		linear[i].height = i ? height : source_height;
		linear[i].depth = 1;
		linear[i].mipLevels = 1;
		linear[i].format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
		linear[i].aa = GX2_AA_MODE1X;
		linear[i].use = GX2_SURFACE_USE_TEXTURE;
		linear[i].tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
		GX2CalcSurfaceSizeAndAlignment(&linear[i]);
		if (linear[i].imageSize > mip_scratch_bytes)
		{
			for (unsigned j = 0; j < 2; j++)
			{
				if (mip_scratch[j])
					MEMFreeToDefaultHeap(mip_scratch[j]);
				mip_scratch[j] = NULL;
			}
			mip_scratch_bytes = 0;
		}
	}
	if (!mip_scratch[0])
	{
		const uint32_t bytes = linear[0].imageSize > linear[1].imageSize ? linear[0].imageSize : linear[1].imageSize;

		mip_scratch[0] = gx_alloc_raw(bytes, 256);
		mip_scratch[1] = gx_alloc_raw(bytes, 256);
		if (!mip_scratch[0] || !mip_scratch[1])
		{
			if (mip_scratch[0])
				MEMFreeToDefaultHeap(mip_scratch[0]);
			if (mip_scratch[1])
				MEMFreeToDefaultHeap(mip_scratch[1]);
			mip_scratch[0] = mip_scratch[1] = NULL;
			return 0;
		}
		mip_scratch_bytes = bytes;
	}
	linear[0].image = mip_scratch[0];
	linear[1].image = mip_scratch[1];
	GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_COLOR_BUFFER | GX2_INVALIDATE_MODE_TEXTURE), composite->base,
		composite->texture.surface.imageSize + composite->texture.surface.mipmapSize);
	GX2CopySurface(&composite->texture.surface, level - 1, 0, &linear[0], 0, 0);
	GX2DrawDone();
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU, linear[0].image, linear[0].imageSize);
	wiiu_mip_next((const uint8_t *)linear[0].image, source_width, source_height, linear[0].pitch, (uint8_t *)linear[1].image, linear[1].pitch);
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, linear[1].image, linear[1].imageSize);
	GX2CopySurface(&linear[1], 0, 0, &composite->texture.surface, level, 0);
	return 1;
}

/* brings the composite up to date: its levels the game rendered copied again from the targets drawn into since, the others made from the level before (a level that was never
made from anything stays empty). Returns whether it waited for the GPU (and so left the context for the caller to set up again). */
static void context_resume(void);

/* the composite's levels brought up to date with what was rendered into them; returns whether the GPU's context was changed on the way: GX2CopySurface draws
with a context of its own and leaves it set (as composite_derive and the screenshot's copy do), so the caller resumes the frame's */
static int composite_refresh(struct composite *composite, int may_derive)
{
	unsigned level;
	int changed = 0;

	for (level = 0; level < composite->levels; level++)
	{
		struct target *target = target_latest(composite->level_data[level]);

		if (!target || target->color.surface.width != level_size(composite->width, level) || target->color.surface.height != level_size(composite->height, level))
		{
			if (level && composite->level_written[level - 1] && may_derive && composite->level_derived[level] != composite->level_written[level - 1] && !(gx.features_off & 64) &&
				composite_derive(composite, level))
			{
				composite->level_derived[level] = composite->level_written[level - 1];
				composite->level_written[level] = composite->level_written[level - 1] + 1; /* (so that a level after it follows) */
				changed = 1;
			}
			continue;
		}
		if (target->last_used == composite->level_written[level] && composite->level_written[level])
			continue;
		GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_COLOR_BUFFER | GX2_INVALIDATE_MODE_TEXTURE), target->color.surface.image,
			target->color.surface.imageSize);
		GX2CopySurface(&target->color.surface, 0, 0, &composite->texture.surface, level, 0);
		composite->level_written[level] = target->last_used ? target->last_used : 1;
		changed = 1;
	}
	return changed;
}

static struct composite *composite_find(uint32_t data, uint32_t levels)
{
	unsigned i;

	for (i = 0; i < COMPOSITE_LIMIT; i++)
		if (composites[i].used && composites[i].data == data && composites[i].levels == levels)
			return &composites[i];
	return NULL;
}

/* before a draw sets its state up: the composites its stages sample are brought up to date, which can mean waiting for the GPU */
static void composites_prepare(void)
{
	unsigned stage;
	int changed = 0;

	for (stage = 0; stage < 4; stage++)
	{
		const struct dk_stage_texture *entry = &gx.stages[stage];
		struct composite *composite;

		if (entry->id || !entry->composite || !(composite = composite_find(entry->target, entry->composite)))
			continue;
		changed |= composite_refresh(composite, 1);
	}
	/* (a level copied or derived: the copy left its own context set, and every draw after it went to that context's target until the next targets command. a30's
	sky water was the first draw to sample a composite each frame - its bump map, rendered a level at a time - and, once its shaders were in the pack, the
	terrain and objects after it were drawn nowhere: a pale plane where the water's reflection had been drawn, the decals, smoke and HUD on top of it.) */
	if (changed)
		context_resume();
}

/* the composite whose levels the stage samples */
static const GX2Texture *composite_sampled(uint32_t data, uint32_t levels)
{
	struct composite *composite = composite_find(data, levels);

	if (!composite)
		return NULL;
	/* (composites_prepare brought it up to date before this draw's state was set; copying here, in the middle of the draw's setup, would change the context) */
	return &composite->texture;
}

/* the texture a stage samples: its image, the composite or render target it names, or a white texel */
static const GX2Texture *stage_texture(const struct dk_stage_texture *stage, unsigned *levels)
{
	*levels = 1;
	gx.stage_dynamic = 0;
	if (stage->id && stage->id < DK_TEXTURE_LIMIT && images && images[stage->id].used && images[stage->id].supported)
	{
		*levels = images[stage->id].levels;
		return &images[stage->id].texture;
	}
	if (!stage->id && stage->composite)
	{
		const GX2Texture *texture = composite_sampled(stage->target, stage->composite);

		gx.stage_dynamic = 1;
		if (texture)
		{
			*levels = stage->composite;
			return texture;
		}
	}
	else if (!stage->id && stage->target)
	{
		struct target *found = target_latest(stage->target);

		gx.stage_dynamic = 1;
		if (found)
		{
			GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_COLOR_BUFFER | GX2_INVALIDATE_MODE_TEXTURE), found->color.surface.image,
				found->color.surface.imageSize);
			return &found->texture;
		}
	}
	return &gx.white;
}

static void textures_bind(const struct shader *ps)
{
	unsigned stage;

	for (stage = 0; stage < 4; stage++)
	{
		unsigned levels;
		const GX2Texture *texture;

		if (ps->sampler[stage] < 0)
			continue;
		texture = stage_texture(&gx.stages[stage], &levels);
		if (!gx.sampler_valid[stage] || gx.sampler_levels[stage] != levels)
		{
			sampler_set(&gx.samplers[stage], &gx.stages[stage].sampler, levels);
			gx.sampler_valid[stage] = 1;
			gx.sampler_levels[stage] = levels;
			gx.samplers_changed[stage] = 1;
		}
		if (!gx.stage_dynamic && gx.bound_texture[stage] == texture && gx.bound_location[stage] == ps->sampler[stage] && gx.sampler_valid[stage] &&
			gx.sampler_levels[stage] == levels && !gx.samplers_changed[stage])
			continue;
		gx.counts_texture_sets++;
		GX2SetPixelTexture(texture, (uint32_t)ps->sampler[stage]);
		GX2SetPixelSampler(&gx.samplers[stage], (uint32_t)ps->sampler[stage]);
		gx.bound_texture[stage] = gx.stage_dynamic ? NULL : texture;
		gx.bound_location[stage] = ps->sampler[stage];
		gx.samplers_changed[stage] = 0;
	}
}

static int white_make(void)
{
	GX2Surface *surface = &gx.white.surface;

	surface->dim = GX2_SURFACE_DIM_TEXTURE_2D;
	surface->width = 1;
	surface->height = 1;
	surface->depth = 1;
	surface->mipLevels = 1;
	surface->format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
	surface->aa = GX2_AA_MODE1X;
	surface->use = GX2_SURFACE_USE_TEXTURE;
	surface->tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
	GX2CalcSurfaceSizeAndAlignment(surface);
	surface->image = gx_alloc_raw(surface->imageSize, surface->alignment ? surface->alignment : 256);
	if (!surface->image)
		return 0;
	memset(surface->image, 0xff, surface->imageSize);
	gx.white.viewNumMips = 1;
	gx.white.viewNumSlices = 1;
	gx.white.compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
	GX2InitTextureRegs(&gx.white);
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, surface->image, surface->imageSize);
	return 1;
}

/* ---------- draws */

static GX2PrimitiveMode primitive_of(uint32_t primitive)
{
	switch (primitive)
	{
	case DK_PRIMITIVE_POINTS: return GX2_PRIMITIVE_MODE_POINTS;
	case DK_PRIMITIVE_LINES: return GX2_PRIMITIVE_MODE_LINES;
	case DK_PRIMITIVE_LINE_LOOP: return GX2_PRIMITIVE_MODE_LINE_LOOP;
	case DK_PRIMITIVE_LINE_STRIP: return GX2_PRIMITIVE_MODE_LINE_STRIP;
	case DK_PRIMITIVE_TRIANGLE_STRIP: return GX2_PRIMITIVE_MODE_TRIANGLE_STRIP;
	case DK_PRIMITIVE_TRIANGLE_FAN: return GX2_PRIMITIVE_MODE_TRIANGLE_FAN;
	case DK_PRIMITIVE_QUADS: return GX2_PRIMITIVE_MODE_QUADS;
	default: return GX2_PRIMITIVE_MODE_TRIANGLES;
	}
}

/* the draws of the frame halo/gx2_trace.txt names, one line each, to see what the game asked of the GPU */
static void draw_trace(const struct dk_command_draw *command, const struct shader *vs, const struct shader *ps)
{
	const struct dk_draw_state *s = &gx.state;
	const struct dk_vertex_format *f = &gx.format;
	unsigned location, slot = 0, stream;
	char text[512];
	size_t used = 0;
	float first[4] = { 0, 0, 0, 0 };
	char bytes[96] = "";

	for (location = 0; location < DK_ATTRIBUTE_COUNT; location++)
		if (vs->attribute_used & (1u << location))
			used += (size_t)snprintf(text + used, sizeof(text) - used, " %u:f%u@s%u+%u", location, (unsigned)f->attributes[location].format,
				(unsigned)f->attributes[location].stream, (unsigned)f->attributes[location].offset);
	for (stream = 0; stream < DK_STREAM_COUNT; stream++)
	{
		if (!((f->stream_mask >> stream) & 1))
			continue;
		if (slot < command->stream_count && stream == f->attributes[0].stream && f->attributes[0].format >= DK_ATTRIBUTE_FLOAT1 &&
			f->attributes[0].format <= DK_ATTRIBUTE_FLOAT4 && !command->inline_bytes)
		{
			const unsigned char *at = (const unsigned char *)(uintptr_t)command->streams[slot][0];
			unsigned b;

			memcpy(first, at + f->attributes[0].offset, sizeof(float) * 3);
			for (b = 0; b < 32; b++)
				snprintf(bytes + b * 2 + b / 4, sizeof(bytes) - b * 2 - b / 4, "%02x%s", at[b], b % 4 == 3 ? " " : "");
		}
		slot++;
	}
	wiiu_log("GX2 draw %u: vs %u ps %u prim %u count %u idx %s off %d streams %u (%08x, %u bytes, stride %u); attrs%s; v0 %g %g %g bytes %s", gx.traced, (unsigned)gx.vs_handle, (unsigned)gx.ps_handle,
		(unsigned)command->primitive, (unsigned)command->count, command->index_address ? "yes" : "no", (int)command->vertex_offset,
		(unsigned)command->stream_count, command->stream_count ? (unsigned)command->streams[0][0] : 0, command->stream_count ? (unsigned)command->streams[0][1] : 0,
		(unsigned)f->strides[0], text, first[0], first[1], first[2], bytes);
	if (command->inline_bytes)
	{
		const float *vertices = (const float *)(const void *)&command->streams[command->stream_count];
		unsigned vertex;

		for (vertex = 0; vertex < command->count && vertex < 4; vertex++)
		{
			const float *v = vertices + vertex * 64;

			wiiu_log("GX2   vertex %u: r0 %g %g %g %g | r4 %g %g %g %g | r9 %g %g %g %g", vertex, v[0], v[1], v[2], v[3], v[16], v[17], v[18], v[19], v[36], v[37],
				v[38], v[39]);
		}
	}
	wiiu_log("GX2   format: stream mask %x little-endian %x static %x strides %u %u %u %u; stream bytes (second stream first 16): %s", (unsigned)f->stream_mask,
		(unsigned)f->little_endian, (unsigned)f->static_streams, (unsigned)f->strides[0], (unsigned)f->strides[1], (unsigned)f->strides[2], (unsigned)f->strides[3],
		command->stream_count > 1 && !command->inline_bytes ? "see below" : "-");
	if (command->stream_count > 1 && !command->inline_bytes)
	{
		const unsigned char *at = (const unsigned char *)(uintptr_t)command->streams[1][0];

		wiiu_log("GX2   stream 2 at %08x (%u bytes): %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x", (unsigned)command->streams[1][0],
			(unsigned)command->streams[1][1], at[0], at[1], at[2], at[3], at[4], at[5], at[6], at[7], at[8], at[9], at[10], at[11], at[12], at[13], at[14], at[15]);
	}
	wiiu_log("GX2   state: vp %d,%d %dx%d z %g..%g cull %u ccw %u depth %u/%u/%x stencil %u blend %u %x->%x mask %x; tex %u/%u %u/%u %u/%u %u/%u",
		(int)s->viewport[0], (int)s->viewport[1], (int)s->viewport[2], (int)s->viewport[3], s->depth_range[0], s->depth_range[1], (unsigned)s->cull,
		(unsigned)s->front_face_ccw, (unsigned)s->depth_test, (unsigned)s->depth_write, (unsigned)s->depth_function, (unsigned)s->stencil_test,
		(unsigned)s->blend, (unsigned)s->blend_source, (unsigned)s->blend_destination, (unsigned)s->color_mask, (unsigned)gx.stages[0].id,
		(unsigned)gx.stages[0].target, (unsigned)gx.stages[1].id, (unsigned)gx.stages[1].target, (unsigned)gx.stages[2].id, (unsigned)gx.stages[2].target,
		(unsigned)gx.stages[3].id, (unsigned)gx.stages[3].target);
	{
		const struct dk_pixel_parameters *p = &gx.pixel_parameters;
		const struct shader *pixel = shader_table[gx.ps_handle];

		wiiu_log("GX2   ps %u: texture_scale at %d (%u floats): %g %g %g %g | %g %g %g %g | %g %g %g %g; ps_c0[0] %g %g %g %g; alpha_ref %g; samplers %d %d %d %d", (unsigned)gx.ps_handle,
			pixel ? pixel->uniform[PS_TEXTURE_SCALE] : -2, pixel ? (unsigned)pixel->uniform_floats[PS_TEXTURE_SCALE] : 0u, p->texture_scale[0][0],
			p->texture_scale[0][1], p->texture_scale[0][2], p->texture_scale[0][3], p->texture_scale[1][0], p->texture_scale[1][1], p->texture_scale[1][2],
			p->texture_scale[1][3], p->texture_scale[2][0], p->texture_scale[2][1], p->texture_scale[2][2], p->texture_scale[2][3], p->ps_c0[0][0],
			p->ps_c0[0][1], p->ps_c0[0][2], p->ps_c0[0][3], p->alpha_reference[0], pixel ? pixel->sampler[0] : -2, pixel ? pixel->sampler[1] : -2,
			pixel ? pixel->sampler[2] : -2, pixel ? pixel->sampler[3] : -2);
	}
	{
		unsigned row, shown = 0;
		char rows[400];
		size_t at = 0;
		const struct shader *vertex = shader_table[gx.vs_handle];

		for (row = 0; row < 192 && shown < 8; row++)
			if (gx.constants[row][0] != 0.0f || gx.constants[row][1] != 0.0f || gx.constants[row][2] != 0.0f || gx.constants[row][3] != 0.0f)
			{
				at += (size_t)snprintf(rows + at, sizeof(rows) - at, " c%u=(%g %g %g %g)", row, gx.constants[row][0], gx.constants[row][1], gx.constants[row][2],
					gx.constants[row][3]);
				shown++;
			}
		if (vertex)
			wiiu_log("GX2   vs %u: point_size at %d, screen_offset at %d (%u floats): point_and_screen %g %g", (unsigned)gx.vs_handle, vertex->uniform[VS_POINT_SIZE],
				vertex->uniform[VS_SCREEN_OFFSET], (unsigned)vertex->uniform_floats[VS_SCREEN_OFFSET], gx.vertex_parameters.point_and_screen[0],
				gx.vertex_parameters.point_and_screen[1]);
		wiiu_log("GX2   vs %u: c at %d (%u floats), %u..%u dirty:%s", (unsigned)gx.vs_handle, vertex ? vertex->uniform[VS_C] : -2, vertex ? (unsigned)vertex->uniform_floats[VS_C] : 0u,
			gx.constants_lo, gx.constants_hi, rows);
	}
	wiiu_log("GX2   vp params: scale %g %g %g %g offset %g %g %g %g; c[0..3] %g %g %g %g | %g %g %g %g", gx.vertex_parameters.viewport_scale[0],
		gx.vertex_parameters.viewport_scale[1], gx.vertex_parameters.viewport_scale[2], gx.vertex_parameters.viewport_scale[3],
		gx.vertex_parameters.viewport_offset[0], gx.vertex_parameters.viewport_offset[1], gx.vertex_parameters.viewport_offset[2],
		gx.vertex_parameters.viewport_offset[3], gx.constants[0][0], gx.constants[0][1], gx.constants[0][2], gx.constants[0][3], gx.constants[1][0],
		gx.constants[1][1], gx.constants[1][2], gx.constants[1][3]);
}

/* an attribute buffer register, written only if it holds something else (the constants' buffer is the same for every draw that has the same format) */
static void attrib_set(unsigned index, uint32_t size, uint32_t stride, void *buffer)
{
	if (gx.attrib[index].buffer == buffer && gx.attrib[index].size == size && gx.attrib[index].stride == stride)
		return;
	GX2SetAttribBuffer(index, size, stride, buffer);
	gx.attrib[index].buffer = buffer;
	gx.attrib[index].size = size;
	gx.attrib[index].stride = stride;
}

static void draw(const struct dk_command_draw *command)
{
	/* (HOME can come between two draws: the game thread is held here, before the next call into GX2, not at the end of the stream) */
	gx_gate();
	gx.draw_started = OSGetSystemTime();
	struct shader *vs, *ps;
	struct fetch *fetch;
	const unsigned char *inline_data = (const unsigned char *)&command->streams[command->stream_count];
	void *buffers[DK_STREAM_COUNT];
	uint32_t sizes[DK_STREAM_COUNT];
	void *indices = NULL;
	void *constants;
	unsigned stream, slot = 0;
	const uint32_t primitive = command->primitive & ~DK_DRAW_RANGE_LATER;
	int32_t vertex_offset = command->vertex_offset;
	uint32_t lowest = 0, highest = 0;
	const int range_later = (command->primitive & DK_DRAW_RANGE_LATER) != 0;

	if (!gx.color && !gx.depth)
	{
		gx.skipped_target++;
		return;
	}
	if (range_later)
	{
		/* (the game thread left the indices' range to this thread, the list copied into the command or the map's own: d3d8_gx2.c, draw_write) */
		const uint16_t *list = command->index_address == DK_INDEX_INLINE ? (const uint16_t *)(const void *)(inline_data + ((command->inline_bytes + 3u) & ~3u)) :
			(const uint16_t *)(uintptr_t)command->index_address;

		wiiu_index_range(list, command->count, &lowest, &highest);
		vertex_offset = -(int32_t)lowest;
	}
	if (!gx.vs_handle || !gx.ps_handle || !gx.format_set)
	{
		gx.skipped_state++;
		if (gx.format_set)
			gx.skipped_missing++;
		return;
	}
	{
		const OSTime load_start = OSGetSystemTime();

		composites_prepare();
		vs = shader_get(gx.vs_handle);
		ps = shader_get(gx.ps_handle);
		gx.shader_ticks += OSGetSystemTime() - load_start;
	}
	if (!vs || !ps)
	{
		gx.skipped_shader++;
		return;
	}
	if (gx.memo_vs == vs && gx.memo_serial == gx.format_serial)
		fetch = gx.memo_fetch;
	else
	{
		fetch = fetch_get(vs);
		gx.memo_vs = vs;
		gx.memo_serial = gx.format_serial;
		gx.memo_fetch = fetch;
	}
	if (!fetch)
	{
		gx.skipped_shader++;
		return;
	}
	memset(buffers, 0, sizeof(buffers));
	for (stream = 0; stream < DK_STREAM_COUNT; stream++)
	{
		const unsigned char *source;

		if (!((gx.format.stream_mask >> stream) & 1))
			continue;
		if (slot >= command->stream_count)
			break;
		if (range_later)
		{
			const uint32_t stride = command->streams[slot][1];

			sizes[stream] = stride ? stride * (highest - lowest + 1) : 64u;
			source = (const unsigned char *)(uintptr_t)(command->streams[slot][0] + stride * lowest);
		}
		else
		{
			sizes[stream] = command->streams[slot][1];
			source = command->inline_bytes ? inline_data : (const unsigned char *)(uintptr_t)command->streams[slot][0];
		}
		buffers[stream] = NULL;
		if (!command->inline_bytes && ((gx.format.static_streams >> stream) & 1))
		{
			buffers[stream] = block_get((uint32_t)(uintptr_t)source, sizes[stream]);
			if (!buffers[stream])
				buffers[stream] = cache_get((uint32_t)(uintptr_t)source, sizes[stream]);
		}
		gx.ring_what = command->inline_bytes ? 1 : 0;
		if (!buffers[stream])
		{
			buffers[stream] = ring_copy(source, sizes[stream]);
			if (!command->inline_bytes && sizes[stream] >= 16384)
				big_copy_note((uint32_t)(uintptr_t)source, sizes[stream], (gx.format.static_streams >> stream) & 1);
		}
		if (!buffers[stream])
		{
			gx.skipped_ring++;
			return;
		}
		slot++;
	}
	if (gx.constants_ring && gx.constants_frame == gx.frames && gx.constants_serial == gx.format_serial)
		constants = gx.constants_ring;
	else
	{
		gx.ring_what = 2;
		constants = ring_copy(gx.format.constants, sizeof(gx.format.constants));
		gx.constants_ring = constants;
		gx.constants_frame = gx.frames;
		gx.constants_serial = gx.format_serial;
	}
	if (command->index_address)
	{
		const unsigned char *source;

		if (command->index_address == DK_INDEX_INLINE)
			source = inline_data + ((command->inline_bytes + 3u) & ~3u);
		else
			source = (const unsigned char *)(uintptr_t)command->index_address;
		gx.index_bytes += command->count * 2;
		indices = command->index_address != DK_INDEX_INLINE ? cache_get((uint32_t)(uintptr_t)source, command->count * 2) : NULL;
		gx.ring_what = 3;
		if (!indices)
			indices = ring_copy(source, command->count * 2);
		if (!indices)
		{
			gx.skipped_ring++;
			return;
		}
	}
	if (!constants)
	{
		gx.skipped_ring++;
		return;
	}
	if (gx.trace_frame >= 0 && (int)gx.frames == gx.trace_frame && gx.traced < 64)
	{
		draw_trace(command, vs, ps);
		gx.traced++;
	}
	if (gx.debug_mode == 3)
		return; /* (no draws at all: what the frame costs without the GPU's part) */
	if (gx.debug_mode == 1 && gx.state.blend)
	{
		gx.state.blend = 0;
		gx.state_dirty = 1;
	}
	if (gx.debug_mode == 2 && gx.state.cull)
	{
		gx.state.cull = 0;
		gx.state_dirty = 1;
	}
	{
		const OSTime t0 = OSGetSystemTime();

		gx.section_ticks[0] += t0 - gx.draw_started;
		gx.section_now = t0;
	}
	if (gx.state_dirty)
		state_apply();
	gx.section_ticks[1] += OSGetSystemTime() - gx.section_now;
	gx.section_now = OSGetSystemTime();
	if (gx.cur_fetch != fetch || gx.cur_vs != vs)
	{
		gx.counts_fetch++;
		GX2SetFetchShader(&fetch->shader);
		gx.cur_fetch = fetch;
	}
	if (gx.cur_vs != vs)
	{
		gx.counts_vs++;
		GX2SetVertexShader(vs->vertex);
		gx.cur_vs = vs;
	}
	if (gx.cur_ps != ps)
	{
		gx.counts_ps++;
		GX2SetPixelShader(ps->pixel);
		gx.cur_ps = ps;
	}
	gx.section_ticks[2] += OSGetSystemTime() - gx.section_now;
	gx.section_now = OSGetSystemTime();
	uniforms_apply(vs, ps);
	gx.section_ticks[3] += OSGetSystemTime() - gx.section_now;
	gx.section_now = OSGetSystemTime();
	textures_bind(ps);
	gx.section_ticks[4] += OSGetSystemTime() - gx.section_now;
	gx.section_now = OSGetSystemTime();
	for (stream = 0; stream < DK_STREAM_COUNT; stream++)
		if (buffers[stream])
			attrib_set(stream, sizes[stream], gx.format.strides[stream], buffers[stream]);
	attrib_set(CONSTANTS_BUFFER, sizeof(gx.format.constants), 0, constants);
	if (gx.color)
		gx.color->last_used = ++gx.use_serial;
	gx.section_ticks[5] += OSGetSystemTime() - gx.section_now;
	gx.section_now = OSGetSystemTime();
	if (!gx.gpu_frame_open && !gx.replaying)
		gpu_frame_begin();
	if (indices)
		GX2DrawIndexedEx(primitive_of(primitive), command->count, GX2_INDEX_TYPE_U16, indices, (uint32_t)vertex_offset, 1);
	else
		GX2DrawEx(primitive_of(primitive), command->count, 0, 1);
	gx.section_ticks[6] += OSGetSystemTime() - gx.section_now;
	gx.draws_made++;
	gx.draw_total++;
	if (!(gx.draw_total & 31))
		gpu_poll();
}

int wiiu_memcpy_selftest(void);

/* wiiu_core_probe.c: what the other core sees of this one's writes, if halo/probe_core.txt is there */
void wiiu_core_probe(void);

/* wiiu_profile.c: a sampling profiler of this thread, if halo/profile.txt is there */
void wiiu_profile_start(void);

/* ---------- starting */

/* (There is no GX2 shutdown at exit: the shell drains the GPU before the app is let go, and any GX2 call after ProcUI has been shut down - which is where atexit handlers run - freezes the console
with the last picture on the TV. One was here, and froze every run that ended with a map loaded.) */

/* set when a GX2 thread makes the context: the profiler then follows the game's thread (gt_start starts it) */
static int profile_by_game_thread;

static int initialize(void)
{
	uint32_t i;

	if (gx.ready)
		return 1;
	if (gx.failed)
		return 0;
	gx.failed = 1;
	if (!WHBGfxInit())
	{
		wiiu_log("GX2: WHBGfxInit failed");
		return 0;
	}
	/* display.vsync off: the display shows a frame as soon as it is swapped (it can tear), not at the next refresh: frames of 21 ms are then 47 a second, not 40 (a frame is one refresh
	or two) */
	if (!config_boolean("display.vsync"))
	{
		GX2SetSwapInterval(0);
		wiiu_log("GX2: no vsync (display.vsync is false): frames are shown as soon as they are swapped");
	}
	if (!WHBGfxLoadGFDShaderGroup(&gx.blit, 0, wiiu_gx2_blit_gsh))
	{
		wiiu_log("GX2: the blit shaders did not load");
		return 0;
	}
	if (!WHBGfxInitShaderAttribute(&gx.blit, "a_pos", 0, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32) ||
		!WHBGfxInitShaderAttribute(&gx.blit, "a_uv", 0, 8, GX2_ATTRIB_FORMAT_FLOAT_32_32) ||
		!WHBGfxInitShaderAttribute(&gx.blit, "a_col", 0, 16, GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32) || !WHBGfxInitFetchShader(&gx.blit))
	{
		wiiu_log("GX2: the blit shaders lack an input");
		return 0;
	}
	for (i = 0; i < gx.blit.pixelShader->samplerVarCount; i++)
		if (!strcmp(gx.blit.pixelShader->samplerVars[i].name, "tex0"))
			gx.sampler_location = gx.blit.pixelShader->samplerVars[i].location;
	GX2InitSampler(&gx.sampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_LINEAR);
	gx.quad = gx_alloc(6 * sizeof(struct blit_vertex), GX2_VERTEX_BUFFER_ALIGNMENT);
	if (!gx.quad)
	{
		wiiu_log("GX2: no memory for the blit's quad");
		return 0;
	}
	images = calloc(DK_TEXTURE_LIMIT, sizeof(*images));
	if (!images || !white_make())
	{
		wiiu_log("GX2: no memory for the texture table");
		return 0;
	}
	for (i = 0; i < RING_SEGMENTS; i++)
	{
		gx.ring[i].base = gx_alloc_raw(RING_SEGMENT_BYTES, GX2_VERTEX_BUFFER_ALIGNMENT);
		if (!gx.ring[i].base)
		{
			wiiu_log("GX2: no memory for the vertex ring (%u bytes)", (unsigned)RING_SEGMENT_BYTES);
			return 0;
		}
	}
	/* display.render_lines (480, 540, 720, 900 or 1080); a note in halo/render_height.txt overrides it, for a run that tries another */
	gx.render_height = 540;
	{
		const long setting = config_integer("display.render_lines");
		FILE *file = fopen(WIIU_DATA_DIRECTORY "/render_height.txt", "r");
		unsigned lines = 0;

		if (setting >= 240 && setting <= (long)WHBGfxGetTVColourBuffer()->surface.height)
			gx.render_height = (unsigned)setting;
		if (file)
		{
			if (fscanf(file, "%u", &lines) == 1 && lines >= 240 && lines <= WHBGfxGetTVColourBuffer()->surface.height)
				gx.render_height = lines;
			fclose(file);
		}
		wiiu_log("GX2: the game draws at %u lines (display.render_lines, or halo/render_height.txt)", gx.render_height);
	}
	/* Dynamic resolution's targets are those lines; a frame can draw fewer. A note in halo/drs_lines.txt draws that many for the whole run, whatever
	display.dynamic_resolution says, and the controller does not run: for a run that tries one step (0.9, 0.8 and 0.75 of 720 lines are 648, 576 and
	540; of 1080, 972, 864 and 810), or the targets' own lines with the controller off. */
	{
		FILE *file = fopen(WIIU_DATA_DIRECTORY "/drs_lines.txt", "r");
		unsigned lines = 0;

		if (file)
		{
			if (fscanf(file, "%u", &lines) == 1 && lines >= 240 && lines <= gx.render_height)
				gx.drs_forced = lines;
			fclose(file);
		}
		gx.drs_lines = gx.drs_forced ? gx.drs_forced : gx.render_height;
		if (gx.drs_forced)
			wiiu_log("GX2: dynamic resolution: the frames draw %u of the targets' %u lines for this run (halo/drs_lines.txt); its controller is off", gx.drs_forced,
				gx.render_height);
	}
	/* halo/mem1.txt: 1, the game's render targets (the big ones) are made in the eDRAM */
	{
		FILE *file = fopen(WIIU_DATA_DIRECTORY "/mem1.txt", "r");
		int on = 1; /* (on unless the file says 0: HOME, return and closing were tried on the console with it on; a file without a number is the default) */

		if (file)
		{
			if (fscanf(file, "%d", &on) != 1)
				on = 1;
			fclose(file);
		}
		gx.mem1 = on;
		wiiu_log("GX2: render targets in the eDRAM (halo/mem1.txt): %s", on ? "yes" : "no");
		/* (off unless the file says 1: it gained nothing measurable in the first room at 720 lines, and enemies were seen drawn without their bodies with it on,
		2026-10-28 - not shown to be its doing) */
		on = 0;
		file = fopen(WIIU_DATA_DIRECTORY "/hiz.txt", "r");
		if (file)
		{
			if (fscanf(file, "%d", &on) != 1)
				on = 0;
			fclose(file);
		}
		gx.hiz = on;
		wiiu_log("GX2: hierarchical Z (halo/hiz.txt): %s", on ? "yes" : "no");
		file = fopen(WIIU_DATA_DIRECTORY "/mem1_fail.txt", "r");
		if (file)
		{
			if (fscanf(file, "%d", &on) == 1 && on > 0)
			{
				gx.fail_allocations = (unsigned)on;
				wiiu_log("GX2: a test: the next %d allocations of targets made again after HOME fail (halo/mem1_fail.txt)", on);
			}
			fclose(file);
		}
	}
	/* a note in halo/exit_after.txt (seconds) ends the run by itself that long after the first frame: for runs nobody is at the console for */
	{
		FILE *file = fopen(WIIU_DATA_DIRECTORY "/exit_after.txt", "r");

		if (file)
		{
			if (fscanf(file, "%lf", &gx.exit_after) != 1)
				gx.exit_after = 0.0;
			fclose(file);
		}
		gx.trace_frame = -1;
		file = fopen(WIIU_DATA_DIRECTORY "/gx2_trace.txt", "r");
		if (file)
		{
			if (fscanf(file, "%d %d %d %d", &gx.trace_frame, &gx.debug_mode, &gx.features_off, &gx.trace_serial) < 1)
				gx.trace_frame = -1;
			fclose(file);
		}
		if (gx.exit_after > 0.0)
			wiiu_log("GX2: the run ends %.0f seconds after its first frame (exit_after.txt)", gx.exit_after);
		file = fopen(WIIU_DATA_DIRECTORY "/gpu_test.txt", "r");
		if (file)
		{
			int asked = 0;

			gx.gpu_test_draws = 0;
			gx.gpu_test_wanted = fscanf(file, "%d %d", &asked, &gx.gpu_test_draws) >= 1 && asked == 1;
			if (gx.gpu_test_draws <= 0)
				gx.gpu_test_draws = 250;
			fclose(file);
			if (gx.gpu_test_wanted)
				wiiu_log("GX2: the GPU test runs at the first frame of %d draws or more after the 300th (gpu_test.txt)", gx.gpu_test_draws);
		}
	}
	if (overlay_make())
	{
		FILE *file = fopen(WIIU_DATA_DIRECTORY "/overlay.txt", "r");
		int on = 0;

		if (file)
		{
			if (fscanf(file, "%d", &on) != 1)
				on = 0;
			fclose(file);
		}
		gx.overlay_on = 1;
		gx.overlay_shown = on != 0;
		wiiu_log("GX2: the performance overlay is %s (halo/overlay.txt 1 shows it from the start; L + R + - on the GamePad shows or hides it)", gx.overlay_shown ? "shown" : "hidden");
	}
	{
		/* how fast the index range runs on this machine: over a buffer made as the game's index buffers are (calloc), written, then read at once, then again, then with a 1 MB sweep between
		(the L1 holds 32 KB; the L2 the core has 0.5 to 2 MB) */
		const uint32_t bytes = 256 * 1024;
		uint16_t *buffer = calloc(1, bytes), *other = calloc(1, 2 * 1024 * 1024);
		uint32_t lowest, highest, round;

		if (buffer && other)
		{
			for (round = 0; round < bytes / 2; round++)
				buffer[round] = (uint16_t)(round * 7);
			for (round = 0; round < 3; round++)
			{
				const OSTime start = OSGetSystemTime();

				if (round == 2)
					memset(other, round, 2 * 1024 * 1024);
				{
					const OSTime t = OSGetSystemTime();

					wiiu_index_range(buffer, bytes / 2, &lowest, &highest);
					wiiu_log("GX2: index range over %u KB: %s %.2f ms (%u..%u)", bytes / 1024, round == 0 ? "just written" : round == 1 ? "again" : "after a 2 MB sweep",
						(double)OSTicksToMicroseconds(OSGetSystemTime() - t) / 1000.0, (unsigned)lowest, (unsigned)highest);
				}
				(void)start;
			}
		}
		/* the copy, as a draw makes it: into a buffer the size of the stream's (the command's index area), from the list just read, and again with the source pushed out of the cache */
		if (buffer && other)
		{
			uint16_t *destination = (uint16_t *)other;
			unsigned pass;

			for (pass = 0; pass < 4; pass++)
			{
				OSTime t;

				if (pass == 2)
					memset(other + 262144 / 2, 0x55, 1536 * 1024); /* (a 1.5 MB sweep: the source leaves the L1 and most of the L2) */
				if (pass == 3)
					memset(other + 262144 / 2, 0x66, 1536 * 1024);
				t = OSGetSystemTime();
				if (pass == 1 || pass == 3)
					memcpy(destination, buffer, bytes);
				else
					wiiu_index_copy_range(destination, buffer, bytes / 2, &lowest, &highest);
				wiiu_log("GX2: %s of %u KB: %s %.2f ms", pass == 1 || pass == 3 ? "memcpy" : "index copy and range", bytes / 1024, pass < 2 ? "source just read" : "source pushed out of the cache",
					(double)OSTicksToMicroseconds(OSGetSystemTime() - t) / 1000.0);
			}
		}
		free(buffer);
		free(other);
	}
	{
		FILE *file = fopen(WIIU_DATA_DIRECTORY "/probe_core.txt", "r");

		int asked = 0;

		if (file)
		{
			if (fscanf(file, "%d", &asked) != 1)
				asked = 0;
			fclose(file);
			if (asked == 1)
				wiiu_core_probe();
		}
	}
	wiiu_log("memcpy/memset self-test (wiiu_memcpy.c): %d wrong", wiiu_memcpy_selftest());
	if (!profile_by_game_thread)
		wiiu_profile_start(); /* (with a GX2 thread the game's thread is the one to sample: gt_start does it) */
	gx.state_dirty = 1;
	gx.constants_lo = 192;
	gx.constants_hi = 0;
	gx.failed = 0;
	gx.ready = 1;
	wiiu_log("GX2: started; the TV is %ux%u, the GamePad %ux%u", (unsigned)WHBGfxGetTVColourBuffer()->surface.width,
		(unsigned)WHBGfxGetTVColourBuffer()->surface.height, (unsigned)WHBGfxGetDRCColourBuffer()->surface.width,
		(unsigned)WHBGfxGetDRCColourBuffer()->surface.height);
	return 1;
}

/* ---------- the platform layer's calls about the screen (sdl_platform.h's, as the Wii U answers them) */

/* the display's size: the TV's, in pixels */
int platform_screen_mode(long *width, long *height);

static int gt_start(void);

int platform_screen_mode(long *width, long *height)
{
	if (!gt_start())
		return 0;
	*height = (long)gx.render_height;
	*width = (long)((uint64_t)WHBGfxGetTVColourBuffer()->surface.width * gx.render_height / WHBGfxGetTVColourBuffer()->surface.height);
	return 1;
}

/* "creates the window and makes its OpenGL context current": the GX2 context was made by initialize */
int platform_video_initialize(unsigned long width, unsigned long height);

int platform_video_initialize(unsigned long width, unsigned long height)
{
	(void)width;
	(void)height;
	return gt_start();
}

/* the swap is the present command's */
void platform_video_swap(void);

void platform_video_swap(void)
{
}

/* the window's and controllers' events: ProcUI is the shell's, and the input layer is not written */
void platform_pump_events(void);

void platform_pump_events(void)
{
}

void platform_menus_set_active(int active);

void platform_menus_set_active(int active)
{
	(void)active;
}

/* ---------- render targets */

/* The surface's image: in the eDRAM (MEM1) if halo/mem1.txt says so and it is big, as libwhb does for the TV's and the GamePad's buffers (the DDR3 the colors and depths of every pixel drawn
would go to is shared with the CPU: the frame's fill is what the GPU spends its time on); WHBGfx's allocator (GX2RSetAllocator) gives MEM1 for colour and depth buffers. The DDR3 if there is no room. */
#define MEM1_MIN_BYTES (512u * 1024u)

static void target_image(struct target *target, GX2Surface *s, int depth, int eDRAM_allowed)
{
	if (gx.mem1 && eDRAM_allowed)
	{
		const uint32_t imageSize = s->imageSize;

		GX2CalcSurfaceSizeAndAlignment(s);
		if (s->imageSize >= MEM1_MIN_BYTES &&
			GX2RCreateSurface(s, (GX2RResourceFlags)((depth ? GX2R_RESOURCE_BIND_DEPTH_BUFFER : GX2R_RESOURCE_BIND_COLOR_BUFFER) | GX2R_RESOURCE_BIND_TEXTURE |
				GX2R_RESOURCE_USAGE_GPU_READ | GX2R_RESOURCE_USAGE_GPU_WRITE)))
		{
			memset(s->image, 0, s->imageSize);
			target->in_mem1 = 1;
			return;
		}
		s->imageSize = imageSize;
	}
	s->image = gx_alloc(s->imageSize, s->alignment ? s->alignment : 256);
}

/* xinput_wiiu.c: L + R + - on the GamePad shows or hides the performance overlay */
void wiiu_overlay_toggle(void);

void wiiu_overlay_toggle(void)
{
	gx.overlay_shown = !gx.overlay_shown;
	wiiu_log("GX2: the performance overlay is %s (L + R + -)", gx.overlay_shown ? "shown" : "hidden");
}

/* HOME (the shell's release callback): the system takes the eDRAM, and libwhb makes its own heap and buffers again when the app is back */
void wiiu_gx2_mem1_lost(void);
void wiiu_gx2_mem1_restore(void);

void wiiu_gx2_mem1_lost(void)
{
	unsigned i, count = 0;

	if (!gx.mem1)
		return;
	/* Only noted: the images stay where they are until libwhb's release callback destroys its heap, which runs after this one (the higher priority first) and after the GPU
	is drained; a stream being run (the GX2 thread's, mid-job) may still use them until then, and no stream starts while this is set (run_stream waits for mem1_restore) */
	gx.mem1_lost = 1;
	for (i = 0; i < gx.target_count; i++)
		count += gx.targets[i].in_mem1;
	wiiu_log("GX2: HOME: the eDRAM's render targets (%u) are given up", count);
}

/* a target's image made again (after HOME) and its registers set for it; unusable if there is no memory (target_get tries again, once a second) */
static void target_remake(struct target *target, int eDRAM_allowed)
{
	const int depth = target->surface.kind == DK_SURFACE_DEPTH;
	GX2Surface *s = depth ? &target->depth.surface : &target->color.surface;

	target->in_mem1 = 0;
	if (gx.fail_allocations)
	{
		/* (halo/mem1_fail.txt N: the next N makings after HOME fail, the retries of target_get included: 1, a single failure; a large N, one that lasts) */
		gx.fail_allocations--;
		s->image = NULL;
	}
	else
		target_image(target, s, depth, eDRAM_allowed);
	target->unusable = !s->image;
	target->remade_at = OSGetSystemTime();
	if (target->unusable)
	{
		wiiu_log("GX2: no memory for the %ux%u %s target: nothing is drawn into it or from it until there is", (unsigned)s->width, (unsigned)s->height, depth ? "depth" : "color");
		return;
	}
	if (depth)
		GX2InitDepthBufferRegs(&target->depth);
	else
	{
		GX2InitColorBufferRegs(&target->color);
		target->texture.surface = target->color.surface;
		GX2InitTextureRegs(&target->texture);
	}
}

/* the targets again (in the eDRAM if libwhb has its heap back, else in the DDR3), and their registers; the shell's late acquire callback (the priority after libwhb's), or the GX2 thread
if that does not come (mem1_wait) */
static void mem1_restore(int eDRAM_allowed)
{
	unsigned i, again = 0, unusable = 0;

	/* (one thread at a time: the acquire callback and the GX2 thread's fallback could both come here) */
	while (__sync_lock_test_and_set(&gx.mem1_restoring, 1))
		OSSleepTicks(OSMillisecondsToTicks(1));
	if (!gx.mem1_lost)
	{
		__sync_lock_release(&gx.mem1_restoring);
		return;
	}
	for (i = 0; i < gx.target_count; i++)
	{
		struct target *target = &gx.targets[i];

		if (!target->in_mem1)
			continue;
		target_remake(target, eDRAM_allowed);
		again += target->in_mem1;
		unusable += target->unusable;
	}
	/* (the targets being drawn into are bound again by context_resume; if one of them has no memory, neither is: nothing is drawn until the game binds targets again) */
	if ((gx.color && gx.color->unusable) || (gx.depth && gx.depth->unusable))
		gx.color = gx.depth = NULL;
	gx.resume_needed = 1;
	__sync_synchronize(); /* (what was made is seen before the flag is) */
	gx.mem1_lost = 0;
	wiiu_log("GX2: back: %u render targets are in the eDRAM again, %u have no memory", again, unusable);
	__sync_lock_release(&gx.mem1_restoring);
}

void wiiu_gx2_mem1_restore(void)
{
	mem1_restore(1);
}

/* where the GX2 calls may go on after HOME: the app in the foreground and the targets made again. A thread held at the foreground gate in the middle of a stream (a draw, a submit)
comes back here before its next GX2 call; the context is set again (the targets' new images) before it goes on. If the acquire callback has not made the targets in 3 s, they are made
here, in the DDR3 (libwhb's heap may be being made by the thread of that callback). */
static void mem1_wait(void)
{
	const OSTime start = OSGetSystemTime();

	while (gx.mem1_lost && wiiu_in_foreground() && OSGetSystemTime() - start < OSMillisecondsToTicks(3000))
		OSSleepTicks(OSMillisecondsToTicks(2));
	if (gx.mem1_lost && wiiu_in_foreground())
	{
		wiiu_log("GX2: the targets were not made again by the acquire callback in 3 s: made here, in the DDR3");
		mem1_restore(0);
	}
}

static int gate_is_game_thread(void);

static void gx_gate(void)
{
	/* (held again if the foreground goes while the targets are waited for: no GX2 call in the background) */
	for (;;)
	{
		wiiu_foreground_gate(gate_is_game_thread());
		if (gx.mem1_lost)
			mem1_wait();
		if (!gx.mem1_lost && wiiu_in_foreground())
			break;
	}
	if (gx.resume_needed)
	{
		gx.resume_needed = 0;
		context_resume();
	}
}

/* whether a surface is a target's: all but the part drawn into, which dynamic resolution's step changes between frames (target_step), is the same.
One target serves every step, so a change of step makes none, and the screen's targets stay where they were made, in the eDRAM. */
static int surface_is_target(const struct dk_surface *a, const struct dk_surface *b)
{
	return a->data == b->data && a->width == b->width && a->height == b->height && a->kind == b->kind && a->pixel_width == b->pixel_width &&
		a->pixel_height == b->pixel_height;
}

static struct target *target_get(const struct dk_surface *surface)
{
	struct target *target;
	GX2Surface *s;
	unsigned i;
	const int depth = surface->kind == DK_SURFACE_DEPTH;

	if (surface->kind == DK_SURFACE_NONE || !surface->width || !surface->height)
		return NULL;
	for (i = 0; i < gx.target_count; i++)
		if (surface_is_target(&gx.targets[i].surface, surface))
		{
			if (gx.targets[i].unusable && OSGetSystemTime() - gx.targets[i].remade_at > OSMillisecondsToTicks(1000))
				target_remake(&gx.targets[i], 1);
			if (gx.targets[i].unusable)
			{
				gx.target_missing = 1;
				return NULL;
			}
			return &gx.targets[i];
		}
	if (gx.target_count == TARGET_LIMIT)
	{
		wiiu_log("GX2: more than %d render targets; %08x is not drawn into", TARGET_LIMIT, (unsigned)surface->data);
		return NULL;
	}
	target = &gx.targets[gx.target_count];
	memset(target, 0, sizeof(*target));
	s = depth ? &target->depth.surface : &target->color.surface;
	s->dim = GX2_SURFACE_DIM_TEXTURE_2D;
	s->width = SURFACE_PIXEL_WIDTH(surface);
	s->height = SURFACE_PIXEL_HEIGHT(surface);
	s->depth = 1;
	s->mipLevels = 1;
	s->format = depth ? GX2_SURFACE_FORMAT_FLOAT_D24_S8 : GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
	s->aa = GX2_AA_MODE1X;
	s->use = (GX2SurfaceUse)((depth ? GX2_SURFACE_USE_DEPTH_BUFFER : GX2_SURFACE_USE_COLOR_BUFFER) | GX2_SURFACE_USE_TEXTURE);
	s->tileMode = GX2_TILE_MODE_DEFAULT;
	GX2CalcSurfaceSizeAndAlignment(s);
	target->in_mem1 = 0;
	target_image(target, s, depth, 1);
	if (!s->image)
	{
		wiiu_log("GX2: no memory for the %ux%u %s target at %08x", (unsigned)s->width, (unsigned)s->height, depth ? "depth" : "color",
			(unsigned)surface->data);
		return NULL;
	}
	if (depth)
	{
		target->depth.viewNumSlices = 1;
		target->depth.depthClear = 1.0f;
		if (gx.hiz && s->imageSize >= MEM1_MIN_BYTES)
		{
			/* hierarchical Z (halo/hiz.txt): the GPU keeps a coarse depth per tile and rejects hidden tiles before shading them (a buffer of its own, set before the registers) */
			uint32_t size = 0, alignment = 0;

			GX2CalcDepthBufferHiZInfo(&target->depth, &size, &alignment);
			target->depth.hiZPtr = size ? gx_alloc(size, alignment ? alignment : 256) : NULL;
			target->depth.hiZSize = size;
			if (target->depth.hiZPtr)
			{
				GX2InitDepthBufferHiZEnable(&target->depth, TRUE);
				wiiu_log("GX2: hierarchical Z on for the %ux%u depth target (%u bytes)", (unsigned)s->width, (unsigned)s->height, (unsigned)size);
			}
		}
		GX2InitDepthBufferRegs(&target->depth);
	}
	else
	{
		target->color.viewNumSlices = 1;
		GX2InitColorBufferRegs(&target->color);
		target->texture.surface = target->color.surface;
		target->texture.viewFirstMip = 0;
		target->texture.viewNumMips = 1;
		target->texture.viewFirstSlice = 0;
		target->texture.viewNumSlices = 1;
		target->texture.compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
		GX2InitTextureRegs(&target->texture);
	}
	target->surface = *surface;
	gx.target_count++;
	wiiu_log("GX2: %s target %08x, %ux%u (%ux%u pixels, %u KB in the %s)", depth ? "depth" : "color", (unsigned)surface->data, (unsigned)surface->width,
		(unsigned)surface->height, (unsigned)s->width, (unsigned)s->height, (unsigned)(s->imageSize / 1024), target->in_mem1 ? "eDRAM" : "DDR3");
	return target;
}

static void context_begin(void)
{
	GX2SetContextState(WHBGfxGetTVContextState());
}

/* after a clear or a frame's presenting, which used the context for something else: the game's targets back, and everything a draw needs set again */
static void context_resume(void)
{
	context_begin();
	if (gx.color)
		GX2SetColorBuffer(&gx.color->color, GX2_RENDER_TARGET_0);
	if (gx.depth)
		GX2SetDepthBuffer(&gx.depth->depth);
	GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
	gx.state_dirty = 1;
	gx.regs_vs = gx.regs_ps = gx.cur_vs = gx.cur_ps = NULL;
	gx.cur_fetch = NULL;
	memset(gx.bound_texture, 0, sizeof(gx.bound_texture));
	memset(gx.attrib, 0, sizeof(gx.attrib));
	gx.vertex_parameters_dirty = gx.pixel_parameters_dirty = 1;
}

/* A target bound to be drawn into at another step of dynamic resolution than it was last drawn at (the step changes between frames): cleared whole
first. What the step before drew outside the new part would otherwise stay there, where a draw that samples near the part's edge (a blur's taps, the
blit's filter) can read it; cleared, the edge reads black. A target made since was cleared when it was made. */
static void target_step(struct target *target, const struct dk_surface *surface)
{
	if (!target || (target->surface.active_width == surface->active_width && target->surface.active_height == surface->active_height))
		return;
	target->surface.active_width = surface->active_width;
	target->surface.active_height = surface->active_height;
	if (target->surface.kind == DK_SURFACE_DEPTH)
		GX2ClearDepthStencilEx(&target->depth, 1.0f, 0, (GX2ClearFlags)(GX2_CLEAR_FLAGS_DEPTH | GX2_CLEAR_FLAGS_STENCIL));
	else
		GX2ClearColor(&target->color, 0.0f, 0.0f, 0.0f, 0.0f);
	target->last_used = ++gx.use_serial;
	gx.drs_clears++;
}

static void targets_bind(const struct dk_command_targets *command)
{
	gx.target_missing = 0;
	gx.color = target_get(&command->color);
	gx.depth = target_get(&command->depth);
	/* (the clears use the context; context_resume, below, sets the frame's again) */
	target_step(gx.color, &command->color);
	target_step(gx.depth, &command->depth);
	/* (a pass that asks for a colour and a depth target of which one has no memory is not drawn at all: half of it would draw into what the context last had; a pass
	that asks for only one, as the game's colour-only and depth-only passes do, is drawn as before) */
	if (gx.target_missing)
		gx.color = gx.depth = NULL;
	context_resume();
	if (gx.color || gx.depth)
	{
		const uint32_t width = gx.color ? gx.color->color.surface.width : gx.depth->depth.surface.width;
		const uint32_t height = gx.color ? gx.color->color.surface.height : gx.depth->depth.surface.height;

		GX2SetViewport(0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f);
		GX2SetScissor(0, 0, width, height);
	}
}

/* A clear of part of a target (a split-screen window), of some of its channels (the fog screen's alpha only) or of the stencil with the depth kept is a draw of
rectangles with the blit shader: the colour through the channel masks, the depth by a viewport whose near and far are the value, the stencil by replacing. */
static void clear_draw(const struct dk_command_clear *command, int colour, int depth, int stencil)
{
	const struct target *size_from = gx.color ? gx.color : gx.depth;
	const float width = (float)SURFACE_PIXEL_WIDTH(&size_from->surface), height = (float)SURFACE_PIXEL_HEIGHT(&size_from->surface);
	const unsigned count = command->rectangle_count ? command->rectangle_count : 1;
	struct blit_vertex *vertices;
	unsigned rectangle, i;
	static const unsigned corner_x[6] = { 0, 1, 0, 1, 1, 0 };
	static const unsigned corner_y[6] = { 0, 0, 1, 0, 1, 1 };
	const GX2StencilFunction keep = GX2_STENCIL_FUNCTION_KEEP, replace = GX2_STENCIL_FUNCTION_REPLACE;
	const GX2ChannelMask none = (GX2ChannelMask)0;
	const GX2ChannelMask all = GX2_CHANNEL_MASK_RGBA;
	unsigned mask = 0;

	if (count > 64)
		return;
	vertices = (struct blit_vertex *)ring_alloc(count * 6 * sizeof(struct blit_vertex));
	if (!vertices)
	{
		gx.skipped_ring++;
		return;
	}
	for (rectangle = 0; rectangle < count; rectangle++)
	{
		const uint32_t *r = command->rectangle_count ? command->rectangles[rectangle] : NULL;
		const float x0 = r ? (float)r[0] : 0.0f, y0 = r ? (float)r[1] : 0.0f, x1 = r ? x0 + (float)r[2] : width, y1 = r ? y0 + (float)r[3] : height;

		for (i = 0; i < 6; i++)
		{
			struct blit_vertex *v = &vertices[rectangle * 6 + i];

			v->x = (corner_x[i] ? x1 : x0) * 2.0f / width - 1.0f;
			v->y = 1.0f - (corner_y[i] ? y1 : y0) * 2.0f / height;
			v->u = v->v = 0.0f;
			v->r = command->color[0];
			v->g = command->color[1];
			v->b = command->color[2];
			v->a = command->color[3];
		}
	}
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, vertices, count * 6 * sizeof(struct blit_vertex));
	if (command->flags & DK_CLEAR_RED)
		mask |= 1;
	if (command->flags & DK_CLEAR_GREEN)
		mask |= 2;
	if (command->flags & DK_CLEAR_BLUE)
		mask |= 4;
	if (command->flags & DK_CLEAR_ALPHA)
		mask |= 8;
	GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
	GX2SetRasterizerClipControlEx(TRUE, TRUE, FALSE);
	GX2SetViewport(0.0f, 0.0f, width, height, depth ? command->depth : 0.0f, depth ? command->depth : 1.0f);
	GX2SetScissor(0, 0, (uint32_t)width, (uint32_t)height);
	GX2SetPolygonControl(GX2_FRONT_FACE_CCW, FALSE, FALSE, FALSE, GX2_POLYGON_MODE_TRIANGLE, GX2_POLYGON_MODE_TRIANGLE, FALSE, FALSE, FALSE);
	GX2SetColorControl(GX2_LOGIC_OP_COPY, 0, FALSE, TRUE);
	GX2SetTargetChannelMasks(colour ? (GX2ChannelMask)mask : none, all, all, all, all, all, all, all);
	if (depth || stencil)
	{
		GX2SetDepthStencilControl(TRUE, depth ? TRUE : FALSE, GX2_COMPARE_FUNC_ALWAYS, stencil ? TRUE : FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS,
			stencil ? replace : keep, stencil ? replace : keep, stencil ? replace : keep, GX2_COMPARE_FUNC_ALWAYS, stencil ? replace : keep,
			stencil ? replace : keep, stencil ? replace : keep);
		GX2SetStencilMask(0xff, 0xff, (uint8_t)command->stencil, 0xff, 0xff, (uint8_t)command->stencil);
	}
	else
	{
		GX2SetDepthStencilControl(FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS, FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS, keep, keep, keep, GX2_COMPARE_FUNC_ALWAYS, keep,
			keep, keep);
	}
	GX2SetFetchShader(&gx.blit.fetchShader);
	GX2SetVertexShader(gx.blit.vertexShader);
	GX2SetPixelShader(gx.blit.pixelShader);
	GX2SetAttribBuffer(0, count * 6 * sizeof(struct blit_vertex), sizeof(struct blit_vertex), vertices);
	gx.attrib[0].buffer = NULL;
	GX2SetPixelTexture(&gx.white, gx.sampler_location);
	GX2SetPixelSampler(&gx.sampler, gx.sampler_location);
	GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, count * 6, 0, 1);
	gx.rectangle_clears++;
}

static void clear(const struct dk_command_clear *command)
{
	const uint32_t color_flags = DK_CLEAR_RED | DK_CLEAR_GREEN | DK_CLEAR_BLUE | DK_CLEAR_ALPHA;
	const int whole_color = (command->flags & color_flags) == color_flags;
	const struct target *size_from = gx.color ? gx.color : gx.depth;
	int whole_rectangle = 1;

	/* (a clear of all the part the frame draws into is a clear of the whole target: at a lower step of dynamic resolution the rest is outside every
	viewport, and is better clear than left with what the step before drew there) */
	if (command->rectangle_count && size_from)
	{
		const uint32_t *rectangle = command->rectangles[0];

		whole_rectangle = command->rectangle_count == 1 && !rectangle[0] && !rectangle[1] && rectangle[2] >= SURFACE_ACTIVE_WIDTH(&size_from->surface) &&
			rectangle[3] >= SURFACE_ACTIVE_HEIGHT(&size_from->surface);
	}
	if (gx.color && (command->flags & color_flags))
	{
		if (whole_color && whole_rectangle)
			GX2ClearColor(&gx.color->color, command->color[0], command->color[1], command->color[2], command->color[3]);
		else
			clear_draw(command, 1, 0, 0);
		gx.color->last_used = ++gx.use_serial;
	}
	if (gx.depth && (command->flags & (DK_CLEAR_DEPTH | DK_CLEAR_STENCIL)))
	{
		const int both = (command->flags & (DK_CLEAR_DEPTH | DK_CLEAR_STENCIL)) == (DK_CLEAR_DEPTH | DK_CLEAR_STENCIL);

		if (whole_rectangle)
		{
			GX2ClearFlags flags = (GX2ClearFlags)0;

			if (command->flags & DK_CLEAR_DEPTH)
				flags = (GX2ClearFlags)(flags | GX2_CLEAR_FLAGS_DEPTH);
			if (command->flags & DK_CLEAR_STENCIL)
				flags = (GX2ClearFlags)(flags | GX2_CLEAR_FLAGS_STENCIL);
			GX2ClearDepthStencilEx(&gx.depth->depth, command->depth, (uint8_t)command->stencil, flags);
		}
		else
		{
			(void)both;
			clear_draw(command, 0, (command->flags & DK_CLEAR_DEPTH) != 0, (command->flags & DK_CLEAR_STENCIL) != 0);
		}
	}
	/* (a clear is a draw of its own: the state the draws need goes back) */
	context_resume();
}

/* ---------- presenting */

static void blit_state(void)
{
	GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS);
	GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);
	GX2SetColorControl(GX2_LOGIC_OP_COPY, 0, FALSE, TRUE);
	GX2SetTargetChannelMasks(GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA,
		GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA);
}

/* the back buffer, letterboxed, onto the screen whose color buffer and context state are given */
/* ---------- the performance overlay */

/* the game's configuration (platform code): a boolean setting by name */
int config_boolean(const char *name);

/* The GPU's time for a frame's draws, from the GPU's own clock: a bottom-of-pipe sample, which the GPU writes once everything handed to it before is done,
before the frame's first draw (so: when the last frame's pictures and swap were done, or when the first draw reached an idle GPU) and after its last
(before its pictures are copied to the screens and swapped, and so without the wait for the display). The difference is an upper bound of the GPU's
work for the frame: it includes the moments the GPU waited for commands still being made. (It replaced a measure that was not the GPU's: from the
frame's last flush to when the CPU next saw its submission counter retired, waits for the display and for the next poll included, 2026-10-30.) The
sampler is gx2.rpl's (wut's import library has it; its headers do not declare it). Its clock runs at 27 MHz: on the console the samples, read as they are
(big-endian), rose 26.8 MHz against the CPU's clock (2026-10-30), and the run logs its own measure of it once; gx2.rpl's GX2GPUTimeToCPUTime, as declared
in an unmerged wut fork, returned nonsense there (21350398519233349 CPU ticks for 27000000), so the difference is converted here. */
void GX2SampleBottomGPUCycle(uint64_t *timestamp);
#define GPU_TIMER_HZ 27000000ull

/* one sample to a cache line: the GPU writes it, and the CPU reads it only after dropping its own copy of the line */
struct gpu_sample
{
	volatile uint64_t value;
	uint8_t pad[56];
};
static struct gpu_sample gpu_samples[GPU_SAMPLE_SLOTS][2] __attribute__((aligned(64)));

static void gpu_sample(struct gpu_sample *sample)
{
	sample->value = UINT64_MAX;
	DCFlushRange(sample, sizeof(*sample));
	GX2SampleBottomGPUCycle((uint64_t *)&sample->value);
}

/* at the frame's first draw */
static void gpu_frame_begin(void)
{
	const unsigned slot = gx.gpu_slot;

	if (gx.gpu_slot_state[slot] != 0)
	{
		gx.gpu_frame_open = 2;
		return;
	}
	gpu_sample(&gpu_samples[slot][0]);
	gx.gpu_slot_state[slot] = 1;
	gx.gpu_frame_open = 1;
}

/* at the frame's present, before anything of it is drawn on the screens */
static void gpu_frame_end(void)
{
	const unsigned slot = gx.gpu_slot;

	if (gx.gpu_frame_open == 1)
	{
		gpu_sample(&gpu_samples[slot][1]);
		gx.gpu_slot_state[slot] = 2;
		gx.gpu_slot_ended[slot] = OSGetSystemTime();
		gx.gpu_slot = (slot + 1) % GPU_SAMPLE_SLOTS;
	}
	gx.gpu_frame_open = 0;
}

/* the frames whose samples the GPU has written: polled between draws and while waiting for the flip */
static void gpu_poll(void)
{
	unsigned slot;

	for (slot = 0; slot < GPU_SAMPLE_SLOTS; slot++)
	{
		uint64_t begun, ended;

		if (gx.gpu_slot_state[slot] != 2)
			continue;
		DCInvalidateRange(gpu_samples[slot], sizeof(gpu_samples[slot]));
		begun = gpu_samples[slot][0].value;
		ended = gpu_samples[slot][1].value;
		if (begun != UINT64_MAX && ended != UINT64_MAX)
		{
			{
				/* (the GPU's clock against the CPU's, once, over the first five seconds of samples) */
				static uint64_t first_sample;
				static OSTime first_seen;
				static int told;

				if (!first_seen)
				{
					first_sample = begun;
					first_seen = OSGetSystemTime();
				}
				else if (!told && OSGetSystemTime() - first_seen >= OSSecondsToTicks(5) && begun > first_sample)
				{
					told = 1;
					wiiu_log("GX2: the GPU's clock (its samples) runs at %.2f MHz by the CPU's (27 taken)",
						(double)(begun - first_sample) / ((double)OSTicksToMicroseconds(OSGetSystemTime() - first_seen)));
				}
			}
			if (ended >= begun && ended - begun < GPU_TIMER_HZ)
			{
				const OSTime ticks = (OSTime)((ended - begun) * (uint64_t)OSSecondsToTicks(1) / GPU_TIMER_HZ);

				gx.gpu_ms = (float)OSTicksToMicroseconds(ticks) / 1000.0f;
				gx.gpu_window_ticks += ticks;
				if (ticks > gx.gpu_window_longest)
					gx.gpu_window_longest = ticks;
				gx.gpu_window_frames++;
			}
			gx.gpu_slot_state[slot] = 0;
		}
		else if (OSGetSystemTime() - gx.gpu_slot_ended[slot] > OSMillisecondsToTicks(1000))
		{
			/* (not written in a second: the slot is given up) */
			gx.gpu_slot_state[slot] = 0;
		}
	}
}

static int overlay_make(void)
{
	GX2Surface *surface = &gx.overlay_texture.surface;

	surface->dim = GX2_SURFACE_DIM_TEXTURE_2D;
	surface->width = OVERLAY_WIDTH;
	surface->height = OVERLAY_HEIGHT;
	surface->depth = 1;
	surface->mipLevels = 1;
	surface->format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
	surface->aa = GX2_AA_MODE1X;
	surface->use = GX2_SURFACE_USE_TEXTURE;
	surface->tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
	GX2CalcSurfaceSizeAndAlignment(surface);
	surface->image = gx_alloc_raw(surface->imageSize, surface->alignment ? surface->alignment : 256);
	gx.overlay_quad = gx_alloc(6 * sizeof(struct blit_vertex), GX2_VERTEX_BUFFER_ALIGNMENT);
	if (!surface->image || !gx.overlay_quad)
		return 0;
	memset(surface->image, 0, surface->imageSize);
	gx.overlay_texture.viewNumMips = 1;
	gx.overlay_texture.viewNumSlices = 1;
	gx.overlay_texture.compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
	GX2InitTextureRegs(&gx.overlay_texture);
	GX2InitSampler(&gx.overlay_sampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_POINT);
	overlay_reset(&gx.overlay);
	return 1;
}

/* the text and graph drawn again into the texture */
static void overlay_refresh(void)
{
	static uint8_t pixels[OVERLAY_WIDTH * OVERLAY_HEIGHT * 4];
	GX2Surface *surface = &gx.overlay_texture.surface;
	unsigned y;

	overlay_render(&gx.overlay, pixels);
	for (y = 0; y < OVERLAY_HEIGHT; y++)
		memcpy((uint8_t *)surface->image + (size_t)y * surface->pitch * 4, pixels + (size_t)y * OVERLAY_WIDTH * 4, OVERLAY_WIDTH * 4);
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, surface->image, surface->imageSize);
}

/* the overlay over the top left of a screen's picture, in the context blit_to has just drawn it in (a scale of whole pixels per texel, so that the text is sharp) */
static void overlay_draw(GX2ColorBuffer *screen, GX2ContextState *context, unsigned scale)
{
	const float screen_width = (float)screen->surface.width, screen_height = (float)screen->surface.height;
	const float margin_x = screen_width * 0.02f;
	static unsigned told;

	scale = overlay_scale((unsigned)screen_width, scale);

	if (told < 2)
	{
		told++;
		wiiu_log("GX2: the overlay on a %ux%u screen, %u pixels to a texel", (unsigned)screen_width, (unsigned)screen_height, scale);
	}
	const float x0 = -1.0f + 2.0f * margin_x / screen_width, x1 = x0 + 2.0f * (float)(OVERLAY_WIDTH * scale) / screen_width;
	const float y1 = 1.0f, y0 = y1 - 2.0f * (float)(OVERLAY_HEIGHT * scale) / screen_height;
	static const unsigned corner_x[6] = { 0, 1, 0, 1, 1, 0 };
	static const unsigned corner_y[6] = { 0, 0, 1, 0, 1, 1 };
	unsigned i;

	for (i = 0; i < 6; i++)
	{
		struct blit_vertex *v = &gx.overlay_quad[i];

		v->x = corner_x[i] ? x1 : x0;
		v->y = corner_y[i] ? y0 : y1;
		v->u = (float)corner_x[i];
		v->v = (float)corner_y[i];
		v->r = v->g = v->b = v->a = 1.0f;
	}
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, gx.overlay_quad, 6 * sizeof(struct blit_vertex));
	GX2SetContextState(context);
	GX2SetColorBuffer(screen, GX2_RENDER_TARGET_0);
	GX2SetViewport(0.0f, 0.0f, screen_width, screen_height, 0.0f, 1.0f);
	GX2SetScissor(0, 0, (uint32_t)screen_width, (uint32_t)screen_height);
	blit_state();
	GX2SetColorControl(GX2_LOGIC_OP_COPY, 1, FALSE, TRUE);
	GX2SetBlendControl(GX2_RENDER_TARGET_0, GX2_BLEND_MODE_SRC_ALPHA, GX2_BLEND_MODE_INV_SRC_ALPHA, GX2_BLEND_COMBINE_MODE_ADD, TRUE, GX2_BLEND_MODE_ONE,
		GX2_BLEND_MODE_INV_SRC_ALPHA, GX2_BLEND_COMBINE_MODE_ADD);
	GX2SetFetchShader(&gx.blit.fetchShader);
	GX2SetVertexShader(gx.blit.vertexShader);
	GX2SetPixelShader(gx.blit.pixelShader);
	GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
	GX2SetAttribBuffer(0, 6 * sizeof(struct blit_vertex), sizeof(struct blit_vertex), gx.overlay_quad);
	GX2SetPixelTexture(&gx.overlay_texture, gx.sampler_location);
	GX2SetPixelSampler(&gx.overlay_sampler, gx.sampler_location);
	GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, 6, 0, 1);
}

/* the hold-to-skip bar (xinput_wiiu.c says how far the hold is, 0 to 1): a grey frame and a white fill growing from the left, at the bottom middle of each screen, as games
show a held skip. Its vertices are the ring's (each screen's draw its own: the GPU reads them after this returns). */
static volatile float skip_progress;

void wiiu_skip_progress(float progress);

void wiiu_skip_progress(float progress)
{
	skip_progress = progress < 0.0f ? 0.0f : progress > 1.0f ? 1.0f : progress;
}

static void skip_bar_draw(GX2ColorBuffer *screen, GX2ContextState *context, float progress)
{
	static const unsigned corner_x[6] = { 0, 1, 0, 1, 1, 0 };
	static const unsigned corner_y[6] = { 0, 0, 1, 0, 1, 1 };
	const float screen_width = (float)screen->surface.width, screen_height = (float)screen->surface.height;
	/* (in the clip space of the screen: x -1 to 1 left to right, y 1 at the top) */
	const float half_width = 0.16f, bar_height = 2.0f * 10.0f / 480.0f, border_x = 2.0f * 3.0f / 854.0f, border_y = 2.0f * 3.0f / 480.0f;
	const float bottom = -1.0f + 2.0f * 36.0f / 480.0f;
	const float rectangles[2][8] = {
		/* x0, y0, x1, y1, r, g, b, a */
		{ -half_width - border_x, bottom - border_y, half_width + border_x, bottom + bar_height + border_y, 0.35f, 0.35f, 0.35f, 0.7f }, /* (grey: the cinematics' letterbox under it is black) */
		{ -half_width, bottom, -half_width + 2.0f * half_width * progress, bottom + bar_height, 1.0f, 1.0f, 1.0f, 0.9f },
	};
	struct blit_vertex *vertices = ring_reserve(12 * sizeof(struct blit_vertex));
	unsigned rectangle, i;

	if (!vertices)
		return;
	for (rectangle = 0; rectangle < 2; rectangle++)
		for (i = 0; i < 6; i++)
		{
			struct blit_vertex *v = &vertices[rectangle * 6 + i];
			const float *r = rectangles[rectangle];

			v->x = corner_x[i] ? r[2] : r[0];
			v->y = corner_y[i] ? r[1] : r[3];
			v->u = v->v = 0.5f;
			v->r = r[4];
			v->g = r[5];
			v->b = r[6];
			v->a = r[7];
		}
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, vertices, 12 * sizeof(struct blit_vertex));
	GX2SetContextState(context);
	GX2SetColorBuffer(screen, GX2_RENDER_TARGET_0);
	GX2SetViewport(0.0f, 0.0f, screen_width, screen_height, 0.0f, 1.0f);
	GX2SetScissor(0, 0, (uint32_t)screen_width, (uint32_t)screen_height);
	blit_state();
	GX2SetColorControl(GX2_LOGIC_OP_COPY, 1, FALSE, TRUE);
	GX2SetBlendControl(GX2_RENDER_TARGET_0, GX2_BLEND_MODE_SRC_ALPHA, GX2_BLEND_MODE_INV_SRC_ALPHA, GX2_BLEND_COMBINE_MODE_ADD, TRUE, GX2_BLEND_MODE_ONE,
		GX2_BLEND_MODE_INV_SRC_ALPHA, GX2_BLEND_COMBINE_MODE_ADD);
	GX2SetFetchShader(&gx.blit.fetchShader);
	GX2SetVertexShader(gx.blit.vertexShader);
	GX2SetPixelShader(gx.blit.pixelShader);
	GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
	GX2SetAttribBuffer(0, 12 * sizeof(struct blit_vertex), sizeof(struct blit_vertex), vertices);
	GX2SetPixelTexture(&gx.white, gx.sampler_location);
	GX2SetPixelSampler(&gx.sampler, gx.sampler_location);
	GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, 12, 0, 1);
}

/* the back buffer's rectangle (x, y, width and height in its pixels: the part the frame drew, or the GamePad's view of a part of it), letterboxed, onto
the screen whose color buffer and context state are given; quad holds the blit's vertices, one for each screen (the GPU reads them after this returns) */
static void blit_to(struct target *source, const uint32_t rectangle[4], struct blit_vertex *quad, GX2ColorBuffer *screen, GX2ContextState *context)
{
	const float pool_width = (float)source->color.surface.width, pool_height = (float)source->color.surface.height;
	const float source_width = (float)rectangle[2], source_height = (float)rectangle[3];
	const float screen_width = (float)screen->surface.width, screen_height = (float)screen->surface.height;
	float width = screen_width, height = screen_width * source_height / source_width;
	float x0, y0, x1, y1, u[2], v[2];
	static const unsigned corner_x[6] = { 0, 1, 0, 1, 1, 0 };
	static const unsigned corner_y[6] = { 0, 0, 1, 0, 1, 1 };
	unsigned i;

	if (height > screen_height)
	{
		height = screen_height;
		width = screen_height * source_width / source_height;
	}
	x0 = ((screen_width - width) / 2.0f) / screen_width * 2.0f - 1.0f;
	x1 = x0 + width / screen_width * 2.0f;
	y1 = 1.0f - ((screen_height - height) / 2.0f) / screen_height * 2.0f;
	y0 = y1 - height / screen_height * 2.0f;
	/* The texture coordinates of the rectangle's edges. An edge inside the target is half a pixel in from it: the filter then never reaches the pixels
	beyond, which the frame did not draw (at a lower step of dynamic resolution) or which are another window's (the GamePad's view). An edge of the
	target is its own edge, where the sampler clamps; the whole target is sampled 0 to 1, as before. */
	u[0] = rectangle[0] ? ((float)rectangle[0] + 0.5f) / pool_width : 0.0f;
	u[1] = rectangle[0] + rectangle[2] < source->color.surface.width ? ((float)(rectangle[0] + rectangle[2]) - 0.5f) / pool_width : 1.0f;
	v[0] = rectangle[1] ? ((float)rectangle[1] + 0.5f) / pool_height : 0.0f;
	v[1] = rectangle[1] + rectangle[3] < source->color.surface.height ? ((float)(rectangle[1] + rectangle[3]) - 0.5f) / pool_height : 1.0f;
	for (i = 0; i < 6; i++)
	{
		struct blit_vertex *vertex = &quad[i];

		vertex->x = corner_x[i] ? x1 : x0;
		vertex->y = corner_y[i] ? y0 : y1;
		vertex->u = u[corner_x[i]];
		vertex->v = v[corner_y[i]];
		vertex->r = vertex->g = vertex->b = vertex->a = 1.0f;
	}
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, quad, 6 * sizeof(struct blit_vertex));
	GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_COLOR_BUFFER | GX2_INVALIDATE_MODE_TEXTURE), source->color.surface.image,
		source->color.surface.imageSize);
	GX2SetContextState(context);
	/* (the bars of a letterbox are black; a picture that fills the screen needs no clear of it) */
	if (width < screen_width - 6.0f || height < screen_height - 6.0f)
	{
		GX2ClearColor(screen, 0.0f, 0.0f, 0.0f, 1.0f);
		GX2SetContextState(context);
	}
	GX2SetColorBuffer(screen, GX2_RENDER_TARGET_0);
	GX2SetViewport(0.0f, 0.0f, screen_width, screen_height, 0.0f, 1.0f);
	GX2SetScissor(0, 0, (uint32_t)screen_width, (uint32_t)screen_height);
	blit_state();
	GX2SetFetchShader(&gx.blit.fetchShader);
	GX2SetVertexShader(gx.blit.vertexShader);
	GX2SetPixelShader(gx.blit.pixelShader);
	GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
	GX2SetAttribBuffer(0, 6 * sizeof(struct blit_vertex), sizeof(struct blit_vertex), quad);
	gx.attrib[0].buffer = NULL;
	GX2SetPixelTexture(&source->texture, gx.sampler_location);
	GX2SetPixelSampler(&gx.sampler, gx.sampler_location);
	GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, 6, 0, 1);
}

/* ---------- visibility tests (lens flares): GX2's occlusion queries. wut's headers lack them, its library has them (the signatures are decaf's and Cemu's):
GX2QueryBegin(type, buffer), GX2QueryEnd(type, buffer), GX2QueryGetOcclusionResult(buffer, &samples) which says when the result is there. Off unless bit 3 of the third
number in halo/gx2_trace.txt is set: untested on a console. */

void GX2QueryBegin(uint32_t type, uint64_t *buffer);
void GX2QueryEnd(uint32_t type, uint64_t *buffer);
BOOL GX2QueryGetOcclusionResult(uint64_t *buffer, uint64_t *samples);

#define QUERY_BUFFERS 256
#define QUERY_BUFFER_BYTES 256
#define QUERY_OCCLUSION 0

static struct
{
	uint8_t *memory;
	unsigned next;
	int open; /* the buffer the begun test uses, -1: none */
	int slot_buffer[DK_VISIBILITY_SLOTS]; /* the buffer a slot's test is in, -1: none */
	int buffer_slot[QUERY_BUFFERS];
	uint8_t pending[DK_VISIBILITY_SLOTS];
	uint32_t result[DK_VISIBILITY_SLOTS];
	unsigned begun, ended, read_back;
} query;

static int queries_enabled(void)
{
	return (gx.features_off & 8) != 0;
}

/* d3d8_gx2.c: whether a visibility test's draws are seen by anything (with the queries off, their result is 0 whatever they draw) */
int host_gx2_visibility_enabled(void);

int host_gx2_visibility_enabled(void)
{
	return queries_enabled();
}

static void visibility_begin(void)
{
	unsigned buffer, slot;

	if (!queries_enabled() || !gx.color)
		return;
	if (!query.memory)
	{
		query.memory = gx_alloc(QUERY_BUFFERS * QUERY_BUFFER_BYTES, 256);
		if (!query.memory)
		{
			wiiu_log("GX2: no memory for the visibility tests");
			gx.features_off &= ~8;
			return;
		}
		for (slot = 0; slot < DK_VISIBILITY_SLOTS; slot++)
			query.slot_buffer[slot] = -1;
		for (buffer = 0; buffer < QUERY_BUFFERS; buffer++)
			query.buffer_slot[buffer] = -1;
	}
	buffer = query.next;
	query.next = (query.next + 1) % QUERY_BUFFERS;
	if (query.buffer_slot[buffer] >= 0)
	{
		query.pending[query.buffer_slot[buffer]] = 0;
		query.slot_buffer[query.buffer_slot[buffer]] = -1;
		query.buffer_slot[buffer] = -1;
	}
	memset(query.memory + buffer * QUERY_BUFFER_BYTES, 0, QUERY_BUFFER_BYTES);
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU, query.memory + buffer * QUERY_BUFFER_BYTES, QUERY_BUFFER_BYTES);
	GX2QueryBegin(QUERY_OCCLUSION, (uint64_t *)(query.memory + buffer * QUERY_BUFFER_BYTES));
	query.open = (int)buffer;
	query.begun++;
}

static void visibility_end(uint32_t index)
{
	if (!queries_enabled() || query.open < 0)
		return;
	GX2QueryEnd(QUERY_OCCLUSION, (uint64_t *)(query.memory + (unsigned)query.open * QUERY_BUFFER_BYTES));
	if (index > 0 && index < DK_VISIBILITY_SLOTS)
	{
		if (query.slot_buffer[index] >= 0)
			query.buffer_slot[query.slot_buffer[index]] = -1;
		query.slot_buffer[index] = query.open;
		query.buffer_slot[query.open] = (int)index;
		query.pending[index] = 1;
	}
	query.open = -1;
	query.ended++;
}

/* the back buffer's pixels the frame drew (width by height from its top left: all of it, but at a lower step of dynamic resolution), R G B A in
memory, for debug.screenshot_every (d3d8_gx2.c writes the file): copied by the GPU into a linear surface, which the CPU can read */
static void screenshot_take(struct target *source, uint32_t address, uint32_t width, uint32_t height)
{
	ring_flush();
	GX2Surface linear = source->color.surface;
	uint8_t *out = (uint8_t *)(uintptr_t)address;
	unsigned y;

	if (width > linear.width)
		width = linear.width;
	if (height > linear.height)
		height = linear.height;

	linear.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
	linear.use = GX2_SURFACE_USE_TEXTURE;
	linear.image = NULL;
	linear.mipmaps = NULL;
	linear.imageSize = 0;
	linear.pitch = 0;
	linear.alignment = 0;
	GX2CalcSurfaceSizeAndAlignment(&linear);
	linear.image = gx_alloc_raw(linear.imageSize, linear.alignment ? linear.alignment : 256);
	if (!linear.image)
		return;
	GX2DrawDone();
	GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_COLOR_BUFFER | GX2_INVALIDATE_MODE_TEXTURE), source->color.surface.image,
		source->color.surface.imageSize);
	GX2CopySurface(&source->color.surface, 0, 0, &linear, 0, 0);
	GX2DrawDone();
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU, linear.image, linear.imageSize);
	for (y = 0; y < height; y++)
		memcpy(out + (size_t)y * width * 4, (uint8_t *)linear.image + (size_t)y * linear.pitch * 4, (size_t)width * 4);
	MEMFreeToDefaultHeap(linear.image);
	context_resume();
}

/* The GPU test. The commands of one frame, kept as they were handed over, are run again and again without the game (30 frames' worth, or what the ring has room for), then waited for:
the time for each is the GPU's if it is more than the CPU's side of a frame (the commands need 2-4 ms of the GX2 thread), which is the question - could the GPU draw this frame in the time a 60 Hz display
gives it, whatever is done to the CPU? Three runs: the frame as it is, with no pixels drawn (a scissor of one pixel: the cost of the draws without their fill), and with blending off. Asked for with a 1 in
halo/gpu_test.txt (read at the start; run once), at the first frame of 250 draws or more after the 300th, or of the number after the 1 ("1 150"). The picture of the frame is drawn 90 more times: no harm. */
static void gpu_test_run(void)
{
	static const struct { const char *name; int mode; } variants[3] = { { "the frame", 0 }, { "no pixels (scissor 1x1)", 4 }, { "no blending", 1 } };
	const struct ring_segment *segment = &gx.ring[gx.ring_current];
	const uint32_t per_frame = segment->offset > gx.test_ring_start ? segment->offset - gx.test_ring_start : 0;
	const uint32_t room = RING_SEGMENT_BYTES - segment->offset;
	unsigned runs = per_frame ? (unsigned)(room / (per_frame + 64u * 1024u)) : 30;
	const int saved_mode = gx.debug_mode;
	unsigned variant, k;

	if (runs > 30)
		runs = 30;
	if (!gx.test_log_used || runs < 4)
	{
		wiiu_log("GPU test: not run (%u bytes kept, %u KB of the ring a frame, room for %u)", (unsigned)gx.test_log_used, (unsigned)(per_frame / 1024), runs);
		return;
	}
	ring_flush();
	GX2Flush();
	GX2DrawDone();
	for (variant = 0; variant < 3; variant++)
	{
		OSTime start, submitted, done;
		const unsigned char *log;

		gx.debug_mode = variants[variant].mode;
		gx.state_dirty = 1;
		gx.replaying = 1;
		start = OSGetSystemTime();
		for (k = 0; k < runs; k++)
		{
			for (log = gx.test_log; log < gx.test_log + gx.test_log_used;)
			{
				const uint32_t size = *(const uint32_t *)(const void *)log;

				run_stream(log + 4, log + 4 + size);
				log += 4 + ((size + 3u) & ~3u);
			}
			ring_flush();
		}
		GX2Flush();
		submitted = OSGetSystemTime();
		GX2DrawDone();
		done = OSGetSystemTime();
		gx.replaying = 0;
		wiiu_log("GPU test: %s, %u runs of a frame: handed over in %.2f ms each, done in %.2f ms each (%u KB of the ring a frame)", variants[variant].name, runs,
			(double)OSTicksToMicroseconds(submitted - start) / 1000.0 / (double)runs, (double)OSTicksToMicroseconds(done - start) / 1000.0 / (double)runs, (unsigned)(per_frame / 1024));
	}
	gx.debug_mode = saved_mode;
	gx.state_dirty = 1;
	gx.test_log_used = 0;
}

/* the vertices of a screen's blit: the ring's, so that each screen's quad, and each frame's, is its own until the GPU has drawn it (the GamePad's view
and a change of dynamic resolution's step give them other texture coordinates than the TV's and the last frame's); the quad made at the start if the
ring is full */
static struct blit_vertex *blit_quad(void)
{
	struct blit_vertex *quad = ring_reserve(6 * sizeof(struct blit_vertex));

	return quad ? quad : gx.quad;
}

static void present(const struct dk_command_present *command)
{
	struct target *back = target_get(&command->back_buffer);
	const OSTime now = OSGetSystemTime();
	const OSTime present_start = now;
	OSTime wait_ticks = 0;
	const OSTime cpu_ticks = gx.overlay_previous_end ? present_start - gx.overlay_previous_end : 0;
	/* the part of the back buffer the frame drew, which the screens show: all of it, but at a lower step of dynamic resolution */
	const uint32_t drawn[4] = { 0, 0, SURFACE_ACTIVE_WIDTH(&command->back_buffer), SURFACE_ACTIVE_HEIGHT(&command->back_buffer) };

	gpu_frame_end();
	gpu_poll();
	if (gx.overlay_on)
	{
		/* (the panel's numbers are taken four times a second so that they can be read; the graph is drawn again every other frame) */
		if (now - gx.overlay_view_time >= OSMillisecondsToTicks(250))
		{
			overlay_update_view(&gx.overlay);
			gx.overlay_view_time = now;
		}
		if (gx.overlay_shown && !(gx.overlay_frame++ & 1))
			overlay_refresh();
	}
	if (gx.test_armed == 2)
	{
		gx.test_armed = 3;
		gpu_test_run();
	}
	ring_flush();
	if (back && command->screenshot)
		screenshot_take(back, command->screenshot, drawn[2], drawn[3]);
	/* (before the frame is drawn, as the screenshot above is: the screens' buffers then hold the frame before) */
	wiiu_debug_present(back ? &back->color : NULL, WHBGfxGetTVColourBuffer(), WHBGfxGetDRCColourBuffer());
	/* WHBGfx's own Begin/FinishRender wait for the vsync before the frame and for the GPU to finish (GX2DrawDone) after it, so that the CPU could never build a frame while
	the GPU drew one: they are done here without the waits (the commands run in order; what the CPU writes for the GPU is protected by timestamps) and, as they did, the
	screens' buffers are copied to the scan buffers and swapped. At most one swap is queued. */
	if (back)
	{
		blit_to(back, drawn, blit_quad(), WHBGfxGetTVColourBuffer(), WHBGfxGetTVContextState());
		if (gx.overlay_shown)
			overlay_draw(WHBGfxGetTVColourBuffer(), WHBGfxGetTVContextState(), 3); /* (768 pixels of the TV's 1920) */
		if (skip_progress > 0.0f)
			skip_bar_draw(WHBGfxGetTVColourBuffer(), WHBGfxGetTVContextState(), skip_progress);
	}
	GX2CopyColorBufferToScanBuffer(WHBGfxGetTVColourBuffer(), GX2_SCAN_TARGET_TV);
	if (back)
	{
		blit_to(back, drawn, blit_quad(), WHBGfxGetDRCColourBuffer(), WHBGfxGetDRCContextState());
		if (gx.overlay_shown)
			overlay_draw(WHBGfxGetDRCColourBuffer(), WHBGfxGetDRCContextState(), 2); /* (512 of the GamePad's 854) */
		if (skip_progress > 0.0f)
			skip_bar_draw(WHBGfxGetDRCColourBuffer(), WHBGfxGetDRCContextState(), skip_progress);
	}
	GX2CopyColorBufferToScanBuffer(WHBGfxGetDRCColourBuffer(), GX2_SCAN_TARGET_DRC);
	/* (the frame's work is handed to the GPU before the swap, whose wait for the display is then not in the time the work is said to have taken) */
	GX2Flush();
	GX2SwapScanBuffers();
	GX2Flush();
	if (!gx.screens_enabled)
	{
		GX2SetTVEnable(TRUE);
		GX2SetDRCEnable(TRUE);
		gx.screens_enabled = 1;
	}
	{
		const OSTime wait_start = OSGetSystemTime();
		uint32_t swaps, flips;
		OSTime last_flip, last_vsync;

		for (;;)
		{
			GX2GetSwapStatus(&swaps, &flips, &last_flip, &last_vsync);
			if (swaps - flips <= 1)
				break;
			/* (the flip does not come in the background, nor when the system is closing the app: no waiting for it then) */
			if (!wiiu_in_foreground() || wiiu_exit_requested() || OSGetSystemTime() - wait_start > OSMillisecondsToTicks(250))
				break;
			gpu_poll();
			OSSleepTicks(OSMicrosecondsToTicks(200));
		}
		gx.wait_ticks += OSGetSystemTime() - wait_start;
		wait_ticks = OSGetSystemTime() - wait_start;
	}
	if (gx.overlay_on)
	{
		const OSTime end = OSGetSystemTime();
		unsigned long ring = gx.ring_total - gx.overlay_ring_previous, draws = gx.draw_total - gx.overlay_draw_previous;

		if (gx.overlay_previous_end)
		{
			const float frame_ms = (float)OSTicksToMicroseconds(end - gx.overlay_previous_end) / 1000.0f;

			/* (by how many of the display's refreshes the frame took: one is 60 a second) */
			gx.frame_buckets[frame_ms < 20.0f ? 0 : frame_ms < 36.7f ? 1 : frame_ms < 53.3f ? 2 : 3]++;
			gx.overlay.render_width = (unsigned)((float)gx.render_height * 16.0f / 9.0f + 0.5f);
			gx.overlay.render_height = gx.render_height;
			gx.overlay.interpolation = config_boolean("display.interpolation");
			overlay_push(&gx.overlay, (float)OSTicksToMicroseconds(end - gx.overlay_previous_end) / 1000.0f, (float)OSTicksToMicroseconds(cpu_ticks) / 1000.0f,
				(float)OSTicksToMicroseconds(end - present_start) / 1000.0f, (float)OSTicksToMicroseconds(wait_ticks) / 1000.0f, gx.gpu_ms, (unsigned)draws,
				(unsigned)(ring / 1024));
		}
		gx.overlay_previous_end = end;
		gx.overlay_ring_previous = gx.ring_total;
		gx.overlay_draw_previous = gx.draw_total;
	}
	gx.last_frame_draws = (unsigned)(gx.draw_total - gx.test_draw_mark);
	gx.test_draw_mark = gx.draw_total;
	gx.frames++;
	gx.window_frames++;
	if (gx.last_present)
	{
		const OSTime interval = OSGetSystemTime() - gx.last_present;

		if (interval > gx.longest_frame)
			gx.longest_frame = interval;
		if (OSTicksToMilliseconds(interval) > 40)
			gx.slow_frames++;
	}
	gx.last_present = OSGetSystemTime();
	/* (no file is read here: a read of the card took 40-60 ms of the game thread's frame, every 120 frames, when halo/gpu_test.txt and gx2_trace.txt were looked at from here;
	both are read once, at the start) */
	gx.present_ticks += OSGetSystemTime() - present_start;
	if (gx.frames == 1)
	{
		wiiu_log("GX2: first frame presented");
		gx.started = now;
	}
	if (gx.exit_after > 0.0 && (double)(now - gx.started) / (double)OSSecondsToTicks(1) >= gx.exit_after)
	{
		wiiu_log("GX2: the run's time is up");
		gx.exit_after = 0.0;
		wiiu_request_exit();
	}
	if (!gx.told || now - gx.told >= OSSecondsToTicks(5))
	{
		unsigned i;

		gx.told = now;
		wiiu_log("GX2: %u frames; commands since the last note, by type (target 1, clear 2, present 3, state 4, shaders 5 ...): ", gx.frames);
		for (i = 1; i < 18; i++)
			if (gx.counts[i])
				wiiu_log("GX2:   command %u: %u", i, gx.counts[i]);
		wiiu_log("GX2:   %u draws made; skipped: %u no target, %u no shader or format yet (%u of them a shader not in the pack), %u shader not loaded, %u ring "
			"full; %u clears made as rectangles; %u textures made (%u not 2D, %u short), ring overflows %u", gx.draws_made,
			gx.skipped_target, gx.skipped_state, gx.skipped_missing, gx.skipped_shader, gx.skipped_ring, gx.rectangle_clears, gx.textures_made,
			gx.textures_unsupported, gx.texture_short, gx.ring_overflows);
		if (gx.gpu_window_frames)
			wiiu_log("GX2:   the GPU's own time for a frame's draws (bottom-of-pipe samples, at most what it took): %.2f ms on average over %u frames, %.2f the longest",
				(double)OSTicksToMicroseconds(gx.gpu_window_ticks) / 1000.0 / (double)gx.gpu_window_frames, gx.gpu_window_frames,
				(double)OSTicksToMicroseconds(gx.gpu_window_longest) / 1000.0);
		gx.gpu_window_ticks = gx.gpu_window_longest = 0;
		gx.gpu_window_frames = 0;
		wiiu_log("GX2:   per frame over %u frames: %.1f ms in the stream (of which %.1f in draws, %.1f in textures), %.1f ms presenting", gx.window_frames,
			(double)OSTicksToMicroseconds(gx.submit_ticks - gx.present_ticks) / 1000.0 / (double)gx.window_frames,
			(double)OSTicksToMicroseconds(gx.draw_ticks) / 1000.0 / (double)gx.window_frames,
			(double)OSTicksToMicroseconds(gx.texture_ticks) / 1000.0 / (double)gx.window_frames,
			(double)OSTicksToMicroseconds(gx.present_ticks) / 1000.0 / (double)gx.window_frames);
		wiiu_log("GX2:   copied into the ring: %lu KB a frame in %lu copies (largest %lu KB); index data read %lu KB a frame", gx.ring_bytes / 1024 / (gx.window_frames ? gx.window_frames : 1),
			gx.ring_calls / (gx.window_frames ? gx.window_frames : 1), gx.ring_largest / 1024, gx.index_bytes / 1024 / (gx.window_frames ? gx.window_frames : 1));
		if (gx.window_frames)
			gt_log(gx.window_frames);
		if (gx.overlay_on)
		{
			wiiu_log("GX2:   frame times: median %.1f, 95th percentile %.1f, 99th %.1f ms (the last %u frames); frames of 1, 2, 3, more refreshes since the last note: %lu %lu %lu %lu",
				(double)overlay_percentile(&gx.overlay, 50), (double)overlay_percentile(&gx.overlay, 95), (double)overlay_percentile(&gx.overlay, 99), gx.overlay.count,
				gx.frame_buckets[0], gx.frame_buckets[1], gx.frame_buckets[2], gx.frame_buckets[3]);
			memset(gx.frame_buckets, 0, sizeof(gx.frame_buckets));
		}
		if (gx.overlay_on)
			wiiu_log("GX2:   overlay: %.1f frames a second, longest of 120 %.1f ms, cpu %.1f gpu %.1f wait %.1f ms, %ux%u, interpolation %d", (double)overlay_fps(&gx.overlay),
				(double)overlay_longest(&gx.overlay, 120), (double)gx.overlay.cpu_average, (double)gx.overlay.gpu_average, (double)gx.overlay.wait_average,
				gx.overlay.render_width, gx.overlay.render_height, gx.overlay.interpolation);
		wiiu_log("GX2:   a draw's time, ms a frame: copies of its data %.2f, state %.2f, shaders %.2f, uniforms %.2f, textures %.2f, attribute buffers %.2f",
			(double)OSTicksToMicroseconds(gx.section_ticks[0]) / 1000.0 / (double)gx.window_frames, (double)OSTicksToMicroseconds(gx.section_ticks[1]) / 1000.0 / (double)gx.window_frames,
			(double)OSTicksToMicroseconds(gx.section_ticks[2]) / 1000.0 / (double)gx.window_frames, (double)OSTicksToMicroseconds(gx.section_ticks[3]) / 1000.0 / (double)gx.window_frames,
			(double)OSTicksToMicroseconds(gx.section_ticks[4]) / 1000.0 / (double)gx.window_frames, (double)OSTicksToMicroseconds(gx.section_ticks[5]) / 1000.0 / (double)gx.window_frames);
		wiiu_log("GX2:   the draw call %.2f ms a frame; calls a frame: state %lu, fetch shader %lu, vertex shader %lu, pixel shader %lu, vertex register writes %lu, pixel register writes %lu, texture binds %lu, draws %lu",
			(double)OSTicksToMicroseconds(gx.section_ticks[6]) / 1000.0 / (double)gx.window_frames, gx.counts_state / gx.window_frames, gx.counts_fetch / gx.window_frames,
			gx.counts_vs / gx.window_frames, gx.counts_ps / gx.window_frames, gx.counts_vertex_regs / gx.window_frames, gx.counts_pixel_regs / gx.window_frames,
			gx.counts_texture_sets / gx.window_frames, gx.draws_made / gx.window_frames);
		gx.counts_state = gx.counts_fetch = gx.counts_vs = gx.counts_ps = gx.counts_vertex_regs = gx.counts_pixel_regs = gx.counts_texture_sets = 0;
		memset(gx.section_ticks, 0, sizeof(gx.section_ticks));
		big_copies_log();
		wiiu_log("GX2:   %lu copies of the window's data were out of date (written since), so the range was copied into the ring", gx.dynamic_streams);
		gx.dynamic_streams = 0;
		wiiu_log("GX2:   by kind a frame: streams %lu KB in %lu, immediate vertices %lu KB in %lu, constants %lu KB in %lu, indices %lu KB in %lu", gx.ring_kind_bytes[0] / 1024 / (gx.window_frames ? gx.window_frames : 1),
			gx.ring_kind_calls[0] / (gx.window_frames ? gx.window_frames : 1), gx.ring_kind_bytes[1] / 1024 / (gx.window_frames ? gx.window_frames : 1),
			gx.ring_kind_calls[1] / (gx.window_frames ? gx.window_frames : 1), gx.ring_kind_bytes[2] / 1024 / (gx.window_frames ? gx.window_frames : 1),
			gx.ring_kind_calls[2] / (gx.window_frames ? gx.window_frames : 1), gx.ring_kind_bytes[3] / 1024 / (gx.window_frames ? gx.window_frames : 1),
			gx.ring_kind_calls[3] / (gx.window_frames ? gx.window_frames : 1));
		memset(gx.ring_kind_bytes, 0, sizeof(gx.ring_kind_bytes));
		memset(gx.ring_kind_calls, 0, sizeof(gx.ring_kind_calls));
		gx.ring_bytes = gx.ring_calls = gx.ring_largest = gx.index_bytes = 0;
		wiiu_log("GX2:   waiting for the display's flip: %.1f ms a frame", (double)OSTicksToMicroseconds(gx.wait_ticks) / 1000.0 / (double)gx.window_frames);
		gx.wait_ticks = 0;
		wiiu_log("GX2:   frame times: longest %llu ms, %u over 40 ms; shaders loaded %u (%llu ms loading)", (unsigned long long)OSTicksToMilliseconds(gx.longest_frame),
			gx.slow_frames, gx.shaders_loaded, (unsigned long long)OSTicksToMilliseconds(gx.shader_ticks));
		gx.longest_frame = 0;
		gx.slow_frames = 0;
		gx.submit_ticks = gx.present_ticks = gx.texture_ticks = gx.draw_ticks = 0;
		gx.window_frames = 0;
		if (cache.entries || cache.full)
			wiiu_log("GX2:   static data cache: %u copies in %u chunks, %u hits, %u misses, %u times full; blocks: %u copied, %u uses, %u ranges not in a block", cache.entries, cache.chunk_count,
				cache.hits, cache.misses, cache.full, blocks.copies, blocks.hits, blocks.misses_range);
		cache.hits = cache.misses = 0;
		if (query.begun)
			wiiu_log("GX2:   visibility tests: %u begun, %u ended, %u read back", query.begun, query.ended, query.read_back);
		memset(gx.counts, 0, sizeof(gx.counts));
		gx.draws_made = gx.skipped_target = gx.skipped_state = gx.skipped_missing = gx.skipped_shader = gx.skipped_ring = 0;
		gx.rectangle_clears = 0;
		gx.textures_made = gx.textures_unsupported = 0;
	}
	ring_next();
	deferred_flush(0);
	context_resume();
	if (gx.test_armed == 3)
		gx.test_armed = 4; /* (done: once a run) */
	if (!gx.test_armed && gx.gpu_test_wanted && gx.frames > 300 && gx.last_frame_draws >= (unsigned)gx.gpu_test_draws)
	{
		{
			if (!gx.test_log)
				gx.test_log = malloc(TEST_LOG_BYTES);
			if (gx.test_log)
			{
				gx.test_armed = 2;
				gx.test_log_used = 0;
				gx.test_ring_start = gx.ring[gx.ring_current].offset;
				wiiu_log("GPU test: the next frame is kept and run again at its present");
			}
		}
	}
}

/* ---------- the stream */

void host_gx2_submit(uint32_t commands, uint32_t size);
uint32_t host_gx2_retired(void);
void host_gx2_idle(void);
uint32_t host_gx2_visibility(uint32_t index);
unsigned int host_gx2_output_height(void);


/* A batch of decals (DK_DRAW_DECALS, d3d8_gx2.c's decal_batch_end) expanded to the draw the game thread made of them before: each vertex registers 0, 4
and 9 as four floats, position and 1, the texture coordinates times 1/32767 and 0 and 1, the color's bytes / 255 (from a table of the same
quotients), in a draw command of its own (kept until the next). NULL if there is no memory, or the batch is not what its command says. */
static const struct dk_command_draw *decals_expand(const struct dk_command_draw *command)
{
	static unsigned char *buffer;
	static unsigned long capacity;
	static float unit[256];
	const unsigned char *at = (const unsigned char *)&command->streams[1], *end = at + command->inline_bytes;
	const unsigned long header_bytes = sizeof(*command) + 2 * sizeof(uint32_t), bytes = header_bytes + (unsigned long)command->count * 12 * sizeof(float);
	struct dk_command_draw *expanded;
	float *out;
	unsigned long made = 0;

	if (command->stream_count != 1 || (unsigned long)command->inline_bytes > command->header.size - header_bytes)
		return NULL;
	if (unit[255] == 0.0f)
	{
		unsigned i;

		for (i = 0; i < 256; i++)
			unit[i] = (float)i / 255.0f;
	}
	if (bytes > capacity)
	{
		unsigned char *grown = realloc(buffer, bytes);

		if (!grown)
			return NULL;
		buffer = grown;
		capacity = bytes;
	}
	expanded = (struct dk_command_draw *)(void *)buffer;
	memcpy(expanded, command, header_bytes);
	expanded->header.size = (uint32_t)bytes;
	expanded->primitive = command->primitive & ~DK_DRAW_DECALS;
	expanded->inline_bytes = command->count * 12 * sizeof(float);
	expanded->streams[0][0] = 0;
	expanded->streams[0][1] = expanded->inline_bytes;
	out = (float *)(void *)(buffer + header_bytes);
	while (at + 8 <= end && made < command->count)
	{
		uint32_t count, index;
		float color[4];

		memcpy(&count, at, sizeof(count));
		color[0] = unit[at[4]];
		color[1] = unit[at[5]];
		color[2] = unit[at[6]];
		color[3] = unit[at[7]];
		at += 8;
		if (count > command->count - made || (unsigned long)(end - at) < (unsigned long)count * 16)
			return NULL;
		for (index = 0; index < count; index++, at += 16, out += 12)
		{
			float position[3];
			uint32_t texcoord;

			memcpy(position, at, sizeof(position));
			memcpy(&texcoord, at + 12, sizeof(texcoord));
			out[0] = position[0];
			out[1] = position[1];
			out[2] = position[2];
			out[3] = 1.0f;
			out[4] = (float)(short)(texcoord & 0xffff) * (1.0f / 32767.0f);
			out[5] = (float)(short)(texcoord >> 16) * (1.0f / 32767.0f);
			out[6] = 0.0f;
			out[7] = 1.0f;
			out[8] = color[0];
			out[9] = color[1];
			out[10] = color[2];
			out[11] = color[3];
		}
		made += count;
	}
	return made == command->count ? expanded : NULL;
}

/* what the game thread's states and the draw's facts come to (dk_derive.c, which d3d8_gx2.c's draw_prepare did for every draw on the game thread), applied
where it differs from the last: the commands it sent before (STATE, SHADER_KEYS, TEXTURES, the parameters) did the same */
static void derive_apply(void)
{
	struct dk_derive *derive = dk_derive_get();
	const unsigned changed = dk_derive_run(derive);

	if (changed & DK_DERIVED_STATE)
	{
		gx.state = *dk_derive_state(derive);
		gx.state_dirty = 1;
	}
	if (changed & DK_DERIVED_KEYS)
	{
		uint32_t dk_shader_for_draw(uint32_t stage, const void *key_data);

		gx.vs_handle = dk_shader_for_draw(0 /* DK_SHADER_STAGE_VERTEX */, dk_derive_vertex_key(derive));
		gx.ps_handle = dk_shader_for_draw(1 /* DK_SHADER_STAGE_PIXEL */, dk_derive_pixel_key(derive));
		if (!gx.vs_handle || !gx.ps_handle)
			gx.shader_key_misses++;
	}
	if (changed & DK_DERIVED_TEXTURES)
		textures_set(dk_derive_textures(derive));
	if (changed & DK_DERIVED_VERTEX_PARAMETERS)
	{
		gx.vertex_parameters = *dk_derive_vertex_parameters(derive);
		gx.vertex_parameters_dirty = 1;
	}
	if (changed & DK_DERIVED_PIXEL_PARAMETERS)
	{
		gx.pixel_parameters = *dk_derive_pixel_parameters(derive);
		gx.pixel_parameters_dirty = 1;
	}
}

/* runs the commands of a stream; replaying (the GPU test) leaves out those that make or free resources and the present */
static void run_stream(const unsigned char *at, const unsigned char *end)
{
	gx_gate();
	while (at + sizeof(struct dk_command_header) <= end)
	{
		const struct dk_command_header *header = (const struct dk_command_header *)at;

		if (header->size < sizeof(*header) || at + header->size > end)
		{
			wiiu_log("GX2: a command of %u bytes runs past the stream's end; the rest is dropped", (unsigned)header->size);
			break;
		}
		if (header->type < 32)
			gx.counts[header->type]++;
		if (gx.replaying && (header->type == DK_COMMAND_PRESENT || header->type == DK_COMMAND_TEXTURE || header->type == DK_COMMAND_TEXTURE_ROWS ||
			header->type == DK_COMMAND_TEXTURE_FREE || header->type == DK_COMMAND_COMPOSITE || header->type == DK_COMMAND_VISIBILITY_BEGIN ||
			header->type == DK_COMMAND_VISIBILITY_END))
		{
			at += header->size;
			continue;
		}
		switch (header->type)
		{
		case DK_COMMAND_TARGETS:
			targets_bind((const struct dk_command_targets *)header);
			break;
		case DK_COMMAND_CLEAR:
			clear((const struct dk_command_clear *)header);
			break;
		case DK_COMMAND_PRESENT:
			present((const struct dk_command_present *)header);
			break;
		case DK_COMMAND_STATE:
			gx.state = ((const struct dk_command_state *)header)->state;
			gx.state_dirty = 1;
			break;
		case DK_COMMAND_SHADERS:
			gx.vs_handle = ((const struct dk_command_shaders *)header)->vertex;
			gx.ps_handle = ((const struct dk_command_shaders *)header)->pixel;
			break;
		case DK_COMMAND_SHADER_KEYS:
		{
			/* (gx2_shaders.c's, which the game thread called for each draw before: here, on this thread, once per change of the keys) */
			uint32_t dk_shader_for_draw(uint32_t stage, const void *key_data);
			const struct dk_command_shader_keys *keys = (const struct dk_command_shader_keys *)header;
			const unsigned char *data = (const unsigned char *)(keys + 1);

			gx.vs_handle = dk_shader_for_draw(0 /* DK_SHADER_STAGE_VERTEX */, data);
			gx.ps_handle = dk_shader_for_draw(1 /* DK_SHADER_STAGE_PIXEL */, data + keys->vertex_size);
			if (!gx.vs_handle || !gx.ps_handle)
				gx.shader_key_misses++;
			break;
		}
		case DK_COMMAND_CONSTANTS:
		{
			const struct dk_command_constants *constants = (const struct dk_command_constants *)header;

			if (constants->first < 192 && constants->count && constants->first + constants->count <= 192)
			{
				memcpy(gx.constants[constants->first], constants->data, constants->count * 4 * sizeof(float));
				if (constants->first < gx.constants_lo)
					gx.constants_lo = constants->first;
				if (constants->first + constants->count - 1 > gx.constants_hi || gx.constants_lo > gx.constants_hi)
					gx.constants_hi = constants->first + constants->count - 1;
			}
			break;
		}
		case DK_COMMAND_VERTEX_PARAMETERS:
			gx.vertex_parameters = ((const struct dk_command_vertex_parameters *)header)->parameters;
			gx.vertex_parameters_dirty = 1;
			break;
		case DK_COMMAND_PIXEL_PARAMETERS:
			gx.pixel_parameters = ((const struct dk_command_pixel_parameters *)header)->parameters;
			gx.pixel_parameters_dirty = 1;
			break;
		case DK_COMMAND_VERTEX_FORMAT:
			gx.format = ((const struct dk_command_vertex_format *)header)->format;
			gx.format_set = 1;
			gx.format_serial++;
			break;
		case DK_COMMAND_TEXTURE:
		{
			const OSTime texture_start = OSGetSystemTime();

			texture_make((const struct dk_command_texture *)header);
			gx.texture_ticks += OSGetSystemTime() - texture_start;
			break;
		}
		case DK_COMMAND_TEXTURE_ROWS:
			texture_rows((const struct dk_command_texture_rows *)header);
			break;
		case DK_COMMAND_TEXTURE_FREE:
			texture_free((const struct dk_command_texture_free *)header);
			break;
		case DK_COMMAND_TEXTURES:
			textures_set((const struct dk_command_textures *)header);
			break;
		case DK_COMMAND_COMPOSITE:
			composite_receive((const struct dk_command_composite *)header);
			break;
		case DK_COMMAND_STATE_WRITES:
			dk_derive_writes(dk_derive_get(), (const struct dk_command_state_writes *)header);
			break;
		case DK_COMMAND_DRAW_FACTS:
			if (header->size >= sizeof(struct dk_command_draw_facts))
				dk_derive_facts(dk_derive_get(), &((const struct dk_command_draw_facts *)header)->facts);
			break;
		case DK_COMMAND_DRAW:
		{
			const OSTime draw_start = OSGetSystemTime();

			if (dk_derive_pending(dk_derive_get()))
				derive_apply();
			if (((const struct dk_command_draw *)header)->primitive & DK_DRAW_DECALS)
			{
				const struct dk_command_draw *expanded = decals_expand((const struct dk_command_draw *)header);

				if (expanded)
					draw(expanded);
			}
			else
				draw((const struct dk_command_draw *)header);
			gx.draw_ticks += OSGetSystemTime() - draw_start;
			break;
		}
		case DK_COMMAND_VISIBILITY_BEGIN:
			visibility_begin();
			break;
		case DK_COMMAND_VISIBILITY_END:
			visibility_end(((const struct dk_command_visibility_end *)header)->index);
			break;
		default:
			break;
		}
		at += header->size;
	}
}

static void submit_job(uint32_t commands, uint32_t size)
{
	const unsigned char *at = (const unsigned char *)(uintptr_t)commands;
	OSTime submit_start;

	gx_gate();
	if (!gx.ready)
		return;
	submit_start = OSGetSystemTime();
	if (gx.test_armed == 2 && gx.test_log_used + 4 + ((size + 3u) & ~3u) <= TEST_LOG_BYTES)
	{
		/* (the GPU test: this frame's streams are kept, each with its size, to be run again at the frame's present) */
		*(uint32_t *)(void *)(gx.test_log + gx.test_log_used) = size;
		memcpy(gx.test_log + gx.test_log_used + 4, at, size);
		gx.test_log_used += 4 + ((size + 3u) & ~3u);
	}
	run_stream(at, at + size);
	ring_flush();
	gx.submit_ticks += OSGetSystemTime() - submit_start;
}

/* the highest submission the GPU has finished: everything handed over is waited for at its present, so all of it */
/* (host_gx2_retired is below, with the GX2 thread's state) */

static uint32_t visibility_job(uint32_t index)
{
	uint64_t samples;

	if (!query.memory || index == 0 || index >= DK_VISIBILITY_SLOTS)
		return 0;
	if (query.pending[index] && query.slot_buffer[index] >= 0 &&
		GX2QueryGetOcclusionResult((uint64_t *)(query.memory + (unsigned)query.slot_buffer[index] * QUERY_BUFFER_BYTES), &samples))
	{
		query.result[index] = samples > 0xffffffffu ? 0xffffffffu : (uint32_t)samples;
		query.pending[index] = 0;
		query.read_back++;
	}
	return query.result[index];
}

/* ---------- the GX2 thread (phase 1 of reports/research/2026-10-23/two-core-render-plan.md)

Every GX2 call is made by one thread of its own, on core 2: it makes the GX2 context (initialize) and runs what the game thread hands it, one job at a time, the game thread waiting for each to
be done (so nothing is faster yet: the game thread and the stream's memory behave as they did, and everything that can go wrong with a second thread - starting GX2 on it, HOME, closing,
quitting, the foreground gates - can be tested before anything runs at the same time). halo/gx2_thread.txt (0) keeps the old way, and is the default: the game thread does it all (1: core 2, 2: core 1, 3: core 0, 4 and 5 as 1 and 3 with GX2 made on the game's thread: see the notes in
reports/research/2026-10-23/two-core-render-plan.md, section 8, for what each did on the console). */
#define GX2_THREAD_STACK_BYTES (512 * 1024)

enum { JOB_SUBMIT = 1, JOB_IDLE, JOB_MAP_CHANGED, JOB_VISIBILITY };

static struct
{
	OSSemaphore ready, done;
	OSThread thread;
	uint8_t *stack;
	volatile int state; /* 0 not started, 1 starting, 2 running, 3 failed, 4 off (halo/gx2_thread.txt) */
	volatile uint32_t job, a, b, result;
	volatile int release_request, release_done, busy;
	int async; /* streams are posted to the queue (d3d8_gx2.c) and run while the game goes on */
	int affinity, spin, init_here, core_callbacks; /* core_callbacks: the release is handled by a ProcUI callback registered for the GX2 thread's core, run by a thread of that core (ProcUISubProcessMessages) */
	OSTime parked_at; /* when the GX2 thread last waited at the foreground gate */
	OSThread procui_thread;
	uint8_t *procui_stack;
	int affinity_core; /* init_here: GX2 is made by the game's thread (core 1, ProcUI's), the GX2 thread only drives it */ /* spin: jobs and answers are words the threads watch (cores other than the game's), not semaphores: a thread asleep on another core did not wake while the HOME callback ran */
	volatile uint32_t posted, finished;
	OSTime waited;
	unsigned long jobs;
} gt;

/* the foreground gate's caller: the GX2 thread is not the game thread (the shell's release callback waits for the game thread to be held, and must not take the GX2
thread's hold for it) */
static int gate_is_game_thread(void)
{
	return !(gt.state == 2 && OSGetCurrentThread() == &gt.thread);
}

static void gt_log(unsigned frames)
{
	if (gt.state != 2)
		return;
	wiiu_log("GX2:   the GX2 thread: the game thread waited %.2f ms a frame in %lu jobs, the thread worked %.2f ms of it in submits", (double)OSTicksToMicroseconds(gt.waited) / 1000.0 / (double)frames,
		gt.jobs, (double)OSTicksToMicroseconds(gx.submit_ticks) / 1000.0 / (double)frames);
	gt.waited = 0;
	gt.jobs = 0;
}

/* HOME or closing: the work handed to the GPU is waited for by the thread that made it (GX2 keeps a command buffer for each core, and the system's wait for the GPU, ProcUIDrawDoneRelease, is
for the work of the core that did the calls), and then the release callback goes on */
extern void (*wiiu_gate_poll)(void);

static void gt_release_here(void)
{
	if (!gt.release_request)
		return;
	wiiu_log("HOME: the GX2 thread (core %u) is draining the GPU", (unsigned)OSGetCoreId());
	ring_flush();
	GX2Flush();
	wiiu_log("HOME: the GX2 thread flushed");
	GX2DrawDone();
	wiiu_log("HOME: the GX2 thread's GX2DrawDone returned");
	gt.release_request = 0;
	gt.release_done = 1;
}

/* the foreground gate's poll, run by whichever thread is held there: only the GX2 thread acts */
static void gt_gate_poll(void)
{
	if (OSGetCurrentThread() == &gt.thread)
		gt.parked_at = OSGetSystemTime(); /* (the GX2 thread is held at the gate between two commands: it is in no GX2 call) */
	if (gt.release_request && OSGetCurrentThread() == &gt.thread)
		gt_release_here();
}

static void gt_barrier(void)
{
	__asm__ volatile("sync" : : : "memory");
}

static uint32_t run_job(uint32_t job, uint32_t a, uint32_t b)
{
	switch (job)
	{
	case JOB_SUBMIT:
		submit_job(a, b);
		break;
	case JOB_IDLE:
		gx_gate();
		GX2DrawDone();
		break;
	case JOB_MAP_CHANGED:
		cache_flush();
		break;
	case JOB_VISIBILITY:
		return visibility_job(a);
	default:
		break;
	}
	return 0;
}

static int gx2_thread_main(int argc, const char **argv)
{
	int ok;

	(void)argc;
	(void)argv;
	wiiu_fpu_setup();
	wiiu_tag_thread("gx2");
	wiiu_log("The GX2 thread starts on core %u", (unsigned)OSGetCoreId());
	wiiu_gate_poll = gt_gate_poll;
	ok = gt.init_here ? gx.ready : initialize();
	if (ok && gt.init_here)
		context_resume(); /* (the context is set for this core's command buffer) */
	gt_barrier();
	gt.state = ok ? 2 : 3;
	OSSignalSemaphore(&gt.done);
	if (!ok)
		return 0;
	if (gt.spin)
	{
		uint32_t seen = gt.posted;

		for (;;)
		{
			unsigned slot;
			uint32_t used, number;

			/* the queue first (with gx2_thread.txt 7: the streams the game thread posts while it goes on) */
			if (gt.async && stream_queue_take(&dk_stream_queue, &slot, &used, &number))
			{
				gt.busy = 1;
				submit_job((uint32_t)(uintptr_t)dk_stream_buffer(slot), used);
				gt.busy = 0;
				stream_queue_retire(&dk_stream_queue);
				continue;
			}
			while (gt.posted == seen && !gt.release_request && !(gt.async && __atomic_load_n(&dk_stream_queue.posted, __ATOMIC_ACQUIRE) != dk_stream_queue.taken))
			{
				/* (in the background the app gets a little of core 2 and the system's own threads need the rest: a thread that spins there froze the console when the app was closed from the HOME menu) */
				if (wiiu_in_foreground() && !wiiu_exit_requested())
					OSYieldThread();
				else
					OSSleepTicks(OSMillisecondsToTicks(2));
			}
			if (gt.release_request)
			{
				gt_release_here();
				continue;
			}
			if (gt.posted == seen)
				continue; /* (a stream was posted: taken at the top) */
			seen = gt.posted;
			gt_barrier();
			gt.busy = 1;
			gt.result = run_job(gt.job, gt.a, gt.b);
			/* (every job ends with its commands handed to the GPU: the threads of the other cores are not run while the HOME callback is, so what this core has not flushed by then cannot be) */
			GX2Flush();
			gt.busy = 0;
			gt_barrier();
			gt.finished = seen;
		}
	}
	for (;;)
	{
		OSWaitSemaphore(&gt.ready);
		gt_barrier();
		if (gt.release_request)
		{
			/* (a wake-up from the release callback, not a job: its signal is not answered by `done`) */
			gt_release_here();
			continue;
		}
		gt.busy = 1;
		gt.result = run_job(gt.job, gt.a, gt.b);
		gt.busy = 0;
		gt_barrier();
		OSSignalSemaphore(&gt.done);
	}
	return 0;
}

int wiiu_game_gate_is_held(void);

/* The release callback registered for the GX2 thread's core (gx2_thread.txt 6): ProcUI runs it on a thread of core 2 that calls ProcUISubProcessMessages, at HOME, when the
system wants the GPU. It waits for the game thread to be held, drains the GPU from the core GX2 was made on, and says the GPU is released. */
static uint32_t core_release(void *unused)
{
	uint32_t waited = 0;

	(void)unused;
	wiiu_log("HOME: the release callback of core %u (the GX2 thread's) runs", (unsigned)OSGetCoreId());
	/* the game thread held, and the GX2 thread either idle or held at the gate (not in a GX2 call: GX2 is not safe to call from two threads, and the soak's last quit left the console
	hung with this callback calling it while the thread was in the middle of a stream) */
	while ((!wiiu_game_gate_is_held() || (gt.busy && OSGetSystemTime() - gt.parked_at > OSMillisecondsToTicks(30))) && waited < 3000u)
	{
		OSSleepTicks(OSMillisecondsToTicks(2));
		waited += 2u;
	}
	wiiu_log("HOME: the GX2 thread is %s", gt.busy ? (OSGetSystemTime() - gt.parked_at <= OSMillisecondsToTicks(30) ? "held at the gate" : "STILL IN A JOB") : "idle");
	wiiu_log("HOME: on core %u the game thread is %s after %u ms", (unsigned)OSGetCoreId(), wiiu_game_gate_is_held() ? "held" : "NOT held", (unsigned)waited);
	ring_flush();
	GX2Flush();
	GX2DrawDone();
	wiiu_log("HOME: the GX2 thread's core drained the GPU");
	ProcUIDrawDoneRelease();
	return 0;
}

/* a thread of the GX2 thread's core that does what the main thread does on core 1: takes ProcUI's messages (the callbacks registered for its core run on it) */
static int procui_core_main(int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	wiiu_fpu_setup();
	wiiu_tag_thread("procui core");
	wiiu_log("The ProcUI thread of core %u starts", (unsigned)OSGetCoreId());
	OSSetThreadPriority(OSGetCurrentThread(), 31); /* (the lowest: it must never take the core from the GX2 thread) */
	for (;;)
	{
		const OSTime start = OSGetSystemTime();
		const ProcUIStatus status = ProcUISubProcessMessages(TRUE);

		if (status == PROCUI_STATUS_EXITING)
			break;
		if (OSGetSystemTime() - start < OSMillisecondsToTicks(1))
			OSSleepTicks(OSMillisecondsToTicks(2)); /* (it returned at once: it is not blocking here, and must not spin) */
	}
	return 0;
}

/* starts the thread (or, with halo/gx2_thread.txt 0, only makes the context here) and waits for GX2 to be made: whether it was */
static int gt_start(void)
{
	if (gt.state == 2 || gt.state == 4)
		return gx.ready || initialize();
	if (gt.state == 3)
		return 0;
	if (gt.state == 0)
	{
		FILE *file = fopen(WIIU_DATA_DIRECTORY "/gx2_thread.txt", "r");
		int on = 7; /* (the GX2 thread on core 2 with the stream queue unless the file says otherwise: 0 is the game thread alone; HOME, return and Close were run with it) */

		if (file)
		{
			if (fscanf(file, "%d", &on) != 1)
				on = 0;
			fclose(file);
		}
		/* 1: core 2; 2: core 1, the game's; 3: core 0 */
		gt.affinity = on == 2 ? OS_THREAD_ATTRIB_AFFINITY_CPU1 : on == 3 ? OS_THREAD_ATTRIB_AFFINITY_CPU0 : OS_THREAD_ATTRIB_AFFINITY_CPU2;
		gt.spin = on != 2;
		/* a value of 4 and over: as 1 and 3 but GX2 is made on the game's thread (halo/gx2_thread.txt 4: core 2, 5: core 0) */
		gt.init_here = on == 4 || on == 5;
		gt.core_callbacks = on == 6 || on == 7;
		gt.async = on == 7;
		if (on == 6 || on == 7)
			gt.affinity = OS_THREAD_ATTRIB_AFFINITY_CPU2;
		else if (on == 4)
			gt.affinity = OS_THREAD_ATTRIB_AFFINITY_CPU2;
		else if (on == 5)
			gt.affinity = OS_THREAD_ATTRIB_AFFINITY_CPU0;
		if (!on)
		{
			gt.state = 4;
			wiiu_log("GX2: no GX2 thread (halo/gx2_thread.txt is 0): the game thread makes the calls");
			return initialize();
		}
		if (gt.init_here && !initialize())
		{
			gt.state = 3;
			return 0;
		}
		gt.stack = memalign(32, GX2_THREAD_STACK_BYTES);
		OSInitSemaphore(&gt.ready, 0);
		OSInitSemaphore(&gt.done, 0);
		if (!gt.stack || !OSCreateThread(&gt.thread, gx2_thread_main, 0, NULL, gt.stack + GX2_THREAD_STACK_BYTES, GX2_THREAD_STACK_BYTES,
			OSGetThreadPriority(OSGetCurrentThread()), (OSThreadAttributes)gt.affinity))
		{
			wiiu_log("GX2: no GX2 thread could be made (%s): the game thread makes the calls", gt.stack ? "OSCreateThread" : "no memory for the stack");
			gt.state = 4;
			return initialize();
		}
		if (gt.core_callbacks)
		{
			gt.procui_stack = memalign(32, 64 * 1024);
			ProcUIRegisterCallbackCore(PROCUI_CALLBACK_RELEASE, core_release, NULL, 100, 2);
			if (gt.procui_stack && OSCreateThread(&gt.procui_thread, procui_core_main, 0, NULL, gt.procui_stack + 64 * 1024, 64 * 1024, OSGetThreadPriority(OSGetCurrentThread()),
				OS_THREAD_ATTRIB_AFFINITY_CPU2))
			{
				OSSetThreadName(&gt.procui_thread, "procui core");
				OSResumeThread(&gt.procui_thread);
			}
			else
				wiiu_log("GX2: no ProcUI thread for core 2");
		}
		gt.state = 1;
		profile_by_game_thread = 1;
		OSSetThreadName(&gt.thread, "gx2");
		OSResumeThread(&gt.thread);
		OSWaitSemaphore(&gt.done);
		wiiu_log("GX2: the GX2 thread says it is %s", gt.state == 2 ? "ready" : "NOT ready (GX2 could not be made on it)");
		if (gt.state == 2)
			wiiu_profile_start(); /* (the sampler follows the thread that calls this: the game's) */
	}
	return gt.state == 2;
}

/* hands a job over and waits for it (or, with no thread, does it) */
static uint32_t gt_job(uint32_t job, uint32_t a, uint32_t b)
{
	OSTime start;
	uint32_t result;

	if (!gt_start())
		return 0;
	if (gt.state == 4)
		return run_job(job, a, b);
	if (gt.async)
	{
		/* (the jobs of this mailbox are about what the streams before them did: the queue is emptied first) */
		while (stream_queue_retired(&dk_stream_queue) < __atomic_load_n(&dk_stream_queue.posted, __ATOMIC_ACQUIRE))
			OSYieldThread();
	}
	start = OSGetSystemTime();
	gt.job = job;
	gt.a = a;
	gt.b = b;
	gt_barrier();
	if (gt.spin)
	{
		const uint32_t id = gt.posted + 1;

		gt.posted = id;
		while (gt.finished != id)
			OSYieldThread();
	}
	else
	{
		OSSignalSemaphore(&gt.ready);
		OSWaitSemaphore(&gt.done);
	}
	gt_barrier();
	result = gt.result;
	gt.waited += OSGetSystemTime() - start;
	gt.jobs++;
	return result;
}

void host_gx2_submit(uint32_t commands, uint32_t size)
{
	wiiu_foreground_gate(1);
	gt_job(JOB_SUBMIT, commands, size);
}

/* for the release callback (wiiu_shell.c): the GPU drained by the GX2 thread (a wait of up to 3 s for it, then by the caller, as before) */
int wiiu_gx2_release_drain(void);

/* returns 1 if the GPU is given back by another callback (the GX2 thread's, on its own core: gt.core_callbacks), 0 if the caller does it (ProcUIDrawDoneRelease) */
int wiiu_gx2_release_drain(void)
{
	OSTime start;

	wiiu_gx2_mem1_lost();
	if (gt.core_callbacks)
		return 1;
	if (gt.state != 2 || gt.spin)
	{
		/* (a thread of another core is not run during this callback; its work was handed to the GPU at the end of its job) */
		GX2DrawDone();
		return 0;
	}
	wiiu_log("HOME: asking the GX2 thread to drain the GPU (it is %s; main is on core %u)", gt.busy ? "inside a job" : "idle", (unsigned)OSGetCoreId());
	gt.release_done = 0;
	gt_barrier();
	gt.release_request = 1;
	if (!gt.busy && !gt.spin)
		OSSignalSemaphore(&gt.ready); /* (wakes the thread if it is idle; one held at the gate in a job sees the request there) */
	start = OSGetSystemTime();
	while (!gt.release_done && OSGetSystemTime() - start < OSMillisecondsToTicks(3000))
		OSSleepTicks(OSMillisecondsToTicks(2));
	wiiu_log("HOME: the GX2 thread drained the GPU %s after %u ms", gt.release_done ? "" : "(NOT: it did not answer) ",
		(unsigned)OSTicksToMilliseconds(OSGetSystemTime() - start));
	if (!gt.release_done)
		GX2DrawDone();
	return 0;
}

void host_gx2_idle(void)
{
	gt_job(JOB_IDLE, 0, 0);
}

/* the highest submission the GPU has finished: with a queue, the last stream the GX2 thread has run; without, everything handed over is waited for at its present, so all of it */
uint32_t host_gx2_retired(void)
{
	return gt.async && gt.state == 2 ? stream_queue_retired(&dk_stream_queue) : 0xffffffffu;
}

/* for d3d8_gx2.c: whether the streams are posted to a queue and run while the game goes on (gx2_thread.txt 7); asked once, at the first stream, so the thread is started by then */
/* the GX2 thread's priority (the game thread's, which makes it), for a thread that shares its core (wiiu_shell.c, wiiu_thread_beside_gx2): it waits for work
yielding (OSYieldThread), which lets only threads of its priority or above run */
int wiiu_gx2_thread_priority(void);

int wiiu_gx2_thread_priority(void)
{
	return gt.state == 2 ? OSGetThreadPriority(&gt.thread) : OSGetThreadPriority(OSGetDefaultThread(1));
}

int host_gx2_async(void);

int host_gx2_async(void)
{
	return gt_start() && gt.state == 2 && gt.async;
}

/* a wait of the game thread for the GX2 thread: gives the core up for a moment */
void host_gx2_pause(void);

void host_gx2_pause(void)
{
	OSYieldThread();
}

void host_gx2_map_changed(void)
{
	gt_job(JOB_MAP_CHANGED, 0, 0);
}

uint32_t host_gx2_visibility(uint32_t index)
{
	return gt_job(JOB_VISIBILITY, index, 0);
}

/* the lines the screen's targets are drawn at, at most: the TV's */
unsigned int host_gx2_output_height(void)
{
	return gt_start() ? gx.render_height : 480u;
}

/* for d3d8_gx2.c's halo_screen_commit, between the game thread's frames: the lines its next frames draw of the screen's targets (dynamic resolution:
halo/drs_lines.txt's, or the controller's, set by the GX2 thread at its presents), 0 before GX2 is made */
unsigned int host_gx2_drs_lines(void);

unsigned int host_gx2_drs_lines(void)
{
	return gx.ready ? gx.drs_lines : 0u;
}
