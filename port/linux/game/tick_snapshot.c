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
 32  measure: once in each log window (5 s) a bench copies the same blocks into scratch buffers in other ways, each after a sweep of the caches, and says how long each took
     (tick_snapshot_bench): as the capture copies them, asking for the next block's lines ahead, without the orientation blocks, the pool in one copy, and again at once.
 64  measure (host): each block compared with the capture before it, and the log says what changed by part of the block.
128  check (host): each object left out of a capture because nothing marked it is compared with the game state, and a difference is said (it should be none).
256  the orientation blocks of an object are copied after all (they are left out: only the ticks' animation reads them; the first cause of the capture's size).
512  every object is copied at every capture, even those nothing marked since the slot's copy (the unchanged are left out: below).
1024 the next block's cache lines are not asked for ahead while a block is copied.
2048 the blends find the rotations of both poses of an object each frame (they keep the rotations found for a pose: render_interpolation.c).
4096 test: what the copies leave out is filled with 0xCD, so that a read of it would change the frames.
8192 test aid: each frame drawn from the ring says a hash of what it can read of its two captures (tick_snapshot_hash.inc).
256, 512, 1024 and 2048 are the ways to run this build as the one before it copied (the sum, 3840, is the same as "all four off"); with only the file's 1 the build copies as
described below.

What a capture leaves out (the overlap): the orientation blocks of every object, and the objects nothing has marked since the slot's last copy of them. The accessors of
the game state (objects.h: object_header_get, object_header_try_and_get, the object iterator) mark an object, with the number of the first capture that can see the mark
(tick_snapshot_dirty); an object being updated is not marked by what its update does, but its block is compared with what it was when the update began and marked if it is not
the same (tick_snapshot_scope_begin/end), and the collision tests, which only read objects, mark none. A slot of the ring that copied an object in a capture numbered at or
after the last mark has the object as the game state has it, and the next capture into the slot does not copy it again. The ring's four slots each hold their own copy, so an
object that changes is copied into each of them in turn. The thread that draws, outside a frame's view, marks for good.

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
#include "smooth_defaults.h" /* port: what the cards and the map decide (smooth_defaults.c) */
#include "tick_perf.h"
#include "tick_ring.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* (the card's file is read with the C library's fopen: the game's stdio.h takes the Xbox's paths) */
#undef fopen

void platform_log(char const *format, ...);

/* a line of the log holds 199 characters on the console (platform_log), and the numbers here are more: said in lines of up to 185, each broken after a comma or a semicolon, the next
begun with "... " */
void tick_snapshot_say(char const *format, ...)
{
	char text[1024];
	char const *at = text;
	boolean first = TRUE;
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	while (*at)
	{
		size_t length = strlen(at), cut = length;

		if (length > 185)
		{
			size_t back;

			for (back = 185; back > 90 && !(at[back - 1] == ' ' && (at[back - 2] == ',' || at[back - 2] == ';')); back--)
				;
			cut = back > 90 ? back : 185;
		}
		platform_log("%s%.*s", first ? "" : "... ", (int)cut, at);
		at += cut;
		first = FALSE;
	}
}

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

/* object_lights.c's */
extern struct data_array *light_data;
extern struct cluster_partition light_cluster_partition;

enum
{
	MODE_ON = 1,
	MODE_ALL = 2,
	MODE_POISON_OBJECTS = 4,
	MODE_VERIFY = 8,
	MODE_POISON_CAMERAS = 16,
	MODE_HASH = 8192, /* test aid: each frame drawn from the ring says a hash of what it can read of the two captures (tick_snapshot_hash.inc) */
	MODE_BENCH = 32, /* the copies measured against other ways of making them (tick_snapshot_bench), once in each log window */
	MODE_MEASURE = 64, /* what changed in each object since the capture before (a host measure: it reads every block twice) */
	MODE_CHECK_SKIPS = 128, /* a host check: each object left out of a capture because nothing accessed it is compared with the game state, and a difference is said */
	MODE_KEEP_ORIENTATIONS = 256, /* the orientation blocks are copied after all (off: they are left out) */
	MODE_COPY_ALL = 512, /* every object is copied at every capture, even if nothing accessed it since (off: the unchanged are left out) */
	MODE_NO_PREFETCH = 1024, /* the next block's cache lines are not asked for while one is copied */
	MODE_NO_ROTATION_CACHE = 2048, /* the blends find the rotations of both poses every frame (off: each pose's are found once and kept) */
	MODE_POISON_LEFT_OUT = 4096, /* test: what the copies leave out is filled with 0xCD, so that a read of it changes the frames */
	MODE_FLUSH = 16384, /* the tick thread writes its copies back to memory after a capture (dcbst), so that the game thread's reads find them in memory, not in the tick core's cache. Off by default: the console's second matrix (2026-11-03) found no difference */
	MODE_VIEW_PREFETCH = 32768, /* the game thread asks for the cache lines of the newest capture's new copies when a frame begins. Off by default: it cost 1.0 to 1.1 ms a frame for no gain in the frame (rendering 25.25 ms with it, 25.4 without) */
};

/* an object copied at a capture if it was asked for within this many captures */
#define DRAWN_CAPTURES 3
#define LOG_CAPTURES 150
/* the verify mode's table: object types, and words of a block */
#define VERIFY_TYPES 16
#define VERIFY_WORDS 4096
/* no tick */
#define NO_TICK TICK_RING_NONE

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
#define PARTITION_COUNT 3

struct partition_copy
{
	struct cluster_partition const *live;
	boolean valid;
	struct cluster_partition view;
	struct data_array references;
	long first_references[MAXIMUM_CLUSTERS_PER_STRUCTURE];
	struct data_reference entries[HALO_PORT_MAXIMUM_CLUSTER_REFERENCES];
	/* and the other way round: the clusters each object is in (an object's first_cluster_reference_index is the first of its list) */
	struct data_array cluster_references;
	struct data_reference cluster_entries[HALO_PORT_MAXIMUM_CLUSTER_REFERENCES];
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
	struct data_array array; /* the object header array as of the capture, its data the copies below */
	struct object_header_datum headers[MAXIMUM_OBJECTS_PER_MAP];
	byte *blocks[MAXIMUM_OBJECTS_PER_MAP]; /* by absolute index: the copy of an object's block (the buffer is kept) */
	unsigned short capacity[MAXIMUM_OBJECTS_PER_MAP];
	boolean copied[MAXIMUM_OBJECTS_PER_MAP]; /* this capture has a copy for the slot */
	/* what the slot refers to of an object, by the versions below (the lazy mode of the serial frames has buffers of its own: blocks) */
	long copy_serial[MAXIMUM_OBJECTS_PER_MAP]; /* the capture the buffer was filled in */
	signed char ref[MAXIMUM_OBJECTS_PER_MAP]; /* (the overlap's) the version of the object the slot refers to, plus one (0: none) */
	long capture_serial; /* the capture that last wrote the slot, counted from 1 over the whole run: the same number is the same pose of every object in it */
	unsigned short changed[MAXIMUM_OBJECTS_PER_MAP]; /* the objects this capture copied afresh (a frame asks for the cache lines of those when it begins) */
	long changed_count;
	struct camera_view cameras[MAXIMUM_LOCAL_PLAYERS];
	struct partition_copy partitions[PARTITION_COUNT];
	/* the lights as of the capture: the light array (header and the used part of its data) the frame reads them through (object_lights.c's LIGHT_DATA) */
	boolean lights_valid;
	struct data_array lights;
	byte *light_bytes;
	long light_bytes_capacity;
};

static struct slot slots[TICK_SNAPSHOT_SLOTS];

static struct
{
	int mode; /* -1 not read yet */
	boolean all; /* every object is copied at each capture (the mode's bit, or the overlap's) */
	boolean skip_unchanged; /* the objects nothing has accessed since a slot's copy are left out of the capture into it */
	boolean rotation_cache; /* the blends keep the rotations found for a capture's pose of an object */
	boolean prefetch; /* the cache lines of the next block are asked for ahead */
	boolean flush; /* the copies are written back to memory after the capture (dcbst) */
	boolean view_prefetch; /* the frame's begin asks for the cache lines of the newest capture's new copies */
	boolean skip_orientations; /* the orientation blocks of an object are not copied: only the ticks' animation reads them */
	boolean overlap_requested; /* halo/tick_overlap.txt is on: the ring is drawn from beside the tick thread, the copies hold the tick's fields */
	struct tick_ring ring; /* which slot holds which tick, what is committed, what is pinned (tick_ring.h) */
	volatile boolean dirty; /* objects made or deleted since the last capture */
	boolean capturing_cameras;
	long serial; /* the captures made, counted from 1 */
	/* the view (the game thread's): the slot a frame is drawn from, and the one before it for the blend (NULL: none) */
	struct slot *view;
	struct slot *previous;
	boolean pair_view; /* the view is of a pinned pair (the overlap's), not of the newest capture */
	long wanted[MAXIMUM_OBJECTS_PER_MAP]; /* the capture an object was last asked for in (the lazy copy of the serial mode) */
	/* the render-owned fields of each object (by absolute index) with the identifier they are of */
	short field_identifier[MAXIMUM_OBJECTS_PER_MAP];
	byte fields[MAXIMUM_OBJECTS_PER_MAP][RENDER_FIELD_BYTES];
	/* (the overlap's: the view in which an object's kept fields were put into its copy, and the view's number) */
	long applied[MAXIMUM_OBJECTS_PER_MAP];
	long applied_list[MAXIMUM_OBJECTS_PER_MAP]; /* ... the objects they were put into in this view, so that its end looks at those only */
	long applied_count;
	long view_serial;
	/* counts, for the log */
	unsigned long captures, window_captures;
	unsigned long objects_copied, bytes_copied, header_bytes, block_calls, partition_bytes, light_bytes;
	unsigned long misses, catch_ups, fallbacks, reads, views, camera_copies, ring_full, pair_absent;
	/* (the game thread's, never reset: the blends' log says the difference between its windows) */
	unsigned long reads_total, fallbacks_total, applied_total;
	/* verify */
	unsigned long *verify_begin, *verify_end; /* [type][word]: blocks that differ from the game state's at the view's start and end */
	unsigned long verify_objects_begin, verify_objects_end, verify_views;
	unsigned long verify_size_differences;
	unsigned long verify_partition_begin, verify_partition_end; /* lists' words that differ from the game state's, summed over the views */
} s = { -1 };

/* ---------- the fine timers

What a capture does, split by part, in the console's clock ticks (tick_overlap_now), with the counts and bytes, so that one run says where the time of a capture goes. The tick
thread's only (a capture is made by it); said with the capture log line, every LOG_CAPTURES captures. A mark is a read of the time base (mftb in an OS call: some tens of nanoseconds);
a capture reads it about 3 times an object. The parts of the frame's blends (render_interpolation.c) are said by tick_snapshot_blend_log from the overlap's log. */

#define SIZE_BUCKETS 6 /* the copies by size: under 128 bytes, 256, 512, 1024, 2048, more */

static struct
{
	unsigned long captures;
	/* ticks */
	unsigned long long total, headers, blocks, copy, inputs, kept, alloc, partitions, lights, cameras, ring, bench;
	/* counts and bytes, summed over the captures */
	unsigned long objects, bytes, bytes_fixed, bytes_type, bytes_matrices, bytes_orientations;
	unsigned long jumps, sequential; /* the copied objects' live blocks: the next one is the one after in memory, or is not */
	unsigned long grown; /* a copy's buffer made or made larger */
	unsigned long type_objects[16], type_bytes[16];
	unsigned long long bucket_ticks[SIZE_BUCKETS];
	unsigned long bucket_count[SIZE_BUCKETS], bucket_bytes[SIZE_BUCKETS];
	unsigned long partition_bytes_first, partition_bytes_entries, partition_bytes_clusters;
	/* the misaligned accesses the handler took on this core during the captures */
	unsigned long fixups;
	/* the objects left out because nothing accessed them, and (the host check) the ones that were not the same after all */
	unsigned long skipped, shared, skipped_bytes, marked, marked_for_good, skipped_wrong, function_inputs_skipped;
	unsigned long marked_type[16], skipped_type[16];
	struct tick_perf perf; /* the counters of the core, over the captures (tick_perf.h) */
	unsigned perf_core;
	/* the change measure (mode bit 64): objects whose live block differs from the last capture's copy */
	unsigned long compared, changed, changed_fixed, changed_type, changed_matrices, changed_orientations, changed_words;
} cs;

uint32_t wiiu_crash_fixups_here(void);
unsigned long wiiu_ticks_per_second(void);

static void tick_snapshot_bench(void);
static void tick_snapshot_capture_log(void);

static int size_bucket(long size)
{
	return size < 128 ? 0 : size < 256 ? 1 : size < 512 ? 2 : size < 1024 ? 3 : size < 2048 ? 4 : 5;
}

/* the view: the drawing thread's is the only one that ever has one (objects.h's tick_snapshot_headers finds the calling thread by its stack), so a tick
on another thread reads the game state */
volatile int tick_snapshot_view_any;
volatile int tick_snapshot_lights_in_view; /* a frame drawn from a pinned pair reads the lights from the ring (tick_snapshot_light_array) */
struct data_array *volatile tick_snapshot_view_array;

/* the accessors' marks (objects.h): set while the snapshot skips the objects nothing has accessed since a slot's copy */
volatile int tick_snapshot_dirty_tracking;
volatile int tick_snapshot_read_only;
volatile int tick_snapshot_render_read_only;
volatile unsigned long tick_snapshot_dirty[HALO_PORT_MAXIMUM_OBJECTS_PER_MAP];
volatile unsigned long tick_snapshot_dirty_next = 1;
volatile unsigned char tick_snapshot_in_scope[HALO_PORT_MAXIMUM_OBJECTS_PER_MAP];

/* ---------- the switch */

/* the card's word for the snapshot, raw: halo/tick_snapshot.txt (HALO_TICK_SNAPSHOT on the host), -1 if there is none or it is negative (the default: smooth_defaults.h), 0 off, else the
bits. Read once. */
long tick_snapshot_card(void)
{
	static long card = -2;

	if (card == -2)
	{
		char const *environment = getenv("HALO_TICK_SNAPSHOT");
		long value = -1;

		if (environment && *environment)
		{
			value = strtol(environment, NULL, 0);
			if (value < 0)
				value = -1;
		}
		else
		{
			FILE *file = fopen(SNAPSHOT_PATH, "r");

			if (file)
			{
				if (fscanf(file, "%li", &value) != 1 || value < 0)
					value = -1; /* (-1: as no file) */
				fclose(file);
			}
		}
		card = value;
	}
	return card;
}

static int tick_snapshot_mode(void)
{
	if (s.mode < 0)
	{
		/* (what the cards and the map decide for this map: smooth_defaults.c; before a first map starts, the card as it has always been read) */
		long value = smooth_defaults_value(SMOOTH_TICK_SNAPSHOT);

		if (value < 0)
			value = 0;
		s.mode = (int)value;
		/* the overlap copies every object at every capture and has no lazy copy (tick_overlap.h) */
		s.overlap_requested = (s.mode & MODE_ON) != 0 && tick_overlap_requested();
		s.all = (s.mode & MODE_ALL) != 0 || s.overlap_requested;
		s.skip_orientations = (s.mode & MODE_ON) != 0 && !(s.mode & MODE_KEEP_ORIENTATIONS);
		s.skip_unchanged = s.overlap_requested && !(s.mode & MODE_COPY_ALL);
		s.prefetch = (s.mode & MODE_ON) != 0 && !(s.mode & MODE_NO_PREFETCH);
		s.flush = s.overlap_requested && (s.mode & MODE_FLUSH) != 0;
		s.view_prefetch = s.overlap_requested && (s.mode & MODE_VIEW_PREFETCH) != 0;
		s.rotation_cache = s.overlap_requested && !(s.mode & MODE_NO_ROTATION_CACHE);
		if (s.skip_unchanged)
		{
			memset((void *)tick_snapshot_dirty, 0, sizeof(tick_snapshot_dirty));
			tick_snapshot_dirty_tracking = 1;
		}
		else
			tick_snapshot_dirty_tracking = 0;
		if (s.mode)
			platform_log("tick snapshot: on, mode %d (halo/tick_snapshot.txt or the smooth defaults)%s", s.mode, s.overlap_requested && !(s.mode & MODE_ALL) ? ", every object copied (the overlap)" : "");
	}
	return s.mode;
}

int tick_snapshot_enabled(void)
{
	return tick_snapshot_mode() & MODE_ON;
}

/* The map's decision (smooth_defaults.c, tick_overlap_decide), at a map start with the tick thread idle: the ring, the versions and the marks are forgotten and the mode
is read again, as the cards and the map decide it: the copies of one map never meet the next map's frames, and a map that is not one for the snapshot has the game read
its own state, with no mark kept by the accessors. */
void tick_snapshot_decide(void)
{
	const int before = s.mode;

	tick_snapshot_reset();
	s.mode = -1;
	tick_snapshot_mode();
	if (before >= 0 && before != s.mode && !s.mode)
		platform_log("tick snapshot: off for this map");
}

long tick_snapshot_none(void)
{
	return NO_TICK;
}

int tick_snapshot_viewing(void)
{
	return tick_snapshot_headers != NULL;
}

int tick_snapshot_pair_viewing(void)
{
	return tick_snapshot_headers != NULL && s.pair_view;
}

int tick_snapshot_object_present(long object_index)
{
	struct object_header_datum const *header;

	if (!tick_snapshot_headers || !s.pair_view || !s.view)
		return 1;
	header = (struct object_header_datum const *)datum_try_and_get(&s.view->array, object_index);
	return header != NULL && header->datum != NULL;
}

/* the slot of a tick, or NULL */
static struct slot *tick_snapshot_slot_of(long tick)
{
	const int index = tick_ring_find(&s.ring, tick);

	return index >= 0 ? &slots[index] : NULL;
}

/* ---------- the copies */

static void tick_snapshot_log(void)
{
	const unsigned long captures = s.window_captures ? s.window_captures : 1;

	tick_snapshot_say("tick snapshot (%lu captures, %lu frames): per capture %lu objects, %lu bytes in all (%lu in %lu object copies, headers %lu, "
		"cluster lists %lu); rotations found per capture: %lu snapshots, %lu nodes; "
		"in the window: %lu misses, %lu catch-ups, %lu fallbacks, %lu reads, %lu camera copies, %lu ring full",
		s.window_captures, s.views, s.objects_copied / captures,
		(s.bytes_copied + s.header_bytes + s.partition_bytes) / captures, s.bytes_copied / captures, s.block_calls / captures,
		s.header_bytes / captures, s.partition_bytes / captures, rotation_snapshots / captures, rotation_nodes / captures, s.misses,
		s.catch_ups, s.fallbacks, s.reads, s.camera_copies, s.ring_full);
	rotation_snapshots = rotation_nodes = 0;
	s.window_captures = 0;
	s.objects_copied = s.bytes_copied = s.header_bytes = s.block_calls = s.partition_bytes = s.light_bytes = 0;
	s.misses = s.catch_ups = s.fallbacks = s.reads = s.views = s.camera_copies = s.ring_full = 0;
}

/* what an object's block holds, by its own references: the object structure, the type's own data, and the three blocks that follow (node matrices and the two orientation
blocks, which only the ticks' animation reads: object_compute_node_matrices, object_start_interpolation, the preprocess functions). 0 sizes for a reference that is not valid. */
struct block_parts
{
	long fixed, matrices, orientations, type;
	long skip_offset[2], skip_size[2]; /* the orientation blocks, in order of offset (skips of them: 0, 1 or 2) */
	int skips;
};

static boolean block_reference_valid(struct object_header_block_reference const *reference, long size)
{
	return reference->size > 0 && reference->offset >= (short)sizeof(struct object_datum) && (long)reference->offset + reference->size <= size;
}

static void tick_snapshot_block_parts(struct object_datum const *block, long size, struct block_parts *parts)
{
	struct object_header_block_reference const *matrices = &block->object.node_matrices;
	struct object_header_block_reference const *current = &block->object.node_orientations;
	struct object_header_block_reference const *original = &block->object.original_node_orientations;
	const boolean current_valid = block_reference_valid(current, size), original_valid = block_reference_valid(original, size);

	memset(parts, 0, sizeof(*parts));
	parts->fixed = size < (long)sizeof(struct object_datum) ? size : (long)sizeof(struct object_datum);
	if (block_reference_valid(matrices, size))
		parts->matrices = matrices->size;
	if (current_valid)
		parts->orientations += current->size;
	if (original_valid)
		parts->orientations += original->size;
	parts->type = size - parts->fixed - parts->matrices - parts->orientations;
	if (current_valid && original_valid)
	{
		struct object_header_block_reference const *first = current->offset < original->offset ? current : original;
		struct object_header_block_reference const *second = first == current ? original : current;

		/* (two that overlap are not two blocks: none is skipped) */
		if ((long)first->offset + first->size <= second->offset)
		{
			parts->skip_offset[0] = first->offset;
			parts->skip_size[0] = first->size;
			parts->skip_offset[1] = second->offset;
			parts->skip_size[1] = second->size;
			parts->skips = 2;
		}
	}
	else if (current_valid || original_valid)
	{
		struct object_header_block_reference const *one = current_valid ? current : original;

		parts->skip_offset[0] = one->offset;
		parts->skip_size[0] = one->size;
		parts->skips = 1;
	}
}

/* whether a word of a copy is in a range that is not copied */
static boolean tick_snapshot_word_skipped(struct block_parts const *parts, long offset)
{
	int skip;

	for (skip = 0; skip < parts->skips; skip++)
	{
		if (offset >= parts->skip_offset[skip] && offset < parts->skip_offset[skip] + parts->skip_size[skip])
			return TRUE;
	}
	return FALSE;
}

/* a block copied in the parts the renderer can read: with skip, the orientation blocks (only the ticks' animation reads them) are left out */
static void tick_snapshot_copy_parts(byte *destination, byte const *source, long size, struct block_parts const *parts, boolean skip)
{
	long at = 0;
	int which;

	if (!skip || !parts->skips)
	{
		memcpy(destination, source, (size_t)size);
		return;
	}
	for (which = 0; which < parts->skips; which++)
	{
		if (parts->skip_offset[which] > at)
			memcpy(destination + at, source + at, (size_t)(parts->skip_offset[which] - at));
		at = parts->skip_offset[which] + parts->skip_size[which];
	}
	if (at < size)
		memcpy(destination + at, source + at, (size_t)(size - at));
}

/* the cache lines of a range asked for ahead (dcbt on the Espresso, which has no prefetching of its own) */
static void tick_snapshot_prefetch(void const *address, long size)
{
	byte const *line = (byte const *)((uintptr_t)address & ~(uintptr_t)31);
	byte const *end = (byte const *)address + size;

	for (; line < end; line += 32)
	{
#if defined(__powerpc__)
		/* (the compiler's own prefetch is nothing for this target: the port asks for lines with the instruction, as wiiu_indices.c does) */
		__asm__ volatile("dcbt 0,%0" : : "r"(line));
#else
		__builtin_prefetch(line);
#endif
	}
}

/* the cache lines of a range written back to memory (dcbst: the lines stay in the cache, clean). The tick thread's copies are read by the game thread on another core, which finds a
line another core holds modified (its L2 on core 2, 512 KB) slower than one in memory; the tick thread has the time. */
static void tick_snapshot_flush(void const *address, long size)
{
#if defined(__powerpc__)
	byte const *line = (byte const *)((uintptr_t)address & ~(uintptr_t)31);
	byte const *end = (byte const *)address + size;

	for (; line < end; line += 32)
		__asm__ volatile("dcbst 0,%0" : : "r"(line) : "memory");
#else
	(void)address;
	(void)size;
#endif
}

/* whether the orientation blocks are left out of the copies */
static boolean tick_snapshot_skipping(void)
{
	return s.skip_orientations;
}

/* the change measure (mode bit 64): the live block against the copy the last capture made, by the part of the block, without the two words the renderer's side owns */
static void tick_snapshot_measure_change(struct object_header_datum const *live, struct slot const *previous, long absolute_index, struct block_parts const *parts)
{
	struct object_header_datum const *before = &previous->headers[absolute_index];
	unsigned long const *now_words = (unsigned long const *)live->datum;
	unsigned long const *then_words;
	long words, word;
	boolean in_fixed = FALSE, in_type = FALSE, in_matrices = FALSE, in_orientations = FALSE;

	if (!previous->copied[absolute_index] || !before->datum || before->identifier != live->identifier || before->data_size != live->data_size)
		return;
	then_words = (unsigned long const *)before->datum;
	words = live->data_size / 4;
	cs.compared++;
	for (word = 0; word < words; word++)
	{
		const long offset = word * 4;

		if (offset == (long)offsetof(struct object_datum, object.magic_number) || offset == (long)offsetof(struct object_datum, object.cached_render_state_index))
			continue;
		/* (ranges not copied are not in the copy to compare with) */
		if (tick_snapshot_skipping() && tick_snapshot_word_skipped(parts, offset))
			continue;
		if (now_words[word] == then_words[word])
			continue;
		cs.changed_words++;
		if (offset < parts->fixed)
			in_fixed = TRUE;
		else if (offset >= live->datum->object.node_matrices.offset && offset < live->datum->object.node_matrices.offset + live->datum->object.node_matrices.size)
			in_matrices = TRUE;
		else if (tick_snapshot_word_skipped(parts, offset))
			in_orientations = TRUE;
		else
			in_type = TRUE;
	}
	if (in_fixed || in_type || in_matrices || in_orientations)
	{
		cs.changed++;
		cs.changed_fixed += in_fixed;
		cs.changed_type += in_type;
		cs.changed_matrices += in_matrices;
		cs.changed_orientations += in_orientations;
	}
}

/* an object's block copied from the game state's into the slot's (a capture, or an asking for one not copied) */
static struct slot const *measure_previous; /* (the capture before this one, when the change measure is on) */

static void tick_snapshot_copy_block(struct slot *slot, long absolute_index, struct object_header_datum const *live, boolean keep_fields, byte *destination)
{
	struct object_header_datum *header = &slot->headers[absolute_index];
	long size = live->data_size;
	long field;
	byte *block;
	struct block_parts parts;
	unsigned long long t0, t1, t2;
	long left_out = 0; /* (bytes of the block not copied) */

	if (size <= 0 || !live->datum)
		return;
	t0 = tick_overlap_now();
	if (destination)
	{
		/* (the buffer is a version's, which the caller made large enough) */
		block = destination;
	}
	else
	{
		if (slot->capacity[absolute_index] < size)
		{
			/* (the game's free takes no NULL) */
			if (slot->blocks[absolute_index])
				free(slot->blocks[absolute_index]);
			slot->blocks[absolute_index] = (byte *)malloc((size_t)size);
			slot->capacity[absolute_index] = slot->blocks[absolute_index] ? (unsigned short)size : 0;
			cs.grown++;
			if (!slot->blocks[absolute_index])
				return;
			cs.alloc += tick_overlap_now() - t0;
			t0 = tick_overlap_now();
		}
		block = slot->blocks[absolute_index];
	}
	tick_snapshot_block_parts(live->datum, size, &parts);
	if (measure_previous)
		tick_snapshot_measure_change(live, measure_previous, absolute_index, &parts);
	tick_snapshot_copy_parts(block, (byte const *)live->datum, size, &parts, tick_snapshot_skipping());
	if (tick_snapshot_skipping())
	{
		int which;

		for (which = 0; which < parts.skips; which++)
			left_out += parts.skip_size[which];
	}
	if ((s.mode & MODE_POISON_LEFT_OUT) && tick_snapshot_skipping())
	{
		int skip;

		for (skip = 0; skip < parts.skips; skip++)
			memset(block + parts.skip_offset[skip], 0xCD, (size_t)parts.skip_size[skip]);
	}
	t1 = tick_overlap_now();
	/* what the renderer wrote in the last copy of this object (not in the overlap: the game thread writes the kept fields, and puts them in
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
	t2 = tick_overlap_now();
	object_function_inputs_copy_to_snapshot((int)(slot - slots), absolute_index);
	cs.copy += t1 - t0;
	cs.kept += t2 - t1;
	cs.inputs += tick_overlap_now() - t2;
	{
		const int bucket = size_bucket(size);

		cs.bucket_ticks[bucket] += t1 - t0;
		cs.bucket_count[bucket]++;
		cs.bucket_bytes[bucket] += (unsigned long)size;
		cs.objects++;
		cs.bytes += (unsigned long)size - (unsigned long)left_out;
		cs.bytes_fixed += (unsigned long)parts.fixed;
		cs.bytes_type += (unsigned long)parts.type;
		cs.bytes_matrices += (unsigned long)parts.matrices;
		cs.bytes_orientations += (unsigned long)parts.orientations;
		cs.type_objects[live->type & 15]++;
		cs.type_bytes[live->type & 15] += (unsigned long)size - (unsigned long)left_out;
	}
	s.objects_copied++;
	s.bytes_copied += (unsigned long)size - (unsigned long)left_out;
	s.block_calls++;
}

static void tick_snapshot_check_skip(struct slot const *slot, long absolute_index, struct object_header_datum const *live);

/* ---------- the versions of an object's copy (the overlap's)

An object that does not change is the same object in every capture, yet each of the ring's four slots held a copy of its own. Now an object has up to VERSIONS buffers, each a copy of the
object as one capture saw it, and a slot refers to the buffer that is its capture's: four slots that saw an object as it was after the same tick refer to one. A capture of an object that
changed copies it once, into a buffer no slot refers to (or into the one only this slot refers to), and an object that did not change since the newest version was copied is that version: the
slot takes it, with no copy. A buffer is written only when no slot refers to it, or the slot being captured is the only one that does (the slots a frame is drawn from are not captured into);
the references are the tick thread's alone. What the frame writes into a copy (the marker, the cached render state, which it puts back from the kept fields before it reads) is the same
for every slot that shares it. */
#define VERSIONS 5

struct version
{
	byte *data;
	unsigned short capacity;
	short identifier; /* the object it is a copy of */
	short size;
	short refs; /* slots that refer to it */
	long serial; /* the capture it was copied in (0: none) */
};

static struct object_versions
{
	struct version v[VERSIONS];
	short latest; /* the version copied last, or -1 */
} versions[MAXIMUM_OBJECTS_PER_MAP];

/* whether the object's newest version is the object as it is: no mark since it was copied, the same object and size */
static boolean tick_snapshot_latest_current(struct object_versions const *vs, struct object_header_datum const *live, unsigned long dirty)
{
	struct version const *latest;

	if (vs->latest < 0 || !live->datum)
		return FALSE;
	latest = &vs->v[vs->latest];
	return latest->serial && latest->data && latest->identifier == live->identifier && latest->size == live->data_size && dirty <= (unsigned long)latest->serial;
}

/* Whether an object's block need not be copied into a slot, when the unchanged are left out (the overlap's): the slot refers to the newest version if it does not already, which is
current if no access has been marked since it was copied (tick_snapshot_dirty is the number of the first capture that sees one). */
static boolean tick_snapshot_block_unchanged(struct slot *slot, long absolute_index, struct object_header_datum const *live)
{
	const unsigned long dirty = tick_snapshot_dirty[absolute_index];
	struct object_versions *vs = &versions[absolute_index];
	const int current = (int)slot->ref[absolute_index] - 1;

	if (tick_snapshot_latest_current(vs, live, dirty))
	{
		struct version *latest = &vs->v[vs->latest];

		if (current != vs->latest)
		{
			if (current >= 0)
				vs->v[current].refs--;
			latest->refs++;
			slot->ref[absolute_index] = (signed char)(vs->latest + 1);
			/* (the function inputs of this slot are of the object as the slot last copied it) */
			object_function_inputs_copy_to_snapshot((int)(slot - slots), absolute_index);
			cs.shared++;
		}
		slot->headers[absolute_index].datum = (struct object_datum *)latest->data;
		slot->copied[absolute_index] = TRUE;
		slot->copy_serial[absolute_index] = latest->serial;
		cs.skipped++;
		cs.skipped_type[live->type & 15]++;
		cs.skipped_bytes += (unsigned long)live->data_size;
		if (dirty == ~0UL)
			cs.marked_for_good++;
		if (s.mode & MODE_CHECK_SKIPS)
			tick_snapshot_check_skip(slot, absolute_index, live);
		return TRUE;
	}
	cs.marked++;
	cs.marked_type[live->type & 15]++;
	if (dirty == ~0UL)
		cs.marked_for_good++;
	return FALSE;
}

/* the object copied: into the buffer only this slot refers to, or one no slot refers to (made if there is none with room), which becomes the newest version */
static void tick_snapshot_capture_block(struct slot *slot, long absolute_index, struct object_header_datum const *live)
{
	struct object_versions *vs = &versions[absolute_index];
	const int current = (int)slot->ref[absolute_index] - 1;
	const long size = live->data_size;
	int chosen = -1, index;
	struct version *version;
	unsigned long long started;

	if (size <= 0 || !live->datum)
		return;
	if (current >= 0 && vs->v[current].refs == 1)
	{
		chosen = current;
	}
	else
	{
		for (index = 0; index < VERSIONS; index++)
		{
			if (vs->v[index].refs == 0 && (chosen < 0 || (vs->v[index].capacity >= size && vs->v[chosen].capacity < size)))
				chosen = index;
		}
		if (chosen < 0)
			return; /* (not possible: four slots refer to at most four) */
	}
	version = &vs->v[chosen];
	if (version->capacity < size)
	{
		started = tick_overlap_now();
		if (version->data)
			free(version->data);
		version->data = (byte *)malloc((size_t)size);
		version->capacity = version->data ? (unsigned short)size : 0;
		cs.grown++;
		cs.alloc += tick_overlap_now() - started;
		if (!version->data)
		{
			version->serial = 0;
			return;
		}
	}
	if (chosen != current)
	{
		if (current >= 0)
			vs->v[current].refs--;
		version->refs++;
		slot->ref[absolute_index] = (signed char)(chosen + 1);
	}
	version->serial = 0; /* (not current while it is written) */
	tick_snapshot_copy_block(slot, absolute_index, live, FALSE, version->data);
	if (slot->copied[absolute_index])
	{
		version->identifier = live->identifier;
		version->size = (short)size;
		version->serial = slot->capture_serial;
		vs->latest = (short)chosen;
		slot->copy_serial[absolute_index] = version->serial;
		if (slot->changed_count < MAXIMUM_OBJECTS_PER_MAP)
			slot->changed[slot->changed_count++] = (unsigned short)absolute_index;
		if (s.flush)
		{
			struct block_parts parts;
			long at = 0;
			int which;

			tick_snapshot_block_parts((struct object_datum const *)version->data, size, &parts);
			for (which = 0; which < (s.skip_orientations ? parts.skips : 0); which++)
			{
				if (parts.skip_offset[which] > at)
					tick_snapshot_flush(version->data + at, parts.skip_offset[which] - at);
				at = parts.skip_offset[which] + parts.skip_size[which];
			}
			if (at < size)
				tick_snapshot_flush(version->data + at, size - at);
		}
	}
	else if (vs->latest == chosen)
	{
		vs->latest = -1;
	}
}

/* a slot lets go of the buffer it referred to for an object (the object is gone) */
static void tick_snapshot_release(struct slot *slot, long absolute_index)
{
	const int current = (int)slot->ref[absolute_index] - 1;

	if (current >= 0)
		versions[absolute_index].v[current].refs--;
	slot->ref[absolute_index] = 0;
	slot->copy_serial[absolute_index] = 0;
}

/* the host check (mode bit 128): an object left out because nothing accessed it, against the game state's block (without the words the renderer's side owns and what is not copied) */
static void tick_snapshot_check_skip(struct slot const *slot, long absolute_index, struct object_header_datum const *live)
{
	struct block_parts parts;
	unsigned long const *now_words = (unsigned long const *)live->datum;
	unsigned long const *then_words = (unsigned long const *)slot->headers[absolute_index].datum;
	const long words = live->data_size / 4;
	long word;

	tick_snapshot_block_parts(live->datum, live->data_size, &parts);
	for (word = 0; word < words; word++)
	{
		const long offset = word * 4;

		if (offset == (long)offsetof(struct object_datum, object.magic_number) || offset == (long)offsetof(struct object_datum, object.cached_render_state_index))
			continue;
		if (tick_snapshot_skipping() && tick_snapshot_word_skipped(&parts, offset))
			continue;
		if (now_words[word] != then_words[word])
		{
			cs.skipped_wrong++;
			if (cs.skipped_wrong <= 20)
			{
				tick_snapshot_say("tick snapshot CHECK: object %ld (type %d, %ld bytes) was left out of a capture but its game state differs at +%ld: copy %08lx, game state %08lx",
					absolute_index, (int)live->type, (long)live->data_size, offset, then_words[word], now_words[word]);
			}
			return;
		}
	}
}

/* ---------- the update scope

An object is updated every tick it is active, and almost always to what it was. While object_update runs for one (and the updates of its children and siblings, which it runs), what
the accessors give is not marked; its block is kept as it was when the update began, and compared when the update ends: marked if the block is not the same (or is another's, or is
another size). The ticks' one thread runs these, one at a time, nested only by the children and siblings. Objects too big for the keeping, or too deep to keep, are marked at once. */
#define SCOPE_DEPTH 24
#define SCOPE_BYTES 6144

static struct
{
	long absolute_index;
	short identifier;
	short size;
	boolean kept;
} scope_stack[SCOPE_DEPTH];
static byte scope_saved[SCOPE_DEPTH][SCOPE_BYTES];
static int scope_depth;
static long scope_overflow;

void tick_snapshot_scope_begin(long object_index)
{
	const long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	struct object_header_datum const *header;

	if (TICK_OVERLAP_ON_RENDER_STACK())
		return;
	if (scope_depth >= SCOPE_DEPTH)
	{
		scope_overflow++;
		if (absolute_index >= 0 && absolute_index < MAXIMUM_OBJECTS_PER_MAP)
			tick_snapshot_dirty[absolute_index] = tick_snapshot_dirty_next;
		return;
	}
	scope_stack[scope_depth].absolute_index = absolute_index;
	scope_stack[scope_depth].kept = FALSE;
	if (absolute_index < 0 || absolute_index >= MAXIMUM_OBJECTS_PER_MAP)
	{
		scope_depth++;
		return;
	}
	header = (struct object_header_datum const *)((byte const *)object_header_data->data + object_header_data->size * absolute_index);
	if (header->identifier && header->datum && header->data_size > 0 && header->data_size <= SCOPE_BYTES && !tick_snapshot_in_scope[absolute_index])
	{
		memcpy(scope_saved[scope_depth], header->datum, (size_t)header->data_size);
		scope_stack[scope_depth].identifier = header->identifier;
		scope_stack[scope_depth].size = header->data_size;
		scope_stack[scope_depth].kept = TRUE;
		tick_snapshot_in_scope[absolute_index] = 1;
	}
	else
	{
		tick_snapshot_dirty[absolute_index] = tick_snapshot_dirty_next;
	}
	scope_depth++;
}

void tick_snapshot_scope_end(long object_index)
{
	struct object_header_datum const *header;
	long absolute_index, word, words;
	boolean changed = FALSE;
	struct block_parts parts;

	if (TICK_OVERLAP_ON_RENDER_STACK())
		return;
	if (scope_overflow)
	{
		scope_overflow--;
		return;
	}
	if (scope_depth <= 0)
		return;
	scope_depth--;
	absolute_index = scope_stack[scope_depth].absolute_index;
	if (!scope_stack[scope_depth].kept)
		return;
	(void)object_index;
	tick_snapshot_in_scope[absolute_index] = 0;
	header = (struct object_header_datum const *)((byte const *)object_header_data->data + object_header_data->size * absolute_index);
	if (!header->identifier || header->identifier != scope_stack[scope_depth].identifier || header->data_size != scope_stack[scope_depth].size || !header->datum)
	{
		changed = TRUE;
	}
	else
	{
		unsigned long const *now_words = (unsigned long const *)header->datum;
		unsigned long const *then_words = (unsigned long const *)scope_saved[scope_depth];
		const long marker = (long)offsetof(struct object_datum, object.magic_number);
		const long cached = (long)offsetof(struct object_datum, object.cached_render_state_index);

		tick_snapshot_block_parts(header->datum, header->data_size, &parts);
		words = header->data_size / 4;
		for (word = 0; word < words; word++)
		{
			const long offset = word * 4;

			if (now_words[word] == then_words[word] || offset == marker || offset == cached)
				continue;
			/* (the orientation blocks are not in the copies, whatever is done to them) */
			if (tick_snapshot_skipping() && tick_snapshot_word_skipped(&parts, offset))
				continue;
			changed = TRUE;
			break;
		}
	}
	if (changed)
		tick_snapshot_dirty[absolute_index] = tick_snapshot_dirty_next;
}

/* the objects a capture copies (one thread makes a capture at a time) and the bytes of a block asked for ahead */
static long copy_list[MAXIMUM_OBJECTS_PER_MAP];
#define PREFETCH_BYTES 1024

/* ---------- the capture's numbers and the copy bench */

static double ticks_ms(unsigned long long ticks, unsigned long divisor)
{
	return (double)ticks / ((double)wiiu_ticks_per_second() / 1000.0) / (double)(divisor ? divisor : 1);
}

/* what the copies cost by part, said every LOG_CAPTURES captures (a log line holds 199 characters) */
static void tick_snapshot_capture_log(void)
{
	static const char *const types[12] = { "biped", "vehicle", "weapon", "equipment", "garbage", "projectile", "scenery", "machine", "control",
		"lightfix", "placeholder", "soundscn" };
	const unsigned long n = cs.captures ? cs.captures : 1;
	char line[256];
	size_t used = 0;
	int bucket, type;
	static const char *const bucket_names[SIZE_BUCKETS] = { "<128", "<256", "<512", "<1K", "<2K", ">=2K" };

	tick_snapshot_say("tick snapshot capture, ms a capture (%lu): total %.3f = headers %.3f, blocks %.3f (copy %.3f, function inputs %.3f, kept fields %.3f, buffers made %.3f, the rest %.3f), "
		"cluster lists %.3f, lights %.3f, cameras %.3f, ring %.3f",
		cs.captures, ticks_ms(cs.total, n), ticks_ms(cs.headers, n), ticks_ms(cs.blocks, n), ticks_ms(cs.copy, n), ticks_ms(cs.inputs, n), ticks_ms(cs.kept, n),
		ticks_ms(cs.alloc, n), ticks_ms(cs.blocks - cs.copy - cs.inputs - cs.kept - cs.alloc, n), ticks_ms(cs.partitions, n), ticks_ms(cs.lights, n),
		ticks_ms(cs.cameras, n), ticks_ms(cs.ring, n));
	tick_snapshot_say("tick snapshot capture, a capture: %lu objects, %lu bytes in them (the structure %lu, the type's data %lu, node matrices %lu, orientation blocks %lu%s), "
		"%lu buffers made, blocks in memory order %lu, not %lu; misaligned accesses taken on this core %lu a capture (%lu in all)",
		cs.objects / n, cs.bytes / n, cs.bytes_fixed / n, cs.bytes_type / n, cs.bytes_matrices / n, cs.bytes_orientations / n, s.skip_orientations ? " not copied" : "", cs.grown, cs.sequential / n, cs.jumps / n,
		cs.fixups / n, cs.fixups);
	for (type = 0; type < 12; type++)
	{
		if (cs.type_objects[type] && used < sizeof(line) - 40)
			used += (size_t)snprintf(line + used, sizeof(line) - used, "%s%s %lu/%lu", used ? ", " : "", types[type], cs.type_objects[type] / n, cs.type_bytes[type] / n);
	}
	tick_snapshot_say("tick snapshot capture, a capture, objects/bytes by type: %s", line);
	used = 0;
	line[0] = 0;
	for (bucket = 0; bucket < SIZE_BUCKETS; bucket++)
	{
		if (cs.bucket_count[bucket] && used < sizeof(line) - 40)
		{
			used += (size_t)snprintf(line + used, sizeof(line) - used, "%s%s: %lu a capture, %.2f us each (%lu bytes)", used ? "; " : "", bucket_names[bucket],
				cs.bucket_count[bucket] / n, ticks_ms(cs.bucket_ticks[bucket], cs.bucket_count[bucket]) * 1000.0, cs.bucket_bytes[bucket] / cs.bucket_count[bucket]);
		}
	}
	tick_snapshot_say("tick snapshot capture, the copies by size of block: %s", line);
	if (tick_perf_on() && cs.perf.stretches)
	{
		const double per = (double)cs.perf.stretches;

		tick_snapshot_say("tick snapshot capture, counters of core %u, a capture: %s %.2f M, %s %.2f M, %s %.1f K, %s %.1f K", cs.perf_core, wiiu_perfmon_name(0), (double)cs.perf.sum[0] / per / 1e6,
			wiiu_perfmon_name(1), (double)cs.perf.sum[1] / per / 1e6, wiiu_perfmon_name(2), (double)cs.perf.sum[2] / per / 1e3, wiiu_perfmon_name(3), (double)cs.perf.sum[3] / per / 1e3);
	}
	if (s.skip_unchanged)
	{
		tick_snapshot_say("tick snapshot capture, a capture: %lu objects changed and copied, %lu left out (%lu bytes not copied, %lu of them taken from the newest copy); %lu marked for good by the drawing thread",
			cs.marked / n, cs.skipped / n, cs.skipped_bytes / n, cs.shared / n, cs.marked_for_good / n);
		{
			char marked_line[240];
			size_t marked_used = 0;

			for (type = 0; type < 12; type++)
			{
				if ((cs.marked_type[type] || cs.skipped_type[type]) && marked_used < sizeof(marked_line) - 40)
				{
					marked_used += (size_t)snprintf(marked_line + marked_used, sizeof(marked_line) - marked_used, "%s%s %lu copied/%lu not", marked_used ? ", " : "", types[type],
						cs.marked_type[type] / n, cs.skipped_type[type] / n);
				}
			}
			marked_line[marked_used < sizeof(marked_line) ? marked_used : sizeof(marked_line) - 1] = 0;
			tick_snapshot_say("tick snapshot capture, a capture, by type: %s", marked_line);
		}
		if (s.mode & MODE_CHECK_SKIPS)
			platform_log("tick snapshot capture, check of the objects left out: %lu of %lu were not the same as the game state (0 is the answer)", cs.skipped_wrong, cs.skipped);
	}
	if (s.mode & MODE_MEASURE)
	{
		tick_snapshot_say("tick snapshot capture, changed since the capture before (%lu blocks compared): %lu changed (%lu in the structure, %lu in the type's data, %lu in the node matrices, "
			"%lu in the orientations), %lu words in all", cs.compared, cs.changed, cs.changed_fixed, cs.changed_type, cs.changed_matrices, cs.changed_orientations, cs.changed_words);
	}
	if (s.mode & MODE_BENCH)
		tick_snapshot_bench();
	memset(&cs, 0, sizeof(cs));
}

/* The copy bench (mode bit 32), once in each log window, on the tick thread after a capture, with the ticks quiet: the same objects copied into scratch buffers in other ways, each after the
caches were swept with a read of 1.5 MB, so that every way starts with its source cold as the capture's was. It says which way is the fastest on this console, with the same data. */
static void tick_snapshot_bench(void)
{
	static byte *scratch[4];
	static long scratch_capacity;
	static byte *sweep;
	static long list[MAXIMUM_OBJECTS_PER_MAP];
	static volatile unsigned long sink;
	static unsigned long window;
	struct data_array const *live = object_header_data;
	struct object_header_datum const *headers;
	void *pool_base;
	long pool_used, count = 0, index, total = 0, aligned_total = 0, skipped = 0, variant;
	double ms[6];
	static const char *const names[6] = { "as now", "prefetching the next block", "without the orientation blocks", "both", "the pool in one copy", "as now again at once" };

	if (!live || !live->valid)
		return;
	headers = (struct object_header_datum const *)live->data;
	object_memory_pool_extent(&pool_base, &pool_used);
	for (index = 0; index < live->count && index < MAXIMUM_OBJECTS_PER_MAP; index++)
	{
		struct block_parts parts;

		if (!headers[index].identifier || !headers[index].datum || headers[index].data_size <= 0)
			continue;
		tick_snapshot_block_parts(headers[index].datum, headers[index].data_size, &parts);
		list[count++] = index;
		total += headers[index].data_size;
		aligned_total += (headers[index].data_size + 31) & ~31L;
		skipped += parts.orientations;
	}
	if (!count)
		return;
	if (scratch_capacity < aligned_total + 64 || scratch_capacity < pool_used + 64)
	{
		const long capacity = (aligned_total > pool_used ? aligned_total : pool_used) + 64;

		for (variant = 0; variant < 4; variant++)
		{
			if (scratch[variant])
				free(scratch[variant]);
			scratch[variant] = (byte *)malloc((size_t)capacity);
			if (!scratch[variant])
			{
				scratch_capacity = 0;
				return;
			}
			memset(scratch[variant], 0, (size_t)capacity);
		}
		scratch_capacity = capacity;
	}
	if (!sweep)
	{
		sweep = (byte *)malloc(1536 * 1024);
		if (!sweep)
			return;
		memset(sweep, 1, 1536 * 1024);
	}
	for (variant = 0; variant < 6; variant++)
	{
		byte *destination = scratch[(window + (unsigned long)variant) & 3];
		unsigned long long started;
		long at;

		if (variant != 5)
		{
			unsigned long sum = 0;
			unsigned long const *words = (unsigned long const *)sweep;

			for (at = 0; at < 1536 * 1024 / 4; at += 8)
				sum += words[at];
			sink += sum;
		}
		started = tick_overlap_now();
		if (variant == 4)
		{
			if (pool_base && pool_used > 0)
				memcpy(destination, pool_base, (size_t)pool_used);
		}
		else
		{
			const boolean prefetch = variant == 1 || variant == 3, skip = variant == 2 || variant == 3;

			for (at = 0, index = 0; index < count; index++)
			{
				struct object_header_datum const *header = &headers[list[index]];
				struct block_parts parts;

				if (prefetch && index + 1 < count)
				{
					struct object_header_datum const *next = &headers[list[index + 1]];

					tick_snapshot_prefetch(next->datum, next->data_size > 1024 ? 1024 : next->data_size);
				}
				tick_snapshot_block_parts(header->datum, header->data_size, &parts);
				tick_snapshot_copy_parts(destination + at, (byte const *)header->datum, header->data_size, &parts, skip);
				at += (header->data_size + 31) & ~31L;
			}
		}
		ms[variant] = ticks_ms(tick_overlap_now() - started, 1);
	}
	window++;
	tick_snapshot_say("tick snapshot bench, ms for %ld blocks, %ld bytes (orientation blocks %ld, pool %ld), sources cold: %s %.3f (%.0f MB/s), %s %.3f, %s %.3f, %s %.3f, %s %.3f, %s %.3f",
		count, total, skipped, pool_used, names[0], ms[0], (double)total / ms[0] / 1000.0, names[1], ms[1], names[2], ms[2], names[3], ms[3], names[4], ms[4], names[5], ms[5]);
}

/* the object partitions' lists (the clusters' first references and the references in use), as the ticks left them */
static void tick_snapshot_capture_partitions(struct slot *slot)
{
	long partition_index;

	slot->partitions[0].live = &collideable_object_cluster_partition;
	slot->partitions[1].live = &noncollideable_object_cluster_partition;
	slot->partitions[2].live = &light_cluster_partition;
	for (partition_index = 0; partition_index < PARTITION_COUNT; partition_index++)
	{
		struct partition_copy *copy = &slot->partitions[partition_index];
		struct cluster_partition const *live = copy->live;
		struct data_array const *references = live->data_reference_data;

		copy->valid = FALSE;
		/* (the lights' partition is the overlap's: stage 3's snapshot, on its own, leaves the lights to the game state) */
		if (partition_index == 2 && !s.overlap_requested)
			continue;
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
		s.partition_bytes += sizeof(copy->first_references) + (unsigned long)copy->references.count * sizeof(copy->entries[0]);
		if (live->cluster_reference_data && live->cluster_reference_data->valid)
		{
			copy->cluster_references = *live->cluster_reference_data;
			if (copy->cluster_references.count > HALO_PORT_MAXIMUM_CLUSTER_REFERENCES)
				copy->cluster_references.count = HALO_PORT_MAXIMUM_CLUSTER_REFERENCES;
			memcpy(copy->cluster_entries, live->cluster_reference_data->data,
				(size_t)copy->cluster_references.count * sizeof(copy->cluster_entries[0]));
			copy->cluster_references.data = copy->cluster_entries;
			copy->view.cluster_reference_data = &copy->cluster_references;
			s.partition_bytes += (unsigned long)copy->cluster_references.count * sizeof(copy->cluster_entries[0]);
		}
		copy->valid = TRUE;
	}
}

/* the light array, header and the used part of its data (the lights themselves are small: the game's light datum is 0x7C bytes, about a hundred of them at once) */
static void tick_snapshot_capture_lights(struct slot *slot)
{
	long needed;

	slot->lights_valid = FALSE;
	if (!s.overlap_requested || !light_data || !light_data->valid)
		return;
	needed = (long)light_data->count * (long)light_data->size;
	/* (never no buffer: an array of no lights, the main menu's scene, still has its data pointer, and data_verify halts the game on one that has none) */
	if (slot->light_bytes_capacity < needed || !slot->light_bytes)
	{
		const long capacity = needed > light_data->size ? needed : light_data->size;
		byte *grown = slot->light_bytes ? (byte *)realloc(slot->light_bytes, (size_t)capacity) : (byte *)malloc((size_t)capacity);

		if (!grown)
			return;
		slot->light_bytes = grown;
		slot->light_bytes_capacity = capacity;
	}
	slot->lights = *light_data;
	if (needed > 0)
		memcpy(slot->light_bytes, light_data->data, (size_t)needed);
	slot->lights.data = slot->light_bytes;
	slot->lights_valid = TRUE;
	s.light_bytes += (unsigned long)needed;
}

/* the lights a frame reads: the view's copy (the game state's if the capture has none) */
/* The lights a frame reads are the copy of the ring's only in a frame drawn from a pinned pair (the overlap's: the tick thread changes the lights meanwhile). A serial frame, drawn with the tick
thread idle and the lights as the last tick left them, reads and CHANGES the game state's own: lights_preprocess_scene lets go of the lights that have faded (datum_delete on the game state's
array) and moves the ones that follow an object (light_disconnect_from_map and light_reconnect_to_map, which write a light's place in its clusters), and a frame drawn from the capture of the
last tick would do each again from the same capture in the next frame (the frames come faster than the ticks, and the capture is of the tick): the game halted on "lights index ... is unused or
changed" in the console's run of 2026-11-03 (G2, 4.3 s into the serial frames of a save), and the light that followed its object was moved in the copy and not in the game state. */
struct data_array *tick_snapshot_light_array(void)
{
	struct slot *view = s.view;

	if (view && view->lights_valid && s.pair_view)
		return &view->lights;
	return light_data;
}

struct cluster_partition const *tick_snapshot_cluster_partition(struct cluster_partition const *live)
{
	long partition_index;
	struct slot *view = s.view;

	if (!tick_snapshot_headers || !view)
		return live;
	for (partition_index = 0; partition_index < PARTITION_COUNT; partition_index++)
	{
		/* (the lights' partition, as the lights themselves, only in a frame drawn from a pinned pair: tick_snapshot_light_array) */
		if (view->partitions[partition_index].live == live && view->partitions[partition_index].valid && (partition_index != 2 || s.pair_view))
			return &view->partitions[partition_index].view;
	}
	return live;
}

/* the slot a capture of tick is written to (tick_ring.h's rules). NULL if there is none */
static struct slot *tick_snapshot_slot_for(long tick)
{
	const int index = tick_ring_slot_for(&s.ring, tick);

	return index >= 0 ? &slots[index] : NULL;
}

void tick_snapshot_capture_tick(long tick, boolean commit)
{
	struct data_array const *live = object_header_data;
	struct object_header_datum const *live_headers;
	struct slot *slot;
	long count, previous_count, index;
	unsigned long long started, headers_started, started_ticks, mark, mark2;
	unsigned long fixups_before;
	byte const *last_end = NULL;
	long copy, copy_count;
	unsigned long perf_before[4];

	if (!tick_snapshot_mode() || !live || !live->valid)
		return;
	started = CLOCK();
	started_ticks = tick_overlap_now();
	fixups_before = wiiu_crash_fixups_here();
	tick_perf_begin(perf_before);
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
	/* (the change measure compares with the capture before this one) */
	measure_previous = NULL;
	if ((s.mode & MODE_MEASURE) && s.ring.captured != NO_TICK)
	{
		struct slot const *before = tick_snapshot_slot_of(s.ring.captured);

		if (before && before != slot)
			measure_previous = before;
	}
	tick_ring_begin_write(&s.ring, (int)(slot - slots), tick);
	slot->changed_count = 0;
	s.serial++;
	slot->capture_serial = s.serial;
	tick_snapshot_dirty_next = (unsigned long)s.serial + 1;
	headers_started = CLOCK();
	mark = tick_overlap_now();
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
	mark2 = tick_overlap_now();
	cs.headers += mark2 - mark;
	mark = mark2;
	/* the blocks of the objects drawn lately (the overlap: all of them, a tick being free to change what a lazy copy would read), but those
	nothing has accessed since the slot's copy; first the list of those to copy, then the copies, each asking for the cache lines of the next */
	copy_count = 0;
	for (index = 0; index < count; index++)
	{
		slot->copied[index] = FALSE;
		slot->headers[index].datum = NULL;
		if (!slot->headers[index].identifier)
		{
			if (s.skip_unchanged && slot->ref[index])
				tick_snapshot_release(slot, index);
			continue;
		}
		if (s.all || (s.wanted[index] && s.serial - s.wanted[index] <= DRAWN_CAPTURES))
		{
			if (s.skip_unchanged && tick_snapshot_block_unchanged(slot, index, &live_headers[index]))
				continue;
			copy_list[copy_count] = index;
			copy_count++;
		}
	}
	for (copy = 0; copy < copy_count; copy++)
	{
		struct object_header_datum const *from = &live_headers[copy_list[copy]];

		if (s.prefetch && copy + 1 < copy_count)
		{
			struct object_header_datum const *next = &live_headers[copy_list[copy + 1]];

			if (next->datum)
				tick_snapshot_prefetch(next->datum, next->data_size > PREFETCH_BYTES ? PREFETCH_BYTES : next->data_size);
		}
		if (from->datum && from->data_size > 0)
		{
			/* (is the next copied block the next in memory? the blocks lie in the pool in the order they were made, compacted) */
			if (last_end)
			{
				if ((byte const *)from->datum == last_end || (byte const *)from->datum == last_end + 24)
					cs.sequential++;
				else
					cs.jumps++;
			}
			last_end = (byte const *)from->datum + from->data_size;
		}
		if (s.skip_unchanged)
			tick_snapshot_capture_block(slot, copy_list[copy], from);
		else
			tick_snapshot_copy_block(slot, copy_list[copy], from, !s.overlap_requested, NULL);
	}
	for (; index < previous_count && index < MAXIMUM_OBJECTS_PER_MAP; index++)
	{
		slot->copied[index] = FALSE;
		if (s.skip_unchanged && slot->ref[index])
			tick_snapshot_release(slot, index);
	}
	mark2 = tick_overlap_now();
	cs.blocks += mark2 - mark;
	mark = mark2;
	{
		const unsigned long before_first = s.partition_bytes;

		tick_snapshot_capture_partitions(slot);
		cs.partition_bytes_first += s.partition_bytes - before_first;
	}
	mark2 = tick_overlap_now();
	cs.partitions += mark2 - mark;
	mark = mark2;
	tick_snapshot_capture_lights(slot);
	mark2 = tick_overlap_now();
	cs.lights += mark2 - mark;
	mark = mark2;
	memset(slot->cameras, 0, sizeof(slot->cameras));
	ADD(PART_BLOCKS, headers_started);
	mark2 = tick_overlap_now();
	cs.cameras += mark2 - mark;
	mark = mark2;
	tick_ring_end_write(&s.ring, (int)(slot - slots), commit);
	s.dirty = FALSE;
	s.captures++;
	s.window_captures++;
	ADD(PART_CAPTURE, started);
	mark2 = tick_overlap_now();
	cs.ring += mark2 - mark;
	cs.total += mark2 - started_ticks;
	cs.captures++;
	cs.fixups += wiiu_crash_fixups_here() - fixups_before;
	tick_perf_end(&cs.perf, perf_before);
	cs.perf_core = tick_perf_on() ? wiiu_perfmon_core() : 0;
	measure_previous = NULL;
	tick_overlap_note_capture(mark2 - started_ticks);
	if (s.window_captures >= LOG_CAPTURES)
	{
		tick_snapshot_log();
		tick_snapshot_capture_log();
	}
}

/* the serial mode's catch-up (a view begun with no capture of this map's objects, or after objects were made or deleted): the world as it is, with
the ticks finished */
void tick_snapshot_capture(void)
{
	tick_snapshot_capture_tick(game_time_initialized() ? game_time_get() : 0, TRUE);
}

void tick_snapshot_commit(void)
{
	tick_ring_commit(&s.ring);
}

long tick_snapshot_published_tick(void)
{
	return tick_ring_published(&s.ring);
}

void tick_snapshot_reset(void)
{
	long index;
	int slot_index;

	for (slot_index = 0; slot_index < TICK_SNAPSHOT_SLOTS; slot_index++)
	{
		struct slot *slot = &slots[slot_index];

		memset(&slot->array, 0, sizeof(slot->array));
		for (index = 0; index < MAXIMUM_OBJECTS_PER_MAP; index++)
		{
			slot->headers[index].identifier = 0;
			slot->headers[index].datum = NULL;
			slot->copied[index] = FALSE;
			slot->ref[index] = 0;
			slot->copy_serial[index] = 0;
		}
		memset(slot->cameras, 0, sizeof(slot->cameras));
		for (index = 0; index < PARTITION_COUNT; index++)
			slot->partitions[index].valid = FALSE;
		slot->lights_valid = FALSE;
	}
	for (index = 0; index < MAXIMUM_OBJECTS_PER_MAP; index++)
	{
		int version;

		for (version = 0; version < VERSIONS; version++)
		{
			versions[index].v[version].refs = 0;
			versions[index].v[version].serial = 0;
			versions[index].v[version].identifier = 0;
			versions[index].v[version].size = 0;
		}
		versions[index].latest = -1;
	}
	for (index = 0; index < MAXIMUM_OBJECTS_PER_MAP; index++)
	{
		s.wanted[index] = 0;
		s.field_identifier[index] = 0;
		s.applied[index] = 0;
	}
	tick_ring_reset(&s.ring, TICK_SNAPSHOT_SLOTS);
	if (tick_snapshot_dirty_tracking)
	{
		/* (the ticks are idle: no update is open, no read-only code is running) */
		memset((void *)tick_snapshot_dirty, 0, sizeof(tick_snapshot_dirty));
		memset((void *)tick_snapshot_in_scope, 0, sizeof(tick_snapshot_in_scope));
		scope_depth = 0;
		scope_overflow = 0;
		tick_snapshot_read_only = 0;
		tick_snapshot_render_read_only = 0;
	}
	s.dirty = FALSE;
	s.view = NULL;
	s.previous = NULL;
	s.pair_view = FALSE;
	tick_snapshot_view_array = NULL;
	tick_snapshot_view_any = 0;
	tick_snapshot_lights_in_view = 0;
}

void tick_snapshot_reads_total(unsigned long *reads, unsigned long *fallbacks, unsigned long *applied)
{
	*reads = s.reads_total;
	*fallbacks = s.fallbacks_total;
	*applied = s.applied_total;
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
	tick_snapshot_copy_block(s.view, absolute_index, live, TRUE, NULL);
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
	s.applied_total++;
	if (s.applied_count < MAXIMUM_OBJECTS_PER_MAP)
		s.applied_list[s.applied_count++] = absolute_index;
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
	s.reads_total++;
	if (!header)
	{
		/* (an object the snapshot has not: made since it, and the view's start would have caught it) */
		s.fallbacks++;
		s.fallbacks_total++;
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
	s.reads_total++;
	if (!header)
	{
		struct object_header_datum *live;

		/* (a frame drawn from a pinned pair: an object the pair does not hold was made since, and does not exist for this frame; its block, its clusters and its lights are of another
		tick than the partitions and the lights the frame reads, and the game state's own is being changed beside the drawing) */
		if (s.pair_view)
		{
			/* (NONE is no object that the pair lacks: a caller asking for an object it has not, and the log's one line is for a real one) */
			if (object_index != NONE && !s.pair_absent++)
				platform_log("tick snapshot: an object (%08lx) that the frame's pair of the ring does not hold was asked for by name; it does not exist for this frame (said once)", (unsigned long)object_index);
			return NULL;
		}
		live = (struct object_header_datum *)datum_try_and_get(object_header_data, object_index);
		if (live)
		{
			s.fallbacks++;
			s.fallbacks_total++;
		}
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

/* the blends' keys for what they keep (render_interpolation.c): the capture numbers of the view's pose and of the pose before it (0: none), and whether to keep */
/* the number of the capture an object's pose in the view's slot (or the one before) was copied in: the same number is the same pose (0: none) */
long tick_snapshot_pose_serial(long absolute_index, boolean previous)
{
	struct slot const *slot = previous ? s.previous : s.view;

	if (!slot || absolute_index < 0 || absolute_index >= MAXIMUM_OBJECTS_PER_MAP)
		return 0;
	return slot->copy_serial[absolute_index] ? slot->copy_serial[absolute_index] : slot->capture_serial;
}

int tick_snapshot_rotation_cache(void)
{
	tick_snapshot_mode();
	return s.rotation_cache;
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
	const unsigned long long started_ticks = tick_overlap_now();

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
	tick_overlap_note_publish(tick_overlap_now() - started_ticks);
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

static void tick_snapshot_hash_log(void);

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
		struct block_parts parts;

		if (!view->copied[index] || live->identifier != view->headers[index].identifier || !live->datum)
			continue;
		if (live->data_size != view->headers[index].data_size)
		{
			s.verify_size_differences++;
			continue;
		}
		words = live->data_size / 4;
		tick_snapshot_block_parts(live->datum, live->data_size, &parts);
		for (word = 0; word < words; word++)
		{
			/* (the orientation blocks are not copied) */
			if (tick_snapshot_skipping() && tick_snapshot_word_skipped(&parts, word * 4))
				continue;
			if (((unsigned long const *)live->datum)[word] != ((unsigned long const *)view->headers[index].datum)[word])
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
	return tick_ring_pin(&s.ring, tick) != 0;
}

void tick_snapshot_unpin(void)
{
	tick_ring_unpin(&s.ring);
}

/* the game thread's, what a frame's begin and end cost (and the cache lines asked for): said by tick_snapshot_view_log with the blends */
static struct
{
	unsigned long frames, lines, objects;
	unsigned long long begin, end, prefetch;
	struct tick_perf perf_begin, perf_end;
} vs_stats;

/* the cache lines of the objects the newest capture copied afresh, asked for when a frame begins: the tick thread (another core) wrote them, so they are in no cache of this core, and the frame
reads them in the order it draws, one miss at a time; asked for together, the misses overlap. The orientation blocks, which are not copied, are left out. */
static void tick_snapshot_view_prefetch(struct slot const *slot)
{
	long entry;

	for (entry = 0; entry < slot->changed_count; entry++)
	{
		const long index = slot->changed[entry];
		struct object_header_datum const *header = &slot->headers[index];
		struct block_parts parts;
		long at = 0;
		int which;

		if (!slot->copied[index] || !header->datum || header->data_size <= 0)
			continue;
		tick_snapshot_block_parts(header->datum, header->data_size, &parts);
		for (which = 0; which < (s.skip_orientations ? parts.skips : 0); which++)
		{
			if (parts.skip_offset[which] > at)
				tick_snapshot_prefetch((byte const *)header->datum + at, parts.skip_offset[which] - at);
			at = parts.skip_offset[which] + parts.skip_size[which];
		}
		if (at < header->data_size)
			tick_snapshot_prefetch((byte const *)header->datum + at, header->data_size - at);
		vs_stats.lines += (unsigned long)((header->data_size + 31) / 32);
		vs_stats.objects++;
	}
}

void tick_snapshot_view_begin(void)
{
	unsigned long perf_before[4];
	unsigned long long entered = 0;

	unsigned long long started;
	struct slot *latest = NULL;
	struct slot *before = NULL;

	if (!tick_snapshot_mode() || !object_header_data || !object_header_data->valid)
		return;
	started = CLOCK();
	entered = tick_overlap_now();
	tick_perf_begin(perf_before);
	if (s.ring.pinned[0] >= 0)
	{
		/* the overlap's: the tick pinned (tick_snapshot_pin) */
		latest = &slots[s.ring.pinned[0]];
		before = s.ring.pinned[1] >= 0 ? &slots[s.ring.pinned[1]] : NULL;
		if (!s.ring.ready[s.ring.pinned[0]])
			return;
	}
	else
	{
		if (tick_ring_published(&s.ring) == NO_TICK || s.dirty)
		{
			/* (objects made or deleted since the last capture, or no tick yet in this map: the ticks have finished, so the copy is of now) */
			tick_snapshot_capture();
			s.catch_ups++;
		}
		if (tick_ring_published(&s.ring) == NO_TICK)
			return;
		latest = tick_snapshot_slot_of(tick_ring_published(&s.ring));
		if (!latest)
			return;
		memset(latest->cameras, 0, sizeof(latest->cameras));
	}
	s.view = latest;
	s.previous = before;
	s.pair_view = s.ring.pinned[0] >= 0;
	s.view_serial++;
	if (s.pair_view)
		tick_snapshot_hash_log();
	s.applied_count = 0;
	object_function_inputs_set_view((int)(latest - slots));
	if ((s.mode & MODE_VERIFY) && !s.verify_begin)
	{
		s.verify_begin = (unsigned long *)calloc((size_t)VERIFY_TYPES * VERIFY_WORDS, sizeof(unsigned long));
		s.verify_end = (unsigned long *)calloc((size_t)VERIFY_TYPES * VERIFY_WORDS, sizeof(unsigned long));
	}
	if (s.verify_begin && s.ring.pinned[0] < 0)
		tick_snapshot_verify(FALSE);
	if (s.view_prefetch && s.pair_view)
	{
		const unsigned long long prefetch_started = tick_overlap_now();

		tick_snapshot_view_prefetch(latest);
		vs_stats.prefetch += tick_overlap_now() - prefetch_started;
	}
	/* (the headers last: from here the accessors on this core give the copies) */
	tick_snapshot_view_array = &latest->array;
	tick_snapshot_view_any = 1;
	tick_snapshot_lights_in_view = s.pair_view && latest->lights_valid;
	s.views++;
	vs_stats.frames++;
	vs_stats.begin += tick_overlap_now() - entered;
	tick_perf_end(&vs_stats.perf_begin, perf_before);
	ADD(PART_VIEW_BEGIN, started);
}

void tick_snapshot_view_end(void)
{
	unsigned long perf_before[4];
	unsigned long long entered;
	unsigned long long started;
	struct slot *view = s.view;
	long index, field, entry;

	if (!tick_snapshot_headers || !view)
		return;
	started = CLOCK();
	entered = tick_overlap_now();
	tick_perf_begin(perf_before);
	if (s.verify_end && s.ring.pinned[0] < 0)
	{
		tick_snapshot_verify(TRUE);
		if (++s.verify_views % 300 == 0)
			tick_snapshot_verify_log();
	}
	/* what the frame wrote in the render-owned fields of the copies, kept for the next copy of the object (the overlap: of the objects the frame asked for, which are listed) */
	for (entry = 0; entry < (s.overlap_requested ? s.applied_count : (long)view->array.count) && entry < MAXIMUM_OBJECTS_PER_MAP; entry++)
	{
		byte *kept;

		index = s.overlap_requested ? s.applied_list[entry] : entry;
		if (!view->copied[index] || !view->headers[index].identifier)
			continue;
		/* (the overlap: only the objects the frame asked for hold what it wrote; the others' copies hold the tick's) */
		if (s.overlap_requested && s.applied[index] != s.view_serial)
			continue;
		kept = s.fields[index];
		for (field = 0; field < RENDER_FIELD_COUNT; field++)
		{
			if (RENDER_FIELDS[field].offset + RENDER_FIELDS[field].size <= view->headers[index].data_size)
				memcpy(kept, (byte const *)view->headers[index].datum + RENDER_FIELDS[field].offset, RENDER_FIELDS[field].size);
			kept += RENDER_FIELDS[field].size;
		}
		s.field_identifier[index] = view->headers[index].identifier;
	}
	tick_snapshot_view_array = NULL;
	tick_snapshot_view_any = 0;
	tick_snapshot_lights_in_view = 0;
	s.view = NULL;
	s.previous = NULL;
	s.pair_view = FALSE;
	vs_stats.end += tick_overlap_now() - entered;
	tick_perf_end(&vs_stats.perf_end, perf_before);
	ADD(PART_VIEW_END, started);
	ADD(PART_FRAME, 0);
}

/* the frame's begin and end, a frame (the overlap's log, with the blends') */
void tick_snapshot_view_log(double ticks_per_ms)
{
	const double f = vs_stats.frames ? (double)vs_stats.frames : 1.0;
	const double ms = ticks_per_ms > 0.0 ? ticks_per_ms : 1.0;

	if (!vs_stats.frames)
		return;
	tick_snapshot_say("tick snapshot, a frame's begin %.3f ms (asking for the lines of %.1f new copies, %.1f lines: %.3f ms) and end %.3f ms (%lu frames)", (double)vs_stats.begin / ms / f,
		(double)vs_stats.objects / f, (double)vs_stats.lines / f, (double)vs_stats.prefetch / ms / f, (double)vs_stats.end / ms / f, vs_stats.frames);
	if (vs_stats.perf_begin.stretches)
	{
		unsigned long long const *b = vs_stats.perf_begin.sum, *e = vs_stats.perf_end.sum;

		tick_snapshot_say("tick snapshot, a frame's begin and end, counters of core %u, a frame: %s %.2f M and %.2f M, %s %.2f M and %.2f M, %s %.1f K and %.1f K, %s %.1f K and %.1f K",
			wiiu_perfmon_core(), wiiu_perfmon_name(0), b[0] / f / 1e6, e[0] / f / 1e6, wiiu_perfmon_name(1), b[1] / f / 1e6, e[1] / f / 1e6, wiiu_perfmon_name(2), b[2] / f / 1e3,
			e[2] / f / 1e3, wiiu_perfmon_name(3), b[3] / f / 1e3, e[3] / f / 1e3);
	}
	memset(&vs_stats, 0, sizeof(vs_stats));
}

#include "tick_snapshot_hash.inc"

#else /* !HALO_WIIU */

typedef int tick_snapshot_is_for_the_wii_u_only;

#endif
