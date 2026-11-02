/*
TICK_SNAPSHOT.C

The render view snapshot, stage 3 of the plan to run the game's ticks on a thread of their own (the tools repository's
reports/research/2026-11-01/ticks-off-thread.md, option C-prime). For the Wii U only (HALO_WIIU).

The renderer reads the game state the ticks change, and writes some of it. To draw a frame while the next tick runs, it has to read
copies the tick does not touch. After each tick this takes one, of what the renderer reads of the objects:

- the object header array (the objects' identifiers, flags, types and sizes), whole;
- the object blocks of the objects drawn lately (about 190 in a fight; 3 captures is "lately"), each as the header gives it: object
  data, node matrices and every other block of it, which are reached by offset from the object's own address;
- the objects' function inputs (objects.c), which the pictures' function values come from;
- the object cluster partitions' lists (which objects are in which clusters: how a frame finds the objects to draw), whole.

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

Stage 4 (the overlap, tick_overlap.c) keeps a ring of SNAPSHOT_SLOTS captures, each tagged with its tick, and a frame is drawn from the slot of
one tick, blended with the one before it; the tick thread writes the next slots meanwhile (see "the ring" below). In that mode every object is
copied at each capture (the lazy copy of an object not drawn lately needs the live state, which a tick may be changing), and the cameras are taken
by the tick thread at the end of the job that ran the tick.

The log (every 150 captures, about 5 s) says the copies' size and number per capture and the reads the snapshot served;
port/wiiu/src/wiiu_tick_snapshot_timers.c, on the console, the milliseconds a frame (it installs the clock the timers here use).
*/

#ifdef HALO_WIIU

#include "cseries.h"
#include "objects/objects.h"
#include "camera/director.h"
#include "camera/observer.h"
#include "game/game.h"
#include "game/players.h"
#include "math/real_math.h"
#include "objects/reference_lists.h"
#include "structures/cluster_partitions.h"
#include "structures/structures.h"

#include "tick_owners.h"
#include "tick_overlap.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* (the card's file is read with the C library's fopen: the game's stdio.h takes the Xbox's paths) */
#undef fopen

void platform_log(char const *format, ...);

/* The timers are the console's (port/wiiu/src/wiiu_tick_snapshot_timers.c installs them when the program starts); the host's runs have none */
static unsigned long long (*timer_clock)(void);
static void (*timer_add)(int part, unsigned long long since);

void tick_snapshot_timers_install(unsigned long long (*clock)(void), void (*add)(int part, unsigned long long since))
{
	timer_clock = clock;
	timer_add = add;
}

static unsigned long rotation_snapshots, rotation_nodes;

void tick_snapshot_note_rotations(long nodes)
{
	rotation_snapshots++;
	rotation_nodes += (unsigned long)nodes;
}

unsigned long long tick_snapshot_time(void)
{
	return timer_clock ? timer_clock() : 0ULL;
}

void tick_snapshot_time_add(int part, unsigned long long since)
{
	if (timer_add)
		timer_add(part, since);
}

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

#define CLOCK() tick_snapshot_time()
#define ADD(part, since) tick_snapshot_time_add((part), (since))

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
/* no tick */
#define NO_TICK ((long)0x80000000)

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

/* the object cluster partitions' lists as of the capture: which clusters an object is in is what a frame finds the objects to draw by */
#define PARTITION_COUNT 2

struct partition_copy
{
	struct cluster_partition const *live;
	boolean valid;
	struct cluster_partition view;
	struct data_array references;
	long first_references[MAXIMUM_CLUSTERS_PER_STRUCTURE];
	struct data_reference entries[HALO_PORT_MAXIMUM_CLUSTER_REFERENCES];
};

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

/* ---------- the ring

SNAPSHOT_SLOTS captures, each of the world as one tick left it (tick, the clock's count after the tick). The tick thread writes the next capture
into a slot that is neither pinned by the frame being drawn nor one of the two newest committed: with the frame pinning at most its pair (the
latest and the tick before) and a job capturing at most two ticks (the last two of its ticks, the only ones a frame can still blend), four slots
are enough. A capture is visible to the game thread when it is committed, which the tick thread does when its job is over. */
struct slot
{
	long tick; /* NO_TICK: none */
	boolean ready;
	struct data_array array; /* the object header array as of the capture, its data the copies below */
	struct object_header_datum headers[MAXIMUM_OBJECTS_PER_MAP];
	byte *blocks[MAXIMUM_OBJECTS_PER_MAP]; /* by absolute index: the copy of an object's block (the buffer is kept) */
	unsigned short capacity[MAXIMUM_OBJECTS_PER_MAP];
	boolean copied[MAXIMUM_OBJECTS_PER_MAP]; /* this capture has a copy for the slot */
	struct camera_view cameras[MAXIMUM_LOCAL_PLAYERS];
	struct partition_copy partitions[PARTITION_COUNT];
};

static struct slot slots[TICK_SNAPSHOT_SLOTS];

static struct
{
	int mode; /* -1 not read yet */
	boolean all; /* every object is copied at each capture (the mode's bit, or the overlap's) */
	boolean overlap_requested; /* halo/tick_overlap.txt is on: the ring is drawn from beside the tick thread, the copies hold the tick's fields */
	volatile long published_tick; /* the newest committed capture's tick, NO_TICK before one */
	long captured_tick; /* the newest capture's tick */
	volatile boolean dirty; /* objects made or deleted since the last capture */
	boolean capturing_cameras;
	long serial; /* the captures made, counted from 1 */
	/* the view (the game thread's): the slot a frame is drawn from, and the one before it for the blend (NULL: none) */
	struct slot *view;
	struct slot *previous;
	boolean pair_view; /* the view is of a pinned pair (the overlap's), not of the newest capture */
	/* the slots the frame pins, for the tick thread's choice of a slot to write (-1: none) */
	volatile int pinned[2];
	long wanted[MAXIMUM_OBJECTS_PER_MAP]; /* the capture an object was last asked for in (the lazy copy of the serial mode) */
	/* the render-owned fields of each object (by absolute index) with the identifier they are of */
	short field_identifier[MAXIMUM_OBJECTS_PER_MAP];
	byte fields[MAXIMUM_OBJECTS_PER_MAP][RENDER_FIELD_BYTES];
	/* (the overlap's: the view in which an object's kept fields were put into its copy, and the view's number) */
	long applied[MAXIMUM_OBJECTS_PER_MAP];
	long view_serial;
	/* counts, for the log */
	unsigned long captures, window_captures;
	unsigned long objects_copied, bytes_copied, header_bytes, block_calls, partition_bytes;
	unsigned long misses, catch_ups, fallbacks, reads, views, camera_copies, ring_full;
	/* verify */
	unsigned long *verify_begin, *verify_end; /* [type][word]: blocks that differ from the game state's at the view's start and end */
	unsigned long verify_objects_begin, verify_objects_end, verify_views;
	unsigned long verify_size_differences;
	unsigned long verify_partition_begin, verify_partition_end; /* lists' words that differ from the game state's, summed over the views */
} s = { -1, FALSE, FALSE, NO_TICK, NO_TICK };

/* the view, for each core: the game thread's (core 1) is the only one that ever has one, so a tick on another core reads the game state
(objects.h) */
volatile int tick_snapshot_view_any;
struct data_array *volatile tick_snapshot_view_for_core[4];

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
		/* the overlap copies every object at every capture and has no lazy copy (tick_overlap.h) */
		s.overlap_requested = (s.mode & MODE_ON) != 0 && tick_overlap_requested();
		s.all = (s.mode & MODE_ALL) != 0 || s.overlap_requested;
		if (s.mode)
			platform_log("tick snapshot: on, mode %d (halo/tick_snapshot.txt)%s", s.mode, s.overlap_requested && !(s.mode & MODE_ALL) ? ", every object copied (the overlap)" : "");
	}
	return s.mode;
}

int tick_snapshot_enabled(void)
{
	return tick_snapshot_mode() & MODE_ON;
}

long tick_snapshot_none(void)
{
	return NO_TICK;
}

int tick_snapshot_viewing(void)
{
	return tick_snapshot_headers != NULL;
}

/* the slot of a tick, or NULL */
static struct slot *tick_snapshot_slot_of(long tick)
{
	int index;

	for (index = 0; index < TICK_SNAPSHOT_SLOTS; index++)
	{
		if (slots[index].ready && slots[index].tick == tick)
			return &slots[index];
	}
	return NULL;
}

/* ---------- the copies */

static void tick_snapshot_log(void)
{
	const unsigned long captures = s.window_captures ? s.window_captures : 1;

	platform_log("tick snapshot (%lu captures, %lu frames): per capture %lu objects, %lu bytes in all (%lu in %lu object copies, headers %lu, "
		"cluster lists %lu); rotations found per capture: %lu snapshots, %lu nodes; "
		"in the window: %lu misses, %lu catch-ups, %lu fallbacks, %lu reads, %lu camera copies, %lu ring full",
		s.window_captures, s.views, s.objects_copied / captures,
		(s.bytes_copied + s.header_bytes + s.partition_bytes) / captures, s.bytes_copied / captures, s.block_calls / captures,
		s.header_bytes / captures, s.partition_bytes / captures, rotation_snapshots / captures, rotation_nodes / captures, s.misses,
		s.catch_ups, s.fallbacks, s.reads, s.camera_copies, s.ring_full);
	rotation_snapshots = rotation_nodes = 0;
	s.window_captures = 0;
	s.objects_copied = s.bytes_copied = s.header_bytes = s.block_calls = s.partition_bytes = 0;
	s.misses = s.catch_ups = s.fallbacks = s.reads = s.views = s.camera_copies = s.ring_full = 0;
}

/* an object's block copied from the game state's into the slot's (a capture, or an asking for one not copied) */
static void tick_snapshot_copy_block(struct slot *slot, long absolute_index, struct object_header_datum const *live, boolean keep_fields)
{
	struct object_header_datum *header = &slot->headers[absolute_index];
	long size = live->data_size;
	long field;
	byte *block;

	if (size <= 0 || !live->datum)
		return;
	if (slot->capacity[absolute_index] < size)
	{
		/* (the game's free takes no NULL) */
		if (slot->blocks[absolute_index])
			free(slot->blocks[absolute_index]);
		slot->blocks[absolute_index] = (byte *)malloc((size_t)size);
		slot->capacity[absolute_index] = slot->blocks[absolute_index] ? (unsigned short)size : 0;
		if (!slot->blocks[absolute_index])
			return;
	}
	block = slot->blocks[absolute_index];
	memcpy(block, live->datum, (size_t)size);
	/* what the renderer wrote in the last copy of this object (not in the overlap's: the game thread writes the kept fields, and puts them in
	when it first asks for the object in a frame) */
	if (keep_fields && s.field_identifier[absolute_index] == live->identifier)
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
	slot->copied[absolute_index] = TRUE;
	object_function_inputs_copy_to_snapshot((int)(slot - slots), absolute_index);
	s.objects_copied++;
	s.bytes_copied += (unsigned long)size;
	s.block_calls++;
}

/* the object partitions' lists (the clusters' first references and the references in use), as the ticks left them */
static void tick_snapshot_capture_partitions(struct slot *slot)
{
	long partition_index;

	slot->partitions[0].live = &collideable_object_cluster_partition;
	slot->partitions[1].live = &noncollideable_object_cluster_partition;
	for (partition_index = 0; partition_index < PARTITION_COUNT; partition_index++)
	{
		struct partition_copy *copy = &slot->partitions[partition_index];
		struct cluster_partition const *live = copy->live;
		struct data_array const *references = live->data_reference_data;

		copy->valid = FALSE;
		if (!live->cluster_first_data_references || !references || !references->valid)
			continue;
		memcpy(copy->first_references, live->cluster_first_data_references, sizeof(copy->first_references));
		copy->references = *references;
		if (copy->references.count > HALO_PORT_MAXIMUM_CLUSTER_REFERENCES)
			copy->references.count = HALO_PORT_MAXIMUM_CLUSTER_REFERENCES;
		memcpy(copy->entries, references->data, (size_t)copy->references.count * sizeof(copy->entries[0]));
		copy->references.data = copy->entries;
		copy->view = *live;
		copy->view.cluster_first_data_references = copy->first_references;
		copy->view.data_reference_data = &copy->references;
		copy->valid = TRUE;
		s.partition_bytes += sizeof(copy->first_references) + (unsigned long)copy->references.count * sizeof(copy->entries[0]);
	}
}

struct cluster_partition const *tick_snapshot_cluster_partition(struct cluster_partition const *live)
{
	long partition_index;
	struct slot *view = s.view;

	if (!tick_snapshot_headers || !view)
		return live;
	for (partition_index = 0; partition_index < PARTITION_COUNT; partition_index++)
	{
		if (view->partitions[partition_index].live == live && view->partitions[partition_index].valid)
			return &view->partitions[partition_index].view;
	}
	return live;
}

static boolean tick_snapshot_slot_pinned(int index)
{
	return s.pinned[0] == index || s.pinned[1] == index;
}

/* the slot a capture of tick is written to: that tick's own if it has one (the catch-up of the serial mode), else an unused one, else the oldest
that is not pinned and is neither one of the two newest committed nor newer than them (the captures of the job being run). NULL if there is none */
static struct slot *tick_snapshot_slot_for(long tick)
{
	struct slot *found = tick_snapshot_slot_of(tick);
	struct slot *best = NULL;
	int index;

	if (found && !tick_snapshot_slot_pinned((int)(found - slots)))
		return found;
	for (index = 0; index < TICK_SNAPSHOT_SLOTS; index++)
	{
		struct slot *slot = &slots[index];

		if (tick_snapshot_slot_pinned(index))
			continue;
		if (!slot->ready)
			return slot;
		if (s.published_tick != NO_TICK && slot->tick <= s.published_tick - 2 && (!best || slot->tick < best->tick))
			best = slot;
	}
	return best;
}

void tick_snapshot_capture_tick(long tick, boolean commit)
{
	struct data_array const *live = object_header_data;
	struct object_header_datum const *live_headers;
	struct slot *slot;
	long count, previous_count, index;
	unsigned long long started, headers_started;

	if (!tick_snapshot_mode() || !live || !live->valid)
		return;
	started = CLOCK();
	slot = tick_snapshot_slot_for(tick);
	if (!slot)
	{
		/* (all four are pinned or newer than the committed: the job captured more than it should) */
		s.ring_full++;
		return;
	}
	live_headers = (struct object_header_datum const *)live->data;
	count = live->count;
	previous_count = slot->array.count;
	slot->ready = FALSE;
	slot->tick = tick;
	s.serial++;
	headers_started = CLOCK();
	/* the headers, whole */
	if (count > MAXIMUM_OBJECTS_PER_MAP)
		count = MAXIMUM_OBJECTS_PER_MAP;
	memcpy(slot->headers, live_headers, (size_t)count * sizeof(slot->headers[0]));
	for (index = count; index < previous_count && index < MAXIMUM_OBJECTS_PER_MAP; index++)
		slot->headers[index].identifier = 0;
	slot->array = *live;
	slot->array.data = slot->headers;
	s.header_bytes += (unsigned long)count * sizeof(slot->headers[0]);
	ADD(PART_HEADERS, headers_started);
	headers_started = CLOCK();
	/* the blocks of the objects drawn lately (the overlap: all of them, a tick being free to change what a lazy copy would read) */
	for (index = 0; index < count; index++)
	{
		slot->copied[index] = FALSE;
		slot->headers[index].datum = NULL;
		if (!slot->headers[index].identifier)
			continue;
		if (s.all || (s.wanted[index] && s.serial - s.wanted[index] <= DRAWN_CAPTURES))
			tick_snapshot_copy_block(slot, index, &live_headers[index], !s.overlap_requested);
	}
	for (; index < previous_count && index < MAXIMUM_OBJECTS_PER_MAP; index++)
		slot->copied[index] = FALSE;
	tick_snapshot_capture_partitions(slot);
	memset(slot->cameras, 0, sizeof(slot->cameras));
	ADD(PART_BLOCKS, headers_started);
	slot->ready = TRUE;
	s.captured_tick = tick;
	s.dirty = FALSE;
	s.captures++;
	s.window_captures++;
	if (commit)
		s.published_tick = tick;
	ADD(PART_CAPTURE, started);
	if (s.window_captures >= LOG_CAPTURES)
		tick_snapshot_log();
}

/* the serial mode's catch-up (a view begun with no capture of this map's objects, or after objects were made or deleted): the world as it is, with
the ticks finished */
void tick_snapshot_capture(void)
{
	tick_snapshot_capture_tick(game_time_initialized() ? game_time_get() : 0, TRUE);
}

void tick_snapshot_commit(void)
{
	if (s.captured_tick != NO_TICK && (s.published_tick == NO_TICK || s.captured_tick > s.published_tick))
		s.published_tick = s.captured_tick;
}

long tick_snapshot_published_tick(void)
{
	return s.published_tick;
}

void tick_snapshot_reset(void)
{
	long index;
	int slot_index;

	for (slot_index = 0; slot_index < TICK_SNAPSHOT_SLOTS; slot_index++)
	{
		struct slot *slot = &slots[slot_index];

		slot->ready = FALSE;
		slot->tick = NO_TICK;
		memset(&slot->array, 0, sizeof(slot->array));
		for (index = 0; index < MAXIMUM_OBJECTS_PER_MAP; index++)
		{
			slot->headers[index].identifier = 0;
			slot->headers[index].datum = NULL;
			slot->copied[index] = FALSE;
		}
		memset(slot->cameras, 0, sizeof(slot->cameras));
		for (index = 0; index < PARTITION_COUNT; index++)
			slot->partitions[index].valid = FALSE;
	}
	for (index = 0; index < MAXIMUM_OBJECTS_PER_MAP; index++)
	{
		s.wanted[index] = 0;
		s.field_identifier[index] = 0;
		s.applied[index] = 0;
	}
	s.published_tick = NO_TICK;
	s.captured_tick = NO_TICK;
	s.dirty = FALSE;
	s.view = NULL;
	s.previous = NULL;
	s.pair_view = FALSE;
	s.pinned[0] = s.pinned[1] = -1;
	tick_snapshot_view_for_core[tick_overlap_render_core() & 3] = NULL;
	tick_snapshot_view_any = 0;
}

void tick_snapshot_objects_changed(void)
{
	s.dirty = TRUE;
}

/* ---------- the accessors */

/* an object not copied at the capture (the serial mode's lazy copy): copied now, from the game state (the ticks have finished). The game state's
header if the snapshot has none to give or the object is another than it holds */
static struct object_header_datum *tick_snapshot_fill(long object_index, struct object_header_datum *header)
{
	const long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	struct object_header_datum *live = (struct object_header_datum *)((byte *)object_header_data->data +
		object_header_data->size * absolute_index);

	/* (with the overlap there is no such thing as the ticks having finished) */
	if (s.overlap_requested || live->identifier != header->identifier || !live->datum)
	{
		s.fallbacks++;
		return live;
	}
	s.misses++;
	tick_snapshot_copy_block(s.view, absolute_index, live, TRUE);
	if (!header->datum)
	{
		s.fallbacks++;
		return live;
	}
	return header;
}

/* the kept render-owned fields put into the object's copy the first time a frame asks for it (the overlap) */
static void tick_snapshot_apply_fields(long absolute_index, struct object_header_datum *header)
{
	byte const *kept = s.fields[absolute_index];
	long field;

	s.applied[absolute_index] = s.view_serial;
	if (s.field_identifier[absolute_index] != header->identifier)
		return;
	for (field = 0; field < RENDER_FIELD_COUNT; field++)
	{
		if (RENDER_FIELDS[field].offset + RENDER_FIELDS[field].size <= header->data_size)
			memcpy((byte *)header->datum + RENDER_FIELDS[field].offset, kept, RENDER_FIELDS[field].size);
		kept += RENDER_FIELDS[field].size;
	}
}

struct object_header_datum *tick_snapshot_object_header_get(long object_index)
{
	struct object_header_datum *header = (struct object_header_datum *)datum_try_and_get(&s.view->array, object_index);

	s.reads++;
	if (!header)
	{
		/* (an object the snapshot has not: made since it, and the view's start would have caught it) */
		s.fallbacks++;
		return (struct object_header_datum *)datum_get(object_header_data, object_index);
	}
	if (s.overlap_requested && header->datum)
	{
		const long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);

		if (s.applied[absolute_index] != s.view_serial)
			tick_snapshot_apply_fields(absolute_index, header);
		return header;
	}
	s.wanted[DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index)] = s.serial;
	if (!header->datum)
		return tick_snapshot_fill(object_index, header);
	return header;
}

struct object_header_datum *tick_snapshot_object_header_try_and_get(long object_index)
{
	struct object_header_datum *header = (struct object_header_datum *)datum_try_and_get(&s.view->array, object_index);

	s.reads++;
	if (!header)
	{
		struct object_header_datum *live = (struct object_header_datum *)datum_try_and_get(object_header_data, object_index);

		if (live)
			s.fallbacks++;
		return live;
	}
	if (s.overlap_requested && header->datum)
	{
		const long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);

		if (s.applied[absolute_index] != s.view_serial)
			tick_snapshot_apply_fields(absolute_index, header);
		return header;
	}
	s.wanted[DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index)] = s.serial;
	if (!header->datum)
		return tick_snapshot_fill(object_index, header);
	return header;
}

/* the node matrices an object had one tick before the view's (the overlap's blend): NULL if the object has no copy there, is another, or has
another number of nodes */
real_matrix4x3 const *tick_snapshot_previous_node_matrices(long object_index, short node_count)
{
	struct slot *previous = s.previous;
	struct object_header_datum const *header;
	struct object_header_datum const *current;
	struct object_datum const *object;
	long absolute_index;

	if (!previous || !s.view)
		return NULL;
	absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	if (absolute_index < 0 || absolute_index >= MAXIMUM_OBJECTS_PER_MAP || absolute_index >= previous->array.count)
		return NULL;
	header = &previous->headers[absolute_index];
	current = &s.view->headers[absolute_index];
	if (!header->identifier || header->identifier != current->identifier || !previous->copied[absolute_index] || !header->datum)
		return NULL;
	object = header->datum;
	if (object->object.node_matrices.size / (short)sizeof(real_matrix4x3) != node_count ||
		object->object.node_matrices.offset <= 0 ||
		object->object.node_matrices.offset + object->object.node_matrices.size > header->data_size)
	{
		return NULL;
	}
	return (real_matrix4x3 const *)((byte const *)object + object->object.node_matrices.offset);
}

/* ---------- the cameras */

static void tick_snapshot_fill_camera(struct camera_view *view, short local_player_index)
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
}

/* a local player's camera as of this frame's start, kept for the frame (the serial mode); as the tick thread left it with the capture (the
overlap's) */
static struct camera_view *tick_snapshot_camera_view(short local_player_index)
{
	struct camera_view *view;

	if (!tick_snapshot_headers || !s.view || s.capturing_cameras || local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS)
		return NULL;
	view = &s.view->cameras[local_player_index];
	if (!view->valid)
	{
		if (s.pair_view)
			return NULL;
		tick_snapshot_fill_camera(view, local_player_index);
		s.camera_copies++;
	}
	return view;
}

/* the tick thread, at the end of the job that ran the ticks up to tick (ticks of them, the last two captured): the cameras of the newest capture, and
of the one before it when the job made it too */
void tick_snapshot_job_end(long tick, long ticks_in_job, long previous_job_tick)
{
	struct slot *newest = tick_snapshot_slot_of(tick);
	struct slot *before;
	struct slot *earlier;
	short local_player_index;

	if (!newest || ticks_in_job <= 0)
		return;
	for (local_player_index = 0; local_player_index < MAXIMUM_LOCAL_PLAYERS; local_player_index++)
	{
		if (local_player_get_player_index(local_player_index) == NONE)
			continue;
		tick_snapshot_fill_camera(&newest->cameras[local_player_index], local_player_index);
		s.camera_copies++;
	}
	/* the tick before: a tick of this job (its camera is where a steady move from the last job's, to this job's, has it: the serial mode takes it
	at the first frame after a tick, and a long frame blends the same way, render_interpolation.c), else the last job's */
	before = tick_snapshot_slot_of(tick - 1);
	earlier = previous_job_tick != NO_TICK ? tick_snapshot_slot_of(previous_job_tick) : NULL;
	if (before && before != newest && ticks_in_job >= 2)
	{
		for (local_player_index = 0; local_player_index < MAXIMUM_LOCAL_PLAYERS; local_player_index++)
		{
			struct camera_view *view = &before->cameras[local_player_index];

			*view = newest->cameras[local_player_index];
			if (earlier && earlier->cameras[local_player_index].valid && earlier->cameras[local_player_index].has_result && view->has_result)
			{
				render_interpolation_camera_between(&earlier->cameras[local_player_index].result, &newest->cameras[local_player_index].result,
					(real)(ticks_in_job - 1) / (real)ticks_in_job, &view->result);
			}
		}
	}
	tick_snapshot_commit();
}

struct observer_result const *tick_snapshot_camera(short local_player_index, struct observer_result const *live)
{
	struct camera_view *view = tick_snapshot_camera_view(local_player_index);

	if (!view || !view->has_result)
		return live;
	return &view->result;
}

/* the cameras of the pair a frame is drawn between (the overlap's): the tick before (NULL: none) and the view's tick's */
int tick_snapshot_view_cameras(short local_player_index, struct observer_result const **previous, struct observer_result const **latest)
{
	struct camera_view *view;

	*previous = *latest = NULL;
	if (!s.view || local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS)
		return 0;
	view = &s.view->cameras[local_player_index];
	if (!view->valid || !view->has_result)
		return 0;
	*latest = &view->result;
	if (s.previous && s.previous->cameras[local_player_index].valid && s.previous->cameras[local_player_index].has_result)
		*previous = &s.previous->cameras[local_player_index].result;
	return 1;
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
	struct slot *view = s.view;
	unsigned long *histogram = at_end ? s.verify_end : s.verify_begin;
	unsigned long objects = 0;

	if (!histogram || !view)
		return;
	for (index = 0; index < view->array.count && index < MAXIMUM_OBJECTS_PER_MAP; index++)
	{
		struct object_header_datum const *live = (struct object_header_datum const *)((byte const *)object_header_data->data +
			object_header_data->size * index);
		long word, words;
		boolean different = FALSE;

		if (!view->copied[index] || live->identifier != view->headers[index].identifier || !live->datum)
			continue;
		if (live->data_size != view->headers[index].data_size)
		{
			s.verify_size_differences++;
			continue;
		}
		words = live->data_size / 4;
		for (word = 0; word < words; word++)
		{
			if (((unsigned long const *)live->datum)[word] != ((unsigned long const *)view->blocks[index])[word])
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
	/* the cluster lists: the first references and the entries in use */
	for (index = 0; index < PARTITION_COUNT; index++)
	{
		struct partition_copy const *copy = &view->partitions[index];
		unsigned long differing = 0;
		long word;

		if (!copy->valid || !copy->live->cluster_first_data_references || !copy->live->data_reference_data->valid)
			continue;
		for (word = 0; word < MAXIMUM_CLUSTERS_PER_STRUCTURE; word++)
			differing += copy->first_references[word] != copy->live->cluster_first_data_references[word];
		if (copy->live->data_reference_data->count != copy->references.count)
			differing++;
		for (word = 0; word < copy->references.count && word < copy->live->data_reference_data->count; word++)
		{
			struct data_reference const *live_entry = (struct data_reference const *)copy->live->data_reference_data->data + word;

			differing += memcmp(live_entry, &copy->entries[word], sizeof(*live_entry)) != 0;
		}
		if (at_end)
			s.verify_partition_end += differing;
		else
			s.verify_partition_begin += differing;
	}
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
		"(written by the frame); blocks of another size %lu; cluster list entries that differ, in all: %lu at the start, %lu at the end",
		s.verify_views, s.verify_objects_begin / (s.verify_views ? s.verify_views : 1),
		s.verify_objects_end / (s.verify_views ? s.verify_views : 1), s.verify_size_differences, s.verify_partition_begin,
		s.verify_partition_end);
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

/* the overlap: the slot of a tick (and the one before it) pinned for the frame about to be drawn, by the game thread while the tick thread is
not running a job. FALSE if there is no committed capture of that tick. */
boolean tick_snapshot_pin(long tick)
{
	struct slot *latest = s.published_tick == NO_TICK || tick > s.published_tick ? NULL : tick_snapshot_slot_of(tick);
	struct slot *before;

	s.pinned[0] = s.pinned[1] = -1;
	if (!latest)
		return FALSE;
	before = tick_snapshot_slot_of(tick - 1);
	s.pinned[0] = (int)(latest - slots);
	s.pinned[1] = before ? (int)(before - slots) : -1;
	return TRUE;
}

void tick_snapshot_unpin(void)
{
	s.pinned[0] = s.pinned[1] = -1;
}

/* whether a pair of the ring can be drawn: the newest committed tick (NO_TICK: none) */
long tick_snapshot_view_tick(void)
{
	return s.view && tick_snapshot_headers ? s.view->tick : NO_TICK;
}

void tick_snapshot_view_begin(void)
{
	unsigned long long started;
	struct slot *latest = NULL;
	struct slot *before = NULL;

	if (!tick_snapshot_mode() || !object_header_data || !object_header_data->valid)
		return;
	started = CLOCK();
	if (s.pinned[0] >= 0)
	{
		/* the overlap's: the tick pinned (tick_snapshot_pin) */
		latest = &slots[s.pinned[0]];
		before = s.pinned[1] >= 0 ? &slots[s.pinned[1]] : NULL;
		if (!latest->ready)
			return;
	}
	else
	{
		if (s.published_tick == NO_TICK || s.dirty)
		{
			/* (objects made or deleted since the last capture, or no tick yet in this map: the ticks have finished, so the copy is of now) */
			tick_snapshot_capture();
			s.catch_ups++;
		}
		if (s.published_tick == NO_TICK)
			return;
		latest = tick_snapshot_slot_of(s.published_tick);
		if (!latest)
			return;
		memset(latest->cameras, 0, sizeof(latest->cameras));
	}
	s.view = latest;
	s.previous = before;
	s.pair_view = s.pinned[0] >= 0;
	s.view_serial++;
	object_function_inputs_set_view((int)(latest - slots));
	if ((s.mode & MODE_VERIFY) && !s.verify_begin)
	{
		s.verify_begin = (unsigned long *)calloc((size_t)VERIFY_TYPES * VERIFY_WORDS, sizeof(unsigned long));
		s.verify_end = (unsigned long *)calloc((size_t)VERIFY_TYPES * VERIFY_WORDS, sizeof(unsigned long));
	}
	if (s.verify_begin && s.pinned[0] < 0)
		tick_snapshot_verify(FALSE);
	/* (the headers last: from here the accessors on this core give the copies) */
	tick_snapshot_view_for_core[tick_overlap_render_core() & 3] = &latest->array;
	tick_snapshot_view_any = 1;
	s.views++;
	ADD(PART_VIEW_BEGIN, started);
}

void tick_snapshot_view_end(void)
{
	unsigned long long started;
	struct slot *view = s.view;
	long index, field;

	if (!tick_snapshot_headers || !view)
		return;
	started = CLOCK();
	if (s.verify_end && s.pinned[0] < 0)
	{
		tick_snapshot_verify(TRUE);
		if (++s.verify_views % 300 == 0)
			tick_snapshot_verify_log();
	}
	/* what the frame wrote in the render-owned fields of the copies, kept for the next copy of the object */
	for (index = 0; index < view->array.count && index < MAXIMUM_OBJECTS_PER_MAP; index++)
	{
		byte *kept;

		if (!view->copied[index] || !view->headers[index].identifier)
			continue;
		/* (the overlap: only the objects the frame asked for hold what it wrote; the others' copies hold the tick's) */
		if (s.overlap_requested && s.applied[index] != s.view_serial)
			continue;
		kept = s.fields[index];
		for (field = 0; field < RENDER_FIELD_COUNT; field++)
		{
			if (RENDER_FIELDS[field].offset + RENDER_FIELDS[field].size <= view->headers[index].data_size)
				memcpy(kept, view->blocks[index] + RENDER_FIELDS[field].offset, RENDER_FIELDS[field].size);
			kept += RENDER_FIELDS[field].size;
		}
		s.field_identifier[index] = view->headers[index].identifier;
	}
	tick_snapshot_view_for_core[tick_overlap_render_core() & 3] = NULL;
	tick_snapshot_view_any = 0;
	s.view = NULL;
	s.previous = NULL;
	s.pair_view = FALSE;
	ADD(PART_VIEW_END, started);
	ADD(PART_FRAME, 0);
}

#else /* !HALO_WIIU */

typedef int tick_snapshot_is_for_the_wii_u_only;

#endif
