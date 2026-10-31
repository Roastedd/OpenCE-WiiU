/*
DRAWN.C

The obstacle drawn: a wall 0.5 thick, 20 long and 2 high, a unit in front of
the player, whose model is a box of one part (eight corners and a strip of
twelve triangles in the map's model data, as Custom Edition keeps a model's
geometry: tests/test_drawn.py: the map loads its model's geometry, and the
biped stops at the wall as at the obstacle's).
*/

#include "../stagekit.h"

static void drawn_build(
	struct stage *stage)
{
	real_rectangle3d wall = { { -0.25f, 0.25f, -10.f, 10.f, 0.f, 2.f } };
	real_point3d position = { { 1.25f, 0.f, 0.f } };

	prop_drawn_obstacle(stage, "stagekit\\drawn", &wall, &position);
}

struct stagekit_scenario const scenario_drawn = {
	"drawn", "the obstacle's wall, drawn with a model of one part in the map's model data", drawn_build };
