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

/* The cards, read once (the platform's: wiiu_study_number, tick_snapshot.c's card, render_governor.c's text). */
void smooth_defaults_read_cards(long card[NUMBER_OF_SMOOTH_SWITCHES]);

/* The decision, at a map start; the reason it gets from the game (main.c's main_smooth_defaults_reason). The tick thread idle. */
void smooth_defaults_new_map(void);

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
