/*
XBOX_TEXTURES_DK.C

Xbox texture decoding and the texture cache for the deko3d renderer
(port/switch/DEKO3D.md, phase 6). A copy of port/linux/src/xbox_textures.c,
which the OpenGL renderer and the other builds keep unchanged: the decoding
(formats, geometry, swizzling, palettes) is the same text; what differs is
where a decoded texture goes. Here the cache keeps a number for each texture,
which names an image the host makes (dk_commands.h), and a texture's texels
go to the host as a command: the compressed formats (BC1 to BC3 are native to
the GPU) as the address of the game's own data, and every other format
decoded to 32-bit BGRA in a buffer of the guest's that the host reads when it
runs the command.

A cached texture stays valid until a page it was read from is written. Under
deko3d nothing is write-protected (the console will not change the window's
protection), so the writes the game announces (memory_watch_prepare_write, in
the texture locks and the file reads) are what memory_watch_generation counts.

The OpenGL version's replacements of a texture by a high-res HUD bitmap, by
the text's atlas and by a menu's art (hud_hires.h, text_hires.h, menu_files.h)
are images of their own here, numbered from the same allocator: their texels
(the PNGs decoded, mip levels made by halving; the atlas's coverage as white
with that alpha) go to the host as rows (DK_COMMAND_TEXTURE_ROWS), a piece at
a time, and only the atlas's rows that changed are sent again.
*/

#include "xgpu.h"
#include "halo_startup.h"
#include "port_config.h"
#include "dk_commands.h"
#include "dk_textures.h"
#include "hud_hires.h"
#include "menu_files.h"
#include "text_hires.h"
#include "wiiu_png_chain.h"
#include "wiiu_hud_sheet.h"
#include "../../linux/game/cache_file_formats.h"

/* (the high-res HUD's and the menus' PNGs come from hud_hires.c and menu_files.c, built without OpenGL here: hud_hires_png_pixels, menu_art_name, menu_art_png,
menu_art_serial; the text's atlas from text_hires.c, text_hires_atlas_rows) */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- formats */

enum texel_kind
{
	_texel_unknown,
	_texel_a8r8g8b8, _texel_x8r8g8b8, _texel_r5g6b5, _texel_a1r5g5b5, _texel_x1r5g5b5, _texel_a4r4g4b4,
	_texel_l8, _texel_al8, _texel_a8, _texel_a8l8, _texel_p8, _texel_g8b8, _texel_r8b8, _texel_r6g5b5,
	_texel_l16, _texel_v16u16, _texel_a8b8g8r8, _texel_b8g8r8a8, _texel_r8g8b8a8, _texel_r5g5b5a1,
	_texel_r4g4b4a4, _texel_yuy2, _texel_uyvy, _texel_d24s8, _texel_d16,
	_texel_dxt1, _texel_dxt3, _texel_dxt5,
};

struct format_information
{
	unsigned char kind;
	unsigned char bytes; /* per texel; per 4x4 block for DXT */
	unsigned char linear;
};

static struct format_information format_information(DWORD format)
{
	static const struct format_information table[0x42] =
	{
		[0x00] = { _texel_l8, 1, 0 },
		[0x01] = { _texel_al8, 1, 0 },
		[0x02] = { _texel_a1r5g5b5, 2, 0 },
		[0x03] = { _texel_x1r5g5b5, 2, 0 },
		[0x04] = { _texel_a4r4g4b4, 2, 0 },
		[0x05] = { _texel_r5g6b5, 2, 0 },
		[0x06] = { _texel_a8r8g8b8, 4, 0 },
		[0x07] = { _texel_x8r8g8b8, 4, 0 },
		[0x0b] = { _texel_p8, 1, 0 },
		[0x0c] = { _texel_dxt1, 8, 0 },
		[0x0e] = { _texel_dxt3, 16, 0 },
		[0x0f] = { _texel_dxt5, 16, 0 },
		[0x10] = { _texel_a1r5g5b5, 2, 1 },
		[0x11] = { _texel_r5g6b5, 2, 1 },
		[0x12] = { _texel_a8r8g8b8, 4, 1 },
		[0x13] = { _texel_l8, 1, 1 },
		[0x16] = { _texel_r8b8, 2, 1 },
		[0x17] = { _texel_g8b8, 2, 1 },
		[0x19] = { _texel_a8, 1, 0 },
		[0x1a] = { _texel_a8l8, 2, 0 },
		[0x1b] = { _texel_al8, 1, 1 },
		[0x1c] = { _texel_x1r5g5b5, 2, 1 },
		[0x1d] = { _texel_a4r4g4b4, 2, 1 },
		[0x1e] = { _texel_x8r8g8b8, 4, 1 },
		[0x1f] = { _texel_a8, 1, 1 },
		[0x20] = { _texel_a8l8, 2, 1 },
		[0x24] = { _texel_yuy2, 2, 1 },
		[0x25] = { _texel_uyvy, 2, 1 },
		[0x27] = { _texel_r6g5b5, 2, 0 },
		[0x28] = { _texel_g8b8, 2, 0 },
		[0x29] = { _texel_r8b8, 2, 0 },
		[0x2a] = { _texel_d24s8, 4, 0 },
		[0x2b] = { _texel_d24s8, 4, 0 },
		[0x2c] = { _texel_d16, 2, 0 },
		[0x2d] = { _texel_d16, 2, 0 },
		[0x2e] = { _texel_d24s8, 4, 1 },
		[0x2f] = { _texel_d24s8, 4, 1 },
		[0x30] = { _texel_d16, 2, 1 },
		[0x31] = { _texel_d16, 2, 1 },
		[0x32] = { _texel_l16, 2, 0 },
		[0x33] = { _texel_v16u16, 4, 0 },
		[0x35] = { _texel_l16, 2, 1 },
		[0x36] = { _texel_v16u16, 4, 1 },
		[0x37] = { _texel_r6g5b5, 2, 1 },
		[0x38] = { _texel_r5g5b5a1, 2, 0 },
		[0x39] = { _texel_r4g4b4a4, 2, 0 },
		[0x3a] = { _texel_a8b8g8r8, 4, 0 },
		[0x3b] = { _texel_b8g8r8a8, 4, 0 },
		[0x3c] = { _texel_r8g8b8a8, 4, 0 },
		[0x3d] = { _texel_r5g5b5a1, 2, 1 },
		[0x3e] = { _texel_r4g4b4a4, 2, 1 },
		[0x3f] = { _texel_a8b8g8r8, 4, 1 },
		[0x40] = { _texel_b8g8r8a8, 4, 1 },
		[0x41] = { _texel_r8g8b8a8, 4, 1 },
	};
	struct format_information unknown = { _texel_a8r8g8b8, 4, 0 };

	if (format < sizeof(table) / sizeof(table[0]) && table[format].kind != _texel_unknown)
		return table[format];
	return unknown;
}

static BOOL kind_compressed(unsigned char kind)
{
	return kind == _texel_dxt1 || kind == _texel_dxt3 || kind == _texel_dxt5;
}

/* ---------- geometry of a texture in memory */

static unsigned long floor_log2(unsigned long value)
{
	unsigned long result = 0;

	while (value > 1)
	{
		value >>= 1;
		result++;
	}
	return result;
}

static unsigned long level_dimension(unsigned long base, unsigned long level)
{
	unsigned long value = base >> level;

	return value ? value : 1;
}

void xgpu_texture_describe(DWORD format_word, DWORD size_word, struct xgpu_texture_description *description)
{
	struct format_information information;

	memset(description, 0, sizeof(*description));
	description->format = (format_word & D3DFORMAT_FORMAT_MASK) >> D3DFORMAT_FORMAT_SHIFT;
	information = format_information(description->format);
	description->cube_map = (format_word & D3DFORMAT_CUBEMAP) != 0;
	description->compressed = kind_compressed(information.kind);
	if (size_word)
	{
		description->width = (size_word & D3DSIZE_WIDTH_MASK) + 1;
		description->height = ((size_word & D3DSIZE_HEIGHT_MASK) >> D3DSIZE_HEIGHT_SHIFT) + 1;
		description->depth = 1;
		description->levels = 1;
		description->pitch = (((size_word & D3DSIZE_PITCH_MASK) >> D3DSIZE_PITCH_SHIFT) + 1) * D3DTEXTURE_PITCH_ALIGNMENT;
		description->linear = TRUE;
	}
	else
	{
		description->width = 1UL << ((format_word & D3DFORMAT_USIZE_MASK) >> D3DFORMAT_USIZE_SHIFT);
		description->height = 1UL << ((format_word & D3DFORMAT_VSIZE_MASK) >> D3DFORMAT_VSIZE_SHIFT);
		description->depth = 1UL << ((format_word & D3DFORMAT_PSIZE_MASK) >> D3DFORMAT_PSIZE_SHIFT);
		description->levels = (format_word & D3DFORMAT_MIPMAP_MASK) >> D3DFORMAT_MIPMAP_SHIFT;
		if (!description->levels)
			description->levels = 1;
		description->linear = information.linear;
		description->pitch = description->width * information.bytes;
	}
	if ((format_word & D3DFORMAT_DIMENSION_MASK) >> D3DFORMAT_DIMENSION_SHIFT != 3)
		description->depth = 1;
}

static unsigned long level_bytes(const struct xgpu_texture_description *description, unsigned long level)
{
	struct format_information information = format_information(description->format);
	unsigned long width = level_dimension(description->width, level);
	unsigned long height = level_dimension(description->height, level);
	unsigned long depth = level_dimension(description->depth, level);

	if (description->compressed)
		return ((width + 3) / 4) * ((height + 3) / 4) * information.bytes * depth;
	if (description->linear)
		return description->pitch * height;
	return width * height * depth * information.bytes;
}

unsigned long xgpu_texture_level_offset(const struct xgpu_texture_description *description, unsigned long level)
{
	unsigned long offset = 0;
	unsigned long index;

	for (index = 0; index < level && index < description->levels; index++)
		offset += level_bytes(description, index);
	return offset;
}

unsigned long xgpu_texture_face_size(const struct xgpu_texture_description *description)
{
	unsigned long size = xgpu_texture_level_offset(description, description->levels);

	if (description->cube_map)
		size = (size + D3DTEXTURE_CUBEFACE_ALIGNMENT - 1) & ~(unsigned long)(D3DTEXTURE_CUBEFACE_ALIGNMENT - 1);
	return size;
}

unsigned long xgpu_texture_level_pitch(const struct xgpu_texture_description *description, unsigned long level)
{
	struct format_information information = format_information(description->format);

	if (description->linear)
		return description->pitch;
	if (description->compressed)
		return ((level_dimension(description->width, level) + 3) / 4) * information.bytes;
	return level_dimension(description->width, level) * information.bytes;
}

/* ---------- swizzling */

struct swizzle_masks
{
	unsigned long x, y, z;
};

static struct swizzle_masks swizzle_masks(unsigned long width, unsigned long height, unsigned long depth)
{
	struct swizzle_masks masks = { 0, 0, 0 };
	unsigned long bit = 1, mask_bit = 1;
	BOOL done;

	/* bits of x, y and z alternate until each dimension runs out */
	do
	{
		done = TRUE;
		if (bit < width)
		{
			masks.x |= mask_bit;
			mask_bit <<= 1;
			done = FALSE;
		}
		if (bit < height)
		{
			masks.y |= mask_bit;
			mask_bit <<= 1;
			done = FALSE;
		}
		if (bit < depth)
		{
			masks.z |= mask_bit;
			mask_bit <<= 1;
			done = FALSE;
		}
		bit <<= 1;
	} while (!done);
	return masks;
}

static unsigned long spread(unsigned long mask, unsigned long value)
{
	unsigned long result = 0, bit = 1;

	while (value && bit)
	{
		if (mask & bit)
		{
			if (value & 1)
				result |= bit;
			value >>= 1;
		}
		bit <<= 1;
	}
	return result;
}

/* ---------- texel conversion */

static unsigned long expand5(unsigned long v) { return (v << 3) | (v >> 2); }
static unsigned long expand6(unsigned long v) { return (v << 2) | (v >> 4); }
static unsigned long expand4(unsigned long v) { return v * 0x11; }

static unsigned long argb(unsigned long a, unsigned long r, unsigned long g, unsigned long b)
{
	return (a << 24) | (r << 16) | (g << 8) | b;
}

static unsigned char clamp_byte(long value)
{
	return (unsigned char)(value < 0 ? 0 : value > 255 ? 255 : value);
}

static unsigned long yuv_to_argb(long y, long u, long v)
{
	long c = y - 16, d = u - 128, e = v - 128;

	return argb(255, clamp_byte((298 * c + 409 * e + 128) >> 8),
		clamp_byte((298 * c - 100 * d - 208 * e + 128) >> 8),
		clamp_byte((298 * c + 516 * d + 128) >> 8));
}

static unsigned long convert_texel(unsigned char kind, const unsigned char *source, const D3DCOLOR *palette,
	unsigned long x, const unsigned char *row)
{
	unsigned long v16 = source[0] | ((unsigned long)source[1] << 8);
	unsigned long v32 = v16 | ((unsigned long)source[2] << 16) | ((unsigned long)source[3] << 24);

	switch (kind)
	{
	case _texel_a8r8g8b8: return v32;
	case _texel_x8r8g8b8: return v32 | 0xff000000UL;
	case _texel_r5g6b5: return argb(255, expand5(v16 >> 11), expand6((v16 >> 5) & 0x3f), expand5(v16 & 0x1f));
	case _texel_a1r5g5b5: return argb((v16 & 0x8000) ? 255 : 0, expand5((v16 >> 10) & 0x1f), expand5((v16 >> 5) & 0x1f), expand5(v16 & 0x1f));
	case _texel_x1r5g5b5: return argb(255, expand5((v16 >> 10) & 0x1f), expand5((v16 >> 5) & 0x1f), expand5(v16 & 0x1f));
	case _texel_a4r4g4b4: return argb(expand4(v16 >> 12), expand4((v16 >> 8) & 0xf), expand4((v16 >> 4) & 0xf), expand4(v16 & 0xf));
	case _texel_l8: return argb(255, source[0], source[0], source[0]);
	case _texel_al8: return argb(source[0], source[0], source[0], source[0]);
	case _texel_a8: return argb(source[0], 255, 255, 255);
	case _texel_a8l8: return argb(source[1], source[0], source[0], source[0]);
	case _texel_p8: return palette ? palette[source[0]] : argb(255, source[0], source[0], source[0]);
	/* V8U8 shares this format: U (the low byte) reads as red, V as green */
	case _texel_g8b8: return argb(255, source[0], source[1], 0);
	case _texel_r8b8: return argb(255, source[1], 0, source[0]);
	case _texel_r6g5b5: return argb(255, expand6(v16 >> 10), expand5((v16 >> 5) & 0x1f), expand5(v16 & 0x1f));
	case _texel_l16: return argb(255, source[1], source[1], source[1]);
	case _texel_v16u16: return argb(255, source[1], source[3], 0);
	case _texel_a8b8g8r8: return argb(source[3], source[0], source[1], source[2]);
	case _texel_b8g8r8a8: return argb(source[0], source[1], source[2], source[3]);
	case _texel_r8g8b8a8: return argb(source[0], source[3], source[2], source[1]);
	case _texel_r5g5b5a1: return argb((v16 & 1) ? 255 : 0, expand5(v16 >> 11), expand5((v16 >> 6) & 0x1f), expand5((v16 >> 1) & 0x1f));
	case _texel_r4g4b4a4: return argb(expand4(v16 & 0xf), expand4(v16 >> 12), expand4((v16 >> 8) & 0xf), expand4((v16 >> 4) & 0xf));
	case _texel_yuy2:
	{
		const unsigned char *pair = row + (x & ~1UL) * 2;

		return yuv_to_argb(pair[(x & 1) ? 2 : 0], pair[1], pair[3]);
	}
	case _texel_uyvy:
	{
		const unsigned char *pair = row + (x & ~1UL) * 2;

		return yuv_to_argb(pair[(x & 1) ? 3 : 1], pair[0], pair[2]);
	}
	case _texel_d24s8: return argb(255, source[3], source[3], source[3]);
	case _texel_d16: return argb(255, source[1], source[1], source[1]);
	default: return v32;
	}
}

/* The image's texels are bytes, blue first (dk_commands.h): a native 0xAARRGGBB on a big-endian machine is in memory alpha first, so it is swapped. */
static unsigned long texel_bytes(unsigned long argb)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	return __builtin_bswap32((unsigned int)argb);
#else
	return argb;
#endif
}

/* one level (or 3D slice set) of an uncompressed texture into BGRA */
static void decode_level(const struct xgpu_texture_description *description, unsigned long level,
	const unsigned char *source, const D3DCOLOR *palette, unsigned long *destination)
{
	struct format_information information = format_information(description->format);
	unsigned long width = level_dimension(description->width, level);
	unsigned long height = level_dimension(description->height, level);
	unsigned long depth = level_dimension(description->depth, level);
	unsigned long x, y, z;

	if (description->linear)
	{
		for (y = 0; y < height; y++)
		{
			const unsigned char *row = source + y * description->pitch;

			for (x = 0; x < width; x++)
				destination[y * width + x] = texel_bytes(convert_texel(information.kind, row + x * information.bytes, palette, x, row));
		}
		return;
	}
	{
		struct swizzle_masks masks = swizzle_masks(width, height, depth);
		unsigned long *x_offsets = malloc(width * sizeof(unsigned long));

		for (x = 0; x < width; x++)
			x_offsets[x] = spread(masks.x, x);
		for (z = 0; z < depth; z++)
		{
			unsigned long z_offset = spread(masks.z, z);

			for (y = 0; y < height; y++)
			{
				unsigned long y_offset = spread(masks.y, y) | z_offset;

				for (x = 0; x < width; x++)
				{
					const unsigned char *texel = source + (x_offsets[x] | y_offset) * information.bytes;

					destination[(z * height + y) * width + x] = texel_bytes(convert_texel(information.kind, texel, palette, x, texel));
				}
			}
		}
		free(x_offsets);
	}
}
/* ---------- upload */

/* the bytes of a decoded level: 32 bits a texel, whatever the format was */
static unsigned long decoded_bytes(const struct xgpu_texture_description *description, unsigned long level)
{
	return level_dimension(description->width, level) * level_dimension(description->height, level) *
		level_dimension(description->depth, level) * 4;
}

static uint32_t compressed_format(unsigned char kind)
{
	switch (kind)
	{
	case _texel_dxt1: return DK_TEXTURE_BC1;
	case _texel_dxt3: return DK_TEXTURE_BC2;
	default: return DK_TEXTURE_BC3;
	}
}

/* Sends a texture's texels to the host as image id. A compressed texture is
read where the game keeps it; any other is decoded (all its faces and levels,
each level after the last, with no padding) into a buffer the host reads when
it runs the command, which the stream frees once it has been handed over. */
static uint32_t custom_edition_texels_command_channels(unsigned long address);

static void upload(uint32_t id, uint32_t kind, const struct xgpu_texture_description *description,
	const unsigned char *base, const D3DCOLOR *palette)
{
	struct format_information information = format_information(description->format);
	unsigned long face_count = description->cube_map ? 6 : 1;
	unsigned long face_size = xgpu_texture_face_size(description);
	struct dk_command_texture *command;
	unsigned char *decoded = NULL;
	unsigned long face, level, total = 0;

	if (description->compressed)
	{
		command = dk_stream_command(DK_COMMAND_TEXTURE, sizeof(*command));
		command->format = compressed_format(information.kind);
		command->source = (uint32_t)(uintptr_t)base;
		command->face_bytes = (uint32_t)face_size;
		command->source_bytes = (uint32_t)(face_size * (face_count - 1) + xgpu_texture_level_offset(description, description->levels));
	}
	else
	{
		unsigned long per_face = 0;
		unsigned char *at;
		STARTUP_TIMER(decoding);

		for (level = 0; level < description->levels; level++)
			per_face += decoded_bytes(description, level);
		total = per_face * face_count;
		decoded = malloc(total);
		if (!decoded)
			return;
		at = decoded;
		for (face = 0; face < face_count; face++)
		{
			for (level = 0; level < description->levels; level++)
			{
				decode_level(description, level, base + face * face_size + xgpu_texture_level_offset(description, level),
					palette, (unsigned long *)at);
				at += decoded_bytes(description, level);
			}
		}
		STARTUP_ADD("tex_decode", decoding, 1, total);
		command = dk_stream_command(DK_COMMAND_TEXTURE, sizeof(*command));
		command->format = DK_TEXTURE_BGRA;
		command->source = (uint32_t)(uintptr_t)decoded;
		command->face_bytes = (uint32_t)per_face;
		command->source_bytes = (uint32_t)total;
	}
	command->id = id;
	command->kind = kind;
	command->width = (uint32_t)description->width;
	command->height = (uint32_t)description->height;
	command->depth = (uint32_t)description->depth;
	command->levels = (uint32_t)description->levels;
	command->channels = custom_edition_texels_command_channels((unsigned long)base);
	/* (last: it may hand the stream over, which the command is in) */
	if (decoded)
		dk_stream_free_after_handover(decoded, total);
	else
		/* the host reads a compressed texture's texels where the game keeps them when it runs the command: a write to them before then hands the stream over first (d3d8_gx2.c);
		noted once the command is written, as noting can hand the stream over */
		dk_stream_note_read(base, face_size * (face_count - 1) + xgpu_texture_level_offset(description, description->levels));
}

/* ---------- cache */

struct texture_entry
{
	struct texture_entry *next;
	DWORD data, format_word, size_word;
	unsigned long palette_hash;
	/* the image the host makes of it, 0 until the first upload is sent */
	uint32_t id;
	uint32_t kind;
	struct xgpu_texture_description description;
	unsigned long address, size;
	unsigned long generation;
	unsigned long last_used_frame;
	/* the high-res HUD bitmap that stands for it (hud_hires.h), or -1 */
	long override;
	/* its menu art's image and levels (0: none), as of menu_art_serial */
	unsigned long menu_serial;
	uint32_t menu_id;
	unsigned long menu_levels;
	/* the image was still being made when it was looked up (halo/menu_smooth.txt): looked up again when the builder has finished one since (hud_built_seen) */
	unsigned char menu_pending;
	unsigned long menu_built_seen;
};

#define TEXTURE_BUCKET_COUNT 4096
#define TEXTURE_IDLE_FRAMES 1800
#define MAXIMUM_PALETTE_VARIANTS 8

static struct texture_entry *texture_buckets[TEXTURE_BUCKET_COUNT];

/* the image numbers (the host's table has DK_TEXTURE_LIMIT of them, 0 being
no texture): handed out in order, and taken back when a texture is dropped */
static uint32_t id_next = 1;
static uint32_t id_free[DK_TEXTURE_LIMIT];
static uint32_t id_free_count;

static uint32_t id_take(void)
{
	if (id_free_count)
		return id_free[--id_free_count];
	if (id_next >= DK_TEXTURE_LIMIT)
		return 0;
	return id_next++;
}

/* Draws mostly bind the textures the draws before them bound. A lookup of a
texture that is not palettized is remembered with the memory watch serial it
started at: while no watched page has been written since, and no texture
has been dropped, the same lookup finds the same current texture. */
#define RECENT_TEXTURE_COUNT 64

static struct
{
	DWORD data, format_word, size_word;
	struct texture_entry *entry;
	unsigned long watch_serial;
	unsigned long drop_serial;
} recent_textures[RECENT_TEXTURE_COUNT];
static unsigned long texture_drop_serial = 1;
static unsigned long texture_frame = 0;

static unsigned long bucket_index(DWORD data, DWORD format_word, DWORD size_word)
{
	return ((data >> 7) ^ (format_word * 2654435761UL) ^ size_word) % TEXTURE_BUCKET_COUNT;
}

/* palettized textures are cached per palette contents: the game rewrites
palettes freely, and often cycles a texture through a few of them */
static unsigned long palette_hash_compute(const D3DCOLOR *palette)
{
	unsigned long hash = 2166136261UL, index;

	for (index = 0; index < 256; index++)
		hash = (hash ^ palette[index]) * 16777619UL;
	return hash ? hash : 1;
}

/* a palette's hash is asked for at every draw that uses it (256 multiplications, over a microsecond): remembered while the memory watch says the palette has not been written (a
D3DPalette_Lock announces its writes) */
static unsigned long palette_hash(const D3DCOLOR *palette)
{
	static struct
	{
		const D3DCOLOR *palette;
		unsigned long generation, hash;
	} remembered[8];
	const unsigned slot = (unsigned)(((uintptr_t)palette >> 10) & 7);
	unsigned long generation;

	if (!palette)
		return 0;
	if (!platform_is_contiguous(palette))
		return palette_hash_compute(palette); /* (writes outside the window are not watched) */
	generation = memory_watch_generation((unsigned long)(uintptr_t)palette, 256 * sizeof(D3DCOLOR));
	if (remembered[slot].palette == palette && remembered[slot].generation == generation && remembered[slot].hash)
		return remembered[slot].hash;
	remembered[slot].palette = palette;
	remembered[slot].generation = generation;
	remembered[slot].hash = palette_hash_compute(palette);
	return remembered[slot].hash;
}

/* ---------- high-res replacements */

/* the most bytes of rows one command names: small enough that the buffer is
seldom split between two of the host's window chunks, which sends it
through the staging buffer instead */
#define ROWS_PIECE_BYTES (1024 * 1024)

/* sends rows [top, top + rows) of a level width texels wide, whose BGRA
texels are in buffer (malloc'd; the stream frees it once handed over) */
static void rows_send(uint32_t id, unsigned long level, unsigned long width, unsigned long top, unsigned long rows,
	unsigned char *buffer)
{
	struct dk_command_texture_rows *command = dk_stream_command(DK_COMMAND_TEXTURE_ROWS, sizeof(*command));

	command->id = id;
	command->level = (uint32_t)level;
	command->top = (uint32_t)top;
	command->rows = (uint32_t)rows;
	command->source = (uint32_t)(uintptr_t)buffer;
	command->source_bytes = (uint32_t)(rows * width * 4);
	/* (last: it may hand the stream over, which the command is in) */
	dk_stream_free_after_handover(buffer, rows * width * 4);
}

/* makes 2D BGRA image id, its texels left for rows_send */
static void image_make(uint32_t id, unsigned long width, unsigned long height, unsigned long levels)
{
	struct dk_command_texture *command = dk_stream_command(DK_COMMAND_TEXTURE, sizeof(*command));

	command->id = id;
	command->kind = DK_TEXTURE_2D;
	command->format = DK_TEXTURE_BGRA;
	command->width = (uint32_t)width;
	command->height = (uint32_t)height;
	command->depth = 1;
	command->levels = (uint32_t)levels;
	command->source = 0;
	command->face_bytes = 0;
	command->source_bytes = 0;
	command->channels = DK_TEXTURE_CHANNELS_AS_STORED;
}

/* sends a whole level of BGRA texels, a piece at a time */
static BOOL level_send(uint32_t id, unsigned long level, unsigned long width, unsigned long height,
	const unsigned char *texels)
{
	unsigned long piece_rows = ROWS_PIECE_BYTES / (width * 4), top;

	if (!piece_rows)
		piece_rows = 1;
	for (top = 0; top < height; top += piece_rows)
	{
		unsigned long rows = height - top < piece_rows ? height - top : piece_rows;
		unsigned char *buffer = malloc(rows * width * 4);

		if (!buffer)
			return FALSE;
		memcpy(buffer, texels + top * width * 4, rows * width * 4);
		rows_send(id, level, width, top, rows, buffer);
	}
	return TRUE;
}

/* a PNG's texels and all its mip levels, ready to be sent: the level 0 (blue first) and the rest, one level after another */
struct png_chain
{
	unsigned long width, height, levels;
	unsigned char *base, *rest;
	unsigned long bytes;
	/* (the milliseconds of the decoding and of the levels, for the log line of the HUD's sheets: hud_image) */
	unsigned long decode_ms, mips_ms;
};

unsigned long system_milliseconds(void);

static unsigned long png_level_count(unsigned long width, unsigned long height)
{
	unsigned long largest, levels = 1;

	for (largest = width > height ? width : height; largest > 1; largest >>= 1)
		levels++;
	return levels;
}

static unsigned long png_level_size(unsigned long dimension, unsigned long level)
{
	return (dimension >> level) ? dimension >> level : 1;
}

static void png_chain_free(struct png_chain *chain)
{
	free(chain->base);
	free(chain->rest);
	memset(chain, 0, sizeof(*chain));
}

/* the PNG's texels decoded, with their levels made (the mean of the 2 by 2 texels above each: wiiu_png_chain.h); 1 if made. How the image comes to be
sent is png_image's; this is the work of the game thread's first frame that can be done earlier, on another thread (dk_texture_prewarm_png) */
static int png_chain_build(const void *png, unsigned long size, struct png_chain *chain)
{
	unsigned long level, rest_bytes = 0;
	STARTUP_TIMER(decoding);
	const unsigned long decode_started = system_milliseconds();

	memset(chain, 0, sizeof(*chain));
	chain->base = hud_hires_png_pixels(png, size, &chain->width, &chain->height);
	STARTUP_ADD("png_decode", decoding, 1, chain->base ? chain->width * chain->height * 4 : 0);
	if (!chain->base)
		return 0;
	chain->decode_ms = system_milliseconds() - decode_started;
	chain->levels = png_level_count(chain->width, chain->height);
	chain->bytes = chain->width * chain->height * 4;
	/* (red first to blue first) */
	wiiu_png_swap_red_blue((uint32_t *)chain->base, chain->width * chain->height);
	for (level = 1; level < chain->levels; level++)
		rest_bytes += png_level_size(chain->width, level) * png_level_size(chain->height, level) * 4;
	if (rest_bytes)
	{
		STARTUP_TIMER(averaging);
		const unsigned long mips_started = system_milliseconds();
		const unsigned char *above = chain->base;
		unsigned char *at;

		chain->rest = malloc(rest_bytes);
		if (!chain->rest)
		{
			png_chain_free(chain);
			return 0;
		}
		at = chain->rest;
		for (level = 1; level < chain->levels; level++)
		{
			const unsigned long above_width = png_level_size(chain->width, level - 1), above_height = png_level_size(chain->height, level - 1);
			const unsigned long next_width = png_level_size(chain->width, level), next_height = png_level_size(chain->height, level);

			wiiu_png_next_level((const uint32_t *)above, above_width, above_height, (uint32_t *)at, next_width, next_height);
			above = at;
			at += next_width * next_height * 4;
		}
		STARTUP_ADD("png_mips", averaging, chain->levels - 1, rest_bytes);
		chain->bytes += rest_bytes;
		chain->mips_ms = system_milliseconds() - mips_started;
	}
	return 1;
}

/* the images made ahead of the game thread's asking (menu art that the main menu will draw, decoded while the game loads its map): by the PNG's address
and size. A slot is working (the helper thread has it), ready, or free. The game thread that wants one that is being made waits for it; one that is not
there it makes itself, as before. */
#define PREPARED_LIMIT 128
/* (the menus' art uses the first 48 slots, as it always did; the HUD's images all of them) */
#define PREPARED_MENU_SLOTS 48
#define PREPARED_BYTES_LIMIT (32ul << 20)
enum { PREPARED_FREE, PREPARED_WORKING, PREPARED_READY };
_Static_assert(PREPARED_FREE == WIIU_HUD_AHEAD_NONE && PREPARED_WORKING == WIIU_HUD_AHEAD_WORKING && PREPARED_READY == WIIU_HUD_AHEAD_READY,
	"wiiu_hud_sheet_next is told the state of a made-ahead image as the prepared slots have it");

static struct
{
	const void *png;
	unsigned long size;
	int state;
	struct png_chain chain;
} prepared[PREPARED_LIMIT];
static unsigned long prepared_bytes;
static pthread_mutex_t prepared_lock;
static pthread_cond_t prepared_changed;
static volatile int prepared_ready_to_use;
/* the images the builder (and the menus' preload thread) have finished: the frame's begin looks every texture up again when it has grown (hud_built_seen) */
static volatile unsigned long hud_built_count;
static unsigned long hud_built_seen;

static void prepared_init(void)
{
	if (prepared_ready_to_use == 2)
		return;
	if (__sync_bool_compare_and_swap(&prepared_ready_to_use, 0, 1))
	{
		pthread_mutex_init(&prepared_lock, NULL);
		pthread_cond_init(&prepared_changed, NULL);
		__sync_synchronize();
		prepared_ready_to_use = 2;
	}
	else
	{
		while (prepared_ready_to_use != 2)
			;
	}
}

/* (any thread) the image of a PNG made, to be had by png_image when it asks; the PNG's bytes stay where they are */
void dk_texture_prewarm_png(const void *png, unsigned long size);
/* the same with the most the made images may hold (the menus' art, 32 MB; the HUD's: wiiu_hud_prewarm); 1 if the image is made or was, 0 if not */
static int prewarm_png(const void *png, unsigned long size, unsigned long bytes_limit, int slot_limit);

void dk_texture_prewarm_png(const void *png, unsigned long size)
{
	prewarm_png(png, size, PREPARED_BYTES_LIMIT, PREPARED_MENU_SLOTS);
}

static int prewarm_png(const void *png, unsigned long size, unsigned long bytes_limit, int slot_limit)
{
	int slot, free_slot = -1;
	struct png_chain chain;
	int made;

	if (!png || !size)
		return 0;
	prepared_init();
	pthread_mutex_lock(&prepared_lock);
	for (slot = 0; slot < PREPARED_LIMIT; slot++)
	{
		if (prepared[slot].state != PREPARED_FREE && prepared[slot].png == png && prepared[slot].size == size)
		{
			pthread_mutex_unlock(&prepared_lock);
			return 1;
		}
		if (prepared[slot].state == PREPARED_FREE && free_slot < 0 && slot < slot_limit)
			free_slot = slot;
	}
	if (free_slot < 0 || prepared_bytes >= bytes_limit)
	{
		pthread_mutex_unlock(&prepared_lock);
		return 0;
	}
	prepared[free_slot].png = png;
	prepared[free_slot].size = size;
	prepared[free_slot].state = PREPARED_WORKING;
	pthread_mutex_unlock(&prepared_lock);
	made = png_chain_build(png, size, &chain);
	pthread_mutex_lock(&prepared_lock);
	if (made)
	{
		prepared[free_slot].chain = chain;
		prepared[free_slot].state = PREPARED_READY;
		prepared_bytes += chain.bytes;
	}
	else
	{
		prepared[free_slot].state = PREPARED_FREE;
		prepared[free_slot].png = NULL;
	}
	pthread_cond_broadcast(&prepared_changed);
	pthread_mutex_unlock(&prepared_lock);
	if (made)
		__sync_fetch_and_add(&hud_built_count, 1);
	return made;
}

/* the image made ahead, handed over (and its slot freed); 0 if there is none (or the making failed) */
static int prepared_take(const void *png, unsigned long size, struct png_chain *chain)
{
	int slot, found = 0;

	if (prepared_ready_to_use != 2)
		return 0;
	pthread_mutex_lock(&prepared_lock);
	for (;;)
	{
		for (slot = 0; slot < PREPARED_LIMIT; slot++)
		{
			if (prepared[slot].state != PREPARED_FREE && prepared[slot].png == png && prepared[slot].size == size)
				break;
		}
		if (slot == PREPARED_LIMIT)
			break;
		if (prepared[slot].state == PREPARED_WORKING)
		{
			pthread_cond_wait(&prepared_changed, &prepared_lock);
			continue;
		}
		*chain = prepared[slot].chain;
		prepared_bytes -= chain->bytes;
		memset(&prepared[slot], 0, sizeof(prepared[slot]));
		found = 1;
		break;
	}
	pthread_mutex_unlock(&prepared_lock);
	return found;
}

/* whether the image of a PNG is made ahead (PREPARED_READY), being made (PREPARED_WORKING) or not there (PREPARED_FREE), without waiting for it */
static int prepared_state(const void *png, unsigned long size)
{
	int slot, state = PREPARED_FREE;

	if (prepared_ready_to_use != 2)
		return PREPARED_FREE;
	pthread_mutex_lock(&prepared_lock);
	for (slot = 0; slot < PREPARED_LIMIT; slot++)
	{
		if (prepared[slot].state != PREPARED_FREE && prepared[slot].png == png && prepared[slot].size == size)
		{
			state = prepared[slot].state;
			break;
		}
	}
	pthread_mutex_unlock(&prepared_lock);
	return state;
}

/* the images made ahead of the HUD's sheets (a slot whose PNG is one of hud_hires_embedded's; a slot being made is left) let go; how many, and the bytes they held */
static unsigned prepared_drop_hud(unsigned long *bytes)
{
	int slot;
	unsigned dropped = 0;

	*bytes = 0;
	if (prepared_ready_to_use != 2)
		return 0;
	pthread_mutex_lock(&prepared_lock);
	for (slot = 0; slot < PREPARED_LIMIT; slot++)
	{
		unsigned long asset;

		if (prepared[slot].state != PREPARED_READY)
			continue;
		for (asset = 0; asset < hud_hires_embedded_count; asset++)
		{
			if (hud_hires_embedded[asset].png == prepared[slot].png)
				break;
		}
		if (asset == hud_hires_embedded_count)
			continue;
		*bytes += prepared[slot].chain.bytes;
		prepared_bytes -= prepared[slot].chain.bytes;
		png_chain_free(&prepared[slot].chain);
		memset(&prepared[slot], 0, sizeof(prepared[slot]));
		dropped++;
	}
	pthread_mutex_unlock(&prepared_lock);
	return dropped;
}

/* what the last image png_image made took, for the HUD's log line (the game thread's alone): the milliseconds of the decoding and of the levels (a chain made ahead
says what its maker took), of the copies into pieces and the sending, and the bytes of the image with its levels */
static struct
{
	unsigned long decode_ms, mips_ms, send_ms, send_started, bytes;
} png_last;

/* an image of a PNG (8-bit RGBA) with all its mip levels, as
hud_hires_png_texture makes in GL: its number, or 0 */
static uint32_t png_image(const void *png, unsigned long size, unsigned long *levels, unsigned long *width_out, unsigned long *height_out)
{
	struct png_chain chain;
	unsigned long level;
	uint32_t id;
	STARTUP_TIMER(whole);

	if (!prepared_take(png, size, &chain) && !png_chain_build(png, size, &chain))
		return 0;
	id = id_take();
	if (!id)
	{
		png_chain_free(&chain);
		return 0;
	}
	*levels = chain.levels;
	if (width_out)
		*width_out = chain.width;
	if (height_out)
		*height_out = chain.height;
	png_last.decode_ms = chain.decode_ms;
	png_last.mips_ms = chain.mips_ms;
	png_last.bytes = chain.bytes;
	png_last.send_started = system_milliseconds();
	image_make(id, chain.width, chain.height, chain.levels);
	{
		const unsigned char *at = chain.rest;

		for (level = 0; level < chain.levels; level++)
		{
			const unsigned long width = png_level_size(chain.width, level), height = png_level_size(chain.height, level);
			STARTUP_TIMER(sending);

			level_send(id, level, width, height, level ? at : chain.base);
			STARTUP_ADD("png_send", sending, 1, width * height * 4);
			if (level)
				at += width * height * 4;
		}
	}
	png_chain_free(&chain);
	png_last.send_ms = system_milliseconds() - png_last.send_started;
	STARTUP_ADD("png_all", whole, 1, 0);
	return id;
}

/* the text's atlas (text_hires.h): its image, made and its changed rows sent
when data is its placeholder bitmap's; 0 if not */
static uint32_t atlas_image(unsigned long data)
{
	static uint32_t id;
	static BOOL failed;
	long size, top, bottom;
	const unsigned char *coverage;
	STARTUP_TIMER(started);

	if (failed)
		return 0;
	coverage = text_hires_atlas_rows(data, &size, id == 0, &top, &bottom);
	if (!coverage)
		return 0;
	STARTUP_ADD("atlas_rows", started, 1, 0);
	if (!id)
	{
		id = id_take();
		if (!id)
		{
			failed = TRUE;
			return 0;
		}
		image_make(id, (unsigned long)size, (unsigned long)size, 1);
	}
	while (top < bottom)
	{
		/* (white, the glyph's coverage its alpha, as the maps' fonts are) */
		long rows = ROWS_PIECE_BYTES / (size * 4);
		unsigned char *buffer;

		if (rows > bottom - top)
			rows = bottom - top;
		buffer = malloc((size_t)rows * size * 4);
		if (!buffer)
			break;
		wiiu_png_coverage_to_white((uint32_t *)buffer, coverage + top * size, (unsigned long)(rows * size));
		rows_send(id, 0, (unsigned long)size, (unsigned long)top, (unsigned long)rows, buffer);
		top += rows;
	}
	STARTUP_ADD("atlas_all", started, 1, 0);
	return id;
}

/* a menu's art (menu_files.h), by its file: made on first use, and kept.

A picture of 65,536 pixels or more is not decoded on the game thread when the menu first draws it (halo/menu_smooth.txt, 1 by default; 0 puts back the game thread's
making of them): the 700 to 1,400 ms hitches of the console's logs, in the "hud" part of the rendering, with next to no draws and no file time, each time a menu
screen's large background was first drawn and the preload thread (dk_texture_prewarm_png) had not made it (it holds 32 MB and the main menu's screens first). The
lookup asks the builder thread (the HUD's: menu_request) and answers 0 meanwhile, so the game's own bitmap, a transparent 4 by 4, is drawn and the picture appears
when the builder has finished it, a frame after (xgpu_texture_cache_begin_frame looks the textures up again; the copies into pieces and the sending stay the game
thread's). A picture the builder could not make is made on the game thread, as before. */
#define MAXIMUM_MENU_IMAGES 256
#define MENU_ASYNC_MIN_PIXELS 65536ul

static struct
{
	char *name;
	uint32_t id;
	unsigned long levels;
	/* the PNG, while the image is not made */
	const unsigned char *png;
	unsigned long size;
	unsigned char pending, big;
	long request;
	unsigned long asked_at;
} menu_images[MAXIMUM_MENU_IMAGES];
static long menu_image_count;
/* whether menu_image's last answer was 0 because the picture is being made (the lookup is made again when the builder has finished one) */
static int menu_image_pending;
/* the pictures made on the game thread though they are large, and the ones the builder made ahead (for the log line) */
static unsigned menu_image_here, menu_image_ahead;

static int menu_async_on(void);
static long menu_request(const void *png, unsigned long size);
static int menu_request_done(long request);

/* the image made (1), or still to be (0); the game thread's */
static int menu_image_make(long index)
{
	const unsigned long started = system_milliseconds();
	int ahead = 0;
	const int asked = menu_images[index].request >= 0;

	switch (wiiu_hud_sheet_next(menu_images[index].big, menu_images[index].big, prepared_state(menu_images[index].png, menu_images[index].size), asked,
		asked ? menu_request_done(menu_images[index].request) : 0))
	{
	case WIIU_HUD_SHEET_TAKE_AHEAD:
		ahead = 1;
		break;
	case WIIU_HUD_SHEET_ASK:
		menu_images[index].request = menu_request(menu_images[index].png, menu_images[index].size);
		menu_images[index].asked_at = started;
		if (menu_images[index].request >= 0)
			return 0;
		break;
	case WIIU_HUD_SHEET_WAIT:
		return 0;
	default:
		break;
	}
	menu_images[index].id = png_image(menu_images[index].png, menu_images[index].size, &menu_images[index].levels, NULL, NULL);
	menu_images[index].pending = 0;
	if (!menu_images[index].id)
		platform_log("menus: could not draw %s", menu_images[index].name);
	else if (menu_images[index].big)
	{
		static unsigned told;

		if (ahead)
			menu_image_ahead++;
		else
			menu_image_here++;
		if (told < 48)
		{
			told++;
			platform_log("menus: %s made %s, %lu ms of the game thread's (%lu ms after it was asked for; %u made ahead, %u here so far)", menu_images[index].name,
				ahead ? "ahead on the builder thread" : "on the game thread", system_milliseconds() - started,
				menu_images[index].asked_at ? started - menu_images[index].asked_at : 0ul, menu_image_ahead, menu_image_here);
		}
	}
	return 1;
}

static uint32_t menu_image(unsigned long data, unsigned long *levels)
{
	const char *name = menu_art_name(data);
	unsigned long size = 0;
	long index;

	menu_image_pending = 0;
	if (!name)
		return 0;
	for (index = 0; index < menu_image_count; index++)
	{
		if (!strcmp(menu_images[index].name, name))
		{
			if (menu_images[index].pending && !menu_image_make(index))
			{
				menu_image_pending = 1;
				*levels = 1;
				return 0;
			}
			*levels = menu_images[index].levels;
			return menu_images[index].id;
		}
	}
	if (menu_image_count == MAXIMUM_MENU_IMAGES)
		return 0;
	/* (failed ones are remembered too, as image 0) */
	menu_images[menu_image_count].name = strdup(name);
	if (!menu_images[menu_image_count].name)
		return 0;
	menu_images[menu_image_count].request = -1;
	menu_images[menu_image_count].png = menu_art_png(name, &size);
	menu_images[menu_image_count].size = size;
	if (!menu_images[menu_image_count].png)
	{
		platform_log("menus: could not draw %s", name);
		*levels = menu_images[menu_image_count].levels;
		return menu_images[menu_image_count++].id;
	}
	{
		/* (the PNG's header says its size: width and height, 4 bytes each, from byte 16) */
		const unsigned char *header = menu_images[menu_image_count].png;
		const unsigned long width = size >= 24 ? (unsigned long)header[16] << 24 | (unsigned long)header[17] << 16 | (unsigned long)header[18] << 8 | header[19] : 0;
		const unsigned long height = size >= 24 ? (unsigned long)header[20] << 24 | (unsigned long)header[21] << 16 | (unsigned long)header[22] << 8 | header[23] : 0;

		menu_images[menu_image_count].big = menu_async_on() && width && height && width <= 16384 && height <= 16384 && width * height >= MENU_ASYNC_MIN_PIXELS;
	}
	menu_images[menu_image_count].pending = 1;
	index = menu_image_count++;
	if (!menu_image_make(index))
	{
		menu_image_pending = 1;
		*levels = 1;
		return 0;
	}
	*levels = menu_images[index].levels;
	return menu_images[index].id;
}

/* a high-res HUD bitmap (hud_hires.h): made on first use, and kept until the map goes (wiiu_hud_map_unloaded) */
static struct
{
	uint32_t id;
	unsigned long levels;
	/* the multiplier of v the image is drawn with: 1, or the sheet's height over the rows of the copy the Wii U embeds in its place (wiiu_hud_sheet.h) */
	float v_scale;
	BOOL tried;
	/* the sheets that are not made on the game thread (hud_sheets.txt): handed to the builder thread, the lookup that found it not yet made, the builder's
	end of it (1 made ahead, 2 not: the game thread makes it itself), when it was asked for and the milliseconds the builder took */
	unsigned char asked, pending;
	volatile unsigned char built;
	unsigned long asked_at, builder_ms;
} *hud_images;

/* the HUD's sheets of the map that has gone (wiiu_hud_map_unloaded): their images freed at the next frame (xgpu_texture_cache_begin_frame; the GX2 thread frees the
memory a few frames on, with the draws that used them done), and every texture looked up again */
static uint32_t *hud_to_free;
static unsigned hud_to_free_count;

/* the sheets made (the log line of every HUD sheet says how many are held, and their bytes with their levels) */
static unsigned hud_held_count;
static unsigned long hud_held_bytes;

long wiiu_study_number(const char *name, long fallback);
unsigned wiiu_thread_core_id(void);
void wiiu_thread_background(const char *name);

/* ---------- the HUD's sheets without the game thread's waiting (halo/hud_sheets.txt)

A sheet of a million pixels or more cost the game thread 0.2 to 0.95 s the first time the HUD drew it (the decoding, the levels, the copies: the 700 to 1,600 ms
hitches of 2026-11-06). With this on (the default; halo/hud_sheets.txt 0 puts back the game thread's making of them), the draw that first needs such a sheet asks a
thread of its own (below the game thread's priority, on core 0: wiiu_thread_background) to decode it and make its levels with the menus' prepared images
(prewarm_png), and meanwhile draws the game's own bitmap, as it does with display.high_res_hud off. The builder says when it has made one (hud_built_count); the next
frame's begin (xgpu_texture_cache_begin_frame) makes every texture be looked up again, and the lookup that finds the image ready sends it (the copies of the levels
into pieces, the part of the cost left to the game thread) and draws the sheet from then on. The map's HUD list (hud_hires_map_loaded) asks for the meters, then the
other sheets that are not scope masks, up to HUD_PREFETCH_BYTES, so they are mostly ready before the HUD's first frame. A sheet the builder could not make (no
memory for its levels) is made by the game thread, as before. When the map goes, the sheets are let go (wiiu_hud_map_unloaded). */
#define HUD_ASYNC_MIN_PIXELS 1000000ul
#define HUD_QUEUE 64
/* the builder makes an image while the images made ahead hold less than they do at that moment plus this (an image of the larger sheets is 8.4 MB with levels) */
#define HUD_ASYNC_HOLD (12ul << 20)
/* the most the map's HUD list asks for at its load (the sheets' bytes with their levels, from the PNGs' sizes) */
#define HUD_PREFETCH_BYTES (24ul << 20)

static pthread_mutex_t hud_queue_lock;
static pthread_cond_t hud_queue_wake;
static long hud_queue[HUD_QUEUE];
static unsigned hud_queue_head, hud_queue_count;
/* (the game thread's) 0 not started, 1 the builder runs, -1 it could not be made */
static int hud_queue_state;

static int hud_async_on(void)
{
	static int on = -1;

	if (on < 0)
	{
		on = wiiu_study_number("hud_sheets.txt", 1) > 0;
		platform_log("hud sheets: the big sheets are made %s (halo/hud_sheets.txt)", on ? "on a thread of their own, the game's own bitmap drawn until one is ready" : "by the game thread when first drawn");
	}
	return on;
}

/* the menus' pictures asked of the builder (menu_image; the queue holds -1 - the index): the PNG, and whether the builder has finished with it */
#define MENU_REQUESTS 128
static struct menu_request
{
	const void *png;
	unsigned long size;
	volatile unsigned char done;
	unsigned long builder_ms;
} menu_requests[MENU_REQUESTS];
static unsigned long menu_request_next;

static int hud_sheet_is_big(const struct hud_hires_embedded *embedded)
{
	return !embedded->title && (unsigned long)embedded->width * embedded->height >= HUD_ASYNC_MIN_PIXELS;
}

static void *hud_builder_main(void *unused)
{
	(void)unused;
	wiiu_thread_background("hudsheets");
	platform_log("hud sheets: the builder thread runs on core %u", wiiu_thread_core_id());
	for (;;)
	{
		const struct hud_hires_embedded *embedded;
		unsigned long started;
		long asset;
		int made;

		pthread_mutex_lock(&hud_queue_lock);
		while (!hud_queue_count)
			pthread_cond_wait(&hud_queue_wake, &hud_queue_lock);
		asset = hud_queue[hud_queue_head];
		hud_queue_head = (hud_queue_head + 1) % HUD_QUEUE;
		hud_queue_count--;
		pthread_mutex_unlock(&hud_queue_lock);
		if (asset < 0)
		{
			/* (a menu's picture: menu_request) */
			struct menu_request *request = &menu_requests[-1 - asset];

			started = system_milliseconds();
			prewarm_png(request->png, request->size, prepared_bytes + HUD_ASYNC_HOLD, PREPARED_LIMIT);
			request->builder_ms = system_milliseconds() - started;
			__sync_synchronize();
			request->done = 1;
			__sync_fetch_and_add(&hud_built_count, 1);
			continue;
		}
		embedded = &hud_hires_embedded[asset];
		started = system_milliseconds();
		made = prewarm_png(embedded->png, embedded->png_size, prepared_bytes + HUD_ASYNC_HOLD, PREPARED_LIMIT);
		hud_images[asset].builder_ms = system_milliseconds() - started;
		__sync_synchronize();
		hud_images[asset].built = made ? 1 : 2;
		__sync_fetch_and_add(&hud_built_count, 1);
	}
	return NULL;
}

/* (the game thread) an entry handed to the builder (an HUD asset, or -1 - a menu request); FALSE if it could not be */
static BOOL builder_enqueue(long entry)
{
	if (hud_queue_state == 0)
	{
		pthread_t thread;
		pthread_attr_t attributes;

		prepared_init();
		pthread_mutex_init(&hud_queue_lock, NULL);
		pthread_cond_init(&hud_queue_wake, NULL);
		pthread_attr_init(&attributes);
		pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
		pthread_attr_setstacksize(&attributes, 64 * 1024);
		hud_queue_state = pthread_create(&thread, &attributes, hud_builder_main, NULL) == 0 ? 1 : -1;
		pthread_attr_destroy(&attributes);
	}
	if (hud_queue_state < 0)
		return FALSE;
	pthread_mutex_lock(&hud_queue_lock);
	if (hud_queue_count == HUD_QUEUE)
	{
		pthread_mutex_unlock(&hud_queue_lock);
		return FALSE;
	}
	hud_queue[(hud_queue_head + hud_queue_count++) % HUD_QUEUE] = entry;
	pthread_cond_signal(&hud_queue_wake);
	pthread_mutex_unlock(&hud_queue_lock);
	return TRUE;
}

/* (the game thread) the sheet handed to the builder; FALSE if it could not be (the game thread then makes it) */
static BOOL hud_request(long asset)
{
	if (!builder_enqueue(asset))
		return FALSE;
	hud_images[asset].asked = 1;
	hud_images[asset].built = 0;
	hud_images[asset].asked_at = system_milliseconds();
	return TRUE;
}

static int menu_async_on(void)
{
	static int on = -1;

	if (on < 0)
	{
		on = wiiu_study_number("menu_smooth.txt", 1) > 0;
		platform_log("menus: the large pictures are made %s (halo/menu_smooth.txt)", on ? "on the builder thread, nothing drawn in their place until one is ready" : "by the game thread when first drawn");
	}
	return on;
}

/* (the game thread) a menu's picture handed to the builder: its request, or -1 if it could not be (the game thread makes it) */
static long menu_request(const void *png, unsigned long size)
{
	const long request = (long)(menu_request_next++ % MENU_REQUESTS);

	menu_requests[request].png = png;
	menu_requests[request].size = size;
	menu_requests[request].done = 0;
	menu_requests[request].builder_ms = 0;
	__sync_synchronize();
	if (!builder_enqueue(-1 - request))
		return -1;
	return request;
}

static int menu_request_done(long request)
{
	return menu_requests[request].done != 0;
}

static BOOL hud_images_ensure(void)
{
	if (!hud_images)
	{
		hud_images = calloc(hud_hires_embedded_count, sizeof(*hud_images));
		hud_to_free = calloc(hud_hires_embedded_count, sizeof(*hud_to_free));
		if (!hud_images || !hud_to_free)
		{
			/* (no table, no sheets: every lookup answers 0, and the game's own bitmaps are drawn) */
			free(hud_images);
			free(hud_to_free);
			hud_images = NULL;
			hud_to_free = NULL;
			return FALSE;
		}
	}
	return TRUE;
}

/* whether the last lookup of the sheet found it being made (its draw uses the game's own bitmap, which the texture entry has uploaded) */
static BOOL hud_sheet_pending(long asset)
{
	return hud_images && asset >= 0 && asset < (long)hud_hires_embedded_count && hud_images[asset].pending;
}

static uint32_t hud_image(long asset, unsigned long *levels, float *v_scale)
{
	if (asset < 0 || asset >= (long)hud_hires_embedded_count || !hud_images_ensure())
		return 0;
	if (!hud_images[asset].tried)
	{
		const struct hud_hires_embedded *embedded = &hud_hires_embedded[asset];
		/* (the first draw that needs the image decodes it, makes its levels and sends it, on the game thread: the 700 to 1,600 ms hitches in the HUD pass of the console's
		logs. Said by name, once each, to tell which bitmap a hitch was, and how big the image the GX2 thread is to find memory for) */
		static unsigned told;
		const unsigned long started = system_milliseconds();
		unsigned long width = 0, height = 0;
		unsigned long asked_for = 0;
		int ahead = 0;

		hud_images[asset].pending = 0;
		switch (wiiu_hud_sheet_next(hud_async_on(), hud_sheet_is_big(embedded), prepared_state(embedded->png, embedded->png_size), hud_images[asset].asked,
			hud_images[asset].built != 0))
		{
		case WIIU_HUD_SHEET_TAKE_AHEAD:
			ahead = 1;
			asked_for = hud_images[asset].asked ? started - hud_images[asset].asked_at : 0;
			break;
		case WIIU_HUD_SHEET_ASK:
			if (!hud_request(asset))
				break;
			/* fall through */
		case WIIU_HUD_SHEET_WAIT:
			hud_images[asset].pending = 1;
			*levels = 1;
			if (v_scale)
				*v_scale = 1.0f;
			return 0;
		default:
			break;
		}
		/* (made here: not a big sheet, or the builder is off, could not be started or could not make it) */
		hud_images[asset].tried = TRUE;
		hud_images[asset].v_scale = 1.0f;
		hud_images[asset].id = png_image(embedded->png, embedded->png_size, &hud_images[asset].levels, &width, &height);
		if (!hud_images[asset].id)
			platform_log("high-res hud: could not decode the texture for %s bitmap %d", embedded->tag,
				embedded->bitmap);
		else
		{
			/* (the Wii U embeds a smaller copy of some sheets: the table has the sheet's size, the image its own) */
			if (wiiu_hud_sheet_fits(embedded->width, embedded->height, width, height))
				hud_images[asset].v_scale = wiiu_hud_sheet_v_scale(embedded->width, embedded->height, width, height);
			else
				platform_log("high-res hud: the image for %s bitmap %d is %lux%lu, which does not stand for its sheet's %ux%u: drawn unscaled", embedded->tag,
					embedded->bitmap, width, height, embedded->width, embedded->height);
			hud_held_count++;
			hud_held_bytes += png_last.bytes;
			if (told < 160)
			{
				told++;
				platform_log("high-res hud: image %u made for %s bitmap %d (the game's bitmap is %ux%u; the image is %lux%lu with %lu levels, v scale %g) in %lu ms on the game thread: decode %lu, levels %lu, send %lu%s; %u held, %lu KB",
					(unsigned)hud_images[asset].id, embedded->tag, embedded->bitmap, embedded->width, embedded->height, width, height, hud_images[asset].levels,
					(double)hud_images[asset].v_scale, system_milliseconds() - started, ahead ? 0ul : png_last.decode_ms, ahead ? 0ul : png_last.mips_ms, png_last.send_ms,
					ahead ? "" : " (all here: not made ahead)", hud_held_count, hud_held_bytes >> 10);
				if (ahead)
					platform_log("high-res hud: image %u was made ahead on the builder thread in %lu ms (decode %lu, levels %lu), %lu ms after it was asked for",
						(unsigned)hud_images[asset].id, hud_images[asset].builder_ms, png_last.decode_ms, png_last.mips_ms, asked_for);
			}
		}
	}
	*levels = hud_images[asset].levels;
	if (v_scale)
		*v_scale = hud_images[asset].v_scale;
	return hud_images[asset].id;
}

/* ---------- the HUD's images made ahead (halo/hud_prewarm.txt)

The high-res HUD's sheets are decoded when the HUD first draws them, on the game thread (hud_image, png_image): 2026-11-06, in a joined game on Ratrace, the
first frames of the HUD took 1559, 1214, 768 and 923 ms (the log's hitch lines: "its longest part of the rendering was hud"), one more sheet each time something new
came into view, about 8.6 million pixels a second of the console's decoding (the start-up timeline's png_decode); the second map of the session, drawing the same
sheets from the images kept, had none. The PC build decodes them on a thread of its own as the map loads (hud_hires.c, HUD_HIRES_DECODE_AHEAD); this does the same
with the menus' prepared images (prewarm_png): the images are the same ones png_image makes, only decoded before the HUD asks.

The file halo/hud_prewarm.txt holds the largest sheet to make ahead in thousands of pixels (1100: the sheets of a million pixels and under, 5.3 MB each with their
mip levels; 4200 all of them, 22 MB each); no file, or 0, makes nothing ahead (the game thread decodes as it always did). The made images hold at most
HUD_PREWARM_BYTES between them (and what the menus' art still holds) and wait in memory until the HUD asks for them. */
#define HUD_PREWARM_BYTES (16ul << 20)
#define HUD_PREWARM_ASSETS 256

void wiiu_thread_off_game_core(void);

static long hud_prewarm_list[HUD_PREWARM_ASSETS];
static long hud_prewarm_count;
static unsigned long hud_prewarm_largest;
static volatile int hud_prewarm_running;

static void *hud_prewarm_main(void *unused)
{
	long index, made = 0, skipped = 0, refused = 0;
	/* (what the menus' art still holds is not the HUD's to use: the limit is over it, fixed here) */
	const unsigned long limit = prepared_bytes + HUD_PREWARM_BYTES;
	STARTUP_TIMER(started);

	(void)unused;
	wiiu_thread_off_game_core();
	for (index = 0; index < hud_prewarm_count; index++)
	{
		const long asset = hud_prewarm_list[index];
		const struct hud_hires_embedded *embedded;

		if (asset < 0 || asset >= (long)hud_hires_embedded_count)
			continue;
		embedded = &hud_hires_embedded[asset];
		/* (titles are the menus', sheets above the file's size are left to the game thread, and one already made is not made again) */
		if (embedded->title || (unsigned long)embedded->width * embedded->height > hud_prewarm_largest ||
			(hud_images && hud_images[asset].tried))
		{
			skipped++;
			continue;
		}
		if (prewarm_png(embedded->png, embedded->png_size, limit, PREPARED_LIMIT))
			made++;
		else
			refused++;
	}
	platform_log("hud prewarm: %ld sheets made ahead of the HUD (%ld left to it, %ld not made: the images held are at their limit), %lu KB held, %lu ms",
		made, skipped, refused, prepared_bytes >> 10, STARTUP_SINCE_MS(started));
	hud_prewarm_running = 0;
	return NULL;
}

/* (the game thread, a map has loaded: hud_hires_tags.c) the assets the map's HUD may draw; their images are made on a thread of its own */
void wiiu_hud_prewarm(const long *assets, long count);

/* the sheets of the map's HUD asked of the builder thread (hud_sheets.txt), the meters first and then the others that are not scope masks (a zoom is not at every
map's start), while their images with levels stay within HUD_PREFETCH_BYTES; the PNGs' sizes are in their headers */
static void hud_prefetch(const long *assets, long count)
{
	unsigned long bytes = 0;
	long index, asked = 0;
	int pass;

	if (!hud_images_ensure())
		return;
	for (pass = 0; pass < 2; pass++)
	{
		for (index = 0; index < count; index++)
		{
			const long asset = assets[index];
			const struct hud_hires_embedded *embedded;
			const unsigned char *png;
			unsigned long width, height, image_bytes;

			if (asset < 0 || asset >= (long)hud_hires_embedded_count)
				continue;
			embedded = &hud_hires_embedded[asset];
			png = (const unsigned char *)embedded->png;
			if (!hud_sheet_is_big(embedded) || hud_images[asset].tried || hud_images[asset].asked || embedded->png_size < 24 ||
				(pass == 0) != (embedded->coverage != 0) || (pass == 1 && strstr(embedded->tag, "scope")))
				continue;
			width = ((unsigned long)png[16] << 24) | ((unsigned long)png[17] << 16) | ((unsigned long)png[18] << 8) | png[19];
			height = ((unsigned long)png[20] << 24) | ((unsigned long)png[21] << 16) | ((unsigned long)png[22] << 8) | png[23];
			image_bytes = width * height * 4 + width * height * 4 / 3;
			if (bytes + image_bytes > HUD_PREFETCH_BYTES || !hud_request(asset))
				continue;
			bytes += image_bytes;
			asked++;
		}
	}
	platform_log("hud sheets: %ld of the map's %ld HUD bitmaps asked of the builder thread ahead of the HUD's first frame (%lu KB with levels)", asked, count, bytes >> 10);
}

/* (the game thread, a map has gone: hud_hires_map_unloaded) the sheets' images let go, the ones made ahead and not drawn too: the next map asks for its own, and the
heap does not keep the blocks of a map that has gone (the sheets are the 22 MB blocks of the console's "no memory for a 2048x2048 texture") */
void wiiu_hud_map_unloaded(void);

void wiiu_hud_map_unloaded(void)
{
	unsigned long asset, dropped_bytes;
	unsigned freed = 0, dropped;

	if (!hud_images)
		return;
	for (asset = 0; asset < hud_hires_embedded_count; asset++)
	{
		if (hud_images[asset].id)
		{
			hud_to_free[hud_to_free_count++] = hud_images[asset].id;
			freed++;
		}
		hud_images[asset].id = 0;
		hud_images[asset].levels = 0;
		hud_images[asset].v_scale = 1.0f;
		hud_images[asset].tried = FALSE;
		hud_images[asset].pending = 0;
		hud_images[asset].asked = 0;
	}
	dropped = prepared_drop_hud(&dropped_bytes);
	if (freed || dropped)
		platform_log("hud sheets: the map has gone: %u sheets freed (%lu KB held with levels), %u made ahead and not drawn let go (%lu KB)", freed, hud_held_bytes >> 10, dropped,
			dropped_bytes >> 10);
	hud_held_count = 0;
	hud_held_bytes = 0;
}

void wiiu_hud_prewarm(const long *assets, long count)
{
	pthread_t thread;
	pthread_attr_t attributes;
	long largest;

	if (hud_async_on())
	{
		hud_prefetch(assets, count);
		return;
	}
	largest = wiiu_study_number("hud_prewarm.txt", 0);
	if (largest <= 0 || count <= 0 || hud_prewarm_running)
		return;
	prepared_init();
	if (count > HUD_PREWARM_ASSETS)
		count = HUD_PREWARM_ASSETS;
	memcpy(hud_prewarm_list, assets, (size_t)count * sizeof(*assets));
	hud_prewarm_count = count;
	hud_prewarm_largest = (unsigned long)largest * 1000ul;
	hud_prewarm_running = 1;
	pthread_attr_init(&attributes);
	pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
	pthread_attr_setstacksize(&attributes, 64 * 1024);
	if (pthread_create(&thread, &attributes, hud_prewarm_main, NULL) != 0)
		hud_prewarm_running = 0;
	pthread_attr_destroy(&attributes);
}

/* an entry's image and description: its replacement's, if it has one (as
texture_entry_result in xbox_textures.c), with the bitmap's own size, which
its coordinates are in; *atlas: whether it is the text's atlas */
static uint32_t texture_entry_result(struct texture_entry *entry, int *kind, struct xgpu_texture_description *description, int *atlas)
{
	unsigned long levels = 1;
	float v_scale = 1.0f;
	uint32_t id;

	*kind = (int)entry->kind;
	*description = entry->description;
	*atlas = 0;
	if ((id = atlas_image(entry->data)) != 0)
	{
		*kind = DK_TEXTURE_2D;
		description->levels = 1;
		*atlas = 1;
		return id;
	}
	/* (looked up again only when the art registered has changed: the search
	was 1.3% of the game thread, for every texture of every draw) */
	if (entry->menu_serial != menu_art_serial() || (entry->menu_pending && entry->menu_built_seen != hud_built_seen))
	{
		entry->menu_serial = menu_art_serial();
		entry->menu_levels = 1;
		entry->menu_id = menu_image(entry->data, &entry->menu_levels);
		entry->menu_pending = (unsigned char)menu_image_pending;
		entry->menu_built_seen = hud_built_seen;
	}
	levels = entry->menu_levels;
	if ((id = entry->menu_id) != 0)
	{
		*kind = DK_TEXTURE_2D;
		description->levels = levels;
		description->hires = TRUE;
		return id;
	}
	/* (a placeholder the game draws some sprites of a bitmap from: a button icon in a prompt, hud_hires_tags.c) */
	{
		const long asset = hud_hires_placeholder_asset(entry->data);

		if (asset >= 0 && (id = hud_image(asset, &levels, &v_scale)) != 0)
		{
			*kind = DK_TEXTURE_2D;
			description->levels = levels;
			description->hires = TRUE;
			description->hires_v_scale = v_scale;
			description->hires_coverage = hud_hires_override_coverage(asset);
			description->hires_point_threshold = hud_hires_override_point_threshold(asset);
			return id;
		}
	}
	if (entry->override >= 0 && (id = hud_image(entry->override, &levels, &v_scale)) != 0)
	{
		*kind = DK_TEXTURE_2D;
		description->levels = levels;
		description->hires = TRUE;
		description->hires_v_scale = v_scale;
		description->hires_coverage = hud_hires_override_coverage(entry->override);
		description->hires_point_threshold = hud_hires_override_point_threshold(entry->override);
		return id;
	}
	return entry->id;
}

unsigned long dk_texture_serial(void)
{
	return texture_drop_serial;
}

static uint32_t dk_texture_get_inner(const DWORD *resource, const D3DCOLOR *palette, int *kind,
	struct xgpu_texture_description *description, int *lasting);

uint32_t dk_texture_get(const DWORD *resource, const D3DCOLOR *palette, int *kind,
	struct xgpu_texture_description *description, int *lasting)
{
	STARTUP_TIMER(started);
	uint32_t id = dk_texture_get_inner(resource, palette, kind, description, lasting);

	STARTUP_ADD("tex_get", started, 1, 0);
	return id;
}

static uint32_t dk_texture_get_inner(const DWORD *resource, const D3DCOLOR *palette, int *kind,
	struct xgpu_texture_description *description, int *lasting)
{
	DWORD data = resource[1], format_word = resource[3], size_word = resource[4];
	struct texture_entry **bucket = &texture_buckets[bucket_index(data, format_word, size_word)];
	struct texture_entry *entry;
	unsigned long generation;
	BOOL palettized = ((format_word & D3DFORMAT_FORMAT_MASK) >> D3DFORMAT_FORMAT_SHIFT) == 0x0b;
	unsigned long hash = palettized ? palette_hash(palette) : 0;

	struct texture_entry *oldest_variant = NULL;
	unsigned long variant_count = 0;
	static int no_cache = -1;
	unsigned long recent = bucket_index(data, format_word, size_word) % RECENT_TEXTURE_COUNT;
	unsigned long watch_serial = memory_watch_serial();
	uint32_t id;
	int atlas;

	if (lasting)
		*lasting = 0;
	if (!palettized && recent_textures[recent].entry && recent_textures[recent].data == data &&
		recent_textures[recent].format_word == format_word && recent_textures[recent].size_word == size_word &&
		recent_textures[recent].watch_serial == watch_serial &&
		recent_textures[recent].drop_serial == texture_drop_serial)
	{
		entry = recent_textures[recent].entry;
		entry->last_used_frame = texture_frame;
		id = texture_entry_result(entry, kind, description, &atlas);
		if (lasting)
			*lasting = !atlas;
		return id;
	}

	for (entry = *bucket; entry; entry = entry->next)
	{
		if (entry->data == data && entry->format_word == format_word && entry->size_word == size_word)
		{
			if (entry->palette_hash == hash)
				break;
			variant_count++;
			if (!oldest_variant || entry->last_used_frame < oldest_variant->last_used_frame)
				oldest_variant = entry;
		}
	}
	if (!entry && variant_count >= MAXIMUM_PALETTE_VARIANTS)
	{
		/* a palette that keeps changing reuses the stalest copy */
		entry = oldest_variant;
		entry->palette_hash = hash;
		entry->generation = 0;
	}
	if (!entry)
	{
		uint32_t id = id_take();

		if (!id)
			return 0;
		entry = calloc(1, sizeof(*entry));
		entry->data = data;
		entry->format_word = format_word;
		entry->size_word = size_word;
		entry->palette_hash = hash;
		xgpu_texture_describe(format_word, size_word, &entry->description);
		entry->kind = entry->description.cube_map ? DK_TEXTURE_CUBE : entry->description.depth > 1 ? DK_TEXTURE_3D :
			DK_TEXTURE_2D;
		entry->address = (unsigned long)PLATFORM_PHYSICAL_TO_VIRTUAL(data);
		entry->size = xgpu_texture_face_size(&entry->description) * (entry->description.cube_map ? 6 : 1);
		entry->generation = 0;
		entry->override = -1;
		entry->id = id;
		entry->next = *bucket;
		*bucket = entry;
	}

	if (no_cache < 0)
		no_cache = config_boolean("debug.texture_no_cache");
	generation = memory_watch_generation(entry->address, entry->size);
	if (!entry->generation || generation > entry->generation || no_cache)
	{
		/* mark first, so a write racing with the upload is noticed */
		memory_watch_protect(entry->address, entry->size);
		entry->generation = memory_watch_generation(entry->address, entry->size);
		if (!entry->generation)
			entry->generation = 1;
		/* (which bitmap is here may have changed with the pixels) */
		entry->override = -1;
		if (!palettized && !entry->description.cube_map && entry->description.depth == 1)
		{
			unsigned long levels;

			entry->override = hud_hires_override_find(entry->address, entry->description.width,
				entry->description.height, entry->description.levels > 1 ?
				xgpu_texture_level_offset(&entry->description, 1) : xgpu_texture_face_size(&entry->description));
			/* (a sheet being made on the builder thread: the game's own bitmap is uploaded now and drawn until it is ready) */
			if (entry->override >= 0 && !hud_image(entry->override, &levels, NULL) && !hud_sheet_pending(entry->override))
				entry->override = -1;
		}
		if ((entry->override < 0 || hud_sheet_pending(entry->override)) && platform_is_contiguous((void *)entry->address) &&
			platform_is_contiguous((void *)(entry->address + entry->size - 1)))
		{
			if (config_boolean("debug.texture_log"))
				platform_log("texture upload %08lx fmt %02lx %lux%lu size %lu gen %lu id %u", (unsigned long)data,
					(unsigned long)entry->description.format, entry->description.width, entry->description.height,
					entry->size, entry->generation, (unsigned)entry->id);
			upload(entry->id, entry->kind, &entry->description, (const unsigned char *)entry->address, palette);
		}
	}
	entry->last_used_frame = texture_frame;
	if (!palettized && !no_cache)
	{
		recent_textures[recent].data = data;
		recent_textures[recent].format_word = format_word;
		recent_textures[recent].size_word = size_word;
		recent_textures[recent].entry = entry;
		recent_textures[recent].watch_serial = watch_serial;
		recent_textures[recent].drop_serial = texture_drop_serial;
	}
	id = texture_entry_result(entry, kind, description, &atlas);
	if (lasting)
		*lasting = !palettized && !no_cache && !atlas;
	return id;
}

void xgpu_texture_cache_begin_frame(void)
{
	unsigned long index;

	texture_frame++;
	/* (a sheet the builder has made since the last frame: the textures are looked up again, and the lookup that finds it sends it) */
	if (hud_built_seen != hud_built_count)
	{
		hud_built_seen = hud_built_count;
		texture_drop_serial++;
	}
	if (hud_to_free_count)
	{
		unsigned freed;

		for (freed = 0; freed < hud_to_free_count; freed++)
		{
			struct dk_command_texture_free *command = dk_stream_command(DK_COMMAND_TEXTURE_FREE, sizeof(*command));

			command->id = hud_to_free[freed];
			id_free[id_free_count++] = hud_to_free[freed];
		}
		hud_to_free_count = 0;
		texture_drop_serial++;
	}
	if (texture_frame % 600)
		return;
	/* drop textures that have not been used for a while */
	for (index = 0; index < TEXTURE_BUCKET_COUNT; index++)
	{
		struct texture_entry **link = &texture_buckets[index];

		while (*link)
		{
			struct texture_entry *entry = *link;

			if (texture_frame - entry->last_used_frame > TEXTURE_IDLE_FRAMES)
			{
				struct dk_command_texture_free *command = dk_stream_command(DK_COMMAND_TEXTURE_FREE, sizeof(*command));

				command->id = entry->id;
				*link = entry->next;
				id_free[id_free_count++] = entry->id;
				texture_drop_serial++;
				free(entry);
			}
			else
			{
				link = &entry->next;
			}
		}
	}
}

/* ---------- Custom Edition channel orders (platform.h)

Halo PC keeps what some textures hold in other channels than the game reads
it from (enum custom_edition_channel_order): a model shader's multipurpose
masks, and a HUD meter's shape and fill order. The Custom Edition map's
loading says which texels hold which order as they arrive
(port/linux/game/custom_edition_bitmaps.c), and a texture made of them is
sampled with each channel taken from where Halo PC keeps it, by its image's
component map (wiiu_gx2.c), as xbox_textures.c's texture swizzle samples it:
the texels stay as compressed as they were. Addresses stay listed until other
texels arrive there, which the loading also says, or the map goes; the game
and this cache share a thread.

A meter drawn from a texture whose texels are in this build's order is
sampled as one by xbox_textures.c's pixel shaders on a Custom Edition map's
HUD (its meter channels key); this renderer's shaders are the shader pack's,
which has no such key yet, so here such a meter is drawn as its texture is. */

/* for each order, the channel (red, green, blue, alpha) of the texels each
channel is sampled from (xbox_textures.c's) */
static const unsigned char custom_edition_channel_sources[NUMBER_OF_CUSTOM_EDITION_CHANNEL_ORDERS][4] =
{
	{ 0, 1, 2, 3 },
	/* specular, self-illumination, color change and the auxiliary mask */
	{ 2, 1, 3, 0 },
	/* the fill order in color, the shape in alpha */
	{ 3, 3, 3, 0 },
	/* Explicit Xbox-order HUD artwork must not be converted per draw. */
	{ 0, 1, 2, 3 },
};

struct custom_edition_texels
{
	unsigned long address;
	unsigned char channel_order;
};

static struct custom_edition_texels *custom_edition_texels;
static unsigned long custom_edition_texel_count;
static unsigned long custom_edition_texel_capacity;

/* the order of the texels at address */
static unsigned char custom_edition_texels_order(unsigned long address)
{
	unsigned long index;

	for (index = 0; index < custom_edition_texel_count; index++)
	{
		if (custom_edition_texels[index].address == address)
			return custom_edition_texels[index].channel_order;
	}
	return _custom_edition_channels_xbox;
}

/* the texture command's channels for the texels at address
(dk_command_texture) */
static uint32_t custom_edition_texels_command_channels(unsigned long address)
{
	const unsigned char *sources = custom_edition_channel_sources[custom_edition_texels_order(address)];

	return (uint32_t)sources[0] | (uint32_t)sources[1] << 8 | (uint32_t)sources[2] << 16 | (uint32_t)sources[3] << 24;
}

void halo_custom_edition_texels_channels(const void *texels, unsigned char channel_order)
{
	unsigned long address = (unsigned long)texels;
	unsigned long index;

	if (channel_order >= NUMBER_OF_CUSTOM_EDITION_CHANNEL_ORDERS)
		channel_order = _custom_edition_channels_xbox;
	for (index = 0; index < custom_edition_texel_count && custom_edition_texels[index].address != address; index++)
	{
	}
	if (index < custom_edition_texel_count)
	{
		if (channel_order == _custom_edition_channels_xbox)
			custom_edition_texels[index] = custom_edition_texels[--custom_edition_texel_count];
		else
			custom_edition_texels[index].channel_order = channel_order;
	}
	else if (channel_order != _custom_edition_channels_xbox)
	{
		if (custom_edition_texel_count == custom_edition_texel_capacity)
		{
			unsigned long capacity = custom_edition_texel_capacity ? custom_edition_texel_capacity * 2 : 64;
			struct custom_edition_texels *grown = realloc(custom_edition_texels, capacity * sizeof(*grown));

			if (!grown)
			{
				platform_log("no memory to list the texels at %08lx: they are sampled in Halo PC's channel order",
					address);
				return;
			}
			custom_edition_texels = grown;
			custom_edition_texel_capacity = capacity;
		}
		custom_edition_texels[custom_edition_texel_count].address = address;
		custom_edition_texels[custom_edition_texel_count].channel_order = channel_order;
		custom_edition_texel_count++;
	}
}

void halo_custom_edition_texels_forget(void)
{
	free(custom_edition_texels);
	custom_edition_texels = NULL;
	custom_edition_texel_count = 0;
	custom_edition_texel_capacity = 0;
}

/* (hud_draw.c: a Custom Edition HUD sheet's meter drawn, which xbox_textures.c samples in its own channels: not here,
above) */
void halo_custom_edition_hud_meter(int active)
{
	(void)active;
}
