/*
BOTS_NAV_BAKE.C

The bots' navigation bake: a map's navigation graph (bots_nav.h) made from its collision BSP and the game's own collision tests, offline.
It runs inside a headless game on the Mac (debug.bots_bake_nav = the output file; tools/harness/bots_sim.py plays the map, the bake runs
at the first tick of a bot that is alive and the game ends), never on the console.

What it does, in order (the log says each step's count and time):

1. The biped. The player's biped tag gives the radius, the standing height, the steepest slope and the jump; the game globals the run
   speed; the falling damage block the drop the biped survives.
2. The floor. Every collision surface that a biped can stand on (its slope under the biped's limit, or the engine's walkable flag) is
   sampled at the centres of a grid of cells (debug.bots_bake_cell, 1 unit by default): a node at each, with the surface's height. Nodes
   with less than the biped's height above them (a floor under a wall, under a ledge, under a machine that is in the way) are left out.
   The engine's flag is only a 45-degree slope test: nothing in it says that a biped can be there.
3. The walks. A node is joined to the nodes of the cells next to it when the biped's capsule can go between them: spheres swept along
   the way at the knees, the body and the head meet nothing of the level, the scenery or the machines (a door shut at the bake is a wall:
   doors and lifts are taken as walls, conservatively), the floor in the middle is where the two ends say, and the slope is one a biped
   climbs.
4. The areas. The nodes are cut into patches of up to a few units' reach, each star-shaped about its anchor: a node joins the patch only
   when a straight walk from it to the anchor is clear (the same sweeps, every unit along the way). That is what lets a path go by the
   anchors and the portals between patches without the funnel of a mesh.
5. The jumps and drops. From each node at the edge of the floor, in each direction it has no walk, a biped is flown: running, and
   jumping or walking off, with the game's gravity and the biped's jump, the sphere swept tick by tick; a landing on a floor node of
   another area, a fall the biped survives and a flight that lands there at a tenth more and a tenth less of the speed make a link.
   Teleporters join their source and target flags.
6. The places: the item spawns (with their period), flag stands, ball spawns, hill points, starting locations, vehicles.

The floor is made only inside the box of the map's places and a margin (debug.bots_bake_margin, 15 units): a map's sky and its far hulls (Boarding Action's
372,000 nodes were 97,000 inside it) are floors no game is played on. debug.bots_bake_cell sets the grid (a map with doorways and ramps under a unit wide
wants 0.25: Boarding Action's and Put Put's records are baked so). The log of the bake also says what the map has in it (the objects by type, the scenery, the
machines), where the pieces of the floor graph nearly touch (the gaps: what may join them), and debug.bots_bake_probe "x0 y0 x1 y1 [step]" and
debug.bots_bake_segment "x0 y0 z0 x1 y1 z1" log the floors under a line and the bake's walk test between two points, step by step. A graph that reaches too little of the
places from the starts is marked fragmented by tools/bots_nav_pack.py merge, and the game then walks the map by the engine's surfaces.

What it does not do (bots2-result.md): the validation by driving a unit through each link in a game; doors and elevators as links; the
hint file; the exposure and cover analysis.
*/

/* the console reads the pack and never makes it: the bake is for the desktop's builds (and the big-endian test build) */
#ifndef HALO_WIIU
/* (the output file is a real path, not an Xbox one) */
#define HALO_LINUX_PLATFORM_LAYER 1

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/game_globals.h"
#include "game/players.h"
#include "main/main.h"
#include "objects/objects.h"
#include "units/units.h"
#include "units/bipeds.h"
#include "units/biped_definitions.h"
#include "items/items.h"
#include "items/item_definitions.h"
#include "devices/device_machines.h"
#include "devices/device_definitions.h"
#include "physics/collisions.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "structures/structure_bsp_definitions.h"
#include "physics/collision_bsp_definitions.h"
#include "tag_files/tag_groups.h"
#include "tag_files/tag_files.h"

#include "bots_nav.h"
#include "bots_nav_bake.h"

#define item_collection_definition_get(index) ((struct item_collection_definition *)tag_get(ITEM_COLLECTION_DEFINITION_TAG, index))

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (the C library's allocator, throughout: cseries.h maps malloc, free and realloc to the game's debug memory, which does not know the pointers
that bots_nav.c, calloc and the file writes hand back) */
#undef malloc
#undef free
#undef realloc

void platform_log(char const *format, ...);
long config_integer(char const *name);
const char *config_string(char const *name);
long long platform_microseconds(void);
extern real global_gravity;

#define PI_F 3.14159265f

/* the falling damage block of the game globals (bipeds.c and damage.c each carry the layout) */
struct bake_falling_damage
{
	long unused0[2];
	real falling_distance_lower_bound;
	real falling_distance_upper_bound;
	struct tag_reference falling_damage;
	long terminal_velocity_unused[2];
	real maximum_distance;
	struct tag_reference maximum_distance_damage;
	struct tag_reference vehicle_hit_environment_damage_effect;
	struct tag_reference vehicle_killed_unit_damage_effect;
	struct tag_reference vehicle_collision_damage;
	struct tag_reference flaming_death_damage;
	long unused7c[4];
	real runtime_maximum_falling_velocity;
	real runtime_minimum_damage_velocity;
	real runtime_maximum_damage_velocity;
};
typedef char bake_falling_damage_size_assert[sizeof(struct bake_falling_damage) == 0x98 ? 1 : -1];

static void bake_engine_pieces(struct collision_bsp const *bsp, struct structure_bsp const *structure);
static int bake_engine_piece_of(float const *p);
static void bake_engine_path(struct collision_bsp const *bsp, struct structure_bsp const *structure);

/* ---------- the world's tests */

#define BAKE_SOLID_FLAGS (FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_back_facing_surfaces_bit) | \
	FLAG(_collision_test_structure_bit) | FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit) | \
	FLAG(_collision_test_objects_machines_bit))
#define BAKE_FLOOR_FLAGS (FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_structure_bit))

static struct
{
	/* the biped */
	float radius, height, slope_tan, jump_velocity, gravity, run_speed, drop_height, step;
	/* the cell of the grid */
	float cell;
	/* the box of the places (and a margin) the floor is made inside of: a map's sky and its far hulls are no floor a game is played on */
	int play_on;
	float play_min[2], play_max[2];
	long cast_count;
	/* why a walk was refused: no floor, too steep, the knees met something, the body did */
	long refused[4];
	/* a place to watch: refusals near it are logged (the probe's first point) */
	float watch[2];
	int watching;
	long watch_logged;
	float last_refused[3];
	int last_reason;
} bake;

/* whether the sphere can go from a to b meeting nothing */
static int bake_sphere_clear(float const *a, float const *b, float radius)
{
	struct collision_result collision;
	real_point3d point;
	real_vector3d vector;

	point.x = a[0];
	point.y = a[1];
	point.z = a[2];
	vector.i = b[0] - a[0];
	vector.j = b[1] - a[1];
	vector.k = b[2] - a[2];
	bake.cast_count++;
	if (collision_test_pill(BAKE_SOLID_FLAGS, &point, &vector, radius, NONE, &collision))
	{
		if (bake.watching && (float)fabs(a[0] - bake.watch[0]) < 4.0f && (float)fabs(a[1] - bake.watch[1]) < 4.0f && radius < 0.5f && bake.watch_logged < 40)
		{
			bake.watch_logged++;
			platform_log("bots: bake: watch: sphere r%.2f from (%.2f %.2f %.2f) to (%.2f %.2f %.2f) hit type %d at (%.2f %.2f %.2f) normal (%.2f %.2f %.2f) surface %ld object %s",
				radius, a[0], a[1], a[2], b[0], b[1], b[2], (int)collision.type, collision.point.x, collision.point.y, collision.point.z, collision.plane.n.i,
				collision.plane.n.j, collision.plane.n.k, collision.surface_index,
				collision.type == _collision_result_object && collision.object_index != NONE ? tag_get_name(object_get(collision.object_index)->definition_index) : "-");
		}
		return 0;
	}
	return 1;
}

/* the floor under (x, y), looking down from `from` for `depth`: its height and the slope's normal; FALSE for none */
static int bake_floor(float x, float y, float from, float depth, float *height, float *normal_z)
{
	struct collision_result collision;
	real_point3d point;
	real_vector3d vector;

	point.x = x;
	point.y = y;
	point.z = from;
	vector.i = 0.0f;
	vector.j = 0.0f;
	vector.k = -depth;
	bake.cast_count++;
	if (!collision_test_vector(BAKE_FLOOR_FLAGS, &point, &vector, NONE, &collision) || collision.type != _collision_result_structure)
		return 0;
	*height = collision.point.z;
	if (normal_z)
		*normal_z = collision.plane.n.k;
	return 1;
}

/* whether the way from a to b (both on the floor) is a walk: the floor under each foot step along it is a step or a slope from the one
before, and the knee, body and head spheres meet nothing. The floor's height at each step is followed, not the line between the ends */
static int bake_segment_walkable_inner(float const *a, float const *b, float spacing);

static int bake_segment_walkable(float const *a, float const *b, float spacing)
{
	int ok = bake_segment_walkable_inner(a, b, spacing);

	if (!ok && bake.watching && spacing < 0.6f && (float)fabs(a[0] - bake.watch[0]) < 4.0f && (float)fabs(a[1] - bake.watch[1]) < 4.0f)
		platform_log("bots: bake: watch: (%.1f %.1f %.2f) to (%.1f %.1f %.2f) refused: %s", a[0], a[1], a[2], b[0], b[1], b[2],
			bake.last_reason == 0 ? "no floor" : bake.last_reason == 1 ? "too steep" : bake.last_reason == 2 ? "the knees met something" : "the body met something");
	return ok;
}

static int bake_segment_walkable_inner(float const *a, float const *b, float spacing)
{
	float dx = b[0] - a[0], dy = b[1] - a[1];
	float distance = (float)sqrt(dx * dx + dy * dy);
	long steps = (long)ceil(distance / spacing), step;
	float previous[3] = { a[0], a[1], a[2] };
	float knee = bake.radius * 1.1f + 0.1f, body = bake.height * 0.55f;
	float climb = bake.slope_tan * spacing + 0.03f;

	for (step = 1; step <= steps; step++)
	{
		float t = (float)step / (float)steps;
		float next[3] = { a[0] + dx * t, a[1] + dy * t, 0.0f };
		float a_knee[3], b_knee[3], a_body[3], b_body[3];

		if (step == steps)
			next[2] = b[2];
		else
		{
			/* (the floor near the height it was at: a floor above or below is another floor) */
			float height;

			if (!bake_floor(next[0], next[1], previous[2] + bake.step + 0.45f, 0.45f + 2.0f * bake.step + 0.5f, &height, NULL))
			{
				bake.refused[0]++;
				bake.last_reason = 0;
				return 0;
			}
			next[2] = height;
		}
		if ((float)fabs(next[2] - previous[2]) > climb + bake.step)
		{
			bake.refused[1]++;
			bake.last_reason = 1;
			return 0;
		}
		a_knee[0] = previous[0]; a_knee[1] = previous[1]; a_knee[2] = MAX(previous[2], next[2]) + knee;
		b_knee[0] = next[0]; b_knee[1] = next[1]; b_knee[2] = MAX(previous[2], next[2]) + knee;
		a_body[0] = previous[0]; a_body[1] = previous[1]; a_body[2] = previous[2] + body;
		b_body[0] = next[0]; b_body[1] = next[1]; b_body[2] = next[2] + body;
		if (!bake_sphere_clear(a_knee, b_knee, bake.radius * 0.9f))
		{
			bake.refused[2]++;
			bake.last_reason = 2;
			return 0;
		}
		if (!bake_sphere_clear(a_body, b_body, bake.radius))
		{
			bake.refused[3]++;
			bake.last_reason = 3;
			return 0;
		}
		previous[0] = next[0];
		previous[1] = next[1];
		previous[2] = next[2];
	}
	return 1;
}

/* ---------- the nodes */

struct bake_node
{
	float p[3];
	long cx, cy;
	/* the neighbours in the eight directions (a node index or -1) */
	long neighbor[8];
	long area;
	/* it is at the edge of the floor: some direction has no neighbour and no floor beside it */
	unsigned char edge_mask;
	unsigned char dead;
};

static struct
{
	struct bake_node *nodes;
	long count, capacity;
	/* the grid: the nodes of cell (x, y) are cell_start[y * cells_x + x] .. [... + 1] (sorted by height) */
	long *cell_start;
	long cells_x, cells_y;
	float origin[2];
	float bounds_min[3], bounds_max[3];
} grid;

static int const direction_x[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
static int const direction_y[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };

static int bake_node_compare(void const *a, void const *b)
{
	struct bake_node const *x = (struct bake_node const *)a, *y = (struct bake_node const *)b;
	long kx = x->cy * grid.cells_x + x->cx, ky = y->cy * grid.cells_x + y->cx;

	if (kx != ky)
		return kx < ky ? -1 : 1;
	if (x->p[2] != y->p[2])
		return x->p[2] < y->p[2] ? -1 : 1;
	return 0;
}

static void bake_add_node(float x, float y, float z, long cx, long cy)
{
	struct bake_node *node;

	if (bake.play_on && (x < bake.play_min[0] || x > bake.play_max[0] || y < bake.play_min[1] || y > bake.play_max[1]))
		return;
	if (grid.count >= grid.capacity)
	{
		long capacity = grid.capacity ? grid.capacity * 2 : 65536;
		struct bake_node *nodes = (struct bake_node *)realloc(grid.nodes, (size_t)capacity * sizeof(struct bake_node));

		if (!nodes)
			return;
		grid.nodes = nodes;
		grid.capacity = capacity;
	}
	node = &grid.nodes[grid.count++];
	memset(node, 0, sizeof(*node));
	node->p[0] = x;
	node->p[1] = y;
	node->p[2] = z;
	node->cx = cx;
	node->cy = cy;
	node->area = -1;
	{
		int d;

		for (d = 0; d < 8; d++)
			node->neighbor[d] = -1;
	}
}

#define BAKE_CORNERS 16
static long bake_surface_corners(struct collision_bsp const *bsp, long surface_index, real_point3d *corners)
{
	struct collision_surface const *surface;
	long edge_index, count = 0;

	surface = TAG_BLOCK_GET_ELEMENT(&bsp->surfaces, surface_index, struct collision_surface);
	edge_index = surface->first_edge_index;
	do
	{
		struct collision_edge const *edge;
		boolean right;

		if (edge_index < 0 || edge_index >= bsp->edges.count)
			return 0;
		edge = TAG_BLOCK_GET_ELEMENT(&bsp->edges, edge_index, struct collision_edge);
		right = edge->surface_indices[1] == surface_index;
		if (!VALID_INDEX(edge->vertex_indices[right], bsp->vertices.count))
			return 0;
		corners[count++] = TAG_BLOCK_GET_ELEMENT(&bsp->vertices, edge->vertex_indices[right], struct collision_vertex)->point;
		edge_index = edge->edge_indices[right];
	} while (edge_index != surface->first_edge_index && count < BAKE_CORNERS);
	return count;
}

/* the floor nodes: the centres of the grid's cells on every surface a biped can stand on */
static void bake_collect_floor(struct collision_bsp const *bsp, struct structure_bsp const *structure, int upward_sign)
{
	long surface_index, flagged = 0, sloped = 0, standable = 0;
	float const slope_cos = 1.0f / (float)sqrt(1.0f + bake.slope_tan * bake.slope_tan);

	for (surface_index = 0; surface_index < bsp->surfaces.count; surface_index++)
	{
		real_point3d corners[BAKE_CORNERS];
		long count = bake_surface_corners(bsp, surface_index, corners), k;
		double nx = 0, ny = 0, nz = 0, length;
		float low[2] = { 1e30f, 1e30f }, high[2] = { -1e30f, -1e30f };
		int walkable_flag = 0, walkable_slope;
		long cx0, cx1, cy0, cy1, cx, cy;

		if (count < 3)
			continue;
		/* (Newell's normal: the polygon's winding gives its facing) */
		for (k = 0; k < count; k++)
		{
			real_point3d const *p = &corners[k], *q = &corners[(k + 1) % count];

			nx += ((double)p->y - q->y) * ((double)p->z + q->z);
			ny += ((double)p->z - q->z) * ((double)p->x + q->x);
			nz += ((double)p->x - q->x) * ((double)p->y + q->y);
			low[0] = MIN(low[0], p->x);
			low[1] = MIN(low[1], p->y);
			high[0] = MAX(high[0], p->x);
			high[1] = MAX(high[1], p->y);
		}
		length = sqrt(nx * nx + ny * ny + nz * nz);
		if (length < 1e-9)
			continue;
		nz = nz / length * upward_sign;
		if (structure && structure->pathfinding_surfaces.address && surface_index < structure->pathfinding_surfaces.count &&
			TEST_FLAG(((byte const *)structure->pathfinding_surfaces.address)[surface_index], 6))
		{
			walkable_flag = 1;
			flagged++;
		}
		walkable_slope = nz >= slope_cos;
		if (walkable_slope)
			sloped++;
		if (!walkable_slope && !(walkable_flag && nz > 0.5))
			continue;
		standable++;
		cx0 = (long)floor((low[0] - grid.origin[0]) / bake.cell);
		cx1 = (long)floor((high[0] - grid.origin[0]) / bake.cell);
		cy0 = (long)floor((low[1] - grid.origin[1]) / bake.cell);
		cy1 = (long)floor((high[1] - grid.origin[1]) / bake.cell);
		for (cy = cy0; cy <= cy1; cy++)
			for (cx = cx0; cx <= cx1; cx++)
			{
				float x = grid.origin[0] + ((float)cx + 0.5f) * bake.cell, y = grid.origin[1] + ((float)cy + 0.5f) * bake.cell;
				int inside = 0;
				double z;

				if (cx < 0 || cy < 0 || cx >= grid.cells_x || cy >= grid.cells_y)
					continue;
				/* (even-odd in the plane) */
				for (k = 0; k < count; k++)
				{
					real_point3d const *p = &corners[k], *q = &corners[(k + 1) % count];

					if ((p->y > y) != (q->y > y) && x < (q->x - p->x) * (y - p->y) / (q->y - p->y) + p->x)
						inside = !inside;
				}
				if (!inside)
					continue;
				/* (the plane's height there) */
				{
					double plane_nx = nx / length * upward_sign, plane_ny = ny / length * upward_sign;
					double plane_nz = nz;

					z = corners[0].z - (plane_nx * (x - corners[0].x) + plane_ny * (y - corners[0].y)) / plane_nz;
				}
				bake_add_node(x, y, (float)z, cx, cy);
			}
	}
	platform_log("bots: bake: %ld collision surfaces, %ld with the engine's walkable flag, %ld within the biped's slope, %ld stood on; %ld nodes before the clearance",
		(long)bsp->surfaces.count, flagged, sloped, standable, grid.count);
}

/* the floor nodes' order: by cell and height; two within a cell at the same height are one (a surface's edge) */
static void bake_sort_nodes(void)
{
	long index, kept = 0;

	qsort(grid.nodes, (size_t)grid.count, sizeof(struct bake_node), bake_node_compare);
	for (index = 0; index < grid.count; index++)
	{
		if (kept > 0 && grid.nodes[kept - 1].cx == grid.nodes[index].cx && grid.nodes[kept - 1].cy == grid.nodes[index].cy &&
			(float)fabs(grid.nodes[kept - 1].p[2] - grid.nodes[index].p[2]) < 0.06f)
		{
			continue;
		}
		grid.nodes[kept++] = grid.nodes[index];
	}
	grid.count = kept;
}

static void bake_index_cells(void)
{
	long cells = grid.cells_x * grid.cells_y, index, cell;

	free(grid.cell_start);
	grid.cell_start = (long *)calloc((size_t)cells + 1, sizeof(long));
	if (!grid.cell_start)
		return;
	for (index = 0; index < grid.count; index++)
		grid.cell_start[grid.nodes[index].cy * grid.cells_x + grid.nodes[index].cx + 1]++;
	for (cell = 0; cell < cells; cell++)
		grid.cell_start[cell + 1] += grid.cell_start[cell];
}

/* the biped has room: nothing of the level, scenery or machines within its height above the node (a crouching room is no room) */
static void bake_clearance(void)
{
	long index, kept = 0, low = 0;
	float up[3], from[3];

	for (index = 0; index < grid.count; index++)
	{
		struct bake_node *node = &grid.nodes[index];
		struct collision_result collision;
		real_point3d point;
		real_vector3d vector;

		(void)up;
		(void)from;
		if (index % 2000 == 0)
			platform_log("bots: bake: clearance %ld of %ld nodes, %ld casts, %.1f s", index, grid.count, bake.cast_count,
				(double)platform_microseconds() / 1e6);
		point.x = node->p[0];
		point.y = node->p[1];
		point.z = node->p[2] + 0.04f;
		vector.i = 0.0f;
		vector.j = 0.0f;
		vector.k = bake.height + 0.02f;
		bake.cast_count++;
		if (collision_test_vector(BAKE_SOLID_FLAGS, &point, &vector, NONE, &collision))
		{
			low++;
			continue;
		}
		/* (and room for the body's sphere to stand there at all) */
		{
			float centre[3] = { node->p[0], node->p[1], node->p[2] + bake.height * 0.55f };

			float knee_centre[3] = { node->p[0], node->p[1], node->p[2] + bake.radius * 1.1f + 0.1f };

			/* (room for the biped's whole width at the body and at the knees: a node is not where its bot's pill meets a wall) */
			if (!bake_sphere_clear(centre, centre, bake.radius * 1.0f) || !bake_sphere_clear(knee_centre, knee_centre, bake.radius * 0.9f))
			{
				low++;
				continue;
			}
		}
		grid.nodes[kept++] = *node;
	}
	grid.count = kept;
	platform_log("bots: bake: %ld nodes with room for a biped standing (%ld without)", grid.count, low);
}

/* the node of cell (cx, cy) nearest in height to z within `tolerance`; -1 for none */
static long bake_find_in_cell(long cx, long cy, float z, float tolerance)
{
	long cell, k, best = -1;
	float best_difference = tolerance;

	if (cx < 0 || cy < 0 || cx >= grid.cells_x || cy >= grid.cells_y)
		return -1;
	cell = cy * grid.cells_x + cx;
	for (k = grid.cell_start[cell]; k < grid.cell_start[cell + 1]; k++)
	{
		float difference = (float)fabs(grid.nodes[k].p[2] - z);

		if (difference <= best_difference)
		{
			best_difference = difference;
			best = k;
		}
	}
	return best;
}

/* the floor graph's pieces (over the walk links), the largest few, and an image of them (<path>.pieces.ppm) for looking at */
static void bake_report_pieces(void)
{
	long *piece = (long *)malloc((size_t)grid.count * sizeof(long)), *queue = (long *)malloc((size_t)grid.count * sizeof(long));
	long pieces = 0, index, sizes[16] = { 0 }, big = 0;
	char const *path = config_string("debug.bots_bake_nav");

	if (!piece || !queue)
		return;
	for (index = 0; index < grid.count; index++)
		piece[index] = -1;
	for (index = 0; index < grid.count; index++)
	{
		long head = 0, tail = 0, size = 0;

		if (piece[index] >= 0)
			continue;
		piece[index] = pieces;
		queue[tail++] = index;
		while (head < tail)
		{
			struct bake_node *node = &grid.nodes[queue[head++]];
			int d;

			size++;
			for (d = 0; d < 8; d++)
				if (node->neighbor[d] >= 0 && piece[node->neighbor[d]] < 0)
				{
					piece[node->neighbor[d]] = pieces;
					queue[tail++] = node->neighbor[d];
				}
		}
		if (pieces < 16)
			sizes[pieces] = size;
		if (size > big)
			big = size;
		pieces++;
	}
	platform_log("bots: bake: the floor graph has %ld pieces, the largest of %ld of %ld nodes (the first sixteen: %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld)", pieces, big,
		grid.count, sizes[0], sizes[1], sizes[2], sizes[3], sizes[4], sizes[5], sizes[6], sizes[7], sizes[8], sizes[9], sizes[10], sizes[11], sizes[12],
		sizes[13], sizes[14], sizes[15]);
	/* (where the pieces nearly touch: for each pair of the larger pieces the two nodes nearest each other within two units in the plane, for looking at what
	may join them: a door, a lift, a step too tall for a walk, a jump) */
	{
		long *piece_size = (long *)calloc((size_t)pieces + 1, sizeof(long)), *rank = (long *)malloc(((size_t)pieces + 1) * sizeof(long));
		long considered = 0, a, b;
		struct gap { float distance; long first, second; } *gaps;

		if (piece_size && rank)
		{
			for (index = 0; index < grid.count; index++)
				piece_size[piece[index]]++;
			for (a = 0; a < pieces; a++)
				rank[a] = piece_size[a] >= 40 && considered < 60 ? considered++ : -1;
			gaps = (struct gap *)malloc((size_t)(considered * considered + 1) * sizeof(struct gap));
			if (gaps)
			{
				for (a = 0; a < considered * considered; a++)
				{
					gaps[a].distance = 1.0e30f;
					gaps[a].first = gaps[a].second = -1;
				}
				for (index = 0; index < grid.count; index++)
				{
					struct bake_node *node = &grid.nodes[index];
					long from_rank = rank[piece[index]], dx, dy;

					if (from_rank < 0)
						continue;
					for (dy = -4; dy <= 4; dy++)
						for (dx = -4; dx <= 4; dx++)
						{
							long cx = node->cx + dx, cy = node->cy + dy, k;

							if (cx < 0 || cy < 0 || cx >= grid.cells_x || cy >= grid.cells_y)
								continue;
							for (k = grid.cell_start[cy * grid.cells_x + cx]; k < grid.cell_start[cy * grid.cells_x + cx + 1]; k++)
							{
								long to_rank = rank[piece[k]];
								float ex, ey, ez, distance;

								if (to_rank < 0 || to_rank == from_rank)
									continue;
								ex = grid.nodes[k].p[0] - node->p[0];
								ey = grid.nodes[k].p[1] - node->p[1];
								ez = grid.nodes[k].p[2] - node->p[2];
								if ((float)fabs(ez) > 3.0f)
									continue;
								distance = (float)sqrt(ex * ex + ey * ey) + 0.3f * (float)fabs(ez);
								if (distance < gaps[from_rank * considered + to_rank].distance)
								{
									gaps[from_rank * considered + to_rank].distance = distance;
									gaps[from_rank * considered + to_rank].first = index;
									gaps[from_rank * considered + to_rank].second = k;
								}
							}
						}
				}
				for (a = 0; a < pieces; a++)
					for (b = a + 1; b < pieces; b++)
						if (rank[a] >= 0 && rank[b] >= 0 && gaps[rank[a] * considered + rank[b]].first >= 0)
						{
							struct gap *g = &gaps[rank[a] * considered + rank[b]];
							struct bake_node *n1 = &grid.nodes[g->first], *n2 = &grid.nodes[g->second];

							platform_log("bots: bake: gap: piece %ld (%ld nodes) and piece %ld (%ld nodes): (%.2f %.2f %.2f) to (%.2f %.2f %.2f), %.2f apart in the plane, %.2f in height", a,
								piece_size[a], b, piece_size[b], n1->p[0], n1->p[1], n1->p[2], n2->p[0], n2->p[1], n2->p[2],
								(double)sqrt((n1->p[0] - n2->p[0]) * (n1->p[0] - n2->p[0]) + (n1->p[1] - n2->p[1]) * (n1->p[1] - n2->p[1])), (double)(n2->p[2] - n1->p[2]));
						}
				free(gaps);
			}
		}
		free(piece_size);
		free(rank);
	}
	if (path && path[0])
	{
		char image[1100];
		FILE *file;

		snprintf(image, sizeof(image), "%s.pieces.ppm", path);
		file = fopen(image, "wb");
		if (file)
		{
			long x, y;
			unsigned char *pixels = (unsigned char *)calloc((size_t)grid.cells_x * (size_t)grid.cells_y, 3);

			if (pixels)
			{
				for (index = 0; index < grid.count; index++)
				{
					struct bake_node *node = &grid.nodes[index];
					unsigned char *pixel = pixels + 3 * (((size_t)(grid.cells_y - 1 - node->cy)) * (size_t)grid.cells_x + (size_t)node->cx);
					unsigned long h = (unsigned long)piece[index] * 2654435761UL;

					if (piece[index] == 0)
					{
						pixel[0] = 90; pixel[1] = 90; pixel[2] = 90;
					}
					else
					{
						pixel[0] = (unsigned char)(80 + (h >> 8) % 175);
						pixel[1] = (unsigned char)(80 + (h >> 16) % 175);
						pixel[2] = (unsigned char)(80 + (h >> 24) % 175);
					}
				}
				fprintf(file, "P6\n%ld %ld\n255\n", grid.cells_x, grid.cells_y);
				fwrite(pixels, 3, (size_t)grid.cells_x * (size_t)grid.cells_y, file);
				/* (and the same by height, for a second image: the heights (0 to 4 units) in the red channel, the piece's size class in green) */
				memset(pixels, 0, (size_t)grid.cells_x * (size_t)grid.cells_y * 3);
				for (index = 0; index < grid.count; index++)
				{
					struct bake_node *node = &grid.nodes[index];
					unsigned char *pixel = pixels + 3 * (((size_t)(grid.cells_y - 1 - node->cy)) * (size_t)grid.cells_x + (size_t)node->cx);
					float h = (node->p[2] + 1.0f) * 40.0f;

					pixel[0] = (unsigned char)(h < 0 ? 0 : h > 255 ? 255 : h);
					pixel[1] = piece[index] == 0 ? 120 : 0;
					pixel[2] = (unsigned char)(node->neighbor[0] >= 0 || node->neighbor[2] >= 0 || node->neighbor[4] >= 0 || node->neighbor[6] >= 0 ? 0 : 255);
				}
				fwrite(pixels, 3, (size_t)grid.cells_x * (size_t)grid.cells_y, file);
				free(pixels);
			}
			(void)x;
			(void)y;
			fclose(file);
		}
	}
	free(piece);
	free(queue);
}

/* the walks between a node and the nodes of the cells next to it, validated by sweeps */
static void bake_link_nodes(void)
{
	long index, links = 0, tried = 0, refused = 0;

	for (index = 0; index < grid.count; index++)
	{
		struct bake_node *node = &grid.nodes[index];
		int d;

		/* (E, NE, N and NW: the other four are the neighbours' own) */
		for (d = 0; d < 4; d++)
		{
			long other = bake_find_in_cell(node->cx + direction_x[d], node->cy + direction_y[d], node->p[2],
				bake.step + bake.slope_tan * bake.cell * (d & 1 ? 1.42f : 1.0f));

			if (other < 0 || grid.nodes[other].neighbor[(d + 4) & 7] >= 0)
				continue;
			/* (a diagonal only where the two cells beside it were walked: no cutting a corner of a wall) */
			if (d & 1)
			{
				long first = node->neighbor[(d + 7) & 7];
				long second = node->neighbor[(d + 1) & 7];

				(void)first;
				(void)second;
			}
			tried++;
			if (bake_segment_walkable(node->p, grid.nodes[other].p, bake.cell * 0.55f))
			{
				node->neighbor[d] = other;
				grid.nodes[other].neighbor[(d + 4) & 7] = index;
				links++;
			}
			else
				refused++;
		}
	}
	platform_log("bots: bake: %ld walk links between floor nodes (%ld refused by the sweeps of %ld tried: no floor %ld, too steep %ld, knees %ld, body %ld)", links,
		refused, tried, bake.refused[0], bake.refused[1], bake.refused[2], bake.refused[3]);
	bake_report_pieces();
}

/* ---------- the areas */

struct bake_area
{
	float anchor[3];
	long anchor_node;
	long member_count;
	float radius;
	long first_link;
	long link_count;
};

static struct
{
	struct bake_area *areas;
	long count, capacity;
} areas;

static struct bake_area *bake_area_new(long anchor_node)
{
	struct bake_area *area;

	if (areas.count >= areas.capacity)
	{
		long capacity = areas.capacity ? areas.capacity * 2 : 1024;
		struct bake_area *list = (struct bake_area *)realloc(areas.areas, (size_t)capacity * sizeof(struct bake_area));

		if (!list)
			return NULL;
		areas.areas = list;
		areas.capacity = capacity;
	}
	area = &areas.areas[areas.count++];
	memset(area, 0, sizeof(*area));
	area->anchor_node = anchor_node;
	memcpy(area->anchor, grid.nodes[anchor_node].p, sizeof(area->anchor));
	return area;
}

/* the patches: grown from a node over the walk links, a node joining while the straight walk to the anchor is clear and within `reach` */
static void bake_make_areas(float reach)
{
	long index, *queue = (long *)malloc((size_t)grid.count * sizeof(long));
	long tested = 0, refused = 0;

	if (!queue)
		return;
	for (index = 0; index < grid.count; index++)
	{
		long head = 0, tail = 0;
		struct bake_area *area;
		long area_index;

		if (grid.nodes[index].area >= 0)
			continue;
		area = bake_area_new(index);
		if (!area)
			break;
		area_index = areas.count - 1;
		grid.nodes[index].area = area_index;
		area->member_count = 1;
		queue[tail++] = index;
		while (head < tail)
		{
			struct bake_node *node = &grid.nodes[queue[head++]];
			int d;

			for (d = 0; d < 8; d++)
			{
				long other = node->neighbor[d];
				float dx, dy;

				if (other < 0 || grid.nodes[other].area >= 0)
					continue;
				dx = grid.nodes[other].p[0] - area->anchor[0];
				dy = grid.nodes[other].p[1] - area->anchor[1];
				if ((float)sqrt(dx * dx + dy * dy) > reach || (float)fabs(grid.nodes[other].p[2] - area->anchor[2]) > 1.5f)
					continue;
				tested++;
				if (!bake_segment_walkable(area->anchor, grid.nodes[other].p, 0.7f))
				{
					refused++;
					continue;
				}
				grid.nodes[other].area = area_index;
				area->member_count++;
				area->radius = MAX(area->radius, (float)sqrt(dx * dx + dy * dy));
				queue[tail++] = other;
			}
		}
	}
	free(queue);
	platform_log("bots: bake: %ld areas cut (reach %.1f), %ld star tests of which %ld refused", areas.count, reach, tested, refused);
}

/* an area of fewer than `minimum` nodes goes into a neighbouring area when its nodes all see that area's anchor */
static void bake_merge_small(long minimum, float reach)
{
	long pass, merged_total = 0;

	for (pass = 0; pass < 2; pass++)
	{
		long area_index;

		for (area_index = 0; area_index < areas.count; area_index++)
		{
			struct bake_area *area = &areas.areas[area_index];
			long node_index, target = -1;

			if (area->member_count <= 0 || area->member_count >= minimum)
				continue;
			/* (the neighbouring areas across the walk links) */
			for (node_index = 0; node_index < grid.count && target < 0; node_index++)
			{
				int d;

				if (grid.nodes[node_index].area != area_index)
					continue;
				for (d = 0; d < 8 && target < 0; d++)
				{
					long other = grid.nodes[node_index].neighbor[d];
					struct bake_area *candidate;
					long member;
					int all = 1;

					if (other < 0 || grid.nodes[other].area == area_index)
						continue;
					candidate = &areas.areas[grid.nodes[other].area];
					for (member = 0; member < grid.count && all; member++)
					{
						float dx, dy;

						if (grid.nodes[member].area != area_index)
							continue;
						dx = grid.nodes[member].p[0] - candidate->anchor[0];
						dy = grid.nodes[member].p[1] - candidate->anchor[1];
						if ((float)sqrt(dx * dx + dy * dy) > reach * 1.3f ||
							!bake_segment_walkable(candidate->anchor, grid.nodes[member].p, 0.7f))
						{
							all = 0;
						}
					}
					if (all)
						target = grid.nodes[other].area;
				}
			}
			if (target >= 0)
			{
				long member;

				for (member = 0; member < grid.count; member++)
				{
					if (grid.nodes[member].area == area_index)
					{
						float dx = grid.nodes[member].p[0] - areas.areas[target].anchor[0];
						float dy = grid.nodes[member].p[1] - areas.areas[target].anchor[1];

						grid.nodes[member].area = target;
						areas.areas[target].member_count++;
						areas.areas[target].radius = MAX(areas.areas[target].radius, (float)sqrt(dx * dx + dy * dy));
					}
				}
				area->member_count = 0;
				merged_total++;
			}
		}
	}
	platform_log("bots: bake: %ld small areas merged into a neighbour", merged_total);
}

/* the areas that are left, numbered from 0 (the empty ones dropped); the nodes follow */
static void bake_compact_areas(void)
{
	long *remap = (long *)malloc((size_t)areas.count * sizeof(long));
	long index, kept = 0;

	if (!remap)
		return;
	for (index = 0; index < areas.count; index++)
	{
		if (areas.areas[index].member_count > 0)
		{
			remap[index] = kept;
			areas.areas[kept++] = areas.areas[index];
		}
		else
			remap[index] = -1;
	}
	areas.count = kept;
	for (index = 0; index < grid.count; index++)
		if (grid.nodes[index].area >= 0)
			grid.nodes[index].area = remap[grid.nodes[index].area];
	free(remap);
}

/* ---------- links between areas */

struct bake_link
{
	long from, to;
	unsigned char type, flags;
	float exit[3], entry[3];
	long script;
	unsigned short cost_q;
};

static struct
{
	struct bake_link *links;
	long count, capacity;
	struct bots_nav_script *scripts;
	long script_count, script_capacity;
} edges;

static struct bake_link *bake_link_new(void)
{
	if (edges.count >= edges.capacity)
	{
		long capacity = edges.capacity ? edges.capacity * 2 : 4096;
		struct bake_link *list = (struct bake_link *)realloc(edges.links, (size_t)capacity * sizeof(struct bake_link));

		if (!list)
			return NULL;
		edges.links = list;
		edges.capacity = capacity;
	}
	memset(&edges.links[edges.count], 0, sizeof(struct bake_link));
	edges.links[edges.count].script = 0xFFFF;
	return &edges.links[edges.count++];
}

static float bake_dist3(float const *a, float const *b)
{
	float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];

	return (float)sqrt(dx * dx + dy * dy + dz * dz);
}

static unsigned short bake_cost_q(float seconds)
{
	float q = seconds * 16.0f + 0.5f;

	return (unsigned short)(q > 65535.0f ? 65535 : q);
}

/* the pair (from, to) -> a link, for the links made so far (open addressing; a key is from * 65536 + to + 1) */
static struct
{
	unsigned long *key;
	long *value;
	long size;
} pair_table;

static void bake_pair_table_new(long size)
{
	free(pair_table.key);
	free(pair_table.value);
	pair_table.size = size;
	pair_table.key = (unsigned long *)calloc((size_t)size, sizeof(unsigned long));
	pair_table.value = (long *)calloc((size_t)size, sizeof(long));
}

/* the slot of a key: its own, or the free one where it goes */
static long bake_pair_slot(unsigned long key)
{
	unsigned long h = key * 2654435761UL;
	long slot = (long)((h >> 7) % (unsigned long)pair_table.size);

	while (pair_table.key[slot] && pair_table.key[slot] != key)
		slot = (slot + 1) % pair_table.size;
	return slot;
}

static long bake_pair_find(long from, long to)
{
	unsigned long key = (unsigned long)from * 65536UL + (unsigned long)to + 1;
	long slot = bake_pair_slot(key);

	return pair_table.key[slot] == key ? pair_table.value[slot] : -1;
}

static void bake_pair_set(long from, long to, long value)
{
	unsigned long key = (unsigned long)from * 65536UL + (unsigned long)to + 1;
	long slot = bake_pair_slot(key);

	pair_table.key[slot] = key;
	pair_table.value[slot] = value;
}

/* a walk link for each pair of areas that fine walk links join: the pair of nodes nearest the two anchors; and each has its mirror */
static void bake_area_walk_links(void)
{
	long index, made = 0, count;

	bake_pair_table_new(1 << 18);
	for (index = 0; index < grid.count; index++)
	{
		int d;

		for (d = 0; d < 8; d++)
		{
			long other = grid.nodes[index].neighbor[d];
			long a = grid.nodes[index].area, b, found;
			struct bake_link *link;
			float score, existing;

			if (other < 0)
				continue;
			b = grid.nodes[other].area;
			if (a == b)
				continue;
			score = bake_dist3(grid.nodes[index].p, areas.areas[a].anchor) + bake_dist3(grid.nodes[other].p, areas.areas[b].anchor);
			found = bake_pair_find(a, b);
			if (found >= 0)
			{
				link = &edges.links[found];
				existing = bake_dist3(link->exit, areas.areas[a].anchor) + bake_dist3(link->entry, areas.areas[b].anchor);
				if (existing <= score)
					continue;
			}
			else
			{
				link = bake_link_new();
				if (!link)
					return;
				link->from = a;
				link->to = b;
				link->type = _bots_nav_link_walk;
				bake_pair_set(a, b, edges.count - 1);
				made++;
			}
			memcpy(link->exit, grid.nodes[index].p, sizeof(link->exit));
			memcpy(link->entry, grid.nodes[other].p, sizeof(link->entry));
			link->cost_q = bake_cost_q((bake_dist3(areas.areas[a].anchor, link->exit) + bake_dist3(link->exit, link->entry) +
				bake_dist3(link->entry, areas.areas[b].anchor)) / bake.run_speed);
		}
	}
	/* (the links are made from both ends' fine links, so the mirror exists where the fine link was seen from the other node too) */
	count = edges.count;
	for (index = 0; index < count; index++)
	{
		struct bake_link forward = edges.links[index];

		if (bake_pair_find(forward.to, forward.from) < 0)
		{
			struct bake_link *back = bake_link_new();

			if (!back)
				return;
			back->from = forward.to;
			back->to = forward.from;
			back->type = _bots_nav_link_walk;
			memcpy(back->exit, forward.entry, sizeof(back->exit));
			memcpy(back->entry, forward.exit, sizeof(back->entry));
			back->cost_q = forward.cost_q;
			bake_pair_set(back->from, back->to, edges.count - 1);
			made++;
		}
	}
	platform_log("bots: bake: %ld walk links between %ld areas", made, areas.count);
}


/* ---------- flights (jumps and drops) */

/* a biped run from `start` in direction `yaw` at `speed` (units a tick), jumping at the edge or not: its feet and its head swept tick by
tick under the game's gravity; TRUE with where it lands (a floor under the slope limit), the ticks it flew and its speed down at the
landing (units a tick) */
static int bake_flight(float const *start, float yaw, float speed, int jump, float *landing, long *ticks, float *landing_speed)
{
	float velocity[3] = { (float)cos(yaw) * speed, (float)sin(yaw) * speed, jump ? bake.jump_velocity : 0.0f };
	float feet[3] = { start[0] + velocity[0] * 0.5f, start[1] + velocity[1] * 0.5f, start[2] + bake.radius + 0.03f };
	float const floor_cos = 1.0f / (float)sqrt(1.0f + bake.slope_tan * bake.slope_tan);
	long tick;

	for (tick = 1; tick < 150; tick++)
	{
		float next[3], head_from[3], head_to[3];
		struct collision_result collision;
		real_point3d point;
		real_vector3d vector;

		velocity[2] -= bake.gravity;
		next[0] = feet[0] + velocity[0];
		next[1] = feet[1] + velocity[1];
		next[2] = feet[2] + velocity[2];
		point.x = feet[0];
		point.y = feet[1];
		point.z = feet[2];
		vector.i = velocity[0];
		vector.j = velocity[1];
		vector.k = velocity[2];
		bake.cast_count++;
		if (collision_test_pill(BAKE_SOLID_FLAGS, &point, &vector, bake.radius, NONE, &collision))
		{
			if (collision.type == _collision_result_structure && velocity[2] < 0.0f && collision.plane.n.k >= floor_cos)
			{
				landing[0] = collision.point.x;
				landing[1] = collision.point.y;
				landing[2] = collision.point.z;
				*ticks = tick;
				*landing_speed = -velocity[2];
				return 1;
			}
			return 0;
		}
		/* (the head, a biped's height above the feet, going up: a ceiling stops a jump) */
		if (velocity[2] > 0.0f)
		{
			head_from[0] = feet[0];
			head_from[1] = feet[1];
			head_from[2] = feet[2] + bake.height - 2.0f * bake.radius;
			head_to[0] = next[0];
			head_to[1] = next[1];
			head_to[2] = next[2] + bake.height - 2.0f * bake.radius;
			if (!bake_sphere_clear(head_from, head_to, bake.radius))
				return 0;
		}
		feet[0] = next[0];
		feet[1] = next[1];
		feet[2] = next[2];
		if (feet[2] < grid.bounds_min[2] - 2.0f)
			return 0;
	}
	return 0;
}

static long bake_add_script(float const *launch, float yaw, float run_up, long ticks, int jump, int successes)
{
	struct bots_nav_script *script;

	if (edges.script_count >= edges.script_capacity)
	{
		long capacity = edges.script_capacity ? edges.script_capacity * 2 : 256;
		struct bots_nav_script *list = (struct bots_nav_script *)realloc(edges.scripts, (size_t)capacity * sizeof(*list));

		if (!list)
			return 0xFFFF;
		edges.scripts = list;
		edges.script_capacity = capacity;
	}
	script = &edges.scripts[edges.script_count];
	memset(script, 0, sizeof(*script));
	memcpy(script->launch, launch, sizeof(script->launch));
	script->yaw = yaw;
	script->run_up = run_up;
	script->flight_ticks = (unsigned short)(ticks > 65535 ? 65535 : ticks);
	script->jump = (unsigned char)jump;
	script->successes = (unsigned char)successes;
	script->attempts = 3;
	return edges.script_count++;
}

/* the node nearest a landing point on a floor (within 0.8 in the plane and 0.4 in height); -1 for none */
static long bake_node_at(float const *p)
{
	long cx = (long)floor((p[0] - grid.origin[0]) / bake.cell), cy = (long)floor((p[1] - grid.origin[1]) / bake.cell), best = -1;
	long dx, dy;
	float best_distance = 1e30f;

	for (dy = -1; dy <= 1; dy++)
		for (dx = -1; dx <= 1; dx++)
		{
			long cell_x = cx + dx, cell_y = cy + dy, k;

			if (cell_x < 0 || cell_y < 0 || cell_x >= grid.cells_x || cell_y >= grid.cells_y)
				continue;
			for (k = grid.cell_start[cell_y * grid.cells_x + cell_x]; k < grid.cell_start[cell_y * grid.cells_x + cell_x + 1]; k++)
			{
				float ex = grid.nodes[k].p[0] - p[0], ey = grid.nodes[k].p[1] - p[1], ez = grid.nodes[k].p[2] - p[2];
				float distance = (float)sqrt(ex * ex + ey * ey);

				if (distance < 0.8f && (float)fabs(ez) < 0.4f && distance + (float)fabs(ez) < best_distance)
				{
					best_distance = distance + (float)fabs(ez);
					best = k;
				}
			}
		}
	return best;
}

/* the flight links: from each node at the edge of the floor (a walk behind it, none ahead), running in the direction of the edge, a drop
(walking off) and a jump; kept when they land on a node of another area, survivably, and again at a tenth more and a tenth less of the
speed (two of the three: risky). One flight link for a pair of areas */
static void bake_make_flights(long stride)
{
	long index, tried = 0, drops = 0, jumps = 0, unsafe = 0, same_area = 0, no_floor = 0, inconsistent = 0, duplicates = 0, risky = 0;
	float const speed = bake.run_speed / (float)TICKS_PER_SECOND;
	float const safe_speed = bake.drop_height > 0.0f ? (float)sqrt(2.0f * bake.gravity * bake.drop_height) : 0.0f;
	static float const speeds[3] = { 1.0f, 0.9f, 1.1f };

	for (index = 0; index < grid.count; index++)
	{
		struct bake_node *node = &grid.nodes[index];
		int d;

		if (stride > 1 && ((node->cx + node->cy) % stride) != 0)
			continue;
		for (d = 0; d < 8; d++)
		{
			float yaw = (float)atan2((double)direction_y[d], (double)direction_x[d]);
			long behind = node->neighbor[(d + 4) & 7];
			int mode;

			/* (an edge: a run-up behind it and no walk ahead) */
			if (node->neighbor[d] >= 0 || behind < 0)
				continue;
			for (mode = 0; mode < 2; mode++)
			{
				float landed[3];
				long ticks = 0, trial, target = -1, target_node = -1, successes = 0;
				float landing_speed = 0.0f;
				struct bake_link *link;
				long script;

				tried++;
				for (trial = 0; trial < 3; trial++)
				{
					float where[3], fell;
					long flown, other;

					if (!bake_flight(node->p, yaw, speed * speeds[trial], mode, where, &flown, &fell))
					{
						if (trial == 0)
						{
							no_floor++;
							break;
						}
						continue;
					}
					other = bake_node_at(where);
					if (other < 0)
					{
						if (trial == 0)
						{
							no_floor++;
							break;
						}
						continue;
					}
					if (trial == 0)
					{
						target = grid.nodes[other].area;
						target_node = other;
						memcpy(landed, where, sizeof(landed));
						ticks = flown;
						landing_speed = fell;
						if (target == node->area)
						{
							same_area++;
							break;
						}
						if (landing_speed > safe_speed)
						{
							unsafe++;
							break;
						}
						successes = 1;
					}
					else if (grid.nodes[other].area == target)
						successes++;
				}
				if (successes == 0)
					continue;
				if (successes < 2)
				{
					inconsistent++;
					continue;
				}
				if (bake_pair_find(node->area, target) >= 0)
				{
					duplicates++;
					continue;
				}
				link = bake_link_new();
				if (!link)
					return;
				link->from = node->area;
				link->to = target;
				link->type = mode ? _bots_nav_link_jump : _bots_nav_link_drop;
				link->flags = _bots_nav_link_flag_one_way | (successes < 3 ? _bots_nav_link_flag_risky : 0);
				memcpy(link->exit, node->p, sizeof(link->exit));
				memcpy(link->entry, grid.nodes[target_node].p, sizeof(link->entry));
				script = bake_add_script(grid.nodes[behind].p, yaw, bake_dist3(grid.nodes[behind].p, node->p), ticks, mode, (int)successes);
				link->script = script;
				link->cost_q = bake_cost_q((bake_dist3(areas.areas[node->area].anchor, link->exit) +
					bake_dist3(link->entry, areas.areas[target].anchor)) / bake.run_speed + (float)ticks / (float)TICKS_PER_SECOND + 0.3f);
				bake_pair_set(node->area, target, edges.count - 1);
				if (mode)
					jumps++;
				else
					drops++;
				if (successes < 3)
					risky++;
				(void)landed;
			}
		}
	}
	platform_log("bots: bake: flights tried %ld: %ld drops, %ld jumps (%ld risky); refused: %ld landed nowhere, %ld in the same area, %ld too hard a fall, %ld inconsistent, %ld duplicates",
		tried, drops, jumps, risky, no_floor, same_area, unsafe, inconsistent, duplicates);
}

/* ---------- the scenario's places */

static struct
{
	struct bots_nav_place *places;
	long count, capacity;
} places;

static struct bots_nav_place *bake_place_new(int type)
{
	struct bots_nav_place *place;

	if (places.count >= places.capacity)
	{
		long capacity = places.capacity ? places.capacity * 2 : 256;
		struct bots_nav_place *list = (struct bots_nav_place *)realloc(places.places, (size_t)capacity * sizeof(*list));

		if (!list)
			return NULL;
		places.places = list;
		places.capacity = capacity;
	}
	place = &places.places[places.count++];
	memset(place, 0, sizeof(*place));
	place->type = (unsigned char)type;
	place->area = 0xFFFF;
	return place;
}

/* the area of a point on the floor: the node nearest it (within 1.5 across and 1 in height), else the nearest anchor within 6 */
static long bake_area_of(float const *p)
{
	long cx = (long)floor((p[0] - grid.origin[0]) / bake.cell), cy = (long)floor((p[1] - grid.origin[1]) / bake.cell), best = -1, dx, dy;
	float best_distance = 1e30f;

	for (dy = -2; dy <= 2; dy++)
		for (dx = -2; dx <= 2; dx++)
		{
			long cell_x = cx + dx, cell_y = cy + dy, k;

			if (cell_x < 0 || cell_y < 0 || cell_x >= grid.cells_x || cell_y >= grid.cells_y)
				continue;
			for (k = grid.cell_start[cell_y * grid.cells_x + cell_x]; k < grid.cell_start[cell_y * grid.cells_x + cell_x + 1]; k++)
			{
				float ex = grid.nodes[k].p[0] - p[0], ey = grid.nodes[k].p[1] - p[1], ez = grid.nodes[k].p[2] - p[2];
				float distance = (float)sqrt(ex * ex + ey * ey);

				if (distance < 1.5f && (float)fabs(ez) < 1.0f && distance + 2.0f * (float)fabs(ez) < best_distance)
				{
					best_distance = distance + 2.0f * (float)fabs(ez);
					best = grid.nodes[k].area;
				}
			}
		}
	if (best >= 0)
		return best;
	{
		long area;

		for (area = 0; area < areas.count; area++)
		{
			float distance = bake_dist3(areas.areas[area].anchor, p);

			if (distance < 6.0f && distance < best_distance)
			{
				best_distance = distance;
				best = area;
			}
		}
	}
	return best;
}

static unsigned short bake_item_class(long collection_tag_index)
{
	char name[256];
	char const *tag_name;
	long k;
	struct item_collection_definition *collection;

	if (collection_tag_index == NONE)
		return _bots_nav_item_unknown;
	/* (the collection's name, then the first permutation's item: "weapons\rocket launcher\rocket launcher") */
	tag_name = tag_get_name(collection_tag_index);
	snprintf(name, sizeof(name), "%s", tag_name ? tag_name : "");
	collection = item_collection_definition_get(collection_tag_index);
	if (collection && collection->permutations.count > 0)
	{
		struct item_permutation_definition *permutation = TAG_BLOCK_GET_ELEMENT(&collection->permutations, 0, struct item_permutation_definition);
		char const *item_name = permutation->item.index != NONE ? tag_get_name(permutation->item.index) : NULL;

		if (item_name)
		{
			size_t length = strlen(name);

			snprintf(name + length, sizeof(name) - length, " %s", item_name);
		}
	}
	for (k = 0; name[k]; k++)
		if (name[k] >= 'A' && name[k] <= 'Z')
			name[k] = (char)(name[k] - 'A' + 'a');
	if (strstr(name, "rocket") || strstr(name, "sniper") || strstr(name, "flamethrower") || strstr(name, "fuel rod") ||
		strstr(name, "fuel_rod") || strstr(name, "plasma cannon") || strstr(name, "plasma_cannon"))
	{
		return _bots_nav_item_power_weapon;
	}
	if (strstr(name, "overshield") || strstr(name, "camo") || strstr(name, "powerup") || strstr(name, "over shield"))
		return _bots_nav_item_powerup;
	if (strstr(name, "grenade"))
		return _bots_nav_item_grenades;
	if (strstr(name, "ammo") || strstr(name, "clip") || strstr(name, "health"))
		return _bots_nav_item_ammo;
	if (strstr(name, "weapon") || strstr(name, "rifle") || strstr(name, "pistol") || strstr(name, "shotgun") || strstr(name, "needler") ||
		strstr(name, "plasma"))
	{
		return _bots_nav_item_weapon;
	}
	return _bots_nav_item_unknown;
}

static void bake_make_places(void)
{
	struct scenario *scenario = global_scenario_get();
	long index, missing = 0, source_flags[2][64];
	long teleports = 0;

	memset(source_flags, 0, sizeof(source_flags));
	if (!scenario)
		return;
	for (index = 0; index < scenario->netgame_equipment.count; index++)
	{
		struct scenario_netgame_equipment *equipment = TAG_BLOCK_GET_ELEMENT(&scenario->netgame_equipment, index, struct scenario_netgame_equipment);
		struct bots_nav_place *place = bake_place_new(_bots_nav_place_item);
		float p[3];

		if (!place)
			return;
		place->x = p[0] = equipment->position.x;
		place->y = p[1] = equipment->position.y;
		place->z = p[2] = equipment->position.z;
		place->team = (unsigned char)(equipment->team_index < 0 ? 0 : equipment->team_index + 1);
		place->period_seconds = (unsigned short)equipment->spawn_time;
		if (equipment->spawn_time == 0 && equipment->item_collection.index != NONE)
		{
			struct item_collection_definition *collection = item_collection_definition_get(equipment->item_collection.index);

			place->period_seconds = (unsigned short)(collection->spawn_time ? collection->spawn_time : 30);
		}
		else if (equipment->spawn_time == 0)
			place->period_seconds = 30;
		place->item_class = bake_item_class(equipment->item_collection.index);
		place->source = (unsigned long)index;
		place->area = (unsigned short)(bake_area_of(p) < 0 ? 0xFFFF : bake_area_of(p));
		if (place->area == 0xFFFF)
			missing++;
	}
	for (index = 0; index < scenario->netgame_flags.count; index++)
	{
		struct scenario_netgame_flag *flag = TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, index, struct scenario_netgame_flag);
		int type;
		struct bots_nav_place *place;
		float p[3];

		switch (flag->type)
		{
		case _netgame_flag_ctf_flag: type = _bots_nav_place_flag_stand; break;
		case _netgame_flag_oddball_ball_spawn: type = _bots_nav_place_ball_spawn; break;
		case _netgame_flag_hill: type = _bots_nav_place_hill_point; break;
		case _netgame_flag_teleporter_source: type = _bots_nav_place_teleport_source; break;
		case _netgame_flag_teleporter_target: type = _bots_nav_place_teleport_target; break;
		case _netgame_flag_race_track: type = _bots_nav_place_race_point; break;
		case _netgame_flag_race_vehicle:
		case _netgame_flag_ctf_vehicle: type = _bots_nav_place_vehicle; break;
		default: continue;
		}
		place = bake_place_new(type);
		if (!place)
			return;
		place->x = p[0] = flag->position.x;
		place->y = p[1] = flag->position.y;
		place->z = p[2] = flag->position.z;
		place->team = (unsigned char)(flag->team_index < 0 ? 0 : flag->team_index + 1);
		place->aux = (unsigned short)(flag->team_index < 0 ? 0 : flag->team_index);
		place->source = (unsigned long)index;
		place->area = (unsigned short)(bake_area_of(p) < 0 ? 0xFFFF : bake_area_of(p));
		if (place->area == 0xFFFF)
			missing++;
	}
	for (index = 0; index < scenario->players.count; index++)
	{
		struct player_starting_location *location = TAG_BLOCK_GET_ELEMENT(&scenario->players, index, struct player_starting_location);
		struct bots_nav_place *place = bake_place_new(_bots_nav_place_start);
		float p[3];

		if (!place)
			return;
		place->x = p[0] = location->position.x;
		place->y = p[1] = location->position.y;
		place->z = p[2] = location->position.z;
		place->team = (unsigned char)(location->team_index < 0 ? 0 : location->team_index + 1);
		place->source = (unsigned long)index;
		place->area = (unsigned short)(bake_area_of(p) < 0 ? 0xFFFF : bake_area_of(p));
		if (place->area == 0xFFFF)
			missing++;
	}
	/* (teleporters: the source flag's area to the target flag's of the same pair id: the flags' team index) */
	{
		long source;

		for (source = 0; source < places.count; source++)
		{
			long target;

			if (places.places[source].type != _bots_nav_place_teleport_source || places.places[source].area == 0xFFFF)
				continue;
			for (target = 0; target < places.count; target++)
			{
				struct bake_link *link;
				float exit_point[3], entry_point[3];

				if (places.places[target].type != _bots_nav_place_teleport_target || places.places[target].aux != places.places[source].aux ||
					places.places[target].area == 0xFFFF || places.places[target].area == places.places[source].area)
				{
					continue;
				}
				exit_point[0] = places.places[source].x;
				exit_point[1] = places.places[source].y;
				exit_point[2] = places.places[source].z;
				entry_point[0] = places.places[target].x;
				entry_point[1] = places.places[target].y;
				entry_point[2] = places.places[target].z;
				link = bake_link_new();
				if (!link)
					return;
				link->from = places.places[source].area;
				link->to = places.places[target].area;
				link->type = _bots_nav_link_teleport;
				link->flags = _bots_nav_link_flag_one_way;
				memcpy(link->exit, exit_point, sizeof(link->exit));
				memcpy(link->entry, entry_point, sizeof(link->entry));
				link->cost_q = bake_cost_q(bake_dist3(areas.areas[link->from].anchor, exit_point) / bake.run_speed + 1.0f);
				teleports++;
				break;
			}
		}
	}
	(void)source_flags;
	platform_log("bots: bake: %ld places (%ld not on the floor graph), %ld teleporter links", places.count, missing, teleports);
}

/* the engine's own view, for comparing: the walkable-flagged surfaces joined across the collision edges they share (what path.c walks), the
component of the surface under a point (a floor ray), and the size of the largest. -1 for none */
static struct
{
	long *parent;
	long count;
	long largest;
	long *sizes;
} engine_pieces;

static long engine_find(long x)
{
	while (engine_pieces.parent[x] != x)
	{
		engine_pieces.parent[x] = engine_pieces.parent[engine_pieces.parent[x]];
		x = engine_pieces.parent[x];
	}
	return x;
}

static void bake_engine_pieces(struct collision_bsp const *bsp, struct structure_bsp const *structure)
{
	long index;
	byte const *flags = (byte const *)structure->pathfinding_surfaces.address;

	engine_pieces.count = bsp->surfaces.count;
	engine_pieces.parent = (long *)malloc((size_t)bsp->surfaces.count * sizeof(long));
	engine_pieces.sizes = (long *)calloc((size_t)bsp->surfaces.count, sizeof(long));
	if (!engine_pieces.parent || !engine_pieces.sizes || !flags)
		return;
	for (index = 0; index < bsp->surfaces.count; index++)
		engine_pieces.parent[index] = index;
	for (index = 0; index < bsp->edges.count; index++)
	{
		struct collision_edge const *edge = TAG_BLOCK_GET_ELEMENT(&bsp->edges, index, struct collision_edge);
		long a = edge->surface_indices[0], b = edge->surface_indices[1];

		if (a >= 0 && b >= 0 && a < bsp->surfaces.count && b < bsp->surfaces.count && a < structure->pathfinding_surfaces.count &&
			b < structure->pathfinding_surfaces.count && TEST_FLAG(flags[a], 6) && TEST_FLAG(flags[b], 6))
		{
			long ra = engine_find(a), rb = engine_find(b);

			if (ra != rb)
				engine_pieces.parent[ra] = rb;
		}
	}
	for (index = 0; index < bsp->surfaces.count; index++)
		if (index < structure->pathfinding_surfaces.count && TEST_FLAG(flags[index], 6))
			engine_pieces.sizes[engine_find(index)]++;
	for (index = 0; index < bsp->surfaces.count; index++)
		if (engine_pieces.sizes[index] > engine_pieces.sizes[engine_pieces.largest])
			engine_pieces.largest = index;
}

/* 1 if the surface under the point is in the engine's largest piece, 0 if in another, -1 for none */
static int bake_engine_piece_of(float const *p)
{
	struct collision_result collision;
	real_point3d point = { p[0], p[1], p[2] + 0.4f };
	real_vector3d vector = { 0.0f, 0.0f, -3.0f };

	if (!engine_pieces.parent)
		return -1;
	if (!collision_test_vector(BAKE_FLOOR_FLAGS, &point, &vector, NONE, &collision) || collision.type != _collision_result_structure ||
		collision.surface_index < 0 || collision.surface_index >= engine_pieces.count)
	{
		return -1;
	}
	return engine_find(collision.surface_index) == engine_find(engine_pieces.largest) ? 1 : 0;
}

/* (debug.bots_bake_path "x y z x y z": the engine's walkable surfaces' shortest way, edge to edge, from the surface under the first point to the one under the
second, with each surface's middle and its normal's height: for finding what joins two places the floor graph does not) */
static void bake_engine_path(struct collision_bsp const *bsp, struct structure_bsp const *structure)
{
	char const *given = config_string("debug.bots_bake_path");
	float a[3], b[3];
	long count = bsp->surfaces.count, *parent, *queue, head = 0, tail = 0, from, to, index;
	byte const *flags = (byte const *)structure->pathfinding_surfaces.address;
	struct collision_result collision;
	real_point3d point;
	real_vector3d vector = { 0.0f, 0.0f, -3.0f };

	if (!given || !given[0] || sscanf(given, "%f %f %f %f %f %f", &a[0], &a[1], &a[2], &b[0], &b[1], &b[2]) != 6 || !flags)
		return;
	point.x = a[0]; point.y = a[1]; point.z = a[2] + 0.4f;
	if (!collision_test_vector(BAKE_FLOOR_FLAGS, &point, &vector, NONE, &collision))
		return;
	from = collision.surface_index;
	point.x = b[0]; point.y = b[1]; point.z = b[2] + 0.4f;
	if (!collision_test_vector(BAKE_FLOOR_FLAGS, &point, &vector, NONE, &collision))
		return;
	to = collision.surface_index;
	parent = (long *)malloc((size_t)count * sizeof(long));
	queue = (long *)malloc((size_t)count * sizeof(long));
	for (index = 0; index < count; index++)
		parent[index] = -2;
	parent[from] = -1;
	queue[tail++] = from;
	while (head < tail && parent[to] == -2)
	{
		long surface = queue[head++];
		struct collision_surface const *s = TAG_BLOCK_GET_ELEMENT(&bsp->surfaces, surface, struct collision_surface);
		long edge_index = s->first_edge_index, guard = 0;

		do
		{
			struct collision_edge const *edge = TAG_BLOCK_GET_ELEMENT(&bsp->edges, edge_index, struct collision_edge);
			boolean right = edge->surface_indices[1] == surface;
			long other = edge->surface_indices[right ? 0 : 1];

			if (other >= 0 && other < count && other < structure->pathfinding_surfaces.count && TEST_FLAG(flags[other], 6) && parent[other] == -2)
			{
				parent[other] = surface;
				queue[tail++] = other;
			}
			edge_index = edge->edge_indices[right];
		} while (edge_index != s->first_edge_index && ++guard < 64);
	}
	platform_log("bots: bake: engine path from surface %ld to %ld: %s", from, to, parent[to] == -2 ? "none" : "found");
	if (parent[to] != -2)
	{
		long chain[4000], n = 0, k;

		for (k = to; k != -1 && n < 4000; k = parent[k])
			chain[n++] = k;
		for (k = n - 1; k >= 0; k--)
		{
			real_point3d corners[BAKE_CORNERS];
			long c = bake_surface_corners(bsp, chain[k], corners), q;
			float mx = 0, my = 0, mz = 0, nz = 0;

			for (q = 0; q < c; q++)
			{
				mx += corners[q].x / (float)c;
				my += corners[q].y / (float)c;
				mz += corners[q].z / (float)c;
				nz += (float)(((double)corners[q].x - corners[(q + 1) % c].x) * ((double)corners[q].y + corners[(q + 1) % c].y));
			}
			if (k % 3 == 0 || k == n - 1 || k == 0)
				platform_log("bots: bake: engine path %ld: surface %ld (%.1f %.1f %.1f) corners %ld", n - 1 - k, chain[k], mx, my, mz, c);
		}
	}
	free(parent);
	free(queue);
}

/* ---------- the result */

static int bake_link_compare(void const *a, void const *b)
{
	struct bake_link const *x = (struct bake_link const *)a, *y = (struct bake_link const *)b;

	if (x->from != y->from)
		return x->from < y->from ? -1 : 1;
	if (x->to != y->to)
		return x->to < y->to ? -1 : 1;
	return (int)x->type - (int)y->type;
}

/* the graph's pieces over its links taken both ways (a jump or a drop joins as a walk does: the report says what could be reached if a bot may
go either way), by union-find; piece[area] is the root */
static long bake_graph_root(long *parent, long x)
{
	while (parent[x] != x)
	{
		parent[x] = parent[parent[x]];
		x = parent[x];
	}
	return x;
}

static void bake_graph_pieces(struct bots_nav const *nav, long *parent)
{
	long area, k;

	for (area = 0; area < nav->area_count; area++)
		parent[area] = area;
	for (area = 0; area < nav->area_count; area++)
	{
		struct bots_nav_area const *a = &nav->areas[area];

		for (k = 0; k < a->link_count; k++)
		{
			long ra = bake_graph_root(parent, area), rb = bake_graph_root(parent, nav->links[a->first_link + k].to);

			if (ra != rb)
				parent[ra] = rb;
		}
	}
}

/* the areas that no place is joined to are of no use to a bot (the hull of a ship, the far side of a wall): dropped, the rest numbered again */
static void bake_prune(struct bots_nav *nav)
{
	long *parent = (long *)malloc((size_t)nav->area_count * sizeof(long)), *keep_root = (long *)calloc((size_t)nav->area_count, sizeof(long));
	long *remap = (long *)malloc((size_t)nav->area_count * sizeof(long)), *script_remap = (long *)malloc(((size_t)nav->script_count + 1) * sizeof(long));
	long area, k, kept_areas = 0, kept_links = 0, kept_scripts = 0, old_areas = nav->area_count, old_links = nav->link_count;
	struct bots_nav_area *areas_out;
	struct bots_nav_link *links_out;
	struct bots_nav_script *scripts_out;

	if (!parent || !keep_root || !remap || !script_remap)
		goto done;
	bake_graph_pieces(nav, parent);
	for (k = 0; k < nav->place_count; k++)
		if (nav->places[k].area != 0xFFFF)
			keep_root[bake_graph_root(parent, nav->places[k].area)] = 1;
	for (area = 0; area < nav->area_count; area++)
		remap[area] = keep_root[bake_graph_root(parent, area)] ? kept_areas++ : -1;
	areas_out = (struct bots_nav_area *)calloc((size_t)kept_areas + 1, sizeof(*areas_out));
	links_out = (struct bots_nav_link *)calloc((size_t)nav->link_count + 1, sizeof(*links_out));
	scripts_out = (struct bots_nav_script *)calloc((size_t)nav->script_count + 1, sizeof(*scripts_out));
	if (!areas_out || !links_out || !scripts_out)
		goto done;
	for (k = 0; k < nav->script_count + 1; k++)
		script_remap[k] = -1;
	for (area = 0; area < nav->area_count; area++)
	{
		struct bots_nav_area const *a = &nav->areas[area];

		if (remap[area] < 0)
			continue;
		areas_out[remap[area]] = *a;
		areas_out[remap[area]].first_link = (unsigned short)kept_links;
		areas_out[remap[area]].link_count = 0;
		for (k = 0; k < a->link_count; k++)
		{
			struct bots_nav_link link = nav->links[a->first_link + k];

			if (remap[link.to] < 0)
				continue;
			link.to = (unsigned short)remap[link.to];
			if (link.script != 0xFFFF)
			{
				if (script_remap[link.script] < 0)
				{
					scripts_out[kept_scripts] = nav->scripts[link.script];
					script_remap[link.script] = kept_scripts++;
				}
				link.script = (unsigned short)script_remap[link.script];
			}
			links_out[kept_links++] = link;
			areas_out[remap[area]].link_count++;
		}
	}
	for (k = 0; k < nav->place_count; k++)
		if (nav->places[k].area != 0xFFFF)
			nav->places[k].area = remap[nav->places[k].area] < 0 ? 0xFFFF : (unsigned short)remap[nav->places[k].area];
	free(nav->areas);
	free(nav->links);
	nav->areas = areas_out;
	nav->links = links_out;
	nav->area_count = kept_areas;
	nav->link_count = kept_links;
	/* (the scripts are edges.scripts': the kept ones are copied back, in order) */
	memcpy(nav->scripts, scripts_out, (size_t)kept_scripts * sizeof(*scripts_out));
	free(scripts_out);
	nav->script_count = kept_scripts;
	platform_log("bots: bake: pruned the areas no place is joined to: %ld of %ld areas and %ld of %ld links kept", kept_areas, old_areas, kept_links, old_links);
done:
	free(parent);
	free(keep_root);
	free(remap);
	free(script_remap);
}

/* the graph, summed up: the pieces over its links, which hold the places (starts, items, flags, hills): the bake's own check */
static void bake_report_connectivity(struct bots_nav const *nav)
{
	long *parent = (long *)malloc((size_t)nav->area_count * sizeof(long)), *places_in = (long *)calloc((size_t)nav->area_count, sizeof(long));
	long *size = (long *)calloc((size_t)nav->area_count, sizeof(long));
	long area, k, pieces = 0, placed = 0, best = -1, with_places = 0, outside_logged = 0;

	if (!parent || !places_in || !size)
		goto done;
	bake_graph_pieces(nav, parent);
	for (area = 0; area < nav->area_count; area++)
		size[bake_graph_root(parent, area)]++;
	for (k = 0; k < nav->place_count; k++)
		if (nav->places[k].area != 0xFFFF)
		{
			places_in[bake_graph_root(parent, nav->places[k].area)]++;
			placed++;
		}
	for (area = 0; area < nav->area_count; area++)
		if (parent[area] == area)
		{
			pieces++;
			if (places_in[area])
				with_places++;
			if (best < 0 || places_in[area] > places_in[best])
				best = area;
		}
	platform_log("bots: bake: %ld pieces of the graph (links taken both ways), %ld hold places; the one with most holds %ld of the %ld placed places, in %ld of %ld areas", pieces,
		with_places, best >= 0 ? places_in[best] : 0L, placed, best >= 0 ? size[best] : 0L, nav->area_count);
	for (k = 0; k < nav->place_count && best >= 0; k++)
	{
		if (nav->places[k].area == 0xFFFF || bake_graph_root(parent, nav->places[k].area) == best || outside_logged >= 40)
			continue;
		outside_logged++;
		{
			float where[3] = { nav->places[k].x, nav->places[k].y, nav->places[k].z };

			platform_log("bots: bake: place %ld (type %d team %d) at (%.1f %.1f %.1f) is in area %d, in a piece of %ld areas with %ld places (the engine's surfaces put it %s)", k,
				(int)nav->places[k].type, (int)nav->places[k].team, nav->places[k].x, nav->places[k].y, nav->places[k].z, (int)nav->places[k].area,
				size[bake_graph_root(parent, nav->places[k].area)], places_in[bake_graph_root(parent, nav->places[k].area)],
				bake_engine_piece_of(where) > 0 ? "in its largest piece" : bake_engine_piece_of(where) == 0 ? "in another piece" : "on no walkable surface");
		}
	}
done:
	free(parent);
	free(places_in);
	free(size);
}

/* writes the pack of one map (see bots_nav_write_pack) to path */
static int bake_write(struct bots_nav *nav, char const *path, unsigned long surfaces, unsigned long walkable)
{
	size_t record_count, pack_count;
	unsigned char *record = bots_nav_write_record(nav, &record_count), *pack;
	FILE *file;

	if (!record)
		return 0;
	pack = bots_nav_write_pack(nav->name, surfaces, walkable, record, record_count, 1, &pack_count);
	free(record);
	if (!pack)
		return 0;
	file = fopen(path, "wb");
	if (!file)
	{
		free(pack);
		return 0;
	}
	fwrite(pack, 1, pack_count, file);
	fclose(file);
	free(pack);
	platform_log("bots: bake: wrote %s: %lu bytes (%ld areas, %ld links, %ld scripts, %ld places)", path, (unsigned long)pack_count, nav->area_count,
		nav->link_count, nav->script_count, nav->place_count);
	return 1;
}

int bots_nav_bake_run(long unit_index, char const *map_name, char const *path)
{
	long long started = platform_microseconds(), stage = started;
	struct scenario *scenario = global_scenario_get();
	struct structure_bsp *structure = global_structure_bsp_get();
	struct collision_bsp const *bsp;
	struct object_datum *object = object_get(unit_index);
	struct biped_definition *biped = biped_definition_get(object->definition_index);
	struct game_globals_player_information *player_information;
	struct bake_falling_damage *falling;
	struct bots_nav nav;
	long index, surfaces, walkable = 0, cell_hundredths = config_integer("debug.bots_bake_cell"), reach_tenths = config_integer("debug.bots_bake_reach");
	long flight_stride = config_integer("debug.bots_bake_stride");
	int upward_sign = 1;
	float low[3] = { 1e30f, 1e30f, 1e30f }, high[3] = { -1e30f, -1e30f, -1e30f };

	if (!scenario || !structure || structure->collision_bsp.count <= 0)
		return 0;
	bsp = TAG_BLOCK_GET_ELEMENT(&structure->collision_bsp, 0, struct collision_bsp);
	memset(&bake, 0, sizeof(bake));
	memset(&grid, 0, sizeof(grid));
	memset(&areas, 0, sizeof(areas));
	memset(&edges, 0, sizeof(edges));
	memset(&places, 0, sizeof(places));

	/* the biped */
	player_information = TAG_BLOCK_GET_ELEMENT(&scenario_get_game_globals()->player_information, 0, struct game_globals_player_information);
	falling = TAG_BLOCK_GET_ELEMENT(&scenario_get_game_globals()->falling_damage, 0, struct bake_falling_damage);
	bake.radius = biped->biped.collision_radius > 0.0f ? biped->biped.collision_radius : 0.1f;
	bake.height = biped->biped.collision_height_standing > 0.0f ? biped->biped.collision_height_standing : 0.7f;
	{
		real slope = biped->biped.maximum_slope_angle;

		/* (the tag holds radians; a number past a quarter turn is degrees) */
		if (slope > 1.6f)
			slope = slope * PI_F / 180.0f;
		/* (the slope a bake walks: the biped's, but not past 50 degrees, where the engine's own flag stops being reliable underfoot) */
		slope = MIN(slope, 50.0f * PI_F / 180.0f);
		if (slope < 0.3f)
			slope = 45.0f * PI_F / 180.0f;
		bake.slope_tan = (float)tan(slope);
	}
	bake.jump_velocity = biped->biped.jump_velocity;
	bake.gravity = global_gravity;
	bake.run_speed = player_information->run_forward_speed > 0.0f ? player_information->run_forward_speed : 2.25f;
	bake.step = 0.1f;
	bake.cell = cell_hundredths > 0 ? (float)cell_hundredths / 100.0f : 0.5f;
	bake.drop_height = falling->runtime_minimum_damage_velocity > 0.0f ?
		0.8f * falling->runtime_minimum_damage_velocity * falling->runtime_minimum_damage_velocity / (2.0f * bake.gravity) : 1.5f;
	platform_log("bots: bake: the biped: radius %.3f height %.3f slope tan %.3f (tag %.3f rad) jump %.4f a tick (apex %.3f) gravity %.7f run %.3f units a second, surviving a fall of %.2f (damage speed %.4f)",
		bake.radius, bake.height, bake.slope_tan, (double)biped->biped.maximum_slope_angle, bake.jump_velocity,
		bake.jump_velocity * bake.jump_velocity / (2.0f * bake.gravity), bake.gravity, bake.run_speed, bake.drop_height,
		falling->runtime_minimum_damage_velocity);

	/* the bounds and the grid */
	for (index = 0; index < bsp->vertices.count; index++)
	{
		struct collision_vertex const *vertex = TAG_BLOCK_GET_ELEMENT(&bsp->vertices, index, struct collision_vertex);

		low[0] = MIN(low[0], vertex->point.x);
		low[1] = MIN(low[1], vertex->point.y);
		low[2] = MIN(low[2], vertex->point.z);
		high[0] = MAX(high[0], vertex->point.x);
		high[1] = MAX(high[1], vertex->point.y);
		high[2] = MAX(high[2], vertex->point.z);
	}
	memcpy(grid.bounds_min, low, sizeof(low));
	memcpy(grid.bounds_max, high, sizeof(high));
	grid.origin[0] = low[0];
	grid.origin[1] = low[1];
	grid.cells_x = (long)ceil((high[0] - low[0]) / bake.cell) + 1;
	grid.cells_y = (long)ceil((high[1] - low[1]) / bake.cell) + 1;
	surfaces = bsp->surfaces.count;
	if (structure->pathfinding_surfaces.address)
	{
		for (index = 0; index < structure->pathfinding_surfaces.count; index++)
			if (TEST_FLAG(((byte const *)structure->pathfinding_surfaces.address)[index], 6))
				walkable++;
	}
	platform_log("bots: bake: map %s: bounds (%.0f %.0f %.0f) to (%.0f %.0f %.0f), %ld x %ld cells of %.2f", map_name, low[0], low[1], low[2], high[0],
		high[1], high[2], grid.cells_x, grid.cells_y, bake.cell);

	/* the winding's facing: the engine's walkable surfaces say which way up is (most of them must face up) */
	{
		long up = 0, down = 0;

		for (index = 0; index < bsp->surfaces.count && structure->pathfinding_surfaces.address; index++)
		{
			real_point3d corners[BAKE_CORNERS];
			long count, k;
			double nz = 0;

			if (index >= structure->pathfinding_surfaces.count || !TEST_FLAG(((byte const *)structure->pathfinding_surfaces.address)[index], 6))
				continue;
			count = bake_surface_corners(bsp, index, corners);
			for (k = 0; k < count && count >= 3; k++)
				nz += ((double)corners[k].x - corners[(k + 1) % count].x) * ((double)corners[k].y + corners[(k + 1) % count].y);
			if (nz > 0)
				up++;
			else
				down++;
		}
		upward_sign = up >= down ? 1 : -1;
		platform_log("bots: bake: of the engine's walkable surfaces %ld wind upward and %ld downward (sign %d)", up, down, upward_sign);
	}

	{
		/* (what a cast costs, with and without the objects: the bake's casts are thousands) */
		static unsigned long const kinds[3] = { FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_structure_bit),
			BAKE_SOLID_FLAGS, FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_back_facing_surfaces_bit) |
			FLAG(_collision_test_structure_bit) };
		long kind, trial;

		for (kind = 0; kind < 3; kind++)
		{
			long long begun = platform_microseconds();
			long hits = 0;

			for (trial = 0; trial < 200; trial++)
			{
				struct collision_result collision;
				real_point3d point = { 20.0f + (float)(trial % 20), -100.0f - (float)(trial / 20), 40.0f };
				real_vector3d vector = { 0.0f, 0.0f, -60.0f };

				if (collision_test_vector(kinds[kind], &point, &vector, NONE, &collision))
					hits++;
			}
			platform_log("bots: bake: 200 vector casts of kind %ld: %.0f us each (%ld hits)", kind, (double)(platform_microseconds() - begun) / 200.0, hits);
		}
		{
			long long begun = platform_microseconds();
			long hits = 0;

			for (trial = 0; trial < 100; trial++)
			{
				struct collision_result collision;
				real_point3d point = { 20.0f + (float)(trial % 20), -100.0f - (float)(trial / 20), 6.0f };
				real_vector3d vector = { 1.0f, 0.0f, 0.0f };

				if (collision_test_pill(BAKE_SOLID_FLAGS, &point, &vector, 0.2f, NONE, &collision))
					hits++;
			}
			platform_log("bots: bake: 100 pill casts: %.0f us each (%ld hits)", (double)(platform_microseconds() - begun) / 100.0, hits);
		}
	}
	{
		/* (debug.bots_bake_probe "x0 y0 x1 y1": the floors under a line of points, every half unit, for looking at a place that does not link) */
		char const *probe = config_string("debug.bots_bake_probe");
		float x0, y0, x1, y1, probe_step = 0.5f;

		if (probe && probe[0] && sscanf(probe, "%f %f %f %f %f", &x0, &y0, &x1, &y1, &probe_step) >= 4)
		{
			bake.watch[0] = x0;
			bake.watch[1] = y0;
			bake.watching = 1;
			long steps = (long)(sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)) / MAX(probe_step, 0.02f)), step;

			for (step = 0; step <= steps; step++)
			{
				float x = x0 + (x1 - x0) * (float)step / (float)steps, y = y0 + (y1 - y0) * (float)step / (float)steps, from = high[2] + 1.0f;
				char line[300];
				int length = 0, hits = 0;

				length = snprintf(line, sizeof(line), "bots: bake: probe (%.2f %.2f):", x, y);
				while (hits < 6)
				{
					float height, normal;

					if (!bake_floor(x, y, from, from - low[2] + 1.0f, &height, &normal))
						break;
					length += snprintf(line + length, sizeof(line) - (size_t)length, " %.2f(n%.2f)", height, normal);
					from = height - 0.05f;
					hits++;
				}
				platform_log("%s", line);
			}
		}
	}
	{
		/* (the machines: what they are and where, for deciding what the bake does with doors and lifts) */
		struct object_iterator machines;
		long machine_count = 0;

		long type_counts[32] = { 0 };

		object_iterator_new(&machines, _object_mask_all, 0);
		while (object_iterator_next(&machines))
		{
			short type = object_get(machines.index)->object.type;

			if (type >= 0 && type < 32)
				type_counts[type]++;
		}
		platform_log("bots: bake: objects by type: biped %ld vehicle %ld weapon %ld equipment %ld garbage %ld projectile %ld scenery %ld machine %ld control %ld light %ld placeholder %ld", type_counts[_object_type_biped],
			type_counts[_object_type_vehicle], type_counts[_object_type_weapon], type_counts[_object_type_equipment], type_counts[_object_type_garbage],
			type_counts[_object_type_projectile], type_counts[_object_type_scenery], type_counts[_object_type_machine], type_counts[_object_type_control],
			type_counts[_object_type_light_fixture], type_counts[_object_type_placeholder]);
		object_iterator_new(&machines, _object_mask_scenery, 0);
		while (object_iterator_next(&machines))
		{
			struct object_datum *scenery = object_get(machines.index);

			platform_log("bots: bake: scenery %s at (%.2f %.2f %.2f)", tag_get_name(scenery->definition_index), scenery->object.position.x, scenery->object.position.y,
				scenery->object.position.z);
		}
		object_iterator_new(&machines, _object_mask_machine, 0);
		while (object_iterator_next(&machines))
		{
			struct machine_datum *machine = machine_get(machines.index);
			struct machine_definition *definition = (struct machine_definition *)tag_get(MACHINE_DEFINITION_TAG, machine->definition_index);

			machine_count++;
			platform_log("bots: bake: machine %ld %s type %d flags %x position (%.2f %.2f %.2f) power %.2f open %.2f", machine_count - 1, tag_get_name(machine->definition_index),
				(int)definition->machine.type, (unsigned)definition->machine.flags, machine->object.position.x, machine->object.position.y, machine->object.position.z,
				(double)machine->device.power, (double)machine->device.position);
		}
	}
	{
		/* (debug.bots_bake_segment "x0 y0 z0 x1 y1 z1": the walk test of the bake from one floor point to another, step by step, with the reason it says no) */
		char const *segment = config_string("debug.bots_bake_segment");
		float a[3], b[3];

		if (segment && segment[0] && sscanf(segment, "%f %f %f %f %f %f", &a[0], &a[1], &a[2], &b[0], &b[1], &b[2]) == 6)
		{
			float spacing = bake.cell * 0.55f, dx = b[0] - a[0], dy = b[1] - a[1], distance = (float)sqrt(dx * dx + dy * dy);
			long steps = (long)ceil(distance / spacing), step;

			for (step = 0; step <= steps; step++)
			{
				float t = (float)step / (float)steps, height = 0.0f, normal = 0.0f;
				int found = bake_floor(a[0] + dx * t, a[1] + dy * t, a[2] + bake.step + 0.45f, 0.45f + 2.0f * bake.step + 0.5f, &height, &normal);

				platform_log("bots: bake: segment sample %ld (%.2f %.2f): floor %s %.3f (normal %.2f)", step, a[0] + dx * t, a[1] + dy * t, found ? "at" : "none", height, normal);
			}
			platform_log("bots: bake: segment (%.2f %.2f %.2f) to (%.2f %.2f %.2f): %s (reason %d)", a[0], a[1], a[2], b[0], b[1], b[2],
				bake_segment_walkable_inner(a, b, spacing) ? "walkable" : "refused", bake.last_reason);
		}
	}
	{
		/* (the box of the places: items, flags, balls, hills, starts, teleporters, with a margin; debug.bots_bake_margin tenths of a unit, 0 for the whole map) */
		long margin_tenths = config_integer("debug.bots_bake_margin");
		float lo[2] = { 1e30f, 1e30f }, hi[2] = { -1e30f, -1e30f };
		long k;

		for (k = 0; k < scenario->netgame_equipment.count; k++)
		{
			real_point3d const *q = &TAG_BLOCK_GET_ELEMENT(&scenario->netgame_equipment, k, struct scenario_netgame_equipment)->position;

			lo[0] = MIN(lo[0], q->x); lo[1] = MIN(lo[1], q->y); hi[0] = MAX(hi[0], q->x); hi[1] = MAX(hi[1], q->y);
		}
		for (k = 0; k < scenario->netgame_flags.count; k++)
		{
			real_point3d const *q = &TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, k, struct scenario_netgame_flag)->position;

			lo[0] = MIN(lo[0], q->x); lo[1] = MIN(lo[1], q->y); hi[0] = MAX(hi[0], q->x); hi[1] = MAX(hi[1], q->y);
		}
		for (k = 0; k < scenario->players.count; k++)
		{
			real_point3d const *q = &TAG_BLOCK_GET_ELEMENT(&scenario->players, k, struct player_starting_location)->position;

			lo[0] = MIN(lo[0], q->x); lo[1] = MIN(lo[1], q->y); hi[0] = MAX(hi[0], q->x); hi[1] = MAX(hi[1], q->y);
		}
		if (margin_tenths > 0 && lo[0] < hi[0])
		{
			float margin = (float)margin_tenths / 10.0f;

			bake.play_on = 1;
			bake.play_min[0] = lo[0] - margin;
			bake.play_min[1] = lo[1] - margin;
			bake.play_max[0] = hi[0] + margin;
			bake.play_max[1] = hi[1] + margin;
			platform_log("bots: bake: the floor is made inside the box of the places and %.0f units: (%.1f %.1f) to (%.1f %.1f)", margin, bake.play_min[0], bake.play_min[1],
				bake.play_max[0], bake.play_max[1]);
		}
	}
	bake_engine_pieces(bsp, structure);
	bake_engine_path(bsp, structure);
	bake_collect_floor(bsp, structure, upward_sign);
	bake_sort_nodes();
	platform_log("bots: bake: %ld nodes sorted", grid.count);
	bake_index_cells();
	platform_log("bots: bake: cells indexed");
	bake_clearance();
	bake_index_cells();
	platform_log("bots: bake: floor nodes in %.1f s", (double)(platform_microseconds() - stage) / 1e6);
	stage = platform_microseconds();
	bake_link_nodes();
	platform_log("bots: bake: walks in %.1f s, %ld casts so far", (double)(platform_microseconds() - stage) / 1e6, bake.cast_count);
	stage = platform_microseconds();
	bake_make_areas(reach_tenths > 0 ? (float)reach_tenths / 10.0f : 6.0f);
	bake_merge_small(6, reach_tenths > 0 ? (float)reach_tenths / 10.0f : 6.0f);
	bake_compact_areas();
	platform_log("bots: bake: %ld areas in %.1f s, %ld casts so far", areas.count, (double)(platform_microseconds() - stage) / 1e6, bake.cast_count);
	stage = platform_microseconds();
	bake_area_walk_links();
	bake_make_flights(flight_stride > 0 ? flight_stride : 1);
	platform_log("bots: bake: flights in %.1f s, %ld casts in all", (double)(platform_microseconds() - stage) / 1e6, bake.cast_count);
	bake_make_places();

	/* the nav: links in order of their areas */
	qsort(edges.links, (size_t)edges.count, sizeof(struct bake_link), bake_link_compare);
	memset(&nav, 0, sizeof(nav));
	/* (the pack's name for a map: the last component of the scenario's name, in lower case: "levels\test\bloodgulch\bloodgulch" is "bloodgulch") */
	{
		char const *last = map_name, *c;
		long k;

		for (c = map_name; *c; c++)
			if (*c == '\\' || *c == '/')
				last = c + 1;
		snprintf(nav.name, sizeof(nav.name), "%s", last);
		for (k = 0; nav.name[k]; k++)
			if (nav.name[k] >= 'A' && nav.name[k] <= 'Z')
				nav.name[k] = (char)(nav.name[k] - 'A' + 'a');
	}
	nav.area_count = areas.count;
	nav.link_count = edges.count;
	nav.script_count = edges.script_count;
	nav.place_count = places.count;
	memcpy(nav.bounds_min, low, sizeof(low));
	memcpy(nav.bounds_max, high, sizeof(high));
	nav.run_speed = bake.run_speed;
	nav.jump_height = bake.jump_velocity * bake.jump_velocity / (2.0f * bake.gravity);
	nav.drop_height = bake.drop_height;
	nav.areas = (struct bots_nav_area *)calloc((size_t)areas.count, sizeof(struct bots_nav_area));
	nav.links = (struct bots_nav_link *)calloc((size_t)edges.count + 1, sizeof(struct bots_nav_link));
	nav.scripts = edges.scripts;
	nav.places = places.places;
	if (!nav.areas || !nav.links || areas.count > BOTS_NAV_MAXIMUM_AREAS || edges.count > BOTS_NAV_MAXIMUM_LINKS)
	{
		platform_log("bots: bake: %ld areas and %ld links do not fit the format", areas.count, edges.count);
		return 0;
	}
	for (index = 0; index < areas.count; index++)
	{
		struct bots_nav_area *a = &nav.areas[index];

		a->x = areas.areas[index].anchor[0];
		a->y = areas.areas[index].anchor[1];
		a->z = areas.areas[index].anchor[2];
		a->radius = areas.areas[index].radius + 0.3f;
		a->first_link = 0;
		a->link_count = 0;
	}
	for (index = 0; index < edges.count; index++)
	{
		struct bake_link const *l = &edges.links[index];
		struct bots_nav_area *a = &nav.areas[l->from];
		struct bots_nav_link *out = &nav.links[index];

		if (a->link_count == 0)
			a->first_link = (unsigned short)index;
		a->link_count++;
		out->to = (unsigned short)l->to;
		out->type = l->type;
		out->flags = l->flags;
		out->cost_q = l->cost_q;
		out->script = (unsigned short)l->script;
		memcpy(out->exit, l->exit, sizeof(out->exit));
		memcpy(out->entry, l->entry, sizeof(out->entry));
	}
	bake_prune(&nav);
	bake_report_connectivity(&nav);
	{
		int ok = bake_write(&nav, path, (unsigned long)surfaces, (unsigned long)walkable);

		platform_log("bots: bake: done in %.1f s (%ld casts)%s", (double)(platform_microseconds() - started) / 1e6, bake.cast_count, ok ? "" : ": the file was not written");
		free(nav.areas);
		free(nav.links);
		free(grid.nodes);
		free(grid.cell_start);
		free(areas.areas);
		free(edges.links);
		free(edges.scripts);
		free(places.places);
		return ok;
	}
}

#endif /* !HALO_WIIU */
