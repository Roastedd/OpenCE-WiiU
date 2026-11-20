/*
SMOOTH_DEFAULTS.H

The Wii U port is smooth by default for a game of one local player: the ticks run on a thread of their own beside the drawing (the tick overlap,
tick_overlap.h, with the tick snapshot, tick_snapshot.c) and the dynamic quality governor (render_governor.h) holds the frame near 22 ms. All of it
used to need card files (halo/tick_thread.txt, tick_snapshot.txt, tick_overlap.txt, governor.txt). These four files now mean:

  absent, or -1 (governor.txt: or a first word of -1 or "default")   the default: the smooth setting, when the map is one the smooth settings are for
  0 (governor.txt: "off")                                            off, whatever the map: an explicit opt-out (a test, a comparison)
  above 0 (governor.txt: "on", "1", or any tuning line)             on as it has always been, but still never for a map that is not one of the above

The map decides, at each map start (game_initialize_for_new_map, with the tick thread idle: smooth_defaults_new_map): exactly one local player, no
network session of any kind (hosted, joined, system link, internet: a network game stays the serial loop), no split screen, no saved film, no
dedicated server, not the main menu's scene, display.interpolation on. A map that is not that gets the settings of the build without the smooth
defaults (everything off but what display.dynamic_quality asks of the governor). The log says it, once a map:

  smooth defaults: ON for this map (one local player, no network)
  smooth defaults: OFF for this map: <the reason>

and then what each of the four came to and where it came from. The decision is taken again at every map start, so a level change, a return to the
main menu, a game started from it and a revert to a checkpoint's map each get their own, and the overlap's state (its ring, the snapshot's dirty
marks, the owners' queues' mode, the late state) is made again from nothing at each: tick_overlap_decide.

The pieces that decide are pure functions of the cards and the reason (smooth_defaults_resolve), tested apart from the game
(port/wiiu/tests/test_smooth_defaults.c).
*/

#ifndef __SMOOTH_DEFAULTS_H
#define __SMOOTH_DEFAULTS_H

#ifdef __cplusplus
extern "C" {
#endif

enum smooth_switch
{
	SMOOTH_TICK_THREAD,
	SMOOTH_TICK_SNAPSHOT,
	SMOOTH_TICK_OVERLAP,
	SMOOTH_GOVERNOR,
	NUMBER_OF_SMOOTH_SWITCHES
};

/* a card file's word: SMOOTH_CARD_DEFAULT (absent, or -1), SMOOTH_CARD_OFF (0), or a number above 0 (on; the snapshot's is its bits) */
#define SMOOTH_CARD_DEFAULT (-1L)
#define SMOOTH_CARD_OFF (0L)

/* What the game says of a map (main.c fills it): the rule that says whether the smooth settings are for it is pure (smooth_defaults_reason) */
struct smooth_defaults_map
{
	int main_menu;          /* the main menu's scene is loaded */
	int connection;         /* SMOOTH_CONNECTION_*: the game's connection */
	int network_session;    /* a network session that has peers or can have them: a client, or a server that accepts remote connections (system link, internet) */
	int local_server;       /* the game is run by a game server that accepts no remote connections: the split screen of this machine's players */
	int dedicated_server;
	int editor;
	int recording;          /* a movie is being recorded */
	int interpolation;      /* display.interpolation is on */
	int spawn_players;      /* the menu's player_spawn_count: the local players the game starts with (1, or 2 or more for split screen) */
	int local_players;      /* local_player_count, once the map has run a few ticks (0 until then: at a map start it still holds the last map's) */
	int net_overlap;        /* halo/net_overlap.txt: 0 no network game (the default), 1 a game joined as a client, 2 a client or the host of a game with remote machines (smooth_defaults_net_overlap) */
	int coop;               /* the game is network co-op (the campaign's scripts and cinematics run in it) */
};

enum
{
	SMOOTH_CONNECTION_LOCAL,
	SMOOTH_CONNECTION_SERVER,
	SMOOTH_CONNECTION_CLIENT,
	SMOOTH_CONNECTION_FILM,
	SMOOTH_CONNECTION_OTHER
};

/* halo/net_overlap.txt (HALO_NET_OVERLAP on the host), read once: 0 or absent, the default, keeps every network game the serial loop's; 1 lets a game joined as a client use the
tick overlap; 2 lets the host of a game with remote machines (not its split screen, not a dedicated server) use it too. Never for network co-op. A study switch: the build with the
file absent plays a network game as the build without it did. */
long smooth_defaults_net_overlap(void);

/* NULL for a map the smooth settings are for (exactly one local player, no film, no split screen, and no network of any kind unless halo/net_overlap.txt lets this one in), else why not, in words for the log */
char const *smooth_defaults_reason(struct smooth_defaults_map const *map);

struct smooth_defaults
{
	/* what the cards say (SMOOTH_CARD_*: for the governor 1 is on) */
	long card[NUMBER_OF_SMOOTH_SWITCHES];
	/* what the map gets: the number the switch's reader is given (the thread and the overlap: 0 or 1, the snapshot: its bits, the governor: 0 or 1) */
	long value[NUMBER_OF_SMOOTH_SWITCHES];
	/* where each came from: 0 off by a card file, 1 off by the map, 2 off for want of the others, 3 on by default, 4 on by a card file */
	int source[NUMBER_OF_SMOOTH_SWITCHES];
};

enum smooth_source
{
	SMOOTH_SOURCE_CARD_OFF,
	SMOOTH_SOURCE_MAP_OFF,
	SMOOTH_SOURCE_OTHERS_OFF,
	SMOOTH_SOURCE_DEFAULT_ON,
	SMOOTH_SOURCE_CARD_ON
};

/* The pure rule. reason is NULL for a map the smooth settings are for, else why not. snapshot_default is the snapshot's bits for the default (1). The
three of the tick overlap (thread, snapshot, overlap) are one feature: the overlap needs the other two, so a card file of 0 for any of them turns the
feature off, and the others' default is then off too (a half of it would be a study mode nobody asked for); a file above 0 on its own (the thread
alone is the ticks on another thread while the game waits) is as it has always been. The governor is its own. */
void smooth_defaults_resolve(struct smooth_defaults *out, long const card[NUMBER_OF_SMOOTH_SWITCHES], char const *reason, long snapshot_default);

/* display.wiiu_performance (the Video Settings menu's Performance row, config.toml): "default" leaves the four card files to say what they say, and a file that is absent (or -1) gives
the build's default; "smoother" makes an absent file mean 1 (the governor: on); "off" makes it mean 0. A card file that is there always wins. Read at each map start, so a change in
the menus applies at the next map. The same word decides halo/gpu_offload.txt's blends where that file is absent (wiiu_gpu_offload_set_default). */
enum
{
	SMOOTH_PERFORMANCE_DEFAULT,
	SMOOTH_PERFORMANCE_SMOOTHER,
	SMOOTH_PERFORMANCE_OFF
};

/* the mode a setting's text names (anything but "smoother" and "off", or no text, is the default) */
int smooth_defaults_performance_parse(char const *text);
/* The pure part: card words as the files gave them, with the mode's words in place of those that are SMOOTH_CARD_DEFAULT */
void smooth_defaults_performance_apply(long card[NUMBER_OF_SMOOTH_SWITCHES], int mode);

/* The cards, read once (the platform's: wiiu_study_number, tick_snapshot.c's card, render_governor.c's text). */
void smooth_defaults_read_cards(long card[NUMBER_OF_SMOOTH_SWITCHES]);

/* The decision, at a map start; the reason it gets from the game (main.c's main_smooth_defaults_reason). The tick thread idle. */
void smooth_defaults_new_map(void);
/* The same in the middle of a map that stops being one for the smooth defaults (a second local player, a session opened, a film): at the sync point, the tick thread idle; the owners' queues
keep their mode (a sound started as a ticket would be taken for a datum in the other) */
void smooth_defaults_map_stops(void);

/* What the current map gets of a switch. Before the first map start (nothing decided) it is 0: the start-up and the pregame run without the smooth defaults. */
long smooth_defaults_value(enum smooth_switch which);
/* whether a map has been decided, whether it is the smooth kind, and the decisions so far */
int smooth_defaults_decided(void);
int smooth_defaults_map_ok(void);
unsigned long smooth_defaults_map_count(void);

/* the decision in words, for the map summary's label: "smooth defaults on", or "smooth defaults off: <the reason>", or "" before any map */
char const *smooth_defaults_label(void);

/* a card word as the log writes it: "absent or -1", "0" or the number */
char const *smooth_defaults_card_text(long card, char *buffer, unsigned long size);

#ifdef __cplusplus
}
#endif

#endif
