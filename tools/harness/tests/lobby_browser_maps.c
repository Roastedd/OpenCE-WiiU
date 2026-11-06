/*
LOBBY_BROWSER_MAPS.C (test)

What the server browser (menu_functions.c's lobby_browser_map_state) makes of the map a listing names. A listing
names the map without its folder, so a Custom Edition map is a stock map's name to look at: the games' own maps are
installed; a Custom Edition map on this machine is found under custom_maps\ its name; any other can only be a Custom
Edition map, downloaded as the game is joined if map downloads are on and Custom Edition maps can run.
test_lobby_browser_maps.py takes the function from the source.
*/

#include "harness.h"
#include "enums.inc"

#define CUSTOM_EDITION_LEVEL_NAME_PREFIX "custom_maps\\"
#define CUSTOM_EDITION_MAXIMUM_LEVEL_NAME_LENGTH 63

static int fake_map_download = 1;
static int fake_tag_cache = 1;

static int config_boolean(char const *name)
{
	(void)name;
	return fake_map_download;
}

static void *halo_custom_edition_tag_cache(void)
{
	return fake_tag_cache ? (void *)&fake_map_download : NULL;
}

#define custom_edition_level_name(level_name) (!strncmp(level_name, "custom_maps\\", 12))

/* installed on this machine: the Custom Edition map hugeass */
static short custom_edition_maps_display_index(char const *level_name)
{
	return !strcmp(level_name, "custom_maps\\hugeass") ? 3 : NONE;
}

static char const *const stock_maps[] = { "levels\\test\\bloodgulch\\bloodgulch", "levels\\test\\chillout\\chillout" };

static char const *ui_widget_port_multiplayer_maps(char const *const **names, void *unused)
{
	(void)unused;
	*names = stock_maps;
	return "maps";
}

static short xbox_multiplayer_map_count(char const *maps)
{
	(void)maps;
	return 2;
}

static char const *scenario_name(char const *path)
{
	char const *slash = strrchr(path, '\\');

	return slash ? slash + 1 : path;
}

#define _stricmp strcasecmp
static short campaign_level_of(char const *map)
{
	return !strcmp(map, "a30") ? 1 : NONE;
}

#include "under_test.inc"

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	CASE("stock") {
		CHECK(lobby_browser_map_state("bloodgulch") == P2P_LOBBY_MAP_INSTALLED, "a stock map");
		CHECK(lobby_browser_map_state("chillout") == P2P_LOBBY_MAP_INSTALLED, "another");
		CHECK(lobby_browser_map_state("a30") == P2P_LOBBY_MAP_INSTALLED, "a campaign level");
	} else CASE("installed-custom-map") {
		CHECK(lobby_browser_map_state("hugeass") == P2P_LOBBY_MAP_INSTALLED, "a listing's Custom Edition map this machine has");
		CHECK(lobby_browser_map_state("custom_maps\\hugeass") == P2P_LOBBY_MAP_INSTALLED, "and by its level name");
		fake_map_download = 0;
		CHECK(lobby_browser_map_state("hugeass") == P2P_LOBBY_MAP_INSTALLED, "with downloads off too");
	} else CASE("downloadable") {
		CHECK(lobby_browser_map_state("coldsnap") == P2P_LOBBY_MAP_DOWNLOADABLE, "a Custom Edition map it lacks");
		CHECK(lobby_browser_map_state("infinity") == P2P_LOBBY_MAP_DOWNLOADABLE, "so is a Gearbox map");
		CHECK(lobby_browser_map_state("bigass_open_v1.1") == P2P_LOBBY_MAP_DOWNLOADABLE, "with dots in its name");
		CHECK(lobby_browser_map_state("custom_maps\\coldsnap") == P2P_LOBBY_MAP_DOWNLOADABLE, "by its level name");
	} else CASE("missing") {
		fake_map_download = 0;
		CHECK(lobby_browser_map_state("coldsnap") == P2P_LOBBY_MAP_MISSING, "downloads off");
		CHECK(lobby_browser_map_state("custom_maps\\coldsnap") == P2P_LOBBY_MAP_MISSING, "by its level name");
		fake_map_download = 1;
		fake_tag_cache = 0;
		CHECK(lobby_browser_map_state("coldsnap") == P2P_LOBBY_MAP_MISSING, "a machine that cannot run Custom Edition maps");
		fake_tag_cache = 1;
		{
			char name[200];

			memset(name, 'x', sizeof(name) - 1);
			name[sizeof(name) - 1] = 0;
			CHECK(lobby_browser_map_state(name) == P2P_LOBBY_MAP_MISSING, "a name too long to be a Custom Edition map's");
		}
	} else {
		fprintf(stderr, "unknown case %s\n", case_name);
		return 2;
	}
	return 0;
}
