/* The smooth defaults' rule (port/linux/game/smooth_defaults.c) apart from the game: which maps they are for, what each card file's word comes to for a map
of each kind, and the decision at each map start (the order of the calls it makes, what it logs, what it leaves behind from one map to the next). */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HALO_WIIU 1
#include "smooth_defaults.h"

static int failures;
#define CHECK(condition) do { if (!(condition)) { printf("FAIL line %d: %s\n", __LINE__, #condition); failures++; } } while (0)

/* ---------- the platform and the units' (stand-ins) */

static char logs[64][300];
static int log_count;
static long study_files[8]; /* by file, -1 absent */
static long snapshot_card = -1, governor_card = -1;
static char const *reason_now;
static int calls[8];
static long last_governor_on = -9;
static int last_governor_map_ok = -9;

void platform_log(char const *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	if (log_count < 64)
		vsnprintf(logs[log_count++], sizeof(logs[0]), format, arguments);
	va_end(arguments);
}

long wiiu_study_number(const char *name, long fallback)
{
	long value = !strcmp(name, "tick_thread.txt") ? study_files[0] : !strcmp(name, "tick_overlap.txt") ? study_files[1] : -1;

	return value < 0 ? fallback : value;
}

long tick_snapshot_card(void) { return snapshot_card; }
long render_governor_card(void) { return governor_card; }
char const *main_smooth_defaults_reason(void) { return reason_now; }
void tick_overlap_decide(int mid_map) { calls[0]++; calls[2] += mid_map; }
void render_governor_decide(int map_ok, int on) { calls[1]++; last_governor_map_ok = map_ok; last_governor_on = on; }

static int logged(char const *text)
{
	int i;

	for (i = 0; i < log_count; i++)
		if (strstr(logs[i], text))
			return 1;
	return 0;
}

/* ---------- the rule */

static struct smooth_defaults resolve(long thread, long snapshot, long overlap, long governor, char const *reason)
{
	long const card[NUMBER_OF_SMOOTH_SWITCHES] = { thread, snapshot, overlap, governor };
	struct smooth_defaults out;

	smooth_defaults_resolve(&out, card, reason, 1);
	return out;
}

static void test_cards(void)
{
	struct smooth_defaults d;

	/* no card files at all: all four on for a map of one player, all four off for any other */
	d = resolve(-1, -1, -1, -1, NULL);
	CHECK(d.value[SMOOTH_TICK_THREAD] == 1 && d.value[SMOOTH_TICK_SNAPSHOT] == 1 && d.value[SMOOTH_TICK_OVERLAP] == 1 && d.value[SMOOTH_GOVERNOR] == 1);
	CHECK(d.source[SMOOTH_TICK_OVERLAP] == SMOOTH_SOURCE_DEFAULT_ON);
	d = resolve(-1, -1, -1, -1, "a hosted network game");
	CHECK(!d.value[SMOOTH_TICK_THREAD] && !d.value[SMOOTH_TICK_SNAPSHOT] && !d.value[SMOOTH_TICK_OVERLAP] && !d.value[SMOOTH_GOVERNOR]);
	CHECK(d.source[SMOOTH_TICK_OVERLAP] == SMOOTH_SOURCE_MAP_OFF);
	/* the owner's card (all four on): the same, but the source says the card file; never for a map that is not one */
	d = resolve(1, 1, 1, 1, NULL);
	CHECK(d.value[SMOOTH_TICK_THREAD] == 1 && d.value[SMOOTH_TICK_SNAPSHOT] == 1 && d.value[SMOOTH_TICK_OVERLAP] == 1 && d.value[SMOOTH_GOVERNOR] == 1);
	CHECK(d.source[SMOOTH_TICK_OVERLAP] == SMOOTH_SOURCE_CARD_ON);
	d = resolve(1, 1, 1, 1, "split screen");
	CHECK(!d.value[SMOOTH_TICK_THREAD] && !d.value[SMOOTH_TICK_SNAPSHOT] && !d.value[SMOOTH_TICK_OVERLAP] && !d.value[SMOOTH_GOVERNOR]);
	/* the snapshot's bits are kept (a study mode) and the default is bit 1 */
	d = resolve(1, 8321, 1, 1, NULL);
	CHECK(d.value[SMOOTH_TICK_SNAPSHOT] == 8321);
	/* an explicit 0 is off, on a map of one player too: for each of the four */
	d = resolve(0, -1, -1, -1, NULL);
	CHECK(!d.value[SMOOTH_TICK_THREAD] && !d.value[SMOOTH_TICK_OVERLAP] && !d.value[SMOOTH_TICK_SNAPSHOT] && d.value[SMOOTH_GOVERNOR] == 1);
	CHECK(d.source[SMOOTH_TICK_THREAD] == SMOOTH_SOURCE_CARD_OFF && d.source[SMOOTH_TICK_OVERLAP] == SMOOTH_SOURCE_OTHERS_OFF && d.source[SMOOTH_TICK_SNAPSHOT] == SMOOTH_SOURCE_OTHERS_OFF);
	d = resolve(-1, 0, -1, -1, NULL);
	CHECK(!d.value[SMOOTH_TICK_SNAPSHOT] && !d.value[SMOOTH_TICK_OVERLAP] && !d.value[SMOOTH_TICK_THREAD] && d.value[SMOOTH_GOVERNOR] == 1);
	d = resolve(-1, -1, 0, -1, NULL);
	CHECK(!d.value[SMOOTH_TICK_OVERLAP] && !d.value[SMOOTH_TICK_THREAD] && !d.value[SMOOTH_TICK_SNAPSHOT] && d.value[SMOOTH_GOVERNOR] == 1);
	d = resolve(-1, -1, -1, 0, NULL);
	CHECK(d.value[SMOOTH_TICK_THREAD] == 1 && d.value[SMOOTH_TICK_SNAPSHOT] == 1 && d.value[SMOOTH_TICK_OVERLAP] == 1 && !d.value[SMOOTH_GOVERNOR]);
	CHECK(d.source[SMOOTH_GOVERNOR] == SMOOTH_SOURCE_CARD_OFF);
	/* the thread on its own (1) is the stage 1 study mode, as it has always been: the thread, not the overlap, and the snapshot is not defaulted on behind it */
	d = resolve(1, -1, 0, -1, NULL);
	CHECK(d.value[SMOOTH_TICK_THREAD] == 1 && !d.value[SMOOTH_TICK_OVERLAP] && !d.value[SMOOTH_TICK_SNAPSHOT]);
	/* the overlap named on while the thread is off: nothing to run it on */
	d = resolve(0, 1, 1, -1, NULL);
	CHECK(!d.value[SMOOTH_TICK_OVERLAP] && !d.value[SMOOTH_TICK_THREAD] && d.value[SMOOTH_TICK_SNAPSHOT] == 1);
	/* a negative word is the default, whatever the negative number */
	d = resolve(-7, -1, -1, -1, NULL);
	CHECK(d.card[SMOOTH_TICK_THREAD] == SMOOTH_CARD_DEFAULT && d.value[SMOOTH_TICK_OVERLAP] == 1);
	/* the governor is its own: the tick overlap off does not turn it off */
	d = resolve(0, 0, 0, -1, NULL);
	CHECK(d.value[SMOOTH_GOVERNOR] == 1);
	d = resolve(0, 0, 0, -1, "a joined network game");
	CHECK(d.value[SMOOTH_GOVERNOR] == 0);
}

/* ---------- the maps */

static struct smooth_defaults_map one_player(void)
{
	struct smooth_defaults_map map;

	memset(&map, 0, sizeof(map));
	map.connection = SMOOTH_CONNECTION_LOCAL;
	map.interpolation = 1;
	map.spawn_players = 1;
	return map;
}

static void test_maps(void)
{
	struct smooth_defaults_map map = one_player();

	CHECK(smooth_defaults_reason(&map) == NULL);
	/* a count of local players of 0 is "not counted yet", and 1 is the one player */
	map.local_players = 1;
	CHECK(smooth_defaults_reason(&map) == NULL);
	map.local_players = 0;
	/* 2 or more local players, from the menu's count or from the players counted: split screen */
	map.spawn_players = 2;
	CHECK(smooth_defaults_reason(&map) && strstr(smooth_defaults_reason(&map), "split screen"));
	map.spawn_players = 4;
	CHECK(smooth_defaults_reason(&map) && strstr(smooth_defaults_reason(&map), "split screen"));
	map = one_player();
	map.local_players = 2;
	CHECK(smooth_defaults_reason(&map) && strstr(smooth_defaults_reason(&map), "split screen"));
	map.local_players = 3;
	CHECK(smooth_defaults_reason(&map) && strstr(smooth_defaults_reason(&map), "split screen"));
	/* the network, every kind: hosted (also with no one joined), joined, a session open with the game local (a lobby, system link, internet), a saved film */
	map = one_player();
	map.connection = SMOOTH_CONNECTION_SERVER;
	CHECK(smooth_defaults_reason(&map) && strstr(smooth_defaults_reason(&map), "hosted"));
	map.connection = SMOOTH_CONNECTION_CLIENT;
	CHECK(smooth_defaults_reason(&map) && strstr(smooth_defaults_reason(&map), "joined"));
	map.connection = SMOOTH_CONNECTION_FILM;
	CHECK(smooth_defaults_reason(&map) && strstr(smooth_defaults_reason(&map), "film"));
	map.connection = SMOOTH_CONNECTION_OTHER;
	CHECK(smooth_defaults_reason(&map) && strstr(smooth_defaults_reason(&map), "network"));
	map = one_player();
	map.network_session = 1;
	CHECK(smooth_defaults_reason(&map) && strstr(smooth_defaults_reason(&map), "session"));
	/* the rest */
	map = one_player();
	map.main_menu = 1;
	CHECK(smooth_defaults_reason(&map) && strstr(smooth_defaults_reason(&map), "main menu"));
	map = one_player();
	map.dedicated_server = 1;
	CHECK(smooth_defaults_reason(&map));
	map = one_player();
	map.editor = 1;
	CHECK(smooth_defaults_reason(&map));
	map = one_player();
	map.recording = 1;
	CHECK(smooth_defaults_reason(&map));
	map = one_player();
	map.interpolation = 0;
	CHECK(smooth_defaults_reason(&map) && strstr(smooth_defaults_reason(&map), "interpolation"));
	/* the reason of the first rule that applies: a network game that is also split screen says the network */
	map = one_player();
	map.connection = SMOOTH_CONNECTION_SERVER;
	map.spawn_players = 2;
	CHECK(strstr(smooth_defaults_reason(&map), "hosted"));
}

/* ---------- the decision at each map start */

static void reset_log(void)
{
	log_count = 0;
	memset(calls, 0, sizeof(calls));
	last_governor_on = -9;
	last_governor_map_ok = -9;
}

static void test_decisions(void)
{
	/* before any map: nothing is on, whatever the cards say (nothing has been read yet either) */
	study_files[0] = study_files[1] = -1;
	snapshot_card = governor_card = -1;
	CHECK(!smooth_defaults_decided());
	CHECK(smooth_defaults_value(SMOOTH_TICK_OVERLAP) == 0 && smooth_defaults_value(SMOOTH_TICK_THREAD) == 0 && smooth_defaults_value(SMOOTH_GOVERNOR) == 0);

	/* the main menu's scene (the first map of a run) */
	reset_log();
	reason_now = "the main menu's scene";
	smooth_defaults_new_map();
	CHECK(smooth_defaults_decided() && !smooth_defaults_map_ok());
	CHECK(logged("smooth defaults: OFF for this map: the main menu's scene"));
	CHECK(calls[0] == 1 && calls[1] == 1 && last_governor_on == 0 && last_governor_map_ok == 0);
	CHECK(smooth_defaults_value(SMOOTH_TICK_OVERLAP) == 0 && smooth_defaults_value(SMOOTH_GOVERNOR) == 0);
	CHECK(strstr(smooth_defaults_label(), "off: the main menu"));

	/* a game started from it, one local player */
	reset_log();
	reason_now = NULL;
	smooth_defaults_new_map();
	CHECK(smooth_defaults_map_ok());
	CHECK(logged("smooth defaults: ON for this map (one local player, no network)"));
	CHECK(calls[0] == 1 && calls[1] == 1 && last_governor_on == 1 && last_governor_map_ok == 1);
	CHECK(smooth_defaults_value(SMOOTH_TICK_THREAD) == 1 && smooth_defaults_value(SMOOTH_TICK_SNAPSHOT) == 1 && smooth_defaults_value(SMOOTH_TICK_OVERLAP) == 1 && smooth_defaults_value(SMOOTH_GOVERNOR) == 1);
	CHECK(strcmp(smooth_defaults_label(), "smooth defaults on") == 0);

	/* the next map is a hosted game: everything off again, nothing left over */
	reset_log();
	reason_now = "a hosted network game";
	smooth_defaults_new_map();
	CHECK(!smooth_defaults_map_ok());
	CHECK(logged("OFF for this map: a hosted network game"));
	CHECK(smooth_defaults_value(SMOOTH_TICK_THREAD) == 0 && smooth_defaults_value(SMOOTH_TICK_SNAPSHOT) == 0 && smooth_defaults_value(SMOOTH_TICK_OVERLAP) == 0 && smooth_defaults_value(SMOOTH_GOVERNOR) == 0);
	CHECK(last_governor_on == 0);

	/* and a split screen game, then a level of one player again */
	reset_log();
	reason_now = "split screen";
	smooth_defaults_new_map();
	CHECK(smooth_defaults_value(SMOOTH_TICK_OVERLAP) == 0);
	reset_log();
	reason_now = NULL;
	smooth_defaults_new_map();
	CHECK(smooth_defaults_value(SMOOTH_TICK_OVERLAP) == 1 && smooth_defaults_value(SMOOTH_GOVERNOR) == 1);
	CHECK(smooth_defaults_map_count() == 5);
	/* a map that stops being one in the middle of it: decided again, with the owners' queues left as they are */
	reset_log();
	reason_now = "a network session is open (system link or internet)";
	smooth_defaults_map_stops();
	CHECK(calls[0] == 1 && calls[2] == 1 && calls[1] == 1 && !smooth_defaults_map_ok());
	CHECK(logged("OFF for this map: a network session is open"));
	CHECK(smooth_defaults_value(SMOOTH_TICK_OVERLAP) == 0);
}

/* the owner's card (1, 1, 1, on) in a process of its own: the cards are read once, at the first map */
static void test_owner_card(void)
{
	study_files[0] = study_files[1] = 1;
	snapshot_card = 1;
	governor_card = 1;
	reset_log();
	reason_now = NULL;
	smooth_defaults_new_map();
	CHECK(smooth_defaults_value(SMOOTH_TICK_OVERLAP) == 1 && smooth_defaults_value(SMOOTH_TICK_THREAD) == 1 && smooth_defaults_value(SMOOTH_GOVERNOR) == 1);
	CHECK(logged("tick overlap on, its card file"));
	reset_log();
	reason_now = "a hosted network game";
	smooth_defaults_new_map();
	CHECK(smooth_defaults_value(SMOOTH_TICK_OVERLAP) == 0 && smooth_defaults_value(SMOOTH_TICK_THREAD) == 0 && smooth_defaults_value(SMOOTH_TICK_SNAPSHOT) == 0 && smooth_defaults_value(SMOOTH_GOVERNOR) == 0);
	CHECK(logged("a card file that holds 1 is on only for the maps the smooth settings are for"));
	reset_log();
	reason_now = "split screen";
	smooth_defaults_new_map();
	CHECK(smooth_defaults_value(SMOOTH_TICK_OVERLAP) == 0 && smooth_defaults_value(SMOOTH_GOVERNOR) == 0);
	reset_log();
	reason_now = NULL;
	smooth_defaults_new_map();
	CHECK(smooth_defaults_value(SMOOTH_TICK_OVERLAP) == 1 && smooth_defaults_value(SMOOTH_GOVERNOR) == 1);
}

/* the opt-outs, each card file at 0 in a process of its own */
static void test_opt_out(char const *which)
{
	study_files[0] = study_files[1] = -1;
	snapshot_card = governor_card = -1;
	if (!strcmp(which, "thread")) study_files[0] = 0;
	if (!strcmp(which, "overlap")) study_files[1] = 0;
	if (!strcmp(which, "snapshot")) snapshot_card = 0;
	if (!strcmp(which, "governor")) governor_card = 0;
	reason_now = NULL;
	smooth_defaults_new_map();
	CHECK(smooth_defaults_value(SMOOTH_TICK_OVERLAP) == (strcmp(which, "governor") ? 0 : 1));
	CHECK(smooth_defaults_value(SMOOTH_GOVERNOR) == (strcmp(which, "governor") ? 1 : 0));
}

int main(int argc, char **argv)
{
	if (argc > 2 && !strcmp(argv[1], "opt-out"))
		test_opt_out(argv[2]);
	else if (argc > 1 && !strcmp(argv[1], "owner"))
		test_owner_card();
	else
	{
		test_cards();
		test_maps();
		test_decisions();
	}
	printf(failures ? "FAILED %d\n" : "smooth defaults: all checks passed\n", failures);
	return failures ? 1 : 0;
}
