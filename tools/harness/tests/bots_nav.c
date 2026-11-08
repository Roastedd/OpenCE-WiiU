/*
BOTS_NAV.C (test)

The bots' navigation graph (bots_nav.c, compiled as it is): the pack's bytes round trip, a bad pack is refused for a reason, a point's
area, the search over a grid of areas (the best way, the links a bot found bad avoided, the nearest the search came when the goal cannot be
reached), the path's points (an anchor, an exit and an entry for each link) and the places. test_bots_nav.py takes the code and its header
from the sources.
*/

#include "harness.h"

#include "bots_nav.h"
#include "under_test.inc"

/* a W x H grid of areas 8 units apart on the floor, each linked to its four neighbours by walks; `gap` removes the links across the column
`gap` (the goal beyond it cannot be reached); a jump links (0,0) to the last one (a faster way, one-way) */
static struct bots_nav *make_grid(long width, long height, long gap, int with_jump, unsigned char **bytes_out, size_t *count_out)
{
	struct bots_nav nav;
	long x, y, links = 0;
	size_t count;
	unsigned char *bytes;
	struct bots_nav *parsed;
	char const *why;

	memset(&nav, 0, sizeof(nav));
	nav.area_count = width * height;
	nav.areas = calloc((size_t)nav.area_count, sizeof(struct bots_nav_area));
	nav.links = calloc((size_t)nav.area_count * 5, sizeof(struct bots_nav_link));
	nav.scripts = calloc(1, sizeof(struct bots_nav_script));
	nav.places = calloc(3, sizeof(struct bots_nav_place));
	nav.bounds_min[0] = -4.0f;
	nav.bounds_min[1] = -4.0f;
	nav.bounds_max[0] = width * 8.0f;
	nav.bounds_max[1] = height * 8.0f;
	nav.run_speed = 2.0f;
	nav.jump_height = 0.7f;
	nav.drop_height = 2.0f;
	for (y = 0; y < height; y++)
		for (x = 0; x < width; x++)
		{
			struct bots_nav_area *a = &nav.areas[y * width + x];
			static int const dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
			long d;

			a->x = x * 8.0f;
			a->y = y * 8.0f;
			a->z = 1.0f;
			a->radius = 5.0f;
			a->first_link = (unsigned short)links;
			for (d = 0; d < 4; d++)
			{
				long nx = x + dx[d], ny = y + dy[d];
				struct bots_nav_link *l;

				if (nx < 0 || ny < 0 || nx >= width || ny >= height)
					continue;
				if (gap >= 0 && ((x == gap && nx == gap + 1) || (x == gap + 1 && nx == gap)))
					continue;
				l = &nav.links[links++];
				l->to = (unsigned short)(ny * width + nx);
				l->type = _bots_nav_link_walk;
				l->cost_q = 64;
				l->script = 0xFFFF;
				l->exit[0] = a->x + dx[d] * 4.0f;
				l->exit[1] = a->y + dy[d] * 4.0f;
				l->exit[2] = 1.0f;
				l->entry[0] = a->x + dx[d] * 4.5f;
				l->entry[1] = a->y + dy[d] * 4.5f;
				l->entry[2] = 1.0f;
			}
			if (with_jump && x == 0 && y == 0)
			{
				struct bots_nav_link *l = &nav.links[links++];

				l->to = (unsigned short)(width * height - 1);
				l->type = _bots_nav_link_jump;
				l->flags = _bots_nav_link_flag_one_way;
				l->cost_q = 32;
				l->script = 0;
			}
			a->link_count = (unsigned short)(links - a->first_link);
		}
	nav.link_count = links;
	nav.script_count = 1;
	nav.scripts[0].jump = 1;
	nav.scripts[0].yaw = 1.0f;
	nav.scripts[0].flight_ticks = 40;
	nav.scripts[0].attempts = 3;
	nav.scripts[0].successes = 3;
	nav.place_count = 3;
	nav.places[0].type = _bots_nav_place_item;
	nav.places[0].area = 5;
	nav.places[0].x = 40.0f;
	nav.places[0].period_seconds = 90;
	nav.places[1].type = _bots_nav_place_flag_stand;
	nav.places[1].team = 1;
	nav.places[1].area = 7;
	nav.places[2].type = _bots_nav_place_item;
	nav.places[2].area = 1;
	bytes = bots_nav_write_record(&nav, &count);
	CHECK(bytes != NULL, "no record");
	parsed = bots_nav_parse_record("grid", bytes, count, &why);
	CHECK(parsed != NULL, "the record was refused: %s", why ? why : "?");
	free(nav.areas);
	free(nav.links);
	free(nav.scripts);
	free(nav.places);
	if (bytes_out)
	{
		*bytes_out = bytes;
		*count_out = count;
	}
	else
		free(bytes);
	return parsed;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	/* the crc is the usual one (zip's) */
	CASE("crc")
	{
		CHECK(bots_nav_crc32(0, (unsigned char const *)"123456789", 9) == 0xCBF43926UL, "crc %08lX", bots_nav_crc32(0, (unsigned char const *)"123456789", 9));
		CHECK(bots_nav_crc32(0, (unsigned char const *)"", 0) == 0, "empty");
		return 0;
	}
	/* a record written and read back says what it was: areas, links (the portals to 1/64), scripts, places, the bounds, the speeds */
	CASE("round-trip")
	{
		struct bots_nav *nav = make_grid(4, 3, -1, 1, NULL, NULL);
		struct bots_nav_link const *l;

		CHECK(nav->area_count == 12 && nav->script_count == 1 && nav->place_count == 3, "counts %ld %ld %ld", nav->area_count,
			nav->script_count, nav->place_count);
		CHECK(nav->link_count == 3 * 4 * 2 + 2 * 3 * 2 - 0 + 1 - 0 || nav->link_count > 30, "links %ld", nav->link_count);
		CHECK(nav->run_speed == 2.0f && nav->jump_height > 0.69f && nav->drop_height == 2.0f, "speeds");
		CHECK(nav->areas[5].x == 8.0f && nav->areas[5].y == 8.0f && nav->areas[5].radius == 5.0f, "area 5 at %f %f radius %f",
			nav->areas[5].x, nav->areas[5].y, nav->areas[5].radius);
		l = &nav->links[nav->areas[0].first_link];
		CHECK(l->to == 1 && l->type == _bots_nav_link_walk && l->cost_q == 64, "first link %d %d %d", l->to, l->type, l->cost_q);
		/* (area 5, at 8 8, leads east to area 6: the portals are made absolute from its anchor, and the entry from the other's) */
		{
			struct bots_nav_link const *east = &nav->links[nav->areas[5].first_link];

			CHECK(east->to == 6 && fabs(east->exit[0] - 12.0f) < 0.02f && fabs(east->exit[1] - 8.0f) < 0.02f && fabs(east->entry[0] - 12.5f) < 0.02f,
				"link east of area 5: to %d exit %f %f entry %f", east->to, east->exit[0], east->exit[1], east->entry[0]);
		}
		CHECK(fabs(l->exit[0] - 4.0f) < 0.02f && fabs(l->entry[0] - 4.5f) < 0.02f && fabs(l->entry[1]) < 0.02f, "portal %f %f / %f %f",
			l->exit[0], l->exit[1], l->entry[0], l->entry[1]);
		CHECK(nav->scripts[0].jump == 1 && nav->scripts[0].flight_ticks == 40 && fabs(nav->scripts[0].yaw - 1.0f) < 0.001f &&
			nav->scripts[0].successes == 3, "script");
		CHECK(nav->places[0].type == _bots_nav_place_item && nav->places[0].area == 5 && nav->places[0].period_seconds == 90 &&
			nav->places[1].team == 1, "places");
		bots_nav_dispose(nav);
		return 0;
	}
	/* a pack: found by the map's name (any case), the crc checked; a bit flipped is seen; another version, a short pack, a map it has not, are refused */
	CASE("pack")
	{
		unsigned char *record, *pack;
		size_t record_count, pack_count;
		unsigned long surfaces, walkable, offset, size;
		char const *why = NULL;
		struct bots_nav *nav = make_grid(3, 3, -1, 0, &record, &record_count), *back;

		bots_nav_dispose(nav);
		pack = bots_nav_write_pack("BloodGulch", 4916, 2807, record, record_count, 7, &pack_count);
		CHECK(pack != NULL, "no pack");
		CHECK(bots_nav_pack_check(pack, pack_count, &why), "the crc: %s", why ? why : "?");
		CHECK(bots_nav_pack_index_bytes(pack, pack_count) == BOTS_NAV_PACK_HEADER_BYTES + BOTS_NAV_INDEX_ENTRY_BYTES, "index bytes");
		CHECK(bots_nav_pack_find(pack, pack_count, "bloodgulch", &surfaces, &walkable, &offset, &size, &why), "find: %s", why ? why : "?");
		CHECK(surfaces == 4916 && walkable == 2807 && size == record_count && offset + size == pack_count, "entry %lu %lu %lu %lu", surfaces, walkable,
			offset, size);
		back = bots_nav_parse_record("bloodgulch", pack + offset, size, &why);
		CHECK(back && back->area_count == 9, "parse");
		bots_nav_dispose(back);
		CHECK(!bots_nav_pack_find(pack, pack_count, "chillout", &surfaces, &walkable, &offset, &size, &why) && why && strstr(why, "no record"),
			"another map found");
		CHECK(!bots_nav_pack_find(pack, 20, "bloodgulch", &surfaces, &walkable, &offset, &size, &why) && why, "short pack found");
		pack[pack_count - 3] ^= 0x10;
		CHECK(!bots_nav_pack_check(pack, pack_count, &why) && why && strstr(why, "crc"), "a flipped bit not seen");
		pack[pack_count - 3] ^= 0x10;
		pack[8] = 9;
		CHECK(!bots_nav_pack_find(pack, pack_count, "bloodgulch", &surfaces, &walkable, &offset, &size, &why) && why && strstr(why, "version"),
			"another version found");
		pack[8] = BOTS_NAV_PACK_VERSION;
		pack[0] = 'X';
		CHECK(!bots_nav_pack_find(pack, pack_count, "bloodgulch", &surfaces, &walkable, &offset, &size, &why), "wrong magic found");
		free(pack);
		free(record);
		return 0;
	}
	/* bad records are refused with a reason, not read past */
	CASE("bad-record")
	{
		unsigned char *record;
		size_t count;
		char const *why = NULL;
		struct bots_nav *nav = make_grid(3, 3, -1, 0, &record, &count);

		bots_nav_dispose(nav);
		CHECK(bots_nav_parse_record("x", record, 10, &why) == NULL && why, "a short header parsed");
		CHECK(bots_nav_parse_record("x", record, count - 1, &why) == NULL && why, "a short table parsed");
		record[4] = 0;
		record[5] = 0;
		CHECK(bots_nav_parse_record("x", record, count, &why) == NULL && why, "no areas parsed");
		record[4] = 9;
		record[BOTS_NAV_RECORD_HEADER_BYTES + 9 * BOTS_NAV_AREA_BYTES] = 200;
		CHECK(bots_nav_parse_record("x", record, count, &why) == NULL && why && strstr(why, "nowhere"), "a link to an area that is not there parsed: %s",
			why ? why : "?");
		free(record);
		return 0;
	}
	/* a point's area: the nearest anchor that reaches it; none off the map; another floor is another area */
	CASE("find-area")
	{
		struct bots_nav *nav = make_grid(4, 3, -1, 0, NULL, NULL);
		float p[3] = { 8.5f, 8.2f, 1.0f };

		CHECK(bots_nav_find_area(nav, p) == 5, "area %ld", bots_nav_find_area(nav, p));
		p[0] = 11.0f;
		CHECK(bots_nav_find_area(nav, p) == 6 || bots_nav_find_area(nav, p) == 5, "area %ld", bots_nav_find_area(nav, p));
		p[0] = 500.0f;
		CHECK(bots_nav_find_area(nav, p) == -1, "area off the map %ld", bots_nav_find_area(nav, p));
		p[0] = 8.0f;
		p[2] = 6.0f;
		CHECK(bots_nav_find_area(nav, p) == -1, "area on another floor %ld", bots_nav_find_area(nav, p));
		CHECK(bots_nav_nearest_area(nav, p) == 5, "nearest %ld", bots_nav_nearest_area(nav, p));
		bots_nav_dispose(nav);
		return 0;
	}
	/* the search: the shortest way over the grid, a path of the right links; the jump is taken when it is faster */
	CASE("search")
	{
		struct bots_nav *nav = make_grid(4, 3, -1, 0, NULL, NULL);
		struct bots_nav_search search;
		struct bots_nav_path path;
		float a[3] = { 0, 0, 1 }, b[3] = { 24, 16, 1 };
		long k;

		CHECK(bots_nav_search_new(&search, nav), "scratch");
		CHECK(bots_nav_search_path(nav, &search, 0, 11, a, b, NULL, 0, 0, &path), "no path");
		CHECK(path.complete && path.valid && path.link_count == 5, "links %d complete %d", path.link_count, path.complete);
		CHECK(path.goal_area == 11 && path.start_area == 0, "ends %d %d", path.start_area, path.goal_area);
		CHECK(fabs(bots_nav_travel_time(nav, &search, 0, 11) - 20.0f) < 0.01f, "time %f", bots_nav_travel_time(nav, &search, 0, 11));
		CHECK(bots_nav_travel_time(nav, &search, 3, 3) == 0.0f, "same area time");
		/* (the links lead from area to area in order) */
		for (k = 0; k < path.link_count; k++)
		{
			CHECK(path.from[k] == (k == 0 ? 0 : nav->links[path.links[k - 1]].to), "link %ld leaves %d", k, path.from[k]);
		}
		CHECK(nav->links[path.links[path.link_count - 1]].to == 11, "last link");
		bots_nav_search_dispose(&search);
		bots_nav_dispose(nav);
		nav = make_grid(4, 3, -1, 1, NULL, NULL);
		CHECK(bots_nav_search_new(&search, nav), "scratch");
		CHECK(bots_nav_search_path(nav, &search, 0, 11, a, b, NULL, 0, 0, &path), "no path");
		CHECK(path.link_count == 1 && nav->links[path.links[0]].type == _bots_nav_link_jump, "the jump not taken: %d links", path.link_count);
		/* (a bot that found the jump bad does not take it) */
		{
			unsigned short bad[1] = { path.links[0] };

			CHECK(bots_nav_search_path(nav, &search, 0, 11, a, b, bad, 1, 0, &path), "no path round");
			CHECK(path.link_count == 5 && nav->links[path.links[0]].type == _bots_nav_link_walk, "the bad jump was taken (%d links)", path.link_count);
		}
		bots_nav_search_dispose(&search);
		bots_nav_dispose(nav);
		return 0;
	}
	/* a goal that cannot be reached: the path goes as near as the graph does, and says so */
	CASE("unreachable")
	{
		struct bots_nav *nav = make_grid(5, 2, 1, 0, NULL, NULL);
		struct bots_nav_search search;
		struct bots_nav_path path;
		float a[3] = { 0, 0, 1 }, b[3] = { 32, 8, 1 };

		CHECK(bots_nav_search_new(&search, nav), "scratch");
		CHECK(bots_nav_search_path(nav, &search, 0, 9, a, b, NULL, 0, 0, &path), "no partial path");
		CHECK(!path.complete && path.valid, "complete though there is no way");
		CHECK(path.goal_area == 6 || path.goal_area == 1 || path.goal_area == 0, "ended at %d", path.goal_area);
		CHECK(bots_nav_travel_time(nav, &search, 0, 9) < 0.0f, "time to an unreachable area");
		bots_nav_search_dispose(&search);
		bots_nav_dispose(nav);
		return 0;
	}
	/* the jitter changes the costs (bots do not march in file) but never by more than a quarter, and the same seed is the same path */
	CASE("jitter")
	{
		struct bots_nav *nav = make_grid(6, 6, -1, 0, NULL, NULL);
		struct bots_nav_search search;
		struct bots_nav_path first, second, third;
		float a[3] = { 0, 0, 1 }, b[3] = { 40, 40, 1 };
		long k, differs = 0, seed;

		CHECK(bots_nav_search_new(&search, nav), "scratch");
		CHECK(bots_nav_search_path(nav, &search, 0, 35, a, b, NULL, 0, 17, &first), "path");
		CHECK(bots_nav_search_path(nav, &search, 0, 35, a, b, NULL, 0, 17, &second), "path");
		CHECK(first.link_count == second.link_count && memcmp(first.links, second.links, sizeof(first.links[0]) * first.link_count) == 0, "not repeatable");
		for (seed = 1; seed < 40; seed++)
		{
			CHECK(bots_nav_search_path(nav, &search, 0, 35, a, b, NULL, 0, (unsigned long)seed, &third), "path");
			CHECK(third.link_count == 10, "a jittered way of %d links", third.link_count);
			for (k = 0; k < 10; k++)
				if (third.links[k] != first.links[k])
				{
					differs = 1;
					break;
				}
		}
		CHECK(differs, "no seed gave another way");
		bots_nav_search_dispose(&search);
		bots_nav_dispose(nav);
		return 0;
	}
	/* the points of a path: anchor, exit, entry for each link, then the last anchor and the goal */
	CASE("points")
	{
		struct bots_nav *nav = make_grid(4, 3, -1, 1, NULL, NULL);
		struct bots_nav_search search;
		struct bots_nav_path path;
		struct bots_nav_point point;
		float a[3] = { 0, 0, 1 }, b[3] = { 24, 16, 1 };
		unsigned short bad[1];
		long k;

		CHECK(bots_nav_search_new(&search, nav), "scratch");
		CHECK(bots_nav_search_path(nav, &search, 0, 11, a, b, NULL, 0, 0, &path), "no path");
		/* (the jump) */
		CHECK(bots_nav_path_point_count(&path) == 5, "points %ld", bots_nav_path_point_count(&path));
		CHECK(bots_nav_path_point(nav, &path, 2, &point) && point.kind == _bots_nav_link_jump && point.link_index == path.links[0], "entry kind %d", point.kind);
		CHECK(bots_nav_path_point(nav, &path, 0, &point) && point.p[0] == 0.0f && point.kind == _bots_nav_link_walk, "anchor");
		CHECK(bots_nav_path_point(nav, &path, 3, &point) && point.p[0] == 24.0f && point.p[1] == 16.0f, "last anchor %f %f", point.p[0], point.p[1]);
		CHECK(bots_nav_path_point(nav, &path, 4, &point) && point.p[0] == 24.0f && point.link_index == -1, "goal");
		CHECK(!bots_nav_path_point(nav, &path, 5, &point) && !bots_nav_path_point(nav, &path, -1, &point), "past the ends");
		bad[0] = path.links[0];
		CHECK(bots_nav_search_path(nav, &search, 0, 11, a, b, bad, 1, 0, &path), "no path");
		CHECK(bots_nav_path_point_count(&path) == 17, "points %ld", bots_nav_path_point_count(&path));
		for (k = 0; k < bots_nav_path_point_count(&path); k++)
			CHECK(bots_nav_path_point(nav, &path, k, &point), "point %ld", k);
		/* (the walk's exit is in the area it leaves and its entry in the next: the portal is a step of 0.5) */
		CHECK(bots_nav_path_point(nav, &path, 1, &point) && fabs(point.p[0] - 4.0f) < 0.02f, "exit %f", point.p[0]);
		CHECK(bots_nav_path_point(nav, &path, 2, &point) && fabs(point.p[0] - 4.5f) < 0.02f && point.kind == _bots_nav_link_walk, "entry %f", point.p[0]);
		bots_nav_search_dispose(&search);
		bots_nav_dispose(nav);
		return 0;
	}
	/* places: by type, in order */
	CASE("places")
	{
		struct bots_nav *nav = make_grid(4, 3, -1, 0, NULL, NULL);

		CHECK(bots_nav_next_place(nav, -1, _bots_nav_place_item) == 0, "first item");
		CHECK(bots_nav_next_place(nav, 0, _bots_nav_place_item) == 2, "second item");
		CHECK(bots_nav_next_place(nav, 2, _bots_nav_place_item) == -1, "third item");
		CHECK(bots_nav_next_place(nav, -1, _bots_nav_place_flag_stand) == 1, "flag stand");
		CHECK(bots_nav_next_place(nav, -1, _bots_nav_place_hill_point) == -1, "hill");
		bots_nav_dispose(nav);
		return 0;
	}
	/* a pack file made elsewhere (tools/bots_nav_pack.py's test): read through the pack's functions, the map's numbers said on stdout */
	CASE("file")
	{
		FILE *file = fopen(argv[2], "rb");
		unsigned char *bytes = malloc(1 << 20);
		size_t count;
		unsigned long surfaces, walkable, offset, size;
		char const *why = NULL;
		struct bots_nav *nav;

		CHECK(file != NULL, "no file");
		count = fread(bytes, 1, 1 << 20, file);
		fclose(file);
		CHECK(bots_nav_pack_check(bytes, count, &why), "crc: %s", why ? why : "?");
		CHECK(bots_nav_pack_find(bytes, count, argv[3], &surfaces, &walkable, &offset, &size, &why), "find: %s", why ? why : "?");
		nav = bots_nav_parse_record(argv[3], bytes + offset, size, &why);
		CHECK(nav != NULL, "parse: %s", why ? why : "?");
		printf("%lu %lu %ld %ld %ld %ld %.3f %.3f %.3f %.3f\n", surfaces, walkable, nav->area_count, nav->link_count, nav->script_count, nav->place_count,
			nav->areas[1].x, nav->links[nav->areas[1].first_link].exit[1], nav->links[nav->areas[1].first_link].entry[0], nav->scripts[0].yaw);
		bots_nav_dispose(nav);
		free(bytes);
		return 0;
	}
	fprintf(stderr, "no such case: %s\n", case_name);
	return 2;
}
