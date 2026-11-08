/*
BOTS_NAV_LOAD.H

Reading a map's navigation record from navs.pack (bots_nav.h) on a thread of its own, so that the game thread never waits for the card: the
game thread starts the read at the map's first bot tick and takes the graph on the first tick after the thread is done (a map is a game of
thousands of ticks; the bots walk as they always did until then). No pack, no record for the map, a pack that does not match the map's
BSP or a bad crc: no graph, and the bots walk as they always did.
*/
#ifndef HALO_BOTS_NAV_LOAD_H
#define HALO_BOTS_NAV_LOAD_H

struct bots_nav;

enum
{
	_bots_nav_load_idle = 0,
	_bots_nav_load_running,
	_bots_nav_load_ready,
	/* nothing to use: why is in the log */
	_bots_nav_load_none,
	/* the graph was taken */
	_bots_nav_load_taken,
};

/* begins the read for the map `map_name` (the scenario's name: its last component is looked up) against a BSP of this many collision surfaces
and walkable surfaces; sync (the tests, bots.nav_sync) reads on the calling thread. Does nothing if a read was begun for this map */
void bots_nav_load_begin(char const *map_name, unsigned long surfaces, unsigned long walkable, int sync);
/* the state, and the graph once ready (taken: the caller owns it and disposes of it) */
int bots_nav_load_state(void);
struct bots_nav *bots_nav_load_take(void);
/* the map's end: waits for the thread, frees what was not taken */
void bots_nav_load_reset(void);
/* the pack's path, for the log */
char const *bots_nav_load_path(void);

#endif
