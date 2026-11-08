/*
BOTS_NAV.H

The bots' navigation graph for one map: a few hundred to a thousand areas, the walks, steps, jumps, drops and teleports between them,
and the places on the map (item spawns, flag stands, hills, starting locations). It is made offline (bots_nav_bake.c: a game run on
the Mac, never on the console), kept in navs.pack, and read here. The module is pure (no engine calls): the pack's byte format, its
CRC, the graph search and the lookups, so that the host tests compile it as it is.

The format (bots_nav.c, "the pack") is little-endian on every machine, read byte by byte. Design: reports/research/2026-11-07/design/
bots.md 4.1, with the changes bots2-result.md lists.

Areas: each is a patch of floor that the bake checked to be star-shaped about its anchor: a straight walk from any point of it to the
anchor is clear (by sweeps). The path through an area therefore goes by the anchor, and the portal of a link (the exit point in the
area and the entry point in the next) is a point of the area, so every leg of a path is a straight walk that was checked.
*/

#ifndef HALO_BOTS_NAV_H
#define HALO_BOTS_NAV_H

#include <stddef.h>

/* the file */
#define BOTS_NAV_MAGIC "HALOBOTN"
#define BOTS_NAV_PACK_VERSION 1
#define BOTS_NAV_FORMAT 1
#define BOTS_NAV_PACK_HEADER_BYTES 32
#define BOTS_NAV_INDEX_ENTRY_BYTES 48
#define BOTS_NAV_RECORD_HEADER_BYTES 64
#define BOTS_NAV_AREA_BYTES 24
#define BOTS_NAV_LINK_BYTES 20
#define BOTS_NAV_SCRIPT_BYTES 24
#define BOTS_NAV_PLACE_BYTES 32
/* a map's name in the index */
#define BOTS_NAV_NAME_BYTES 32
/* the largest record read (a map's) */
#define BOTS_NAV_MAXIMUM_RECORD_BYTES (4 * 1024 * 1024)
#define BOTS_NAV_MAXIMUM_AREAS 4096
#define BOTS_NAV_MAXIMUM_LINKS 32768
#define BOTS_NAV_MAXIMUM_PLACES 1024
#define BOTS_NAV_MAXIMUM_SCRIPTS 8192
/* the lookup grid's cell (world units) */
#define BOTS_NAV_CELL 4.0f
/* the path: links (areas) kept, and the links a bot has found bad at once */
#define BOTS_NAV_PATH_LINKS 160
#define BOTS_NAV_BAD_LINKS 8

enum
{
	_bots_nav_link_walk = 0,
	/* a step up a biped climbs with a hop */
	_bots_nav_link_step,
	_bots_nav_link_jump,
	_bots_nav_link_drop,
	_bots_nav_link_teleport,
	NUMBER_OF_BOTS_NAV_LINK_TYPES,
};

enum
{
	_bots_nav_link_flag_one_way = 1,
	/* one of the three tries (speed -10%, 0, +10%) landed elsewhere */
	_bots_nav_link_flag_risky = 2,
};

enum
{
	_bots_nav_area_open = 1,
	_bots_nav_area_high_ground = 2,
	_bots_nav_area_cover = 4,
};

enum
{
	_bots_nav_place_item = 1,
	_bots_nav_place_flag_stand,
	_bots_nav_place_ball_spawn,
	_bots_nav_place_hill_point,
	_bots_nav_place_start,
	_bots_nav_place_teleport_source,
	_bots_nav_place_teleport_target,
	_bots_nav_place_vehicle,
	_bots_nav_place_race_point,
	NUMBER_OF_BOTS_NAV_PLACE_TYPES,
};

/* an item place's class (what the collection holds; bots_items.c's table by the weapon's name) */
enum
{
	_bots_nav_item_unknown = 0,
	_bots_nav_item_weapon,
	_bots_nav_item_power_weapon,
	_bots_nav_item_powerup,
	_bots_nav_item_grenades,
	_bots_nav_item_ammo,
};

struct bots_nav_area
{
	float x, y, z;
	unsigned short first_link;
	unsigned short link_count;
	/* the farthest member from the anchor (world units) */
	float radius;
	unsigned short region;
	unsigned char flags;
	unsigned char exposure;
};

struct bots_nav_link
{
	unsigned short to;
	unsigned char type;
	unsigned char flags;
	/* the time it takes, in 1/16 seconds */
	unsigned short cost_q;
	/* the link's script (jumps and drops), or 0xFFFF */
	unsigned short script;
	/* the exit point (in this area) and the entry point (in the one it leads to), world units */
	float exit[3];
	float entry[3];
};

struct bots_nav_script
{
	/* where the run-up begins, and the direction of the jump (radians) */
	float launch[3];
	float yaw;
	/* the run-up's length (the exit point is where it ends), and the ticks of flight */
	float run_up;
	unsigned short flight_ticks;
	/* the jump is pressed at the exit point (JUMP), not at all (DROP) */
	unsigned char jump;
	/* the bake's tries (of three speeds) that landed on the target */
	unsigned char successes;
	unsigned char attempts;
};

struct bots_nav_place
{
	unsigned char type;
	unsigned char team;
	unsigned short area;
	float x, y, z;
	/* the games it is for (bit per game type, 0: all), the seconds between spawns, the item's class, and an index of its own (hill, ball) */
	unsigned short game_type_mask;
	unsigned short period_seconds;
	unsigned short item_class;
	unsigned short aux;
	/* the scenario's block index it came from */
	unsigned long source;
};

struct bots_nav
{
	char name[BOTS_NAV_NAME_BYTES];
	long area_count, link_count, place_count, script_count;
	float bounds_min[3], bounds_max[3];
	/* what the bake measured of the biped: seconds' speed of a run (units a second), the jump's height, the safest drop */
	float run_speed;
	float jump_height;
	float drop_height;
	struct bots_nav_area *areas;
	struct bots_nav_link *links;
	struct bots_nav_script *scripts;
	struct bots_nav_place *places;
	/* the lookup grid: cells of BOTS_NAV_CELL, cell_first[i] .. cell_first[i + 1] of cell_areas */
	long cells_x, cells_y;
	unsigned long *cell_first;
	unsigned short *cell_areas;
	/* the allocation (everything above is inside it) */
	void *memory;
};

/* a path: the links from the area the bot is in to the area of the goal (or as far as the search came), and the points at the ends.
Its waypoints are made when asked for (bots_nav_path_point) */
struct bots_nav_path
{
	short link_count;
	short step;
	/* the whole way to the goal's area (no: the search ended short, and the bot searches again from the end) */
	char complete;
	char valid;
	short start_area;
	short goal_area;
	float start[3];
	float goal[3];
	unsigned short links[BOTS_NAV_PATH_LINKS];
	/* the area each link leaves */
	unsigned short from[BOTS_NAV_PATH_LINKS];
};

/* one point of a path */
struct bots_nav_point
{
	float p[3];
	/* how the bot arrives at it from the point before: a walk, or the link's type */
	unsigned char kind;
	/* the link the point belongs to, or -1 (anchors and the goal) */
	long link_index;
};

/* the search's scratch (one is shared by all bots: a search runs to its end) */
struct bots_nav_search
{
	long capacity_areas;
	long capacity_heap;
	float *cost;
	unsigned short *parent_link;
	unsigned short *parent_area;
	unsigned char *state;
	unsigned short *heap;
	float *heap_key;
	long heap_count;
	long expanded;
};

/* ---------- reading and writing the pack (bytes, little-endian) */

unsigned long bots_nav_crc32(unsigned long crc, unsigned char const *bytes, size_t count);

/* a map's record from bytes: allocates; 0 with a reason (a short constant string) for a bad record */
struct bots_nav *bots_nav_parse_record(char const *name, unsigned char const *bytes, size_t count, char const **why);
void bots_nav_dispose(struct bots_nav *nav);
/* the bytes of a record for nav (malloc; its size in *count); NULL when it cannot be made */
unsigned char *bots_nav_write_record(struct bots_nav const *nav, size_t *count);

/* the pack's header and index from its first bytes: the entry for `name` (surfaces, walkable surfaces, the record's offset and size); 0
with a reason for a pack that is no good or has no such map. The pack's crc is checked against the whole file by bots_nav_pack_check */
int bots_nav_pack_find(unsigned char const *bytes, size_t count, char const *name, unsigned long *surfaces, unsigned long *walkable,
	unsigned long *offset, unsigned long *size, char const **why);
/* header and index (count bytes of them needed: BOTS_NAV_PACK_HEADER_BYTES + maps * BOTS_NAV_INDEX_ENTRY_BYTES) */
size_t bots_nav_pack_index_bytes(unsigned char const *header, size_t count);
/* the whole pack's crc against its header's; 0 with a reason if it does not match */
int bots_nav_pack_check(unsigned char const *bytes, size_t count, char const **why);
/* a pack of one map's record (the bake's output, merged by tools/bots_nav_pack.py) */
unsigned char *bots_nav_write_pack(char const *name, unsigned long surfaces, unsigned long walkable, unsigned char const *record,
	size_t record_count, unsigned long bake_build, size_t *count);

/* ---------- the graph */

/* the area a point is in (the nearest anchor whose area reaches it, nearest in height first among those); -1 for none */
long bots_nav_find_area(struct bots_nav const *nav, float const *point);
/* the area nearest a point whatever its reach (a goal off the graph is gone to as near as it goes) */
long bots_nav_nearest_area(struct bots_nav const *nav, float const *point);

int bots_nav_search_new(struct bots_nav_search *search, struct bots_nav const *nav);
void bots_nav_search_dispose(struct bots_nav_search *search);
/* a path from area `from` to area `to` (links in bad[] are not taken; the cost of a link is its time, times 1 + `jitter` of a hash of the bot and the link in 0 .. 0.25 if
jitter_seed != 0): TRUE with a path (complete or ending short, nearer the goal), FALSE for no path at all. search->expanded is the areas taken */
int bots_nav_search_path(struct bots_nav const *nav, struct bots_nav_search *search, long from, long to, float const *start,
	float const *goal, unsigned short const *bad, long bad_count, unsigned long jitter_seed, struct bots_nav_path *path);
/* the time (seconds) from one area to another, by a search; a negative number for no way */
float bots_nav_travel_time(struct bots_nav const *nav, struct bots_nav_search *search, long from, long to);

/* the path's points: 3 for each link (the area's anchor, the exit, the entry) and the last area's anchor and the goal; the point with that
number (0 up), FALSE past the end */
long bots_nav_path_point_count(struct bots_nav_path const *path);
int bots_nav_path_point(struct bots_nav const *nav, struct bots_nav_path const *path, long number, struct bots_nav_point *point);

/* places of a type, in order: the index after `after` (-1: the first), or -1 */
long bots_nav_next_place(struct bots_nav const *nav, long after, int type);

#endif
