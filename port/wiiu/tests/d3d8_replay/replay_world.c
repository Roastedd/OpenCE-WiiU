/*
REPLAY_WORLD.C

What the Wii U's Direct3D layer calls outside itself, for its replay on Linux (port/wiiu/tests/d3d8_replay/replay_main.c): the Xbox memory window and its
allocator, the tag cache, the settings, the time base, the menus' art, the high-res HUD's textures and the text's atlas, each made up and the same on
every run. Built as the platform layer is (the game's ABI and the Xbox headers).
*/

#include "replay.h"
#include "platform.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/* ---------- the trace's hash */

uint64_t replay_hash(uint64_t hash, const void *data, unsigned long size)
{
	const unsigned char *at = data;

	while (size--)
		hash = (hash ^ *at++) * 1099511628211ULL;
	return hash;
}

uint32_t replay_random(uint32_t *state)
{
	uint32_t x = *state;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return *state = x ? x : 1;
}

/* ---------- the window, as port/wiiu/src/wiiu_window.c makes it, and its blocks (xbox_memory_wiiu.c: a block given out is told to the memory watch) */

static unsigned long window_used;

int replay_window_make(void)
{
	void *window = mmap((void *)PLATFORM_CONTIGUOUS_BASE, PLATFORM_CONTIGUOUS_SIZE, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);

	if (window != (void *)PLATFORM_CONTIGUOUS_BASE)
	{
		fprintf(stderr, "replay: the window cannot be mapped at %08lx\n", (unsigned long)PLATFORM_CONTIGUOUS_BASE);
		return 0;
	}
	/* (the first page stays unused: a Data of 0 means none) */
	window_used = 4096;
	return 1;
}

void *platform_contiguous_alloc(unsigned long size, unsigned long alignment, unsigned long physical_address, DWORD protect)
{
	unsigned long at;

	(void)protect;
	if (physical_address != PLATFORM_ANY_PHYSICAL_ADDRESS)
		at = physical_address;
	else
	{
		if (alignment < 32)
			alignment = 32;
		at = (window_used + alignment - 1) & ~(alignment - 1);
	}
	if (at + size > REPLAY_TAG_CACHE_OFFSET)
	{
		fprintf(stderr, "replay: the window is full\n");
		exit(3);
	}
	if (at + size > window_used)
		window_used = at + size;
	memory_watch_forget(PLATFORM_PHYSICAL_TO_VIRTUAL(at), size);
	return PLATFORM_PHYSICAL_TO_VIRTUAL(at);
}

void platform_contiguous_free(void *address)
{
	/* (blocks are not given out again: the replay is short) */
	memory_watch_forget(address, 4096);
}

BOOL platform_is_contiguous(const void *address)
{
	return (unsigned long)address >= PLATFORM_CONTIGUOUS_BASE && (unsigned long)address < PLATFORM_CONTIGUOUS_BASE + PLATFORM_CONTIGUOUS_SIZE;
}

/* the tag cache: the window's top 32 MB but for its last 10 (TAG_CACHE_BYTES is 0x1600000 in d3d8_gx2.c) */
void *physical_memory_get_tag_cache_base_address(void)
{
	return PLATFORM_PHYSICAL_TO_VIRTUAL(REPLAY_TAG_CACHE_OFFSET);
}

void *halo_custom_edition_tag_cache(void)
{
	return NULL;
}

/* the layer's note of the card's files (xbox_files.c's counters, which the replay does not link): none were read */
void platform_file_io_snapshot(struct platform_file_io *out)
{
	memset(out, 0, sizeof(*out));
}

/* ---------- the log: counted, and the lines that say a state was found changed with no setter are counted apart */

unsigned long replay_log_lines, replay_states_missed_lines, replay_targets_missed_lines;

void platform_log(const char *format, ...)
{
	char line[1024];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	replay_log_lines++;
	if (strstr(line, "found changed with no setter"))
		replay_states_missed_lines++;
	if (strstr(line, "with no SetRenderTarget"))
		replay_targets_missed_lines++;
	if (replay_verbose_log)
		fprintf(stderr, "log: %s\n", line);
}

int replay_verbose_log;

/* ---------- the settings the layer reads (port_config.c's defaults) */

const char *config_string(const char *name)
{
	if (!strcmp(name, "debug.gpu_texture_clip"))
		return "on";
	return "";
}

long config_integer(const char *name)
{
	(void)name;
	return 0;
}

int config_boolean(const char *name)
{
	(void)name;
	return 0;
}

unsigned long config_changes(void)
{
	return 1;
}

/* ---------- the time base: a hundred ticks a reading */

static OSTime system_time = 1000000;

OSTime OSGetSystemTime(void)
{
	system_time += 100;
	return system_time;
}

/* ---------- the rest of the platform: nothing to do */

/* the TV at 720 lines, as the console runs (the game draws 852x480 at 1280x720), until the game changes it */
long replay_screen_height = 720;
uint64_t replay_locks = REPLAY_HASH_INIT;

BOOL platform_screen_mode(long *width, long *height)
{
	*width = replay_screen_height * 16 / 9;
	*height = replay_screen_height;
	return 1;
}

BOOL platform_video_initialize(unsigned long width, unsigned long height)
{
	(void)width;
	(void)height;
	return 1;
}

void platform_video_swap(void)
{
}

void platform_pump_events(void)
{
}

void platform_menus_set_active(BOOL active)
{
	(void)active;
}

void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size)
{
	snprintf(host_path, host_path_size, "/nonexistent/%s", xbox_path);
}

int halo_interpolation_enabled(void)
{
	return 1;
}

/* hud_hires_tags.c's: no menu has the screen, so dynamic resolution's controller would run (it is off: host_gx2_drs_lines answers no step) */
int port_menus_active(void)
{
	return 0;
}

/* gamepad_window.c's: one window, the whole screen, so the GamePad shows the TV's picture */
short gamepad_window_bounds(short bounds[4])
{
	bounds[0] = bounds[1] = 0;
	bounds[2] = 640;
	bounds[3] = 480;
	return 1;
}

/* wiiu_gx2.c's: dynamic resolution asks no step, so the frames are drawn at the most lines, as they were before it */
unsigned int host_gx2_drs_lines(void)
{
	return 0;
}

int posix_make_directory(const char *path)
{
	(void)path;
	return 0;
}

void wiiu_foreground_gate(int is_game_thread)
{
	(void)is_game_thread;
}

void wiiu_thread_off_game_core(void)
{
}

int wiiu_input_touch(float *x, float *y)
{
	*x = *y = 0.0f;
	return 0;
}

void touch_menu_init(struct touch_menu *menu, const struct touch_menu_settings *settings)
{
	(void)menu;
	(void)settings;
}

void touch_menu_reset(struct touch_menu *menu)
{
	(void)menu;
}

void touch_menu_down(struct touch_menu *menu, unsigned long long finger, float x, float y)
{
	(void)menu;
	(void)finger;
	(void)x;
	(void)y;
}

void touch_menu_move(struct touch_menu *menu, unsigned long long finger, float x, float y)
{
	(void)menu;
	(void)finger;
	(void)x;
	(void)y;
}

void touch_menu_up(struct touch_menu *menu, unsigned long long finger, float x, float y)
{
	(void)menu;
	(void)finger;
	(void)x;
	(void)y;
}

void touch_menu_read(struct touch_menu *menu, struct touch_menu_output *output)
{
	(void)menu;
	memset(output, 0, sizeof(*output));
}

/* the shader cache's guest half (gx2_shaders.c): the keys a draw needs are found by the GX2 thread, which the replay does not have; nothing here */
void dk_shader_program_note(uint64_t program_hash, const uint32_t *words, unsigned long count)
{
	(void)program_hash;
	(void)words;
	(void)count;
}

void dk_shader_map_loaded(uint32_t map_hash)
{
	(void)map_hash;
}

void dk_shader_start(void)
{
}

void dk_shader_frame(void)
{
}

char *nv2a_dk_vertex_shader_to_glsl(const uint32_t *instructions, unsigned long instruction_count, unsigned long packed_attribute_mask)
{
	(void)instructions;
	(void)instruction_count;
	(void)packed_attribute_mask;
	return calloc(1, 1);
}

char *nv2a_dk_pixel_shader_to_glsl(const struct nv2a_pixel_shader_key *key)
{
	(void)key;
	return calloc(1, 1);
}

char *_strdup(const char *text)
{
	char *copy = malloc(strlen(text) + 1);

	if (copy)
		strcpy(copy, text);
	return copy;
}

/* ---------- the high-res HUD (hud_hires.h): one texture, standing for the replay's HUD placeholder and for the bitmap at replay_hud_override_address */

static const unsigned int hud_png[4] = { 0x89504e47, 1, 2, 3 };
const struct hud_hires_embedded hud_hires_embedded[1] =
{
	{ .tag = "replay\\hud", .bitmap = 0, .width = 16, .height = 16, .png = hud_png, .png_size = sizeof(hud_png) },
};
const unsigned int hud_hires_embedded_count = 1;

unsigned long replay_hud_placeholder_data, replay_hud_override_address;
static unsigned long hud_placeholder_serial = 1;

/* the placeholder registered for another texture (hud_hires_register_placeholder) */
void replay_hud_placeholder_move(unsigned long data)
{
	replay_hud_placeholder_data = data;
	hud_placeholder_serial++;
}

unsigned long hud_hires_placeholder_serial(void)
{
	return hud_placeholder_serial;
}

/* (a PNG is decoded to its size's texels, made from its first word: the replay's PNGs are only told apart) */
unsigned char *hud_hires_png_pixels(const void *png, unsigned long size, unsigned long *width, unsigned long *height)
{
	unsigned char *pixels;
	unsigned long index;
	uint32_t seed = 0x1234567u ^ (uint32_t)size ^ *(const uint32_t *)png;

	*width = 16;
	*height = 8;
	pixels = malloc(16 * 8 * 4);
	if (!pixels)
		return NULL;
	for (index = 0; index < 16 * 8 * 4; index++)
		pixels[index] = (unsigned char)replay_random(&seed);
	return pixels;
}

long hud_hires_override_find(unsigned long address, unsigned long width, unsigned long height, unsigned long level0_size)
{
	(void)width;
	(void)height;
	(void)level0_size;
	return address && address == replay_hud_override_address ? 0 : -1;
}

int hud_hires_override_coverage(long asset)
{
	(void)asset;
	return 0;
}

long hud_hires_placeholder_asset(unsigned long data)
{
	return data && data == replay_hud_placeholder_data ? 0 : -1;
}

/* ---------- the menus' art (menu_files.h): one picture, for the texture whose Data is replay_menu_data while replay_menu_serial is odd */

unsigned long replay_menu_data, replay_menu_serial = 2;
static const unsigned int menu_png[4] = { 0x89504e47, 7, 8, 9 };

const char *menu_art_name(unsigned long data)
{
	return data && data == replay_menu_data && (replay_menu_serial & 1) ? "replay.png" : NULL;
}

const unsigned char *menu_art_png(const char *name, unsigned long *size)
{
	(void)name;
	*size = sizeof(menu_png);
	return (const unsigned char *)menu_png;
}

unsigned long menu_art_serial(void)
{
	return replay_menu_serial;
}

/* ---------- the text's atlas (text_hires.h): rows the game marks drawn into, handed over when the texture cache asks */

unsigned long replay_atlas_data;
static unsigned char atlas[REPLAY_ATLAS_SIZE * REPLAY_ATLAS_SIZE];
static long atlas_dirty_top = 0, atlas_dirty_bottom = REPLAY_ATLAS_SIZE;

void replay_atlas_draw(uint32_t *seed)
{
	const long top = (long)(replay_random(seed) % (REPLAY_ATLAS_SIZE - 8)), rows = 1 + (long)(replay_random(seed) % 8);
	long index;

	for (index = top * REPLAY_ATLAS_SIZE; index < (top + rows) * REPLAY_ATLAS_SIZE; index++)
		atlas[index] = (unsigned char)replay_random(seed);
	if (top < atlas_dirty_top)
		atlas_dirty_top = top;
	if (top + rows > atlas_dirty_bottom)
		atlas_dirty_bottom = top + rows;
}

const unsigned char *text_hires_atlas_rows(unsigned long data, long *size, int first, long *top, long *bottom)
{
	if (!data || data != replay_atlas_data)
		return NULL;
	*size = REPLAY_ATLAS_SIZE;
	if (first)
	{
		*top = 0;
		*bottom = REPLAY_ATLAS_SIZE;
	}
	else
	{
		*top = atlas_dirty_top;
		*bottom = atlas_dirty_bottom;
	}
	atlas_dirty_top = REPLAY_ATLAS_SIZE;
	atlas_dirty_bottom = 0;
	return atlas;
}
