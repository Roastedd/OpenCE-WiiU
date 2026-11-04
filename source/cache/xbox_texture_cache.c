/*
XBOX_TEXTURE_CACHE.C

symbols in this file:
001AE410 0020:
	_texture_cache_delete (0000)
001AE430 0010:
	_texture_cache_open (0000)
001AE440 0010:
	_texture_cache_idle (0000)
001AE450 0090:
	_texture_cache_bitmap_new (0000)
001AE4E0 0040:
	_texture_cache_bitmap_delete (0000)
001AE520 00e0:
	_texture_cache_steal_memory (0000)
001AE600 0060:
	_texture_cache_return_memory (0000)
001AE660 0030:
	_texture_cache_name_block_proc (0000)
001AE690 0090:
	_bitmap_format_to_d3d_format (0000)
001AE720 0090:
	_bitmap_format_to_d3d_linear_format (0000)
001AE7B0 0030:
	_compare (0000)
001AE7E0 0010:
	_IDirect3DDevice8_IsBusy@4 (0000)
001AE7F0 0010:
	_IDirect3DDevice8_KickPushBuffer@4 (0000)
001AE800 0010:
	_IDirect3DBaseTexture8_IsBusy@4 (0000)
001AE810 0010:
	_IDirect3DBaseTexture8_Register@8 (0000)
001AE820 0020:
	_texture_cache_flush (0000)
001AE840 0040:
	_texture_cache_locked_block_proc (0000)
001AE880 00a0:
	_texture_cache_delete_block_proc (0000)
001AE920 0150:
	_texture_cache_initialize_hardware_format (0000)
001AEA70 0100:
	_render_inverse_transform_screen_point (0000)
001AEB70 00b0:
	_texture_cache_new (0000)
001AEC20 0050:
	_texture_cache_close (0000)
001AEC70 00e0:
	_texture_cache_start_loading_bitmap (0000)
001AED50 0310:
	_texture_cache_debug_render (0000)
001AF060 01f0:
	__texture_cache_bitmap_get_hardware_format (0000)
002A7BD0 0090:
	_bitmap_d3d_format_tables (0000)
002A7C60 002e:
	??_C@_0CO@EKKOPCBA@?$CBTEST_FLAG?$CIbitmap?9?$DOflags?0?5_bitma@ (0000)
002A7C90 002a:
	??_C@_0CK@BDDINKJM@c?3?2halo?2SOURCE?2cache?2xbox_textur@ (0000)
002A7CBC 002a:
	??_C@_0CK@MBCIMJKB@?$CBxbox_texture_cache_globals?4stol@ (0000)
002A7CE8 0017:
	??_C@_0BH@KHBOCJGF@remaining_page_count?$DO0?$AA@ (0000)
002A7D00 0029:
	??_C@_0CJ@MMJPDHMC@xbox_texture_cache_globals?4stole@ (0000)
002A7D2C 0014:
	??_C@_0BE@ILIIMHBE@table?$FLformat?$FN?$CB?$DNNONE?$AA@ (0000)
002A7D40 0030:
	??_C@_0DA@KFGIBCJF@texture?9?$DObitmap?9?$DOcache_block_ind@ (0000)
002A7D70 0028:
	??_C@_0CI@OLENPGPB@xbox_texture_cache_globals?4base_@ (0000)
002A7D98 0021:
	??_C@_0CB@NHIMPHNC@xbox_texture_cache_globals?4cache@ (0000)
002A7DBC 0013:
	??_C@_0BD@GPJOJANJ@xbox?5texture?5cache?$AA@ (0000)
002A7DD0 0024:
	??_C@_0CE@IHAAKOAF@xbox_texture_cache_globals?4textu@ (0000)
002A7DF4 000d:
	??_C@_0N@HJPPIHML@xbox?5texture?$AA@ (0000)
002A7E04 0025:
	??_C@_0CF@LJFHGILO@new_texture_index?$DN?$DNcache_block_i@ (0000)
002A7E2C 000b:
	??_C@_0L@PCGBDGNI@?$HMt?$CFd?$HMt?$CFs?$CFs?$AA@ (0000)
002A7E38 0044:
	??_C@_0EE@BCDFBEJK@YOU?5GOT?5STABBED?$CB?$CB?$CB?$CB?5double?9click@ (0000)
004D1198 1618:
	_texture_cache_debug_bitmaps (0000)
	_xbox_texture_cache_globals (1600)
	_texture_cache_debug_options (1610)
	_debug_texture_cache (1612)
*/

/* ---------- headers */

#include "cseries/cseries.h"
#include "cseries/cseries_windows.h"
#include "cseries/errors.h"
#include "cseries/sort.h"
#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmaps.h"
#include "cache/cache_files.h"
#include "cache/texture_cache.h"
#include "cache/physical_memory_map.h"
#include "interface/interface.h"
#include "interface/terminal.h"
#include "main/console.h"
#include "math/integer_math.h"
#include "memory/data.h"
#include "memory/lruv_cache.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_swizzle.h"
#include "rasterizer/xbox/rasterizer_xbox.h"
#include "rasterizer/xbox/rasterizer_xbox_internal.h"
#include "render/render.h"
#include "render/render_debug.h"
#include "scenario/scenario.h"
#include "sound/sound_manager.h"
#include "tag_files/tag_files.h"
#include "tag_files/tag_groups.h"
#include "text/draw_string.h"
#include <xtl.h>
#include "custom_edition_cache.h" /* port: port/linux/game/custom_edition_cache.c */
#include "halo_startup.h" /* port: port/linux/include/halo_startup.h */

#ifdef HALO_WIIU
void platform_log(char const *format, ...);
long wiiu_study_number(const char *name, long fallback);
#include "texture_stream.h" /* port: port/linux/game/texture_stream.h */
#endif

#ifdef HALO_ANDROID
#include "android_ce_texture_cache.h" /* port: port/linux/game/android_ce_texture_cache.c */
#include "../../port/android/include/android_render_policy.h" /* port: Android chip policy */
#endif

/* ---------- constants */

typedef char verify_xbox_texture_cache_size[
	HALO_PORT_TEXTURE_CACHE_SIZE == HALO_PORT_TEXTURE_CACHE_PAGE_COUNT * 0x4000 ? 1 : -1];

enum
{
	/* port: the native builds' larger cache (halo_port_capacity.h) */
	XBOX_TEXTURE_CACHE_PAGE_COUNT = HALO_PORT_TEXTURE_CACHE_PAGE_COUNT,
	XBOX_TEXTURE_CACHE_PAGE_SIZE_BITS = 14,
	XBOX_TEXTURE_CACHE_PAGE_SIZE = 1 << XBOX_TEXTURE_CACHE_PAGE_SIZE_BITS,
	XBOX_TEXTURE_CACHE_STEAL_GUARD_SIZE = 0x104000,
	XBOX_TEXTURE_CACHE_STEALABLE_PAGE_COUNT =
		XBOX_TEXTURE_CACHE_PAGE_COUNT -
		2 * (XBOX_TEXTURE_CACHE_STEAL_GUARD_SIZE / XBOX_TEXTURE_CACHE_PAGE_SIZE),
	XBOX_TEXTURE_CACHE_ENTRY_SIZE = 0x20,
	XBOX_TEXTURE_CACHE_SIZE = HALO_PORT_TEXTURE_CACHE_SIZE,
	XBOX_TEXTURE_CACHE_PROTECTION = 0x404,
};

enum
{
	_bitmap_type_2d,
	_bitmap_type_3d,
	_bitmap_type_cube_map,
	NUMBER_OF_BITMAP_TYPES,
};

enum
{
	_bitmap_format_a8,
	_bitmap_format_y8,
	_bitmap_format_ay8,
	_bitmap_format_a8y8,
	_bitmap_format_unused1,
	_bitmap_format_unused2,
	_bitmap_format_r5g6b5,
	_bitmap_format_unused3,
	_bitmap_format_a1r5g5b5,
	_bitmap_format_a4r4g4b4,
	_bitmap_format_x8r8g8b8,
	_bitmap_format_a8r8g8b8,
	_bitmap_format_unused4,
	_bitmap_format_unused5,
	_bitmap_format_dxt1,
	_bitmap_format_dxt3,
	_bitmap_format_dxt5,
	_bitmap_format_p8_bump,
	NUMBER_OF_BITMAP_FORMATS,
};

enum
{
	_bitmap_has_power_of_two_dimensions_bit,
	_bitmap_compressed_bit,
	_bitmap_palettized_bit,
	_bitmap_swizzled_bit,
	_bitmap_linear_bit,
	_bitmap_v16u16_bit,
	_bitmap_allocated_bit,
	_bitmap_cached_bit,
	NUMBER_OF_BITMAP_FLAGS,
};

enum
{
	_bitmap_d3d_format_table_regular,
	_bitmap_d3d_format_table_linear,
	NUMBER_OF_BITMAP_D3D_FORMAT_TABLES,
};

/* ---------- macros */

/* ---------- structures */

struct xbox_texture_cache_globals
{
	struct data_array *textures;
	byte *base_address;
	struct lruv_cache *cache;
	boolean stolen_memory;
};

struct xbox_texture_cache_texture
{
	short identifier;
	short read_request_handle;
	boolean loaded;
	boolean used;
	struct bitmap_data *bitmap;
	D3DBaseTexture hardware_format;
};

struct texture_cache_debug_options
{
	boolean graph;
	boolean list;
};

typedef char verify_xbox_texture_cache_textures_offset[
	offsetof(
		struct xbox_texture_cache_globals,
		textures) == 0 ? 1 : -1];
typedef char verify_xbox_texture_cache_base_address_offset[
	offsetof(
		struct xbox_texture_cache_globals,
		base_address) == 0x4 ? 1 : -1];
typedef char verify_xbox_texture_cache_cache_offset[
	offsetof(
		struct xbox_texture_cache_globals,
		cache) == 0x8 ? 1 : -1];
typedef char verify_xbox_texture_cache_stolen_memory_offset[
	offsetof(
		struct xbox_texture_cache_globals,
		stolen_memory) == 0xC ? 1 : -1];
typedef char verify_xbox_texture_cache_globals_size[
	sizeof(struct xbox_texture_cache_globals) == 0x10 ? 1 : -1];
typedef char verify_xbox_texture_cache_texture_loaded_offset[
	offsetof(
		struct xbox_texture_cache_texture,
		loaded) == 0x4 ? 1 : -1];
typedef char verify_xbox_texture_cache_texture_used_offset[
	offsetof(
		struct xbox_texture_cache_texture,
		used) == 0x5 ? 1 : -1];
typedef char verify_xbox_texture_cache_texture_bitmap_offset[
	offsetof(
		struct xbox_texture_cache_texture,
		bitmap) == 0x8 ? 1 : -1];
typedef char verify_xbox_texture_cache_texture_hardware_format_offset[
	offsetof(
		struct xbox_texture_cache_texture,
		hardware_format) == 0xC ? 1 : -1];
typedef char verify_xbox_texture_cache_texture_size[
	sizeof(struct xbox_texture_cache_texture) == 0x20 ? 1 : -1];
/* ---------- prototypes */

static boolean texture_cache_locked_block_proc(
	long block_index);
static void texture_cache_delete_block_proc(
	long block_index);
static const char *texture_cache_name_block_proc(
	long block_index);
long bitmap_format_to_d3d_format(
	short format,
	word flags);
long bitmap_format_to_d3d_linear_format(
	short format,
	word flags);
static boolean compare(
	struct bitmap_data *first,
	struct bitmap_data *second);
static void texture_cache_initialize_hardware_format(
	struct bitmap_data *bitmap,
	D3DBaseTexture *texture);
static void render_inverse_transform_screen_point(
	real_point2d const *screen_position,
	real_point3d *world_position,
	real_vector3d *world_vector);
static boolean texture_cache_start_loading_bitmap(
	struct bitmap_data *bitmap,
	boolean block);
static boolean texture_cache_bitmap_valid(
	struct bitmap_data *bitmap);
#ifdef HALO_WIIU
static boolean texture_cache_stream_start(
	struct bitmap_data *bitmap,
	long cache_block_index,
	byte *base_address,
	struct xbox_texture_cache_texture *texture,
	boolean block);
#endif

/* ---------- globals */

static const long bitmap_d3d_format_tables
	[NUMBER_OF_BITMAP_D3D_FORMAT_TABLES][NUMBER_OF_BITMAP_FORMATS] =
{
	{
		D3DFMT_A8,
		D3DFMT_L8,
		D3DFMT_AL8,
		D3DFMT_A8L8,
		NONE,
		NONE,
		D3DFMT_R5G6B5,
		NONE,
		D3DFMT_A1R5G5B5,
		D3DFMT_A4R4G4B4,
		D3DFMT_X8R8G8B8,
		D3DFMT_A8R8G8B8,
		NONE,
		NONE,
		D3DFMT_DXT1,
		D3DFMT_DXT3,
		D3DFMT_DXT5,
		D3DFMT_P8,
	},
	{
		D3DFMT_LIN_A8,
		D3DFMT_LIN_L8,
		D3DFMT_LIN_AL8,
		D3DFMT_LIN_A8L8,
		NONE,
		NONE,
		D3DFMT_LIN_R5G6B5,
		NONE,
		D3DFMT_LIN_A1R5G5B5,
		D3DFMT_LIN_A4R4G4B4,
		D3DFMT_LIN_X8R8G8B8,
		D3DFMT_LIN_A8R8G8B8,
		NONE,
		NONE,
		NONE,
		NONE,
		NONE,
		NONE,
	},
};
/* provisional name: the January map leaves this .bss array unnamed; it holds the debug
 * listing's cached bitmaps */
static struct bitmap_data *texture_cache_debug_bitmaps[XBOX_TEXTURE_CACHE_PAGE_COUNT];
static struct xbox_texture_cache_globals xbox_texture_cache_globals;
struct texture_cache_debug_options texture_cache_debug_options = {0};
boolean debug_texture_cache = FALSE;
static unsigned long texture_cache_last_failure_time = 0;

#ifdef HALO_WIIU
/* port: texture streaming (halo/stream.txt, default 0; port/linux/game/texture_stream.h has the plan). The texture of a block whose map read was cut in two: the tail
(the small mipmaps, read first) is described by a second header at the tail's first level. A block's state is here, by the block's index. */
struct texture_stream_state
{
	D3DBaseTexture header; /* the tail alone: its Lock says whether a draw reads it */
	volatile boolean tail_loaded; /* set by the cache file thread: TRUE, or failed */
	boolean active; /* the block's read was cut in two and the texture has not been given whole yet */
	boolean tail_used; /* drawn from its tail at least once */
	long level;
	unsigned long first_ms;
};

static struct texture_stream_state texture_streams[XBOX_TEXTURE_CACHE_PAGE_COUNT];
/* (a block's index is a datum index: its salt in the high 16 bits and its place, which is the cache's page index, in the low) */
#define TEXTURE_STREAM_STATE(block_index) (&texture_streams[(block_index) & 0xFFFF])
static long texture_stream_setting = -1;

/* what streaming did, for the log */
static struct
{
	unsigned long tails_drawn; /* textures drawn from their tails */
	unsigned long tail_binds; /* binds of a tail */
	unsigned long whole_after_ms; /* the sum, over those, of the time from the first bind of the tail to the whole texture */
	unsigned long longest_ms;
	unsigned long tail_waits; /* binds that waited for a tail still being read */
	unsigned long tail_wait_ms;
	unsigned long old_waits; /* binds of a texture that was not cut, which waited as the game always did */
	unsigned long old_wait_ms;
	unsigned long cut; /* textures read in two */
	unsigned long not_cut; /* ... and not */
	unsigned long last_note;
	unsigned long said;
} texture_stream_totals;

/* the thread that draws is told (d3d8_gx2.c) that a block of the cache is read for a texture by another one, which must not hand the draws over */
int halo_texture_stream_on;

boolean texture_cache_stream_enabled(
	void)
{
	if (texture_stream_setting < 0)
	{
		texture_stream_setting = wiiu_study_number("stream.txt", 0) > 0 ? 1 : 0;
		halo_texture_stream_on = (int)texture_stream_setting;
		if (texture_stream_setting)
			platform_log("stream: texture streaming is on (halo/stream.txt): the tail of a bitmap's mipmaps is read first and drawn from while the rest comes");
	}
	return texture_stream_setting > 0;
}

static boolean texture_stream_block_busy(
	long block_index)
{
	struct texture_stream_state *stream = TEXTURE_STREAM_STATE(block_index);

	return stream->active && (!stream->tail_loaded || IDirect3DBaseTexture8_IsBusy(&stream->header));
}
#endif

/* ---------- public code */

void texture_cache_delete(
	void)
{
#ifdef HALO_ANDROID
	/* port: release CE heap pixels before their bitmap owners disappear. */
	android_ce_texture_reset();
#endif
	data_dispose(xbox_texture_cache_globals.textures);
	lruv_delete(xbox_texture_cache_globals.cache);

	return;
}

void texture_cache_open(
	void)
{
	data_make_valid(xbox_texture_cache_globals.textures);

	return;
}

void texture_cache_idle(
	void)
{
#ifdef HALO_ANDROID
	/* port: release the preceding frame's CE residency locks. */
	android_ce_texture_idle();
#endif
	lruv_idle(xbox_texture_cache_globals.cache);

	return;
}

void texture_cache_bitmap_new(
	long bitmap_tag_index,
	struct bitmap_data *bitmap)
{
	struct bitmap_group *bitmap_group;

	match_assert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		157,
		!TEST_FLAG(bitmap->flags, _bitmap_cached_bit));
	SET_FLAG(bitmap->flags, _bitmap_cached_bit, TRUE);
	bitmap->cache_block_index = NONE;
	bitmap->base_address = NULL;
	bitmap->hardware_format = NULL;
	bitmap_group = bitmap_group_get(bitmap_tag_index);
	bitmap->pixels_offset += bitmap_group->pixel_data.file_offset;
	bitmap->pixels_size = bitmap_get_pixel_data_size(bitmap);
	bitmap->tag_index = bitmap_tag_index;
	bitmap->base_address = NULL;
	bitmap->hardware_format = NULL;
	bitmap->cache_block_index = NONE;

	return;
}

void texture_cache_bitmap_delete(
	struct bitmap_data *bitmap)
{
	long cache_block_index;

#ifdef HALO_ANDROID
	/* port: independent CE allocations have no Xbox LRU block index. */
	android_ce_texture_delete(bitmap);
#endif
	if (TEST_FLAG(bitmap->flags, _bitmap_cached_bit))
	{
		cache_block_index = bitmap->cache_block_index;
		if (cache_block_index != NONE)
		{
			lruv_block_delete(
				xbox_texture_cache_globals.cache,
				cache_block_index);
		}
		SET_FLAG(bitmap->flags, _bitmap_cached_bit, FALSE);
		bitmap->cache_block_index = NONE;
		bitmap->base_address = NULL;
	}

	return;
}

void *texture_cache_steal_memory(
	long size)
{
	long page_count = size / XBOX_TEXTURE_CACHE_PAGE_SIZE + 1;
	long remaining_page_count =
		XBOX_TEXTURE_CACHE_STEALABLE_PAGE_COUNT - page_count;
	byte *base_address =
		(byte *)physical_memory_get_texture_cache_base_address() +
		remaining_page_count * XBOX_TEXTURE_CACHE_PAGE_SIZE;
	long stolen_size = page_count * XBOX_TEXTURE_CACHE_PAGE_SIZE;
	byte *stolen_address =
		base_address + XBOX_TEXTURE_CACHE_STEAL_GUARD_SIZE;
	byte *end_guard_address =
		base_address + XBOX_TEXTURE_CACHE_STEAL_GUARD_SIZE + stolen_size;

	match_assert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		0x13F,
		remaining_page_count>0);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		0x140,
		!xbox_texture_cache_globals.stolen_memory);
	lruv_resize(
		xbox_texture_cache_globals.cache,
		remaining_page_count);
	XPhysicalProtect(
		stolen_address,
		stolen_size,
		PAGE_READWRITE);
	XPhysicalProtect(
		base_address,
		XBOX_TEXTURE_CACHE_STEAL_GUARD_SIZE,
		PAGE_READONLY);
	XPhysicalProtect(
		end_guard_address,
		XBOX_TEXTURE_CACHE_STEAL_GUARD_SIZE,
		PAGE_READONLY);
	xbox_texture_cache_globals.stolen_memory = TRUE;

	return stolen_address;
}

void texture_cache_return_memory(
	void)
{
	match_assert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		345,
		xbox_texture_cache_globals.stolen_memory);
	lruv_resize(
		xbox_texture_cache_globals.cache,
		XBOX_TEXTURE_CACHE_PAGE_COUNT);
	XPhysicalProtect(
		physical_memory_get_texture_cache_base_address(),
		XBOX_TEXTURE_CACHE_SIZE,
		XBOX_TEXTURE_CACHE_PROTECTION);
	xbox_texture_cache_globals.stolen_memory = FALSE;

	return;
}

static const char *texture_cache_name_block_proc(
	long block_index)
{
	struct xbox_texture_cache_texture *texture = datum_get(
		xbox_texture_cache_globals.textures,
		block_index);

	return tag_get_name(texture->bitmap->tag_index);
}

long bitmap_format_to_d3d_format(
	short format,
	word flags)
{
	const long *table =
		bitmap_d3d_format_tables[_bitmap_d3d_format_table_regular];

	match_assert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		481,
		format>=0 && format<NUMBER_OF_BITMAP_FORMATS);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		482,
		table[format]!=NONE);
	if (TEST_FLAG(flags, _bitmap_v16u16_bit) &&
		(format == _bitmap_format_x8r8g8b8 || format == _bitmap_format_a8r8g8b8))
	{
		return D3DFMT_V16U16;
	}

	return table[format];
}

long bitmap_format_to_d3d_linear_format(
	short format,
	word flags)
{
	const long *table =
		bitmap_d3d_format_tables[_bitmap_d3d_format_table_linear];

	match_assert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		518,
		format>=0 && format<NUMBER_OF_BITMAP_FORMATS);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		519,
		table[format]!=NONE);
	if (TEST_FLAG(flags, _bitmap_v16u16_bit) &&
		(format == _bitmap_format_x8r8g8b8 || format == _bitmap_format_a8r8g8b8))
	{
		return D3DFMT_LIN_V16U16;
	}

	return table[format];
}

static boolean compare(
	struct bitmap_data *first,
	struct bitmap_data *second)
{
	long difference = rasterizer_xbox_bitmap_get_pixel_data_size(first) -
		rasterizer_xbox_bitmap_get_pixel_data_size(second);

	return difference > 0;
}

void texture_cache_flush(
	void)
{
	IDirect3DDevice8_KickPushBuffer(global_d3d_device);
	IDirect3DDevice8_IsBusy(global_d3d_device);
#ifdef HALO_ANDROID
	/* port: include CE pixels in map unload, reload and explicit flush. */
	android_ce_texture_reset();
#endif
	lruv_flush(xbox_texture_cache_globals.cache);

	return;
}

static boolean texture_cache_locked_block_proc(
	long block_index)
{
	struct xbox_texture_cache_texture *texture = datum_get(
		xbox_texture_cache_globals.textures,
		block_index);

	return !texture->loaded ||
#ifdef HALO_WIIU
		texture_stream_block_busy(block_index) ||
#endif
		IDirect3DBaseTexture8_IsBusy(&texture->hardware_format);
}

static void texture_cache_delete_block_proc(
	long block_index)
{
	struct xbox_texture_cache_texture *texture;
	struct xbox_texture_cache_texture *cache_entry;

	texture = datum_get(
		xbox_texture_cache_globals.textures,
		block_index);
	do
	{
		cache_entry = datum_get(
			xbox_texture_cache_globals.textures,
			block_index);
	}
	while (!cache_entry->loaded ||
#ifdef HALO_WIIU
		texture_stream_block_busy(block_index) ||
#endif
		IDirect3DBaseTexture8_IsBusy(&cache_entry->hardware_format));
#ifdef HALO_WIIU
	TEXTURE_STREAM_STATE(block_index)->active = FALSE;
#endif

	match_assert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		0x187,
		texture->bitmap->cache_block_index==block_index);
	texture->bitmap->cache_block_index = NONE;
	texture->bitmap->base_address = NULL;
	datum_delete(
		xbox_texture_cache_globals.textures,
		block_index);

	return;
}

static void texture_cache_initialize_hardware_format(
	struct bitmap_data *bitmap,
	D3DBaseTexture *texture)
{
	match_assert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		532,
		bitmap);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		533,
		texture);

	texture->Data = 0;
	texture->Lock = 0;
	texture->Common = D3DCOMMON_TYPE_TEXTURE | 1;
	if (TEST_FLAG(bitmap->flags, _bitmap_linear_bit))
	{
		texture->Format =
			(bitmap_format_to_d3d_linear_format(bitmap->format, bitmap->flags) << D3DFORMAT_FORMAT_SHIFT) |
			(1 << D3DFORMAT_MIPMAP_SHIFT) |
			(2 << D3DFORMAT_DIMENSION_SHIFT) |
			D3DFORMAT_BORDERSOURCE_COLOR |
			D3DFORMAT_DMACHANNEL_A;
		/* port: the pitch rounded up, as rasterizer_xbox_bitmap_rebuild_hardware_format
		pads the rows (an Xbox map's linear rows are whole multiples of it; a
		Custom Edition map's need not be) */
		texture->Size =
			(((bitmap_mipmap_get_row_pitch(bitmap, 0) + D3DTEXTURE_PITCH_ALIGNMENT - 1) / D3DTEXTURE_PITCH_ALIGNMENT - 1)
				<< D3DSIZE_PITCH_SHIFT) |
			((bitmap->height - 1) << D3DSIZE_HEIGHT_SHIFT) |
			(bitmap->width - 1);
	}
	else
	{
		texture->Format =
			(floor_log2(bitmap->depth) << D3DFORMAT_PSIZE_SHIFT) |
			(floor_log2(bitmap->height) << D3DFORMAT_VSIZE_SHIFT) |
			(floor_log2(bitmap->width) << D3DFORMAT_USIZE_SHIFT) |
			(bitmap_format_to_d3d_format(bitmap->format, bitmap->flags) << D3DFORMAT_FORMAT_SHIFT) |
			((bitmap->type == _bitmap_type_3d ? 3 : 2) << D3DFORMAT_DIMENSION_SHIFT) |
			((rasterizer_xbox_bitmap_get_max_mipmap_count(bitmap) + 1) << D3DFORMAT_MIPMAP_SHIFT) |
			(bitmap->type == _bitmap_type_cube_map ? D3DFORMAT_CUBEMAP : 0) |
			D3DFORMAT_BORDERSOURCE_COLOR |
			D3DFORMAT_DMACHANNEL_A;
		texture->Size = 0;
	}
	IDirect3DBaseTexture8_Register(texture, bitmap->base_address);

	return;
}

static void render_inverse_transform_screen_point(
	real_point2d const *screen_position,
	real_point3d *world_position,
	real_vector3d *world_vector)
{
	real screen_x;
	real screen_y;
	real_vector3d delta0;
	real_vector3d delta1;
	real_point3d point;

	screen_x = screen_position->x * (1.0f / 640.0f);
	screen_y = 1.0f - screen_position->y * (1.0f / 480.0f);
	add_vectors3d(
		(real_vector3d const *)&render.frustum.world_vertices[4],
		global_zero_vector3d,
		(real_vector3d *)world_position);

	delta0.i = render.frustum.world_vertices[1].n[0] - render.frustum.world_vertices[0].n[0];
	delta0.j = render.frustum.world_vertices[1].n[1] - render.frustum.world_vertices[0].n[1];
	delta0.k = render.frustum.world_vertices[1].n[2] - render.frustum.world_vertices[0].n[2];
	delta1.i = render.frustum.world_vertices[2].n[0] - render.frustum.world_vertices[0].n[0];
	delta1.j = render.frustum.world_vertices[2].n[1] - render.frustum.world_vertices[0].n[1];
	delta1.k = render.frustum.world_vertices[2].n[2] - render.frustum.world_vertices[0].n[2];

	point.x = delta0.i * screen_x + render.frustum.world_vertices[0].n[0];
	point.y = delta0.j * screen_x + render.frustum.world_vertices[0].n[1];
	point.z = delta0.k * screen_x + render.frustum.world_vertices[0].n[2];
	point.x = delta1.i * screen_y + point.x;
	point.y = delta1.j * screen_y + point.y;
	point.z = delta1.k * screen_y + point.z;

	world_vector->i = point.x - world_position->x;
	world_vector->j = point.y - world_position->y;
	world_vector->k = point.z - world_position->z;

	return;
}

void texture_cache_new(
	void)
{
	xbox_texture_cache_globals.textures = data_new(
		"xbox texture",
		XBOX_TEXTURE_CACHE_PAGE_COUNT,
		XBOX_TEXTURE_CACHE_ENTRY_SIZE);
	match_vassert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		98,
		xbox_texture_cache_globals.textures != NULL,
		"xbox_texture_cache_globals.textures");
	xbox_texture_cache_globals.cache = lruv_new(
		"xbox texture cache",
		XBOX_TEXTURE_CACHE_PAGE_COUNT,
		XBOX_TEXTURE_CACHE_PAGE_SIZE_BITS,
		XBOX_TEXTURE_CACHE_PAGE_COUNT,
		texture_cache_delete_block_proc,
		texture_cache_locked_block_proc);
	match_vassert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		102,
		xbox_texture_cache_globals.cache != NULL,
		"xbox_texture_cache_globals.cache");
	xbox_texture_cache_globals.base_address =
		physical_memory_get_texture_cache_base_address();
	match_vassert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		105,
		xbox_texture_cache_globals.base_address != NULL,
		"xbox_texture_cache_globals.base_address");

	return;
}

void texture_cache_close(
	void)
{
	match_assert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		133,
		!xbox_texture_cache_globals.stolen_memory);
	texture_cache_flush();
	data_make_invalid(xbox_texture_cache_globals.textures);

	return;
}

/* port: whether a map's bitmap is one the hardware format can describe and
the texture cache can size: a type and format the tables have, sizes up to
the device's (D3DDevice_GetDeviceCaps: 4096, a volume 512; retail's are 2048
and 32 at most), mipmaps its sizes have, the compressed flag its format's,
and a linear one's rows as Size can hold them. Its pixels are read and its
hardware format built from these; a bad one isn't loaded, said once. */
static boolean texture_cache_bitmap_valid(
	struct bitmap_data *bitmap)
{
	static boolean reported = FALSE;
	boolean linear = TEST_FLAG(bitmap->flags, _bitmap_linear_bit);
	short maximum_dimension = bitmap->type==_bitmap_type_3d ? 512 : 4096;
	boolean valid =
		VALID_INDEX(bitmap->type, NUMBER_OF_BITMAP_TYPES) &&
		VALID_INDEX(bitmap->format, NUMBER_OF_BITMAP_FORMATS) &&
		bitmap->width>0 && bitmap->width<=maximum_dimension &&
		bitmap->height>0 && bitmap->height<=maximum_dimension &&
		bitmap->depth>0 && bitmap->depth<=maximum_dimension &&
		bitmap->mipmap_count>=0 &&
		bitmap->mipmap_count<=floor_log2(MAX(bitmap->width, MAX(bitmap->height, bitmap->depth))) &&
		bitmap->pixels_size>=0;

	if (valid)
	{
		boolean compressed_format =
			bitmap->format>=_bitmap_format_dxt1 && bitmap->format<=_bitmap_format_dxt5;
		boolean compressed_flag = TEST_FLAG(bitmap->flags, _bitmap_compressed_bit) ? TRUE : FALSE;

		valid =
			bitmap_d3d_format_tables[linear ? _bitmap_d3d_format_table_linear : _bitmap_d3d_format_table_regular][bitmap->format]!=NONE &&
			compressed_flag==compressed_format;
	}
	/* port: a Custom Edition map's linear rows need not be whole steps of the
	pitch, nor a mod's: they are padded to them as its pixels load
	(port/linux/game/custom_edition_bitmaps.c, mod_bitmaps.c) */
	if (valid && linear)
	{
		extern boolean mod_bitmaps_contain(struct bitmap_data const *bitmap);
		long row_pitch = bitmap_mipmap_get_row_pitch(bitmap, 0);
		long steps = (row_pitch+D3DTEXTURE_PITCH_ALIGNMENT-1)/D3DTEXTURE_PITCH_ALIGNMENT;

		valid =
			row_pitch>0 &&
			(row_pitch%D3DTEXTURE_PITCH_ALIGNMENT==0 || custom_edition_cache_tags_loaded() ||
				mod_bitmaps_contain(bitmap)) &&
			steps<=(long)((D3DSIZE_PITCH_MASK>>D3DSIZE_PITCH_SHIFT)+1);
	}
	if (!valid && !reported)
	{
		error(
			_error_silent,
			"### ERROR bitmap %s (#%dx#%dx#%d, format #%d, type #%d) can't be cached; it isn't drawn",
			tag_get_name(bitmap->tag_index),
			bitmap->width,
			bitmap->height,
			bitmap->depth,
			bitmap->format,
			bitmap->type);
		reported = TRUE;
	}

	return valid;
}

#ifdef HALO_ANDROID
/* port: CE/mod readers fill and convert pixels synchronously before the
 * independent hardware header is published to the renderer. */
static void texture_cache_read_android_ce(struct bitmap_data *bitmap, void *header)
{
	extern boolean mod_bitmaps_read(struct bitmap_data *bitmap, void *buffer);
	boolean loaded = FALSE;

	texture_cache_initialize_hardware_format(bitmap, header);
	if (!mod_bitmaps_read(bitmap, bitmap->base_address))
		cache_file_read(bitmap->tag_index, bitmap->pixels_offset, bitmap->pixels_size,
			bitmap->base_address, &loaded, TRUE);
}
#endif

static boolean texture_cache_start_loading_bitmap(
	struct bitmap_data *bitmap,
	boolean block)
{
	long cache_block_index;
	byte *base_address;
	long size;

	/* port: only a bitmap the hardware format and the cache can hold (a
	map's fields) */
	if (!texture_cache_bitmap_valid(bitmap))
	{
		return FALSE;
	}
	size = rasterizer_xbox_bitmap_get_pixel_data_size(bitmap);

	size = MAX(size, bitmap->pixels_size);
	cache_block_index = lruv_block_new(
		xbox_texture_cache_globals.cache,
		size);
	if (cache_block_index != NONE)
	{
		long new_texture_index;
		struct xbox_texture_cache_texture *texture;

		base_address = xbox_texture_cache_globals.base_address +
			(unsigned long)lruv_block_get_address(
				xbox_texture_cache_globals.cache,
				cache_block_index);
		new_texture_index = datum_new_at_index(
			xbox_texture_cache_globals.textures,
			cache_block_index);
		texture = datum_get(
			xbox_texture_cache_globals.textures,
			cache_block_index);
		match_assert(
			"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
			431,
			new_texture_index==cache_block_index);
		bitmap->cache_block_index = cache_block_index;
		bitmap->base_address = base_address;
		texture->bitmap = bitmap;
		texture_cache_initialize_hardware_format(bitmap, &texture->hardware_format);
		/* port: or, the bitmap of a mod played over the map's, from its
		file, at once (port/linux/game/mod_bitmaps.c) */
		{
			extern boolean mod_bitmaps_read(struct bitmap_data *bitmap, void *buffer);

			if (mod_bitmaps_read(bitmap, base_address))
			{
				texture->loaded = TRUE;
				texture->read_request_handle = NONE;
			}
#ifdef HALO_WIIU
			else if (texture_cache_stream_start(bitmap, cache_block_index, base_address, texture, block))
			{
				/* port: the read is in two (texture streaming): the tail's and then the rest's, asked for */
			}
#endif
			else
			{
				texture->read_request_handle = cache_file_read(
					bitmap->tag_index,
					bitmap->pixels_offset,
					bitmap->pixels_size,
					base_address,
					&texture->loaded,
					block);
			}
		}

		return TRUE;
	}

	return FALSE;
}

#ifdef HALO_WIIU
/* port: cuts the read of a bitmap's pixels in two, if streaming is on and the bitmap is one it can serve from a tail (a 2D bitmap with mipmaps of the swizzled or
compressed layout, not a palettized or linear one), and makes the tail's header; returns TRUE if the two reads were asked for */
static boolean texture_cache_stream_start(
	struct bitmap_data *bitmap,
	long cache_block_index,
	byte *base_address,
	struct xbox_texture_cache_texture *texture,
	boolean block)
{
	struct texture_stream_state *stream = TEXTURE_STREAM_STATE(cache_block_index);
	struct texture_stream_plan plan;
	long sizes[TEXTURE_STREAM_MAXIMUM_LEVELS];
	short max_level, level;

	stream->active = FALSE;
	stream->tail_loaded = FALSE;
	stream->tail_used = FALSE;
	if (!texture_cache_stream_enabled() || custom_edition_cache_tags_loaded())
	{
		return FALSE;
	}
	if (bitmap->type != _bitmap_type_2d ||
		TEST_FLAG(bitmap->flags, _bitmap_linear_bit) ||
		TEST_FLAG(bitmap->flags, _bitmap_palettized_bit) ||
		TEST_FLAG(bitmap->flags, _bitmap_v16u16_bit) ||
		!TEST_FLAG(bitmap->flags, _bitmap_has_power_of_two_dimensions_bit) ||
		bitmap->format == _bitmap_format_p8_bump)
	{
		texture_stream_totals.not_cut++;
		return FALSE;
	}
	max_level = rasterizer_xbox_bitmap_get_max_mipmap_count(bitmap);
	if (max_level < 2 || max_level >= TEXTURE_STREAM_MAXIMUM_LEVELS)
	{
		texture_stream_totals.not_cut++;
		return FALSE;
	}
	for (level = 0; level <= max_level; level++)
	{
		sizes[level] = bitmap_mipmap_get_pixel_data_size(bitmap, level);
	}
	texture_stream_plan_make(&plan, bitmap->width, bitmap->height, max_level, sizes, bitmap->pixels_size);
	if (!plan.eligible)
	{
		texture_stream_totals.not_cut++;
		return FALSE;
	}
	/* the tail's header: this texture's, of its tail's levels alone, at the tail's first level */
	stream->header = texture->hardware_format;
	stream->header.Lock = 0;
	stream->header.Format =
		(stream->header.Format & ~(D3DFORMAT_USIZE_MASK | D3DFORMAT_VSIZE_MASK | D3DFORMAT_MIPMAP_MASK)) |
		((unsigned long)plan.width_log2 << D3DFORMAT_USIZE_SHIFT) |
		((unsigned long)plan.height_log2 << D3DFORMAT_VSIZE_SHIFT) |
		((unsigned long)plan.levels << D3DFORMAT_MIPMAP_SHIFT);
	IDirect3DBaseTexture8_Register(&stream->header, base_address + plan.level_offset);
	stream->level = plan.level;
	stream->first_ms = 0;
	stream->active = TRUE;
	texture_stream_totals.cut++;
	/* the tail first (it goes before the reads that are waiting), then the rest; they write no byte twice */
	cache_file_read_first(
		bitmap->tag_index,
		bitmap->pixels_offset + (long)plan.tail_offset,
		(long)plan.tail_size,
		base_address + plan.tail_offset,
		(boolean *)&stream->tail_loaded);
	texture->read_request_handle = cache_file_read(
		bitmap->tag_index,
		bitmap->pixels_offset,
		(long)plan.tail_offset,
		base_address,
		&texture->loaded,
		block);

	return TRUE;
}

static void texture_cache_stream_note(
	unsigned long now)
{
	if (now - texture_stream_totals.last_note >= 10000 && (texture_stream_totals.cut || texture_stream_totals.not_cut))
	{
		texture_stream_totals.last_note = now;
		platform_log(
			"stream: %lu bitmaps read in two and %lu not; %lu drawn from their tails (%lu binds), whole after %lu ms on average and %lu ms at most; "
			"%lu binds waited %lu ms for a tail, %lu waited %lu ms for a whole bitmap as the game does",
			texture_stream_totals.cut, texture_stream_totals.not_cut, texture_stream_totals.tails_drawn, texture_stream_totals.tail_binds,
			texture_stream_totals.tails_drawn ? texture_stream_totals.whole_after_ms / texture_stream_totals.tails_drawn : 0,
			texture_stream_totals.longest_ms, texture_stream_totals.tail_waits, texture_stream_totals.tail_wait_ms,
			texture_stream_totals.old_waits, texture_stream_totals.old_wait_ms);
	}
}

/* port: what the renderer binds for a bitmap with streaming on: the texture if it is in, else the texture's tail (the small mipmaps, read first) if that is in
(waiting for it a moment if it is not: it is a few KB, and goes before the other reads), else the texture as the game takes it, waiting for all of it */
void *texture_cache_bitmap_get_hardware_format_streaming(
	struct bitmap_data *bitmap)
{
	void *hardware_format = _texture_cache_bitmap_get_hardware_format(bitmap, FALSE, TRUE);
	unsigned long now = system_milliseconds();
	long block_index = bitmap->cache_block_index;

	if (hardware_format && !(TEST_FLAG(bitmap->flags, _bitmap_cached_bit) && block_index != NONE))
	{
		return hardware_format;
	}
	if (TEST_FLAG(bitmap->flags, _bitmap_cached_bit) && block_index != NONE)
	{
		struct texture_stream_state *stream = TEXTURE_STREAM_STATE(block_index);

		if (hardware_format)
		{
			if (stream->active)
			{
				if (stream->tail_used)
				{
					const unsigned long milliseconds = now - stream->first_ms;

					texture_stream_totals.tails_drawn++;
					texture_stream_totals.whole_after_ms += milliseconds;
					if (milliseconds > texture_stream_totals.longest_ms)
					{
						texture_stream_totals.longest_ms = milliseconds;
					}
					if (texture_stream_totals.said < 12)
					{
						texture_stream_totals.said++;
						platform_log("stream: %s (%ld KB) was drawn from its tail (level %ld) for %lu ms until the whole of it was in", tag_get_name(bitmap->tag_index),
							(long)(bitmap->pixels_size / 1024), stream->level, milliseconds);
					}
				}
				stream->active = FALSE;
			}
			texture_cache_stream_note(now);

			return hardware_format;
		}
		if (stream->active && !bitmap->hardware_format)
		{
			struct xbox_texture_cache_texture *texture = datum_try_and_get(
				xbox_texture_cache_globals.textures,
				block_index);

			if (texture && texture->bitmap == bitmap)
			{
				if (!stream->tail_loaded)
				{
					/* the tail goes before the other reads: a few ms; the sound is kept going as the game's wait keeps it */
					unsigned long waited = 0;

					while (!stream->tail_loaded && (waited = system_milliseconds() - now) < 100)
					{
						if (system_milliseconds() - sound_render_time() > 132)
						{
							sound_idle();
						}
						SwitchToThread();
					}
					texture_stream_totals.tail_waits++;
					texture_stream_totals.tail_wait_ms += waited;
				}
				if (stream->tail_loaded == TRUE && !texture->loaded)
				{
					lruv_block_touch(
						xbox_texture_cache_globals.cache,
						block_index);
					if (!stream->tail_used)
					{
						stream->tail_used = TRUE;
						stream->first_ms = now;
					}
					texture_stream_totals.tail_binds++;
					texture_cache_stream_note(now);

					return &stream->header;
				}
			}
		}
	}
	/* the game's way: wait for all of it (a bitmap that was not cut, or whose tail did not come) */
	{
		const unsigned long started = system_milliseconds();

		hardware_format = _texture_cache_bitmap_get_hardware_format(bitmap, TRUE, TRUE);
		if (system_milliseconds() - started >= 2)
		{
			texture_stream_totals.old_waits++;
			texture_stream_totals.old_wait_ms += system_milliseconds() - started;
		}
		texture_cache_stream_note(system_milliseconds());
	}

	return hardware_format;
}
#endif

void texture_cache_debug_render(
	void)
{
	if (texture_cache_debug_options.graph)
	{
		byte page_usage[XBOX_TEXTURE_CACHE_PAGE_COUNT];
		real_point3d world_positions[2];
		real_point2d screen_positions[2];
		real_point3d *world_position;
		real_point2d *screen_position;
		real_argb_color const *colors[4];
		short window_width;
		real distance;
		real scale;
		short x_offset;
		short y_offset;
		real_vector3d world_vector;
		long page_index;
		long state_index;
		long point_index;

		window_width = render.camera.window_bounds.x1 -
			render.camera.window_bounds.x0;
		colors[0] = global_real_argb_red;
		colors[1] = global_real_argb_green;
		colors[2] = global_real_argb_blue;
		colors[3] = NULL;
		lruv_cache_get_page_usage(xbox_texture_cache_globals.cache, page_usage);

		for (page_index = 0; page_index < XBOX_TEXTURE_CACHE_PAGE_COUNT; page_index++)
		{
			x_offset = render.camera.window_bounds.x0 -
				render.camera.viewport_bounds.x0;
			y_offset = (render.camera.window_bounds.y0 -
				render.camera.viewport_bounds.y0) * 4;

			for (state_index = 0; state_index < 3; state_index++)
			{
				if (page_usage[page_index] & FLAG(state_index))
				{
					screen_positions[0].x = (real)(x_offset + page_index % window_width);
					screen_positions[0].y = (real)(y_offset +
						(state_index + (page_index / window_width) * 4) * 10);
					screen_positions[1].x = (real)(x_offset + page_index % window_width);
					screen_positions[1].y = (real)(y_offset +
						(state_index + (page_index / window_width) * 4) * 10 + 10);
					distance = render.camera.z_near + 0.001f;

					world_position = world_positions;
					screen_position = screen_positions;
					for (point_index = 2; point_index; point_index--)
					{
						render_inverse_transform_screen_point(
							screen_position,
							world_position,
							&world_vector);
						scale = distance /
							dot_product3d(&render.camera.forward, &world_vector);
						world_position->x += world_vector.i * scale;
						world_position->y += world_vector.j * scale;
						world_position->z += world_vector.k * scale;
						screen_position++;
						world_position++;
					}

					render_debug_line(
						TRUE,
						&world_positions[0],
						&world_positions[1],
						colors[state_index]);
				}
			}
		}
	}

	if (texture_cache_debug_options.list)
	{
		char string[1024];
		struct data_iterator iterator;
		short tab_stops[2];
		rectangle2d bounds;
		struct xbox_texture_cache_texture *texture;
		short bitmap_count = 0;
		long font_index;
		long bitmap_index;

		data_iterator_new(&iterator, xbox_texture_cache_globals.textures);
		while ((texture = data_iterator_next(&iterator)) != NULL)
		{
			struct bitmap_data *bitmap = texture->bitmap;

			if (bitmap->tag_index != NONE)
			{
				texture_cache_debug_bitmaps[bitmap_count++] = bitmap;
			}
		}
		qsort_4byte(
			(long *)texture_cache_debug_bitmaps,
			bitmap_count,
			(boolean (*)(long, long))compare);

		font_index = interface_get_tag_index(_interface_font_terminal);
		tab_stops[0] = rasterizer_globals.reserved04.frame_bounds.x0;
		tab_stops[1] = rasterizer_globals.reserved04.frame_bounds.x0 + 110;
		draw_string_set_tab_stops(tab_stops, 2);
		if (font_index != NONE)
		{
			draw_string_set_font(font_index);
		}

		for (bitmap_index = bitmap_count - 1; bitmap_index >= 0; bitmap_index--)
		{
			char const *touched_string = lruv_block_touched(
				xbox_texture_cache_globals.cache,
				texture_cache_debug_bitmaps[bitmap_index]->cache_block_index) ? "" : "*";

			sprintf(
				string,
				"|t%d|t%s%s",
				rasterizer_xbox_bitmap_get_pixel_data_size(texture_cache_debug_bitmaps[bitmap_index]),
				touched_string,
				tag_get_name(texture_cache_debug_bitmaps[bitmap_index]->tag_index));
			bounds.y0 = (bitmap_count - bitmap_index) * 10 + 35;
			bounds.x0 = 10;
			bounds.y1 = bounds.x1 = SHORT_MAX;
			draw_string_set_color(global_real_argb_yellow);
			rasterizer_draw_string(&bounds, NULL, NULL, 0, string);
		}
	}

	return;
}

void *_texture_cache_bitmap_get_hardware_format(
	struct bitmap_data *bitmap,
	boolean block,
	boolean load)
{
	void *hardware_format = NULL;

	match_assert(
		"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
		210,
		load || !block);
#ifdef HALO_ANDROID
	/* port: CE's frame working set is not bounded by the Xbox texture arena.
	 * Keep dynamic textures and Xbox maps on their own paths. */
	if (android_texture_fixes && custom_edition_cache_tags_loaded() &&
		TEST_FLAG(bitmap->flags, _bitmap_cached_bit))
	{
		if (texture_cache_bitmap_valid(bitmap))
			hardware_format = android_ce_texture_get(bitmap,
				MAX(rasterizer_xbox_bitmap_get_pixel_data_size(bitmap), bitmap->pixels_size),
				load, texture_cache_read_android_ce);
	}
	else
#endif
	if (TEST_FLAG(bitmap->flags, _bitmap_cached_bit))
	{
		/* port: a cache block that isn't this bitmap's (a map's stale index)
		is none */
		if (bitmap->cache_block_index != NONE)
		{
			struct xbox_texture_cache_texture *cached_texture = datum_try_and_get(
				xbox_texture_cache_globals.textures,
				bitmap->cache_block_index);

			if (!cached_texture || cached_texture->bitmap != bitmap)
			{
				bitmap->cache_block_index = NONE;
			}
		}
		if (bitmap->cache_block_index == NONE && load)
		{
			texture_cache_start_loading_bitmap(bitmap, block);
		}
		if (bitmap->cache_block_index != NONE)
		{
			struct xbox_texture_cache_texture *texture = datum_get(
				xbox_texture_cache_globals.textures,
				bitmap->cache_block_index);

			STARTUP_TIMER(waiting);
			boolean waited = block && !texture->loaded;

			lruv_block_touch(
				xbox_texture_cache_globals.cache,
				bitmap->cache_block_index);
			if (block && !texture->loaded)
			{
				if (debug_texture_cache)
				{
					console_warning(
						"%s",
						tag_get_name(bitmap->tag_index));
				}
				cache_file_promote_read(texture->read_request_handle);
			}
			do
			{
				if (texture->loaded)
				{
					if (!texture->used)
					{
						texture->used = TRUE;
					}
					hardware_format = &texture->hardware_format;
				}
				else
				{
					if (system_milliseconds() - sound_render_time() > 132)
					{
						sound_idle();
					}
					SwitchToThread();
				}
			}
			while (!hardware_format && block);
			/* port: (the start-up's timeline, startup_timeline.c: the time a bitmap's pixels were waited for, the reads of the card) */
			if (waited)
			{
				/* (and a wait of 40 ms or more said, for the first few: which bitmaps the first frames wait for) */
				static short said;
				const unsigned long milliseconds = STARTUP_SINCE_MS(waiting);

				if (milliseconds >= 40 && said < 12)
				{
					said++;
#ifdef HALO_WIIU
					platform_log("startup: waited %lu ms for the pixels of %s (%ld KB)", milliseconds, tag_get_name(bitmap->tag_index),
						(long)(bitmap->pixels_size / 1024));
#endif
				}
				STARTUP_ADD("texture_wait", waiting, 1, bitmap->pixels_size);
			}
		}
	}
	else
	{
		hardware_format = bitmap->hardware_format;
	}

	if (block && !hardware_format)
	{
		if (system_milliseconds() - texture_cache_last_failure_time > 10000)
		{
			/* (port: chatter, shown as config.toml's game.console_log says) */
			if (terminal_shows(_terminal_message_chatter))
			{
				terminal_printf(
					global_real_argb_purple,
					"!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
			}
			error(
				_error_silent,
				"YOU GOT STABBED!!!! double-click \"GETSTABBED.BAT\" on your PC now!!!");
			/* (port: chatter, shown as config.toml's game.console_log says) */
			if (terminal_shows(_terminal_message_chatter))
			{
				terminal_printf(
					global_real_argb_purple,
					"!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
			}
			lruv_debug_to_file(
				"d:\\stabbed.txt",
				tag_get_name(bitmap->tag_index),
				bitmap->pixels_size,
				xbox_texture_cache_globals.cache,
				scenario_debug_to_file,
				texture_cache_name_block_proc);
			texture_cache_last_failure_time = system_milliseconds();
		}
		hardware_format = rasterizer_get_bitmap_default_hardware_format(bitmap);
		match_assert(
			"c:\\halo\\SOURCE\\cache\\xbox_texture_cache.c",
			295,
			hardware_format);
	}

	return hardware_format;
}
