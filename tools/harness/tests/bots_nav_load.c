/*
BOTS_NAV_LOAD.C (test)

The navigation pack's reader (bots_nav_load.c with bots_nav.c, compiled as they are): a record of the map is read (on the calling thread, and on the
thread of its own, which the caller polls), taken once, and not read for a map it was not baked for: no pack, no record for the map, a BSP of other
numbers (surfaces, walkable), a record that is damaged. test_bots_nav_load.py takes the code and headers from the sources.
*/

#include "harness.h"

#include <stdarg.h>
#include <unistd.h>

#include "bots_nav.h"
#include "bots_nav_load.h"

static char pack_path[512];
static long log_lines;
static char last_log[512];

void platform_log(char const *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(last_log, sizeof(last_log), format, arguments);
	va_end(arguments);
	log_lines++;
}

const char *config_string(char const *name)
{
	return !strcmp(name, "bots.nav_pack") ? pack_path : "";
}

void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size)
{
	snprintf(host_path, host_path_size, "/nonexistent/%s", xbox_path);
}

#include "under_test.inc"

/* a pack of one map: a grid of 3 x 3 areas */
static void write_pack(char const *path, char const *name, unsigned long surfaces, unsigned long walkable, int damage)
{
	struct bots_nav nav;
	long k;
	size_t count, pack_count;
	unsigned char *record, *pack;
	FILE *file;

	memset(&nav, 0, sizeof(nav));
	nav.area_count = 9;
	nav.areas = calloc(9, sizeof(struct bots_nav_area));
	nav.links = calloc(1, sizeof(struct bots_nav_link));
	nav.scripts = calloc(1, sizeof(struct bots_nav_script));
	nav.places = calloc(1, sizeof(struct bots_nav_place));
	for (k = 0; k < 9; k++)
	{
		nav.areas[k].x = (float)(k % 3) * 8.0f;
		nav.areas[k].y = (float)(k / 3) * 8.0f;
		nav.areas[k].radius = 4.0f;
	}
	nav.bounds_max[0] = nav.bounds_max[1] = 24.0f;
	nav.run_speed = 2.25f;
	record = bots_nav_write_record(&nav, &count);
	if (damage)
		record[4] = 0;
	pack = bots_nav_write_pack(name, surfaces, walkable, record, count, 1, &pack_count);
	file = fopen(path, "wb");
	CHECK(file != NULL, "cannot write %s", path);
	fwrite(pack, 1, pack_count, file);
	fclose(file);
	free(pack);
	free(record);
	free(nav.areas);
	free(nav.links);
	free(nav.scripts);
	free(nav.places);
}

/* waits for a running read to end (the thread's), at most about 5 seconds */
static void wait_for_read(void)
{
	int tries;

	for (tries = 0; tries < 500 && bots_nav_load_state() == _bots_nav_load_running; tries++)
		usleep(10000);
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	struct bots_nav *nav;

	snprintf(pack_path, sizeof(pack_path), "%s", argc > 2 ? argv[2] : "/tmp/bots_nav_load_test.pack");
	/* a record of the map, read on this thread: ready at once, taken once, with the map's name from the last component of the scenario's, any case */
	CASE("sync")
	{
		write_pack(pack_path, "bloodgulch", 4916, 2807, 0);
		bots_nav_load_begin("levels\\test\\bloodgulch\\BloodGulch", 4916, 2807, 1);
		CHECK(bots_nav_load_state() == _bots_nav_load_ready, "state %d: %s", bots_nav_load_state(), last_log);
		nav = bots_nav_load_take();
		CHECK(nav && nav->area_count == 9 && !strcmp(nav->name, "bloodgulch"), "the record: %p", (void *)nav);
		CHECK(bots_nav_load_take() == NULL && bots_nav_load_state() == _bots_nav_load_taken, "taken twice");
		bots_nav_dispose(nav);
		/* (the same map again: not read again) */
		log_lines = 0;
		bots_nav_load_begin("levels\\test\\bloodgulch\\bloodgulch", 4916, 2807, 1);
		CHECK(log_lines == 0 && bots_nav_load_state() == _bots_nav_load_taken, "read again");
		bots_nav_load_reset();
		return 0;
	}
	/* the thread's read: the game thread starts it and finds it done a while later without having waited */
	CASE("thread")
	{
		write_pack(pack_path, "chillout", 1545, 297, 0);
		bots_nav_load_begin("levels\\test\\chillout\\chillout", 1545, 297, 0);
		wait_for_read();
		CHECK(bots_nav_load_state() == _bots_nav_load_ready, "state %d: %s", bots_nav_load_state(), last_log);
		nav = bots_nav_load_take();
		CHECK(nav && nav->area_count == 9, "the record");
		bots_nav_dispose(nav);
		bots_nav_load_reset();
		/* (a reset while a read runs waits for it and frees what it made) */
		bots_nav_load_begin("levels\\test\\chillout\\chillout", 1545, 297, 0);
		bots_nav_load_reset();
		CHECK(bots_nav_load_state() == _bots_nav_load_idle, "reset left state %d", bots_nav_load_state());
		return 0;
	}
	/* no pack at the path: none, with a line in the log, and the game goes on */
	CASE("no-pack")
	{
		snprintf(pack_path, sizeof(pack_path), "/nonexistent/navs.pack");
		bots_nav_load_begin("levels\\test\\bloodgulch\\bloodgulch", 4916, 2807, 1);
		CHECK(bots_nav_load_state() == _bots_nav_load_none && strstr(last_log, "no navigation pack"), "state %d: %s", bots_nav_load_state(), last_log);
		CHECK(bots_nav_load_take() == NULL, "a graph from no pack");
		bots_nav_load_reset();
		return 0;
	}
	/* the pack has no record for this map */
	CASE("other-map")
	{
		write_pack(pack_path, "chillout", 1545, 297, 0);
		bots_nav_load_begin("levels\\test\\bloodgulch\\bloodgulch", 4916, 2807, 1);
		CHECK(bots_nav_load_state() == _bots_nav_load_none && strstr(last_log, "no record"), "state %d: %s", bots_nav_load_state(), last_log);
		bots_nav_load_reset();
		return 0;
	}
	/* a record baked for a BSP of other numbers (another build of the map, or a custom map of the same name) is not used */
	CASE("bsp-mismatch")
	{
		write_pack(pack_path, "bloodgulch", 4916, 2807, 0);
		bots_nav_load_begin("levels\\test\\bloodgulch\\bloodgulch", 4917, 2807, 1);
		CHECK(bots_nav_load_state() == _bots_nav_load_none && strstr(last_log, "not used"), "surfaces: state %d: %s", bots_nav_load_state(), last_log);
		bots_nav_load_reset();
		bots_nav_load_begin("levels\\test\\bloodgulch\\bloodgulch", 4916, 2800, 1);
		CHECK(bots_nav_load_state() == _bots_nav_load_none, "walkable: state %d", bots_nav_load_state());
		bots_nav_load_reset();
		return 0;
	}
	/* a damaged record (no areas) is refused with its reason */
	CASE("damaged")
	{
		write_pack(pack_path, "bloodgulch", 4916, 2807, 1);
		bots_nav_load_begin("levels\\test\\bloodgulch\\bloodgulch", 4916, 2807, 1);
		CHECK(bots_nav_load_state() == _bots_nav_load_none && strstr(last_log, "no good"), "state %d: %s", bots_nav_load_state(), last_log);
		bots_nav_load_reset();
		return 0;
	}
	/* a file that is not a pack */
	CASE("not-a-pack")
	{
		FILE *file = fopen(pack_path, "wb");

		fputs("this is not a navigation pack, but it is longer than the header of one", file);
		fclose(file);
		bots_nav_load_begin("levels\\test\\bloodgulch\\bloodgulch", 4916, 2807, 1);
		CHECK(bots_nav_load_state() == _bots_nav_load_none, "state %d: %s", bots_nav_load_state(), last_log);
		bots_nav_load_reset();
		return 0;
	}
	fprintf(stderr, "no such case: %s\n", case_name);
	return 2;
}
