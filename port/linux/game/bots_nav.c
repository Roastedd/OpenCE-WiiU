/*
BOTS_NAV.C

The bots' navigation graph (bots_nav.h): the pack's bytes, the lookup of a point's area and the search over the areas. Pure code: no
engine calls, no files. See the header for the model.

The pack (little-endian, read and written byte by byte):

	header, 32 bytes:   "HALOBOTN", u16 pack version, u16 nav format, u32 map count, u32 index offset, u32 crc32 of everything after
	                    the header, u32 the bake tool's build, u32 0
	index, 48 bytes each: map name (32, zero padded), u32 collision surfaces of the map's BSP 0, u32 its walkable ones, u32 record
	                    offset, u32 record size
	record:  64 bytes header (u32 flags, u16 areas, links, places, scripts, f32 bounds min and max, f32 run speed, jump height, drop
	                    height, 16 bytes reserved), then the areas (24 bytes), the links (20), the scripts (24) and the places (32)
	area:   f32 x y z, u16 first link, u16 link count, u16 radius (1/16), u16 region, u8 flags, u8 exposure
	link:   u16 to, u8 type, u8 flags, u16 cost (1/16 s), u16 script, i16 x3 exit (relative to the area's anchor, 1/64), i16 x3 entry
	        (relative to the target's anchor, 1/64)
	script: f32 x y z launch, u16 yaw (65536 = a turn), u16 run-up (1/64), u16 flight ticks, u8 jump, u8 successes, u8 attempts, 3 pad
	place:  u8 type, u8 team, u16 area, f32 x y z, u16 game types, u16 period, u16 item class, u16 aux, u32 source block index, 4 pad
*/

#include "bots_nav.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define NAV_PI 3.14159265f

/* ---------- bytes */

static unsigned long nav_u16(unsigned char const *p)
{
	return (unsigned long)p[0] | ((unsigned long)p[1] << 8);
}

static unsigned long nav_u32(unsigned char const *p)
{
	return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static long nav_i16(unsigned char const *p)
{
	long value = (long)nav_u16(p);

	return value >= 32768 ? value - 65536 : value;
}

static float nav_f32(unsigned char const *p)
{
	unsigned long bits = nav_u32(p);
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

static void nav_put_u16(unsigned char *p, unsigned long value)
{
	p[0] = (unsigned char)(value & 0xFF);
	p[1] = (unsigned char)((value >> 8) & 0xFF);
}

static void nav_put_u32(unsigned char *p, unsigned long value)
{
	p[0] = (unsigned char)(value & 0xFF);
	p[1] = (unsigned char)((value >> 8) & 0xFF);
	p[2] = (unsigned char)((value >> 16) & 0xFF);
	p[3] = (unsigned char)((value >> 24) & 0xFF);
}

static void nav_put_f32(unsigned char *p, float value)
{
	unsigned long bits;

	memcpy(&bits, &value, sizeof(bits));
	nav_put_u32(p, bits);
}

static long nav_quantize(float value, float scale)
{
	float q = (float)floor(value * scale + 0.5f);

	if (q > 32767.0f)
		q = 32767.0f;
	if (q < -32768.0f)
		q = -32768.0f;
	return (long)q;
}

static void nav_put_i16(unsigned char *p, long value)
{
	nav_put_u16(p, (unsigned long)(value & 0xFFFF));
}

/* ---------- crc */

unsigned long bots_nav_crc32(unsigned long crc, unsigned char const *bytes, size_t count)
{
	static unsigned long table[256];
	static int made;
	size_t index;

	if (!made)
	{
		unsigned long n, k;

		for (n = 0; n < 256; n++)
		{
			unsigned long c = n;

			for (k = 0; k < 8; k++)
				c = (c & 1) ? 0xEDB88320UL ^ (c >> 1) : c >> 1;
			table[n] = c;
		}
		made = 1;
	}
	crc = ~crc & 0xFFFFFFFFUL;
	for (index = 0; index < count; index++)
		crc = table[(crc ^ bytes[index]) & 0xFF] ^ (crc >> 8);
	return ~crc & 0xFFFFFFFFUL;
}

/* ---------- the lookup grid */

static int nav_build_grid(struct bots_nav *nav)
{
	long area, cell, cells;
	unsigned long *counts;
	unsigned short *items;
	unsigned long total = 0;

	nav->cells_x = (long)ceil((nav->bounds_max[0] - nav->bounds_min[0]) / BOTS_NAV_CELL) + 1;
	nav->cells_y = (long)ceil((nav->bounds_max[1] - nav->bounds_min[1]) / BOTS_NAV_CELL) + 1;
	if (nav->cells_x < 1 || nav->cells_y < 1 || nav->cells_x > 1024 || nav->cells_y > 1024)
		return 0;
	cells = nav->cells_x * nav->cells_y;
	counts = (unsigned long *)calloc((size_t)cells + 1, sizeof(unsigned long));
	if (!counts)
		return 0;
	for (area = 0; area < nav->area_count; area++)
	{
		struct bots_nav_area const *a = &nav->areas[area];
		long x0 = (long)floor((a->x - a->radius - nav->bounds_min[0]) / BOTS_NAV_CELL);
		long x1 = (long)floor((a->x + a->radius - nav->bounds_min[0]) / BOTS_NAV_CELL);
		long y0 = (long)floor((a->y - a->radius - nav->bounds_min[1]) / BOTS_NAV_CELL);
		long y1 = (long)floor((a->y + a->radius - nav->bounds_min[1]) / BOTS_NAV_CELL);
		long x, y;

		if (x0 < 0) x0 = 0;
		if (y0 < 0) y0 = 0;
		if (x1 >= nav->cells_x) x1 = nav->cells_x - 1;
		if (y1 >= nav->cells_y) y1 = nav->cells_y - 1;
		for (y = y0; y <= y1; y++)
			for (x = x0; x <= x1; x++)
				counts[y * nav->cells_x + x + 1]++;
	}
	for (cell = 0; cell < cells; cell++)
		counts[cell + 1] += counts[cell];
	total = counts[cells];
	items = (unsigned short *)malloc((total ? total : 1) * sizeof(unsigned short));
	if (!items)
	{
		free(counts);
		return 0;
	}
	{
		unsigned long *fill = (unsigned long *)malloc((size_t)cells * sizeof(unsigned long));

		if (!fill)
		{
			free(counts);
			free(items);
			return 0;
		}
		memcpy(fill, counts, (size_t)cells * sizeof(unsigned long));
		for (area = 0; area < nav->area_count; area++)
		{
			struct bots_nav_area const *a = &nav->areas[area];
			long x0 = (long)floor((a->x - a->radius - nav->bounds_min[0]) / BOTS_NAV_CELL);
			long x1 = (long)floor((a->x + a->radius - nav->bounds_min[0]) / BOTS_NAV_CELL);
			long y0 = (long)floor((a->y - a->radius - nav->bounds_min[1]) / BOTS_NAV_CELL);
			long y1 = (long)floor((a->y + a->radius - nav->bounds_min[1]) / BOTS_NAV_CELL);
			long x, y;

			if (x0 < 0) x0 = 0;
			if (y0 < 0) y0 = 0;
			if (x1 >= nav->cells_x) x1 = nav->cells_x - 1;
			if (y1 >= nav->cells_y) y1 = nav->cells_y - 1;
			for (y = y0; y <= y1; y++)
				for (x = x0; x <= x1; x++)
					items[fill[y * nav->cells_x + x]++] = (unsigned short)area;
		}
		free(fill);
	}
	nav->cell_first = counts;
	nav->cell_areas = items;
	return 1;
}

void bots_nav_dispose(struct bots_nav *nav)
{
	if (!nav)
		return;
	free(nav->areas);
	free(nav->links);
	free(nav->scripts);
	free(nav->places);
	free(nav->cell_first);
	free(nav->cell_areas);
	free(nav);
}

/* ---------- parsing */

struct bots_nav *bots_nav_parse_record(char const *name, unsigned char const *bytes, size_t count, char const **why)
{
	struct bots_nav *nav;
	unsigned long areas, links, places, scripts;
	size_t need, at, index;
	char const *reason = "a record that is short";

	if (why)
		*why = NULL;
	if (count < BOTS_NAV_RECORD_HEADER_BYTES)
		goto fail;
	areas = nav_u16(bytes + 4);
	links = nav_u16(bytes + 6);
	places = nav_u16(bytes + 8);
	scripts = nav_u16(bytes + 10);
	need = BOTS_NAV_RECORD_HEADER_BYTES + areas * BOTS_NAV_AREA_BYTES + links * BOTS_NAV_LINK_BYTES + scripts * BOTS_NAV_SCRIPT_BYTES +
		places * BOTS_NAV_PLACE_BYTES;
	if (count < need)
		goto fail;
	reason = "a record with nothing in it";
	if (areas == 0 || areas > BOTS_NAV_MAXIMUM_AREAS || links > BOTS_NAV_MAXIMUM_LINKS)
		goto fail;
	reason = "no memory";
	nav = (struct bots_nav *)calloc(1, sizeof(*nav));
	if (!nav)
		goto fail;
	if (name)
	{
		strncpy(nav->name, name, BOTS_NAV_NAME_BYTES - 1);
		nav->name[BOTS_NAV_NAME_BYTES - 1] = 0;
	}
	nav->area_count = (long)areas;
	nav->link_count = (long)links;
	nav->place_count = (long)places;
	nav->script_count = (long)scripts;
	for (index = 0; index < 3; index++)
	{
		nav->bounds_min[index] = nav_f32(bytes + 12 + 4 * index);
		nav->bounds_max[index] = nav_f32(bytes + 24 + 4 * index);
	}
	nav->run_speed = nav_f32(bytes + 36);
	nav->jump_height = nav_f32(bytes + 40);
	nav->drop_height = nav_f32(bytes + 44);
	nav->areas = (struct bots_nav_area *)calloc(areas, sizeof(struct bots_nav_area));
	nav->links = (struct bots_nav_link *)calloc(links ? links : 1, sizeof(struct bots_nav_link));
	nav->scripts = (struct bots_nav_script *)calloc(scripts ? scripts : 1, sizeof(struct bots_nav_script));
	nav->places = (struct bots_nav_place *)calloc(places ? places : 1, sizeof(struct bots_nav_place));
	if (!nav->areas || !nav->links || !nav->scripts || !nav->places)
	{
		bots_nav_dispose(nav);
		goto fail;
	}
	at = BOTS_NAV_RECORD_HEADER_BYTES;
	for (index = 0; index < areas; index++, at += BOTS_NAV_AREA_BYTES)
	{
		struct bots_nav_area *a = &nav->areas[index];

		a->x = nav_f32(bytes + at);
		a->y = nav_f32(bytes + at + 4);
		a->z = nav_f32(bytes + at + 8);
		a->first_link = (unsigned short)nav_u16(bytes + at + 12);
		a->link_count = (unsigned short)nav_u16(bytes + at + 14);
		a->radius = (float)nav_u16(bytes + at + 16) / 16.0f;
		a->region = (unsigned short)nav_u16(bytes + at + 18);
		a->flags = bytes[at + 20];
		a->exposure = bytes[at + 21];
		if ((unsigned long)a->first_link + a->link_count > links)
		{
			bots_nav_dispose(nav);
			reason = "an area whose links are outside the table";
			goto fail;
		}
	}
	for (index = 0; index < links; index++, at += BOTS_NAV_LINK_BYTES)
	{
		struct bots_nav_link *l = &nav->links[index];
		long axis;

		l->to = (unsigned short)nav_u16(bytes + at);
		l->type = bytes[at + 2];
		l->flags = bytes[at + 3];
		l->cost_q = (unsigned short)nav_u16(bytes + at + 4);
		l->script = (unsigned short)nav_u16(bytes + at + 6);
		if (l->to >= areas || l->type >= NUMBER_OF_BOTS_NAV_LINK_TYPES || (l->script != 0xFFFF && l->script >= scripts))
		{
			bots_nav_dispose(nav);
			reason = "a link that leads nowhere";
			goto fail;
		}
		for (axis = 0; axis < 3; axis++)
		{
			l->exit[axis] = (float)nav_i16(bytes + at + 8 + 2 * axis) / 64.0f;
			l->entry[axis] = (float)nav_i16(bytes + at + 14 + 2 * axis) / 64.0f;
		}
	}
	/* (the portals are stored relative to the anchors: made absolute) */
	{
		long area_index;

		for (area_index = 0; area_index < nav->area_count; area_index++)
		{
			struct bots_nav_area const *a = &nav->areas[area_index];
			long k;

			for (k = 0; k < a->link_count; k++)
			{
				struct bots_nav_link *l = &nav->links[a->first_link + k];
				struct bots_nav_area const *b = &nav->areas[l->to];

				l->exit[0] += a->x;
				l->exit[1] += a->y;
				l->exit[2] += a->z;
				l->entry[0] += b->x;
				l->entry[1] += b->y;
				l->entry[2] += b->z;
			}
		}
	}
	for (index = 0; index < scripts; index++, at += BOTS_NAV_SCRIPT_BYTES)
	{
		struct bots_nav_script *s = &nav->scripts[index];

		s->launch[0] = nav_f32(bytes + at);
		s->launch[1] = nav_f32(bytes + at + 4);
		s->launch[2] = nav_f32(bytes + at + 8);
		s->yaw = (float)nav_u16(bytes + at + 12) * (2.0f * NAV_PI / 65536.0f);
		if (s->yaw > NAV_PI)
			s->yaw -= 2.0f * NAV_PI;
		s->run_up = (float)nav_u16(bytes + at + 14) / 64.0f;
		s->flight_ticks = (unsigned short)nav_u16(bytes + at + 16);
		s->jump = bytes[at + 18];
		s->successes = bytes[at + 19];
		s->attempts = bytes[at + 20];
	}
	for (index = 0; index < places; index++, at += BOTS_NAV_PLACE_BYTES)
	{
		struct bots_nav_place *p = &nav->places[index];

		p->type = bytes[at];
		p->team = bytes[at + 1];
		p->area = (unsigned short)nav_u16(bytes + at + 2);
		p->x = nav_f32(bytes + at + 4);
		p->y = nav_f32(bytes + at + 8);
		p->z = nav_f32(bytes + at + 12);
		p->game_type_mask = (unsigned short)nav_u16(bytes + at + 16);
		p->period_seconds = (unsigned short)nav_u16(bytes + at + 18);
		p->item_class = (unsigned short)nav_u16(bytes + at + 20);
		p->aux = (unsigned short)nav_u16(bytes + at + 22);
		p->source = nav_u32(bytes + at + 24);
		if (p->area != 0xFFFF && p->area >= areas)
			p->area = 0xFFFF;
	}
	if (!nav_build_grid(nav))
	{
		bots_nav_dispose(nav);
		reason = "a bounds that does not make a grid";
		goto fail;
	}
	return nav;
fail:
	if (why)
		*why = reason;
	return NULL;
}

unsigned char *bots_nav_write_record(struct bots_nav const *nav, size_t *count)
{
	size_t total = BOTS_NAV_RECORD_HEADER_BYTES + (size_t)nav->area_count * BOTS_NAV_AREA_BYTES + (size_t)nav->link_count * BOTS_NAV_LINK_BYTES +
		(size_t)nav->script_count * BOTS_NAV_SCRIPT_BYTES + (size_t)nav->place_count * BOTS_NAV_PLACE_BYTES;
	unsigned char *out = (unsigned char *)calloc(1, total);
	size_t at = BOTS_NAV_RECORD_HEADER_BYTES;
	long index, axis;

	if (!out)
		return NULL;
	nav_put_u16(out + 4, (unsigned long)nav->area_count);
	nav_put_u16(out + 6, (unsigned long)nav->link_count);
	nav_put_u16(out + 8, (unsigned long)nav->place_count);
	nav_put_u16(out + 10, (unsigned long)nav->script_count);
	for (axis = 0; axis < 3; axis++)
	{
		nav_put_f32(out + 12 + 4 * axis, nav->bounds_min[axis]);
		nav_put_f32(out + 24 + 4 * axis, nav->bounds_max[axis]);
	}
	nav_put_f32(out + 36, nav->run_speed);
	nav_put_f32(out + 40, nav->jump_height);
	nav_put_f32(out + 44, nav->drop_height);
	for (index = 0; index < nav->area_count; index++, at += BOTS_NAV_AREA_BYTES)
	{
		struct bots_nav_area const *a = &nav->areas[index];

		nav_put_f32(out + at, a->x);
		nav_put_f32(out + at + 4, a->y);
		nav_put_f32(out + at + 8, a->z);
		nav_put_u16(out + at + 12, a->first_link);
		nav_put_u16(out + at + 14, a->link_count);
		nav_put_u16(out + at + 16, (unsigned long)nav_quantize(a->radius, 16.0f));
		nav_put_u16(out + at + 18, a->region);
		out[at + 20] = a->flags;
		out[at + 21] = a->exposure;
	}
	for (index = 0; index < nav->area_count; index++)
	{
		struct bots_nav_area const *a = &nav->areas[index];
		long k;

		for (k = 0; k < a->link_count; k++, at += BOTS_NAV_LINK_BYTES)
		{
			struct bots_nav_link const *l = &nav->links[a->first_link + k];
			struct bots_nav_area const *b = &nav->areas[l->to];

			nav_put_u16(out + at, l->to);
			out[at + 2] = l->type;
			out[at + 3] = l->flags;
			nav_put_u16(out + at + 4, l->cost_q);
			nav_put_u16(out + at + 6, l->script);
			nav_put_i16(out + at + 8, nav_quantize(l->exit[0] - a->x, 64.0f));
			nav_put_i16(out + at + 10, nav_quantize(l->exit[1] - a->y, 64.0f));
			nav_put_i16(out + at + 12, nav_quantize(l->exit[2] - a->z, 64.0f));
			nav_put_i16(out + at + 14, nav_quantize(l->entry[0] - b->x, 64.0f));
			nav_put_i16(out + at + 16, nav_quantize(l->entry[1] - b->y, 64.0f));
			nav_put_i16(out + at + 18, nav_quantize(l->entry[2] - b->z, 64.0f));
		}
	}
	for (index = 0; index < nav->script_count; index++, at += BOTS_NAV_SCRIPT_BYTES)
	{
		struct bots_nav_script const *s = &nav->scripts[index];
		float turns = s->yaw / (2.0f * NAV_PI);

		if (turns < 0.0f)
			turns += 1.0f;
		nav_put_f32(out + at, s->launch[0]);
		nav_put_f32(out + at + 4, s->launch[1]);
		nav_put_f32(out + at + 8, s->launch[2]);
		nav_put_u16(out + at + 12, (unsigned long)(turns * 65536.0f) & 0xFFFF);
		nav_put_u16(out + at + 14, (unsigned long)nav_quantize(s->run_up, 64.0f));
		nav_put_u16(out + at + 16, s->flight_ticks);
		out[at + 18] = s->jump;
		out[at + 19] = s->successes;
		out[at + 20] = s->attempts;
	}
	for (index = 0; index < nav->place_count; index++, at += BOTS_NAV_PLACE_BYTES)
	{
		struct bots_nav_place const *p = &nav->places[index];

		out[at] = p->type;
		out[at + 1] = p->team;
		nav_put_u16(out + at + 2, p->area);
		nav_put_f32(out + at + 4, p->x);
		nav_put_f32(out + at + 8, p->y);
		nav_put_f32(out + at + 12, p->z);
		nav_put_u16(out + at + 16, p->game_type_mask);
		nav_put_u16(out + at + 18, p->period_seconds);
		nav_put_u16(out + at + 20, p->item_class);
		nav_put_u16(out + at + 22, p->aux);
		nav_put_u32(out + at + 24, p->source);
	}
	*count = total;
	return out;
}

/* ---------- the pack */

size_t bots_nav_pack_index_bytes(unsigned char const *header, size_t count)
{
	unsigned long maps;

	if (count < BOTS_NAV_PACK_HEADER_BYTES || memcmp(header, BOTS_NAV_MAGIC, 8) != 0)
		return 0;
	maps = nav_u32(header + 12);
	if (maps == 0 || maps > 64)
		return 0;
	return BOTS_NAV_PACK_HEADER_BYTES + (size_t)maps * BOTS_NAV_INDEX_ENTRY_BYTES;
}

int bots_nav_pack_find(unsigned char const *bytes, size_t count, char const *name, unsigned long *surfaces, unsigned long *walkable,
	unsigned long *offset, unsigned long *size, char const **why)
{
	unsigned long maps, index, index_offset;

	if (count < BOTS_NAV_PACK_HEADER_BYTES || memcmp(bytes, BOTS_NAV_MAGIC, 8) != 0)
	{
		if (why) *why = "not a navigation pack";
		return 0;
	}
	if (nav_u16(bytes + 8) != BOTS_NAV_PACK_VERSION || nav_u16(bytes + 10) != BOTS_NAV_FORMAT)
	{
		if (why) *why = "a pack of another version";
		return 0;
	}
	maps = nav_u32(bytes + 12);
	index_offset = nav_u32(bytes + 16);
	if (maps == 0 || maps > 64 || index_offset != BOTS_NAV_PACK_HEADER_BYTES ||
		count < BOTS_NAV_PACK_HEADER_BYTES + (size_t)maps * BOTS_NAV_INDEX_ENTRY_BYTES)
	{
		if (why) *why = "a pack whose index is short";
		return 0;
	}
	for (index = 0; index < maps; index++)
	{
		unsigned char const *entry = bytes + index_offset + index * BOTS_NAV_INDEX_ENTRY_BYTES;
		char entry_name[BOTS_NAV_NAME_BYTES + 1];
		size_t k;

		memcpy(entry_name, entry, BOTS_NAV_NAME_BYTES);
		entry_name[BOTS_NAV_NAME_BYTES] = 0;
		for (k = 0; entry_name[k]; k++)
			if (entry_name[k] >= 'A' && entry_name[k] <= 'Z')
				entry_name[k] = (char)(entry_name[k] - 'A' + 'a');
		if (strcmp(entry_name, name) == 0)
		{
			*surfaces = nav_u32(entry + 32);
			*walkable = nav_u32(entry + 36);
			*offset = nav_u32(entry + 40);
			*size = nav_u32(entry + 44);
			if (*size < BOTS_NAV_RECORD_HEADER_BYTES || *size > BOTS_NAV_MAXIMUM_RECORD_BYTES ||
				*offset < index_offset + maps * BOTS_NAV_INDEX_ENTRY_BYTES)
			{
				if (why) *why = "an index entry that is outside the pack";
				return 0;
			}
			return 1;
		}
	}
	if (why) *why = "no record for this map";
	return 0;
}

int bots_nav_pack_check(unsigned char const *bytes, size_t count, char const **why)
{
	if (count < BOTS_NAV_PACK_HEADER_BYTES || memcmp(bytes, BOTS_NAV_MAGIC, 8) != 0)
	{
		if (why) *why = "not a navigation pack";
		return 0;
	}
	if (bots_nav_crc32(0, bytes + BOTS_NAV_PACK_HEADER_BYTES, count - BOTS_NAV_PACK_HEADER_BYTES) != nav_u32(bytes + 20))
	{
		if (why) *why = "the pack's crc does not match";
		return 0;
	}
	return 1;
}

unsigned char *bots_nav_write_pack(char const *name, unsigned long surfaces, unsigned long walkable, unsigned char const *record,
	size_t record_count, unsigned long bake_build, size_t *count)
{
	size_t index_end = BOTS_NAV_PACK_HEADER_BYTES + BOTS_NAV_INDEX_ENTRY_BYTES;
	size_t total = index_end + record_count;
	unsigned char *out = (unsigned char *)calloc(1, total);

	if (!out)
		return NULL;
	memcpy(out, BOTS_NAV_MAGIC, 8);
	nav_put_u16(out + 8, BOTS_NAV_PACK_VERSION);
	nav_put_u16(out + 10, BOTS_NAV_FORMAT);
	nav_put_u32(out + 12, 1);
	nav_put_u32(out + 16, BOTS_NAV_PACK_HEADER_BYTES);
	nav_put_u32(out + 24, bake_build);
	strncpy((char *)out + BOTS_NAV_PACK_HEADER_BYTES, name, BOTS_NAV_NAME_BYTES - 1);
	nav_put_u32(out + BOTS_NAV_PACK_HEADER_BYTES + 32, surfaces);
	nav_put_u32(out + BOTS_NAV_PACK_HEADER_BYTES + 36, walkable);
	nav_put_u32(out + BOTS_NAV_PACK_HEADER_BYTES + 40, (unsigned long)index_end);
	nav_put_u32(out + BOTS_NAV_PACK_HEADER_BYTES + 44, (unsigned long)record_count);
	memcpy(out + index_end, record, record_count);
	nav_put_u32(out + 20, bots_nav_crc32(0, out + BOTS_NAV_PACK_HEADER_BYTES, total - BOTS_NAV_PACK_HEADER_BYTES));
	*count = total;
	return out;
}

/* ---------- lookups */

static float nav_distance3(float const *a, float const *b)
{
	float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];

	return (float)sqrt(dx * dx + dy * dy + dz * dz);
}

long bots_nav_find_area(struct bots_nav const *nav, float const *point)
{
	long cx = (long)floor((point[0] - nav->bounds_min[0]) / BOTS_NAV_CELL);
	long cy = (long)floor((point[1] - nav->bounds_min[1]) / BOTS_NAV_CELL);
	unsigned long k, first, last;
	long best = -1;
	float best_score = 1.0e30f;

	if (cx < 0 || cy < 0 || cx >= nav->cells_x || cy >= nav->cells_y)
		return -1;
	first = nav->cell_first[cy * nav->cells_x + cx];
	last = nav->cell_first[cy * nav->cells_x + cx + 1];
	for (k = first; k < last; k++)
	{
		struct bots_nav_area const *a = &nav->areas[nav->cell_areas[k]];
		float dx = point[0] - a->x, dy = point[1] - a->y, dz = point[2] - a->z;
		float flat = (float)sqrt(dx * dx + dy * dy);
		float score;

		/* (inside the area's reach, and not a floor above or below: more than 1.2 units off in height is another floor) */
		if (flat > a->radius + 0.5f || fabs(dz) > 1.2f + 0.15f * flat)
			continue;
		score = flat + 4.0f * (float)fabs(dz);
		if (score < best_score)
		{
			best_score = score;
			best = (long)nav->cell_areas[k];
		}
	}
	return best;
}

long bots_nav_nearest_area(struct bots_nav const *nav, float const *point)
{
	long best = bots_nav_find_area(nav, point), index;
	float best_score = 1.0e30f;

	if (best >= 0)
		return best;
	for (index = 0; index < nav->area_count; index++)
	{
		struct bots_nav_area const *a = &nav->areas[index];
		float dx = point[0] - a->x, dy = point[1] - a->y, dz = point[2] - a->z;
		float score = (float)sqrt(dx * dx + dy * dy) + 4.0f * (float)fabs(dz);

		if (score < best_score)
		{
			best_score = score;
			best = index;
		}
	}
	return best;
}

long bots_nav_next_place(struct bots_nav const *nav, long after, int type)
{
	long index;

	for (index = after + 1; index < nav->place_count; index++)
		if (nav->places[index].type == type)
			return index;
	return -1;
}

/* ---------- the search */

int bots_nav_search_new(struct bots_nav_search *search, struct bots_nav const *nav)
{
	memset(search, 0, sizeof(*search));
	search->capacity_areas = nav->area_count;
	search->capacity_heap = nav->link_count + nav->area_count + 8;
	search->cost = (float *)malloc((size_t)search->capacity_areas * sizeof(float));
	search->parent_link = (unsigned short *)malloc((size_t)search->capacity_areas * sizeof(unsigned short));
	search->parent_area = (unsigned short *)malloc((size_t)search->capacity_areas * sizeof(unsigned short));
	search->state = (unsigned char *)malloc((size_t)search->capacity_areas);
	search->heap = (unsigned short *)malloc((size_t)search->capacity_heap * sizeof(unsigned short));
	search->heap_key = (float *)malloc((size_t)search->capacity_heap * sizeof(float));
	if (!search->cost || !search->parent_link || !search->parent_area || !search->state || !search->heap || !search->heap_key)
	{
		bots_nav_search_dispose(search);
		return 0;
	}
	return 1;
}

void bots_nav_search_dispose(struct bots_nav_search *search)
{
	free(search->cost);
	free(search->parent_link);
	free(search->parent_area);
	free(search->state);
	free(search->heap);
	free(search->heap_key);
	memset(search, 0, sizeof(*search));
}

static void heap_push(struct bots_nav_search *s, unsigned short area, float key)
{
	long i = s->heap_count++;

	if (i >= s->capacity_heap)
	{
		s->heap_count--;
		return;
	}
	while (i > 0)
	{
		long parent = (i - 1) / 2;

		if (s->heap_key[parent] <= key)
			break;
		s->heap[i] = s->heap[parent];
		s->heap_key[i] = s->heap_key[parent];
		i = parent;
	}
	s->heap[i] = area;
	s->heap_key[i] = key;
}

static unsigned short heap_pop(struct bots_nav_search *s)
{
	unsigned short top = s->heap[0];
	unsigned short area = s->heap[s->heap_count - 1];
	float key = s->heap_key[s->heap_count - 1];
	long i = 0;

	s->heap_count--;
	if (s->heap_count > 0)
	{
		for (;;)
		{
			long child = 2 * i + 1;

			if (child >= s->heap_count)
				break;
			if (child + 1 < s->heap_count && s->heap_key[child + 1] < s->heap_key[child])
				child++;
			if (s->heap_key[child] >= key)
				break;
			s->heap[i] = s->heap[child];
			s->heap_key[i] = s->heap_key[child];
			i = child;
		}
		s->heap[i] = area;
		s->heap_key[i] = key;
	}
	return top;
}

static float nav_jitter(unsigned long seed, unsigned long link)
{
	unsigned long h = seed * 2654435761UL + link * 40503UL + 12345UL;

	h ^= h >> 13;
	h *= 0x5BD1E995UL;
	h ^= h >> 15;
	return 0.25f * (float)(h & 0xFFFF) / 65535.0f;
}

enum { NAV_UNSEEN = 0, NAV_OPEN, NAV_CLOSED };

/* the search proper: costs from `from`, to `to`; returns the area it stopped at (the goal, or the nearest it found when the goal cannot be
reached), -1 for none */
static long nav_run(struct bots_nav const *nav, struct bots_nav_search *s, long from, long to, unsigned short const *bad, long bad_count,
	unsigned long jitter_seed, float const *goal)
{
	float speed = nav->run_speed > 0.0f ? nav->run_speed : 2.0f;
	long index, best_area = from;
	float best_h;
	float const *target = goal;

	memset(s->state, NAV_UNSEEN, (size_t)s->capacity_areas);
	s->heap_count = 0;
	s->expanded = 0;
	s->cost[from] = 0.0f;
	s->state[from] = NAV_OPEN;
	s->parent_link[from] = 0xFFFF;
	s->parent_area[from] = 0xFFFF;
	{
		float p[3] = { nav->areas[to].x, nav->areas[to].y, nav->areas[to].z };

		if (!target)
			target = p;
		best_h = nav_distance3(&nav->areas[from].x, target);
		heap_push(s, (unsigned short)from, best_h / speed);
		while (s->heap_count > 0)
		{
			unsigned short area = heap_pop(s);
			struct bots_nav_area const *a = &nav->areas[area];

			if (s->state[area] == NAV_CLOSED)
				continue;
			s->state[area] = NAV_CLOSED;
			s->expanded++;
			if (area == to)
				return to;
			{
				float h = nav_distance3(&a->x, target);

				if (h < best_h)
				{
					best_h = h;
					best_area = area;
				}
			}
			for (index = 0; index < a->link_count; index++)
			{
				long link_index = a->first_link + index;
				struct bots_nav_link const *l = &nav->links[link_index];
				float cost;
				long k;
				int skip = 0;

				if (s->state[l->to] == NAV_CLOSED)
					continue;
				for (k = 0; k < bad_count; k++)
					if (bad[k] == link_index)
						skip = 1;
				if (skip)
					continue;
				cost = (float)l->cost_q / 16.0f;
				if (jitter_seed)
					cost *= 1.0f + nav_jitter(jitter_seed, (unsigned long)link_index);
				if (l->flags & _bots_nav_link_flag_risky)
					cost += 1.0f;
				cost += s->cost[area];
				if (s->state[l->to] == NAV_UNSEEN || cost < s->cost[l->to])
				{
					s->cost[l->to] = cost;
					s->parent_link[l->to] = (unsigned short)link_index;
					s->parent_area[l->to] = area;
					s->state[l->to] = NAV_OPEN;
					heap_push(s, l->to, cost + nav_distance3(&nav->areas[l->to].x, target) / speed);
				}
			}
		}
	}
	return best_area;
}

int bots_nav_search_path(struct bots_nav const *nav, struct bots_nav_search *search, long from, long to, float const *start,
	float const *goal, unsigned short const *bad, long bad_count, unsigned long jitter_seed, struct bots_nav_path *path)
{
	long end, count = 0, area;
	unsigned short chain[BOTS_NAV_PATH_LINKS * 2], chain_from[BOTS_NAV_PATH_LINKS * 2];
	long chain_count = 0;

	memset(path, 0, sizeof(*path));
	if (from < 0 || to < 0 || from >= nav->area_count || to >= nav->area_count || search->capacity_areas < nav->area_count)
		return 0;
	end = nav_run(nav, search, from, to, bad, bad_count, jitter_seed, goal);
	if (end < 0)
		return 0;
	path->start_area = (short)from;
	path->goal_area = (short)end;
	path->complete = end == to;
	memcpy(path->start, start, sizeof(path->start));
	memcpy(path->goal, goal, sizeof(path->goal));
	/* (the links back from the end, then the first BOTS_NAV_PATH_LINKS of them forward) */
	for (area = end; area != from && chain_count < (long)(sizeof(chain) / sizeof(chain[0])); area = search->parent_area[area])
	{
		chain_from[chain_count] = search->parent_area[area];
		chain[chain_count++] = search->parent_link[area];
	}
	if (area != from)
		return 0;
	count = chain_count > BOTS_NAV_PATH_LINKS ? BOTS_NAV_PATH_LINKS : chain_count;
	{
		long k;

		for (k = 0; k < count; k++)
		{
			path->links[k] = chain[chain_count - 1 - k];
			path->from[k] = chain_from[chain_count - 1 - k];
		}
	}
	path->link_count = (short)count;
	if (chain_count > BOTS_NAV_PATH_LINKS)
	{
		/* (cut short: the goal in the last link's area is not the path's) */
		path->complete = 0;
		path->goal_area = (short)nav->links[path->links[count - 1]].to;
	}
	path->step = 0;
	path->valid = 1;
	return 1;
}

float bots_nav_travel_time(struct bots_nav const *nav, struct bots_nav_search *search, long from, long to)
{
	long end;

	if (from < 0 || to < 0 || from >= nav->area_count || to >= nav->area_count)
		return -1.0f;
	if (from == to)
		return 0.0f;
	end = nav_run(nav, search, from, to, NULL, 0, 0, NULL);
	if (end != to)
		return -1.0f;
	return search->cost[to];
}

/* ---------- a path's points */

long bots_nav_path_point_count(struct bots_nav_path const *path)
{
	return path->valid ? 3L * path->link_count + 2 : 0;
}

int bots_nav_path_point(struct bots_nav const *nav, struct bots_nav_path const *path, long number, struct bots_nav_point *point)
{
	long total = bots_nav_path_point_count(path), link, phase;
	struct bots_nav_link const *l;

	if (number < 0 || number >= total)
		return 0;
	point->kind = _bots_nav_link_walk;
	point->link_index = -1;
	if (number >= 3L * path->link_count)
	{
		if (number == 3L * path->link_count)
		{
			struct bots_nav_area const *a = &nav->areas[path->goal_area];

			point->p[0] = a->x;
			point->p[1] = a->y;
			point->p[2] = a->z;
		}
		else
		{
			memcpy(point->p, path->goal, sizeof(point->p));
		}
		return 1;
	}
	link = number / 3;
	phase = number % 3;
	l = &nav->links[path->links[link]];
	if (phase == 0)
	{
		struct bots_nav_area const *a = &nav->areas[path->from[link]];

		point->p[0] = a->x;
		point->p[1] = a->y;
		point->p[2] = a->z;
		point->link_index = path->links[link];
	}
	else if (phase == 1)
	{
		memcpy(point->p, l->exit, sizeof(point->p));
		point->link_index = path->links[link];
	}
	else
	{
		memcpy(point->p, l->entry, sizeof(point->p));
		point->kind = l->type;
		point->link_index = path->links[link];
	}
	return 1;
}
