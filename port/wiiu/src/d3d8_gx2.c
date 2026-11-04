/*
D3D8_DK.C

The Xbox Direct3D 8 device for the Switch's deko3d renderer
(port/switch/DEKO3D.md). Linked into the Switch's game image,
halo_guest.elf, in place of port/linux/src/d3d8_gl.c, which the other builds
keep unchanged.

The parts of d3d8_gl.c that are not OpenGL are copied here as they are - the
screen's width, the state the XDK's inline functions keep, the vertical
blank, the reserved viewport constants, render and texture stage state,
vertex shaders and their declarations, streams and immediate mode - and are
to be merged with it later. What d3d8_gl.c does with OpenGL, this file will
do through the host's deko3d backend (port/switch/host/host_dk.c), to which
it writes a stream of commands over a frame (dk_commands.h). So far clears
and presenting go there; a draw and a visibility test do nothing yet.
*/

#include "xgpu.h"
#include "sdl_platform.h"
#include "halo_ui_pointer.h"
#include "touch_menu.h"
#include "port_config.h"
#include "posix.h"
#include "dk_commands.h"
#include "wiiu_immediate.h"
#include "wiiu_inflight.h"
#include "wiiu_indices.h"
#include "wiiu_pending_reads.h"
#include "wiiu_stream_queue.h"
#include "dk_shaders.h"
#include "dk_derive.h"

#include <coreinit/time.h>
#include <coreinit/systeminfo.h>
#include "dk_textures.h"
#include "hud_hires.h"
#include "menu_files.h"
#include "shader_key_wire.h"

/* gx2_shaders.c: the programs the game makes, kept by hash for the pack generator */
void dk_shader_program_note(uint64_t program_hash, const uint32_t *words, unsigned long count);
#include "../../linux/game/cache_file_formats.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void d3d8_surface_initialize(D3DSurface *surface, D3DFORMAT format, unsigned long width, unsigned long height);
void d3d8_surface_resize(D3DSurface *surface, D3DFORMAT format, unsigned long width, unsigned long height);

/* The Xbox memory window's address in the real memory: the Switch moves it (halo_port_window.h); here it is where the game put it until
the window is a heap block with rebased pointers (WIIU_PORT_PLAN.md, 6.2) */
#define PORT_WINDOW_REBASE(address) (address)

/* waits until a time of the monotonic clock (newlib has no clock_nanosleep) */
static void sleep_until(const struct timespec *target)
{
	struct timespec now, wait;

	clock_gettime(CLOCK_MONOTONIC, &now);
	wait.tv_sec = target->tv_sec - now.tv_sec;
	wait.tv_nsec = target->tv_nsec - now.tv_nsec;
	if (wait.tv_nsec < 0)
	{
		wait.tv_nsec += 1000000000L;
		wait.tv_sec--;
	}
	if (wait.tv_sec >= 0)
		nanosleep(&wait, NULL);
}

/* ---------- the screen's width

The Xbox screen is 640x480. The native ports can draw a wider one: 480
lines, and as many columns as the display's shape gives. On Android that is
display.screen_width (port_config.c; 640 keeps 4:3); on the desktop, the
display's shape while the game is fullscreen, and 640 in a window. The
game's camera derives its horizontal field of view from the viewport, so the
3D view simply widens. The menus and full-screen overlays are laid out for
640 columns; while they draw (halo_screen_ui_offset), everything shifts right
to center them.

Fullscreen on the desktop also draws at the display's resolution: render
targets the size of the screen get that many pixels (screen_scale), and
viewports, clears and visibility counts are scaled to match, so the game
still works in its 480 lines. The width and the scale change only between
frames, after one is presented (halo_screen_commit). */

#define SCREEN_HEIGHT 480
#define SCREEN_MAXIMUM_WIDTH 1920

/* the width the game draws, 0 until first asked, and how many pixels a
render target the size of the screen has per unit of it */
static long screen_width;
static float screen_scale[2] = { 1.0f, 1.0f };
static long ui_offset;
#define UI_OFFSET ((GLint)ui_offset)

/* Dynamic resolution (wiiu_gx2.c, wiiu_drs.c): the share of the screen's
targets, each way, that frames draw into; 1 when they draw all of it. The
targets keep the size screen_scale gives them, that of the most lines
(display.render_lines), so that a change of step makes no new target and
leaves them where they are in the eDRAM. A frame at fewer lines draws into
their top left part: target_scale, which viewports, scissors, clears and
visibility counts follow, is the screen's scale times this, and a draw that
samples one of the targets reads that part (facts_make). It changes only
between frames (halo_screen_commit), as the screen's scale does. */
static float drs_fraction[2] = { 1.0f, 1.0f };

/* wiiu_gx2.c's: the lines the GX2 thread asks the next frames to be drawn at
(halo/drs_lines.txt's, or the controller's), or 0 before its first present */
unsigned int host_gx2_drs_lines(void);

/* port/linux/game/hud_hires_tags.c's: whether a menu has the screen (the
controller is off while one has: the present command's DK_PRESENT_MENUS) */
int port_menus_active(void);

/* port/linux/game/gamepad_window.c's: the first split-screen window's pixels
(x0, y0, x1, y1) and the windows' count, 1 for the whole screen */
short gamepad_window_bounds(short bounds[4]);

/* changed when what a draw's facts but its stages' come from may have (facts_make): the targets' scale, the viewport, the menus' offset, the
texture clip, the zoom's blend */
static unsigned long facts_serial = 1;

#ifdef HALO_SWITCH
/* the host's (host_dk.c): the height it presents at, 1080 docked and 720 in
handheld */
unsigned int host_gx2_output_height(void);
#endif

static void screen_mode_choose(long *width, float scale[2])
{
#ifdef HALO_ANDROID
	/* display.screen_width, or 0 for the display's shape, which the app
	passes (port/android/host/host_main.c) */
	const char *display = getenv("HALO_DISPLAY_WIDTH");

	*width = config_integer("display.screen_width");
	if (*width <= 0)
		*width = display ? atol(display) : 640;
	if (*width < 640)
		*width = 640;
	if (*width > 1600)
		*width = 1600;
	*width &= ~1L;
	scale[0] = scale[1] = 1.0f;
#ifdef HALO_SWITCH
	/* display.resolution_limit: the lines the screen's targets are drawn at,
	"480" (the game's own), "720" or "1080", no more than the host presents
	at (1080 docked, 720 in handheld), which the host scales them up to */
	{
		const char *limit = config_string("display.resolution_limit");
		unsigned int height = !strcmp(limit, "1080") ? 1080 : !strcmp(limit, "720") ? 720 : SCREEN_HEIGHT;
		unsigned int output = host_gx2_output_height();

		if (height > output)
			height = output;
		if (height > SCREEN_HEIGHT)
			scale[0] = scale[1] = (float)height / (float)SCREEN_HEIGHT;
	}
#endif
#else
	long display_width, display_height;

	*width = 640;
	scale[0] = scale[1] = 1.0f;
	if (platform_screen_mode(&display_width, &display_height) && display_width > 0 && display_height > 0)
	{
		long wanted = (SCREEN_HEIGHT * display_width + display_height / 2) / display_height;

		*width = wanted < 640 ? 640 : wanted > SCREEN_MAXIMUM_WIDTH ? SCREEN_MAXIMUM_WIDTH : wanted & ~1L;
		scale[0] = (float)display_width / (float)*width;
		scale[1] = (float)display_height / (float)SCREEN_HEIGHT;
		/* a display narrower or wider than the game can be: the picture
		keeps its shape and the display blit letterboxes it */
		if (*width != wanted && *width != (wanted & ~1L))
			scale[0] = scale[1] = scale[0] < scale[1] ? scale[0] : scale[1];
	}
#endif
}

long halo_screen_width(void)
{
	if (!screen_width)
	{
		screen_mode_choose(&screen_width, screen_scale);
		platform_log("screen: %ldx%d drawn at %.0fx%.0f", screen_width, SCREEN_HEIGHT,
			screen_width * screen_scale[0], SCREEN_HEIGHT * screen_scale[1]);
	}
	return screen_width;
}

/* the display's pixels for each of the 480 lines (text_hires.c). It stays
that of the most lines while dynamic resolution draws fewer: the high-res
text's glyphs are made for it, and all of them are made again whenever it
changes, so a lower step draws the glyphs made for the most lines a little
smaller (by at most 1.33 times, at 0.75 of them). */
float halo_screen_pixel_scale(void)
{
	halo_screen_width();
	return screen_scale[1];
}

/* how many pixels the screen's targets draw to the Xbox's one, the larger
of the two ways, at the step of dynamic resolution the frame is drawn at
(the screen effects' convolutions: rasterizer_xbox_screen_effect.c, which
takes as many passes as there are pixels to the Xbox's one, so that a lower
step takes no more passes than its pixels need) */
float halo_screen_scale(void)
{
	float x, y;

	halo_screen_width();
	x = screen_scale[0] * drs_fraction[0];
	y = screen_scale[1] * drs_fraction[1];
	return x > y ? x : y;
}

void halo_screen_ui_offset(unsigned char centered)
{
	ui_offset = centered ? (halo_screen_width() - 640) / 2 : 0;
	facts_serial++;
}

/* the same for a player's view of a split screen (its width): a half screen's 640 columns are centred as the screen's, a quarter's 320 in the
quarter (render.c, as d3d8_gl.c does); and the shift in effect (ui_widget.c) */
void halo_screen_ui_offset_view(long view_width)
{
	long width = halo_screen_width();

	/* (a quarter's pause menu and dialogs are 320 wide: the Xbox's) */
	ui_offset = view_width > 0 && view_width < width ? (view_width > 320 ? (view_width - 320) / 2 : 0) : (width - 640) / 2;
}

long halo_screen_ui_offset_current(void)
{
	return ui_offset;
}

/* display.shadow_resolution, display.per_pixel_lighting, display.anti_aliasing,
display.model_detail, display.hdr and the overshield's rise are the OpenGL
renderer's (d3d8_gl.c): here the shadow maps stay the Xbox's 128x128, every
draw is lit as its vertex shader lights it, the 3D view is drawn as it is
(no tone curve, no HDR calibration's patterns), models choose their detail as
the Xbox's (stock), and a plasma shader's colors do not fade above a height
(an overshield effect shows at once, whole) */
long halo_shadow_map_scale(void)
{
	return 1;
}

void halo_vertex_shader_lighting(unsigned long handle)
{
	(void)handle;
}

void halo_screen_anti_alias(short x0, short y0, short x1, short y1)
{
	(void)x0; (void)y0; (void)x1; (void)y1;
}

void halo_screen_tone_map(short x0, short y0, short x1, short y1)
{
	(void)x0; (void)y0; (void)x1; (void)y1;
}

int halo_hdr_calibration_available(void)
{
	return 0;
}

void halo_hdr_calibration_show(int pattern, int paper_white, int peak_brightness)
{
	(void)pattern; (void)paper_white; (void)peak_brightness;
}

int halo_model_detail_stock(void)
{
	return 1;
}

/* port/linux/game/render_governor.c's (the Direct3D layer is built without the game's headers): dynamic quality's model level of detail, 1 with it off */
float render_governor_lod_scale(void);
void render_governor_frame(double work_ms, double tick_ms, double render_ms, double now_ms);

float halo_model_detail_scale(int level)
{
	/* (level 1: the size the level of detail is chosen by; 0: the size under which a model is not drawn at all, left to the objects' own culling) */
	return level ? render_governor_lod_scale() : 1.0f;
}

void d3d8_gl_set_height_fade(float inverse_softness, float height, float offset, float scale)
{
	(void)inverse_softness; (void)height; (void)offset; (void)scale;
}

static void *stream_command(uint32_t type, unsigned long size);

/* texture coordinate 0's clip, for the draws that follow: the lowest and
highest u and v as the texture is sampled (0 to 1 across it), and whether
to clip (the pixel parameters' alpha_reference.y to .w) */
static float texture_clip[3];

/* The draws that follow shaded only where texture coordinate 0's u and v are
in [low, high], or everywhere: an object's shadow cast on the level
(rasterizer_xbox_shadows.c) changes nothing outside its map, and was shaded
over the whole of every triangle it falls on - most of a crowd's GPU time.
The pixel key has NV2A_PIXEL_KEY_CLIP while it is on, and that shader lets
the pixels outside go before it reads a texture (nv2a_psh_dk.c). (The vertex
shaders' clip distances were tried first: the clipper did not clip by the
values they were given.) */
void halo_texture_clip(unsigned char clip, float low, float high)
{
	/* debug.gpu_texture_clip: "on", "off" (never clipped), "wide" (clipped
	by limits nothing reaches: the clip's own effect, apart from where), or
	"<low>,<high>": the limits to clip by, whatever the caller asks */
	static int mode = -1;
	static float set_low, set_high;

	if (mode < 0)
	{
		const char *setting = config_string("debug.gpu_texture_clip");

		mode = !strcmp(setting, "off") ? 0 : !strcmp(setting, "wide") ? 2 : 1;
		if (mode == 1 && sscanf(setting, "%f,%f", &set_low, &set_high) == 2)
			mode = 3;
		if (mode != 1)
			platform_log("texture clip: %s (debug.gpu_texture_clip)", setting);
	}
	if (mode == 0)
		clip = FALSE;
	else if (mode == 2)
	{
		low = -1000.0f;
		high = 1000.0f;
	}
	else if (mode == 3)
	{
		low = set_low;
		high = set_high;
	}
	texture_clip[0] = clip ? low : 0.0f;
	texture_clip[1] = clip ? high : 0.0f;
	texture_clip[2] = clip ? 1.0f : 0.0f;
	facts_serial++;
}

/* the zoom's blend of its blur over the screen, for the draws that follow
(rasterizer_xbox_screen_effect.c): the pixel key has NV2A_PIXEL_KEY_WARP_BLEND
while it is on */
static unsigned char warp_blend;

/* the zoom's blur drawn on a small copy of the screen and blended over it
once (rasterizer_xbox_screen_effect.c), instead of its passes over the whole
screen; debug.original_zoom draws it as the game does */
unsigned char halo_screen_fast_warp(void)
{
	static int on = -1;

	if (on < 0)
	{
		on = !config_boolean("debug.original_zoom");
		if (!on)
			platform_log("zoom: as the game draws it (debug.original_zoom)");
	}
	return (unsigned char)on;
}

void halo_screen_warp_blend(unsigned char on)
{
	warp_blend = on;
	facts_serial++;
}

/* the part of a frame the game is about to draw, for the host's record of
the commands it ran (DK_COMMAND_MARKER) */
void halo_render_marker(char const *name)
{
	struct dk_command_marker *command = stream_command(DK_COMMAND_MARKER, sizeof(*command));
	size_t length = 0;

	while (name[length] && length < sizeof(command->name) - 1)
	{
		command->name[length] = name[length];
		length++;
	}
	while (length < sizeof(command->name))
		command->name[length++] = 0;
}

/* ---------- state the XDK header's inline functions read and write */

DWORD D3D__RenderState[D3DRS_MAX];
DWORD D3D__TextureState[D3DTSS_MAXSTAGES][D3DTSS_MAX];
WORD *D3D__IndexData;
BYTE D3D__StateBlockDirty[1024];

/* ---------- vertex shaders */

#define VERTEX_SHADER_SIGNATURE 0x76736864UL /* 'vshd' */
#define VERTEX_PROGRAM_SLOTS 136

struct vertex_element
{
	unsigned char reg;
	unsigned char stream;
	unsigned char type;
	unsigned char bytes;
	unsigned short offset;
};

struct vertex_shader_object
{
	unsigned long signature;
	unsigned long id;
	DWORD *instructions;
	unsigned long instruction_count;
	/* FNV-1a 64 over the instruction count and words (dk_shaders.h's
	mixers): the program's hash, half of the shader key the deko3d
	renderer's cache knows a vertex shader by (dk_shaders.c) */
	uint64_t program_hash;
	struct vertex_element elements[XGPU_VERTEX_ATTRIBUTE_COUNT];
	unsigned long element_count;
	unsigned long packed_mask;
};

/* ---------- the device */

struct dk_device
{
	D3DPRESENT_PARAMETERS presentation;
	D3DSurface back_buffer;
	D3DSurface depth_buffer;
	D3DSurface *render_target;
	D3DSurface *depth_stencil;
	D3DVIEWPORT8 viewport;
	D3DMATRIX transforms[D3DTS_MAX];
	D3DBaseTexture *textures[D3DTSS_MAXSTAGES];
	D3DPalette *palettes[D3DTSS_MAXSTAGES];
	D3DSHADERCONSTANTMODE shader_constant_mode;

	struct vertex_shader_object *vertex_shader;
	struct vertex_shader_object *program_slots[VERTEX_PROGRAM_SLOTS];
	unsigned long program_address;
	float constants[XGPU_VERTEX_CONSTANT_COUNT][4];
	float viewport_scale[4];
	float viewport_offset[4];

	struct
	{
		DWORD data;
		UINT stride;
		/* the buffer itself, whose Lock records its last use */
		D3DVertexBuffer *buffer;
	} streams[16];
	/* SetIndices' base vertex (d3d8_gl.c) */
	UINT base_vertex_index;
	D3DIndexBuffer *index_buffer;

	/* the current value of each input register (SetVertexData) */
	float attributes[XGPU_VERTEX_ATTRIBUTE_COUNT][4];
	BOOL immediate_active;
	BOOL immediate_skipped; /* the Begin was of a visibility test's quad that nothing sees: its vertices are not kept */
	struct wiiu_immediate immediate; /* the vertices since Begin, and the registers SetVertexData wrote */
	D3DPRIMITIVETYPE immediate_type;

	BOOL visibility_test_active;

	unsigned long frame;
	unsigned long next_vertex_shader_id;
	BOOL created;
};

static struct dk_device device;

/* the render states, and each stage's texture states, written since the draw that last told the GX2 thread (state_writes_send), a bit each: every
function that writes D3D__RenderState or D3D__TextureState marks the words it writes (the XDK's inline SetRenderState calls one of them for every
state), so a draw compares those words with the GX2 thread's copy, not the whole of both tables (the two tables' compares, nearly every draw since
nearly every draw follows a state's change, were each draw's longest loop). render_states_all: every render state is compared, the device being new
or SetRenderState_Simple having been given a method that is no simple state's */
#define RENDER_STATE_WORDS ((D3DRS_MAX + 31) / 32)
static uint32_t render_states_dirty[RENDER_STATE_WORDS];
static BOOL render_states_all = TRUE;
static uint32_t texture_states_dirty[D3DTSS_MAXSTAGES] = { ~0u, ~0u, ~0u, ~0u };

/* The producer's last-sent values, not memory shared with the GX2 thread.
A setter only needs to mark a word when the requested value differs from
that copy. Comparing the current game table would miss a direct prewrite. */
static DWORD render_states_told[D3DRS_MAX];
static DWORD texture_states_told[D3DTSS_MAXSTAGES][D3DTSS_MAX];
static BOOL states_told_valid;

static inline void render_state_mark(unsigned long state)
{
	if (state < D3DRS_MAX)
		render_states_dirty[state / 32] |= 1u << (state % 32);
}

/* (first to last, inclusive) */
static void render_states_mark(unsigned long first, unsigned long last)
{
	for (; first <= last; first++)
		render_state_mark(first);
}

static inline void texture_state_mark(DWORD stage, unsigned long type)
{
	if (stage < D3DTSS_MAXSTAGES && type < D3DTSS_MAX)
		texture_states_dirty[stage] |= 1u << type;
}

/* Store every requested value, including a restoration after a direct
write, while leaving already-sent words out of the next sparse scan. */
static inline void render_state_store(unsigned long state, DWORD value)
{
	if (state < D3DRS_MAX)
	{
		if (!states_told_valid || render_states_told[state] != value)
			render_state_mark(state);
		D3D__RenderState[state] = value;
	}
}

static inline void texture_state_store(DWORD stage, unsigned long type, DWORD value)
{
	if (stage < D3DTSS_MAXSTAGES && type < D3DTSS_MAX)
	{
		if (!states_told_valid || texture_states_told[stage][type] != value)
			texture_state_mark(stage, type);
		D3D__TextureState[stage][type] = value;
	}
}

/* changed when a register's current value (SetVertexData) or the byte order the streams were found in (halo_stream_endian_forget) may have: a
draw's vertex format depends on them (format_for_draw); and when a stream is set (SetStreamSource), which it depends on too */
static unsigned long attributes_serial = 1, endian_serial = 1, streams_serial = 1;
/* changed when a resource a draw stamps is bound (draw_resources_used): the targets, a stage's texture or palette, a stream, the index buffer */
static unsigned long resources_serial = 1;

static D3DDevice *device_pointer(void)
{
	return (D3DDevice *)&device;
}

static float dword_to_float(DWORD value)
{
	union { DWORD d; float f; } u;

	u.d = value;
	return u.f;
}

/* ---------- the command stream (dk_commands.h)

Commands are written here over a frame and handed to the host half
(port/switch/host/host_dk.c) at its end, or sooner if the stream fills. */

void host_gx2_submit(unsigned int commands, unsigned int size);
/* the highest submission the GPU has finished (host_dk.c) */
unsigned int host_gx2_retired(void);
/* the queue drained (host_dk.c) */
void host_gx2_idle(void);

/* (halo_switch_renderer_idle is below, after the stream's functions) */
/* a visibility test slot's latest count (host_dk.c) */
unsigned int host_gx2_visibility(unsigned int index);
int host_gx2_visibility_enabled(void);

#define STREAM_SIZE (1024 * 1024)

/* the buffers the stream is written into: one when the host runs a stream as it is handed over, STREAM_SLOTS when the GX2 thread runs them while the game goes on (host_gx2_async).
Six: a stream is handed over every 40 draws, about twenty a frame, and with three the game thread could be only two ahead of the GX2 thread - it waited
for a slot whenever that thread waited for the display's flip or had a frame's worth of work queued (6% of the game thread standing) */
#define STREAM_SLOTS 6
/* (halo/stream_slots.txt can ask for up to STREAM_SLOTS_MAX: the slots beyond STREAM_SLOTS are made when asked for, in the heap - not static memory: 2 MB more of it moved the host's heap and
changed the game's pictures in the digest harness, whose default path must not change) */
#define STREAM_SLOTS_MAX 8
int host_gx2_stream_slots(void);
static uint32_t *stream_extra[STREAM_SLOTS_MAX - STREAM_SLOTS];
/* (on cache lines: the lines made in a slot, stream_lines_make, are whole lines of it) */
static uint32_t stream_buffers[STREAM_SLOTS][STREAM_SIZE / 4] __attribute__((aligned(64)));
static uint32_t *stream = stream_buffers[0];
static unsigned long stream_used;
/* The stream's cache lines below this are made (dcbz) or written: those past it are made before anything is written in them. A write to a line that is not in the cache first
reads it from memory on the 750 core, and the stream is a megabyte a slot of memory last written a few frames ago: the copies into it (wiiu_memcpy.c) made their lines, but
the immediate vertices written straight into it (the decals') waited for every line, more than the copy they replaced. */
static unsigned long stream_lines_made;

#define STREAM_LINE 32UL

/* the whole lines between from and to made (dcbz makes the line an address is in: a line partly outside, which may hold what is written, is left) */
static void lines_make(unsigned char *from, unsigned char *to)
{
	from = (unsigned char *)(((uintptr_t)from + STREAM_LINE - 1) & ~(uintptr_t)(STREAM_LINE - 1));
	to = (unsigned char *)((uintptr_t)to & ~(uintptr_t)(STREAM_LINE - 1));
	for (; from < to; from += STREAM_LINE)
		__asm__ volatile("dcbz 0,%0" : : "r"(from) : "memory");
}

/* the stream's lines made up to end (an offset into it), from the first not made */
static inline void stream_lines_make(unsigned long end)
{
	if (end <= stream_lines_made)
		return;
	end = (end + STREAM_LINE - 1) & ~(STREAM_LINE - 1);
	if (end > STREAM_SIZE)
		end = STREAM_SIZE;
	lines_make((unsigned char *)stream + stream_lines_made, (unsigned char *)stream + end);
	stream_lines_made = end;
}

/* the queue of streams to the GX2 thread, and its buffers: the host (wiiu_gx2.c) takes from them */
struct stream_queue dk_stream_queue;

uint32_t *dk_stream_buffer(unsigned slot)
{
	return slot < STREAM_SLOTS ? stream_buffers[slot] : stream_extra[slot - STREAM_SLOTS];
}
/* Each handing over of the stream is one submission, numbered from 1 as the
host numbers them: this is the number of the one being written. */
static unsigned long submission = 1;

/* buffers of the guest's that commands name (a decoded texture's texels): the
host reads them when it runs the stream, so they are freed once it has */
#define PENDING_FREE_LIMIT 256
#define PENDING_FREE_BYTES (8 * 1024 * 1024)

static struct { void *buffer; unsigned long submission; } pending_frees[PENDING_FREE_LIMIT];
static unsigned long pending_free_count;
static unsigned long pending_free_bytes;

/* ---------- the GX2 thread running streams while the game goes on (halo/gx2_thread.txt 7)

A stream is posted to the queue and the game goes on writing the next, in another buffer; the host's thread runs them in order and counts them retired. What a stream reads of the game's memory
is read late, so the ranges are kept with the stream's number (`inflight`), and a write the game announces into one waits for the stream that reads it (after handing it over, if it is still being
written); buffers a command names are freed once its stream is retired. */
int host_gx2_async(void);
void host_gx2_pause(void);
void wiiu_foreground_gate(int is_game_thread);

static double profile_now_ms(void);
static int dk_async = -1;
static struct inflight inflight;

static int async_on(void)
{
	if (dk_async < 0)
	{
		dk_async = host_gx2_async();
		if (dk_async)
		{
			int slots = host_gx2_stream_slots(), made = STREAM_SLOTS;

			for (; made < slots; made++)
			{
				/* (malloc'd and aligned by hand to a cache line pair: never freed) */
				void *block = malloc(STREAM_SIZE + 64);

				if (!block)
					break;
				stream_extra[made - STREAM_SLOTS] = (uint32_t *)(((uintptr_t)block + 63) & ~(uintptr_t)63);
			}
			stream_queue_init(&dk_stream_queue, made < slots ? made : slots);
		}
	}
	return dk_async;
}

/* waits until the stream numbered `submission` is retired; a wait of more than three seconds in the foreground is said (once), and goes on */
/* the frame's parts (source/main/main.c): the game's ticks and its rendering, for the frame time note and the hitch notes */
static double part_start, frame_tick_ms, frame_render_ms, sum_tick_ms, sum_render_ms, longest_tick_ms;
static unsigned long frame_ticks, sum_ticks, frames_with_ticks;

void wiiu_frame_part(int part, int ticks);

void wiiu_frame_part(int part, int ticks)
{
	const double now = profile_now_ms();

	if (part == 0 || part == 2)
	{
		part_start = now;
		return;
	}
	if (part == 1)
	{
		frame_tick_ms += now - part_start;
		frame_ticks += (unsigned long)(ticks > 0 ? ticks : 0);
	}
	else
		frame_render_ms += now - part_start;
}

static double waits_ms; /* the game thread's time in async_wait_retired and halo_switch_renderer_idle, for the hitch notes (D3DDevice_Present) */
static double idle_ms;
/* this frame's time in the Direct3D layer's draws, in the time base's ticks: the draws add ticks, and they are made milliseconds only where they are
told (a tick count made a double and divided, twice a draw, was a library call and a division of the game thread's each time) */
static OSTime frame_draw_ticks;
static unsigned long idle_calls;

static void async_wait_retired(uint32_t number)
{
	static int said;
	const double start = profile_now_ms();

	if (stream_queue_retired(&dk_stream_queue) >= number)
		return;
	while (stream_queue_retired(&dk_stream_queue) < number)
	{
		host_gx2_pause();
		if (!said && profile_now_ms() - start > 3000.0)
		{
			said = 1;
			platform_log("GX2 thread: waiting 3 s for stream %lu (%lu retired, %lu posted)", (unsigned long)number, (unsigned long)stream_queue_retired(&dk_stream_queue),
				(unsigned long)(stream_queue_submission(&dk_stream_queue) - 1));
		}
	}
	waits_ms += profile_now_ms() - start;
}

/* frees what the retired streams' commands named */
static void async_frees(int all_for_room)
{
	unsigned long kept = 0, i;

	for (i = 0; i < pending_free_count; i++)
	{
		if (pending_frees[i].submission <= stream_queue_retired(&dk_stream_queue))
			free(pending_frees[i].buffer);
		else
			pending_frees[kept++] = pending_frees[i];
	}
	pending_free_count = kept;
	if (all_for_room && pending_free_count == PENDING_FREE_LIMIT)
	{
		/* (no room for another: the oldest is waited for) */
		async_wait_retired((uint32_t)pending_frees[0].submission);
		async_frees(0);
	}
}

/* The memory the commands being written read when they are handed over, not when they are written: vertex streams, the texels of compressed textures (the host reads them
where the game keeps them). The game remakes a dynamic buffer after a draw has named it - split screen draws the second view into the first's, a locked buffer is rewritten
- and the host would then draw the new bytes for both. A write the game announces (memory_watch_prepare_write: Lock, a file read; memory_watch_forget: memory freed) into
one of these ranges hands the stream over first, as the Xbox's runtime does for a busy resource, so each draw reads what the game had made when it drew. The ranges are few
(a draw's streams follow the last's); if there are too many the stream is handed over, as it is at a present. */
static struct pending_reads pending_reads;

static void stream_flush(void);
/* the game thread's waits for a free stream slot (the GX2 thread has not retired the stream that used the slot the next stream goes in), in the time base's ticks and counted: [0] in the
draws' hand-overs and the rest of the frame's work, [1] in the present's (where it is the frame's pacing: the GX2 thread waits for the display's flip). In the log with the draw statistics. */
static OSTime slot_wait_ticks[2];
static unsigned long slot_waits[2];
static int in_present;
/* why the stream was handed over, counted (in the log with the draw statistics): 0 a present, 1 the table of read ranges full, 2 a write into a range a draw reads, 3 the stream full, 4 too much
waiting to be freed, 5 a resource in use asked for, 6 IsBusy, 7 KickPushBuffer */
static unsigned long flush_reasons[9];
#define DRAWS_PER_HANDOVER 40
static unsigned draws_since_handover;
#define flush_because(reason) (flush_reasons[reason]++, stream_flush())

static void pending_read_note(const void *address, unsigned long size)
{
	if (dk_async > 0)
	{
		inflight_add(&inflight, address, size, (uint32_t)submission, stream_queue_retired(&dk_stream_queue));
		return;
	}
	if (!pending_reads_add(&pending_reads, address, size))
	{
		flush_because(1);
		pending_reads_add(&pending_reads, address, size);
	}
}

/* called by the memory watch (memory_watch_wiiu.c) before the game's write is announced */
void halo_stream_before_write(const void *address, unsigned long size);

void halo_stream_before_write(const void *address, unsigned long size)
{
	if (dk_async > 0)
	{
		const uint32_t hazard = inflight_hazard(&inflight, address, size, stream_queue_retired(&dk_stream_queue));

		if (hazard)
		{
			if (hazard >= (uint32_t)submission)
				flush_because(2); /* (the stream still being written is handed over first) */
			async_wait_retired(hazard);
		}
		return;
	}
	if (pending_reads_overlap(&pending_reads, address, size))
		flush_because(2);
}

/* for xbox_textures_gx2.c: a command in the stream reads these bytes of the game's memory */
void dk_stream_note_read(const void *address, unsigned long size)
{
	pending_read_note(address, size);
}

void host_gx2_window(uint32_t base, uint32_t size);

static void stream_flush(void)
{
	static int window_told;

	if (!window_told)
	{
		window_told = 1;
		host_gx2_window((uint32_t)PLATFORM_CONTIGUOUS_BASE, (uint32_t)PLATFORM_CONTIGUOUS_SIZE); /* (the host cuts it in blocks to keep the map's vertices) */
	}
	/* (an immediate draw being written at the stream's tail, past what is handed over: its vertices are moved out first, the slot is the GX2 thread's from now) */
	if (device.immediate.lent_floats)
		wiiu_immediate_unlend(&device.immediate);
	draws_since_handover = 0;
	if (async_on())
	{
		if (!stream_used)
			return;
		wiiu_foreground_gate(1);
		stream_queue_post(&dk_stream_queue, (uint32_t)stream_used);
		stream_used = 0;
		stream_lines_made = 0;
		submission++;
		stream = dk_stream_buffer(stream_queue_slot(&dk_stream_queue));
		/* (the slot to write next must be free: the stream that used it is retired) */
		if (!stream_queue_slot_free(&dk_stream_queue))
		{
			const OSTime waited_from = OSGetSystemTime();

			while (!stream_queue_slot_free(&dk_stream_queue))
				host_gx2_pause();
			slot_wait_ticks[in_present] += OSGetSystemTime() - waited_from;
			slot_waits[in_present]++;
		}
		async_frees(0);
		pending_free_bytes = 0;
		return;
	}
	pending_reads_clear(&pending_reads);
	if (!stream_used)
		return;
	host_gx2_submit((unsigned int)(uintptr_t)stream, (unsigned int)stream_used);
	stream_used = 0;
	stream_lines_made = 0;
	submission++;
	while (pending_free_count)
		free(pending_frees[--pending_free_count].buffer);
	pending_free_bytes = 0;
}

/* everything written is handed over, the GX2 thread's queue emptied, and the GPU waited for (host_dk.c's name, as the game's code asks for it) */
void halo_switch_renderer_idle(void);

void halo_switch_renderer_idle(void)
{
	const double start = profile_now_ms();

	stream_flush();
	if (dk_async > 0)
		async_wait_retired((uint32_t)(submission - 1));
	host_gx2_idle();
	idle_ms += profile_now_ms() - start;
	idle_calls++;
}

/* room for a command of size bytes, its header filled in */
static void *stream_command(uint32_t type, unsigned long size)
{
	struct dk_command_header *header;

	size = (size + 3) & ~3UL;
	/* (the command goes where an immediate draw's vertices are being written: they move out of its way) */
	if (device.immediate.lent_floats)
		wiiu_immediate_unlend(&device.immediate);
	if (stream_used + size > STREAM_SIZE)
		flush_because(3);
	stream_lines_make(stream_used + size);
	header = (struct dk_command_header *)((unsigned char *)stream + stream_used);
	header->type = type;
	header->size = (uint32_t)size;
	stream_used += size;
	return header;
}

void *dk_stream_command(uint32_t type, unsigned long size)
{
	return stream_command(type, size);
}

void dk_stream_free_after_handover(void *buffer, unsigned long size)
{
	if (dk_async > 0 && pending_free_count == PENDING_FREE_LIMIT)
		async_frees(1);
	pending_frees[pending_free_count].buffer = buffer;
	pending_frees[pending_free_count].submission = submission;
	pending_free_count++;
	pending_free_bytes += size;
	if (pending_free_count == PENDING_FREE_LIMIT || pending_free_bytes >= PENDING_FREE_BYTES)
		flush_because(4);
}

static void color_to_vec4(D3DCOLOR color, float *out)
{
	out[0] = ((color >> 16) & 0xff) / 255.0f;
	out[1] = ((color >> 8) & 0xff) / 255.0f;
	out[2] = (color & 0xff) / 255.0f;
	out[3] = ((color >> 24) & 0xff) / 255.0f;
}

/* ---------- vertical blank emulation */

#define VERTICAL_BLANK_NANOSECONDS (1000000000L / 60)

static pthread_mutex_t vertical_blank_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t vertical_blank_condition = PTHREAD_COND_INITIALIZER;
static D3DCALLBACK vertical_blank_callback;
static unsigned long vertical_blank_count;
static volatile unsigned int flip_count;
static unsigned long pending_flips;
static BOOL vertical_blank_thread_started = FALSE;

static void *vertical_blank_thread(void *unused)
{
	struct timespec next;
	void wiiu_thread_off_game_core(void);

	(void)unused;
	wiiu_thread_off_game_core();
	clock_gettime(CLOCK_MONOTONIC, &next);
	for (;;)
	{
		D3DCALLBACK callback;

		next.tv_nsec += VERTICAL_BLANK_NANOSECONDS;
		if (next.tv_nsec >= 1000000000L)
		{
			next.tv_nsec -= 1000000000L;
			next.tv_sec++;
		}
		sleep_until(&next);

		pthread_mutex_lock(&vertical_blank_lock);
		vertical_blank_count++;
		/* a presented frame becomes visible at the next vertical blank */
		if (pending_flips)
		{
			pending_flips--;
			flip_count++;
		}
		callback = vertical_blank_callback;
		pthread_cond_broadcast(&vertical_blank_condition);
		pthread_mutex_unlock(&vertical_blank_lock);

		if (callback)
			callback(0);
	}
	return NULL;
}

static void vertical_blank_start(void)
{
	pthread_mutex_lock(&vertical_blank_lock);
	if (!vertical_blank_thread_started)
	{
		pthread_t thread;

		if (pthread_create(&thread, NULL, vertical_blank_thread, NULL) == 0)
		{
			pthread_detach(thread);
			vertical_blank_thread_started = TRUE;
		}
		else
		{
			platform_log("cannot start the vertical blank thread");
		}
	}
	pthread_mutex_unlock(&vertical_blank_lock);
}

/* replaces main/d3d_intimacy.cpp, which reads the counter out of the Xbox
Direct3D runtime's private device structure */
volatile unsigned int *d3d_find_flipcount(void)
{
	return &flip_count;
}

void WINAPI D3DDevice_SetVerticalBlankCallback(D3DCALLBACK callback)
{
	pthread_mutex_lock(&vertical_blank_lock);
	vertical_blank_callback = callback;
	pthread_mutex_unlock(&vertical_blank_lock);
	vertical_blank_start();
}

void WINAPI D3DDevice_BlockUntilVerticalBlank(void)
{
	unsigned long count;

	vertical_blank_start();
	pthread_mutex_lock(&vertical_blank_lock);
	count = vertical_blank_count;
	while (vertical_blank_count == count)
		pthread_cond_wait(&vertical_blank_condition, &vertical_blank_lock);
	pthread_mutex_unlock(&vertical_blank_lock);
}
/* ---------- render targets

The textures and render targets the OpenGL renderer keeps are the host
backend's to keep here. Until it does, there are none. */

static void surface_dimensions(const D3DSurface *surface, unsigned long *width, unsigned long *height, BOOL *depth)
{
	struct xgpu_texture_description description;
	DWORD format;

	xgpu_texture_describe(surface->Format, surface->Size, &description);
	*width = description.width;
	*height = description.height;
	format = description.format;
	*depth = format == D3DFMT_D24S8 || format == D3DFMT_F24S8 || format == D3DFMT_D16 || format == D3DFMT_F16 ||
		format == D3DFMT_LIN_D24S8 || format == D3DFMT_LIN_F24S8 || format == D3DFMT_LIN_D16 || format == D3DFMT_LIN_F16;
}

/* the host's pixels to a game pixel in the targets bound (targets_bind):
the screen's scale for the screen's targets at dynamic resolution's step,
else 1. Viewports, clears and visibility counts are scaled by it */
static float target_scale[2] = { 1.0f, 1.0f };

/* the share of a surface that frames draw into each way: dynamic
resolution's, if it is the screen's size; all of any other */
static void target_share_of(unsigned long width, unsigned long height, float share[2])
{
	const int screen = width == (unsigned long)halo_screen_width() && height == SCREEN_HEIGHT;

	share[0] = screen ? drs_fraction[0] : 1.0f;
	share[1] = screen ? drs_fraction[1] : 1.0f;
}

/* a surface's scale: the screen's, if it is the screen's size; and drawn,
the scale frames draw into it at (the scale times its share) */
static void target_scale_of(unsigned long width, unsigned long height, float scale[2], float drawn[2])
{
	float share[2];

	scale[0] = scale[1] = 1.0f;
	if (width == (unsigned long)halo_screen_width() && height == SCREEN_HEIGHT)
	{
		scale[0] = screen_scale[0];
		scale[1] = screen_scale[1];
	}
	target_share_of(width, height, share);
	drawn[0] = scale[0] * share[0];
	drawn[1] = scale[1] * share[1];
}

/* a surface as the host knows it, and in scale the scale frames draw into
it at; depth_only: NONE unless it is a depth format (a depth buffer that is
not one is left unbound, as d3d8_gl.c does) */
static void surface_describe(const D3DSurface *surface, BOOL depth_only, struct dk_surface *out, float scale[2])
{
	unsigned long width, height;
	BOOL depth;
	float pixels[2];

	memset(out, 0, sizeof(*out));
	scale[0] = scale[1] = 1.0f;
	if (!surface || !surface->Data)
		return;
	surface_dimensions(surface, &width, &height, &depth);
	if (depth_only && !depth)
		return;
	target_scale_of(width, height, pixels, scale);
	out->data = surface->Data;
	out->width = (uint32_t)width;
	out->height = (uint32_t)height;
	out->kind = depth ? DK_SURFACE_DEPTH : DK_SURFACE_COLOR;
	out->pixel_width = (uint32_t)(width * pixels[0] + 0.5f);
	out->pixel_height = (uint32_t)(height * pixels[1] + 0.5f);
	out->active_width = (uint32_t)(width * scale[0] + 0.5f);
	out->active_height = (uint32_t)(height * scale[1] + 0.5f);
	/* (the share is below 1 or exactly 1: a rounding cannot make the part
	larger than the whole) */
	if (out->active_width > out->pixel_width)
		out->active_width = out->pixel_width;
	if (out->active_height > out->pixel_height)
		out->active_height = out->pixel_height;
}

/* the color surfaces drawn into, by their data, and the size each was last
drawn at: a texture whose data is one samples the host's target
(render-to-texture), as d3d8_gl.c's xgpu_render_target_find finds them */
#define RENDERED_LIMIT 128

static struct rendered
{
	DWORD data;
	unsigned long width, height;
} rendered[RENDERED_LIMIT];
static unsigned long rendered_count;
/* changed when a surface is noted or drawn at another size: a texture's facts depend on it (stage_facts_make) */
static unsigned long rendered_serial = 1;

/* whether two copies of a state are the same, compared a word at a time:
musl's memcmp goes a byte at a time, and every draw compares several
hundred bytes of state against what the host was told (7% of the game
thread in a match's profile). The structs compared are words throughout. */
static int words_equal(const void *a, const void *b, size_t size)
{
	const uint32_t *x = a, *y = b;
	size_t words = size / 4;

	/* (four words a pass, one branch: these compare every state a draw sends against the last, and nearly always find them equal) */
	for (; words >= 4; words -= 4, x += 4, y += 4)
	{
		if ((x[0] ^ y[0]) | (x[1] ^ y[1]) | (x[2] ^ y[2]) | (x[3] ^ y[3]))
			return 0;
	}
	for (; words; words--, x++, y++)
	{
		if (*x != *y)
			return 0;
	}
	return size % 4 ? !memcmp((const char *)a + size - size % 4, (const char *)b + size - size % 4, size % 4) : 1;
}

static void rendered_note(const struct dk_surface *surface)
{
	unsigned long index;

	if (surface->kind != DK_SURFACE_COLOR)
		return;
	for (index = 0; index < rendered_count && rendered[index].data != surface->data; index++)
		;
	if (index == rendered_count)
	{
		if (rendered_count == RENDERED_LIMIT)
			return;
		rendered_count++;
		rendered_serial++;
	}
	else if (rendered[index].width != surface->width || rendered[index].height != surface->height)
		rendered_serial++;
	rendered[index].data = surface->data;
	rendered[index].width = surface->width;
	rendered[index].height = surface->height;
}

static const struct rendered *rendered_find(DWORD data)
{
	unsigned long index;

	for (index = 0; index < rendered_count; index++)
	{
		if (rendered[index].data == data)
			return &rendered[index];
	}
	return NULL;
}

/* the mip composites the host has been told of (DK_COMMAND_COMPOSITE) */
#define COMPOSITE_LIMIT 32

static struct dk_command_composite composites_told[COMPOSITE_LIMIT];
static unsigned long composites_told_count;

/* tells the host of a texture rendered a level at a time, unless it knows
it as it is */
static void composite_tell(const struct xgpu_texture_description *description, DWORD data)
{
	struct dk_command_composite composite;
	unsigned long index, level;

	memset(&composite, 0, sizeof(composite));
	composite.data = data;
	composite.width = (uint32_t)description->width;
	composite.height = (uint32_t)description->height;
	composite.levels = (uint32_t)description->levels;
	for (level = 0; level < description->levels; level++)
		composite.level_data[level] = (uint32_t)(data + xgpu_texture_level_offset(description, level));
	for (index = 0; index < composites_told_count; index++)
	{
		if (!memcmp(composites_told[index].level_data, composite.level_data, sizeof(composite.level_data)) &&
			composites_told[index].data == data && composites_told[index].width == composite.width &&
			composites_told[index].height == composite.height && composites_told[index].levels == composite.levels)
			return;
	}
	{
		struct dk_command_composite *command = stream_command(DK_COMMAND_COMPOSITE, sizeof(*command));

		memcpy((char *)command + sizeof(command->header), (char *)&composite + sizeof(composite.header),
			sizeof(composite) - sizeof(composite.header));
	}
	if (composites_told_count < COMPOSITE_LIMIT)
		composites_told[composites_told_count++] = composite;
}

/* the targets the host has bound, as last told it; cleared when a frame
starts there anew */
static struct dk_command_targets targets_told;
static BOOL targets_known;

/* the surfaces' description and scale as targets_bind last made them, and what they came from: the two surfaces, their data, format and size
words, and the screen's width, scale and share (dynamic resolution's step). Every draw binds the targets, nearly always the ones before (0.8% of
the game thread in a fight). */
static struct
{
	BOOL valid;
	const D3DSurface *color, *depth;
	DWORD words[6];
	long screen_width;
	float screen_scale[2];
	float drs_fraction[2];
	struct dk_command_targets targets;
	float scale[2];
} targets_memo;

/* Set when what targets_bind made may no longer hold: the targets set (SetRenderTarget), the screen's width or scale changed (halo_screen_commit), or a
frame presented, after which the host starts with nothing bound and the game sizes its surfaces again in place (rasterizer_screen_width_update). Between
those the surfaces bound are as targets_bind found them - the game makes a surface's header before it binds it - and a draw does not read them again
(their six words, two surfaces' cache lines a draw); the frame's end checks that they are (targets_check). targets_drawable: targets_bind's answer. */
static BOOL targets_dirty = TRUE, targets_drawable;
static unsigned long targets_missed;

static void targets_words(const D3DSurface *color, const D3DSurface *depth, DWORD words[6])
{
	words[0] = color ? color->Data : 0;
	words[1] = color ? color->Format : 0;
	words[2] = color ? color->Size : 0;
	words[3] = depth ? depth->Data : 0;
	words[4] = depth ? depth->Format : 0;
	words[5] = depth ? depth->Size : 0;
}

/* tells the host the current targets, if they are not what it has; FALSE if there is nothing to draw into */
static BOOL targets_bind_again(void)
{
	const D3DSurface *color = device.render_target, *depth = device.depth_stencil;
	const long width = halo_screen_width();
	DWORD words[6];
	struct dk_command_targets targets;

	targets_words(color, depth, words);
	if (!targets_memo.valid || targets_memo.color != color || targets_memo.depth != depth || !words_equal(words, targets_memo.words, sizeof(words)) ||
		targets_memo.screen_width != width || targets_memo.screen_scale[0] != screen_scale[0] || targets_memo.screen_scale[1] != screen_scale[1] ||
		targets_memo.drs_fraction[0] != drs_fraction[0] || targets_memo.drs_fraction[1] != drs_fraction[1])
	{
		float color_scale[2], depth_scale[2];

		memset(&targets_memo.targets, 0, sizeof(targets_memo.targets));
		surface_describe(color, FALSE, &targets_memo.targets.color, color_scale);
		surface_describe(depth, TRUE, &targets_memo.targets.depth, depth_scale);
		targets_memo.scale[0] = targets_memo.targets.color.kind != DK_SURFACE_NONE ? color_scale[0] : depth_scale[0];
		targets_memo.scale[1] = targets_memo.targets.color.kind != DK_SURFACE_NONE ? color_scale[1] : depth_scale[1];
		targets_memo.valid = TRUE;
		targets_memo.color = color;
		targets_memo.depth = depth;
		memcpy(targets_memo.words, words, sizeof(words));
		targets_memo.screen_width = width;
		targets_memo.screen_scale[0] = screen_scale[0];
		targets_memo.screen_scale[1] = screen_scale[1];
		targets_memo.drs_fraction[0] = drs_fraction[0];
		targets_memo.drs_fraction[1] = drs_fraction[1];
	}
	targets = targets_memo.targets;
	if (targets.color.kind == DK_SURFACE_NONE && targets.depth.kind == DK_SURFACE_NONE)
		return FALSE;
	target_scale[0] = targets_memo.scale[0];
	target_scale[1] = targets_memo.scale[1];
	facts_serial++;
	if (!targets_known || !words_equal(&targets.color, &targets_told.color, sizeof(targets.color)) ||
		!words_equal(&targets.depth, &targets_told.depth, sizeof(targets.depth)))
	{
		struct dk_command_targets *command = stream_command(DK_COMMAND_TARGETS, sizeof(*command));

		command->color = targets.color;
		command->depth = targets.depth;
		rendered_note(&targets.color);
		targets_told = targets;
		targets_known = TRUE;
	}
	return TRUE;
}

/* the targets for a draw or a clear (targets_bind_again's, made again only when targets_dirty says it must be); has_depth (if not NULL): whether there
is a depth buffer */
static BOOL targets_bind(BOOL *has_depth)
{
	if (targets_dirty)
	{
		targets_drawable = targets_bind_again();
		targets_dirty = FALSE;
	}
	if (targets_drawable && has_depth)
		*has_depth = targets_memo.targets.depth.kind != DK_SURFACE_NONE;
	return targets_drawable;
}

/* at a frame's end: the surfaces bound are still those targets_bind described (a bound surface's header rewritten in place without SetRenderTarget
would have been drawn into as it was: logged), and from now on targets_bind looks at them again */
static void targets_check(void)
{
	if (!targets_dirty && targets_memo.valid)
	{
		DWORD words[6];

		targets_words(device.render_target, device.depth_stencil, words);
		if (device.render_target != targets_memo.color || device.depth_stencil != targets_memo.depth ||
			!words_equal(words, targets_memo.words, sizeof(words)))
		{
			static unsigned long told;

			if (++targets_missed >= told * 2 + 1)
			{
				told = targets_missed;
				platform_log("render targets: %lu times a bound surface was found changed with no SetRenderTarget having said so (drawn as it was until the frame's end)",
					targets_missed);
			}
		}
	}
	targets_dirty = TRUE;
}

struct xgpu_render_target *xgpu_render_target_find(unsigned long data)
{
	(void)data;
	return NULL;
}

/* xbox_textures.c, hud_hires.c and text_hires.c call this after their own GL
calls, which under deko3d have no context and do nothing */
void xgpu_gl_state_invalidate(void)
{
}

/* ---------- device creation */

Direct3D *WINAPI Direct3DCreate8(UINT sdk_version)
{
	(void)sdk_version;
	return (Direct3D *)1;
}

void WINAPI Direct3D_SetPushBufferSize(DWORD push_buffer_size, DWORD segment_count)
{
	(void)push_buffer_size;
	(void)segment_count;
}

/* each vertex constant register's serial is the value constants_serial took
when the register last changed; a program's registers are current up to
the serial it recorded when it last uploaded them. The serials are 64-bit,
as in d3d8_gl.c: a skinned model changes up to 132 registers a draw, and
32 bits wrapped, after which a draw found none of its registers changed
and kept what it last uploaded (the other split-screen window's view) */
static unsigned long long constant_serials[XGPU_VERTEX_CONSTANT_COUNT];
static unsigned long long constants_serial;
/* set when a window's viewport changes: the next draw sends every constant,
so one split-screen window cannot draw its map with the other's view */
static BOOL constants_known;
/* the register each of the latest serials changed, so a program that is
only a little behind finds its changed registers without a full scan */
#define CONSTANT_LOG_SIZE 1024
static unsigned char constant_log[CONSTANT_LOG_SIZE];

static void constants_store(unsigned long first, const void *data, unsigned long count)
{
	const float (*values)[4] = data;
	unsigned long index;

	for (index = 0; index < count; index++)
	{
		if (!words_equal(device.constants[first + index], values[index], sizeof(device.constants[0])))
		{
			memcpy(device.constants[first + index], values[index], sizeof(device.constants[0]));
			constant_serials[first + index] = ++constants_serial;
			constant_log[constants_serial % CONSTANT_LOG_SIZE] = (unsigned char)(first + index);
		}
	}
}

static void viewport_update_constants(void)
{
	/* Direct3D's reserved constants c[-38] and c[-37] map clip space to
	the screen; zscale is the depth buffer's range */
	float zscale = 16777215.0f;
	unsigned long width, height;
	BOOL depth;

	if (device.depth_stencil)
	{
		struct xgpu_texture_description description;

		xgpu_texture_describe(device.depth_stencil->Format, device.depth_stencil->Size, &description);
		if (description.format == D3DFMT_D16 || description.format == D3DFMT_LIN_D16 ||
			description.format == D3DFMT_F16 || description.format == D3DFMT_LIN_F16)
		{
			zscale = 65535.0f;
		}
	}
	(void)width; (void)height; (void)depth;
	device.viewport_scale[0] = device.viewport.Width * 0.5f;
	device.viewport_scale[1] = -(float)device.viewport.Height * 0.5f;
	device.viewport_scale[2] = zscale * (device.viewport.MaxZ - device.viewport.MinZ);
	device.viewport_scale[3] = 0.0f;
	device.viewport_offset[0] = device.viewport.X + device.viewport.Width * 0.5f;
	device.viewport_offset[1] = device.viewport.Y + device.viewport.Height * 0.5f;
	device.viewport_offset[2] = zscale * device.viewport.MinZ;
	device.viewport_offset[3] = 0.0f;
	if (!(device.shader_constant_mode & D3DSCM_NORESERVEDCONSTANTS))
	{
		constants_store(XGPU_VERTEX_CONSTANT_BIAS - 38, device.viewport_scale, 1);
		constants_store(XGPU_VERTEX_CONSTANT_BIAS - 37, device.viewport_offset, 1);
	}
	/* The view (c[-96]) is set with this viewport, once per split-screen
	window. Sending every constant on the next draw keeps the map, which is
	drawn after the objects and is the world itself, from keeping the other
	window's view when the two cameras do not agree. */
	constants_known = FALSE;
	facts_serial++;
}
HRESULT WINAPI Direct3D_CreateDevice(UINT adapter, D3DDEVTYPE device_type, void *unused, DWORD behavior_flags,
	D3DPRESENT_PARAMETERS *presentation_parameters, D3DDevice **returned_device)
{
	unsigned long width, height;
	int index;

	(void)adapter;
	(void)device_type;
	(void)unused;
	(void)behavior_flags;
	if (!device.created)
	{
		memset(&device, 0, sizeof(device));
		if (presentation_parameters)
			device.presentation = *presentation_parameters;
		width = device.presentation.BackBufferWidth ? device.presentation.BackBufferWidth : 640;
		height = device.presentation.BackBufferHeight ? device.presentation.BackBufferHeight : 480;
		d3d8_surface_initialize(&device.back_buffer, D3DFMT_LIN_A8R8G8B8, width, height);
		d3d8_surface_initialize(&device.depth_buffer, D3DFMT_LIN_D24S8, width, height);
		device.render_target = &device.back_buffer;
		device.depth_stencil = &device.depth_buffer;
		for (index = 0; index < D3DTS_MAX; index++)
		{
			device.transforms[index]._11 = 1.0f;
			device.transforms[index]._22 = 1.0f;
			device.transforms[index]._33 = 1.0f;
			device.transforms[index]._44 = 1.0f;
		}
		device.viewport.Width = width;
		device.viewport.Height = height;
		device.viewport.MaxZ = 1.0f;
		device.next_vertex_shader_id = 1;
		render_states_all = TRUE;
		memset(texture_states_dirty, 0xff, sizeof(texture_states_dirty));
		D3D__RenderState[D3DRS_ZENABLE] = TRUE;
		D3D__RenderState[D3DRS_ZWRITEENABLE] = TRUE;
		D3D__RenderState[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
		D3D__RenderState[D3DRS_COLORWRITEENABLE] = D3DCOLORWRITEENABLE_ALL;
		D3D__RenderState[D3DRS_SRCBLEND] = D3DBLEND_ONE;
		D3D__RenderState[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
		D3D__RenderState[D3DRS_BLENDOP] = D3DBLENDOP_ADD;
		D3D__RenderState[D3DRS_CULLMODE] = D3DCULL_CCW;
		D3D__RenderState[D3DRS_FRONTFACE] = D3DFRONT_CW;
		D3D__RenderState[D3DRS_FILLMODE] = D3DFILL_SOLID;
		D3D__RenderState[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
		D3D__RenderState[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
		D3D__RenderState[D3DRS_STENCILMASK] = 0xff;
		D3D__RenderState[D3DRS_STENCILWRITEMASK] = 0xff;
		D3D__RenderState[D3DRS_STENCILFAIL] = D3DSTENCILOP_KEEP;
		D3D__RenderState[D3DRS_STENCILZFAIL] = D3DSTENCILOP_KEEP;
		D3D__RenderState[D3DRS_STENCILPASS] = D3DSTENCILOP_KEEP;
		for (index = 0; index < D3DTSS_MAXSTAGES; index++)
		{
			D3D__TextureState[index][D3DTSS_ADDRESSU] = D3DTADDRESS_WRAP;
			D3D__TextureState[index][D3DTSS_ADDRESSV] = D3DTADDRESS_WRAP;
			D3D__TextureState[index][D3DTSS_ADDRESSW] = D3DTADDRESS_WRAP;
			D3D__TextureState[index][D3DTSS_MAGFILTER] = D3DTEXF_POINT;
			D3D__TextureState[index][D3DTSS_MINFILTER] = D3DTEXF_POINT;
			D3D__TextureState[index][D3DTSS_MAXANISOTROPY] = 1;
		}
		viewport_update_constants();
		/* what tells the texture cache a texture has been rewritten
		(xbox_textures_dk.c) */
		memory_watch_initialize();

		/* The window is the host's stand-in (port/switch/host/host.h): the
		platform layer's event loop runs only while it has one, and its swap
		holds frames to the display's rate until deko3d presents them */
		if (!platform_video_initialize(width, height))
			platform_log("Direct3D: no window; the game's events will not be read");
		platform_log("Direct3D: the GX2 renderer");
		device.created = TRUE;
	}
	*returned_device = device_pointer();
	return S_OK;
}

/* ---------- the menus' pointer */

/* the GamePad's touch screen (wiiu_input.c, in the pixels of its 854x480 picture) is the menus' pointer: a tap clicks, a drag scrolls (touch_menu.c, which
the Android build's touch screen shares). The GamePad shows the game's picture, letterboxed as blit_to (wiiu_gx2.c) does it. */
int wiiu_input_touch(float *x, float *y);

#define GAMEPAD_WIDTH 854.0f
#define GAMEPAD_HEIGHT 480.0f

/* Measured on the console (2026-10-22): the menus are drawn at the left of the picture, not centered in its 852 columns, although the game asks for the
offset (halo_screen_ui_offset(TRUE) sets 106 while it draws them: the targets debug.touch_targets shows sit where the menus are drawn). So a touch is
not moved by it here. When the GX2 renderer centers the menus, as d3d8_gl.c does, set this to 1. */
#define MENUS_DRAWN_CENTERED 1

/* a point of the GamePad's picture in the menus' coordinates (the 640x480 they are drawn in), as d3d8_gl.c's ui_point_from_window gives them */
static void menu_point_from_gamepad(float gamepad_x, float gamepad_y, short *x, short *y)
{
	const float game_width = (float)halo_screen_width(), game_height = (float)SCREEN_HEIGHT;
	float width = GAMEPAD_WIDTH, height = GAMEPAD_WIDTH * game_height / game_width;
	float left, top;

	if (height > GAMEPAD_HEIGHT)
	{
		height = GAMEPAD_HEIGHT;
		width = GAMEPAD_HEIGHT * game_width / game_height;
	}
	left = (GAMEPAD_WIDTH - width) / 2.0f;
	top = (GAMEPAD_HEIGHT - height) / 2.0f;
	*x = (short)floorf((gamepad_x - left) * game_width / width - (MENUS_DRAWN_CENTERED ? (game_width - 640.0f) / 2.0f : 0.0f));
	*y = (short)floorf((gamepad_y - top) * game_height / height);
}

int halo_ui_pointer_update(int menus_active, struct halo_ui_pointer *pointer)
{
	static struct touch_menu menu;
	static int ready, was_active, finger_down;
	static float last_x, last_y;
	struct touch_menu_output output;
	float x, y;
	int down;

	platform_menus_set_active(menus_active != 0);
	if (!ready)
	{
		struct touch_menu_settings settings;

		memset(&settings, 0, sizeof(settings));
		/* (pixels of the GamePad's screen: a tap moves at most 14, a notch of the wheel is 40) */
		settings.slop = 14.0f;
		settings.step = 40.0f;
		settings.steps_per_read = 3;
		touch_menu_init(&menu, &settings);
		ready = 1;
	}
	if ((menus_active != 0) != was_active)
	{
		/* what a finger began before a menu opened or closed does not act in the other */
		touch_menu_reset(&menu);
		finger_down = 0;
		was_active = menus_active != 0;
	}
	down = wiiu_input_touch(&x, &y);
	if (!menus_active)
		return 0;
	if (down && !finger_down)
		touch_menu_down(&menu, 1, x, y);
	else if (down)
		touch_menu_move(&menu, 1, x, y);
	else if (finger_down)
		touch_menu_up(&menu, 1, last_x, last_y);
	finger_down = down;
	last_x = x;
	last_y = y;
	touch_menu_read(&menu, &output);
	if (output.clicks)
	{
		short tap_x, tap_y;

		menu_point_from_gamepad(output.click_x, output.click_y, &tap_x, &tap_y);
		platform_log("touch: a tap at %.0f,%.0f, the menus' %d,%d", output.click_x, output.click_y, (int)tap_x, (int)tap_y);
	}
	memset(pointer, 0, sizeof(*pointer));
	menu_point_from_gamepad(output.x, output.y, &pointer->x, &pointer->y);
	menu_point_from_gamepad(output.click_x, output.click_y, &pointer->click_x, &pointer->click_y);
	pointer->moved = output.moved != 0;
	pointer->left_clicks = (unsigned char)(output.clicks < 255 ? output.clicks : 255);
	pointer->wheel_steps = (signed char)(output.wheel_steps < -8 ? -8 : output.wheel_steps > 8 ? 8 : output.wheel_steps);
	pointer->touch = 1;
	pointer->downs = (unsigned char)(output.downs < 255 ? output.downs : 255);
	menu_point_from_gamepad(output.down_x, output.down_y, &pointer->down_x, &pointer->down_y);
	return 1;
}

/* menu_files.c says when it deletes a menu picture's GL texture, for the
OpenGL renderer's bindings (d3d8_gl.c); this renderer keeps images of its
own and has none of those */
void xgpu_gl_texture_deleted(unsigned int texture)
{
	(void)texture;
}

/* the scoreboard's: none, as on Android's OpenGL ES (d3d8_gl.c); a vote to
kick starts from the console (votekick) */
int halo_scoreboard_pointer_update(int offered, struct halo_ui_pointer *pointer)
{
	(void)offered;
	(void)pointer;
	return -1;
}

/* takes up the display's shape and resolution, or the window's, if they
have changed; between frames, since the game's layout and the targets must
agree for a whole frame. Returns the width the game draws. */
/* (the Switch sets the CPU's clock here, performance.cpu_clock; the Wii U's is fixed) */
static void cpu_clock_update(void)
{
}

/* Takes up the lines the GX2 thread asks the next frames to be drawn at
(host_gx2_drs_lines), as the share of the screen's targets they draw into:
the lines over the targets' own, and the columns in the same proportion, in
whole pixels, so that the part drawn keeps the picture's shape and its edge
is a pixel's edge. */
static void drs_commit(void)
{
	const unsigned long pool_width = (unsigned long)(screen_width * screen_scale[0] + 0.5f);
	const unsigned long pool_height = (unsigned long)(SCREEN_HEIGHT * screen_scale[1] + 0.5f);
	const unsigned long lines = host_gx2_drs_lines();
	float fraction[2] = { 1.0f, 1.0f };

	if (lines && lines < pool_height && pool_width)
	{
		const unsigned long columns = (pool_width * lines + pool_height / 2) / pool_height;

		fraction[0] = (float)columns / (float)pool_width;
		fraction[1] = (float)lines / (float)pool_height;
	}
	/* (a new share changes the targets' scale, which the targets bound and a draw's facts are made from: both made again at the next draw) */
	if (fraction[0] != drs_fraction[0] || fraction[1] != drs_fraction[1])
	{
		drs_fraction[0] = fraction[0];
		drs_fraction[1] = fraction[1];
		targets_dirty = TRUE;
		facts_serial++;
	}
}

long halo_screen_commit(void)
{
	long width;
	float scale[2];

	cpu_clock_update();

	if (!screen_width)
		return halo_screen_width();
	screen_mode_choose(&width, scale);
	if (width != screen_width || scale[0] != screen_scale[0] || scale[1] != screen_scale[1])
	{
		platform_log("screen: %ldx%d drawn at %.0fx%.0f", width, SCREEN_HEIGHT,
			width * scale[0], SCREEN_HEIGHT * scale[1]);
		screen_width = width;
		screen_scale[0] = scale[0];
		screen_scale[1] = scale[1];
		targets_dirty = TRUE;
#ifndef HALO_ANDROID
		if (device.created)
		{
			device.presentation.BackBufferWidth = (UINT)width;
			d3d8_surface_resize(&device.back_buffer, D3DFMT_LIN_A8R8G8B8, (unsigned long)width, SCREEN_HEIGHT);
			d3d8_surface_resize(&device.depth_buffer, D3DFMT_LIN_D24S8, (unsigned long)width, SCREEN_HEIGHT);
		}
#endif
	}
	/* (after the screen's size and scale, which the share is of) */
	drs_commit();
	return screen_width;
}
ULONG WINAPI D3DDevice_Release(void)
{
	return 1;
}

void WINAPI D3DDevice_GetDeviceCaps(D3DCAPS8 *caps)
{
	memset(caps, 0, sizeof(*caps));
	caps->DeviceType = D3DDEVTYPE_HAL;
	caps->MaxTextureWidth = 4096;
	caps->MaxTextureHeight = 4096;
	caps->MaxVolumeExtent = 512;
	caps->MaxTextureRepeat = 8192;
	caps->MaxTextureAspectRatio = 4096;
	caps->MaxAnisotropy = 4;
	caps->MaxTextureBlendStages = 4;
	caps->MaxSimultaneousTextures = 4;
	caps->MaxActiveLights = 8;
	caps->MaxVertexBlendMatrices = 4;
	caps->MaxPointSize = 64.0f;
	caps->MaxPrimitiveCount = 0xfffff;
	caps->MaxVertexIndex = 0xffff;
	caps->MaxStreams = 16;
	caps->MaxStreamStride = 255;
	caps->VertexShaderVersion = D3DVS_VERSION(1, 1);
	caps->MaxVertexShaderConst = 192;
	caps->PixelShaderVersion = D3DPS_VERSION(1, 1);
	caps->MaxPixelShaderValue = 1.0f;
}

void WINAPI D3DDevice_GetBackBuffer(INT back_buffer, D3DBACKBUFFER_TYPE type, D3DSurface **result)
{
	(void)back_buffer;
	(void)type;
	/* like Direct3D, the caller gets a reference it must release */
	device.back_buffer.Common++;
	*result = &device.back_buffer;
}

HRESULT WINAPI D3DDevice_GetDepthStencilSurface(D3DSurface **result)
{
	*result = device.depth_stencil;
	if (!*result)
		return D3DERR_NOTFOUND;
	(*result)->Common++;
	return S_OK;
}

void WINAPI D3DDevice_SetRenderTarget(D3DSurface *render_target, D3DSurface *depth_stencil)
{
	if (render_target)
		device.render_target = render_target;
	device.depth_stencil = depth_stencil;
	targets_dirty = TRUE;
	resources_serial++;
	/* like Direct3D, reset the viewport to the whole new target */
	if (device.render_target)
	{
		unsigned long width, height;
		BOOL depth;

		surface_dimensions(device.render_target, &width, &height, &depth);
		device.viewport.X = 0;
		device.viewport.Y = 0;
		device.viewport.Width = width;
		device.viewport.Height = height;
		device.viewport.MinZ = 0.0f;
		device.viewport.MaxZ = 1.0f;
	}
	viewport_update_constants();
}

void WINAPI D3DDevice_SetViewport(CONST D3DVIEWPORT8 *viewport)
{
	device.viewport = *viewport;
	viewport_update_constants();
}

void WINAPI D3DDevice_SetTransform(D3DTRANSFORMSTATETYPE state, CONST D3DMATRIX *matrix)
{
	if ((unsigned long)state < D3DTS_MAX)
		device.transforms[state] = *matrix;
}

void WINAPI D3DDevice_GetTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix)
{
	if ((unsigned long)state < D3DTS_MAX)
		*matrix = device.transforms[state];
}

void WINAPI D3DDevice_SetFlickerFilter(DWORD filter) { (void)filter; }
void WINAPI D3DDevice_SetSoftDisplayFilter(BOOL enable) { (void)enable; }

void WINAPI D3DDevice_SetShaderConstantMode(D3DSHADERCONSTANTMODE mode)
{
	device.shader_constant_mode = mode;
	viewport_update_constants();
}
/* ---------- GPU synchronisation (DEKO3D.md, phase 3)

Draws read the game's memory where the game wrote it, after the draw, so a
resource is busy until the GPU is past the last submission that read it. As
the Xbox's runtime did, the resource's Lock field records that submission
(resource_used); the game sets it to 0, not busy, whenever it makes a
resource's header - but not for the headers a map file holds, which come
with what the file has there (halo_resource_busy). d3d8_resources.c asks halo_resource_busy and
halo_resource_wait for IsBusy, BlockUntilNotBusy and its locks; their
definitions there are empty for the OpenGL image, whose mirror copied a
draw's data at the draw. */

/* a draw or clear in the submission being written uses the resource; called
after the command is written (writing one can hand the stream over) */
static void resource_used(void *resource)
{
	if (resource)
		((D3DResource *)resource)->Lock = submission;
}

/* the resources the current state reads or writes: the bound render
targets, and for a draw its textures, palettes, vertex streams and index
buffer */
static void targets_used(void)
{
	resource_used(device.render_target);
	resource_used(device.depth_stencil);
}

/* what draw_resources_used stamped last and in which submission: a draw with the same bindings in the same submission has nothing to stamp, each of its
resources' Lock holding the submission already (the stamps were up to nine stores a draw into the resources' headers, which are the game's, in lines
the draw does not otherwise touch). indexed, streamed: whether the index buffer and the streams' buffers were stamped too */
static struct
{
	unsigned long serial, submission;
	const struct vertex_shader_object *declaration;
	DWORD texture_modes;
	BOOL indexed, streamed;
} resources_stamped;

static void draw_resources_used(BOOL indexed, BOOL streamed)
{
	unsigned long index;
	int stage;

	if (resources_stamped.serial == resources_serial && resources_stamped.submission == submission && resources_stamped.declaration == device.vertex_shader &&
		resources_stamped.texture_modes == D3D__RenderState[D3DRS_PSTEXTUREMODES])
	{
		if ((resources_stamped.indexed || !indexed) && (resources_stamped.streamed || !streamed))
			return;
		indexed = indexed || resources_stamped.indexed;
		streamed = streamed || resources_stamped.streamed;
	}
	resources_stamped.serial = resources_serial;
	resources_stamped.submission = submission;
	resources_stamped.declaration = device.vertex_shader;
	resources_stamped.texture_modes = D3D__RenderState[D3DRS_PSTEXTUREMODES];
	resources_stamped.indexed = indexed;
	resources_stamped.streamed = streamed;
	targets_used();
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		if (!((D3D__RenderState[D3DRS_PSTEXTUREMODES] >> (5 * stage)) & 0x1f))
			continue;
		resource_used(device.textures[stage]);
		resource_used(device.palettes[stage]);
	}
	/* (immediate mode reads no buffer) */
	for (index = 0; streamed && device.vertex_shader && index < device.vertex_shader->element_count; index++)
		resource_used(device.streams[device.vertex_shader->elements[index].stream].buffer);
	if (indexed)
		resource_used(device.index_buffer);
}

BOOL halo_resource_busy(D3DResource *resource)
{
	unsigned long last = resource ? resource->Lock : 0;

	if (!last)
		return FALSE;
	/* past the submission being written: not a number this device wrote. A
	resource whose header is in a map file comes with whatever the file has
	there (a level's first lock waited for submission 153684298, 410 being
	written, and never came back), so it is not busy, and is now 0 */
	if (last > submission)
	{
		resource->Lock = 0;
		return FALSE;
	}
	/* used in the submission still being written: handed over now, as the
	Xbox's runtime kicks off its push buffer, or a caller spinning on IsBusy
	would wait for a submission that never comes */
	if (last == submission)
		flush_because(5);
	return host_gx2_retired() < last;
}

void halo_resource_wait(D3DResource *resource)
{
	unsigned long waits = 0;

	while (halo_resource_busy(resource))
	{
		/* (said once a wait, at two seconds: a lock that never comes back is
		otherwise a silent stop) */
		if (++waits == 4000)
			platform_log("deko3d: a lock of %p has waited two seconds for submission %lu (%u retired, %lu being "
				"written)", (void *)resource, (unsigned long)resource->Lock, host_gx2_retired(), submission);
		usleep(500);
	}
}

BOOL WINAPI D3DDevice_IsBusy(void)
{
	flush_because(6);
	return host_gx2_retired() < submission - 1;
}

void WINAPI D3DDevice_KickPushBuffer(void)
{
	flush_because(7);
}

void WINAPI D3DDevice_InsertCallback(D3DCALLBACKTYPE type, D3DCALLBACK callback, DWORD context)
{
	(void)type;
	/* the "GPU" reaches the callback immediately */
	if (callback)
		callback(context);
}

/* ---------- visibility (occlusion) tests: the GPU counts the pixels the
draws between a begin and an end pass, and writes the count to the test's
slot when it gets there (DK_COMMAND_VISIBILITY_*). A result is the slot's
latest count - while the GPU is behind, an earlier test's - as d3d8_gl.c's
query buffer gives: the game asks at the start of the next frame, and
waiting there would stop the CPU until the GPU caught up. A count is in
the host's pixels, made the game's (visibility_area): the screen's targets
can be drawn bigger (display.resolution_limit). */

static BOOL visibility_pending[DK_VISIBILITY_SLOTS];
/* the host's pixels to a game pixel where each test drew: its count is
divided by it, the game dividing by its own test's area (lens flares) */
static float visibility_area[DK_VISIBILITY_SLOTS];

void WINAPI D3DDevice_BeginVisibilityTest(void)
{
	if (device.visibility_test_active)
		return;
	device.visibility_test_active = TRUE;
	stream_command(DK_COMMAND_VISIBILITY_BEGIN, sizeof(struct dk_command_header));
}

HRESULT WINAPI D3DDevice_EndVisibilityTest(DWORD index)
{
	struct dk_command_visibility_end *command;

	if (!device.visibility_test_active)
		return S_OK;
	device.visibility_test_active = FALSE;
	index %= DK_VISIBILITY_SLOTS;
	if (!index)
		index = 1;
	command = stream_command(DK_COMMAND_VISIBILITY_END, sizeof(*command));
	command->index = (uint32_t)index;
	visibility_pending[index] = TRUE;
	visibility_area[index] = target_scale[0] * target_scale[1];
	return S_OK;
}

HRESULT WINAPI D3DDevice_GetVisibilityTestResult(DWORD index, UINT *result, ULONGLONG *time_stamp)
{
	if (time_stamp)
		*time_stamp = 0;
	index %= DK_VISIBILITY_SLOTS;
	if (!index)
		index = 1;
	if (result)
	{
		/* (with the queries off, as by default, the answer is none without asking the host: asking is a job of the GX2 thread's, after it has run every stream
		before it - thousands a second with the lens flares, each a wait for the queue to empty) */
		unsigned int count = visibility_pending[index] && host_gx2_visibility_enabled() ? host_gx2_visibility((unsigned int)index) : 0;

		/* (in the game's pixels) */
		*result = visibility_area[index] > 1.0f ? (UINT)(count / visibility_area[index] + 0.5f) : count;
	}
	return S_OK;
}

/* ---------- render and texture stage state */

/* the simple render states by their push buffer method (D3DSIMPLERENDERSTATEENCODE): the state + 1 at (method - 0x40000) / 4, 0 for none. The 82
methods are 0x40260 to 0x41e78, each a state's own */
#define SIMPLE_METHOD_BASE 0x40000UL
#define SIMPLE_METHOD_SLOTS 2048
static unsigned char simple_states[SIMPLE_METHOD_SLOTS];
static BOOL simple_states_made;

static void simple_states_make(void)
{
	unsigned long state;

	for (state = 0; state < D3DRS_SIMPLE_MAX; state++)
	{
		const unsigned long slot = (D3DSIMPLERENDERSTATEENCODE[state] - SIMPLE_METHOD_BASE) / 4;

		/* (a method two states shared would be known by neither: every state compared) */
		if (slot < SIMPLE_METHOD_SLOTS)
			simple_states[slot] = simple_states[slot] ? 0xff : (unsigned char)(state + 1);
	}
	simple_states_made = TRUE;
}

void D3DFASTCALL D3DDevice_SetRenderState_Simple(DWORD method, DWORD value)
{
	/* callers also store the value in D3D__RenderState themselves: the state is marked by its method, which names it */
	const unsigned long slot = (method - SIMPLE_METHOD_BASE) / 4;
	unsigned long state;

	if (!simple_states_made)
		simple_states_make();
	state = slot < SIMPLE_METHOD_SLOTS ? simple_states[slot] : 0;
	if (state && state <= D3DRS_SIMPLE_MAX && D3DSIMPLERENDERSTATEENCODE[state - 1] == method)
	{
		/* Simple's caller normally writes value after this call. Retain a
		mark for raw callers whose value and table word disagree too. */
		if (!states_told_valid || render_states_told[state - 1] != value || D3D__RenderState[state - 1] != value)
			render_state_mark(state - 1);
	}
	else
		render_states_all = TRUE;
}

void D3DFASTCALL D3DDevice_SetRenderState_Deferred(D3DRENDERSTATETYPE state, DWORD value)
{
	render_state_store((unsigned long)state, value);
}

void WINAPI D3DDevice_SetRenderState_ZBias(DWORD value);

void WINAPI D3DDevice_SetRenderStateNotInline(D3DRENDERSTATETYPE state, DWORD value)
{
	if (state == D3DRS_ZBIAS)
		D3DDevice_SetRenderState_ZBias(value);
	else
		render_state_store((unsigned long)state, value);
}

/* As the Xbox's D3D8 does it: a z bias is a polygon offset of -bias depth
units plus -bias/4 times the polygon's depth slope, enabled for every fill
mode. Without the slope term, decals (biased by 8) fight with the surface
under them wherever it is seen at an angle. */
void WINAPI D3DDevice_SetRenderState_ZBias(DWORD value)
{
	float offset = -(float)value;
	float slope = offset * 0.25f;
	DWORD enable = value != 0;

	render_states_mark(D3DRS_POLYGONOFFSETZSLOPESCALE, D3DRS_SOLIDOFFSETENABLE);
	render_state_mark(D3DRS_ZBIAS);
	memcpy(&D3D__RenderState[D3DRS_POLYGONOFFSETZSLOPESCALE], &slope, sizeof(slope));
	memcpy(&D3D__RenderState[D3DRS_POLYGONOFFSETZOFFSET], &offset, sizeof(offset));
	D3D__RenderState[D3DRS_POINTOFFSETENABLE] = enable;
	D3D__RenderState[D3DRS_WIREFRAMEOFFSETENABLE] = enable;
	D3D__RenderState[D3DRS_SOLIDOFFSETENABLE] = enable;
	D3D__RenderState[D3DRS_ZBIAS] = value;
}

#define COMPLEX_RENDER_STATE(name, state) \
	void WINAPI D3DDevice_SetRenderState_##name(DWORD value) { render_state_store(state, value); }

COMPLEX_RENDER_STATE(PSTextureModes, D3DRS_PSTEXTUREMODES)
COMPLEX_RENDER_STATE(VertexBlend, D3DRS_VERTEXBLEND)
COMPLEX_RENDER_STATE(FogColor, D3DRS_FOGCOLOR)
COMPLEX_RENDER_STATE(FillMode, D3DRS_FILLMODE)
COMPLEX_RENDER_STATE(BackFillMode, D3DRS_BACKFILLMODE)
COMPLEX_RENDER_STATE(TwoSidedLighting, D3DRS_TWOSIDEDLIGHTING)
COMPLEX_RENDER_STATE(NormalizeNormals, D3DRS_NORMALIZENORMALS)
COMPLEX_RENDER_STATE(ZEnable, D3DRS_ZENABLE)
COMPLEX_RENDER_STATE(StencilEnable, D3DRS_STENCILENABLE)
COMPLEX_RENDER_STATE(StencilFail, D3DRS_STENCILFAIL)
COMPLEX_RENDER_STATE(FrontFace, D3DRS_FRONTFACE)
COMPLEX_RENDER_STATE(CullMode, D3DRS_CULLMODE)
COMPLEX_RENDER_STATE(TextureFactor, D3DRS_TEXTUREFACTOR)
COMPLEX_RENDER_STATE(LogicOp, D3DRS_LOGICOP)
COMPLEX_RENDER_STATE(EdgeAntiAlias, D3DRS_EDGEANTIALIAS)
COMPLEX_RENDER_STATE(MultiSampleAntiAlias, D3DRS_MULTISAMPLEANTIALIAS)
COMPLEX_RENDER_STATE(MultiSampleMask, D3DRS_MULTISAMPLEMASK)
COMPLEX_RENDER_STATE(MultiSampleType, D3DRS_MULTISAMPLETYPE)
COMPLEX_RENDER_STATE(ShadowFunc, D3DRS_SHADOWFUNC)
COMPLEX_RENDER_STATE(LineWidth, D3DRS_LINEWIDTH)
COMPLEX_RENDER_STATE(Dxt1NoiseEnable, D3DRS_DXT1NOISEENABLE)
COMPLEX_RENDER_STATE(YuvEnable, D3DRS_YUVENABLE)
COMPLEX_RENDER_STATE(OcclusionCullEnable, D3DRS_OCCLUSIONCULLENABLE)
COMPLEX_RENDER_STATE(StencilCullEnable, D3DRS_STENCILCULLENABLE)
COMPLEX_RENDER_STATE(RopZCmpAlwaysRead, D3DRS_ROPZCMPALWAYSREAD)
COMPLEX_RENDER_STATE(RopZRead, D3DRS_ROPZREAD)
COMPLEX_RENDER_STATE(DoNotCullUncompressed, D3DRS_DONOTCULLUNCOMPRESSED)

void D3DFASTCALL D3DDevice_SetTextureState_Deferred(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value)
{
	texture_state_store(stage, (unsigned long)type, value);
}

void WINAPI D3DDevice_SetTextureState_TexCoordIndex(DWORD stage, DWORD value)
{
	texture_state_store(stage, D3DTSS_TEXCOORDINDEX, value);
}

void WINAPI D3DDevice_SetTextureState_BorderColor(DWORD stage, DWORD value)
{
	texture_state_store(stage, D3DTSS_BORDERCOLOR, value);
}

void WINAPI D3DDevice_SetTextureState_ColorKeyColor(DWORD stage, DWORD value)
{
	texture_state_store(stage, D3DTSS_COLORKEYCOLOR, value);
}

void WINAPI D3DDevice_SetTextureState_BumpEnv(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value)
{
	texture_state_store(stage, (unsigned long)type, value);
}

void WINAPI D3DDevice_SetTexture(DWORD stage, D3DBaseTexture *texture)
{
	if (stage < D3DTSS_MAXSTAGES)
		device.textures[stage] = texture;
	resources_serial++;
}

void WINAPI D3DDevice_SetPalette(DWORD stage, D3DPalette *palette)
{
	if (stage < D3DTSS_MAXSTAGES)
		device.palettes[stage] = palette;
	resources_serial++;
}

void WINAPI D3DDevice_SetPixelShaderProgram(D3DPIXELSHADERDEF *definition)
{
	/* the definition's members are the pixel shader render states */
	if (!definition)
		return;
	/* (every pixel shader state below D3DRS_PS_MAX, and the texture modes) */
	render_states_mark(D3DRS_PS_MIN, D3DRS_PS_MAX - 1);
	render_state_mark(D3DRS_PSTEXTUREMODES);
	memcpy(&D3D__RenderState[D3DRS_PSALPHAINPUTS0], definition->PSAlphaInputs, sizeof(definition->PSAlphaInputs));
	D3D__RenderState[D3DRS_PSFINALCOMBINERINPUTSABCD] = definition->PSFinalCombinerInputsABCD;
	D3D__RenderState[D3DRS_PSFINALCOMBINERINPUTSEFG] = definition->PSFinalCombinerInputsEFG;
	memcpy(&D3D__RenderState[D3DRS_PSCONSTANT0_0], definition->PSConstant0, sizeof(definition->PSConstant0));
	memcpy(&D3D__RenderState[D3DRS_PSCONSTANT1_0], definition->PSConstant1, sizeof(definition->PSConstant1));
	memcpy(&D3D__RenderState[D3DRS_PSALPHAOUTPUTS0], definition->PSAlphaOutputs, sizeof(definition->PSAlphaOutputs));
	memcpy(&D3D__RenderState[D3DRS_PSRGBINPUTS0], definition->PSRGBInputs, sizeof(definition->PSRGBInputs));
	D3D__RenderState[D3DRS_PSCOMPAREMODE] = definition->PSCompareMode;
	D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT0] = definition->PSFinalCombinerConstant0;
	D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT1] = definition->PSFinalCombinerConstant1;
	memcpy(&D3D__RenderState[D3DRS_PSRGBOUTPUTS0], definition->PSRGBOutputs, sizeof(definition->PSRGBOutputs));
	D3D__RenderState[D3DRS_PSCOMBINERCOUNT] = definition->PSCombinerCount;
	D3D__RenderState[D3DRS_PSTEXTUREMODES] = definition->PSTextureModes;
	D3D__RenderState[D3DRS_PSDOTMAPPING] = definition->PSDotMapping;
	D3D__RenderState[D3DRS_PSINPUTTEXTURE] = definition->PSInputTexture;
}
/* ---------- vertex shaders */

static unsigned long vertex_type_bytes(unsigned long type)
{
	switch (type)
	{
	case D3DVSDT_FLOAT1: return 4;
	case D3DVSDT_FLOAT2: return 8;
	case D3DVSDT_FLOAT3: return 12;
	case D3DVSDT_FLOAT4: return 16;
	case D3DVSDT_D3DCOLOR: return 4;
	case D3DVSDT_SHORT1: return 2;
	case D3DVSDT_SHORT2: return 4;
	case D3DVSDT_SHORT3: return 6;
	case D3DVSDT_SHORT4: return 8;
	case D3DVSDT_NORMSHORT1: return 2;
	case D3DVSDT_NORMSHORT2: return 4;
	case D3DVSDT_NORMSHORT3: return 6;
	case D3DVSDT_NORMSHORT4: return 8;
	case D3DVSDT_NORMPACKED3: return 4;
	case D3DVSDT_PBYTE1: return 1;
	case D3DVSDT_PBYTE2: return 2;
	case D3DVSDT_PBYTE3: return 3;
	case D3DVSDT_PBYTE4: return 4;
	case D3DVSDT_FLOAT2H: return 12;
	default: return 0;
	}
}

static void parse_declaration(struct vertex_shader_object *object, const DWORD *declaration)
{
	unsigned long stream = 0;
	unsigned long offsets[16] = { 0 };

	for (; declaration && *declaration != D3DVSD_END(); declaration++)
	{
		DWORD token = *declaration;
		unsigned long token_type = (token & D3DVSD_TOKENTYPEMASK) >> D3DVSD_TOKENTYPESHIFT;

		switch (token_type)
		{
		case D3DVSD_TOKEN_STREAM:
			stream = token & D3DVSD_STREAMNUMBERMASK;
			break;
		case D3DVSD_TOKEN_STREAMDATA:
			if (token & D3DVSD_DATALOADTYPEMASK)
			{
				/* skip: the count is in dwords, or in bytes with bit 27 */
				unsigned long count = (token & D3DVSD_SKIPCOUNTMASK) >> D3DVSD_SKIPCOUNTSHIFT;

				offsets[stream] += (token & 0x08000000) ? count : count * 4;
			}
			else if (object->element_count < XGPU_VERTEX_ATTRIBUTE_COUNT)
			{
				struct vertex_element *element = &object->elements[object->element_count++];

				element->reg = (unsigned char)(token & D3DVSD_VERTEXREGMASK);
				element->stream = (unsigned char)stream;
				element->type = (unsigned char)((token & D3DVSD_DATATYPEMASK) >> D3DVSD_DATATYPESHIFT);
				element->bytes = (unsigned char)vertex_type_bytes(element->type);
				element->offset = (unsigned short)offsets[stream];
				offsets[stream] += element->bytes;
				if (element->type == D3DVSDT_NORMPACKED3)
					object->packed_mask |= 1UL << element->reg;
			}
			break;
		case D3DVSD_TOKEN_CONSTMEM:
			declaration += ((token & D3DVSD_CONSTCOUNTMASK) >> D3DVSD_CONSTCOUNTSHIFT) * 4;
			break;
		case D3DVSD_TOKEN_EXT:
			declaration += (token & D3DVSD_EXTCOUNTMASK) >> D3DVSD_EXTCOUNTSHIFT;
			break;
		default:
			break;
		}
	}
}
static void vertex_shader_note(struct vertex_shader_object *object);

HRESULT WINAPI D3DDevice_CreateVertexShader(CONST DWORD *declaration, CONST DWORD *function, DWORD *handle, DWORD usage)
{
	struct vertex_shader_object *object = calloc(1, sizeof(*object));

	(void)usage;
	if (!object)
		return E_OUTOFMEMORY;
	object->signature = VERTEX_SHADER_SIGNATURE;
	object->id = device.next_vertex_shader_id++;
	if (function)
	{
		/* header: program type in the low word, instruction count in the high */
		object->instruction_count = function[0] >> 16;
		object->instructions = malloc(object->instruction_count * 4 * sizeof(DWORD));
		memcpy(object->instructions, function + 1, object->instruction_count * 4 * sizeof(DWORD));
		/* the key the shader cache knows this program by, computed once,
	here, while the words are at hand */
		object->program_hash = shader_vertex_program_hash((const uint32_t *)object->instructions, object->instruction_count);
		dk_shader_program_note(object->program_hash, (const uint32_t *)object->instructions, object->instruction_count);
	}
	parse_declaration(object, declaration);
	vertex_shader_note(object);
	/* odd values are FVF codes; programmable shader handles are even */
	*handle = (DWORD)object;
	return S_OK;
}

static struct vertex_shader_object *vertex_shader_from_handle(DWORD handle)
{
	struct vertex_shader_object *object = (struct vertex_shader_object *)handle;

	if (!handle || (handle & 1) || object->signature != VERTEX_SHADER_SIGNATURE)
		return NULL;
	return object;
}

void WINAPI D3DDevice_DeleteVertexShader(DWORD handle)
{
	/* programs stay cached; the object is small */
	(void)handle;
}

void WINAPI D3DDevice_SetVertexShader(DWORD handle)
{
	struct vertex_shader_object *object = vertex_shader_from_handle(handle);

	if (object)
	{
		device.vertex_shader = object;
		device.program_address = 0;
		device.program_slots[0] = object;
	}
}

void WINAPI D3DDevice_LoadVertexShader(DWORD handle, DWORD address)
{
	if (address < VERTEX_PROGRAM_SLOTS)
		device.program_slots[address] = vertex_shader_from_handle(handle);
}

void WINAPI D3DDevice_SelectVertexShader(DWORD handle, DWORD address)
{
	struct vertex_shader_object *object = vertex_shader_from_handle(handle);

	if (object)
		device.vertex_shader = object;
	if (address < VERTEX_PROGRAM_SLOTS)
		device.program_address = address;
}

void WINAPI D3DDevice_GetVertexShaderSize(DWORD handle, UINT *size)
{
	struct vertex_shader_object *object = vertex_shader_from_handle(handle);

	*size = object ? object->instruction_count : 0;
}

void WINAPI D3DDevice_SetVertexShaderConstant(INT reg, CONST void *constant_data, DWORD constant_count)
{
	long first = reg + XGPU_VERTEX_CONSTANT_BIAS;

	if (first < 0 || first >= XGPU_VERTEX_CONSTANT_COUNT)
		return;
	if (first + (long)constant_count > XGPU_VERTEX_CONSTANT_COUNT)
		constant_count = XGPU_VERTEX_CONSTANT_COUNT - first;
	constants_store((unsigned long)first, constant_data, constant_count);
}

/* the program that runs: the one loaded at the selected address, else the
current shader's own */
static struct vertex_shader_object *current_program(void)
{
	struct vertex_shader_object *program = device.program_slots[device.program_address];

	return program ? program : device.vertex_shader;
}

/* the game finished loading a map (scenario_load) */
void d3d8_gl_map_loaded(const char *name)
{
	/* d3d8_gl.c's program_record_map_hash: the map's name hashed, so the
	keys met on it can be raised in the background compile's queue */
	uint32_t hash = 2166136261UL;

	for (; *name; name++)
		hash = (hash ^ (unsigned char)*name) * 16777619UL;
	dk_shader_map_loaded(hash ? hash : 1);
}

/* ---------- the shader dump (debug.gpu_dump_shaders; DEKO3D.md, phase 4)

The corpus the GLSL generators are proved with is the game's own: the
vertex programs this device holds (the game creates them from its table at
startup), and the pixel shader keys the OpenGL image recorded in
shader_programs.bin (d3d8_gl.c's program records), as GLSL from the deko3d
generators (nv2a_vsh_dk.c, nv2a_psh_dk.c), which UAM then compiles on the PC
and on the console. Written once, a second into the game
(D3DDevice_Present). */

/* the vertex shader objects by their id (d3d8_gl.c's list, kept for its
program records: the records name the programs by id) */
static struct vertex_shader_object **vertex_shaders_by_id;
static unsigned long vertex_shaders_by_id_count;

static void vertex_shader_note(struct vertex_shader_object *object)
{
	if (object->id >= vertex_shaders_by_id_count)
	{
		unsigned long count = object->id + 64;
		struct vertex_shader_object **grown = realloc(vertex_shaders_by_id, count * sizeof(*grown));

		if (!grown)
			return;
		memset(grown + vertex_shaders_by_id_count, 0, (count - vertex_shaders_by_id_count) * sizeof(*grown));
		vertex_shaders_by_id = grown;
		vertex_shaders_by_id_count = count;
	}
	vertex_shaders_by_id[object->id] = object;
}

/* ---------- for dk_shaders.c (the shader cache's guest half): the vertex
programs the game has made, which its keys name by hash and the OpenGL
records name by id. Programs stay cached (DeleteVertexShader keeps them),
so an id keeps naming the same program for the program's life */

unsigned long d3d8_dk_vertex_shader_count(void)
{
	return vertex_shaders_by_id_count;
}

int d3d8_dk_vertex_program_by_id(unsigned long id, uint64_t *program_hash, const uint32_t **instructions,
	unsigned long *instruction_count)
{
	struct vertex_shader_object *object = id < vertex_shaders_by_id_count ? vertex_shaders_by_id[id] : NULL;

	if (!object || !object->instructions)
		return 0;
	if (program_hash)
		*program_hash = object->program_hash;
	if (instructions)
		*instructions = (const uint32_t *)object->instructions;
	if (instruction_count)
		*instruction_count = object->instruction_count;
	return 1;
}


/* d3d8_gl.c's program records, whose keys the dump's pixel shaders are */
#define PROGRAM_RECORD_MAGIC 0x31435350UL /* "PSC1" */
#define PROGRAM_RECORD_PATH "z:\\shader_programs.bin"

struct program_record
{
	DWORD map_hash;
	DWORD vertex_id;
	DWORD variant;
	DWORD packed_mask;
	struct nv2a_pixel_shader_key key;
};

/* d3d8_gl.c's hash, which names the pixel files (size is a multiple of 4) */
static unsigned long hash_words(const void *data, unsigned long size)
{
	const DWORD *words = data;
	unsigned long hash = 2166136261UL;

	for (size /= 4; size; size--)
		hash = (hash ^ *words++) * 16777619UL;
	return hash;
}

/* the records from shader_programs.bin, or as many as memory holds; a file
from a build whose key differs in size is no use (the key struct is shared,
so a record is the same size here - checked anyway) */
static struct program_record *program_records_load(unsigned long *count_out)
{
	char path[512];
	FILE *file;
	DWORD header[2];
	struct program_record *records = NULL;
	unsigned long count = 0, capacity = 0;
	struct program_record record;

	*count_out = 0;
	platform_translate_path(PROGRAM_RECORD_PATH, path, sizeof(path));
	if ((file = fopen(path, "rb")) == NULL)
		return NULL;
	if (fread(header, sizeof(header), 1, file) == 1 && header[0] == PROGRAM_RECORD_MAGIC &&
		header[1] == sizeof(record))
	{
		while (fread(&record, sizeof(record), 1, file) == 1)
		{
			if (count == capacity)
			{
				unsigned long grown_capacity = capacity ? capacity * 2 : 256;
				struct program_record *grown = realloc(records, grown_capacity * sizeof(*grown));

				if (!grown)
					break;
				records = grown;
				capacity = grown_capacity;
			}
			records[count++] = record;
		}
	}
	else
	{
		platform_log("shader dump: %s is not a program records file of this build; its keys are not dumped", path);
	}
	fclose(file);
	*count_out = count;
	return records;
}

static BOOL shaders_dumped;

static void shaders_dump(void)
{
	const char *folder = config_string("debug.gpu_dump_shaders");
	char path[512], line[256];
	FILE *manifest;
	struct program_record *records;
	unsigned long record_count, index;
	struct nv2a_pixel_shader_key *keys = NULL;
	unsigned long key_count = 0, key_capacity = 0, vertex_files = 0, pixel_files = 0;
	unsigned long id;

	if (!*folder)
		return;
	records = program_records_load(&record_count);
	/* (exists already on every run but the first) */
	posix_make_directory(folder);
	snprintf(path, sizeof(path), "%s/manifest.txt", folder);
	manifest = fopen(path, "w");
	if (!manifest)
	{
		platform_log("shader dump: cannot write in %s", folder);
		free(records);
		return;
	}

	/* a vertex file per object and packed mask: mask 0 (the immediate-mode
	variant), and every mask the records show the program drawn with */
	for (id = 1; id < vertex_shaders_by_id_count; id++)
	{
		struct vertex_shader_object *object = vertex_shaders_by_id[id];
		unsigned long masks[16];
		unsigned long mask_count = 0, mask_index;

		if (!object || !object->instructions)
			continue;
		masks[mask_count++] = 0;
		for (index = 0; index < record_count; index++)
		{
			unsigned long mask;

			if (records[index].vertex_id != id)
				continue;
			mask = records[index].packed_mask;
			for (mask_index = 0; mask_index < mask_count; mask_index++)
			{
				if (masks[mask_index] == mask)
					break;
			}
			if (mask_index == mask_count)
			{
				if (mask_count < sizeof(masks) / sizeof(masks[0]))
					masks[mask_count++] = mask;
				else
					platform_log("shader dump: vertex shader %lu has more than %lu packed masks; the rest are not dumped",
						id, (unsigned long)(sizeof(masks) / sizeof(masks[0])));
			}
		}
		for (mask_index = 0; mask_index < mask_count; mask_index++)
		{
			char *source = nv2a_dk_vertex_shader_to_glsl((const uint32_t *)object->instructions,
				object->instruction_count, masks[mask_index]);
			FILE *file;

			snprintf(path, sizeof(path), "%s/vs_%lu_%08lx.vert", folder, id, masks[mask_index]);
			if ((file = fopen(path, "w")) != NULL)
			{
				fputs(source, file);
				fclose(file);
				snprintf(line, sizeof(line), "vs_%lu_%08lx.vert: vertex shader %lu (%lu instructions), packed mask 0x%08lx%s\n",
					id, masks[mask_index], id, object->instruction_count, masks[mask_index],
					masks[mask_index] ? "" : " (the immediate-mode variant)");
				fputs(line, manifest);
				vertex_files++;
			}
			free(source);
		}
	}

	/* a fragment file per distinct key, named by d3d8_gl.c's hash of it */
	for (index = 0; index < record_count; index++)
	{
		struct nv2a_pixel_shader_key *key = &records[index].key;
		unsigned long hash, found;
		char *source;
		FILE *file;

		for (found = 0; found < key_count; found++)
		{
			if (!memcmp(&keys[found], key, sizeof(*key)))
				break;
		}
		if (found < key_count)
			continue;
		if (key_count == key_capacity)
		{
			unsigned long grown_capacity = key_capacity ? key_capacity * 2 : 256;
			struct nv2a_pixel_shader_key *grown = realloc(keys, grown_capacity * sizeof(*grown));

			if (!grown)
				break;
			keys = grown;
			key_capacity = grown_capacity;
		}
		keys[key_count++] = *key;
		hash = hash_words(key, sizeof(*key));
		source = nv2a_dk_pixel_shader_to_glsl(key);
		snprintf(path, sizeof(path), "%s/ps_%08lx.frag", folder, hash);
		if ((file = fopen(path, "w")) != NULL)
		{
			fputs(source, file);
			fclose(file);
			snprintf(line, sizeof(line), "ps_%08lx.frag: the pixel shader key hashing to 0x%08lx, first in the records of map 0x%08lx\n",
				hash, hash, (unsigned long)records[index].map_hash);
			fputs(line, manifest);
			pixel_files++;
		}
		free(source);
	}
	fclose(manifest);
	platform_log("shader dump: %lu vertex and %lu pixel shaders (%lu records) written to %s",
		vertex_files, pixel_files, record_count, folder);
	free(keys);
	free(records);
}

/* ---------- vertex data and drawing */

void WINAPI D3DDevice_SetStreamSource(UINT stream_number, D3DVertexBuffer *stream_data, UINT stride)
{
	if (stream_number >= 16)
		return;
	device.streams[stream_number].data = stream_data ? stream_data->Data : 0;
	device.streams[stream_number].stride = stride;
	device.streams[stream_number].buffer = stream_data;
	streams_serial++;
	resources_serial++;
}

void WINAPI D3DDevice_SetIndices(D3DIndexBuffer *index_data, UINT base_vertex_index)
{
	device.base_vertex_index = base_vertex_index;
	device.index_buffer = index_data;
	resources_serial++;
	/* an index buffer's Data is a window address, not an offset within one:
	from a map file it is the address the window was linked at, and from
	CreateIndexBuffer ordinary memory, which the move leaves alone */
	D3D__IndexData = index_data ? (WORD *)PORT_WINDOW_REBASE(index_data->Data) : NULL;
}
/* where a frame's time goes on this CPU (debug.gpu_stats): the Direct3D layer's draws, the present (the stream's hand-over, the GPU's wait and the display's), and the rest */
static struct
{
	OSTime draw_ticks;
	double present_ms, work_ms, longest_work_ms, longest_present_ms;
	double last_exit_ms;
	unsigned long frames, over_17, over_34;
} profile;

/* ticks of the time base as milliseconds */
static double ticks_ms(OSTime ticks)
{
	return (double)ticks * 1000.0 / (double)OSTimerClockSpeed;
}

/* a draw's time, to the frame's and to the log's (each draw counted in the note it is made before, as when they were milliseconds) */
static inline void draw_ticks_add(OSTime spent)
{
	profile.draw_ticks += spent;
	frame_draw_ticks += spent;
}

static double profile_now_ms(void)
{
	/* (the time base itself, not clock_gettime: this is read twice a draw, and clock_gettime's conversions were a third of a percent of the game thread) */
	static double ticks_per_ms;

	if (!ticks_per_ms)
		ticks_per_ms = (double)OSTimerClockSpeed / 1000.0;
	return (double)OSGetSystemTime() / ticks_per_ms;
}

/* ---------- drawing (DEKO3D.md, phase 6)

The device reads Direct3D's state at each draw as d3d8_gl.c's prepare_draw
does and writes it to the command stream (dk_commands.h) - but only what the
host does not already hold: the host keeps the state, the shaders, the
uniforms and the vertex format it was last told, so a draw that changes one of
them writes one command, and a draw that changes nothing writes the draw. */

static struct
{
	unsigned long draws, immediate_draws, skipped_no_program, skipped_no_target, skipped_shader, skipped_size;
} stats;

/* what the host holds */
static struct dk_vertex_format format_told;
static BOOL format_known;
static unsigned long long constants_told;

/* what the GX2 thread holds a copy of (dk_derive.h): the render and texture stage states it was last told, and the draw's facts. It derives the draw's
state, shader keys, samplers and uniforms from them; on this thread a draw only finds its textures and tells what changed. The copy is sent whole again
after each present (states_resync), which also finds a state written by a way that did not mark it (render_states_dirty, texture_states_dirty). */
static struct dk_draw_facts facts_told;
static BOOL facts_known;
static unsigned long states_missed;

static unsigned long stage_texture_mode(int stage)
{
	return (D3D__RenderState[D3DRS_PSTEXTUREMODES] >> (5 * stage)) & 0x1f;
}

/* the host's pixel edge of a coordinate in the game's pixels of the targets
bound (target_scale: the screen's targets may be drawn bigger) */
static int target_pixel(float coordinate, int axis)
{
	return (int)floorf(coordinate * target_scale[axis] + 0.5f);
}

/* the state words that differ from the GX2 thread's copy, written as one command, in order: of the render states, every one (render_states_all) or the
ones marked, and of each stage's texture states the ones marked; everything when the copy is to be sent whole */
static void state_writes_send(void)
{
	uint32_t writes[2 * (D3DRS_MAX + D3DTSS_MAXSTAGES * D3DTSS_MAX)];
	unsigned long count = 0, index;
	unsigned stage, word;
	struct dk_command_state_writes *command;

	if (!states_told_valid)
	{
		render_states_all = TRUE;
		memset(texture_states_dirty, 0xff, sizeof(texture_states_dirty));
	}
	if (render_states_all)
	{
		for (index = 0; index < D3DRS_MAX; index++)
		{
			if (states_told_valid && D3D__RenderState[index] == render_states_told[index])
				continue;
			render_states_told[index] = D3D__RenderState[index];
			writes[count * 2] = (uint32_t)index;
			writes[count * 2 + 1] = D3D__RenderState[index];
			count++;
		}
	}
	else
	{
		for (word = 0; word < RENDER_STATE_WORDS; word++)
		{
			uint32_t marked = render_states_dirty[word];

			for (; marked; marked &= marked - 1)
			{
				index = word * 32 + (unsigned long)__builtin_ctz(marked);
				if (D3D__RenderState[index] == render_states_told[index])
					continue;
				render_states_told[index] = D3D__RenderState[index];
				writes[count * 2] = (uint32_t)index;
				writes[count * 2 + 1] = D3D__RenderState[index];
				count++;
			}
		}
	}
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		uint32_t marked = texture_states_dirty[stage];

		for (; marked; marked &= marked - 1)
		{
			index = (unsigned long)__builtin_ctz(marked);
			if (states_told_valid && D3D__TextureState[stage][index] == texture_states_told[stage][index])
				continue;
			texture_states_told[stage][index] = D3D__TextureState[stage][index];
			writes[count * 2] = DK_DERIVE_TEXTURE_STATE_INDEX(stage, index);
			writes[count * 2 + 1] = D3D__TextureState[stage][index];
			count++;
		}
	}
	render_states_all = FALSE;
	memset(render_states_dirty, 0, sizeof(render_states_dirty));
	memset(texture_states_dirty, 0, sizeof(texture_states_dirty));
	states_told_valid = TRUE;
	if (!count)
		return;
	command = stream_command(DK_COMMAND_STATE_WRITES, sizeof(*command) + count * 2 * sizeof(uint32_t));
	command->count = (uint32_t)count;
	memcpy(command->writes, writes, count * 2 * sizeof(uint32_t));
}

/* after a present: the GX2 thread's copy is sent whole with the next draw (what a stream it never ran - none is dropped - or a setter that did not say so
could have left wrong is right again a frame later), and a state found changed that no setter marked is logged: a word that differs from the copy
and is not marked */
static void states_resync(void)
{
	if (states_told_valid)
	{
		unsigned long index;
		unsigned stage;

		if (!render_states_all)
			for (index = 0; index < D3DRS_MAX; index++)
				states_missed += D3D__RenderState[index] != render_states_told[index] && !((render_states_dirty[index / 32] >> (index % 32)) & 1);
		for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
			for (index = 0; index < D3DTSS_MAX; index++)
				states_missed += D3D__TextureState[stage][index] != texture_states_told[stage][index] && !((texture_states_dirty[stage] >> index) & 1);
	}
	if (states_missed)
	{
		static unsigned long told;

		if (states_missed >= told * 2 + 1)
		{
			told = states_missed;
			platform_log("draw states: %lu state words found changed with no setter having said so (put right at the frame's end)", states_missed);
		}
	}
	states_told_valid = FALSE;
	facts_known = FALSE;
}

/* The draw's facts as last made, kept from draw to draw so that a draw makes again only what has changed since (each draw made all of them, a
lookup in the texture cache and the render targets for each stage, cleared them first and compared the whole with what the GX2 thread holds): what
is sent when it differs from facts_told. facts_made: what the draw's own facts were made from (facts_serial counts the rest of it); each stage keeps
what its facts were made from in its memo. */
static struct dk_draw_facts facts_now;
static struct
{
	unsigned long serial;
	const struct vertex_shader_object *program, *declaration;
	BOOL has_depth, immediate;
} facts_made;

/* A stage's facts hold while its texture, the texture's header words and the serials of what dk_texture_get's answer comes from are the same - the
memory watch's (a write to its texels), the cache's drops, the menus' art, the HUD placeholders, the render targets noted - and the frame, in which the
cache counts a texture's use; unless the answer was not a lasting one (dk_texture_get), or the texture is a render target (whose composite is told
the host at a draw that samples it). */
#define STAGE_SERIALS 6

static struct
{
	BOOL lasting;
	const D3DBaseTexture *texture;
	DWORD words[3];
	unsigned long serials[STAGE_SERIALS];
} stage_memos[D3DTSS_MAXSTAGES];

static void stage_serials_make(unsigned long serials[STAGE_SERIALS])
{
	serials[0] = memory_watch_serial();
	serials[1] = dk_texture_serial();
	serials[2] = menu_art_serial();
	serials[3] = hud_hires_placeholder_serial();
	serials[4] = rendered_serial;
	serials[5] = device.frame;
}

/* a stage's facts (facts_now's): what its texture is, found as pixel_key_make found it (finding it may send its texels to the host); whether they
changed */
static BOOL stage_facts_make(int stage, const unsigned long serials[STAGE_SERIALS])
{
	struct dk_stage_facts made, *found = &facts_now.stages[stage];
	D3DBaseTexture *texture = device.textures[stage];
	const unsigned long mode = stage_texture_mode(stage);

	if (texture && texture->Data && mode != 0 && mode != 0x04 && mode != 0x05 && mode != 0x11 && stage_memos[stage].lasting &&
		stage_memos[stage].texture == texture && stage_memos[stage].words[0] == texture->Data && stage_memos[stage].words[1] == texture->Format &&
		stage_memos[stage].words[2] == texture->Size && words_equal(stage_memos[stage].serials, serials, sizeof(stage_memos[stage].serials)))
		return FALSE;
	memset(&made, 0, sizeof(made));
	made.scale[0] = made.scale[1] = 1.0f;
	if (!texture || !texture->Data || mode == 0 || mode == 0x04 || mode == 0x05 || mode == 0x11)
		stage_memos[stage].lasting = FALSE;
	else
	{
		const D3DCOLOR *palette = device.palettes[stage] && device.palettes[stage]->Data ?
			(const D3DCOLOR *)PLATFORM_PHYSICAL_TO_VIRTUAL(device.palettes[stage]->Data) : NULL;
		struct xgpu_texture_description description;
		int kind = DK_TEXTURE_2D, lasting = 0;

		const struct rendered *target = rendered_find(texture->Data);

		memset(&description, 0, sizeof(description));
		if (target)
		{
			/* a render target: the host's image of it */
			xgpu_texture_describe(texture->Format, texture->Size, &description);
			made.target = texture->Data;
			/* (rendered a level at a time: the levels as one image, as
			bind_textures in d3d8_gl.c makes them) */
			if (!description.linear && !description.cube_map && description.levels > 1 &&
				description.levels <= DK_COMPOSITE_LEVELS && target->width == description.width &&
				target->height == description.height)
			{
				composite_tell(&description, texture->Data);
				made.composite = (uint32_t)description.levels;
			}
			else
				description.levels = 1;
			if (description.linear)
			{
				float share[2];

				/* (a linear texture's coordinates are the game's texels, 0 to its width: they reach as far
				as the frame drew, the share of the target that dynamic resolution's step draws into; a
				render target's stage is made again at every draw, never kept: stage_memos' lasting) */
				target_share_of(target->width, target->height, share);
				made.scale[0] = share[0] / (float)target->width;
				made.scale[1] = share[1] / (float)target->height;
			}
		}
		else
		{
			made.id = dk_texture_get((const DWORD *)texture, palette, &kind, &description, &lasting);
			{
				/* (the first 64 textures, once each) */
				static unsigned long logged;
				static unsigned long seen[64];
				unsigned long i;

				if (logged < 64)
				{
					for (i = 0; i < logged && seen[i] != made.id; i++)
						;
					if (i == logged)
					{
						seen[logged++] = made.id;
						platform_log("texture %lu: format word %08lx size word %08lx: %lux%lux%lu, %lu levels, linear %d cube %d, kind %d, palette %d",
							(unsigned long)made.id, (unsigned long)((const DWORD *)texture)[3], (unsigned long)((const DWORD *)texture)[4],
							(unsigned long)description.width, (unsigned long)description.height, (unsigned long)description.depth, (unsigned long)description.levels,
							(int)description.linear, (int)description.cube_map, kind, palette != NULL);
					}
				}
			}
			if (description.linear)
			{
				made.scale[0] = 1.0f / (float)description.width;
				made.scale[1] = 1.0f / (float)description.height;
			}
		}
		made.present = 1;
		made.kind = (uint32_t)kind;
		made.mipmapped = description.levels > 1;
		made.hires = description.hires != FALSE;
		made.hires_coverage = description.hires_coverage != FALSE;
		stage_memos[stage].lasting = !target && lasting;
		stage_memos[stage].texture = texture;
		stage_memos[stage].words[0] = texture->Data;
		stage_memos[stage].words[1] = texture->Format;
		stage_memos[stage].words[2] = texture->Size;
		memcpy(stage_memos[stage].serials, serials, sizeof(stage_memos[stage].serials));
	}
	if (words_equal(&made, found, sizeof(made)))
		return FALSE;
	*found = made;
	return TRUE;
}

/* the draw's facts (facts_now), made again where what they come from has changed; whether they may differ from what they were */
static BOOL facts_make(BOOL has_depth, const struct vertex_shader_object *program, BOOL immediate)
{
	unsigned long serials[STAGE_SERIALS];
	BOOL changed = FALSE;
	int stage;

	if (facts_made.serial != facts_serial || facts_made.program != program || facts_made.declaration != device.vertex_shader ||
		facts_made.has_depth != has_depth || facts_made.immediate != immediate)
	{
		struct dk_draw_facts *facts = &facts_now;

		facts->has_depth = has_depth != FALSE;
		facts->target_scale[0] = target_scale[0];
		facts->target_scale[1] = target_scale[1];
		facts->viewport_x = device.viewport.X;
		facts->viewport_y = device.viewport.Y;
		facts->viewport_width = device.viewport.Width;
		facts->viewport_height = device.viewport.Height;
		facts->viewport_min_z = device.viewport.MinZ;
		facts->viewport_max_z = device.viewport.MaxZ;
		memcpy(facts->viewport_scale, device.viewport_scale, sizeof(facts->viewport_scale));
		memcpy(facts->viewport_offset, device.viewport_offset, sizeof(facts->viewport_offset));
		facts->ui_offset = (int32_t)UI_OFFSET;
		memcpy(facts->texture_clip, texture_clip, sizeof(facts->texture_clip));
		facts->warp_blend = warp_blend != 0;
		facts->program_hash[0] = (uint32_t)(program->program_hash >> 32);
		facts->program_hash[1] = (uint32_t)program->program_hash;
		facts->packed_mask = immediate ? 0 : (uint32_t)device.vertex_shader->packed_mask;
		facts_made.serial = facts_serial;
		facts_made.program = program;
		facts_made.declaration = device.vertex_shader;
		facts_made.has_depth = has_depth;
		facts_made.immediate = immediate;
		changed = TRUE;
	}
	stage_serials_make(serials);
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
		changed |= stage_facts_make(stage, serials);
	return changed;
}

/* the vertex constants the host does not have yet: the registers changed since
the serial it was told, as one run (d3d8_gl.c's prepare_draw does the same
for a program's own registers) */
static void draw_constants_send(void)
{
	unsigned long first = XGPU_VERTEX_CONSTANT_COUNT, last = 0, index;
	struct dk_command_constants *command;

	if (constants_known && constants_told == constants_serial)
		return;
	if (!constants_known)
	{
		first = 0;
		last = XGPU_VERTEX_CONSTANT_COUNT - 1;
	}
	else if (constants_serial - constants_told <= CONSTANT_LOG_SIZE)
	{
		unsigned long long serial;

		for (serial = constants_told + 1; serial <= constants_serial; serial++)
		{
			index = constant_log[serial % CONSTANT_LOG_SIZE];
			if (first > index)
				first = index;
			if (last < index)
				last = index;
		}
	}
	else
	{
		for (index = 0; index < XGPU_VERTEX_CONSTANT_COUNT; index++)
		{
			if (constant_serials[index] > constants_told)
			{
				if (first > index)
					first = index;
				last = index;
			}
		}
	}
	constants_told = constants_serial;
	constants_known = TRUE;
	if (first >= XGPU_VERTEX_CONSTANT_COUNT)
		return;
	command = stream_command(DK_COMMAND_CONSTANTS, sizeof(*command) + (last - first + 1) * 4 * sizeof(float));
	command->first = (uint32_t)first;
	command->count = (uint32_t)(last - first + 1);
	memcpy(command->data, device.constants[first], (last - first + 1) * 4 * sizeof(float));
}

static void draw_format_send(const struct dk_vertex_format *format)
{
	if (format_known && words_equal(format, &format_told, sizeof(*format)))
		return;
	{
		struct dk_command_vertex_format *command = stream_command(DK_COMMAND_VERTEX_FORMAT, sizeof(*command));

		command->format = *format;
	}
	format_told = *format;
	format_known = TRUE;
}

/* d3d8_gl.c's prepare_draw: everything a draw needs but its vertices, told the
GX2 thread, which derives the rest (dk_derive.c). FALSE if there is nothing to draw into, or a shader the draw
needs is not ready (skipped, as decided: DEKO3D.md, "Decisions"). */
static BOOL draw_prepare(BOOL immediate)
{
	struct vertex_shader_object *program = current_program();
	BOOL has_depth = FALSE;

	if (!program || !device.vertex_shader || !program->instructions)
	{
		stats.skipped_no_program++;
		return FALSE;
	}
	if (!targets_bind(&has_depth))
	{
		stats.skipped_no_target++;
		return FALSE;
	}
	if (immediate)
		stats.immediate_draws++;
	else
		stats.draws++;
	state_writes_send();
	/* (facts_now and facts_told are the same after this: they are compared only when facts_now may have changed since, or after a present) */
	if ((facts_make(has_depth, program, immediate) || !facts_known) && (!facts_known || !words_equal(&facts_now, &facts_told, sizeof(facts_now))))
	{
		struct dk_command_draw_facts *command = stream_command(DK_COMMAND_DRAW_FACTS, sizeof(*command));

		command->facts = facts_now;
		facts_told = facts_now;
		facts_known = TRUE;
	}
	draw_constants_send();
	return TRUE;
}

/* the tag cache's (cache/physical_memory_map.c) vertex data is the maps' own, which the converter leaves as it was on the Xbox: little-endian; the rest of what
the GPU reads (the BSPs' vertices, which the converter swaps because the CPU reads them, and everything the CPU makes) is big-endian */
void *physical_memory_get_tag_cache_base_address(void);
#define TAG_CACHE_BYTES 0x1600000UL

static int float_plausible(float value)
{
	value = fabsf(value);
	return value == 0.0f || (value > 1e-8f && value < 1e8f);
}

static float float_from(const unsigned char *at, int little)
{
	uint32_t bits = little ? ((uint32_t)at[0] | (uint32_t)at[1] << 8 | (uint32_t)at[2] << 16 | (uint32_t)at[3] << 24) :
		((uint32_t)at[3] | (uint32_t)at[2] << 8 | (uint32_t)at[1] << 16 | (uint32_t)at[0] << 24);
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

/* What is in the tag cache is the maps' vertex data: a model's, which the converter leaves as the Xbox had it, little-endian, and the BSP's (which lives in the
tag cache as well), which it swaps. The positions of the first vertices say which: read as the wrong byte order they are not coordinates (exponents of 1e30). The
streams of a draw that follow the first one are the same kind. */
static struct
{
	const unsigned char *data;
	unsigned long stride, offset;
	BOOL little;
} sniffed[64];

/* (gx2_shaders.c, when a map has loaded: what was at an address is another map's now) */
void halo_stream_endian_forget(void);

void halo_stream_endian_forget(void)
{
	endian_serial++;
	memset(sniffed, 0, sizeof(sniffed));
}

static BOOL stream_is_little_endian(unsigned long stream, unsigned long position_offset, BOOL has_position, BOOL *decided, BOOL *decision)
{
	const char *base = (const char *)physical_memory_get_tag_cache_base_address();
	const unsigned char *data = (const unsigned char *)PLATFORM_PHYSICAL_TO_VIRTUAL(device.streams[stream].data);
	unsigned long stride = device.streams[stream].stride, vertex;
	int little = 0, big = 0;

	if (!(base && (const char *)data >= base && (const char *)data < base + TAG_CACHE_BYTES))
		return FALSE;
	if (!has_position || !stride)
		return *decided ? *decision : TRUE;
	{
		const unsigned long slot = (((unsigned long)data >> 4) ^ stride) % 64;

		if (sniffed[slot].data == data && sniffed[slot].stride == stride && sniffed[slot].offset == position_offset)
		{
			*decided = TRUE;
			*decision = sniffed[slot].little;
			return *decision;
		}
	}
	for (vertex = 0; vertex < 4; vertex++)
	{
		const unsigned char *at = data + vertex * stride + position_offset;
		unsigned long axis;

		for (axis = 0; axis < 3; axis++)
		{
			little += float_plausible(float_from(at + axis * 4, 1));
			big += float_plausible(float_from(at + axis * 4, 0));
		}
	}
	*decided = TRUE;
	*decision = little >= big;
	{
		const unsigned long slot = (((unsigned long)data >> 4) ^ stride) % 64;

		sniffed[slot].data = data;
		sniffed[slot].stride = stride;
		sniffed[slot].offset = position_offset;
		sniffed[slot].little = *decision;
	}
	return *decision;
}

static uint32_t attribute_format(const struct vertex_element *element)
{
	switch (element->type)
	{
	case D3DVSDT_FLOAT1: return DK_ATTRIBUTE_FLOAT1;
	case D3DVSDT_FLOAT2: return DK_ATTRIBUTE_FLOAT2;
	case D3DVSDT_FLOAT3:
	case D3DVSDT_FLOAT2H: return DK_ATTRIBUTE_FLOAT3;
	case D3DVSDT_FLOAT4: return DK_ATTRIBUTE_FLOAT4;
	case D3DVSDT_D3DCOLOR: return DK_ATTRIBUTE_COLOR;
	case D3DVSDT_SHORT1: return DK_ATTRIBUTE_SHORT1;
	case D3DVSDT_SHORT2: return DK_ATTRIBUTE_SHORT2;
	case D3DVSDT_SHORT3: return DK_ATTRIBUTE_SHORT3;
	case D3DVSDT_SHORT4: return DK_ATTRIBUTE_SHORT4;
	case D3DVSDT_NORMSHORT1: return DK_ATTRIBUTE_NORMSHORT1;
	case D3DVSDT_NORMSHORT2: return DK_ATTRIBUTE_NORMSHORT2;
	case D3DVSDT_NORMSHORT3: return DK_ATTRIBUTE_NORMSHORT3;
	case D3DVSDT_NORMSHORT4: return DK_ATTRIBUTE_NORMSHORT4;
	case D3DVSDT_PBYTE1: return DK_ATTRIBUTE_BYTE1;
	case D3DVSDT_PBYTE2: return DK_ATTRIBUTE_BYTE2;
	case D3DVSDT_PBYTE3: return DK_ATTRIBUTE_BYTE3;
	case D3DVSDT_PBYTE4: return DK_ATTRIBUTE_BYTE4;
	case D3DVSDT_NORMPACKED3: return DK_ATTRIBUTE_PACKED;
	default: return DK_ATTRIBUTE_FLOAT4;
	}
}

/* the declaration's registers and the streams' strides, with the unfed
registers at the values SetVertexData last gave them (setup_streams in
d3d8_gl.c); only those values are kept, so that a change to a register the
declaration feeds does not make a new format */
static void format_from_declaration(struct dk_vertex_format *format)
{
	struct vertex_shader_object *declaration = device.vertex_shader;
	unsigned long index;
	BOOL endian_decided = FALSE, endian_decision = TRUE;

	/* (the position's stream first: it settles the byte order of the streams that are in the tag cache) */
	for (index = 0; index < declaration->element_count; index++)
	{
		const struct vertex_element *element = &declaration->elements[index];

		if (element->reg == 0 && element->type != D3DVSDT_NONE && device.streams[element->stream].data)
		{
			stream_is_little_endian(element->stream, element->offset, TRUE, &endian_decided, &endian_decision);
			break;
		}
	}

	memset(format, 0, sizeof(*format));
	for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
	{
		if (declaration->packed_mask & (1UL << index))
		{
			format->attributes[index].format = DK_ATTRIBUTE_UNFED_PACKED;
		}
		else
		{
			format->attributes[index].format = DK_ATTRIBUTE_UNFED;
			memcpy(format->constants[index], device.attributes[index], sizeof(format->constants[index]));
		}
	}
	for (index = 0; index < declaration->element_count; index++)
	{
		const struct vertex_element *element = &declaration->elements[index];

		if (!device.streams[element->stream].data || element->type == D3DVSDT_NONE)
			continue;
		memset(format->constants[element->reg], 0, sizeof(format->constants[element->reg]));
		format->attributes[element->reg].format = attribute_format(element);
		format->attributes[element->reg].stream = element->stream;
		format->attributes[element->reg].offset = element->offset;
		format->strides[element->stream] = device.streams[element->stream].stride;
		format->stream_mask |= 1UL << element->stream;
		if (stream_is_little_endian(element->stream, element->offset, element->reg == 0, &endian_decided, &endian_decision))
			format->little_endian |= 1UL << element->stream;
		if (device.streams[element->stream].stride && platform_is_contiguous(PLATFORM_PHYSICAL_TO_VIRTUAL(device.streams[element->stream].data)))
			format->static_streams |= 1UL << element->stream;
	}
}

/* format_from_declaration's last result and what it came from: the declaration, the streams' addresses and strides, the registers' current values and
the streams' byte order (their serials). Most draws repeat the last one's, which made it again (and sniffed the streams' byte order) for each draw: 2% of
the game thread in a fight. told: it is what the GX2 thread was last told (draw_format_send). */
static struct
{
	BOOL valid, told;
	const struct vertex_shader_object *declaration;
	unsigned long declaration_id, attributes_serial, endian_serial, streams_serial;
	struct dk_vertex_format format;
} format_memo;

static void format_for_draw(void)
{
	const BOOL same = format_memo.valid && format_memo.declaration == device.vertex_shader && format_memo.declaration_id == device.vertex_shader->id &&
		format_memo.attributes_serial == attributes_serial && format_memo.endian_serial == endian_serial && format_memo.streams_serial == streams_serial;

	if (!same)
	{
		format_from_declaration(&format_memo.format);
		format_memo.valid = TRUE;
		format_memo.told = FALSE;
		format_memo.declaration = device.vertex_shader;
		format_memo.declaration_id = device.vertex_shader->id;
		format_memo.attributes_serial = attributes_serial;
		format_memo.endian_serial = endian_serial;
		format_memo.streams_serial = streams_serial;
	}
	if (!format_memo.told)
	{
		draw_format_send(&format_memo.format);
		format_memo.told = TRUE;
	}
}

static uint32_t primitive_of(D3DPRIMITIVETYPE type)
{
	switch (type)
	{
	case D3DPT_POINTLIST: return DK_PRIMITIVE_POINTS;
	case D3DPT_LINELIST: return DK_PRIMITIVE_LINES;
	case D3DPT_LINELOOP: return DK_PRIMITIVE_LINE_LOOP;
	case D3DPT_LINESTRIP: return DK_PRIMITIVE_LINE_STRIP;
	case D3DPT_TRIANGLESTRIP:
	case D3DPT_QUADSTRIP: return DK_PRIMITIVE_TRIANGLE_STRIP;
	case D3DPT_TRIANGLEFAN:
	case D3DPT_POLYGON: return DK_PRIMITIVE_TRIANGLE_FAN;
	case D3DPT_QUADLIST: return DK_PRIMITIVE_QUADS;
	default: return DK_PRIMITIVE_TRIANGLES;
	}
}

/* what the draws of a stream read: the vertices [first, first + count), and
the bytes a stride-0 stream (one value for every vertex) is given */
#define STRIDELESS_STREAM_BYTES 64

/* writes the draw of vertices [first, first + count) of the streams the
format reads (a lowest vertex of first, so a stream's data starts there);
indices, if not NULL, is the guest address of the draw's indices */
/* the most a draw's indices copied into its command take (DK_INDEX_INLINE):
a larger list is read where it is, as before */
#define INLINE_INDEX_LIMIT (256 * 1024)

/* whether a draw's indices stay as they are until the GPU reads them: those
in the Xbox window - the map's own index buffers, where the game keeps them,
and Custom Edition's converted geometry (custom_edition_geometry.c) - which
nothing writes while the map is loaded, or in Custom Edition's tag cache.
The others are the lists the game remakes as it draws, its dynamic index
buffer's (D3DDevice_CreateIndexBuffer's, in the C heap: d3d8_resources.c):
split screen remakes them for the second view where the first's were. Only
those are copied as the draw is made (DK_INDEX_INLINE); copying every list
not in the tag cache, as at first, copied the map's own each draw and slowed
every game */
static BOOL indices_fixed(const WORD *indices, unsigned long count)
{
	unsigned long start = (unsigned long)(uintptr_t)indices, end = start + count * sizeof(WORD);
	unsigned long custom = (unsigned long)(uintptr_t)halo_custom_edition_tag_cache();

	if (platform_is_contiguous(indices) && platform_is_contiguous((const char *)indices + count * sizeof(WORD) - 1))
		return TRUE;
	return custom && start >= custom && end <= custom + CUSTOM_EDITION_TAG_CACHE_BYTES;
}

/* ---------- the dynamic triangles' index buffer, a new one each frame

The game remakes the BSP's visible triangles (and the other dynamic triangles) every frame into one index buffer (rasterizer_xbox_draw_primitives.c), from its start
again at each rasterizer_dynamic_geometry_begin: each draw's list had to be copied into the stream (the GX2 thread reads it later, by when the game has written the
next frame's over it), 4% of the game thread in a fight. Instead the buffer's memory is one of four, the next at each begin, once every stream that could read it
is retired: a list stays as it was until the GX2 thread has read it, and a draw names it where it is (the GX2 thread copies it for the GPU, as it does the map's). */
#define RENEWED_STORES 4

static struct
{
	D3DIndexBuffer *buffer;
	DWORD original; /* (the buffer's own memory, store 0, which Release frees) */
	unsigned long bytes;
	unsigned char *stores[RENEWED_STORES];
	unsigned long last_read[RENEWED_STORES]; /* the last stream that may read each; 0: none */
	unsigned current;
	unsigned long used; /* how far into the store of now the draws' lists reach */
} renewed;

void wiiu_index_buffer_renew_end(D3DIndexBuffer *buffer);

void wiiu_index_buffer_renew_end(D3DIndexBuffer *buffer)
{
	unsigned store;

	if (!renewed.buffer || renewed.buffer != buffer)
		return;
	halo_switch_renderer_idle(); /* (no stream left that reads them) */
	buffer->Data = renewed.original;
	if (device.index_buffer == buffer)
		D3D__IndexData = (WORD *)PORT_WINDOW_REBASE(buffer->Data);
	for (store = 1; store < RENEWED_STORES; store++)
		free(renewed.stores[store]);
	memset(&renewed, 0, sizeof(renewed));
}

/* rasterizer_dynamic_geometry_begin (the game's, on the Wii U): the buffer's memory from now on is the next store; bytes is the buffer's size */
void wiiu_index_buffer_renew(D3DIndexBuffer *buffer, unsigned long bytes);

void wiiu_index_buffer_renew(D3DIndexBuffer *buffer, unsigned long bytes)
{
	unsigned next, store;

	if (!buffer)
		return;
	if (renewed.buffer != buffer)
	{
		wiiu_index_buffer_renew_end(renewed.buffer);
		renewed.original = buffer->Data;
		renewed.bytes = bytes;
		renewed.stores[0] = (unsigned char *)(uintptr_t)buffer->Data;
		for (store = 1; store < RENEWED_STORES; store++)
		{
			renewed.stores[store] = malloc(bytes);
			if (!renewed.stores[store])
			{
				/* (out of memory: the lists are copied as before) */
				while (--store > 0)
					free(renewed.stores[store]);
				memset(&renewed, 0, sizeof(renewed));
				return;
			}
		}
		renewed.buffer = buffer;
		renewed.current = 0;
	}
	renewed.last_read[renewed.current] = submission; /* (the stream being written may read it: the draws so far) */
	next = (renewed.current + 1) % RENEWED_STORES;
	if (renewed.last_read[next])
	{
		if (renewed.last_read[next] >= submission)
			flush_because(2);
		if (dk_async > 0)
			async_wait_retired((uint32_t)renewed.last_read[next]);
	}
	renewed.current = next;
	buffer->Data = (DWORD)(uintptr_t)renewed.stores[next];
	/* (as far as the last frame's lists reached, the store's lines made, not read: it was written four frames ago, and the game waited for each line it wrote) */
	{
		unsigned char *store = renewed.stores[next];
		const unsigned long reach = renewed.used < renewed.bytes ? renewed.used : renewed.bytes;

		lines_make(store, store + reach);
		renewed.used = 0;
	}
	if (device.index_buffer == buffer)
		D3D__IndexData = (WORD *)PORT_WINDOW_REBASE(buffer->Data);
}

/* whether a draw's indices are in the renewed buffer's store of now (unchanged until the stream reading them is retired) */
static BOOL indices_renewed(const WORD *indices, unsigned long count)
{
	const unsigned char *at = (const unsigned char *)indices, *store = renewed.stores[renewed.current];

	return renewed.buffer && store && at >= store && at + count * sizeof(WORD) <= store + renewed.bytes;
}

/* whether every stream the format reads is the map's own vertex data in the tag cache, which the game does not write while it draws (a BSP switch loads into it,
and announces it): a draw of those leaves its indices' range to the host's thread (DK_DRAW_RANGE_LATER) */
static BOOL streams_in_tag_cache(unsigned long first)
{
	const unsigned char *base = (const unsigned char *)physical_memory_get_tag_cache_base_address();
	uint32_t mask;

	if (!base)
		return FALSE;
	for (mask = format_told.stream_mask; mask; mask &= mask - 1)
	{
		const unsigned stream = (unsigned)__builtin_ctz(mask);
		const unsigned char *at;

		if (!device.streams[stream].data || !device.streams[stream].stride)
			return FALSE;
		at = (const unsigned char *)PLATFORM_PHYSICAL_TO_VIRTUAL(device.streams[stream].data) + first * device.streams[stream].stride;
		if (at < base || at + 65536UL * device.streams[stream].stride > base + TAG_CACHE_BYTES)
			return FALSE;
	}
	return TRUE;
}

static void draw_write(D3DPRIMITIVETYPE type, unsigned long count, unsigned long first, unsigned long vertex_count,
	const WORD *indices, long vertex_offset)
{
	struct dk_command_draw *command;
	unsigned long slot = 0;
	uint32_t mask;
	WORD *long_copy = NULL;
	/* (the streams the format reads, a slot each in the command) */
	const unsigned long streams = (unsigned long)__builtin_popcount(format_told.stream_mask);
	/* (a list in the renewed dynamic buffer is read where it is: the buffer is not written again until its stream is retired) */
	const BOOL renewed_list = indices && indices_renewed(indices, count);
	const unsigned long renewed_reach = renewed_list ? (unsigned long)((const unsigned char *)(indices + count) - renewed.stores[renewed.current]) : 0;
	/* (a list that stays as it is until the GPU reads it: asked once, the three places below having asked it each) */
	const BOOL fixed = indices && !renewed_list && indices_fixed(indices, count);
	unsigned long index_bytes = indices && !renewed_list && count * sizeof(WORD) <= INLINE_INDEX_LIMIT && !fixed ? (count * sizeof(WORD) + 3) & ~3UL : 0;

	/* (a list the game made this frame, of the map's vertices: copied here, its range found by the host's thread, the copy and range together having cost the game
	thread 6-9% of its time in a10. Not for streams outside the tag cache, whose read ranges the indices give: noted as any vertex the indices could name, each
	lock of a dynamic buffer's next vertices would wait for the GX2 thread to run the draws before it, as the Direct3D replay of the layer showed) */
	const BOOL range_later = indices && (index_bytes || renewed_list || fixed) && streams_in_tag_cache(first);

	if (renewed_reach > renewed.used)
		renewed.used = renewed_reach;
	command = stream_command(DK_COMMAND_DRAW, sizeof(*command) + streams * 2 * sizeof(uint32_t) + index_bytes);
	command->primitive = primitive_of(type) | (range_later ? DK_DRAW_RANGE_LATER : 0u);
	command->count = (uint32_t)count;
	command->index_address = index_bytes ? DK_INDEX_INLINE : (uint32_t)(uintptr_t)indices;
	if (indices)
	{
		/* an indexed draw: the streams are read from the lowest vertex the indices name to the highest (index i is vertex first + i), found as the indices are copied
		into the command, if they are, so that they are read once */
		uint32_t lowest, highest;

		if (range_later)
		{
			if (index_bytes)
				memcpy(&command->streams[streams], indices, count * sizeof(WORD));
			lowest = 0;
			highest = 65535;
		}
		else if (index_bytes)
			wiiu_index_copy_range((uint16_t *)(void *)&command->streams[streams], indices, count, &lowest, &highest);
		else if (!renewed_list && !fixed && (long_copy = malloc(count * sizeof(WORD))) != NULL)
		{
			/* a list the game remakes as it draws, too long to go in the command: copied now into a buffer of its own, freed once the stream that reads it is
			retired (read where it is, the game could rewrite it before the host reads it: the host reads the stream later, and with the GX2 thread much later) */
			wiiu_index_copy_range((uint16_t *)(void *)long_copy, indices, count, &lowest, &highest);
			command->index_address = (uint32_t)(uintptr_t)long_copy;
		}
		else
			wiiu_index_range(indices, count, &lowest, &highest);
		if (!range_later)
		{
			first += lowest;
			vertex_count = highest - lowest + 1;
			vertex_offset = -(long)lowest;
		}
		else
			vertex_count = highest - lowest + 1; /* (what the notes below cover: any vertex the indices could name) */
	}
	command->vertex_offset = (int32_t)vertex_offset;
	command->inline_bytes = 0;
	command->stream_count = (uint32_t)streams;
	for (mask = format_told.stream_mask; mask; mask &= mask - 1, slot++)
	{
		const unsigned stream = (unsigned)__builtin_ctz(mask);
		const unsigned long stride = device.streams[stream].stride;

		command->streams[slot][0] = (uint32_t)(uintptr_t)PLATFORM_PHYSICAL_TO_VIRTUAL(device.streams[stream].data) +
			(uint32_t)(first * stride);
		command->streams[slot][1] = range_later ? (uint32_t)stride : (uint32_t)(stride ? stride * vertex_count : STRIDELESS_STREAM_BYTES);
	}
	/* (the memory the command reads when the stream is handed over is noted once the command is written: noting can hand the stream over when it has no room, and then the
	command has gone with it, safely) */
	for (mask = format_told.stream_mask; mask; mask &= mask - 1)
	{
		const unsigned stream = (unsigned)__builtin_ctz(mask);
		const unsigned long stride = device.streams[stream].stride;

		dk_stream_note_read((const char *)PLATFORM_PHYSICAL_TO_VIRTUAL(device.streams[stream].data) + first * stride,
			stride ? stride * vertex_count : STRIDELESS_STREAM_BYTES);
	}
	/* (after the command: writing it may have handed the stream over, and the
	draw is in the submission it is written to) */
	draw_resources_used(indices != NULL, TRUE);
	if (long_copy)
		dk_stream_free_after_handover(long_copy, count * sizeof(WORD));
	/* handed over every so many draws, not all at the present: the GPU starts on the frame's first draws while the CPU makes the rest (when the stream went only at the present the frame took the CPU's
	time and then the GPU's: 38 frames a second instead of 43) */
	if (++draws_since_handover >= DRAWS_PER_HANDOVER)
		flush_because(8);
}

static void D3DDevice_DrawVertices_inner(D3DPRIMITIVETYPE primitive_type, UINT start_vertex, UINT vertex_count)
{
	if (primitive_type == D3DPT_QUADLIST)
		vertex_count &= ~3U;
	if (!vertex_count || !draw_prepare(FALSE))
		return;
	format_for_draw();
	draw_write(primitive_type, vertex_count, start_vertex, vertex_count, NULL, 0);
}

void WINAPI D3DDevice_DrawVertices(D3DPRIMITIVETYPE primitive_type, UINT start_vertex, UINT vertex_count)
{
	const OSTime start = OSGetSystemTime();

	D3DDevice_DrawVertices_inner(primitive_type, start_vertex, vertex_count);
	draw_ticks_add(OSGetSystemTime() - start);
}

static void D3DDevice_DrawIndexedVertices_inner(D3DPRIMITIVETYPE primitive_type, UINT vertex_count, CONST WORD *index_data)
{
	if (primitive_type == D3DPT_QUADLIST)
		vertex_count &= ~3U;
	if (!vertex_count || !index_data || !draw_prepare(FALSE))
		return;
	format_for_draw();
	draw_write(primitive_type, vertex_count, device.base_vertex_index, 0, index_data, 0);
}

void WINAPI D3DDevice_DrawIndexedVertices(D3DPRIMITIVETYPE primitive_type, UINT vertex_count, CONST WORD *index_data)
{
	const OSTime start = OSGetSystemTime();

	D3DDevice_DrawIndexedVertices_inner(primitive_type, vertex_count, index_data);
	draw_ticks_add(OSGetSystemTime() - start);
}

/* ---------- immediate mode */

/* which code the immediate-mode draws come from: the place Begin was called from, counted, and logged with the draw statistics (the addresses are the code's own in the log's
terms: tools/wiiu/profile_report.py resolves them as it does the profile's) */
#define IMMEDIATE_SITES 48
static struct { uintptr_t site; unsigned long draws; } immediate_sites[IMMEDIATE_SITES];

static void immediate_site_count(uintptr_t site)
{
	unsigned index;

	for (index = 0; index < IMMEDIATE_SITES; index++)
	{
		if (immediate_sites[index].site == site || !immediate_sites[index].site)
		{
			immediate_sites[index].site = site;
			immediate_sites[index].draws++;
			return;
		}
	}
}

static void decal_batch_forget(void);

void WINAPI D3DDevice_Begin(D3DPRIMITIVETYPE primitive_type)
{
	immediate_site_count((uintptr_t)__builtin_return_address(0));
	device.immediate_active = TRUE;
	decal_batch_forget();
	device.immediate_skipped = device.visibility_test_active && !host_gx2_visibility_enabled();
	device.immediate_type = primitive_type;
	wiiu_immediate_begin(&device.immediate);
}

/* the vertex format of an immediate draw: the registers of `mask` from the stream (four floats each, in order of register), the others at their values now */
static void immediate_format_send(unsigned long mask)
{
	struct dk_vertex_format format;
	unsigned long index, place = 0;

	memset(&format, 0, sizeof(format));
	for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
	{
		if (mask & (1UL << index))
		{
			format.attributes[index].format = DK_ATTRIBUTE_FLOAT4;
			format.attributes[index].stream = 0;
			format.attributes[index].offset = (uint32_t)(place++ * 4 * sizeof(float));
		}
		else
		{
			format.attributes[index].format = DK_ATTRIBUTE_UNFED;
			memcpy(format.constants[index], device.attributes[index], sizeof(format.constants[index]));
		}
	}
	format.strides[0] = (uint32_t)(place * 4 * sizeof(float));
	format.stream_mask = 1;
	draw_format_send(&format);
	format_memo.told = FALSE; /* (the GX2 thread's format is this one now) */
}

#define IMMEDIATE_DRAW_HEADER (sizeof(struct dk_command_draw) + 2 * sizeof(uint32_t))

/* An immediate draw's first vertex: what it draws with (state, shaders, the vertex format: nothing a Begin/End may change comes before its End) goes in the stream now,
and the vertices are written at the stream's tail, after room for the draw's command, where End's command has them inline: they were kept apart and copied in at End,
4-5% of the game thread in a fight (the decals' thousands a frame). The tail is lent until End, a register the layout lacks, too many vertices for the room left, or anything
else written to the stream or handed over: then they go back to the heap (wiiu_immediate_unlend), and End copies them in as before. */
static void immediate_stream_open(void)
{
	const unsigned long stride = device.immediate.layout_count * 4 * sizeof(float);

	if (stream_used + IMMEDIATE_DRAW_HEADER + 64 * stride > STREAM_SIZE)
		flush_because(3);
	if (!draw_prepare(TRUE))
	{
		device.immediate_skipped = TRUE; /* (as End would have: nothing to draw with) */
		return;
	}
	immediate_format_send(device.immediate.mask);
	if (stream_used + IMMEDIATE_DRAW_HEADER + stride > STREAM_SIZE)
		return;
	wiiu_immediate_lend(&device.immediate, (float *)(void *)((unsigned char *)stream + stream_used + IMMEDIATE_DRAW_HEADER),
		(STREAM_SIZE - stream_used - IMMEDIATE_DRAW_HEADER) / sizeof(float));
}

static void immediate_emit(void)
{
	if (device.immediate_skipped)
		return;
	if (!device.immediate.count && !device.immediate.lent_floats)
	{
		immediate_stream_open();
		if (device.immediate_skipped)
			return;
	}
	if (device.immediate.lent_floats)
		stream_lines_make((unsigned long)((unsigned char *)(device.immediate.vertices + (device.immediate.count + 1) * device.immediate.layout_count * 4) -
			(unsigned char *)stream));
	wiiu_immediate_emit(&device.immediate, (const float (*)[4])device.attributes);
}

/* End of a draw whose vertices are at the stream's tail: its command written round them */
static void immediate_stream_close(unsigned long count)
{
	struct dk_command_draw *command = (struct dk_command_draw *)(void *)((unsigned char *)stream + stream_used);
	const unsigned long bytes = count * device.immediate.layout_count * 4 * sizeof(float);

	device.immediate.lent_floats = 0;
	device.immediate.vertices = device.immediate.heap;
	if (!count)
		return;
	command->header.type = DK_COMMAND_DRAW;
	command->header.size = (uint32_t)((IMMEDIATE_DRAW_HEADER + bytes + 3) & ~3UL);
	command->primitive = primitive_of(device.immediate_type);
	command->count = (uint32_t)count;
	command->index_address = 0;
	command->vertex_offset = 0;
	command->inline_bytes = (uint32_t)bytes;
	command->stream_count = 1;
	command->streams[0][0] = 0;
	command->streams[0][1] = (uint32_t)bytes;
	stream_used += command->header.size;
	draw_resources_used(FALSE, FALSE);
}

static BOOL decal_batch_end(void);

static void D3DDevice_End_inner(void)
{
	/* only the registers SetVertexData wrote go in the stream (each is four floats of a vertex, at a place of its own in it); the others are the current values, as the
	declaration's unfed registers are. Each vertex was kept with every register (immediate_emit). */
	unsigned long count = device.immediate.count, index, stride, touched = 0;
	D3DPRIMITIVETYPE type = device.immediate_type;
	unsigned long mask = device.immediate.mask;
	struct dk_command_draw *command;
	unsigned registers[XGPU_VERTEX_ATTRIBUTE_COUNT];

	if (decal_batch_end())
		return;
	device.immediate_active = FALSE;
	device.immediate.count = 0;
	if (type == D3DPT_QUADLIST)
		count &= ~3UL;
	if (device.immediate.lent_floats)
	{
		immediate_stream_close(count);
		return;
	}
	if (!count)
		return;
	/* a lens flare's occlusion test draws a quad between its Begin and End: with the backend's queries off the quad is counted by nothing (the result is 0 whatever is drawn), and
	there were hundreds of them a frame, each a draw */
	if (device.visibility_test_active && !host_gx2_visibility_enabled())
		return;
	for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
		if (mask & (1UL << index))
			registers[touched++] = index;
	stride = touched * 4 * sizeof(float);
	/* the vertices go in the stream with the draw: the next Begin reuses
	the buffer they are in before the stream is handed over */
	if (count * stride + 4096 > STREAM_SIZE)
	{
		stats.skipped_size++;
		return;
	}
	if (!draw_prepare(TRUE))
		return;
	immediate_format_send(mask);
	command = stream_command(DK_COMMAND_DRAW, sizeof(*command) + 2 * sizeof(uint32_t) + count * stride);
	command->primitive = primitive_of(type);
	command->count = (uint32_t)count;
	command->index_address = 0;
	command->vertex_offset = 0;
	command->inline_bytes = (uint32_t)(count * stride);
	command->stream_count = 1;
	command->streams[0][0] = 0;
	command->streams[0][1] = (uint32_t)(count * stride);
	/* (count is what was kept: Begin's count is zeroed above, the pack reads the vertices, which stay) */
	wiiu_immediate_pack(&device.immediate, count, (float *)&command->streams[1], registers);
	draw_resources_used(FALSE, FALSE);
}

void WINAPI D3DDevice_End(void)
{
	const OSTime start = OSGetSystemTime();

	D3DDevice_End_inner();
	draw_ticks_add(OSGetSystemTime() - start);
}

static void set_attribute(INT reg, float a, float b, float c, float d)
{
	BOOL emit = FALSE;

	if (reg == D3DVSDE_VERTEX)
	{
		reg = 0;
		emit = TRUE;
	}
	if (reg < 0 || reg >= XGPU_VERTEX_ATTRIBUTE_COUNT)
		return;
	if (device.immediate_active)
		wiiu_immediate_write(&device.immediate, (unsigned)reg, device.attributes[reg]);
	else
		device.immediate.mask |= 1UL << reg;
	attributes_serial++;
	device.attributes[reg][0] = a;
	device.attributes[reg][1] = b;
	device.attributes[reg][2] = c;
	device.attributes[reg][3] = d;
	/* like the hardware, writing register 0 completes a vertex */
	if (device.immediate_active && (emit || reg == 0))
		immediate_emit();
}

void WINAPI D3DDevice_SetVertexData2f(INT reg, FLOAT a, FLOAT b)
{
	set_attribute(reg, a, b, 0.0f, 1.0f);
}

void WINAPI D3DDevice_SetVertexData4f(INT reg, FLOAT a, FLOAT b, FLOAT c, FLOAT d)
{
	set_attribute(reg, a, b, c, d);
}

void WINAPI D3DDevice_SetVertexData2s(INT reg, SHORT a, SHORT b)
{
	set_attribute(reg, (float)a, (float)b, 0.0f, 1.0f);
}

/* a byte as the vertex attribute it is, b / 255.0f, from a table: four divisions for each colour of each immediate vertex (the decals draw thousands a frame)
cost the Wii U's game thread several percent; the table's values are the divisions' */
static float byte_unit[256];

static const float *byte_units(void)
{
	if (byte_unit[255] == 0.0f)
	{
		unsigned i;

		for (i = 0; i < 256; i++)
			byte_unit[i] = (float)i / 255.0f;
	}
	return byte_unit;
}

void WINAPI D3DDevice_SetVertexData4ub(INT reg, BYTE a, BYTE b, BYTE c, BYTE d)
{
	const float *unit = byte_units();

	set_attribute(reg, unit[a], unit[b], unit[c], unit[d]);
}

void WINAPI D3DDevice_SetVertexDataColor(INT reg, D3DCOLOR color)
{
	const float *unit = byte_units();

	set_attribute(reg, unit[(color >> 16) & 0xff], unit[(color >> 8) & 0xff], unit[color & 0xff], unit[(color >> 24) & 0xff]);
}

/* the decals' vertices (rasterizer_xbox_decals.c's struct decal_vertex) */
struct wiiu_decal_vertex
{
	float position[3];
	uint32_t texcoord;
};

#define DECAL_LAYOUT (1UL | 1UL << 4 | 1UL << 9)

static void decal_vertex_through_registers(const float color[4], const struct wiiu_decal_vertex *vertex)
{
	set_attribute(9, color[0], color[1], color[2], color[3]);
	set_attribute(4, (float)(short)(vertex->texcoord & 0xffff) * (1.0f / 32767.0f), (float)(short)(vertex->texcoord >> 16) * (1.0f / 32767.0f), 0.0f, 1.0f);
	set_attribute(0, vertex->position[0], vertex->position[1], vertex->position[2], 1.0f);
}

/* A decal's vertices between the batch's Begin and End (rasterizer_xbox_decals.c on the Wii U): each is what SetVertexData4ub(9, color), SetVertexData2f(4, texcoord /
32767) and SetVertexData4f(0, position, 1) make, but written straight into the draw, registers 0, 4 and 9 side by side, once the first vertex has laid the draw out so:
three calls and the registers' bookkeeping for each of thousands of vertices a frame were 10% of the game thread in a fight. (The texture coordinates are multiplied by
1/32767, not divided: the last bit may differ, which nothing sees.) */
static void decal_vertices_expanded(const unsigned char *color_bytes, const void *vertices, long count)
{
	const struct wiiu_decal_vertex *vertex = (const struct wiiu_decal_vertex *)vertices;
	const float *unit = byte_units();
	const float color[4] = { unit[color_bytes[0]], unit[color_bytes[1]], unit[color_bytes[2]], unit[color_bytes[3]] };
	float *out;
	long index;

	while (count > 0 && (!device.immediate_active || device.immediate_skipped || device.immediate.layout_mask != DECAL_LAYOUT))
	{
		decal_vertex_through_registers(color, vertex++);
		count--;
	}
	if (count <= 0)
		return;
	out = wiiu_immediate_room(&device.immediate, (unsigned long)count);
	if (!out)
	{
		while (count-- > 0)
			decal_vertex_through_registers(color, vertex++);
		return;
	}
	for (index = 0; index < count; index++, out += 12)
	{
		/* (the stream's lines a few vertices ahead made, the decal's vertices asked for ahead: the 750 core waits for every line it has not) */
		if (!(index & 7))
		{
			if (device.immediate.lent_floats)
				stream_lines_make((unsigned long)((unsigned char *)(out + 12 * 16) - (unsigned char *)stream));
			__asm__ volatile("dcbt 0,%0" : : "r"(&vertex[index + 16]));
			__asm__ volatile("dcbt 0,%0" : : "r"(&vertex[index + 18]));
			__asm__ volatile("dcbt 0,%0" : : "r"(&vertex[index + 20]));
			__asm__ volatile("dcbt 0,%0" : : "r"(&vertex[index + 22]));
		}
		out[0] = vertex[index].position[0];
		out[1] = vertex[index].position[1];
		out[2] = vertex[index].position[2];
		out[3] = 1.0f;
		out[4] = (float)(short)(vertex[index].texcoord & 0xffff) * (1.0f / 32767.0f);
		out[5] = (float)(short)(vertex[index].texcoord >> 16) * (1.0f / 32767.0f);
		out[6] = 0.0f;
		out[7] = 1.0f;
		out[8] = color[0];
		out[9] = color[1];
		out[10] = color[2];
		out[11] = color[3];
	}
	device.immediate.count += (unsigned long)count;
	/* (the registers hold the last vertex's values after, as the calls would have left them) */
	attributes_serial++;
	memcpy(device.attributes[0], out - 12, 4 * sizeof(float));
	memcpy(device.attributes[4], out - 8, 4 * sizeof(float));
	memcpy(device.attributes[9], out - 4, 4 * sizeof(float));
}
/* A batch's decals as the game gave them (rasterizer_xbox_decals.c), for the GX2 thread to expand (DK_DRAW_DECALS): for each decal its vertex count and
color's bytes, then its vertices as they are (16 bytes each, a third of what they are expanded to). Expanding them here was 5% of the game thread in a fight,
with the stream's lines it made for them. The batch is drawn at End as decal_vertices_expanded's vertices would have been: the same vertex format and
registers, the same arithmetic (wiiu_gx2.c's decals_expand); a batch that cannot be kept so (another register written between, no memory, too big for
the stream) is expanded here first, in its order. */
static struct
{
	unsigned char *data;
	unsigned long used, capacity, vertices;
	struct wiiu_decal_vertex last;
	unsigned char last_color[4];
} decal_batch;

static void decal_batch_forget(void)
{
	decal_batch.used = 0;
	decal_batch.vertices = 0;
}

/* (records: count, color's four bytes, then the vertices) */
#define DECAL_RECORD_HEADER 8UL
#define DECAL_BATCH_LIMIT (512UL * 1024UL)

/* the batch kept so far, expanded here into the draw being made, in order */
static void decal_batch_spill(void)
{
	unsigned long at = 0;

	while (at < decal_batch.used)
	{
		uint32_t count;

		memcpy(&count, decal_batch.data + at, sizeof(count));
		decal_batch.vertices -= count;
		decal_vertices_expanded(decal_batch.data + at + 4, decal_batch.data + at + DECAL_RECORD_HEADER, (long)count);
		at += DECAL_RECORD_HEADER + count * sizeof(struct wiiu_decal_vertex);
	}
	decal_batch.used = 0;
	decal_batch.vertices = 0;
}

void wiiu_decal_vertices(const unsigned char *color_bytes, const void *vertices, long count);

void wiiu_decal_vertices(const unsigned char *color_bytes, const void *vertices, long count)
{
	unsigned long bytes;
	uint32_t count32;

	if (count <= 0)
		return;
	bytes = DECAL_RECORD_HEADER + (unsigned long)count * sizeof(struct wiiu_decal_vertex);
	/* (kept only in a QUADLIST Begin that nothing else has written to: anything else goes the way it went) */
	if (!device.immediate_active || device.immediate_skipped || device.immediate_type != D3DPT_QUADLIST || device.immediate.count ||
		device.immediate.mask != 1 || decal_batch.used + bytes > DECAL_BATCH_LIMIT)
	{
		decal_batch_spill();
		decal_vertices_expanded(color_bytes, vertices, count);
		return;
	}
	if (decal_batch.used + bytes > decal_batch.capacity)
	{
		unsigned long capacity = decal_batch.capacity ? decal_batch.capacity : 64UL * 1024UL;
		unsigned char *grown;

		while (capacity < decal_batch.used + bytes)
			capacity *= 2;
		grown = realloc(decal_batch.data, capacity);
		if (!grown)
		{
			decal_batch_spill();
			decal_vertices_expanded(color_bytes, vertices, count);
			return;
		}
		decal_batch.data = grown;
		decal_batch.capacity = capacity;
	}
	count32 = (uint32_t)count;
	memcpy(decal_batch.data + decal_batch.used, &count32, sizeof(count32));
	memcpy(decal_batch.data + decal_batch.used + 4, color_bytes, 4);
	memcpy(decal_batch.data + decal_batch.used + DECAL_RECORD_HEADER, vertices, (unsigned long)count * sizeof(struct wiiu_decal_vertex));
	decal_batch.used += bytes;
	decal_batch.vertices += (unsigned long)count;
	decal_batch.last = ((const struct wiiu_decal_vertex *)vertices)[count - 1];
	memcpy(decal_batch.last_color, color_bytes, 4);
}

/* End of a Begin whose vertices are a kept decal batch: drawn as DK_DRAW_DECALS. FALSE if there is none (End goes on as it did). */
static BOOL decal_batch_end(void)
{
	const float *unit = byte_units();
	unsigned long total, bytes = 0, kept = 0;
	struct dk_command_draw *command;

	if (!decal_batch.vertices)
	{
		decal_batch.used = 0;
		return FALSE;
	}
	if (device.immediate.count)
	{
		/* (vertices were made another way after the batch began: in order, all of them that way) */
		decal_batch_spill();
		return FALSE;
	}
	/* the registers as the last vertex's three calls would have left them */
	attributes_serial++;
	device.attributes[0][0] = decal_batch.last.position[0];
	device.attributes[0][1] = decal_batch.last.position[1];
	device.attributes[0][2] = decal_batch.last.position[2];
	device.attributes[0][3] = 1.0f;
	device.attributes[4][0] = (float)(short)(decal_batch.last.texcoord & 0xffff) * (1.0f / 32767.0f);
	device.attributes[4][1] = (float)(short)(decal_batch.last.texcoord >> 16) * (1.0f / 32767.0f);
	device.attributes[4][2] = 0.0f;
	device.attributes[4][3] = 1.0f;
	device.attributes[9][0] = unit[decal_batch.last_color[0]];
	device.attributes[9][1] = unit[decal_batch.last_color[1]];
	device.attributes[9][2] = unit[decal_batch.last_color[2]];
	device.attributes[9][3] = unit[decal_batch.last_color[3]];
	device.immediate_active = FALSE;
	device.immediate.count = 0;
	total = decal_batch.vertices & ~3UL;
	decal_batch.vertices = 0;
	if (!total || (device.visibility_test_active && !host_gx2_visibility_enabled()) || !draw_prepare(TRUE))
	{
		decal_batch.used = 0;
		return TRUE;
	}
	immediate_format_send(DECAL_LAYOUT);
	/* (the records that hold the first total vertices; the last cut short if the batch was not whole quads) */
	while (bytes < decal_batch.used && kept < total)
	{
		uint32_t count;

		memcpy(&count, decal_batch.data + bytes, sizeof(count));
		if (kept + count > total)
		{
			count = (uint32_t)(total - kept);
			memcpy(decal_batch.data + bytes, &count, sizeof(count));
		}
		kept += count;
		bytes += DECAL_RECORD_HEADER + count * sizeof(struct wiiu_decal_vertex);
	}
	command = stream_command(DK_COMMAND_DRAW, sizeof(*command) + 2 * sizeof(uint32_t) + bytes);
	command->primitive = primitive_of(D3DPT_QUADLIST) | DK_DRAW_DECALS;
	command->count = (uint32_t)total;
	command->index_address = 0;
	command->vertex_offset = 0;
	command->inline_bytes = (uint32_t)bytes;
	command->stream_count = 1;
	command->streams[0][0] = 0;
	command->streams[0][1] = (uint32_t)(total * 12 * sizeof(float));
	memcpy(&command->streams[1], decal_batch.data, bytes);
	decal_batch.used = 0;
	draw_resources_used(FALSE, FALSE);
	return TRUE;
}

/* ---------- clearing */

void WINAPI D3DDevice_Clear(DWORD count, CONST D3DRECT *rectangles, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
	struct dk_command_clear *command;
	uint32_t clear_flags = 0;
	DWORD index, kept = 0;

	if (flags & D3DCLEAR_TARGET)
	{
		/* the Xbox clears the channels named (D3DCLEAR_TARGET_R, _G, _B, _A):
		the fog screen clears only alpha, leaving the picture under the fog */
		clear_flags |= ((flags & D3DCLEAR_TARGET_R) ? DK_CLEAR_RED : 0) | ((flags & D3DCLEAR_TARGET_G) ? DK_CLEAR_GREEN : 0) |
			((flags & D3DCLEAR_TARGET_B) ? DK_CLEAR_BLUE : 0) | ((flags & D3DCLEAR_TARGET_A) ? DK_CLEAR_ALPHA : 0);
	}
	if (flags & D3DCLEAR_ZBUFFER)
		clear_flags |= DK_CLEAR_DEPTH;
	if (flags & D3DCLEAR_STENCIL)
		clear_flags |= DK_CLEAR_STENCIL;
	if (!clear_flags || !targets_bind(NULL))
		return;
	command = stream_command(DK_COMMAND_CLEAR, sizeof(*command) + (count && rectangles ? count : 1) * 4 * sizeof(uint32_t));
	/* (after the command: writing it may have handed the stream over, and the
	clear is in the submission it is written to) */
	targets_used();
	command->flags = clear_flags;
	color_to_vec4(color, command->color);
	command->depth = z;
	command->stencil = stencil;
	if (!count || !rectangles)
	{
		/* the NV2A clips a viewport-less clear to the viewport, which is what
		keeps a split-screen window's clear from wiping the other window */
		int x0 = target_pixel((float)device.viewport.X, 0), y0 = target_pixel((float)device.viewport.Y, 1);

		command->rectangles[0][0] = (uint32_t)x0;
		command->rectangles[0][1] = (uint32_t)y0;
		command->rectangles[0][2] = (uint32_t)(target_pixel((float)(device.viewport.X + device.viewport.Width), 0) - x0);
		command->rectangles[0][3] = (uint32_t)(target_pixel((float)(device.viewport.Y + device.viewport.Height), 1) - y0);
		command->rectangle_count = 1;
		return;
	}
	for (index = 0; index < count; index++)
	{
		INT left = rectangles[index].x1 > (INT)device.viewport.X ? rectangles[index].x1 : (INT)device.viewport.X;
		INT top = rectangles[index].y1 > (INT)device.viewport.Y ? rectangles[index].y1 : (INT)device.viewport.Y;
		INT right = rectangles[index].x2 < (INT)(device.viewport.X + device.viewport.Width) ?
			rectangles[index].x2 : (INT)(device.viewport.X + device.viewport.Width);
		INT bottom = rectangles[index].y2 < (INT)(device.viewport.Y + device.viewport.Height) ?
			rectangles[index].y2 : (INT)(device.viewport.Y + device.viewport.Height);

		if (left >= right || top >= bottom)
			continue;
		{
			int x0 = target_pixel((float)(left + UI_OFFSET), 0), y0 = target_pixel((float)top, 1);

			command->rectangles[kept][0] = (uint32_t)x0;
			command->rectangles[kept][1] = (uint32_t)y0;
			command->rectangles[kept][2] = (uint32_t)(target_pixel((float)(right + UI_OFFSET), 0) - x0);
			command->rectangles[kept][3] = (uint32_t)(target_pixel((float)bottom, 1) - y0);
		}
		kept++;
	}
	command->rectangle_count = kept;
}

/* ---------- presentation */

/* debug.screenshot_every: every that many frames the host reads the back
buffer back into a buffer the guest gives (dk_command_present), and the
frame is written as a BMP in debug.screenshot_directory, as d3d8_gl.c does */
static long screenshot_every = -1;
static int statistics_enabled = -1;
static unsigned char *screenshot_pixels;
static uint32_t screenshot_width, screenshot_height;

static void screenshot_write(void)
{
	const char *directory = config_string("debug.screenshot_directory");
	unsigned long width = screenshot_width, height = screenshot_height;
	unsigned long image_size = width * height * 4, pixel;
	unsigned char header[54] = { 'B', 'M' };
	char path[512];
	FILE *file;

	/* the host's RGBA, as BGRA; the display ignores destination alpha, which
	the game uses as scratch, and image viewers would show it as transparency */
	for (pixel = 0; pixel < width * height; pixel++)
	{
		unsigned char red = screenshot_pixels[pixel * 4];

		screenshot_pixels[pixel * 4] = screenshot_pixels[pixel * 4 + 2];
		screenshot_pixels[pixel * 4 + 2] = red;
		screenshot_pixels[pixel * 4 + 3] = 0xff;
	}
	snprintf(path, sizeof(path), "%s/frame%05lu.bmp", directory, device.frame);
	file = fopen(path, "wb");
	if (!file)
		return;
	*(unsigned int *)(header + 2) = (unsigned int)(54 + image_size);
	*(unsigned int *)(header + 10) = 54;
	*(unsigned int *)(header + 14) = 40;
	*(int *)(header + 18) = (int)width;
	*(int *)(header + 22) = -(int)height; /* rows from the top, as read */
	*(unsigned short *)(header + 26) = 1;
	*(unsigned short *)(header + 28) = 32;
	*(unsigned int *)(header + 34) = (unsigned int)image_size;
	fwrite(header, 1, sizeof(header), file);
	fwrite(screenshot_pixels, 1, image_size, file);
	fclose(file);
}

/* debug.gpu_stats: what the draws of the last second came to */
static void draw_statistics_log(void)
{
	platform_log("frame %lu: %lu draws, %lu immediate; skipped %lu no program, %lu no target, %lu shader not ready, "
		"%lu too big", device.frame, stats.draws, stats.immediate_draws, stats.skipped_no_program,
		stats.skipped_no_target, stats.skipped_shader, stats.skipped_size);
	memset(&stats, 0, sizeof(stats));
	platform_log("stream hand-overs since the last note: present %lu, read table full %lu, a write into a range a draw reads %lu, stream full %lu, frees %lu, resource in use %lu, IsBusy %lu, Kick %lu, every 40 draws %lu",
		flush_reasons[0], flush_reasons[1], flush_reasons[2], flush_reasons[3], flush_reasons[4], flush_reasons[5], flush_reasons[6], flush_reasons[7], flush_reasons[8]);
	memset(flush_reasons, 0, sizeof(flush_reasons));
	{
		unsigned index, shown = 0;

		/* (the five most used, most first) */
		for (; shown < 5; shown++)
		{
			unsigned best = IMMEDIATE_SITES;

			for (index = 0; index < IMMEDIATE_SITES; index++)
				if (immediate_sites[index].draws && (best == IMMEDIATE_SITES || immediate_sites[index].draws > immediate_sites[best].draws))
					best = index;
			if (best == IMMEDIATE_SITES)
				break;
			platform_log("immediate draws from %p: %lu", (void *)immediate_sites[best].site, immediate_sites[best].draws);
			immediate_sites[best].draws = 0;
		}
		memset(immediate_sites, 0, sizeof(immediate_sites));
	}
	if (profile.frames)
	{
		platform_log("frame time: %.1f ms of game and rendering work, of which %.1f in the Direct3D layer's draws, %.1f in Present; longest work %.0f, longest present %.0f; "
			"%lu of %lu frames over 17 ms, %lu over 34", profile.work_ms / (double)profile.frames, ticks_ms(profile.draw_ticks) / (double)profile.frames,
			profile.present_ms / (double)profile.frames, profile.longest_work_ms, profile.longest_present_ms, profile.over_17, profile.frames, profile.over_34);
		platform_log("frame parts: the game's ticks %.1f ms a frame (%lu ticks in %lu of %lu frames, %.1f ms a tick, longest frame's ticks %.1f ms), rendering %.1f ms a frame",
			sum_tick_ms / (double)profile.frames, sum_ticks, frames_with_ticks, profile.frames, sum_ticks ? sum_tick_ms / (double)sum_ticks : 0.0, longest_tick_ms,
			sum_render_ms / (double)profile.frames);
		platform_log("stream slots: the game thread waited for a free one %.2f ms a frame in %.2f waits a frame in the draws and the frame's work, and %.2f ms in %.2f waits a frame in the present",
			ticks_ms(slot_wait_ticks[0]) / (double)profile.frames, (double)slot_waits[0] / (double)profile.frames, ticks_ms(slot_wait_ticks[1]) / (double)profile.frames,
			(double)slot_waits[1] / (double)profile.frames);
		slot_wait_ticks[0] = slot_wait_ticks[1] = 0;
		slot_waits[0] = slot_waits[1] = 0;
		sum_tick_ms = sum_render_ms = longest_tick_ms = 0.0;
		sum_ticks = frames_with_ticks = 0;
		memset(&profile, 0, sizeof(profile));
	}
}

static void D3DDevice_Present_inner(CONST RECT *source_rectangle, CONST RECT *destination_rectangle, void *unused, void *unused2);

/* input.wiiu_gamepad_view: what the GamePad shows of the back buffer (wiiu_gx2.c's blit_to), x, y, width and height in its pixels at the
frame's scale (drawn: the back buffer's, at dynamic resolution's step). "tv": what the TV shows, all the frame drew (a width of 0).
"player1": the first window while the screen is split for the local players; the rest of the time all of it too, and while a menu has the
screen, whose taps on the GamePad are read against the whole picture (halo_ui_pointer_update). It costs no drawing: the GamePad's blit
samples a part of the frame instead of all of it. */
static void gamepad_view_make(const float drawn[2], uint32_t gamepad[4])
{
	static int player1 = -1;
	static unsigned long read_at;
	short bounds[4];
	long x0, y0, x1, y1;

	gamepad[0] = gamepad[1] = gamepad[2] = gamepad[3] = 0;
	if (player1 < 0 || read_at != config_changes())
	{
		read_at = config_changes();
		player1 = !strcmp(config_string("input.wiiu_gamepad_view"), "player1");
	}
	if (!player1 || port_menus_active() || gamepad_window_bounds(bounds) <= 1)
		return;
	x0 = (long)floorf(bounds[0] * drawn[0] + 0.5f);
	y0 = (long)floorf(bounds[1] * drawn[1] + 0.5f);
	x1 = (long)floorf(bounds[2] * drawn[0] + 0.5f);
	y1 = (long)floorf(bounds[3] * drawn[1] + 0.5f);
	if (x0 < 0 || y0 < 0 || x1 <= x0 || y1 <= y0)
		return;
	gamepad[0] = (uint32_t)x0;
	gamepad[1] = (uint32_t)y0;
	gamepad[2] = (uint32_t)(x1 - x0);
	gamepad[3] = (uint32_t)(y1 - y0);
}

void WINAPI D3DDevice_Present(CONST RECT *source_rectangle, CONST RECT *destination_rectangle,
	void *unused, void *unused2)
{
	const double start = profile_now_ms();
	double work = profile.last_exit_ms ? start - profile.last_exit_ms : 0.0, present;

	in_present = 1;
	D3DDevice_Present_inner(source_rectangle, destination_rectangle, unused, unused2);
	in_present = 0;
	present = profile_now_ms() - start;
	profile.last_exit_ms = profile_now_ms();
	profile.present_ms += present;
	profile.work_ms += work;
	/* a frame whose work took more than two refreshes: where it went (the draws, waiting for the GX2 thread, waiting for the renderer to be idle; the rest is the game's) */
	{
		static double waits_before, idle_before;
		static unsigned long idle_calls_before;
		static OSTime slot_wait_before;

		if (work > 34.0)
			platform_log("hitch: frame %lu took %.1f ms of work: %.1f in the game's %lu ticks, %.1f rendering (%.1f of it in the Direct3D layer's draws, %.1f of those for a free stream slot), %.1f waiting for the GX2 thread's "
				"streams, %.1f in %lu waits for the renderer to be idle; present %.1f", device.frame, work, frame_tick_ms, frame_ticks, frame_render_ms, ticks_ms(frame_draw_ticks),
				ticks_ms(slot_wait_ticks[0] - slot_wait_before), waits_ms - waits_before, idle_ms - idle_before, idle_calls - idle_calls_before, present);
		/* dynamic quality (render_governor.c): the frame's work, and how it divides */
		render_governor_frame(work, frame_tick_ms, frame_render_ms, start);
		sum_tick_ms += frame_tick_ms;
		sum_render_ms += frame_render_ms;
		sum_ticks += frame_ticks;
		frames_with_ticks += frame_ticks ? 1 : 0;
		if (frame_tick_ms > longest_tick_ms)
			longest_tick_ms = frame_tick_ms;
		frame_tick_ms = frame_render_ms = 0.0;
		frame_ticks = 0;
		frame_draw_ticks = 0;
		waits_before = waits_ms;
		idle_before = idle_ms;
		idle_calls_before = idle_calls;
		slot_wait_before = slot_wait_ticks[0];
	}
	if (work > profile.longest_work_ms)
		profile.longest_work_ms = work;
	if (present > profile.longest_present_ms)
		profile.longest_present_ms = present;
	profile.frames++;
	if (work + present > 17.5)
		profile.over_17++;
	if (work + present > 34.0)
		profile.over_34++;
}

static void D3DDevice_Present_inner(CONST RECT *source_rectangle, CONST RECT *destination_rectangle, void *unused, void *unused2)
{
	(void)source_rectangle;
	(void)destination_rectangle;
	(void)unused;
	(void)unused2;
	/* Not at the first frame: the rasterizer presents once as it starts,
	before it makes the game's vertex shaders (rasterizer_initialize), and a
	dump then has none. A second's frames on, it has made them all - which
	is also when the shader cache's pass can start (dk_shaders.c) */
	if (!shaders_dumped && device.frame >= 60)
	{
		shaders_dumped = TRUE;
		shaders_dump();
		dk_shader_start();
	}
	/* the startup pass, a few keys a frame (dk_shaders.c) */
	dk_shader_frame();
	/* the hash watch of the pages the texture cache reads from (web_memory_watch.c: no page protection here) looks again once a frame */
	memory_watch_begin_frame();
	if (screenshot_every < 0)
		screenshot_every = config_integer("debug.screenshot_every");
	if (statistics_enabled < 0)
		statistics_enabled = config_boolean("debug.gpu_stats");
	if (statistics_enabled > 0 && device.frame % 60 == 0)
		draw_statistics_log();
	{
		struct dk_command_present *command = stream_command(DK_COMMAND_PRESENT, sizeof(*command));
		float scale[2];

		surface_describe(&device.back_buffer, FALSE, &command->back_buffer, scale);
		gamepad_view_make(scale, command->gamepad);
		command->screenshot = 0;
		/* (for dynamic resolution's controller, which runs where the frame's GPU time is measured, on the GX2 thread: whether the frame is a menu's,
		and the frame rate it has to keep) */
		command->flags = (port_menus_active() ? DK_PRESENT_MENUS : 0u) | (halo_interpolation_enabled() ? 0u : DK_PRESENT_THIRTY);
		if (screenshot_every > 0 && device.frame % (unsigned long)screenshot_every == 0 && command->back_buffer.width &&
			*config_string("debug.screenshot_directory"))
		{
			/* (the back buffer's pixels the frame drew, at dynamic resolution's step: the host reads them as it has them) */
			screenshot_width = command->back_buffer.active_width;
			screenshot_height = command->back_buffer.active_height;
			screenshot_pixels = malloc((unsigned long)screenshot_width * screenshot_height * 4);
			command->screenshot = (uint32_t)(uintptr_t)screenshot_pixels;
		}
	}
	flush_because(0);
	if (screenshot_pixels)
	{
		if (dk_async > 0)
			async_wait_retired((uint32_t)(submission - 1)); /* (the host writes the pixels as it runs the present) */
		screenshot_write();
		free(screenshot_pixels);
		screenshot_pixels = NULL;
	}
	/* the host starts the next frame with nothing bound */
	targets_check();
	targets_known = FALSE;
	states_resync();
	/* the stand-in window's swap: holds the frame to the display's rate
	until the host presents, after which its swapchain does */
	platform_video_swap();
	xgpu_texture_cache_begin_frame();
	device.frame++;
	platform_pump_events();

	pthread_mutex_lock(&vertical_blank_lock);
	/* the Xbox keeps at most two frames queued behind its 60 Hz display;
	with interpolation, frames come at the real display's rate instead */
	if (halo_interpolation_enabled())
	{
		flip_count++;
	}
	else
	{
		while (pending_flips >= 2)
			pthread_cond_wait(&vertical_blank_condition, &vertical_blank_lock);
		pending_flips++;
	}
	pthread_mutex_unlock(&vertical_blank_lock);
}

HRESULT WINAPI D3DDevice_PersistDisplay(void)
{
	return S_OK;
}
