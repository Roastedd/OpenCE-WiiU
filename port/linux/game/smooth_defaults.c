/*
SMOOTH_DEFAULTS.C

See smooth_defaults.h.
*/

#include "smooth_defaults.h"

#ifdef HALO_WIIU

#include <stdio.h>
#include <string.h>

/* the platform layer's (port/wiiu/src/wiiu_study.c, wiiu_shell.c; the digest harness's glue on the host) */
void platform_log(char const *format, ...);
long wiiu_study_number(const char *name, long fallback);

/* the units' */
long tick_snapshot_card(void);                          /* tick_snapshot.c: halo/tick_snapshot.txt (HALO_TICK_SNAPSHOT), SMOOTH_CARD_DEFAULT if absent or negative */
long render_governor_card(void);                        /* render_governor.c: halo/governor.txt's first word: SMOOTH_CARD_DEFAULT, 0 (off), 1 (on or a tuning line) */
void tick_overlap_decide(int mid_map);                  /* tick_overlap.c: the overlap, the snapshot and (not in the middle of a map) the owners' queues made again for the map */
void render_governor_decide(int map_ok, int on);        /* render_governor.c */
char const *main_smooth_defaults_reason(void);          /* main.c: NULL for a map the smooth settings are for, else why not */
int main_smooth_defaults_net_game(void);                /* main.c: the game is a joined one or a hosted one (the log says so) */

static struct
{
	int cards_read;
	int decided;
	int map_ok;
	char label[120];
	unsigned long maps;
	struct smooth_defaults now;
} sd;

static char const *const switch_names[NUMBER_OF_SMOOTH_SWITCHES] = { "tick_thread.txt", "tick_snapshot.txt", "tick_overlap.txt", "governor.txt" };
static char const *const switch_labels[NUMBER_OF_SMOOTH_SWITCHES] = { "tick thread", "tick snapshot", "tick overlap", "governor" };

/* ---------- the rule */

static void resolve_one(struct smooth_defaults *out, enum smooth_switch which, char const *reason, int others_off, int set_ok, long default_value)
{
	const long card = out->card[which];

	if (card == SMOOTH_CARD_OFF)
	{
		out->value[which] = 0;
		out->source[which] = SMOOTH_SOURCE_CARD_OFF;
	}
	else if (reason)
	{
		out->value[which] = 0;
		out->source[which] = SMOOTH_SOURCE_MAP_OFF;
	}
	else if (others_off || (card < 0 && !set_ok))
	{
		/* (the overlap with the thread or the snapshot turned off by a card file, or a default of the three with one of them turned off: the feature is not there) */
		out->value[which] = 0;
		out->source[which] = SMOOTH_SOURCE_OTHERS_OFF;
	}
	else
	{
		out->value[which] = card > 0 ? card : default_value;
		out->source[which] = card > 0 ? SMOOTH_SOURCE_CARD_ON : SMOOTH_SOURCE_DEFAULT_ON;
	}
}

void smooth_defaults_resolve(struct smooth_defaults *out, long const card[NUMBER_OF_SMOOTH_SWITCHES], char const *reason, long snapshot_default)
{
	int which;
	int set_ok;

	for (which = 0; which < NUMBER_OF_SMOOTH_SWITCHES; which++)
		out->card[which] = card[which] < 0 ? SMOOTH_CARD_DEFAULT : card[which];
	set_ok = out->card[SMOOTH_TICK_THREAD] != 0 && out->card[SMOOTH_TICK_SNAPSHOT] != 0 && out->card[SMOOTH_TICK_OVERLAP] != 0;
	resolve_one(out, SMOOTH_TICK_THREAD, reason, 0, set_ok, 1);
	resolve_one(out, SMOOTH_TICK_SNAPSHOT, reason, 0, set_ok, snapshot_default);
	/* (the overlap runs on the thread, from the snapshot's ring: a file of 0 for either leaves it nothing to run on) */
	resolve_one(out, SMOOTH_TICK_OVERLAP, reason, out->card[SMOOTH_TICK_THREAD] == 0 || out->card[SMOOTH_TICK_SNAPSHOT] == 0, set_ok, 1);
	/* (the governor does not need the others: it sheds only what is drawn) */
	resolve_one(out, SMOOTH_GOVERNOR, reason, 0, 1, 1);
}

/* ---------- the map */

char const *smooth_defaults_reason(struct smooth_defaults_map const *map)
{
	/* (halo/net_overlap.txt: a game joined as a client, and with 2 a game this machine hosts - with remote machines, or of its own players alone: a game with bots - may use the
	overlap; never network co-op. A server of this machine's players is still split screen if there are two or more of them: the rule below) */
	const int network_allowed = !map->coop && ((map->connection == SMOOTH_CONNECTION_CLIENT && map->net_overlap >= 1) ||
		(map->connection == SMOOTH_CONNECTION_SERVER && map->net_overlap >= 2));

	if (map->main_menu)
		return "the main menu's scene";
	if (map->connection == SMOOTH_CONNECTION_FILM)
		return "a saved film";
	if (map->connection == SMOOTH_CONNECTION_SERVER)
	{
		if (!network_allowed)
		{
			if (map->local_server)
				return "split screen (a game server of this machine's players)";
			return map->coop && map->net_overlap >= 2 ? "a hosted network co-op game" : "a hosted network game";
		}
	}
	else if (map->connection == SMOOTH_CONNECTION_CLIENT)
	{
		if (!network_allowed)
			return map->coop && map->net_overlap >= 1 ? "a joined network co-op game" : "a joined network game";
	}
	else if (map->connection != SMOOTH_CONNECTION_LOCAL)
		return "a network game";
	if (map->network_session && !network_allowed)
		return "a network session is open (system link or internet)";
	if (map->dedicated_server)
		return "a dedicated server";
	if (map->editor)
		return "the editor";
	if (map->recording)
		return "a recording";
	if (!map->interpolation)
		return "display.interpolation is off: there are no frames between ticks to overlap";
	if (map->spawn_players > 1 || map->local_players > 1)
		return "split screen";
	return NULL;
}

/* ---------- the cards */

long smooth_defaults_net_overlap(void)
{
	static long value = -1;

	if (value < 0)
	{
		const long card = wiiu_study_number("net_overlap.txt", 0);

		value = card < 0 ? 0 : card > 2 ? 2 : card;
		if (value > 0)
			platform_log("net overlap: halo/net_overlap.txt is %ld: %s may run their ticks beside the drawing", card, value >= 2 ? "a joined game and a hosted one" : "a joined game");
	}
	return value;
}

void smooth_defaults_read_cards(long card[NUMBER_OF_SMOOTH_SWITCHES])
{
	card[SMOOTH_TICK_THREAD] = wiiu_study_number("tick_thread.txt", SMOOTH_CARD_DEFAULT);
	card[SMOOTH_TICK_SNAPSHOT] = tick_snapshot_card();
	card[SMOOTH_TICK_OVERLAP] = wiiu_study_number("tick_overlap.txt", SMOOTH_CARD_DEFAULT);
	card[SMOOTH_GOVERNOR] = render_governor_card();
}

static void cards_once(void)
{
	if (!sd.cards_read)
	{
		smooth_defaults_read_cards(sd.now.card);
		sd.cards_read = 1;
	}
}

char const *smooth_defaults_card_text(long card, char *buffer, unsigned long size)
{
	if (card < 0)
		snprintf(buffer, size, "absent or -1");
	else
		snprintf(buffer, size, "%ld", card);
	return buffer;
}

/* ---------- the decision */

static char const *source_text(int source)
{
	switch (source)
	{
		case SMOOTH_SOURCE_CARD_OFF: return "off: its card file holds 0";
		case SMOOTH_SOURCE_MAP_OFF: return "off: not for this map";
		case SMOOTH_SOURCE_OTHERS_OFF: return "off: another of the three is 0";
		case SMOOTH_SOURCE_DEFAULT_ON: return "on, the default";
		default: return "on, its card file";
	}
}

static void decide(int mid_map)
{
	char const *reason;
	struct smooth_defaults decided;
	int which;
	char text[40];

	cards_once();
	reason = main_smooth_defaults_reason();
	smooth_defaults_resolve(&decided, sd.now.card, reason, 1);
	/* (a network game that halo/net_overlap.txt lets in gets the overlap, not the governor: the governor sheds what is drawn by the mean of the frames and was never tuned for a joined game,
	where it would also confound the comparison of the overlap with the serial loop. Its own card file and its own study) */
	if (!reason && main_smooth_defaults_net_game())
	{
		decided.value[SMOOTH_GOVERNOR] = 0;
		decided.source[SMOOTH_GOVERNOR] = SMOOTH_SOURCE_MAP_OFF;
	}
	sd.now = decided;
	sd.decided = 1;
	sd.map_ok = reason == NULL;
	sd.maps++;
	if (reason)
		snprintf(sd.label, sizeof(sd.label), "smooth defaults off: %s", reason);
	else
		snprintf(sd.label, sizeof(sd.label), "smooth defaults on");
	if (reason)
		platform_log("smooth defaults: OFF for this map: %s", reason);
	else
		platform_log(main_smooth_defaults_net_game() ? "smooth defaults: ON for this map (one local player, a network game: halo/net_overlap.txt)" :
			"smooth defaults: ON for this map (one local player, no network)");
	for (which = 0; which < NUMBER_OF_SMOOTH_SWITCHES; which++)
	{
		/* (a switch that is as it always was says nothing more than a line of all four, below; this says each, and what card file or default made it) */
		platform_log("smooth defaults:   %s %s (halo/%s: %s)", switch_labels[which], source_text(sd.now.source[which]), switch_names[which],
			smooth_defaults_card_text(sd.now.card[which], text, sizeof(text)));
	}
	if (sd.now.card[SMOOTH_TICK_THREAD] > 0 || sd.now.card[SMOOTH_TICK_SNAPSHOT] > 0 || sd.now.card[SMOOTH_TICK_OVERLAP] > 0 || sd.now.card[SMOOTH_GOVERNOR] > 0)
	{
		/* (a card file that says 1 is a request, not an order: the map decides) */
		if (reason)
			platform_log("smooth defaults:   a card file that holds 1 is on only for the maps the smooth settings are for: this one is not");
	}
	tick_overlap_decide(mid_map);
	render_governor_decide(sd.map_ok, (int)sd.now.value[SMOOTH_GOVERNOR]);
}

void smooth_defaults_new_map(void)
{
	decide(0);
}

void smooth_defaults_map_stops(void)
{
	decide(1);
}

long smooth_defaults_value(enum smooth_switch which)
{
	if (which < 0 || which >= NUMBER_OF_SMOOTH_SWITCHES)
		return 0;
	/* (before a first map starts, nothing is on: the start-up and the pregame run as the build without the smooth defaults) */
	return sd.decided ? sd.now.value[which] : 0;
}

char const *smooth_defaults_label(void)
{
	return sd.label;
}

int smooth_defaults_decided(void)
{
	return sd.decided;
}

int smooth_defaults_map_ok(void)
{
	return sd.decided && sd.map_ok;
}

unsigned long smooth_defaults_map_count(void)
{
	return sd.maps;
}

#else /* HALO_WIIU */

typedef int smooth_defaults_not_for_this_platform;

#endif
