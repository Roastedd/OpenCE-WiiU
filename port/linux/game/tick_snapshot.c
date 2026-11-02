/*
TICK_SNAPSHOT.C

The render view snapshot, stage 3 of the plan to run the game's ticks on a thread of their own (the tools repository's
reports/research/2026-11-01/ticks-off-thread.md, option C-prime). For the Wii U only (HALO_WIIU).

The renderer reads the game state the ticks change, and writes some of it. To draw a frame while the next tick runs, it has to read
copies the tick does not touch. After each tick this takes one, of what the renderer reads of the objects:

- the object header array (the objects' identifiers, flags, types and sizes), whole;
- the object blocks of the objects drawn lately (about 190 in a fight; 3 captures is "lately"), each as the header gives it: object
  data, node matrices and every other block of it, which are reached by offset from the object's own address;
- the objects' function inputs (objects.c), which the pictures' function values come from.

While a frame is drawn (tick_snapshot_view_begin to tick_snapshot_view_end), the game's accessors (object_header_get,
object_header_try_and_get and the object iterator, objects.h and objects.c) give these copies instead of the game state's: so every
reader that goes through object_get, object_header_block_get and the rest reads the copy, and a write lands in the copy (the object
marker, which the tick also uses, is then the renderer's alone). Three kinds of read cannot be served ahead of time and are made
when asked, as the ticks have finished (this stage is synchronous): an object the renderer asks for that was not drawn lately (its
copy is made then, counted as a "miss"), objects made or deleted since the last capture (the view begins with a new capture), and an
index the snapshot does not have (the game state's is given, counted as a "fallback"; it should be none).

What the renderer writes into an object stays: render-owned fields (cached_render_state_index, which names the object's lit state in the
render's cache) are kept in a side table by object and put back into each new copy, so that a frame draws what it drew when it read
the game state itself. The list is RENDER_FIELDS; verify mode (below) finds the rest.

The cameras: the observer's result, the director's perspective and inhibited facing, and the player control's unit and facing are
read once for each local player in a frame and kept for the frame; the accessors give the copies (director.c, player_control.c).
Nothing here runs the observer: that is still done each frame before the view.

The switch is halo/tick_snapshot.txt on the card (a number, read once with the C library's fopen), or the environment variable
HALO_TICK_SNAPSHOT on the host. Bits:
  1  on. Without it nothing here does anything and the game reads its own state.
  2  every object is copied at each capture, not only those drawn lately (the most it can cost).
  4  test: every copy's bounding sphere is moved far away. The frames must change: it shows that the renderer reads the copies.
  8  verify: each frame's view compares the copies with the game state at its start (what changed since the capture: a write of
     game_frame or of the per-frame code) and at its end (what the renderer wrote), and the log says which fields; the first
     is what stops a frame drawn from the snapshot from being the frame drawn from the game state.
 16  test: the cameras' positions are moved far away.

The log (every 150 captures, about 5 s) says the copies' size and number per capture and the reads the snapshot served;
port/wiiu/src/wiiu_tick_snapshot_timers.c, on the console, the milliseconds a frame (the timers here are weak references to it).
*/

#ifdef HALO_WIIU

#include "cseries.h"
#include "objects/objects.h"
#include "camera/director.h"
#include "camera/observer.h"
#include "game/game.h"
#include "game/players.h"
#include "math/real_math.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* (the card's file is read with the C library's fopen: the game's stdio.h takes the Xbox's paths) */
#undef fopen

void platform_log(char const *format, ...);

/* port/wiiu/src/wiiu_tick_snapshot_timers.c, on the console: weak, so that a build without it (the host's) has no timers */
unsigned long long wiiu_snapshot_clock(void) __attribute__((weak));
void wiiu_snapshot_add(int part, unsigned long long since) __attribute__((weak));

/* the parts timed (wiiu_tick_snapshot_timers.c names them) */
enum
{
	PART_HEADERS = 0,
	PART_BLOCKS,
	PART_CAPTURE,
	PART_ROTATIONS, /* (render_interpolation.c) */
	PART_VIEW_BEGIN,
	PART_VIEW_END,
	PART_FRAME, /* a view: counts the frames */
	PART_COUNT
};

#define CLOCK() (wiiu_snapshot_clock ? wiiu_snapshot_clock() : 0ULL)
#define ADD(part, since) do { if (wiiu_snapshot_add) wiiu_snapshot_add((part), (since)); } while (0)

#define SNAPSHOT_PATH "/vol/external01/wiiu/apps/halo/tick_snapshot.txt"

enum
{
	MODE_ON = 1,
	MODE_ALL = 2,
	MODE_POISON_OBJECTS = 4,
	MODE_VERIFY = 8,
	MODE_POISON_CAMERAS = 16,
};

/* an object copied at a capture if it was asked for within this many captures */
#define DRAWN_CAPTURES 3
#define LOG_CAPTURES 150
/* the verify mode's table: object types, and words of a block */
#define VERIFY_TYPES 16
#define VERIFY_WORDS 4096

/* the fields of an object the renderer writes, kept apart from the copies (kept by object; the bytes put into each new copy) */
static const struct
{
	unsigned short offset;
	unsigned short size;
} RENDER_FIELDS[] =
{
	{ (unsigned short)offsetof(struct object_datum, object.cached_render_state_index), sizeof(long) },
};
#define RENDER_FIELD_COUNT ((int)(sizeof(RENDER_FIELDS) / sizeof(RENDER_FIELDS[0])))
#define RENDER_FIELD_BYTES 16

struct camera_view
{
	boolean valid;
	boolean has_result;
	struct observer_result result;
	int perspective;
	int inhibited_facing;
	long unit_index;
	real_vector3d facing;
};

static struct
{
	int mode; /* -1 not read yet */
	boolean valid; /* a capture of this map's objects is held */
	boolean dirty; /* objects made or deleted since */
	boolean capturing_cameras;
	long serial; /* the captures made, counted from 1 */
	struct data_array array; /* the object header array as of the capture, its data the copies below */
	struct object_header_datum headers[MAXIMUM_OBJECTS_PER_MAP];
	byte *blocks[MAXIMUM_OBJECTS_PER_MAP]; /* by absolute index: the copy of an object's block (the buffer is kept) */
	unsigned short capacity[MAXIMUM_OBJECTS_PER_MAP];
	boolean copied[MAXIMUM_OBJECTS_PER_MAP]; /* this capture has a copy for the slot */
	long wanted[MAXIMUM_OBJECTS_PER_MAP]; /* the capture an object was last asked for in */
	/* the render-owned fields of each object (by absolute index) with the identifier they are of */
	short field_identifier[MAXIMUM_OBJECTS_PER_MAP];
	byte fields[MAXIMUM_OBJECTS_PER_MAP][RENDER_FIELD_BYTES];
	struct camera_view cameras[MAXIMUM_LOCAL_PLAYERS];
	/* counts, for the log */
	unsigned long captures, window_captures;
	unsigned long objects_copied, bytes_copied, header_bytes, block_calls;
	unsigned long misses, catch_ups, fallbacks, reads, views, camera_copies;
	/* verify */
	unsigned long *verify_begin, *verify_end; /* [type][word]: blocks that differ from the game state's at the view's start and end */
	unsigned long verify_objects_begin, verify_objects_end, verify_views;
	unsigned long verify_size_differences;
} s = { -1 };

struct data_array *tick_snapshot_headers;

/* ---------- the switch */

static int tick_snapshot_mode(void)
{
	if (s.mode < 0)
	{
		char const *environment = getenv("HALO_TICK_SNAPSHOT");
		long value = 0;

		if (environment && *environment)
		{
			value = strtol(environment, NULL, 0);
		}
		else
		{
			FILE *file = fopen(SNAPSHOT_PATH, "r");

			if (file)
			{
				if (fscanf(file, "%li", &value) != 1)
					value = 0;
				fclose(file);
			}
		}
		s.mode = (int)value;
		if (s.mode)
			platform_log("tick snapshot: on, mode %d (halo/tick_snapshot.txt)", s.mode);
	}
	return s.mode;
}

int tick_snapshot_viewing(void)
{
	return tick_snapshot_headers != NULL;
}

/* ---------- the copies */

static void tick_snapshot_log(void)
{
	const unsigned long captures = s.window_captures ? s.window_captures : 1;

	platform_log("tick snapshot (%lu captures, %lu frames): per capture %lu objects, %lu bytes in %lu copies, headers %lu bytes; "
		"in the window: %lu misses, %lu catch-ups, %lu fallbacks, %lu reads, %lu camera copies",
		s.window_captures, s.views, s.objects_copied / captures, (s.bytes_copied + s.header_bytes) / captures, s.block_calls / captures,
		s.header_bytes / captures, s.misses, s.catch_ups, s.fallbacks, s.reads, s.camera_copies);
	s.window_captures = 0;
	s.objects_copied = s.bytes_copied = s.header_bytes = s.block_calls = 0;
	s.misses = s.catch_ups = s.fallbacks = s.reads = s.views = s.camera_copies = 0;
}

/* an object's block copied from the game state's into the snapshot's (a capture, or an asking for one not copied) */
static void tick_snapshot_copy_block(long absolute_index, struct object_header_datum const *live)
{
	struct object_header_datum *header = &s.headers[absolute_index];
	long size = live->data_size;
	long field;
	byte *block;

	if (size <= 0 || !live->datum)
		return;
	if (s.capacity[absolute_index] < size)
	{
		free(s.blocks[absolute_index]);
		s.blocks[absolute_index] = (byte *)malloc((size_t)size);
		s.capacity[absolute_index] = s.blocks[absolute_index] ? (unsigned short)size : 0;
		if (!s.blocks[absolute_index])
			return;
	}
	block = s.blocks[absolute_index];
	memcpy(block, live->datum, (size_t)size);
	/* what the renderer wrote in the last copy of this object */
	if (s.field_identifier[absolute_index] == live->identifier)
	{
		byte const *kept = s.fields[absolute_index];

		for (field = 0; field < RENDER_FIELD_COUNT; field++)
		{
			if (RENDER_FIELDS[field].offset + RENDER_FIELDS[field].size <= size)
				memcpy(block + RENDER_FIELDS[field].offset, kept, RENDER_FIELDS[field].size);
			kept += RENDER_FIELDS[field].size;
		}
	}
	if (s.mode & MODE_POISON_OBJECTS)
		((struct object_datum *)block)->object.bounding_sphere_center.x += 5000.0f;
	header->datum = (struct object_datum *)block;
	s.copied[absolute_index] = TRUE;
	object_function_inputs_copy_to_snapshot(absolute_index);
	s.objects_copied++;
	s.bytes_copied += (unsigned long)size;
	s.block_calls++;
}

void tick_snapshot_capture(void)
{
	struct data_array const *live = object_header_data;
	struct object_header_datum const *live_headers;
	long count, previous_count, index;
	unsigned long long started, headers_started, blocks_ticks = 0;

	if (!tick_snapshot_mode() || !live || !live->valid)
		return;
	started = CLOCK();
	live_headers = (struct object_header_datum const *)live->data;
	count = live->count;
	previous_count = s.array.count;
	s.serial++;
	headers_started = CLOCK();
	/* the headers, whole */
	if (count > MAXIMUM_OBJECTS_PER_MAP)
		count = MAXIMUM_OBJECTS_PER_MAP;
	memcpy(s.headers, live_headers, (size_t)count * sizeof(s.headers[0]));
	for (index = count; index < previous_count && index < MAXIMUM_OBJECTS_PER_MAP; index++)
		s.headers[index].identifier = 0;
	s.array = *live;
	s.array.data = s.headers;
	s.header_bytes += (unsigned long)count * sizeof(s.headers[0]);
	ADD(PART_HEADERS, headers_started);
	headers_started = CLOCK();
	/* the blocks of the objects drawn lately */
	for (index = 0; index < count; index++)
	{
		s.copied[index] = FALSE;
		s.headers[index].datum = NULL;
		if (!s.headers[index].identifier)
			continue;
		if ((s.mode & MODE_ALL) || (s.wanted[index] && s.serial - s.wanted[index] <= DRAWN_CAPTURES))
			tick_snapshot_copy_block(index, &live_headers[index]);
	}
	for (; index < previous_count && index < MAXIMUM_OBJECTS_PER_MAP; index++)
		s.copied[index] = FALSE;
	(void)blocks_ticks;
	ADD(PART_BLOCKS, headers_started);
	s.valid = TRUE;
	s.dirty = FALSE;
	s.captures++;
	s.window_captures++;
	ADD(PART_CAPTURE, started);
	if (s.window_captures >= LOG_CAPTURES)
		tick_snapshot_log();
}

void tick_snapshot_reset(void)
{
	long index;

	s.valid = FALSE;
	s.dirty = FALSE;
	memset(&s.array, 0, sizeof(s.array));
	for (index = 0; index < MAXIMUM_OBJECTS_PER_MAP; index++)
	{
		s.headers[index].identifier = 0;
		s.headers[index].datum = NULL;
		s.copied[index] = FALSE;
		s.wanted[index] = 0;
		s.field_identifier[index] = 0;
	}
	memset(s.cameras, 0, sizeof(s.cameras));
	tick_snapshot_headers = NULL;
}

void tick_snapshot_objects_changed(void)
{
	s.dirty = TRUE;
}

/* ---------- the accessors */

/* an object not copied at the capture: copied now, from the game state (the ticks have finished). The game state's header if the
snapshot has none to give or the object is another than it holds */
static struct object_header_datum *tick_snapshot_fill(long object_index, struct object_header_datum *header)
{
	const long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	struct object_header_datum *live = (struct object_header_datum *)((byte *)object_header_data->data +
		object_header_data->size * absolute_index);

	if (live->identifier != header->identifier || !live->datum)
	{
		s.fallbacks++;
		return live;
	}
	s.misses++;
	tick_snapshot_copy_block(absolute_index, live);
	if (!header->datum)
	{
		s.fallbacks++;
		return live;
	}
	return header;
}

struct object_header_datum *tick_snapshot_object_header_get(long object_index)
{
	struct object_header_datum *header = (struct object_header_datum *)datum_try_and_get(&s.array, object_index);

	s.reads++;
	if (!header)
	{
		/* (an object the snapshot has not: made since it, and the view's start would have caught it) */
		s.fallbacks++;
		return (struct object_header_datum *)datum_get(object_header_data, object_index);
	}
	s.wanted[DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index)] = s.serial;
	if (!header->datum)
		return tick_snapshot_fill(object_index, header);
	return header;
}

struct object_header_datum *tick_snapshot_object_header_try_and_get(long object_index)
{
	struct object_header_datum *header = (struct object_header_datum *)datum_try_and_get(&s.array, object_index);

	s.reads++;
	if (!header)
	{
		struct object_header_datum *live = (struct object_header_datum *)datum_try_and_get(object_header_data, object_index);

		if (live)
			s.fallbacks++;
		return live;
	}
	s.wanted[DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index)] = s.serial;
	if (!header->datum)
		return tick_snapshot_fill(object_index, header);
	return header;
}

/* ---------- the cameras */

/* a local player's camera as of this frame's start, kept for the frame */
static struct camera_view *tick_snapshot_camera_view(short local_player_index)
{
	struct camera_view *view;

	if (!tick_snapshot_headers || s.capturing_cameras || local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS)
		return NULL;
	view = &s.cameras[local_player_index];
	if (!view->valid)
	{
		struct observer_result const *observer;

		/* (the accessors give the game state's while these are read) */
		s.capturing_cameras = TRUE;
		observer = observer_get_camera(local_player_index);
		view->has_result = observer != NULL;
		if (observer)
			view->result = *observer;
		view->perspective = (int)director_get_perspective(local_player_index);
		view->inhibited_facing = director_inhibited_facing(local_player_index);
		view->unit_index = player_control_get_unit_index(local_player_index);
		player_control_get_facing_direction(local_player_index, &view->facing);
		s.capturing_cameras = FALSE;
		if ((s.mode & MODE_POISON_CAMERAS) && view->has_result)
			view->result.position.z += 100.0f;
		view->valid = TRUE;
		s.camera_copies++;
	}
	return view;
}

struct observer_result const *tick_snapshot_camera(short local_player_index, struct observer_result const *live)
{
	struct camera_view *view = tick_snapshot_camera_view(local_player_index);

	if (!view || !view->has_result)
		return live;
	return &view->result;
}

int tick_snapshot_director_perspective(short local_player_index, int *perspective)
{
	struct camera_view *view = tick_snapshot_camera_view(local_player_index);

	if (!view)
		return 0;
	*perspective = view->perspective;
	return 1;
}

int tick_snapshot_director_inhibited_facing(short local_player_index, int *inhibited_facing)
{
	struct camera_view *view = tick_snapshot_camera_view(local_player_index);

	if (!view)
		return 0;
	*inhibited_facing = view->inhibited_facing;
	return 1;
}

int tick_snapshot_player_unit(short local_player_index, long *unit_index)
{
	struct camera_view *view = tick_snapshot_camera_view(local_player_index);

	if (!view)
		return 0;
	*unit_index = view->unit_index;
	return 1;
}

int tick_snapshot_player_facing(short local_player_index, real_vector3d *facing)
{
	struct camera_view *view = tick_snapshot_camera_view(local_player_index);

	if (!view)
		return 0;
	*facing = view->facing;
	return 1;
}

/* ---------- the view */

/* the verify mode: where the copies differ from the game state's, by object type and word of the block */
static void tick_snapshot_verify(boolean at_end)
{
	long index;
	unsigned long *histogram = at_end ? s.verify_end : s.verify_begin;
	unsigned long objects = 0;

	if (!histogram)
		return;
	for (index = 0; index < s.array.count && index < MAXIMUM_OBJECTS_PER_MAP; index++)
	{
		struct object_header_datum const *live = (struct object_header_datum const *)((byte const *)object_header_data->data +
			object_header_data->size * index);
		long word, words;
		boolean different = FALSE;

		if (!s.copied[index] || live->identifier != s.headers[index].identifier || !live->datum)
			continue;
		if (live->data_size != s.headers[index].data_size)
		{
			s.verify_size_differences++;
			continue;
		}
		words = live->data_size / 4;
		for (word = 0; word < words; word++)
		{
			if (((unsigned long const *)live->datum)[word] != ((unsigned long const *)s.blocks[index])[word])
			{
				if (word < VERIFY_WORDS)
					histogram[(live->type & (VERIFY_TYPES - 1)) * VERIFY_WORDS + word]++;
				different = TRUE;
			}
		}
		objects += different;
	}
	if (at_end)
		s.verify_objects_end += objects;
	else
		s.verify_objects_begin += objects;
}

/* the name of a word of an object's block, as far as the common part goes */
static void tick_snapshot_field_name(long word, char *name, size_t size)
{
	static const struct
	{
		unsigned short offset;
		char const *name;
	} fields[] =
	{
		{ offsetof(struct object_datum, definition_index), "definition_index" },
		{ offsetof(struct object_datum, object.flags), "flags" },
		{ offsetof(struct object_datum, object.magic_number), "magic_number" },
		{ offsetof(struct object_datum, object.position), "position" },
		{ offsetof(struct object_datum, object.translational_velocity), "translational_velocity" },
		{ offsetof(struct object_datum, object.forward), "forward" },
		{ offsetof(struct object_datum, object.up), "up" },
		{ offsetof(struct object_datum, object.angular_velocity), "angular_velocity" },
		{ offsetof(struct object_datum, object.location), "location" },
		{ offsetof(struct object_datum, object.bounding_sphere_center), "bounding_sphere_center" },
		{ offsetof(struct object_datum, object.bounding_sphere_radius), "bounding_sphere_radius" },
		{ offsetof(struct object_datum, object.scale), "scale" },
		{ offsetof(struct object_datum, object.type), "type" },
		{ offsetof(struct object_datum, object.render_flags), "render_flags" },
		{ offsetof(struct object_datum, object.owner_team_index), "owner_team_index" },
		{ offsetof(struct object_datum, object.idle_ticks), "idle_ticks" },
		{ offsetof(struct object_datum, object.variant_number), "variant_number" },
		{ offsetof(struct object_datum, object.animation), "animation" },
		{ offsetof(struct object_datum, object.body_vitality), "body_vitality" },
		{ offsetof(struct object_datum, object.shield_vitality), "shield_vitality" },
		{ offsetof(struct object_datum, object.current_shield_damage), "current_shield_damage" },
		{ offsetof(struct object_datum, object.recent_shield_damage), "recent_shield_damage" },
		{ offsetof(struct object_datum, object.shield_stun_ticks), "shield_stun_ticks" },
		{ offsetof(struct object_datum, object.damage_flags), "damage_flags" },
		{ offsetof(struct object_datum, object.first_cluster_reference_index), "first_cluster_reference_index" },
		{ offsetof(struct object_datum, object.next_object_index), "next_object_index" },
		{ offsetof(struct object_datum, object.first_child_object_index), "first_child_object_index" },
		{ offsetof(struct object_datum, object.parent_object_index), "parent_object_index" },
		{ offsetof(struct object_datum, object.force_shield_update), "force_shield_update" },
		{ offsetof(struct object_datum, object.incoming_function_values), "incoming_function_values" },
		{ offsetof(struct object_datum, object.outgoing_function_values), "outgoing_function_values" },
		{ offsetof(struct object_datum, object.attachment_types), "attachment_types" },
		{ offsetof(struct object_datum, object.attachment_indices), "attachment_indices" },
		{ offsetof(struct object_datum, object.first_widget_index), "first_widget_index" },
		{ offsetof(struct object_datum, object.cached_render_state_index), "cached_render_state_index" },
		{ offsetof(struct object_datum, object.regions_destroyed_flags), "regions_destroyed_flags" },
		{ offsetof(struct object_datum, object.region_damage), "region_damage" },
		{ offsetof(struct object_datum, object.region_permutations), "region_permutations" },
		{ offsetof(struct object_datum, object.base_change_colors), "base_change_colors" },
		{ offsetof(struct object_datum, object.outgoing_change_colors), "outgoing_change_colors" },
		{ offsetof(struct object_datum, object.original_node_orientations), "original_node_orientations" },
		{ offsetof(struct object_datum, object.node_orientations), "node_orientations" },
		{ offsetof(struct object_datum, object.node_matrices), "node_matrices" },
		{ sizeof(struct object_datum), "(the type's own data)" },
	};
	const unsigned offset = (unsigned)word * 4;
	long field, best = -1;

	for (field = 0; field < (long)(sizeof(fields) / sizeof(fields[0])); field++)
	{
		if (fields[field].offset <= offset && (best < 0 || fields[field].offset >= fields[best].offset))
			best = field;
	}
	snprintf(name, size, "%s+%u", best >= 0 ? fields[best].name : "?", best >= 0 ? offset - fields[best].offset : offset);
}

static void tick_snapshot_verify_log(void)
{
	static const char *const types[] = { "biped", "vehicle", "weapon", "equipment", "garbage", "projectile", "scenery", "machine",
		"control", "light_fixture", "placeholder", "sound_scenery" };
	long type, word, lines = 0;

	platform_log("tick snapshot verify (%lu views): differing objects a view at its start %lu (changed since the capture), at its end %lu "
		"(written by the frame); blocks of another size %lu",
		s.verify_views, s.verify_objects_begin / (s.verify_views ? s.verify_views : 1),
		s.verify_objects_end / (s.verify_views ? s.verify_views : 1), s.verify_size_differences);
	for (type = 0; type < VERIFY_TYPES; type++)
	{
		for (word = 0; word < VERIFY_WORDS && lines < 40; word++)
		{
			const unsigned long begin = s.verify_begin[type * VERIFY_WORDS + word], end = s.verify_end[type * VERIFY_WORDS + word];
			char name[64];

			if (!begin && !end)
				continue;
			tick_snapshot_field_name(word, name, sizeof(name));
			platform_log("tick snapshot verify: %s +%ld (%s): at the start %lu, at the end %lu", type < 12 ? types[type] : "?", word * 4L,
				name, begin, end);
			lines++;
		}
	}
}

void tick_snapshot_view_begin(void)
{
	unsigned long long started;

	if (!tick_snapshot_mode() || !object_header_data || !object_header_data->valid)
		return;
	started = CLOCK();
	if (!s.valid || s.dirty)
	{
		/* (objects made or deleted since the last capture, or no tick yet in this map: the ticks have finished, so the copy is of now) */
		tick_snapshot_capture();
		s.catch_ups++;
	}
	if (!s.valid)
		return;
	memset(s.cameras, 0, sizeof(s.cameras));
	if ((s.mode & MODE_VERIFY) && !s.verify_begin)
	{
		s.verify_begin = (unsigned long *)calloc((size_t)VERIFY_TYPES * VERIFY_WORDS, sizeof(unsigned long));
		s.verify_end = (unsigned long *)calloc((size_t)VERIFY_TYPES * VERIFY_WORDS, sizeof(unsigned long));
	}
	if (s.verify_begin)
		tick_snapshot_verify(FALSE);
	tick_snapshot_headers = &s.array;
	s.views++;
	ADD(PART_VIEW_BEGIN, started);
}

void tick_snapshot_view_end(void)
{
	unsigned long long started;
	long index, field;

	if (!tick_snapshot_headers)
		return;
	started = CLOCK();
	if (s.verify_end)
	{
		tick_snapshot_verify(TRUE);
		if (++s.verify_views % 300 == 0)
			tick_snapshot_verify_log();
	}
	/* what the frame wrote in the render-owned fields of the copies, kept for the next copy of the object */
	for (index = 0; index < s.array.count && index < MAXIMUM_OBJECTS_PER_MAP; index++)
	{
		byte *kept;

		if (!s.copied[index] || !s.headers[index].identifier)
			continue;
		kept = s.fields[index];
		for (field = 0; field < RENDER_FIELD_COUNT; field++)
		{
			if (RENDER_FIELDS[field].offset + RENDER_FIELDS[field].size <= s.headers[index].data_size)
				memcpy(kept, s.blocks[index] + RENDER_FIELDS[field].offset, RENDER_FIELDS[field].size);
			kept += RENDER_FIELDS[field].size;
		}
		s.field_identifier[index] = s.headers[index].identifier;
	}
	tick_snapshot_headers = NULL;
	ADD(PART_VIEW_END, started);
	ADD(PART_FRAME, 0);
}

#else /* !HALO_WIIU */

typedef int tick_snapshot_is_for_the_wii_u_only;

#endif
