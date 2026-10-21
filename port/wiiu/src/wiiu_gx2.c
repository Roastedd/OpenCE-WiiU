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
#include "wiiu_shell.h"

#include <coreinit/cache.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/time.h>
#include <gx2/clear.h>
#include <gx2/context.h>
#include <gx2/draw.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/sampler.h>
#include <gx2/shaders.h>
#include <gx2/state.h>
#include <gx2/surface.h>
#include <gx2/texture.h>
#include <gx2/utils.h>
#include <whb/gfx.h>

#include <stdint.h>
#include <string.h>

extern const unsigned char wiiu_gx2_blit_gsh[];
extern const unsigned long wiiu_gx2_blit_gsh_size;

#define TARGET_LIMIT 64
#define SURFACE_PIXEL_WIDTH(surface) ((surface)->pixel_width ? (surface)->pixel_width : (surface)->width)
#define SURFACE_PIXEL_HEIGHT(surface) ((surface)->pixel_height ? (surface)->pixel_height : (surface)->height)

struct target
{
	struct dk_surface surface;
	GX2ColorBuffer color;
	GX2DepthBuffer depth;
	GX2Texture texture; /* a color target as the blit samples it */
};

struct blit_vertex
{
	float x, y, u, v, r, g, b, a;
};

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
	unsigned partial_clears;
} gx;

static void *gx_alloc(uint32_t size, uint32_t alignment)
{
	void *memory = MEMAllocFromDefaultHeapEx(size, (int32_t)alignment);

	if (memory)
		memset(memory, 0, size);
	return memory;
}

/* ---------- starting */

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

int platform_screen_mode(long *width, long *height)
{
	if (!initialize())
		return 0;
	*width = (long)WHBGfxGetTVColourBuffer()->surface.width;
	*height = (long)WHBGfxGetTVColourBuffer()->surface.height;
	return 1;
}

/* "creates the window and makes its OpenGL context current": the GX2 context was made by initialize */
int platform_video_initialize(unsigned long width, unsigned long height);

int platform_video_initialize(unsigned long width, unsigned long height)
{
	(void)width;
	(void)height;
	return initialize();
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

static struct target *target_get(const struct dk_surface *surface)
{
	struct target *target;
	GX2Surface *s;
	unsigned i;
	const int depth = surface->kind == DK_SURFACE_DEPTH;

	if (surface->kind == DK_SURFACE_NONE || !surface->width || !surface->height)
		return NULL;
	for (i = 0; i < gx.target_count; i++)
		if (!memcmp(&gx.targets[i].surface, surface, sizeof(*surface)))
			return &gx.targets[i];
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
	s->image = gx_alloc(s->imageSize, s->alignment ? s->alignment : 256);
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
	wiiu_log("GX2: %s target %08x, %ux%u (%ux%u pixels)", depth ? "depth" : "color", (unsigned)surface->data, (unsigned)surface->width,
		(unsigned)surface->height, (unsigned)s->width, (unsigned)s->height);
	return target;
}

static void context_begin(void)
{
	GX2SetContextState(WHBGfxGetTVContextState());
}

static void targets_bind(const struct dk_command_targets *command)
{
	gx.color = target_get(&command->color);
	gx.depth = target_get(&command->depth);
	context_begin();
	if (gx.color)
		GX2SetColorBuffer(&gx.color->color, GX2_RENDER_TARGET_0);
	if (gx.depth)
		GX2SetDepthBuffer(&gx.depth->depth);
	if (gx.color || gx.depth)
	{
		const uint32_t width = gx.color ? gx.color->color.surface.width : gx.depth->depth.surface.width;
		const uint32_t height = gx.color ? gx.color->color.surface.height : gx.depth->depth.surface.height;

		GX2SetViewport(0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f);
		GX2SetScissor(0, 0, width, height);
	}
}

static void clear(const struct dk_command_clear *command)
{
	const uint32_t color_flags = DK_CLEAR_RED | DK_CLEAR_GREEN | DK_CLEAR_BLUE | DK_CLEAR_ALPHA;
	const int whole_color = (command->flags & color_flags) == color_flags;

	/* GX2 clears a whole buffer: a clear of part of a target (a split-screen window), or of some channels (the fog screen's alpha only), is
	made as a whole one for now, and counted */
	if (command->rectangle_count)
	{
		const struct target *size_from = gx.color ? gx.color : gx.depth;

		if (size_from)
		{
			const uint32_t *rectangle = command->rectangles[0];

			if (command->rectangle_count != 1 || rectangle[0] || rectangle[1] || rectangle[2] != SURFACE_PIXEL_WIDTH(&size_from->surface) ||
				rectangle[3] != SURFACE_PIXEL_HEIGHT(&size_from->surface))
				gx.partial_clears++;
		}
	}
	if (gx.color && (command->flags & color_flags))
	{
		if (!whole_color)
			gx.partial_clears++;
		else
			GX2ClearColor(&gx.color->color, command->color[0], command->color[1], command->color[2], command->color[3]);
	}
	if (gx.depth && (command->flags & (DK_CLEAR_DEPTH | DK_CLEAR_STENCIL)))
	{
		GX2ClearFlags flags = (GX2ClearFlags)0;

		if (command->flags & DK_CLEAR_DEPTH)
			flags = (GX2ClearFlags)(flags | GX2_CLEAR_FLAGS_DEPTH);
		if (command->flags & DK_CLEAR_STENCIL)
			flags = (GX2ClearFlags)(flags | GX2_CLEAR_FLAGS_STENCIL);
		GX2ClearDepthStencilEx(&gx.depth->depth, command->depth, (uint8_t)command->stencil, flags);
	}
	/* (a clear is a draw of its own: the state the draws need goes back) */
	context_begin();
	if (gx.color)
		GX2SetColorBuffer(&gx.color->color, GX2_RENDER_TARGET_0);
	if (gx.depth)
		GX2SetDepthBuffer(&gx.depth->depth);
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
static void blit_to(struct target *source, GX2ColorBuffer *screen, GX2ContextState *context)
{
	const float source_width = (float)source->color.surface.width, source_height = (float)source->color.surface.height;
	const float screen_width = (float)screen->surface.width, screen_height = (float)screen->surface.height;
	float width = screen_width, height = screen_width * source_height / source_width;
	float x0, y0, x1, y1;
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
	for (i = 0; i < 6; i++)
	{
		struct blit_vertex *v = &gx.quad[i];

		v->x = corner_x[i] ? x1 : x0;
		v->y = corner_y[i] ? y0 : y1;
		v->u = (float)corner_x[i];
		v->v = (float)corner_y[i];
		v->r = v->g = v->b = v->a = 1.0f;
	}
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, gx.quad, 6 * sizeof(struct blit_vertex));
	GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_COLOR_BUFFER | GX2_INVALIDATE_MODE_TEXTURE), source->color.surface.image,
		source->color.surface.imageSize);
	GX2SetContextState(context);
	GX2ClearColor(screen, 0.0f, 0.0f, 0.0f, 1.0f);
	GX2SetContextState(context);
	GX2SetColorBuffer(screen, GX2_RENDER_TARGET_0);
	GX2SetViewport(0.0f, 0.0f, screen_width, screen_height, 0.0f, 1.0f);
	GX2SetScissor(0, 0, (uint32_t)screen_width, (uint32_t)screen_height);
	blit_state();
	GX2SetFetchShader(&gx.blit.fetchShader);
	GX2SetVertexShader(gx.blit.vertexShader);
	GX2SetPixelShader(gx.blit.pixelShader);
	GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
	GX2SetAttribBuffer(0, 6 * sizeof(struct blit_vertex), sizeof(struct blit_vertex), gx.quad);
	GX2SetPixelTexture(&source->texture, gx.sampler_location);
	GX2SetPixelSampler(&gx.sampler, gx.sampler_location);
	GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, 6, 0, 1);
}

static void present(const struct dk_command_present *command)
{
	struct target *back = target_get(&command->back_buffer);
	const OSTime now = OSGetSystemTime();

	WHBGfxBeginRender();
	WHBGfxBeginRenderTV();
	if (back)
		blit_to(back, WHBGfxGetTVColourBuffer(), WHBGfxGetTVContextState());
	WHBGfxFinishRenderTV();
	WHBGfxBeginRenderDRC();
	if (back)
		blit_to(back, WHBGfxGetDRCColourBuffer(), WHBGfxGetDRCContextState());
	WHBGfxFinishRenderDRC();
	WHBGfxFinishRender();
	gx.frames++;
	if (gx.frames == 1)
		wiiu_log("GX2: first frame presented");
	if (!gx.told || now - gx.told >= OSSecondsToTicks(5))
	{
		unsigned i;

		gx.told = now;
		wiiu_log("GX2: %u frames; commands since the last note, by type (target 1, clear 2, present 3, state 4, shaders 5 ...): ", gx.frames);
		for (i = 1; i < 18; i++)
			if (gx.counts[i])
				wiiu_log("GX2:   command %u: %u", i, gx.counts[i]);
		wiiu_log("GX2:   %u draws not made, %u clears of part of a target or of some channels made whole or skipped", gx.counted_draws,
			gx.partial_clears);
		memset(gx.counts, 0, sizeof(gx.counts));
		gx.counted_draws = 0;
		gx.partial_clears = 0;
	}
	context_begin();
}

/* ---------- the stream */

void host_gx2_submit(uint32_t commands, uint32_t size);
uint32_t host_gx2_retired(void);
void host_gx2_idle(void);
uint32_t host_gx2_visibility(uint32_t index);
unsigned int host_gx2_output_height(void);

/* the shell's (wiiu_shell.c): held here while the app is in the background */
void wiiu_foreground_gate(int is_game_thread);

void host_gx2_submit(uint32_t commands, uint32_t size)
{
	const unsigned char *at = (const unsigned char *)(uintptr_t)commands;
	const unsigned char *end = at + size;

	wiiu_foreground_gate(1);
	if (!initialize())
		return;
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
		case DK_COMMAND_DRAW:
			gx.counted_draws++;
			break;
		default:
			break;
		}
		at += header->size;
	}
}

/* the highest submission the GPU has finished: everything handed over is waited for at its present, so all of it */
uint32_t host_gx2_retired(void)
{
	return 0xffffffffu;
}

void host_gx2_idle(void)
{
	GX2DrawDone();
}

uint32_t host_gx2_visibility(uint32_t index)
{
	(void)index;
	return 0;
}

/* the lines the screen's targets are drawn at, at most: the TV's */
unsigned int host_gx2_output_height(void)
{
	return initialize() ? (unsigned int)WHBGfxGetTVColourBuffer()->surface.height : 480u;
}
