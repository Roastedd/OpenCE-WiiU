/*
BOTS_NAV_BAKE.H

The navigation bake (bots_nav_bake.c): offline, in a headless game on the Mac, never on the console.
*/
#ifndef HALO_BOTS_NAV_BAKE_H
#define HALO_BOTS_NAV_BAKE_H

/* bakes the map that is running (the biped of the unit whose index is given is the one the graph is made for) into a one-map pack at
path; TRUE when it wrote the file */
int bots_nav_bake_run(long unit_index, char const *map_name, char const *path);

#endif
