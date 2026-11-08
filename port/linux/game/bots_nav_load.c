/*
BOTS_NAV_LOAD.C

The navigation pack's read on a thread of its own (bots_nav_load.h). It reads the pack's index (a few hundred bytes), then the map's record
only, and checks the record against the BSP it is for (the collision surface count and the walkable surface count the bake recorded: a
map that is not the one baked, or the same name with another build, is not used). The pack's whole crc is kept for the release tool
(tools/bots_nav_pack.py verify) and the manifest's hash: the record's own bounds are checked here by the parser.
*/

/* (the card's real paths: not the Xbox path translation of the game's stdio) */
#define HALO_LINUX_PLATFORM_LAYER 1

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bots_nav.h"
#include "bots_nav_load.h"

void platform_log(char const *format, ...);
const char *config_string(char const *name);
#ifdef HALO_WIIU
void wiiu_thread_background(const char *name);
#else
void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size);
#endif

static struct
{
	volatile int state;
	pthread_t thread;
	int thread_made;
	char map[BOTS_NAV_NAME_BYTES];
	char path[1024];
	unsigned long surfaces, walkable;
	struct bots_nav *nav;
	char begun_for[BOTS_NAV_NAME_BYTES];
} load;

char const *bots_nav_load_path(void)
{
	return load.path;
}

/* the path of the pack: bots.nav_pack, else the card's folder on the console, else beside the data folder's maps */
static void nav_pack_path(char *path, size_t size)
{
	char const *given = config_string("bots.nav_pack");

	if (given && given[0])
	{
		snprintf(path, size, "%s", given);
		return;
	}
#ifdef HALO_WIIU
	snprintf(path, size, "/vol/external01/wiiu/apps/halo/navs.pack");
#else
	{
		char translated[1024];
		FILE *file;

		platform_translate_path("d:\\navs.pack", translated, sizeof(translated));
		file = fopen(translated, "rb");
		if (file)
			fclose(file);
		else
			platform_translate_path("d:\\maps\\navs.pack", translated, sizeof(translated));
		snprintf(path, size, "%s", translated);
	}
#endif
}

/* the last component of a scenario name, in lower case: "levels\test\bloodgulch\bloodgulch" is "bloodgulch" */
static void nav_map_stem(char const *scenario, char *stem, size_t size)
{
	char const *last = scenario, *p;
	size_t k;

	for (p = scenario; *p; p++)
		if (*p == '\\' || *p == '/')
			last = p + 1;
	snprintf(stem, size, "%s", last);
	for (k = 0; stem[k]; k++)
		if (stem[k] >= 'A' && stem[k] <= 'Z')
			stem[k] = (char)(stem[k] - 'A' + 'a');
}

/* the read itself (any thread) */
static void nav_read(void)
{
	FILE *file = fopen(load.path, "rb");
	unsigned char header[BOTS_NAV_PACK_HEADER_BYTES + 64 * BOTS_NAV_INDEX_ENTRY_BYTES];
	size_t got, index_bytes;
	unsigned long surfaces, walkable, offset, size;
	char const *why = NULL;
	unsigned char *record;
	struct bots_nav *nav;

	if (!file)
	{
		platform_log("bots: nav: no navigation pack at %s: the bots walk by the engine's surfaces", load.path);
		load.state = _bots_nav_load_none;
		return;
	}
	got = fread(header, 1, sizeof(header), file);
	index_bytes = bots_nav_pack_index_bytes(header, got);
	if (!index_bytes || !bots_nav_pack_find(header, got, load.map, &surfaces, &walkable, &offset, &size, &why))
	{
		fclose(file);
		platform_log("bots: nav: %s has no use for %s (%s): the bots walk by the engine's surfaces", load.path, load.map, why ? why : "a pack with no index");
		load.state = _bots_nav_load_none;
		return;
	}
	if (surfaces != load.surfaces || walkable != load.walkable)
	{
		fclose(file);
		platform_log("bots: nav: the record for %s is for a BSP of %lu surfaces (%lu walkable), this map's has %lu (%lu): not used", load.map, surfaces,
			walkable, load.surfaces, load.walkable);
		load.state = _bots_nav_load_none;
		return;
	}
	record = (unsigned char *)malloc(size);
	if (!record || fseek(file, (long)offset, SEEK_SET) != 0 || fread(record, 1, size, file) != size)
	{
		free(record);
		fclose(file);
		platform_log("bots: nav: the record for %s could not be read", load.map);
		load.state = _bots_nav_load_none;
		return;
	}
	fclose(file);
	nav = bots_nav_parse_record(load.map, record, size, &why);
	free(record);
	if (!nav)
	{
		platform_log("bots: nav: the record for %s is no good (%s): not used", load.map, why ? why : "?");
		load.state = _bots_nav_load_none;
		return;
	}
	load.nav = nav;
	platform_log("bots: nav: read %s from %s: %ld areas, %ld links, %ld places", load.map, load.path, nav->area_count, nav->link_count, nav->place_count);
	__sync_synchronize();
	load.state = _bots_nav_load_ready;
}

static void *nav_thread(void *argument)
{
	(void)argument;
#ifdef HALO_WIIU
	wiiu_thread_background("navs");
#endif
	nav_read();
	return NULL;
}

void bots_nav_load_begin(char const *map_name, unsigned long surfaces, unsigned long walkable, int sync)
{
	char stem[BOTS_NAV_NAME_BYTES];

	nav_map_stem(map_name, stem, sizeof(stem));
	if (load.state != _bots_nav_load_idle && strcmp(load.begun_for, stem) == 0)
		return;
	bots_nav_load_reset();
	snprintf(load.begun_for, sizeof(load.begun_for), "%s", stem);
	snprintf(load.map, sizeof(load.map), "%s", stem);
	load.surfaces = surfaces;
	load.walkable = walkable;
	nav_pack_path(load.path, sizeof(load.path));
	load.state = _bots_nav_load_running;
	if (sync)
	{
		nav_read();
		return;
	}
	{
		pthread_attr_t attributes;
		int error;

		pthread_attr_init(&attributes);
		pthread_attr_setstacksize(&attributes, 64 * 1024);
		error = pthread_create(&load.thread, &attributes, nav_thread, NULL);
		pthread_attr_destroy(&attributes);
		if (error)
		{
			platform_log("bots: nav: the reader's thread could not be made (%d): the bots walk by the engine's surfaces", error);
			load.state = _bots_nav_load_none;
			return;
		}
		load.thread_made = 1;
	}
}

int bots_nav_load_state(void)
{
	return load.state;
}

struct bots_nav *bots_nav_load_take(void)
{
	struct bots_nav *nav;

	if (load.state != _bots_nav_load_ready)
		return NULL;
	if (load.thread_made)
	{
		pthread_join(load.thread, NULL);
		load.thread_made = 0;
	}
	nav = load.nav;
	load.nav = NULL;
	load.state = _bots_nav_load_taken;
	return nav;
}

void bots_nav_load_reset(void)
{
	if (load.thread_made)
	{
		pthread_join(load.thread, NULL);
		load.thread_made = 0;
	}
	if (load.nav)
		bots_nav_dispose(load.nav);
	load.nav = NULL;
	load.state = _bots_nav_load_idle;
	load.begun_for[0] = 0;
}
