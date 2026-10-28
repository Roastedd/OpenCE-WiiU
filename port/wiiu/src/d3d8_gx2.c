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
#include "dk_textures.h"
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

/* the display's pixels for each of the 480 lines (text_hires.c) */
float halo_screen_pixel_scale(void)
{
	halo_screen_width();
	return screen_scale[1];
}

/* how many pixels the screen's targets draw to the Xbox's one, the larger
of the two ways (the screen effects' convolutions: rasterizer_xbox_screen_effect.c) */
float halo_screen_scale(void)
{
	halo_screen_width();
	return screen_scale[0] > screen_scale[1] ? screen_scale[0] : screen_scale[1];
}

void halo_screen_ui_offset(unsigned char centered)
{
	ui_offset = centered ? (halo_screen_width() - 640) / 2 : 0;
}

/* display.shadow_resolution, display.per_pixel_lighting and
display.anti_aliasing are the OpenGL renderer's (d3d8_gl.c): here the shadow
maps stay the Xbox's 128x128, every draw is lit as its vertex shader lights
it, and the 3D view is drawn as it is */
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

/* the buffers the stream is written into: one when the host runs a stream as it is handed over, STREAM_SLOTS when the GX2 thread runs them while the game goes on (host_gx2_async) */
#define STREAM_SLOTS 3
static uint32_t stream_buffers[STREAM_SLOTS][STREAM_SIZE / 4];
static uint32_t *stream = stream_buffers[0];
static unsigned long stream_used;

/* the queue of streams to the GX2 thread, and its buffers: the host (wiiu_gx2.c) takes from them */
struct stream_queue dk_stream_queue;

uint32_t *dk_stream_buffer(unsigned slot)
{
	return stream_buffers[slot];
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
			stream_queue_init(&dk_stream_queue, STREAM_SLOTS);
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
static double frame_draw_ms; /* this frame's time in the Direct3D layer's draws */
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
	draws_since_handover = 0;
	if (async_on())
	{
		if (!stream_used)
			return;
		wiiu_foreground_gate(1);
		stream_queue_post(&dk_stream_queue, (uint32_t)stream_used);
		stream_used = 0;
		submission++;
		stream = stream_buffers[stream_queue_slot(&dk_stream_queue)];
		/* (the slot to write next must be free: the stream that used it is retired) */
		while (!stream_queue_slot_free(&dk_stream_queue))
			host_gx2_pause();
		async_frees(0);
		pending_free_bytes = 0;
		return;
	}
	pending_reads_clear(&pending_reads);
	if (!stream_used)
		return;
	host_gx2_submit((unsigned int)(uintptr_t)stream, (unsigned int)stream_used);
	stream_used = 0;
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
	if (stream_used + size > STREAM_SIZE)
		flush_because(3);
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
the screen's scale for the screen's targets, else 1. Viewports, clears and
visibility counts are scaled by it */
static float target_scale[2] = { 1.0f, 1.0f };

/* a surface's scale: the screen's, if it is the screen's size */
static void target_scale_of(unsigned long width, unsigned long height, float scale[2])
{
	scale[0] = scale[1] = 1.0f;
	if (width == (unsigned long)halo_screen_width() && height == SCREEN_HEIGHT)
	{
		scale[0] = screen_scale[0];
		scale[1] = screen_scale[1];
	}
}

/* a surface as the host knows it; depth_only: NONE unless it is a depth
format (a depth buffer that is not one is left unbound, as d3d8_gl.c does) */
static void surface_describe(const D3DSurface *surface, BOOL depth_only, struct dk_surface *out, float scale[2])
{
	unsigned long width, height;
	BOOL depth;

	memset(out, 0, sizeof(*out));
	scale[0] = scale[1] = 1.0f;
	if (!surface || !surface->Data)
		return;
	surface_dimensions(surface, &width, &height, &depth);
	if (depth_only && !depth)
		return;
	target_scale_of(width, height, scale);
	out->data = surface->Data;
	out->width = (uint32_t)width;
	out->height = (uint32_t)height;
	out->kind = depth ? DK_SURFACE_DEPTH : DK_SURFACE_COLOR;
	out->pixel_width = (uint32_t)(width * scale[0] + 0.5f);
	out->pixel_height = (uint32_t)(height * scale[1] + 0.5f);
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
	}
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

/* tells the host the current targets, if they are not what it has; FALSE if
there is nothing to draw into. has_depth (if not NULL) says whether there is
a depth buffer. */
static BOOL targets_bind(BOOL *has_depth)
{
	struct dk_command_targets targets;
	float color_scale[2], depth_scale[2];

	memset(&targets, 0, sizeof(targets));
	surface_describe(device.render_target, FALSE, &targets.color, color_scale);
	surface_describe(device.depth_stencil, TRUE, &targets.depth, depth_scale);
	if (targets.color.kind == DK_SURFACE_NONE && targets.depth.kind == DK_SURFACE_NONE)
		return FALSE;
	target_scale[0] = targets.color.kind != DK_SURFACE_NONE ? color_scale[0] : depth_scale[0];
	target_scale[1] = targets.color.kind != DK_SURFACE_NONE ? color_scale[1] : depth_scale[1];
	if (has_depth)
		*has_depth = targets.depth.kind != DK_SURFACE_NONE;
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
#ifndef HALO_ANDROID
		if (device.created)
		{
			device.presentation.BackBufferWidth = (UINT)width;
			d3d8_surface_resize(&device.back_buffer, D3DFMT_LIN_A8R8G8B8, (unsigned long)width, SCREEN_HEIGHT);
			d3d8_surface_resize(&device.depth_buffer, D3DFMT_LIN_D24S8, (unsigned long)width, SCREEN_HEIGHT);
		}
#endif
	}
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

static void draw_resources_used(BOOL indexed, BOOL streamed)
{
	unsigned long index;
	int stage;

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

void D3DFASTCALL D3DDevice_SetRenderState_Simple(DWORD method, DWORD value)
{
	/* callers also store the value in D3D__RenderState themselves */
	(void)method;
	(void)value;
}

void D3DFASTCALL D3DDevice_SetRenderState_Deferred(D3DRENDERSTATETYPE state, DWORD value)
{
	if ((unsigned long)state < D3DRS_MAX)
		D3D__RenderState[state] = value;
}

void WINAPI D3DDevice_SetRenderState_ZBias(DWORD value);

void WINAPI D3DDevice_SetRenderStateNotInline(D3DRENDERSTATETYPE state, DWORD value)
{
	if (state == D3DRS_ZBIAS)
		D3DDevice_SetRenderState_ZBias(value);
	else if ((unsigned long)state < D3DRS_MAX)
		D3D__RenderState[state] = value;
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

	memcpy(&D3D__RenderState[D3DRS_POLYGONOFFSETZSLOPESCALE], &slope, sizeof(slope));
	memcpy(&D3D__RenderState[D3DRS_POLYGONOFFSETZOFFSET], &offset, sizeof(offset));
	D3D__RenderState[D3DRS_POINTOFFSETENABLE] = enable;
	D3D__RenderState[D3DRS_WIREFRAMEOFFSETENABLE] = enable;
	D3D__RenderState[D3DRS_SOLIDOFFSETENABLE] = enable;
	D3D__RenderState[D3DRS_ZBIAS] = value;
}

#define COMPLEX_RENDER_STATE(name, state) \
	void WINAPI D3DDevice_SetRenderState_##name(DWORD value) { D3D__RenderState[state] = value; }

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
	if (stage < D3DTSS_MAXSTAGES && (unsigned long)type < D3DTSS_MAX)
		D3D__TextureState[stage][type] = value;
}

void WINAPI D3DDevice_SetTextureState_TexCoordIndex(DWORD stage, DWORD value)
{
	if (stage < D3DTSS_MAXSTAGES)
		D3D__TextureState[stage][D3DTSS_TEXCOORDINDEX] = value;
}

void WINAPI D3DDevice_SetTextureState_BorderColor(DWORD stage, DWORD value)
{
	if (stage < D3DTSS_MAXSTAGES)
		D3D__TextureState[stage][D3DTSS_BORDERCOLOR] = value;
}

void WINAPI D3DDevice_SetTextureState_ColorKeyColor(DWORD stage, DWORD value)
{
	if (stage < D3DTSS_MAXSTAGES)
		D3D__TextureState[stage][D3DTSS_COLORKEYCOLOR] = value;
}

void WINAPI D3DDevice_SetTextureState_BumpEnv(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value)
{
	if (stage < D3DTSS_MAXSTAGES && (unsigned long)type < D3DTSS_MAX)
		D3D__TextureState[stage][type] = value;
}

void WINAPI D3DDevice_SetTexture(DWORD stage, D3DBaseTexture *texture)
{
	if (stage < D3DTSS_MAXSTAGES)
		device.textures[stage] = texture;
}

void WINAPI D3DDevice_SetPalette(DWORD stage, D3DPalette *palette)
{
	if (stage < D3DTSS_MAXSTAGES)
		device.palettes[stage] = palette;
}

void WINAPI D3DDevice_SetPixelShaderProgram(D3DPIXELSHADERDEF *definition)
{
	/* the definition's members are the pixel shader render states */
	if (!definition)
		return;
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
}

void WINAPI D3DDevice_SetIndices(D3DIndexBuffer *index_data, UINT base_vertex_index)
{
	device.base_vertex_index = base_vertex_index;
	device.index_buffer = index_data;
	/* an index buffer's Data is a window address, not an offset within one:
	from a map file it is the address the window was linked at, and from
	CreateIndexBuffer ordinary memory, which the move leaves alone */
	D3D__IndexData = index_data ? (WORD *)PORT_WINDOW_REBASE(index_data->Data) : NULL;
}
/* where a frame's time goes on this CPU (debug.gpu_stats): the Direct3D layer's draws, the present (the stream's hand-over, the GPU's wait and the display's), and the rest */
static struct
{
	double draw_ms, present_ms, work_ms, longest_work_ms, longest_present_ms;
	double last_exit_ms;
	unsigned long frames, over_17, over_34;
} profile;

static double profile_now_ms(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1e6;
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
static struct dk_draw_state state_told;
static BOOL state_known;
static uint32_t shaders_told[2];
static BOOL shaders_known;
static struct dk_vertex_parameters vertex_parameters_told;
static struct dk_pixel_parameters pixel_parameters_told;
static BOOL parameters_known;
static struct dk_vertex_format format_told;
static BOOL format_known;
static struct dk_command_textures textures_told;
static BOOL textures_known;
static unsigned long long constants_told;

/* the state the uniform blocks above come from: when none of it has changed
they need no converting (most draws share it with the draw before) */
#define DRAW_UNIFORM_INPUT_COUNT (4 + 4 + 16 + 1 + 16 + 2 + 4 + 1 + 1 + 3 + 6 * D3DTSS_MAXSTAGES)

static DWORD draw_uniform_inputs[DRAW_UNIFORM_INPUT_COUNT];
static BOOL draw_uniform_inputs_known;

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

static uint32_t blend_equation(DWORD operation)
{
	switch (operation)
	{
	case D3DBLENDOP_SUBTRACT: return DK_BLEND_SUBTRACT;
	case D3DBLENDOP_REVSUBTRACT:
	case D3DBLENDOP_REVSUBTRACTSIGNED: return DK_BLEND_REVERSE_SUBTRACT;
	case D3DBLENDOP_MIN: return DK_BLEND_MIN;
	case D3DBLENDOP_MAX: return DK_BLEND_MAX;
	default: return DK_BLEND_ADD;
	}
}

/* d3d8_gl.c's apply_raster_state, as a description of what it would set */
static void draw_state_make(BOOL has_depth, struct dk_draw_state *state)
{
	DWORD *rs = D3D__RenderState;
	DWORD write = rs[D3DRS_COLORWRITEENABLE];
	BOOL depth_test = has_depth && rs[D3DRS_ZENABLE];

	memset(state, 0, sizeof(*state));
	state->viewport[0] = target_pixel((float)device.viewport.X, 0);
	state->viewport[1] = target_pixel((float)device.viewport.Y, 1);
	state->viewport[2] = target_pixel((float)(device.viewport.X + device.viewport.Width), 0) - state->viewport[0];
	state->viewport[3] = target_pixel((float)(device.viewport.Y + device.viewport.Height), 1) - state->viewport[1];
	state->depth_range[0] = device.viewport.MinZ;
	state->depth_range[1] = device.viewport.MaxZ;
	/* the game never issues a scissor rectangle, and the NV2A scissor register
	defaults to the viewport, so fragment clipping follows the viewport: this is
	what keeps a split-screen window's geometry from bleeding across the divider */
	memcpy(state->scissor, state->viewport, sizeof(state->scissor));

	state->depth_test = depth_test;
	if (depth_test)
	{
		state->depth_function = rs[D3DRS_ZFUNC] ? rs[D3DRS_ZFUNC] : D3DCMP_NEVER;
		state->depth_write = rs[D3DRS_ZWRITEENABLE] != 0;
	}

	state->stencil_test = has_depth && rs[D3DRS_STENCILENABLE];
	if (state->stencil_test)
	{
		state->stencil_function = rs[D3DRS_STENCILFUNC] ? rs[D3DRS_STENCILFUNC] : D3DCMP_NEVER;
		state->stencil_reference = rs[D3DRS_STENCILREF];
		state->stencil_mask = rs[D3DRS_STENCILMASK];
		state->stencil_write_mask = rs[D3DRS_STENCILWRITEMASK];
		state->stencil_fail = rs[D3DRS_STENCILFAIL];
		state->stencil_depth_fail = rs[D3DRS_STENCILZFAIL];
		state->stencil_pass = rs[D3DRS_STENCILPASS];
	}

	state->blend = rs[D3DRS_ALPHABLENDENABLE] != 0;
	if (state->blend)
	{
		state->blend_source = rs[D3DRS_SRCBLEND];
		state->blend_destination = rs[D3DRS_DESTBLEND];
		state->blend_equation = blend_equation(rs[D3DRS_BLENDOP]);
		color_to_vec4(rs[D3DRS_BLENDCOLOR], state->blend_color);
	}
	state->color_mask = ((write & D3DCOLORWRITEENABLE_RED) ? 1 : 0) | ((write & D3DCOLORWRITEENABLE_GREEN) ? 2 : 0) |
		((write & D3DCOLORWRITEENABLE_BLUE) ? 4 : 0) | ((write & D3DCOLORWRITEENABLE_ALPHA) ? 8 : 0);

	/* the cull mode names the winding to discard; FRONTFACE names the front
	winding */
	if (rs[D3DRS_CULLMODE] != D3DCULL_NONE)
		state->cull = rs[D3DRS_CULLMODE] == rs[D3DRS_FRONTFACE] ? DK_CULL_FRONT : DK_CULL_BACK;
	state->front_face_ccw = rs[D3DRS_FRONTFACE] == D3DFRONT_CCW;
	state->fill_mode = rs[D3DRS_FILLMODE] == D3DFILL_WIREFRAME ? DK_FILL_LINE :
		rs[D3DRS_FILLMODE] == D3DFILL_POINT ? DK_FILL_POINT : DK_FILL_SOLID;

	/* D3DRS_ZBIAS is expressed in these states (D3DDevice_SetRenderState_ZBias) */
	state->offset_enable = rs[D3DRS_SOLIDOFFSETENABLE] != 0;
	if (state->offset_enable)
	{
		state->offset_slope = dword_to_float(rs[D3DRS_POLYGONOFFSETZSLOPESCALE]);
		state->offset_units = dword_to_float(rs[D3DRS_POLYGONOFFSETZOFFSET]);
	}
}

static void draw_state_send(BOOL has_depth)
{
	struct dk_draw_state state;

	draw_state_make(has_depth, &state);
	if (state_known && words_equal(&state, &state_told, sizeof(state)))
		return;
	{
		struct dk_command_state *command = stream_command(DK_COMMAND_STATE, sizeof(*command));

		command->state = state;
	}
	state_told = state;
	state_known = TRUE;
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

/* the uniforms that are not the vertex constants (d3d8_gl.c's
draw_uniforms), converted when the state they come from has changed, and
written when the host does not hold the result */
static void draw_parameters_send(float texture_scale[4][4])
{
	DWORD inputs[DRAW_UNIFORM_INPUT_COUNT];
	unsigned long count = 0;
	int stage;

	memcpy(&inputs[count], device.viewport_scale, sizeof(device.viewport_scale));
	count += 4;
	memcpy(&inputs[count], device.viewport_offset, sizeof(device.viewport_offset));
	count += 4;
	memcpy(&inputs[count], texture_scale, 16 * sizeof(float));
	count += 16;
	inputs[count++] = D3D__RenderState[D3DRS_POINTSIZE];
	for (stage = 0; stage < 8; stage++)
	{
		inputs[count++] = D3D__RenderState[D3DRS_PSCONSTANT0_0 + stage];
		inputs[count++] = D3D__RenderState[D3DRS_PSCONSTANT1_0 + stage];
	}
	inputs[count++] = D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT0];
	inputs[count++] = D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT1];
	inputs[count++] = D3D__RenderState[D3DRS_FOGCOLOR];
	inputs[count++] = D3D__RenderState[D3DRS_FOGSTART];
	inputs[count++] = D3D__RenderState[D3DRS_FOGEND];
	inputs[count++] = D3D__RenderState[D3DRS_FOGDENSITY];
	inputs[count++] = D3D__RenderState[D3DRS_ALPHAREF];
	inputs[count++] = (DWORD)UI_OFFSET;
	memcpy(&inputs[count], texture_clip, 3 * sizeof(float));
	count += 3;
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		DWORD *state = D3D__TextureState[stage];

		inputs[count++] = state[D3DTSS_BUMPENVMAT00];
		inputs[count++] = state[D3DTSS_BUMPENVMAT01];
		inputs[count++] = state[D3DTSS_BUMPENVMAT10];
		inputs[count++] = state[D3DTSS_BUMPENVMAT11];
		inputs[count++] = state[D3DTSS_BUMPENVLSCALE];
		inputs[count++] = state[D3DTSS_BUMPENVLOFFSET];
	}
	if (draw_uniform_inputs_known && words_equal(inputs, draw_uniform_inputs, sizeof(inputs)))
		return;
	memcpy(draw_uniform_inputs, inputs, sizeof(inputs));
	draw_uniform_inputs_known = TRUE;
	{
		struct dk_vertex_parameters vertex;
		struct dk_pixel_parameters pixel;

		memset(&vertex, 0, sizeof(vertex));
		memset(&pixel, 0, sizeof(pixel));
		memcpy(vertex.viewport_scale, device.viewport_scale, sizeof(vertex.viewport_scale));
		memcpy(vertex.viewport_offset, device.viewport_offset, sizeof(vertex.viewport_offset));
		vertex.point_and_screen[0] = D3D__RenderState[D3DRS_POINTSIZE] ?
			dword_to_float(D3D__RenderState[D3DRS_POINTSIZE]) : 1.0f;
		vertex.point_and_screen[1] = (float)UI_OFFSET;


		for (stage = 0; stage < 8; stage++)
		{
			color_to_vec4(D3D__RenderState[D3DRS_PSCONSTANT0_0 + stage], pixel.ps_c0[stage]);
			color_to_vec4(D3D__RenderState[D3DRS_PSCONSTANT1_0 + stage], pixel.ps_c1[stage]);
		}
		color_to_vec4(D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT0], pixel.ps_final_c0);
		color_to_vec4(D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT1], pixel.ps_final_c1);
		color_to_vec4(D3D__RenderState[D3DRS_FOGCOLOR], pixel.fog_color);
		pixel.fog_parameters[0] = dword_to_float(D3D__RenderState[D3DRS_FOGSTART]);
		pixel.fog_parameters[1] = dword_to_float(D3D__RenderState[D3DRS_FOGEND]);
		pixel.fog_parameters[2] = dword_to_float(D3D__RenderState[D3DRS_FOGDENSITY]);
		pixel.alpha_reference[0] = (float)(D3D__RenderState[D3DRS_ALPHAREF] & 0xff);
		/* (the clip, which the pixel shader makes before anything else: its
		limits in the units the stage is sampled in, 0 to 1) */
		pixel.alpha_reference[1] = texture_clip[0];
		pixel.alpha_reference[2] = texture_clip[1];
		pixel.alpha_reference[3] = texture_clip[2];
		memcpy(pixel.texture_scale, texture_scale, sizeof(pixel.texture_scale));
		for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
		{
			DWORD *state = D3D__TextureState[stage];

			pixel.bump_matrix[stage][0] = dword_to_float(state[D3DTSS_BUMPENVMAT00]);
			pixel.bump_matrix[stage][1] = dword_to_float(state[D3DTSS_BUMPENVMAT01]);
			pixel.bump_matrix[stage][2] = dword_to_float(state[D3DTSS_BUMPENVMAT10]);
			pixel.bump_matrix[stage][3] = dword_to_float(state[D3DTSS_BUMPENVMAT11]);
			pixel.bump_luminance[stage][0] = dword_to_float(state[D3DTSS_BUMPENVLSCALE]);
			pixel.bump_luminance[stage][1] = dword_to_float(state[D3DTSS_BUMPENVLOFFSET]);
		}
		if (!parameters_known || !words_equal(&vertex, &vertex_parameters_told, sizeof(vertex)))
		{
			struct dk_command_vertex_parameters *command = stream_command(DK_COMMAND_VERTEX_PARAMETERS, sizeof(*command));

			command->parameters = vertex;
			vertex_parameters_told = vertex;
		}
		if (!parameters_known || !words_equal(&pixel, &pixel_parameters_told, sizeof(pixel)))
		{
			struct dk_command_pixel_parameters *command = stream_command(DK_COMMAND_PIXEL_PARAMETERS, sizeof(*command));

			command->parameters = pixel;
			pixel_parameters_told = pixel;
		}
		parameters_known = TRUE;
	}
}

/* the shaders' keys, sent when they change (most draws repeat the last ones: a compare of the keys, not a hash and a search of the pack, on the game thread) */
static struct dk_vertex_key vertex_key_told;
static struct nv2a_pixel_shader_key pixel_key_told;
static BOOL keys_known;

static void draw_shader_keys_send(const struct dk_vertex_key *vertex, const struct nv2a_pixel_shader_key *pixel)
{
	struct dk_command_shader_keys *command;
	unsigned char *data;

	if (keys_known && words_equal(vertex, &vertex_key_told, sizeof(*vertex)) && words_equal(pixel, &pixel_key_told, sizeof(*pixel)))
		return;
	command = stream_command(DK_COMMAND_SHADER_KEYS, sizeof(*command) + sizeof(*vertex) + sizeof(*pixel));
	command->vertex_size = (uint32_t)sizeof(*vertex);
	command->pixel_size = (uint32_t)sizeof(*pixel);
	data = (unsigned char *)(command + 1);
	memcpy(data, vertex, sizeof(*vertex));
	memcpy(data + sizeof(*vertex), pixel, sizeof(*pixel));
	vertex_key_told = *vertex;
	pixel_key_told = *pixel;
	keys_known = TRUE;
	shaders_known = FALSE;
}

static void draw_shaders_send(uint32_t vertex, uint32_t pixel)
{
	if (shaders_known && shaders_told[0] == vertex && shaders_told[1] == pixel)
		return;
	{
		struct dk_command_shaders *command = stream_command(DK_COMMAND_SHADERS, sizeof(*command));

		command->vertex = vertex;
		command->pixel = pixel;
	}
	shaders_told[0] = vertex;
	shaders_told[1] = pixel;
	shaders_known = TRUE;
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

static uint32_t address_mode(DWORD mode)
{
	switch (mode)
	{
	case D3DTADDRESS_MIRROR: return DK_WRAP_MIRROR;
	case D3DTADDRESS_CLAMP:
	case D3DTADDRESS_CLAMPTOEDGE: return DK_WRAP_CLAMP;
	case D3DTADDRESS_BORDER: return DK_WRAP_BORDER;
	default: return DK_WRAP_REPEAT;
	}
}

/* what d3d8_gl.c's configure_sampler decides for a stage, as values for the
host's sampler. mipmapped: the texture has more than one level. hires: it is
a high-res replacement (xbox_textures_dk.c), drawn smaller than it is, so
filtered and from its mip levels whatever the game asks. */
static void sampler_make(int stage, BOOL mipmapped, BOOL hires, struct dk_sampler *sampler)
{
	DWORD *state = D3D__TextureState[stage];
	DWORD min_filter = hires ? D3DTEXF_LINEAR : state[D3DTSS_MINFILTER];
	DWORD mip_filter = hires ? D3DTEXF_LINEAR : mipmapped ? state[D3DTSS_MIPFILTER] : D3DTEXF_NONE;

	memset(sampler, 0, sizeof(*sampler));
	sampler->min_linear = min_filter != D3DTEXF_POINT;
	sampler->mag_linear = hires || state[D3DTSS_MAGFILTER] != D3DTEXF_POINT;
	sampler->mip_filter = mip_filter == D3DTEXF_NONE ? DK_MIP_NONE : mip_filter == D3DTEXF_POINT ? DK_MIP_NEAREST :
		DK_MIP_LINEAR;
	sampler->wrap[0] = address_mode(state[D3DTSS_ADDRESSU]);
	sampler->wrap[1] = address_mode(state[D3DTSS_ADDRESSV]);
	sampler->wrap[2] = address_mode(state[D3DTSS_ADDRESSW]);
	sampler->lod_bias = hires ? 0.0f : dword_to_float(state[D3DTSS_MIPMAPLODBIAS]);
	sampler->lod_minimum = hires ? 0.0f : (float)state[D3DTSS_MAXMIPLEVEL];
	sampler->anisotropy = (min_filter == D3DTEXF_ANISOTROPIC && state[D3DTSS_MAXANISOTROPY] > 1) ?
		(float)state[D3DTSS_MAXANISOTROPY] : 1.0f;
	color_to_vec4(state[D3DTSS_BORDERCOLOR], sampler->border);
}

/* The pixel shader's key as prepare_draw builds it, and with it what the
stages sample: every stage's texture is found here (finding one may send its
texels to the host), as bind_textures does. */
static void pixel_key_make(struct nv2a_pixel_shader_key *key, float texture_scale[4][4],
	struct dk_command_textures *textures)
{
	int stage;

	memset(key, 0, sizeof(*key));
	memset(textures, 0, sizeof(*textures));
	memcpy(key->combiner_state, D3D__RenderState, sizeof(key->combiner_state));
	/* constants are uniforms, not part of the program */
	memset(&key->combiner_state[D3DRS_PSCONSTANT0_0], 0, 16 * sizeof(DWORD));
	key->combiner_state[D3DRS_PSFINALCOMBINERCONSTANT0] = 0;
	key->combiner_state[D3DRS_PSFINALCOMBINERCONSTANT1] = 0;
	key->texture_modes = D3D__RenderState[D3DRS_PSTEXTUREMODES];
	if (texture_clip[2] != 0.0f)
		key->texture_modes |= NV2A_PIXEL_KEY_CLIP;
	if (warp_blend)
		key->texture_modes |= NV2A_PIXEL_KEY_WARP_BLEND;
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		D3DBaseTexture *texture = device.textures[stage];
		unsigned long mode = stage_texture_mode(stage);

		texture_scale[stage][0] = texture_scale[stage][1] = 1.0f;
		texture_scale[stage][2] = texture_scale[stage][3] = 1.0f;
		key->alpha_kill[stage] = D3D__TextureState[stage][D3DTSS_ALPHAKILL] == D3DTALPHAKILL_ENABLE;
		key->color_sign[stage] = (unsigned char)((D3D__TextureState[stage][D3DTSS_COLORSIGN] >> 28) & 0xf);
		if (!texture || !texture->Data || mode == 0 || mode == 0x04 || mode == 0x05 || mode == 0x11)
		{
			key->sampler_type[stage] = mode == 0x11 ? _xgpu_sampler_2d : _xgpu_sampler_none;
			continue;
		}
		{
			const D3DCOLOR *palette = device.palettes[stage] && device.palettes[stage]->Data ?
				(const D3DCOLOR *)PLATFORM_PHYSICAL_TO_VIRTUAL(device.palettes[stage]->Data) : NULL;
			struct xgpu_texture_description description;
			int kind = DK_TEXTURE_2D;

			const struct rendered *target = rendered_find(texture->Data);

			memset(&description, 0, sizeof(description));
			if (target)
			{
				/* a render target: the host's image of it */
				xgpu_texture_describe(texture->Format, texture->Size, &description);
				textures->stages[stage].target = texture->Data;
				/* (rendered a level at a time: the levels as one image, as
				bind_textures in d3d8_gl.c makes them) */
				if (!description.linear && !description.cube_map && description.levels > 1 &&
					description.levels <= DK_COMPOSITE_LEVELS && target->width == description.width &&
					target->height == description.height)
				{
					composite_tell(&description, texture->Data);
					textures->stages[stage].composite = (uint32_t)description.levels;
				}
				else
					description.levels = 1;
				if (description.linear)
				{
					texture_scale[stage][0] = 1.0f / (float)target->width;
					texture_scale[stage][1] = 1.0f / (float)target->height;
				}
			}
			else
			{
				textures->stages[stage].id = dk_texture_get((const DWORD *)texture, palette, &kind, &description);
				{
					static unsigned long logged;
					static unsigned long seen[64];
					unsigned long i;

					for (i = 0; i < logged && i < 64 && seen[i] != textures->stages[stage].id; i++)
						;
					if (i == logged && logged < 64)
					{
						seen[logged++] = textures->stages[stage].id;
						platform_log("texture %lu: format word %08lx size word %08lx: %lux%lux%lu, %lu levels, linear %d cube %d, kind %d, palette %d",
							(unsigned long)textures->stages[stage].id, (unsigned long)((const DWORD *)texture)[3], (unsigned long)((const DWORD *)texture)[4],
							(unsigned long)description.width, (unsigned long)description.height, (unsigned long)description.depth, (unsigned long)description.levels,
							(int)description.linear, (int)description.cube_map, kind, palette != NULL);
					}
				}
				if (description.linear)
				{
					texture_scale[stage][0] = 1.0f / (float)description.width;
					texture_scale[stage][1] = 1.0f / (float)description.height;
				}
			}
			if (stage == 0)
				key->coverage_alpha = description.hires_coverage != FALSE;
			key->sampler_type[stage] = kind == DK_TEXTURE_CUBE ? _xgpu_sampler_cube : kind == DK_TEXTURE_3D ?
				_xgpu_sampler_3d : _xgpu_sampler_2d;
			sampler_make(stage, description.levels > 1, description.hires, &textures->stages[stage].sampler);
		}
	}
	/* (only with the meter's blend: hud_hires.h, nv2a_pixel_shader_key) */
	key->coverage_alpha = key->coverage_alpha && D3D__RenderState[D3DRS_ALPHABLENDENABLE] &&
		D3D__RenderState[D3DRS_SRCBLEND] == D3DBLEND_CONSTANTCOLOR &&
		D3D__RenderState[D3DRS_DESTBLEND] == D3DBLEND_SRCALPHA;
	key->alpha_test_function = D3D__RenderState[D3DRS_ALPHATESTENABLE] ? D3D__RenderState[D3DRS_ALPHAFUNC] : 0;
	key->fog_enable = D3D__RenderState[D3DRS_FOGENABLE] != 0;
	key->fog_table_mode = (unsigned char)D3D__RenderState[D3DRS_FOGTABLEMODE];
}

static void draw_textures_send(const struct dk_command_textures *textures)
{
	if (textures_known && words_equal(&textures->stages, &textures_told.stages, sizeof(textures->stages)))
		return;
	{
		struct dk_command_textures *command = stream_command(DK_COMMAND_TEXTURES, sizeof(*command));

		memcpy(command->stages, textures->stages, sizeof(command->stages));
	}
	textures_told = *textures;
	textures_known = TRUE;
}

/* d3d8_gl.c's prepare_draw: everything a draw needs but its vertices, written
to the stream. FALSE if there is nothing to draw into, or a shader the draw
needs is not ready (skipped, as decided: DEKO3D.md, "Decisions"). */
static BOOL draw_prepare(BOOL immediate)
{
	struct vertex_shader_object *program = current_program();
	struct dk_vertex_key vertex_key;
	struct nv2a_pixel_shader_key pixel_key;
	struct dk_command_textures textures;
	float texture_scale[4][4];
	uint32_t vertex_shader, pixel_shader;
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
	pixel_key_make(&pixel_key, texture_scale, &textures);
	memset(&vertex_key, 0, sizeof(vertex_key));
	vertex_key.program_hash = program->program_hash;
	vertex_key.packed_mask = immediate ? 0 : (uint32_t)device.vertex_shader->packed_mask;
	(void)vertex_shader;
	(void)pixel_shader;
	(void)draw_shaders_send;
	if (immediate)
		stats.immediate_draws++;
	else
		stats.draws++;
	draw_state_send(has_depth);
	/* (the keys go to the host's thread, which finds the shaders in the pack and skips a draw whose shader it has not: GX2's 'no shader' count) */
	draw_shader_keys_send(&vertex_key, &pixel_key);
	draw_constants_send();
	draw_parameters_send(texture_scale);
	draw_textures_send(&textures);
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

/* whether every stream the format reads is the map's own vertex data in the tag cache, which the game does not write while it draws (a BSP switch loads into it,
and announces it): a draw of those leaves its indices' range to the host's thread (DK_DRAW_RANGE_LATER) */
static BOOL streams_in_tag_cache(unsigned long first)
{
	const unsigned char *base = (const unsigned char *)physical_memory_get_tag_cache_base_address();
	unsigned long stream;

	if (!base)
		return FALSE;
	for (stream = 0; stream < DK_STREAM_COUNT; stream++)
	{
		const unsigned char *at;

		if (!((format_told.stream_mask >> stream) & 1))
			continue;
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
	unsigned long stream, streams = 0, slot = 0;
	WORD *long_copy = NULL;
	unsigned long index_bytes = indices && count * sizeof(WORD) <= INLINE_INDEX_LIMIT && !indices_fixed(indices, count) ?
		(count * sizeof(WORD) + 3) & ~3UL : 0;

	/* (a list the game made this frame, of the map's vertices: copied here, its range found by the host's thread, the copy and range together having cost the game
	thread 6-9% of its time in a10) */
	const BOOL range_later = indices && (index_bytes || indices_fixed(indices, count)) && streams_in_tag_cache(first);

	for (stream = 0; stream < DK_STREAM_COUNT; stream++)
		streams += (format_told.stream_mask >> stream) & 1;
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
		else if (!indices_fixed(indices, count) && (long_copy = malloc(count * sizeof(WORD))) != NULL)
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
	for (stream = 0; stream < DK_STREAM_COUNT; stream++)
	{
		unsigned long stride = device.streams[stream].stride;

		if (!((format_told.stream_mask >> stream) & 1))
			continue;
		command->streams[slot][0] = (uint32_t)(uintptr_t)PLATFORM_PHYSICAL_TO_VIRTUAL(device.streams[stream].data) +
			(uint32_t)(first * stride);
		command->streams[slot][1] = range_later ? (uint32_t)stride : (uint32_t)(stride ? stride * vertex_count : STRIDELESS_STREAM_BYTES);
		slot++;
	}
	/* (the memory the command reads when the stream is handed over is noted once the command is written: noting can hand the stream over when it has no room, and then the
	command has gone with it, safely) */
	for (stream = 0; stream < DK_STREAM_COUNT; stream++)
	{
		unsigned long stride = device.streams[stream].stride;

		if ((format_told.stream_mask >> stream) & 1)
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
	struct dk_vertex_format format;

	if (primitive_type == D3DPT_QUADLIST)
		vertex_count &= ~3U;
	if (!vertex_count || !draw_prepare(FALSE))
		return;
	format_from_declaration(&format);
	draw_format_send(&format);
	draw_write(primitive_type, vertex_count, start_vertex, vertex_count, NULL, 0);
}

void WINAPI D3DDevice_DrawVertices(D3DPRIMITIVETYPE primitive_type, UINT start_vertex, UINT vertex_count)
{
	const double start = profile_now_ms();

	D3DDevice_DrawVertices_inner(primitive_type, start_vertex, vertex_count);
	{
		const double spent = profile_now_ms() - start;

		profile.draw_ms += spent;
		frame_draw_ms += spent;
	}
}

static void D3DDevice_DrawIndexedVertices_inner(D3DPRIMITIVETYPE primitive_type, UINT vertex_count, CONST WORD *index_data)
{
	struct dk_vertex_format format;

	if (primitive_type == D3DPT_QUADLIST)
		vertex_count &= ~3U;
	if (!vertex_count || !index_data || !draw_prepare(FALSE))
		return;
	format_from_declaration(&format);
	draw_format_send(&format);
	draw_write(primitive_type, vertex_count, device.base_vertex_index, 0, index_data, 0);
}

void WINAPI D3DDevice_DrawIndexedVertices(D3DPRIMITIVETYPE primitive_type, UINT vertex_count, CONST WORD *index_data)
{
	const double start = profile_now_ms();

	D3DDevice_DrawIndexedVertices_inner(primitive_type, vertex_count, index_data);
	{
		const double spent = profile_now_ms() - start;

		profile.draw_ms += spent;
		frame_draw_ms += spent;
	}
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

void WINAPI D3DDevice_Begin(D3DPRIMITIVETYPE primitive_type)
{
	immediate_site_count((uintptr_t)__builtin_return_address(0));
	device.immediate_active = TRUE;
	device.immediate_skipped = device.visibility_test_active && !host_gx2_visibility_enabled();
	device.immediate_type = primitive_type;
	wiiu_immediate_begin(&device.immediate);
}

static void immediate_emit(void)
{
	if (device.immediate_skipped)
		return;
	wiiu_immediate_emit(&device.immediate, (const float (*)[4])device.attributes);
}

static void D3DDevice_End_inner(void)
{
	/* only the registers SetVertexData wrote go in the stream (each is four floats of a vertex, at a place of its own in it); the others are the current values, as the
	declaration's unfed registers are. Each vertex was kept with every register (immediate_emit). */
	unsigned long count = device.immediate.count, index, stride, touched = 0;
	D3DPRIMITIVETYPE type = device.immediate_type;
	unsigned long mask = device.immediate.mask;
	struct dk_vertex_format format;
	struct dk_command_draw *command;
	unsigned registers[XGPU_VERTEX_ATTRIBUTE_COUNT];

	device.immediate_active = FALSE;
	device.immediate.count = 0;
	if (type == D3DPT_QUADLIST)
		count &= ~3UL;
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
	memset(&format, 0, sizeof(format));
	{
		unsigned long place = 0;

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
	}
	format.strides[0] = (uint32_t)stride;
	format.stream_mask = 1;
	draw_format_send(&format);
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
	const double start = profile_now_ms();

	D3DDevice_End_inner();
	{
		const double spent = profile_now_ms() - start;

		profile.draw_ms += spent;
		frame_draw_ms += spent;
	}
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
			"%lu of %lu frames over 17 ms, %lu over 34", profile.work_ms / (double)profile.frames, profile.draw_ms / (double)profile.frames,
			profile.present_ms / (double)profile.frames, profile.longest_work_ms, profile.longest_present_ms, profile.over_17, profile.frames, profile.over_34);
		platform_log("frame parts: the game's ticks %.1f ms a frame (%lu ticks in %lu of %lu frames, %.1f ms a tick, longest frame's ticks %.1f ms), rendering %.1f ms a frame",
			sum_tick_ms / (double)profile.frames, sum_ticks, frames_with_ticks, profile.frames, sum_ticks ? sum_tick_ms / (double)sum_ticks : 0.0, longest_tick_ms,
			sum_render_ms / (double)profile.frames);
		sum_tick_ms = sum_render_ms = longest_tick_ms = 0.0;
		sum_ticks = frames_with_ticks = 0;
		memset(&profile, 0, sizeof(profile));
	}
}

static void D3DDevice_Present_inner(CONST RECT *source_rectangle, CONST RECT *destination_rectangle, void *unused, void *unused2);

void WINAPI D3DDevice_Present(CONST RECT *source_rectangle, CONST RECT *destination_rectangle,
	void *unused, void *unused2)
{
	const double start = profile_now_ms();
	double work = profile.last_exit_ms ? start - profile.last_exit_ms : 0.0, present;

	D3DDevice_Present_inner(source_rectangle, destination_rectangle, unused, unused2);
	present = profile_now_ms() - start;
	profile.last_exit_ms = profile_now_ms();
	profile.present_ms += present;
	profile.work_ms += work;
	/* a frame whose work took more than two refreshes: where it went (the draws, waiting for the GX2 thread, waiting for the renderer to be idle; the rest is the game's) */
	{
		static double waits_before, idle_before;
		static unsigned long idle_calls_before;

		if (work > 34.0)
			platform_log("hitch: frame %lu took %.1f ms of work: %.1f in the game's %lu ticks, %.1f rendering (%.1f of it in the Direct3D layer's draws), %.1f waiting for the GX2 thread's "
				"streams, %.1f in %lu waits for the renderer to be idle; present %.1f", device.frame, work, frame_tick_ms, frame_ticks, frame_render_ms, frame_draw_ms,
				waits_ms - waits_before, idle_ms - idle_before, idle_calls - idle_calls_before, present);
		sum_tick_ms += frame_tick_ms;
		sum_render_ms += frame_render_ms;
		sum_ticks += frame_ticks;
		frames_with_ticks += frame_ticks ? 1 : 0;
		if (frame_tick_ms > longest_tick_ms)
			longest_tick_ms = frame_tick_ms;
		frame_tick_ms = frame_render_ms = 0.0;
		frame_ticks = 0;
		frame_draw_ms = 0.0;
		waits_before = waits_ms;
		idle_before = idle_ms;
		idle_calls_before = idle_calls;
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
		command->screenshot = 0;
		if (screenshot_every > 0 && device.frame % (unsigned long)screenshot_every == 0 && command->back_buffer.width &&
			*config_string("debug.screenshot_directory"))
		{
			/* (the back buffer's pixels: the host reads them as it has them) */
			screenshot_width = command->back_buffer.pixel_width;
			screenshot_height = command->back_buffer.pixel_height;
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
	targets_known = FALSE;
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
