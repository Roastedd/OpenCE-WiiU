/*
BOTS.C

Computer players (bots) for local multiplayer games (bots.count): the lobby
of a local (split screen) Slayer game takes as many as the setting asks for
(network_server_manager.c adds them), and each plays as a player does -
spawns, respawns, scores, is in the scores and the game's rules - with its
controls from here instead of a controller: the server decides every bot's
controls in bots_tick, when update_server_next_update (player_queues_new.c)
builds a tick's update, and queues them (bots_fill_action) where it queues a
player's from its pad. A bot has no view: nothing is drawn for it.

What a bot does each tick, in a player's terms:

- It looks for enemies (every third tick, the bots in turn): the nearest
  player of the other team (anyone in a game without teams) within its
  sight's reach and field of view whom a line of sight reaches (the AI's own
  sight test: the level, vehicles, scenery and machines block it), nearer
  for one in active camouflage; and it notices who shot it.
- It reacts after a moment (its skill's reaction time), then turns towards
  the target at a limited speed, with an aim error that settles while it
  keeps the target in sight (and leads a moving target by its weapon's
  projectile speed), and fires in bursts while its aim is on the target
  (released between bursts, so semi-automatic weapons fire too); it throws
  a grenade now and then at middle range and strikes with its weapon close
  up. It strafes while fighting, jumps now and then, and keeps its distance.
- Otherwise it goes where an enemy was last seen, else about the map (the
  player starting locations, the weapon and item spawns), most often to the
  place nearest a living enemy, as a player reads the motion tracker; now
  and then it picks up a better weapon lying near (the action button, as a
  player holds it), and swaps to its other weapon when this one is empty.
- It walks the paths the campaign's AI finds: the engine's own pathfinding
  over the level's walkable surfaces (source/ai/path.c), which every stock
  multiplayer map carries (tool builds it with the structure BSP: Blood
  Gulch has 2807 walkable surfaces of 4916; only the AI's placements,
  encounters and firing positions are missing). A bot that stops moving
  jumps and sidesteps, then finds another path. The multiplayer maps were
  never made for the AI, and the bots' searches read their own copy of the
  walkable surfaces: those with no room above them (a floor going on under
  a wall: Longest's bases) closed a few a tick from the game's start, and
  those under scenery or a machine the path's obstacle avoidance once found
  no way past (Sidewinder's blast door between the bases) closed from then
  on. A goal on ground the walkable surfaces do not reach is gone to as near
  as they go and on with jumps; an item it finds no way to is left be a
  while. Bots in each other's way give way by their order.

Skill (bots.skill): recruit, easy, normal, heroic, veteran, legendary - how
soon it reacts, how far and how wide it sees, how large its aim error is and
how fast that settles, how fast it turns, how tight its shots must be before
it fires, its bursts and its grenades. easy, normal, heroic and legendary
are the levels the bots always had; recruit (for new players) is below
easy, and veteran is halfway from heroic to legendary.

Fighting by level: a bot fights as a player of its level does, by
bots_combat.c's rules - a reaction and a look that are
never the same twice, an aim that trails the target at the lowest levels
and keeps an error at range, headshots once the shields are down, the
pistol paced at range and the sniper rifle zoomed (again, after a hit),
blows and grenades by level (a throw that leads the target's path, one at
the spot it was last seen), strafes and halts of the level's rhythm (short
jiggles at the top), jumps and crouches, a step in for a blow, a run in to
finish a won fight, a retreat along a path when losing (as its shields pop,
at the top), no sight of players far above or below where it looks, and
high ground when it roams. halo.log's minute line is followed by the
fight's counts.

The simulation: a bot decides from the game state at the tick and its own
random numbers (seeded per bot and game), and its path searches run in the
tick, so the bots do the same in two runs of the same input. Nothing of a
bot is in the game state: multiplayer games are never saved. Bots are in
local games, and in a game this machine hosts when bots.online is on (the
host decides their controls and builds the update that carries them as a
human's; the other machines see ordinary players: no message of the network
protocol changes); a client of a hosted game runs none.

A map with a navigation record in navs.pack (bots_nav.h, made by the bake of
bots_nav_bake.c) is walked by its graph (the "navigation" section below);
bots.objectives adds the objective play of capture the flag, oddball and king
of the hill and the timed power weapons (bots_objectives.h); the host's roster
(bots_director.h) and the API the screens call are the last section. halo.log has the map's preparation and each minute of play's bots
line: the searches and how many failed, the times a bot stood stuck, shots
and each bot's kills and deaths; debug.bot_trace logs each bot's state.
*/

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "main/main.h"
#include "objects/objects.h"
#include "units/units.h"
#include "units/bipeds.h"
#include "units/unit_definitions.h"
#include "items/items.h"
#include "items/weapons.h"
#include "items/weapon_definitions.h"
#include "items/projectile_definitions.h"
#include "physics/collisions.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "structures/structure_bsp_definitions.h"
#include "physics/collision_bsp_definitions.h"
#include "networking/network_game_globals.h"
#include "ai/path.h"
#include "devices/devices.h"
#include "devices/device_definitions.h"
#include "tag_files/tag_groups.h"
#include "tag_files/tag_files.h"

#include "bots.h"
#include "bots_budget.h"
#include "bots_combat.h"
#include "bots_director.h"
#include "bots_nav.h"
#include "bots_objectives.h"
#include "bots_nav_bake.h"
#include "bots_nav_load.h"
#include "bots_profile.h"
#include "bots_stats.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* port/linux/src/port_config.c */
const char *config_string(char const *name);
long config_integer(char const *name);
void platform_log(char const *format, ...);

/* ---------- constants */

#define BOTS_PI 3.14159265f
#define BOTS_DEGREES (BOTS_PI / 180.0f)
/* the most places a bot roams to (player starting locations, item spawns) */
#define BOTS_MAXIMUM_ROAM_POINTS 96
/* the path searches the bots make in one tick (each up to the engine's
1024 surfaces); a bot whose search waits asks again next tick */
#define BOTS_SEARCHES_A_TICK 3
/* the bots' size as the path searches know it (a biped's radius) */
#define BOTS_PATH_RADIUS 0.25f
/* how near a path's step a bot is when it has reached it (across) */
#define BOTS_STEP_REACHED 0.35f
/* a pathfinding surface's flag: walkable (path.c's) */
#define BOTS_PATHFINDING_WALKABLE_BIT 6
/* the items a bot remembers it found no way to, at once */
#define BOTS_UNREACHABLE_ITEMS 8

/* where a bot is going */
enum
{
	_bots_goal_none = 0,
	_bots_goal_enemy,
	_bots_goal_roam,
	/* a better weapon to pick up */
	_bots_goal_item,
	/* the game's objective (bots_objectives.h): the flag, the carrier, the hill */
	_bots_goal_objective,
	/* the place of a power weapon or powerup that spawns on a timer */
	_bots_goal_place,
};

/* the fight's counts in halo.log's minute line */
enum
{
	_bots_combat_count_grenades = 0,
	_bots_combat_count_blows,
	_bots_combat_count_blows_held,
	_bots_combat_count_head_aims,
	_bots_combat_count_retreats,
	_bots_combat_count_charges,
	_bots_combat_count_jumps,
	_bots_combat_count_halts,
	_bots_combat_count_paced,
	_bots_combat_count_rescopes,
	_bots_combat_count_prenades,
	_bots_combat_count_high_ground,
	_bots_combat_count_out_of_view,
	NUMBER_OF_BOTS_COMBAT_COUNTS,
};

/* ---------- structures */

struct bots_skill_definition
{
	char const *name;
	/* seconds from first seeing an enemy to firing */
	real reaction_seconds;
	/* how far it sees an enemy (world units), and how wide (degrees) */
	real sight_distance;
	real field_of_view_degrees;
	/* the aim error on taking a target (degrees), and the seconds it takes
	to settle to a third */
	real aim_error_degrees;
	real aim_settle_seconds;
	/* the most it turns a tick (degrees) */
	real turn_degrees_per_tick;
	/* fires while the aim is within this many times the target's size */
	real fire_cone;
	/* how much of a moving target's lead it takes (0 none, 1 all) */
	real lead;
	/* the chance a second of throwing a grenade at middle range (normal's;
	bots_combat_grenade's rules start from it) */
	real grenade_chance;
	/* the ticks of a burst, and of a pause between bursts */
	short burst_ticks;
	short pause_ticks;
};

/* the levels (bots_combat.h's order): easy, normal, heroic and legendary
are the bots' first levels as they were (their jumps in a fight are now
bots_combat_jump_delay's); recruit is below easy, and veteran halfway from
heroic to legendary in every number */
static struct bots_skill_definition const bots_skills[NUMBER_OF_BOTS_SKILLS] =
{
	{ "recruit", 1.4f, 16.0f, 80.0f, 22.0f, 2.5f, 5.0f, 2.6f, 0.0f, 0.0f, 4, 24 },
	{ "easy", 0.75f, 22.0f, 100.0f, 12.0f, 1.4f, 8.0f, 2.2f, 0.3f, 0.04f, 6, 14 },
	{ "normal", 0.45f, 32.0f, 120.0f, 7.0f, 0.9f, 13.0f, 1.8f, 0.7f, 0.10f, 9, 9 },
	{ "heroic", 0.30f, 42.0f, 140.0f, 4.0f, 0.6f, 20.0f, 1.5f, 0.8f, 0.16f, 12, 6 },
	{ "veteran", 0.24f, 48.0f, 150.0f, 3.1f, 0.48f, 25.0f, 1.4f, 0.9f, 0.19f, 14, 5 },
	{ "legendary", 0.18f, 55.0f, 160.0f, 2.2f, 0.35f, 30.0f, 1.25f, 1.0f, 0.22f, 16, 4 },
};

/* the ticks of its target's places a bot keeps (the aim that trails it) */
#define BOTS_TRACK_TICKS 16

/* a bot's fight by level, started over at each spawn */
struct bots_bot_combat
{
	struct bots_combat_retreat retreat;
	struct bots_combat_crouch crouch;
	/* the end of the strafe it last saw (a new strafe: a halt, maybe), and
	the halt's end */
	long last_strafe_until_tick;
	long halt_until_tick;
	/* the fight's last tick (a new fight after 2 s without one), and its
	next jump */
	long last_fight_tick;
	long next_jump_tick;
	/* the tick it strikes, waiting within reach (NONE: not waiting) */
	long melee_ready_tick;
	/* it runs in to finish its target */
	boolean charging;
	/* it aims at its target's head */
	boolean head_aim;
	/* a grenade in its hand until then: the aim leads the target's path by
	this much */
	long throw_until_tick;
	real throw_lead;
	/* a hit knocked its zoom out: no zoom (and past 10 units no shot) until
	then */
	long rescope_until_tick;
	/* the tick it began holding a shot for its weapon's error to settle
	(NONE: not holding) */
	long pace_since_tick;
	/* the throw at the spot its target was last seen: the sighting it was
	weighed for, and the aim held on the spot until then */
	long prenade_seen_tick;
	long prenade_until_tick;
	real_point3d prenade_point;
	/* its target's places, a tick each */
	struct
	{
		long tick;
		long player_index;
		real_point3d point;
	} track[BOTS_TRACK_TICKS];
};

/* what a bot has cost and done since the map's start (halo.log's cost line and the bots-sim block, which the headless
runs read: tools/harness/bots_sim.py); statistics only, nothing in a bot's play reads them */
struct bots_bot_counts
{
	/* its think time a tick (microseconds) */
	struct bots_think_stats think;
	/* the sight tests (rays) it cast, and its path searches that failed or ended short of the goal */
	long rays;
	long search_failures;
	long search_partials;
	/* the seconds it stood stuck while it walked (checked once a second) and the goals it gave up after three */
	long stuck_seconds;
	long stuck_giveups;
	/* its kills and deaths as of its last tick (the players are gone by the map's end) */
	long kills;
	long deaths;
};

struct bots_bot
{
	long player_index;
	long unit_index;
	unsigned long random;

	/* where it looks (radians) */
	real yaw;
	real pitch;

	/* its target */
	long target_player_index;
	long target_seen_tick;
	long target_first_seen_tick;
	real_point3d target_last_position;
	long target_last_surface_index;
	real aim_error_yaw;
	real aim_error_pitch;
	long next_look_tick;
	/* the last time it was hurt, and its vitality then */
	real last_vitality;
	long hurt_tick;

	/* where it is going */
	short goal_kind;
	real_point3d goal_point;
	long goal_surface_index;
	long goal_tick;
	short roam_index;
	/* the roaming place is towards an enemy (looked at again sooner) */
	boolean hunting;
	/* the item it fetches (_bots_goal_item), and when it last pressed the
	action button for it; when it first pressed it for this item */
	long item_index;
	long item_press_tick;
	long item_first_press_tick;
	/* the path there (steps of the engine's path), and when it was found */
	struct path_result path;
	long path_tick;
	long path_failures;
	/* the same by the navigation graph (bots_nav.h), when the map has one: the path, the goal it was made for, the link it is on (a jump, a
	drop, a teleport has a run-up and a flight), the links it found bad and until when */
	struct bots_nav_path nav_path;
	real_point3d nav_goal;
	long nav_goal_area;
	long nav_path_tick;
	long nav_state_tick;
	short nav_state;
	boolean nav_jump;
	real nav_throttle;
	unsigned short nav_bad_links[BOTS_NAV_BAD_LINKS];
	long nav_bad_until[BOTS_NAV_BAD_LINKS];
	short nav_bad_next;
	long nav_progress_tick;
	/* the graph's follower: where the bot was a tick ago, the ticks it has barely moved, the ticks the engine's surfaces lead it (no area in sight, or blocked), its last fight */
	real_point3d nav_prev_position;
	long nav_prev_tick;
	short nav_slow_ticks;
	long nav_blind_until;
	long nav_fought_tick;
	/* the fight's strafe: where the bot was a tick ago and the ticks it has barely moved while it moves */
	real_point3d fight_prev_position;
	long fight_prev_tick;
	short fight_slow_ticks;
	/* the tour (debug.bots_tour): the goal it is on, what kind, since when */
	/* the objective goal's kind, when it was decided; the timed item it is going to and until when it waits there */
	short objective_kind;
	long objective_tick;
	long place_index;
	long place_until_tick;
	long place_check_tick;
	short tour_kind;
	long tour_count;
	long tour_since_tick;
	long tour_target_unit;
	real_point3d tour_goal;
	/* off a surface closed to the searches (beside a door): away from what
	closed it, until then */
	long escape_until_tick;
	real escape_direction;
	/* its path ends short of the goal (the search found no way there: a
	platform the walkable surfaces do not reach); at its end, straight on
	with jumps until then */
	boolean path_short;
	long climb_until_tick;
	long climb_start_tick;
	real climb_best_distance;
	/* where it was last tick (a teleporter's jump) */
	real_point3d last_position;
	/* an item it found no way to (on a ledge), not fetched again until then */
	long unreachable_item_indices[BOTS_UNREACHABLE_ITEMS];
	long unreachable_item_until_ticks[BOTS_UNREACHABLE_ITEMS];
	long unreachable_item_next;
	/* whether it moves, checked once a second */
	real_point3d stuck_position;
	long stuck_tick;
	short stuck_count;
	long unstick_until_tick;
	real unstick_direction;

	/* fighting: the side it strafes to and until when; bursts */
	real strafe_sign;
	long strafe_until_tick;
	long burst_until_tick;
	long pause_until_tick;
	long grenade_tick;
	long melee_tick;
	long item_check_tick;

	/* the buttons held last tick (a press is a tick held, then released) */
	unsigned long previous_control_flags;

	/* its own skill and temperament (bots_profile.h) */
	struct bots_profile profile;

	/* the controls it built last, and the ticks it has held them for the budget (the shedding's top step) */
	struct player_action last_action;
	boolean last_action_valid;
	short held_ticks;

	struct bots_bot_combat combat;

	/* (debug.bot_trace) its path searches */
	long searches;

	struct bots_bot_counts counts;
};

/* a place where a power weapon or a powerup spawns on a timer */
struct bots_timed_item
{
	real_point3d position;
	short period_seconds;
	short item_class;
	long equipment_index;
	/* the bot (its index) on its way there */
	long claimed_by;
};

/* the level the screens or the host asked for a bot by its number (+1; 0: none), kept across the bot's player being made */
static short bots_requested_skill[BOTS_MAXIMUM];

/* ---------- globals */

static struct
{
	boolean map_ready;
	long roam_point_count;
	real_point3d roam_points[BOTS_MAXIMUM_ROAM_POINTS];
	long roam_surfaces[BOTS_MAXIMUM_ROAM_POINTS];
	struct bots_bot bots[BOTS_MAXIMUM];
	/* the skill setting (bots.skill), read once a tick for every bot: each bot's level is its profile's, which follows this
	unless the bot has one of its own */
	short setting_skill;
	/* the fight's counts for halo.log */
	long combat_counts[NUMBER_OF_BOTS_COMBAT_COUNTS];
	/* the path search's state (80 KB: the engine's 1024 nodes, heap and
	table), allocated once a game has bots */
	struct path_state *search;

	/* the statistics of halo.log's line */
	long ticks;
	long searches;
	long search_failures;
	/* searches that stopped short of the goal (a path to as near as they
	came), and those that ran out of nodes */
	long search_partials;
	long search_overflows;
	/* paths taken without the obstacle avoidance, which found no way */
	long search_unavoided;
	/* the paths whose smoothing went nowhere, taken unsmoothed */
	long search_unsmoothed;
	/* the times a bot stood stuck while it walked, and the goals given up
	after three */
	long stuck_events;
	long stuck_giveups;
	long last_report_tick;

	/* the cost of all the bots in a tick (microseconds), and the sight tests cast */
	struct bots_think_stats tick_think;
	long rays;
	/* the bot being thought for (its sight tests are counted to it) */
	struct bots_bot *active_bot;
	/* the map's name, for the bots-sim block, and whether the block was said (once a game) */
	char map_name[64];
	boolean sim_reported;
	/* the clock at the first tick of bots (the run's time a tick) */
	long long first_tick_microseconds;
	/* the map's navigation graph (bots_nav.h), once read, and the scratch of its searches; the setting (bots.nav) and what the bots did by it */
	struct bots_nav *nav;
	struct bots_nav_search nav_search;
	boolean nav_search_ready;
	boolean nav_enabled;
	boolean nav_sync;
	boolean nav_reported;
	long nav_searches, nav_search_failures, nav_search_partials, nav_search_nodes;
	long nav_links_done[NUMBER_OF_BOTS_NAV_LINK_TYPES], nav_links_failed[NUMBER_OF_BOTS_NAV_LINK_TYPES];
	long nav_links_marked_bad, nav_off_graph, nav_fallbacks, nav_shortcuts, nav_logged;
	long nav_blocked, nav_blind, nav_after_fight, fight_flips, nav_learned_bad;
	/* bots.nav_tweaks: the follower's options under trial (BOTS_NAV_TWEAK_*) */
	long nav_tweaks;
	/* the links a stuck or blocked bot found bad, for every bot to avoid for a while */
	unsigned short shared_bad_links[BOTS_NAV_SHARED_BAD];
	long shared_bad_until[BOTS_NAV_SHARED_BAD];
	short shared_bad_next;
	/* bots.objectives: the objective play and the timed items; the snapshot of the game for it, when it was made, who is in it; what the bots did by it */
	boolean objectives_enabled;
	struct bots_objective_state objective_state;
	long objective_tick;
	long objective_players[BOTS_OBJECTIVE_MEMBERS];
	long objective_units[BOTS_OBJECTIVE_MEMBERS];
	long objective_counts[NUMBER_OF_BOTS_OBJECTIVES];
	long objective_stale;
	struct bots_timed_item timed_items[48];
	long timed_item_count;
	long timed_item_trips;
	/* debug.bots_tour: the bots meet nobody, and go from one place to the next (each other, the flags, the starts, the items); what they did */
	boolean tour;
	long stuck_log_on;
	long tour_reached[4], tour_given_up[4], tour_ticks[4];

	/* the tick being built (see bots_now), the game's time when the last was, and the ticks built while the game stood still
	(a frame that owed several ticks: they decide on the world as the frame found it) */
	long now;
	long world_time;
	long stale_ticks;

	/* the share of a tick the bots may take (bots_budget.c), and the cap on how many there may be */
	struct bots_budget budget;
	short maximum_bots;
	/* bots.sim_ticks, read at the map's start */
	long sim_ticks;
	/* the bots in the farther half from the players this tick (those the top step holds) */
	boolean far_from_humans[BOTS_MAXIMUM];
	/* the controls built this tick, by the player's index (bots_fill_action takes them), and the tick they were built for */
	struct player_action actions[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	boolean action_valid[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
} bots_globals;

/* the walkable surfaces the bots' searches read: a copy of the structure's
pathfinding surfaces, less those learnt to be in the way (scenery and
machines) and those with no room above them */
static struct
{
	struct structure_bsp const *source;
	struct structure_bsp structure;
	byte *walkable;
	/* the objects whose surfaces are closed */
	long learned[32];
	long learned_count;
	long closed_count;
	/* the surfaces looked at for room above them, and those closed */
	long scan_index;
	boolean scan_done;
	long roofed_count;
	long scan_ticks;
} bots_paths;

/* ---------- the tick being built

The bots decide in update_server_next_update (bots_tick), before the tick's update is built. A frame that owes several ticks
builds several updates before the game runs the first of them, so the game's time stands still while they are built: a bot's
clock is the tick it is building, which is the game's time for the first and one more for each after it (and the game's time
again, whatever it is, once the world has moved on). Run one tick a frame (debug.fixed_step, a game that keeps up) the two
are the same. */
static long bots_now(void)
{
	return bots_globals.now;
}

/* ---------- the clock (the cost counts; and a budget in time: bots_budget.c) */

#ifdef HALO_WIIU
/* port/wiiu/src/wiiu_tick_parts.c: the console's time base, in microseconds */
long long wiiu_microseconds_now(void);
#if !defined(HALO_PROFILE)
/* the tick parts line of the log (game.c): the bots are one of its parts */
long long wiiu_tick_part_now(void);
void wiiu_tick_part(const char *name, long long started);
#define BOTS_TICK_PART
#endif

static long long bots_clock_microseconds(void)
{
	return wiiu_microseconds_now();
}
#else
/* port/linux/src/xbox_kernel.c: the game's clock in microseconds */
long long platform_microseconds(void);

static long long bots_clock_microseconds(void)
{
	return platform_microseconds();
}
#endif

/* ---------- settings */

/* the most bots there may be (bots.max, up to BOTS_MAXIMUM) */
static long bots_maximum(void)
{
	long maximum = config_integer("bots.max");

	return maximum < 0 ? 0 : maximum > BOTS_MAXIMUM ? BOTS_MAXIMUM : maximum;
}

/* a game of this engine may have bots: a slayer game always, the games with an objective (capture the flag, oddball, king of the hill) when
bots.objectives is on (their bots are played by bots_objectives.h) */
boolean bots_game_allowed(long game_engine_index)
{
	if (game_engine_index == game_engine_slayer)
		return TRUE;
	return config_integer("bots.objectives") != 0 &&
		(game_engine_index == game_engine_ctf || game_engine_index == game_engine_oddball || game_engine_index == game_engine_king);
}

long bots_wanted_count(void)
{
	long count = config_integer("bots.count");

	count = count < 0 ? 0 : count > BOTS_MAXIMUM ? BOTS_MAXIMUM : count;
	return count > bots_maximum() ? bots_maximum() : count;
}

short bots_skill(void)
{
	char const *name = config_string("bots.skill");
	short skill;

	for (skill = 0; skill < NUMBER_OF_BOTS_SKILLS; skill++)
	{
		if (!strcmp(name, bots_skills[skill].name))
			return skill;
	}
	return _bots_skill_normal;
}

short bots_teams(void)
{
	return strcmp(config_string("bots.teams"), "against") ? _bots_teams_even : _bots_teams_against;
}

int bots_machine_is_bot(long machine_index)
{
	return machine_index >= BOTS_FIRST_MACHINE && machine_index < BOTS_FIRST_MACHINE + BOTS_MAXIMUM;
}

void bots_network_player(long bot_index, struct network_player *player)
{
	/* (the Marines of Halo's campaign; eleven characters at most) */
	static char const *const names[BOTS_MAXIMUM] =
	{
		"Johnson", "Jenkins", "Mendoza", "Bisenti", "Stacker", "Polaski", "Chips", "Kojo",
		"Rawley", "Riley", "Wellsley", "Hall", "Fitzgerald", "Locklear", "Mitchell",
	};
	char const *name = names[bot_index % BOTS_MAXIMUM];
	long index;

	csmemset(player, 0, sizeof(*player));
	for (index = 0; index < (long)NUMBEROF(player->name) - 1 && name[index]; index++)
		player->name[index] = (wchar_t)name[index];
	player->name[index] = 0;
	player->primary_color_index = NONE;
	player->icon_index = 0;
	player->machine_index = (char)(BOTS_FIRST_MACHINE + bot_index);
	player->controller_index = 0;
	player->team_index = 0;
	player->player_list_index = NONE;
}

/* ---------- small helpers */

static unsigned long bots_random(struct bots_bot *bot)
{
	/* (xorshift32) */
	unsigned long x = bot->random ? bot->random : 0x9E3779B9UL;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	x &= 0xFFFFFFFFUL;
	bot->random = x;
	return x;
}

/* 0 to 1 */
static real bots_random_real(struct bots_bot *bot)
{
	return (real)(bots_random(bot) & 0xFFFFFF) / (real)0x1000000;
}

/* -1 to 1 */
static real bots_random_signed(struct bots_bot *bot)
{
	return bots_random_real(bot) * 2.0f - 1.0f;
}

static real bots_angle_difference(real a, real b)
{
	real difference = (real)fmod(a - b, 2.0f * BOTS_PI);

	if (difference > BOTS_PI)
		difference -= 2.0f * BOTS_PI;
	else if (difference < -BOTS_PI)
		difference += 2.0f * BOTS_PI;
	return difference;
}

static real bots_distance2d(real_point3d const *a, real_point3d const *b)
{
	real dx = a->x - b->x, dy = a->y - b->y;

	return (real)sqrt(dx * dx + dy * dy);
}

static real bots_distance3d(real_point3d const *a, real_point3d const *b)
{
	real dx = a->x - b->x, dy = a->y - b->y, dz = a->z - b->z;

	return (real)sqrt(dx * dx + dy * dy + dz * dz);
}

/* the surface of the level under a point (a step up included), and the
ground point there; NONE over nothing */
static long bots_surface_below(real_point3d const *point, real_point3d *ground)
{
	struct collision_result collision;
	real_point3d start = *point;
	real_vector3d down = { 0.0f, 0.0f, -6.0f };

	start.z += 0.4f;
	if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
		&start, &down, NONE, &collision) && collision.type == _collision_result_structure)
	{
		if (ground)
			*ground = collision.point;
		return collision.surface_index;
	}
	return NONE;
}

/* the surface a unit stands on (a biped's support surface), and its point */
static long bots_unit_surface(long unit_index, real_point3d *point)
{
	struct object_datum *object = object_get(unit_index);
	struct biped_datum *biped = biped_try_and_get(unit_index);

	*point = object->object.position;
	if (object->object.parent_object_index != NONE)
		return NONE;
	if (biped && biped->biped.support_surface_index != NONE)
		return biped->biped.support_surface_index;
	return bots_surface_below(&object->object.position, point);
}

/* the unit's current weapon, NONE without one */
static long bots_current_weapon(struct unit_datum *unit)
{
	if (unit->unit.current_weapon_index < 0 || unit->unit.current_weapon_index >= MAXIMUM_WEAPONS_PER_UNIT)
		return NONE;
	return unit->unit.weapon_object_indices[unit->unit.current_weapon_index];
}

/* whether a weapon has no rounds left (one with magazines) */
static boolean bots_weapon_empty(long weapon_index)
{
	struct weapon_datum *weapon = weapon_get(weapon_index);
	struct weapon_definition *definition = weapon_definition_get(weapon->definition_index);

	if (definition->weapon.magazines.count <= 0)
		return FALSE;
	return weapon->weapon.magazines[0].rounds_loaded + weapon->weapon.magazines[0].rounds_total <= 0;
}

/* how full the weapon's magazine is, 0 to 1 (1 for one without) */
static real bots_weapon_loaded(long weapon_index)
{
	struct weapon_datum *weapon = weapon_get(weapon_index);
	struct weapon_definition *definition = weapon_definition_get(weapon->definition_index);
	struct weapon_magazine_definition *magazine;

	if (definition->weapon.magazines.count <= 0)
		return 1.0f;
	magazine = TAG_BLOCK_GET_ELEMENT(&definition->weapon.magazines, 0, struct weapon_magazine_definition);
	if (magazine->rounds_loaded_maximum <= 0)
		return 1.0f;
	return (real)weapon->weapon.magazines[0].rounds_loaded / (real)magazine->rounds_loaded_maximum;
}

/* the speed of the weapon's projectile (world units a tick; 0 for one
that hits at once or none) */
static real bots_projectile_speed_compute(long definition_index)
{
	struct weapon_definition *definition = weapon_definition_get(definition_index);

	if (definition->weapon.triggers.count > 0)
	{
		struct weapon_trigger_definition *trigger =
			TAG_BLOCK_GET_ELEMENT(&definition->weapon.triggers, 0, struct weapon_trigger_definition);

		if (trigger->projectile.index != NONE)
		{
			struct projectile_definition *projectile = projectile_definition_get(trigger->projectile.index);

			return 0.5f * (projectile->projectile.initial_velocity + projectile->projectile.final_velocity);
		}
	}
	return 0.0f;
}

/* how much a bot wants a weapon (the stock weapons, by their tags' names),
-1 for the game's own (a flag, a ball) */
static short bots_weapon_value_compute(long definition_index)
{
	static struct
	{
		char const *name;
		short value;
	} const values[] =
	{
		{ "rocket", 9 }, { "sniper", 8 }, { "shotgun", 8 }, { "fuel rod", 7 }, { "plasma_cannon", 7 },
		{ "plasma pistol", 3 }, { "pistol", 7 }, { "assault rifle", 6 }, { "needler", 5 }, { "plasma rifle", 5 },
		{ "flamethrower", 4 }, { "flag", -1 }, { "ball", -1 }, { "skull", -1 },
	};
	char const *name = tag_get_name(definition_index);
	long index;

	if (!name)
		return 4;
	for (index = 0; index < (long)NUMBEROF(values); index++)
	{
		if (strstr(name, values[index].name))
			return values[index].value;
	}
	return 4;
}

/* how a bot fires the weapon: the ticks between presses of a weapon fired a
press at a time (a latched trigger: the pistols, the sniper rifle, the
rocket launcher; or one that charges while held: the plasma pistol), 0 for
one held down in bursts */
static short bots_weapon_tap_ticks_compute(long definition_index)
{
	struct weapon_definition *definition = weapon_definition_get(definition_index);
	struct weapon_trigger_definition *trigger;
	real rate;

	if (definition->weapon.triggers.count <= 0)
		return 0;
	trigger = TAG_BLOCK_GET_ELEMENT(&definition->weapon.triggers, 0, struct weapon_trigger_definition);
	if (!TEST_FLAG(trigger->flags, _weapon_trigger_latched_bit) && trigger->charging_time <= 0.0f)
		return 0;
	rate = trigger->initial_rate_of_fire > 0.0f ? trigger->initial_rate_of_fire : 3.0f;
	return (short)PIN((long)((real)TICKS_PER_SECOND / rate + 0.5f), 2, TICKS_PER_SECOND * 2);
}

/* the distance a bot keeps from its enemy with the weapon */
static real bots_weapon_range_compute(long definition_index)
{
	char const *name = tag_get_name(definition_index);

	if (!name)
		return 6.0f;
	if (strstr(name, "shotgun") || strstr(name, "flamethrower"))
		return 2.0f;
	if (strstr(name, "sniper"))
		return 18.0f;
	if (strstr(name, "rocket") || strstr(name, "fuel rod") || strstr(name, "plasma_cannon"))
		return 12.0f;
	if (strstr(name, "plasma pistol") || strstr(name, "needler"))
		return 5.0f;
	if (strstr(name, "pistol"))
		return 9.0f;
	return 7.0f;
}

/* how a weapon definition aims (the fight by level): whether it kills with
a shot to the head (the pistol, the sniper rifle), whether its shots are
true only zoomed (the sniper rifle: its trigger's error when unzoomed), and
whether its error grows as it fires (the pistol) */
static void bots_weapon_aim_compute(long definition_index, boolean *precision, boolean *sniper, boolean *blooms)
{
	struct weapon_definition *definition = weapon_definition_get(definition_index);
	char const *name = tag_get_name(definition_index);

	*precision = name && ((strstr(name, "pistol") && !strstr(name, "plasma")) || strstr(name, "sniper"));
	*sniper = FALSE;
	*blooms = FALSE;
	if (definition->weapon.triggers.count > 0)
	{
		struct weapon_trigger_definition *trigger =
			TAG_BLOCK_GET_ELEMENT(&definition->weapon.triggers, 0, struct weapon_trigger_definition);

		*sniper = TEST_FLAG(trigger->flags, _weapon_trigger_use_error_when_unzoomed_bit) &&
			definition->weapon.zoom_level_count > 0;
		*blooms = trigger->projectile_error_angle_upper_bound > trigger->projectile_error_angle_lower_bound + 0.001f;
	}
}

/* what the bots know of a weapon definition (its value, the distance kept
with it, how it fires, its projectile's speed, how it aims), made once a map: the
functions above read only its tag, its name and its trigger, which a map
never changes (a bot asked them of its weapon every tick: a dozen name
searches and a few tag lookups) */
#define BOTS_WEAPON_KINDS 32
struct bots_weapon_kind
{
	long definition_index;
	short value;
	short tap_ticks;
	real range;
	real projectile_speed;
	boolean precision;
	boolean sniper;
	boolean blooms;
};

static struct
{
	long count;
	struct bots_weapon_kind kinds[BOTS_WEAPON_KINDS];
	/* (the one made when the table is full) */
	struct bots_weapon_kind spare;
} bots_weapon_kinds;

static struct bots_weapon_kind const *bots_weapon_kind(long definition_index)
{
	struct bots_weapon_kind *kind;
	long index;

	for (index = 0; index < bots_weapon_kinds.count; index++)
	{
		if (bots_weapon_kinds.kinds[index].definition_index == definition_index)
			return &bots_weapon_kinds.kinds[index];
	}
	kind = bots_weapon_kinds.count < BOTS_WEAPON_KINDS ? &bots_weapon_kinds.kinds[bots_weapon_kinds.count++] :
		&bots_weapon_kinds.spare;
	kind->definition_index = definition_index;
	kind->value = bots_weapon_value_compute(definition_index);
	kind->tap_ticks = bots_weapon_tap_ticks_compute(definition_index);
	kind->range = bots_weapon_range_compute(definition_index);
	kind->projectile_speed = bots_projectile_speed_compute(definition_index);
	bots_weapon_aim_compute(definition_index, &kind->precision, &kind->sniper, &kind->blooms);
	return kind;
}

static short bots_weapon_value(long definition_index)
{
	return bots_weapon_kind(definition_index)->value;
}

static real bots_projectile_speed(long weapon_index)
{
	return bots_weapon_kind(weapon_get(weapon_index)->definition_index)->projectile_speed;
}

static short bots_weapon_tap_ticks(long weapon_index)
{
	return bots_weapon_kind(weapon_get(weapon_index)->definition_index)->tap_ticks;
}

static real bots_weapon_range(long weapon_index)
{
	return weapon_index != NONE ? bots_weapon_kind(weapon_get(weapon_index)->definition_index)->range : 6.0f;
}

/* whether the bot found no way to the item a while ago (left be thirty
seconds), and the item so remembered (the oldest of those kept forgotten) */
static boolean bots_item_unreachable(struct bots_bot const *bot, long item_index)
{
	long index;

	for (index = 0; index < BOTS_UNREACHABLE_ITEMS; index++)
	{
		if (bot->unreachable_item_indices[index] == item_index && bots_now() < bot->unreachable_item_until_ticks[index])
			return TRUE;
	}
	return FALSE;
}

static void bots_item_set_unreachable(struct bots_bot *bot, long item_index)
{
	long slot;

	/* (one remembered already: kept longer) */
	for (slot = 0; slot < BOTS_UNREACHABLE_ITEMS; slot++)
	{
		if (bot->unreachable_item_indices[slot] == item_index && bots_now() < bot->unreachable_item_until_ticks[slot])
		{
			bot->unreachable_item_until_ticks[slot] = bots_now() + TICKS_PER_SECOND * 30;
			return;
		}
	}
	slot = bot->unreachable_item_next;
	bot->unreachable_item_indices[slot] = item_index;
	bot->unreachable_item_until_ticks[slot] = bots_now() + TICKS_PER_SECOND * 30;
	bot->unreachable_item_next = (slot + 1) % BOTS_UNREACHABLE_ITEMS;
}

/* a weapon lying near the bot worth fetching: one better than the one it
holds; NONE for none */
static long bots_find_item(struct bots_bot *bot, struct unit_datum *unit, real_point3d const *position,
	real maximum_distance)
{
	struct object_iterator iterator;
	long best_index = NONE;
	real best_score = REAL_MAX;
	long current = bots_current_weapon(unit);
	short current_value = current != NONE ? bots_weapon_value(weapon_get(current)->definition_index) : 0;

	object_iterator_new(&iterator, _object_mask_weapon, 0);
	while (object_iterator_next(&iterator))
	{
		struct item_datum *item = item_get(iterator.index);
		real distance, score;
		short value;
		short slot;
		boolean held = FALSE;

		if (item->object.parent_object_index != NONE ||
			bots_item_unreachable(bot, iterator.index) ||
			!TEST_FLAG(item->object.flags, _object_connected_to_map_bit) ||
			TEST_FLAG(item->item.flags, _item_attached_to_unit_bit))
		{
			continue;
		}
		distance = bots_distance3d(&item->object.position, position);
		if (distance > maximum_distance)
			continue;
		value = bots_weapon_value(item->definition_index);
		/* (one it holds already: only its ammunition, which walking over
		takes) */
		for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
		{
			long other = unit->unit.weapon_object_indices[slot];

			if (other != NONE && weapon_get(other)->definition_index == item->definition_index)
				held = TRUE;
		}
		if (held || value <= current_value)
			continue;
		score = distance - (real)(value - current_value) * 3.0f;
		if (score < best_score)
		{
			best_score = score;
			best_index = iterator.index;
		}
	}
	return best_index;
}

/* ---------- the map */

static void bots_add_roam_point(real_point3d const *point)
{
	real_point3d ground;
	long surface_index;

	if (bots_globals.roam_point_count >= BOTS_MAXIMUM_ROAM_POINTS)
		return;
	surface_index = bots_surface_below(point, &ground);
	if (surface_index == NONE)
		return;
	bots_globals.roam_points[bots_globals.roam_point_count] = ground;
	bots_globals.roam_surfaces[bots_globals.roam_point_count] = surface_index;
	bots_globals.roam_point_count++;
}

/* the bots' walkable surfaces let go */
static void bots_paths_release(void)
{
	if (bots_paths.walkable)
		free(bots_paths.walkable);
	csmemset(&bots_paths, 0, sizeof(bots_paths));
}

/* the surface's corners (at most BOTS_SURFACE_CORNERS), from its ring of
edges; 0 for one not the bsp's */
#define BOTS_SURFACE_CORNERS 16
static long bots_surface_corners(struct collision_bsp const *bsp, long surface_index, real_point3d *corners)
{
	struct collision_surface const *surface;
	long edge_index, count = 0;

	if (surface_index < 0 || surface_index >= bsp->surfaces.count)
		return 0;
	surface = TAG_BLOCK_GET_ELEMENT(&bsp->surfaces, surface_index, struct collision_surface);
	edge_index = surface->first_edge_index;
	do
	{
		struct collision_edge const *edge;
		boolean right;

		if (edge_index < 0 || edge_index >= bsp->edges.count)
			return 0;
		edge = TAG_BLOCK_GET_ELEMENT(&bsp->edges, edge_index, struct collision_edge);
		right = edge->surface_indices[1] == surface_index;
		if (!VALID_INDEX(edge->vertex_indices[right], bsp->vertices.count))
			return 0;
		corners[count++] = TAG_BLOCK_GET_ELEMENT(&bsp->vertices, edge->vertex_indices[right], struct collision_vertex)->point;
		edge_index = edge->edge_indices[right];
	} while (edge_index != surface->first_edge_index && count < BOTS_SURFACE_CORNERS);
	return count;
}

/* whether the object stands across the surface: a line over it at a biped's
knees or chest, from its middle to a little past an edge's, meets it, or
its middle is under it */
static boolean bots_surface_blocked(long object_index, real_point3d const *corners, long corner_count,
	real_point3d const *middle)
{
	static real const heights[] = { 0.35f, 1.0f };
	unsigned long flags = FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_back_facing_surfaces_bit) |
		FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit) |
		FLAG(_collision_test_objects_machines_bit);
	struct collision_result collision;
	real_point3d start = *middle;
	real_vector3d up = { 0.0f, 0.0f, 1.6f };
	long height, corner;

	start.z += 0.05f;
	if (collision_test_vector(flags, &start, &up, NONE, &collision) && collision.type == _collision_result_object &&
		collision.object_index == object_index)
	{
		return TRUE;
	}
	for (height = 0; height < (long)NUMBEROF(heights); height++)
	{
		start.z = middle->z + heights[height];
		for (corner = 0; corner < corner_count; corner++)
		{
			real_point3d const *a = &corners[corner], *b = &corners[(corner + 1) % corner_count];
			real_vector3d vector;
			real length, past;

			vector.i = 0.5f * (a->x + b->x) - start.x;
			vector.j = 0.5f * (a->y + b->y) - start.y;
			vector.k = 0.5f * (a->z + b->z) + heights[height] - start.z;
			/* (on a little past the edge: a door may stand on it) */
			length = (real)sqrt(vector.i * vector.i + vector.j * vector.j + vector.k * vector.k);
			if (length < 0.01f)
				continue;
			past = (length + 0.4f) / length;
			vector.i *= past;
			vector.j *= past;
			vector.k *= past;
			if (collision_test_vector(flags, &start, &vector, NONE, &collision) &&
				collision.type == _collision_result_object && collision.object_index == object_index)
			{
				return TRUE;
			}
		}
	}
	return FALSE;
}

/* the bots' copy of the structure's walkable surfaces, made (the searches
take it from then on); FALSE without one */
static boolean bots_paths_copy(void)
{
	struct structure_bsp *structure = global_structure_bsp_get();
	long count;

	if (!structure || structure->pathfinding_surfaces.count <= 0 || structure->collision_bsp.count <= 0 ||
		(bots_paths.source && bots_paths.source != structure))
	{
		return FALSE;
	}
	if (bots_paths.walkable)
		return TRUE;
	count = structure->pathfinding_surfaces.count;
	bots_paths.walkable = (byte *)malloc((size_t)count);
	if (!bots_paths.walkable)
		return FALSE;
	csmemcpy(bots_paths.walkable, structure->pathfinding_surfaces.address, (size_t)count);
	bots_paths.structure = *structure;
	bots_paths.structure.pathfinding_surfaces.address = bots_paths.walkable;
	bots_paths.source = structure;
	return TRUE;
}

/* a walkable surface of the bots' copy: its corners, its middle and how far
its corners are from it; FALSE for one not walkable (or not the bsp's) */
static boolean bots_paths_walkable_surface(struct collision_bsp const *bsp, long surface_index,
	real_point3d *corners, long *corner_count, real_point3d *middle, real *reach)
{
	long corner;

	if (surface_index >= bots_paths.structure.pathfinding_surfaces.count ||
		!TEST_FLAG(bots_paths.walkable[surface_index], BOTS_PATHFINDING_WALKABLE_BIT))
	{
		return FALSE;
	}
	*corner_count = bots_surface_corners(bsp, surface_index, corners);
	if (*corner_count < 3)
		return FALSE;
	middle->x = middle->y = middle->z = 0.0f;
	for (corner = 0; corner < *corner_count; corner++)
	{
		middle->x += corners[corner].x / (real)*corner_count;
		middle->y += corners[corner].y / (real)*corner_count;
		middle->z += corners[corner].z / (real)*corner_count;
	}
	*reach = 0.0f;
	for (corner = 0; corner < *corner_count; corner++)
		*reach = MAX(*reach, bots_distance3d(middle, &corners[corner]));
	return TRUE;
}

/* the walkable surfaces an object stands across closed to the bots' searches */
static void bots_paths_close_object(long object_index, char const *why)
{
	struct object_datum *object = object_get(object_index);
	struct collision_bsp const *bsp;
	long surface_index, closed = 0;

	if (bots_paths.learned_count >= (long)NUMBEROF(bots_paths.learned) || !bots_paths_copy())
		return;
	bots_paths.learned[bots_paths.learned_count++] = object_index;
	bsp = TAG_BLOCK_GET_ELEMENT(&bots_paths.structure.collision_bsp, 0, struct collision_bsp);
	for (surface_index = 0; surface_index < bsp->surfaces.count; surface_index++)
	{
		real_point3d corners[BOTS_SURFACE_CORNERS], middle;
		real reach;
		long corner_count;

		if (bots_paths_walkable_surface(bsp, surface_index, corners, &corner_count, &middle, &reach) &&
			bots_distance3d(&middle, &object->object.bounding_sphere_center) <
				object->object.bounding_sphere_radius + reach + 0.5f &&
			bots_surface_blocked(object_index, corners, corner_count, &middle))
		{
			bots_paths.walkable[surface_index] &= (byte)~FLAG(BOTS_PATHFINDING_WALKABLE_BIT);
			closed++;
		}
	}
	bots_paths.closed_count += closed;
	platform_log("bots: %s %s at (%.1f %.1f %.1f): %ld walkable surfaces closed", why,
		tag_get_name(object->definition_index), object->object.bounding_sphere_center.x,
		object->object.bounding_sphere_center.y, object->object.bounding_sphere_center.z, closed);
}

/* the walkable surfaces with no room above them closed, a few a tick from
the game's start: a floor that goes on under a wall (Longest's bases: the
floor's strips under the walls joined the rooms either side, and a team's
bots walked into the wall a whole game). A surface is closed whose middle,
or most of the points halfway from it to its corners, have the level less
than half a biped's height above them */
#define BOTS_PATHS_SCAN_A_TICK 32
static void bots_paths_scan(void)
{
	struct collision_bsp const *bsp;
	long scanned;

	if (bots_paths.scan_done || !bots_paths_copy())
		return;
	bots_paths.scan_ticks++;
	bsp = TAG_BLOCK_GET_ELEMENT(&bots_paths.structure.collision_bsp, 0, struct collision_bsp);
	for (scanned = 0; scanned < BOTS_PATHS_SCAN_A_TICK; scanned++)
	{
		long surface_index = bots_paths.scan_index++;
		real_point3d corners[BOTS_SURFACE_CORNERS], middle;
		real reach;
		long corner_count, sample, covered = 0;

		if (surface_index >= bsp->surfaces.count || surface_index >= bots_paths.structure.pathfinding_surfaces.count)
		{
			bots_paths.scan_done = TRUE;
			platform_log("bots: %ld walkable surfaces with no room above closed (%ld ticks, %d surfaces a tick)",
				bots_paths.roofed_count, bots_paths.scan_ticks, BOTS_PATHS_SCAN_A_TICK);
			return;
		}
		if (!bots_paths_walkable_surface(bsp, surface_index, corners, &corner_count, &middle, &reach))
			continue;
		/* (the middle first: covered, the surface is; else most of the
		points between it and the corners) */
		for (sample = -1; sample < corner_count; sample++)
		{
			unsigned long flags = FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit) |
				FLAG(_collision_test_back_facing_surfaces_bit);
			real_vector3d up = { 0.0f, 0.0f, 0.35f };
			struct collision_result collision;
			real_point3d point = middle;

			if (sample >= 0)
			{
				point.x = 0.5f * (middle.x + corners[sample].x);
				point.y = 0.5f * (middle.y + corners[sample].y);
				point.z = 0.5f * (middle.z + corners[sample].z);
			}
			point.z += 0.05f;
			if (collision_test_vector(flags, &point, &up, NONE, &collision))
			{
				if (sample < 0)
				{
					covered = corner_count;
					break;
				}
				covered++;
			}
		}
		if (covered * 2 > corner_count)
		{
			bots_paths.walkable[surface_index] &= (byte)~FLAG(BOTS_PATHFINDING_WALKABLE_BIT);
			bots_paths.roofed_count++;
		}
	}
}

static boolean bots_paths_learned(long object_index)
{
	long index;

	for (index = 0; index < bots_paths.learned_count; index++)
	{
		if (bots_paths.learned[index] == object_index)
			return TRUE;
	}
	return FALSE;
}

/* whether the point is on or in the object (by its bounding sphere) */
static boolean bots_object_holds(struct object_datum *object, real_point3d const *point)
{
	return bots_distance2d(&object->object.bounding_sphere_center, point) < object->object.bounding_sphere_radius + 0.4f &&
		fabs(object->object.bounding_sphere_center.z - point->z) < object->object.bounding_sphere_radius + 1.0f;
}

/* the scenery or machine (with a collision model) the point is on or in,
the nearest; NONE for none */
static long bots_object_at(real_point3d const *point)
{
	struct object_iterator iterator;
	long best_index = NONE;
	real best_distance = REAL_MAX;

	object_iterator_new(&iterator, _object_mask_scenery | _object_mask_machine, 0);
	while (object_iterator_next(&iterator))
	{
		struct object_datum *object = object_get(iterator.index);
		real distance;

		if (object_definition_get(object->definition_index)->object.collision_model.index == NONE ||
			!bots_object_holds(object, point))
		{
			continue;
		}
		distance = bots_distance3d(&object->object.bounding_sphere_center, point);
		if (distance < best_distance)
		{
			best_distance = distance;
			best_index = iterator.index;
		}
	}
	return best_index;
}

/* whether one of the map's game places (a flag's stand, a teleporter, a
hill's corner, a ball's spawn) is on or in the object: never in the way */
static boolean bots_object_marks_place(struct object_datum *object)
{
	struct scenario *scenario = global_scenario_get();
	long index;

	for (index = 0; scenario && index < scenario->netgame_flags.count; index++)
	{
		struct scenario_netgame_flag *flag =
			TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, index, struct scenario_netgame_flag);

		if (bots_object_holds(object, &flag->position))
			return TRUE;
	}
	return FALSE;
}

/* whether the point stands on the object (the level or another object
under it first) */
static boolean bots_object_supports(long object_index, real_point3d const *point)
{
	struct collision_result collision;
	real_point3d start = *point;
	real_vector3d down = { 0.0f, 0.0f, -1.2f };

	start.z += 0.4f;
	return collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit) |
		FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit) |
		FLAG(_collision_test_objects_machines_bit), &start, &down, NONE, &collision) &&
		collision.type == _collision_result_object && collision.object_index == object_index;
}

/* a path's obstacle avoidance found no way past where the bot is: the
scenery and machines about it (not the bipeds and vehicles, which move) are
in the way, their surfaces closed to the searches from now on (Sidewinder's
blast door between the bases is scenery: the search, over the structure
alone, goes through it, the shortest way, and the avoidance then finds no
way past; more than half of the searches across that map failed so). A
rock or a tree the avoidance goes around is never in the way. How many
were learnt */
static long bots_paths_learn(real_point3d const *position, real_point3d const *goal)
{
	struct object_iterator iterator;
	long learned = 0;

	object_iterator_new(&iterator, _object_mask_scenery | _object_mask_machine, 0);
	while (object_iterator_next(&iterator))
	{
		struct object_datum *object = object_get(iterator.index);
		struct object_definition *definition = object_definition_get(object->definition_index);

		/* (not what the bot or its goal stands on or in: a flag's stand, a
		teleporter's base; what the bot stands beside, within its bounding
		sphere but on the level, is learnt: a bot pressed against
		Sidewinder's blast door before it was learnt never learnt it) */
		if (definition->object.collision_model.index == NONE ||
			bots_distance2d(&object->object.bounding_sphere_center, position) > object->object.bounding_sphere_radius + 4.5f ||
			fabs(object->object.bounding_sphere_center.z - position->z) > object->object.bounding_sphere_radius + 2.0f ||
			(bots_object_holds(object, position) && bots_object_supports(iterator.index, position)) ||
			bots_object_holds(object, goal) ||
			bots_paths_learned(iterator.index) || bots_object_marks_place(object))
		{
			continue;
		}
		bots_paths_close_object(iterator.index, "in the way:");
		learned++;
	}
	return learned;
}

/* the way off a closed surface: away from the nearest of what closed it */
static boolean bots_paths_escape_direction(real_point3d const *position, real *direction)
{
	long index, nearest = NONE;
	real nearest_distance = REAL_MAX;

	for (index = 0; index < bots_paths.learned_count; index++)
	{
		struct object_datum *object = object_try_and_get(bots_paths.learned[index]);
		real distance;

		if (!object)
			continue;
		distance = bots_distance2d(&object->object.bounding_sphere_center, position);
		if (distance < nearest_distance && distance < object->object.bounding_sphere_radius + 4.0f)
		{
			nearest_distance = distance;
			nearest = index;
		}
	}
	if (nearest == NONE)
		return FALSE;
	{
		struct object_datum *object = object_get(bots_paths.learned[nearest]);

		*direction = (real)atan2(position->y - object->object.bounding_sphere_center.y,
			position->x - object->object.bounding_sphere_center.x);
	}
	return TRUE;
}

static void bots_nav_prepare(void);
static void bots_nav_release(void);
static void bots_nav_poll(void);
static void bots_items_prepare(void);

/* the bots' searches' walkable surfaces, the structure's to begin with,
less those of a machine its definition calls a pathfinding obstacle (not an
elevator, nor a door open now that is none open) */
static void bots_prepare_paths(void)
{
	struct object_iterator iterator;

	bots_paths_release();
	object_iterator_new(&iterator, _object_mask_machine, 0);
	while (object_iterator_next(&iterator))
	{
		struct object_datum *object = object_get(iterator.index);
		struct machine_definition *definition = machine_definition_get(object->definition_index);

		if (!TEST_FLAG(definition->machine.flags, _machine_is_pathfinding_obstacle_bit) ||
			TEST_FLAG(definition->machine.flags, _machine_is_elevator_bit) ||
			(device_get_position(iterator.index) > 0.5f &&
				TEST_FLAG(definition->machine.flags, _machine_is_not_pathfinding_obstacle_when_open_bit)))
		{
			continue;
		}
		bots_paths_close_object(iterator.index, "a machine in the way:");
	}
}

/* the places a bot roams to: the item spawns and the player starting
locations (once the level's BSP is in) */
static void bots_prepare_map(void)
{
	struct scenario *scenario = global_scenario_get();
	long index;

	bots_globals.map_ready = TRUE;
	bots_globals.roam_point_count = 0;
	bots_prepare_paths();
	bots_nav_prepare();
	bots_globals.objectives_enabled = config_integer("bots.objectives") != 0;
	bots_globals.objective_tick = NONE;
	if (bots_globals.objectives_enabled)
		bots_items_prepare();
	if (!scenario)
		return;
	for (index = 0; index < scenario->netgame_equipment.count; index++)
	{
		struct scenario_netgame_equipment *equipment =
			TAG_BLOCK_GET_ELEMENT(&scenario->netgame_equipment, index, struct scenario_netgame_equipment);

		bots_add_roam_point(&equipment->position);
	}
	for (index = 0; index < scenario->players.count; index += 2)
	{
		struct player_starting_location *location =
			TAG_BLOCK_GET_ELEMENT(&scenario->players, index, struct player_starting_location);

		bots_add_roam_point(&location->position);
	}
	{
		real_point3d low = { REAL_MAX, REAL_MAX, REAL_MAX }, high = { -REAL_MAX, -REAL_MAX, -REAL_MAX };

		for (index = 0; index < bots_globals.roam_point_count; index++)
		{
			low.x = MIN(low.x, bots_globals.roam_points[index].x);
			low.y = MIN(low.y, bots_globals.roam_points[index].y);
			high.x = MAX(high.x, bots_globals.roam_points[index].x);
			high.y = MAX(high.y, bots_globals.roam_points[index].y);
		}
		platform_log("bots: %ld places to roam on this map, over %.0f units", bots_globals.roam_point_count,
			bots_globals.roam_point_count > 0 ? (double)bots_distance2d(&low, &high) : 0.0);
	}
}

static void bots_report_sim(char const *why);

/* the budget the settings ask for: the credit mode (bots.budget_credit, the same every run), else the clock mode
(bots.budget_us), else none: nothing is limited and nothing shed (bots_budget.h) */
static void bots_prepare_budget(void)
{
	long credit = config_integer("bots.budget_credit");
	long microseconds = config_integer("bots.budget_us");

	if (credit > 0)
		bots_budget_initialize(&bots_globals.budget, _bots_budget_credit, credit, bots_globals.maximum_bots, BOTS_SEARCHES_A_TICK);
	else if (microseconds > 0)
		bots_budget_initialize(&bots_globals.budget, _bots_budget_clock, microseconds, bots_globals.maximum_bots, BOTS_SEARCHES_A_TICK);
	else
		bots_budget_initialize(&bots_globals.budget, _bots_budget_off, 0, bots_globals.maximum_bots, BOTS_SEARCHES_A_TICK);
}

void bots_initialize_for_new_map(void)
{
	long index;

	bots_nav_release();
	csmemset(bots_globals.bots, 0, sizeof(bots_globals.bots));
	for (index = 0; index < BOTS_MAXIMUM; index++)
		bots_globals.bots[index].player_index = NONE;
	bots_globals.map_ready = FALSE;
	bots_globals.roam_point_count = 0;
	bots_globals.ticks = 0;
	bots_globals.searches = 0;
	bots_globals.search_failures = 0;
	bots_globals.search_partials = 0;
	bots_globals.search_overflows = 0;
	bots_globals.search_unavoided = 0;
	bots_globals.search_unsmoothed = 0;
	bots_globals.stuck_events = 0;
	bots_globals.stuck_giveups = 0;
	csmemset(bots_globals.combat_counts, 0, sizeof(bots_globals.combat_counts));
	bots_think_reset(&bots_globals.tick_think);
	bots_globals.rays = 0;
	bots_globals.active_bot = NULL;
	bots_globals.sim_reported = FALSE;
	bots_globals.map_name[0] = 0;
	bots_globals.first_tick_microseconds = 0;
	bots_globals.now = 0;
	bots_globals.world_time = NONE;
	bots_globals.stale_ticks = 0;
	bots_globals.maximum_bots = (short)bots_maximum();
	bots_globals.sim_ticks = config_integer("bots.sim_ticks");
	csmemset(bots_globals.action_valid, 0, sizeof(bots_globals.action_valid));
	/* (the graph's, the objectives' and the tour's counts are the new map's) */
	bots_globals.nav_searches = bots_globals.nav_search_failures = bots_globals.nav_search_partials = bots_globals.nav_search_nodes = 0;
	csmemset(bots_globals.nav_links_done, 0, sizeof(bots_globals.nav_links_done));
	csmemset(bots_globals.nav_links_failed, 0, sizeof(bots_globals.nav_links_failed));
	bots_globals.nav_links_marked_bad = bots_globals.nav_off_graph = bots_globals.nav_fallbacks = bots_globals.nav_shortcuts = bots_globals.nav_logged = 0;
	bots_globals.nav_blocked = bots_globals.nav_blind = bots_globals.nav_after_fight = bots_globals.fight_flips = bots_globals.nav_learned_bad = 0;
	csmemset(bots_globals.shared_bad_until, 0, sizeof(bots_globals.shared_bad_until));
	bots_globals.nav_reported = FALSE;
	csmemset(bots_globals.objective_counts, 0, sizeof(bots_globals.objective_counts));
	bots_globals.objective_stale = 0;
	bots_globals.timed_item_count = 0;
	bots_globals.timed_item_trips = 0;
	bots_globals.objective_tick = NONE;
	csmemset(bots_globals.tour_reached, 0, sizeof(bots_globals.tour_reached));
	csmemset(bots_globals.tour_given_up, 0, sizeof(bots_globals.tour_given_up));
	csmemset(bots_globals.tour_ticks, 0, sizeof(bots_globals.tour_ticks));
	bots_prepare_budget();
	/* (the first line a minute into the game) */
	bots_globals.last_report_tick = 0;
	/* (the weapon definitions are the new map's) */
	bots_weapon_kinds.count = 0;
}

void bots_dispose_from_old_map(void)
{
	bots_report_sim("the map's end");
	bots_nav_release();
	bots_paths_release();
	bots_globals.map_ready = FALSE;
}

/* ---------- path searches */

/* the search's way to its destination (or as near as it came): each
surface's entry point (path_state_build_path's raw steps, at most 64), the
endpoint last. TRUE with steps */
static boolean bots_path_raw_steps(struct path_state *state, struct path_step *raw_steps, short *raw_step_count,
	boolean *steps_finish_path, real_point3d *endpoint)
{
	short node_index, child_index = NONE;

	*steps_finish_path = TRUE;
	*raw_step_count = 0;
	if (!state->destination_valid || state->node_count <= 0)
		return FALSE;
	node_index = path_node_from_hash_table(state, state->destination.surface_index);
	*endpoint = state->destination.point;
	if (node_index == NONE)
	{
		node_index = state->closest_node_index;
		*endpoint = state->closest_point;
	}
	if (node_index < 0 || node_index >= state->node_count)
		return FALSE;
	*raw_step_count = (short)MIN(state->node_list[node_index].depth + 1, 64);
	while (node_index != NONE)
	{
		struct path_node *node;

		if (node_index < 0 || node_index >= state->node_count)
			return FALSE;
		node = &state->node_list[node_index];
		if (node->depth >= 64)
			*steps_finish_path = FALSE;
		else if (node->depth >= 0 && node->depth < *raw_step_count)
		{
			raw_steps[node->depth].surface_index = node->surface_index;
			raw_steps[node->depth].point = child_index == NONE ? *endpoint : state->node_list[child_index].entry_point;
		}
		else
			return FALSE;
		child_index = node_index;
		node_index = node->parent_node_index;
	}
	return *raw_step_count > 0;
}

/* the path as path_state_build_path makes it, the steps smoothed, but not
taken round the objects about (the obstacle avoidance found no way round:
the other bots crowding a narrow way out of a base, Longest's, all waiting
on each other); the bots push past each other, and one that stops moving
sidesteps and jumps. TRUE with a path */
static boolean bots_build_path_unavoided(struct path_state *state, struct path_result *path)
{
	struct path_step raw_steps[64];
	struct path_step smoothed_steps[MAXIMUM_SMOOTHED_PATH_STEPS];
	short raw_step_count, smoothed_step_count = 0;
	boolean steps_finish_path;
	real_point3d endpoint;

	csmemset(path, 0, sizeof(*path));
	if (!bots_path_raw_steps(state, raw_steps, &raw_step_count, &steps_finish_path, &endpoint))
		return FALSE;
	path_smooth(state, raw_step_count, raw_steps, &smoothed_step_count, smoothed_steps, &steps_finish_path);
	if (smoothed_step_count <= 0)
		return FALSE;
	smoothed_step_count = MIN(smoothed_step_count, MAXIMUM_SMOOTHED_PATH_STEPS);
	path->valid = TRUE;
	path->endpoint.point = endpoint;
	path->endpoint.surface_index = smoothed_steps[smoothed_step_count - 1].surface_index;
	path->steps_finish_path = steps_finish_path;
	path->step_count = (char)smoothed_step_count;
	path->step_index = 0;
	csmemcpy(path->steps, smoothed_steps, smoothed_step_count * sizeof(struct path_step));
	return TRUE;
}

/* a path that goes nowhere: short of its end, every step where the bot
stands. The smoothing (path_smooth) gave one at the foot of a steep bank:
all four of its steps the same point a few centimetres from the bot, which
it took as reached at once and searched again, the same, while it slid
about (Sidewinder's banks up to the side ledges) */
static boolean bots_path_stalls(struct path_result const *path, real_point3d const *start)
{
	return path->valid && !path->steps_finish_path && path->step_count > 0 &&
		bots_distance2d(&path->steps[path->step_count - 1].point, start) <= BOTS_STEP_REACHED;
}

/* a stalling path's steps instead the search's own, unsmoothed: the next
surfaces' entry points beyond where the bot stands (up the bank), four at
most. TRUE when made */
static boolean bots_path_unsmoothed(struct path_state *state, real_point3d const *start, struct path_result *path)
{
	struct path_step raw_steps[64];
	short raw_step_count, step, count = 0;
	boolean steps_finish_path;
	real_point3d endpoint;
	struct path_result result;

	if (!bots_path_raw_steps(state, raw_steps, &raw_step_count, &steps_finish_path, &endpoint))
		return FALSE;
	csmemset(&result, 0, sizeof(result));
	for (step = 0; step < raw_step_count && count < (short)NUMBEROF(result.steps); step++)
	{
		if (!count && bots_distance2d(&raw_steps[step].point, start) <= BOTS_STEP_REACHED)
			continue;
		result.steps[count++] = raw_steps[step];
	}
	if (!count)
		return FALSE;
	result.valid = TRUE;
	result.steps_finish_path = steps_finish_path && step >= raw_step_count;
	result.step_count = (char)count;
	result.endpoint.point = endpoint;
	result.endpoint.surface_index = result.steps[count - 1].surface_index;
	*path = result;
	return TRUE;
}

/* a search's answer made a path: the way to the goal (or as near as the
search came); when the path's obstacle avoidance finds no way, again with
what the goal or the bot stands on (a flag's stand, a teleporter's base) no
obstacle, and failing that, the scenery and machines about the bot are
learnt to be in the way (the next searches go round them) and this once the
path is taken without the avoidance. TRUE with a path */
static boolean bots_search_build(struct path_state *state, real_point3d const *start, real_point3d const *goal,
	struct path_result *path)
{
	long object_index;

	if (state->node_count <= 0)
		return FALSE;
	if (path_state_build_path(state, path) && path->step_count > 0)
		return TRUE;
	object_index = bots_object_at(goal);
	if (object_index == NONE)
		object_index = bots_object_at(start);
	if (object_index != NONE)
	{
		state->input.ignore_target_object_index = object_index;
		csmemset(path, 0, sizeof(*path));
		if (path_state_build_path(state, path) && path->step_count > 0)
			return TRUE;
	}
	/* (something new learnt: a new search, round it) */
	if (bots_paths_learn(start, goal) > 0)
		return FALSE;
	if (!bots_build_path_unavoided(state, path))
		return FALSE;
	bots_globals.search_unavoided++;
	return TRUE;
}

/* a path from where the bot's unit is to its goal, searched now (a few a
tick: a bot past them asks again next tick), into the bot's path */
static void bots_search(struct bots_bot *bot, long unit_index)
{
	struct path_state *state;
	struct path_input input;
	struct path_result path;
	real_point3d start;
	long start_surface_index;
	boolean reached;

	if (!bots_budget_search_allowed(&bots_globals.budget) || bot->goal_surface_index == NONE)
		return;
	if (!bots_globals.search)
	{
		bots_globals.search = (struct path_state *)malloc(sizeof(struct path_state));
		if (!bots_globals.search)
			return;
	}
	state = bots_globals.search;
	start_surface_index = bots_unit_surface(unit_index, &start);
	if (start_surface_index == NONE)
		return;
	/* (on a surface closed to the searches, whose way out may be closed
	too: away from what closed it first, a second) */
	if (bots_paths.walkable && start_surface_index < bots_paths.structure.pathfinding_surfaces.count &&
		!TEST_FLAG(bots_paths.walkable[start_surface_index], BOTS_PATHFINDING_WALKABLE_BIT) &&
		bots_paths_escape_direction(&start, &bot->escape_direction))
	{
		bot->escape_until_tick = bots_now() + TICKS_PER_SECOND;
		bot->path.valid = FALSE;
		bot->path_tick = bots_now();
		return;
	}
	bots_budget_note_search(&bots_globals.budget);
	bots_globals.searches++;
	bot->searches++;
	path_input_new(&input, BOTS_PATH_RADIUS, FALSE, unit_index);
	path_input_set_start(&input, &start, start_surface_index);
	path_state_new(&input, state, NULL);
	/* (the walkable surfaces less those closed by what is in the way) */
	if (bots_paths_copy() && state->structure == bots_paths.source)
		state->structure = &bots_paths.structure;
	/* (a path to as near the goal as the search reaches: a long way across a
	large level is more surfaces than a search holds, and the bot searches
	again from there) */
	path_state_destination(state, &bot->goal_point, bot->goal_surface_index, 1000.0f);
	path_state_find(state);
	bots_budget_spend(&bots_globals.budget, state->node_count * BOTS_BUDGET_SEARCH_NODE);
	reached = state->node_count > 0 && path_node_from_hash_table(state, bot->goal_surface_index) != NONE;
	if (state->node_count >= PATH_NODE_LIST_SIZE)
		bots_globals.search_overflows++;
	if (!reached)
	{
		bots_globals.search_partials++;
		bot->counts.search_partials++;
	}
	csmemset(&path, 0, sizeof(path));
	if (bots_search_build(state, &start, &bot->goal_point, &path))
	{
		if (bots_path_stalls(&path, &start) && bots_path_unsmoothed(state, &start, &path))
			bots_globals.search_unsmoothed++;
		bot->path = path;
		bot->path.step_index = 0;
		bot->path_failures = 0;
		bot->path_short = !reached;
		if (!bot->path_short)
			bot->climb_until_tick = 0;
	}
	else
	{
		bot->path.valid = FALSE;
		bot->path_failures++;
		bots_globals.search_failures++;
		bot->counts.search_failures++;
	}
	/* (an item it finds no way to: another, and not this one a while) */
	if (bot->goal_kind == _bots_goal_item && !reached)
	{
		bots_item_set_unreachable(bot, bot->item_index);
		bot->goal_kind = _bots_goal_none;
		bot->path.valid = FALSE;
	}
	bot->path_tick = bots_now();
}

/* ---------- navigation (the graph of bots_nav.h)

A map with a navigation record (navs.pack, read on a thread of its own at the map's start) is walked by it: the bot's path is a list of the graph's
links, and the points along them are an area's anchor, a link's exit and its entry. A walk goes from point to point (and skips a point when the one
after it is straight ahead and clear: a ray at the knees and the body, and the floor under the way); a jump or a drop is run: the bot goes to the run-up's
start, runs along the link's direction to the edge, jumps (a jump) and flies to the entry; a teleporter is stood on. A link that does not work (the
bot is not there in time, or is stuck on it) is bad for that bot for thirty seconds, and the next path goes another way. A goal the graph does not
reach (off it, or in a piece it cannot get to) is walked as it always was. */

#define BOTS_NAV_BAD_TICKS (30 * TICKS_PER_SECOND)
/* how near a bot is when it has reached a point of a walk, and the last point (the goal) */
#define BOTS_NAV_POINT_REACHED 0.55f
#define BOTS_NAV_GOAL_REACHED 0.45f
/* the ticks a bot may go without reaching a point before the link it is on is called bad */
#define BOTS_NAV_PROGRESS_TICKS (10 * TICKS_PER_SECOND)
/* the ticks a bot may barely move while the graph leads it before it is called blocked, how little is barely (units a tick: a run is 0.075), and the ticks the
engine's surfaces lead it after that (or when no area is in its sight) before the graph is asked again */
#define BOTS_NAV_BLOCKED_TICKS 14
#define BOTS_NAV_BARELY_MOVED 0.02f
#define BOTS_NAV_BLIND_TICKS 24
#define BOTS_NAV_SHARED_BAD_TICKS (20 * TICKS_PER_SECOND)
#define BOTS_FIGHT_BLOCKED_TICKS 6
enum
{
	BOTS_NAV_TWEAK_STRAFE_FLIP = 1,
	BOTS_NAV_TWEAK_SHARED_BAD = 2,
	BOTS_NAV_TWEAK_LEARN_FLIGHTS = 4,
};

enum
{
	_bots_nav_follow_fallback = 0,
	_bots_nav_follow_moving,
	_bots_nav_follow_arrived,
};

/* what a link's executor says */
enum
{
	_bots_nav_exec_moving = 0,
	_bots_nav_exec_done,
	_bots_nav_exec_failed,
};

/* the states of a bot on a link that is not a walk */
enum
{
	_bots_nav_state_none = 0,
	/* run-up: going to where the run begins */
	_bots_nav_state_run_up,
	/* running along the link's direction to the edge */
	_bots_nav_state_run,
	/* in the air, or waiting on a teleporter */
	_bots_nav_state_flight,
};

static boolean bots_nav_usable(void)
{
	return bots_globals.nav && bots_globals.nav_search_ready;
}

/* the map's record, once the thread has it: taken on the first tick it is ready (a game of thousands of ticks starts before it, as ever) */
static void bots_nav_poll(void)
{
	struct bots_nav *nav;

	if (bots_globals.nav || bots_nav_load_state() != _bots_nav_load_ready)
		return;
	nav = bots_nav_load_take();
	if (!nav)
		return;
	if (!bots_nav_search_new(&bots_globals.nav_search, nav))
	{
		bots_nav_dispose(nav);
		platform_log("bots: nav: no memory for the graph's searches: the bots walk by the engine's surfaces");
		return;
	}
	bots_globals.nav = nav;
	bots_globals.nav_search_ready = TRUE;
	platform_log("bots: nav: in use from tick %ld: %ld areas, %ld links, %ld scripts, %ld places; a run is %.2f units a second, a jump rises %.2f, a drop of %.2f is safe",
		bots_now(), nav->area_count, nav->link_count, nav->script_count, nav->place_count, (double)nav->run_speed, (double)nav->jump_height,
		(double)nav->drop_height);
}

/* the map's start: the setting, and the read of the record begun (a thread: the game thread does not wait for the card) */
static void bots_nav_prepare(void)
{
	struct structure_bsp *structure = global_structure_bsp_get();
	unsigned long surfaces = 0, walkable = 0;
	long index;

	bots_globals.nav_enabled = config_integer("bots.nav") != 0;
	bots_globals.nav_sync = config_integer("bots.nav_sync") != 0;
	bots_globals.nav_tweaks = config_integer("bots.nav_tweaks");
	bots_globals.tour = config_integer("debug.bots_tour") != 0;
	bots_globals.stuck_log_on = config_integer("debug.bots_stuck_log");
	if (!bots_globals.nav_enabled || !structure || structure->collision_bsp.count <= 0)
		return;
	surfaces = (unsigned long)TAG_BLOCK_GET_ELEMENT(&structure->collision_bsp, 0, struct collision_bsp)->surfaces.count;
	if (structure->pathfinding_surfaces.address)
	{
		for (index = 0; index < structure->pathfinding_surfaces.count; index++)
			if (TEST_FLAG(((byte const *)structure->pathfinding_surfaces.address)[index], BOTS_PATHFINDING_WALKABLE_BIT))
				walkable++;
	}
	bots_nav_load_begin(main_get_multiplayer_map_name(), surfaces, walkable, bots_globals.nav_sync);
	if (bots_globals.nav_sync)
		bots_nav_poll();
}

static void bots_nav_release(void)
{
	if (bots_globals.nav)
	{
		bots_nav_search_dispose(&bots_globals.nav_search);
		bots_nav_dispose(bots_globals.nav);
	}
	bots_globals.nav = NULL;
	bots_globals.nav_search_ready = FALSE;
	bots_nav_load_reset();
}

/* the link a bot found bad is not taken for a while */
static void bots_nav_mark_bad(struct bots_bot *bot, long link_index, boolean failed)
{
	short slot = bot->nav_bad_next;

	if (link_index < 0)
		return;
	bot->nav_bad_links[slot] = (unsigned short)link_index;
	bot->nav_bad_until[slot] = bots_now() + BOTS_NAV_BAD_TICKS;
	bot->nav_bad_next = (short)((slot + 1) % BOTS_NAV_BAD_LINKS);
	bots_globals.nav_links_marked_bad++;
	if (bots_globals.nav_tweaks & BOTS_NAV_TWEAK_SHARED_BAD)
	{
		short shared = bots_globals.shared_bad_next;

		bots_globals.shared_bad_links[shared] = (unsigned short)link_index;
		bots_globals.shared_bad_until[shared] = bots_now() + BOTS_NAV_SHARED_BAD_TICKS;
		bots_globals.shared_bad_next = (short)((shared + 1) % BOTS_NAV_SHARED_BAD);
	}
	if (failed && bots_globals.nav && link_index < bots_globals.nav->link_count)
		bots_globals.nav_links_failed[bots_globals.nav->links[link_index].type]++;
	/* a jump or a drop that failed for a bot fails for all: the geometry is the same (a rim, a lip, a run-up too short); no search takes it again this map */
	if (failed && (bots_globals.nav_tweaks & BOTS_NAV_TWEAK_LEARN_FLIGHTS) && bots_globals.nav && link_index < bots_globals.nav->link_count &&
		bots_globals.nav_search.link_blocked && bots_globals.nav_search.link_blocked[link_index] < 2 &&
		(bots_globals.nav->links[link_index].type == _bots_nav_link_jump || bots_globals.nav->links[link_index].type == _bots_nav_link_drop))
	{
		bots_globals.nav_search.link_blocked[link_index] = 2;
		bots_globals.nav_learned_bad++;
		if (bots_globals.stuck_log_on)
		{
			struct bots_nav_link const *l = &bots_globals.nav->links[link_index];

			platform_log("bots: nav: link %ld (%s) failed for a bot: from (%.1f %.1f %.1f) to (%.1f %.1f %.1f), blocked for every bot this map", link_index,
				l->type == _bots_nav_link_jump ? "jump" : "drop", l->exit[0], l->exit[1], l->exit[2], l->entry[0], l->entry[1], l->entry[2]);
		}
	}
	bot->nav_path.valid = FALSE;
	bot->nav_state = _bots_nav_state_none;
	bot->nav_path_tick = bots_now() - TICKS_PER_SECOND;
}

static long bots_nav_collect_bad(struct bots_bot const *bot, unsigned short *out)
{
	long count = 0, slot, now = bots_now();

	for (slot = 0; slot < BOTS_NAV_BAD_LINKS; slot++)
		if (bot->nav_bad_until[slot] > now)
			out[count++] = bot->nav_bad_links[slot];
	if (bots_globals.nav_tweaks & BOTS_NAV_TWEAK_SHARED_BAD)
		for (slot = 0; slot < BOTS_NAV_SHARED_BAD; slot++)
			if (bots_globals.shared_bad_until[slot] > now)
				out[count++] = bots_globals.shared_bad_links[slot];
	return count;
}

/* the area of a point on the floor: the one it is in, else the nearest anchor within `reach` */
static long bots_nav_area_of(real_point3d const *point, float reach)
{
	float p[3] = { point->x, point->y, point->z };
	long area = bots_nav_find_area(bots_globals.nav, p);

	if (area < 0)
	{
		area = bots_nav_nearest_area(bots_globals.nav, p);
		if (area >= 0)
		{
			struct bots_nav_area const *a = &bots_globals.nav->areas[area];
			float dx = p[0] - a->x, dy = p[1] - a->y, dz = p[2] - a->z;

			if ((float)sqrt(dx * dx + dy * dy + dz * dz) > reach)
				area = -1;
		}
	}
	return area;
}

static boolean bots_nav_direct_clear(real_point3d const *from, float const *to, real shrink, real spacing);

/* the area a bot at a point starts a path in: of the areas that reach it, the best whose anchor it can walk straight to (the floor every 0.4 units, the
knees and the body swept a little narrower than the biped, so that a bot against a wall is not blind); -1 when it can walk straight to none of them. A bot
that is off the graph's floor (jammed in a corner, across a crack, on the far side of a rail) is not told to walk through it to an anchor */
static long bots_nav_seen_area(real_point3d const *position)
{
	struct bots_nav const *nav = bots_globals.nav;
	long candidates[8], count, index;
	float p[3] = { position->x, position->y, position->z };

	count = bots_nav_find_areas(nav, p, candidates, 8);
	for (index = 0; index < count; index++)
	{
		struct bots_nav_area const *a = &nav->areas[candidates[index]];
		float anchor[3] = { a->x, a->y, a->z };
		float dx = anchor[0] - p[0], dy = anchor[1] - p[1];

		if (dx * dx + dy * dy < 0.36f && fabs(anchor[2] - p[2]) < 0.6f)
			return candidates[index];
		if (bots_nav_direct_clear(position, anchor, 0.05f, 0.4f))
			return candidates[index];
	}
	return -1;
}

/* the area of a goal: of the areas that reach it, the best whose anchor sees it (the walk from the last anchor to the goal is a straight line), else the best that
reaches it, else the nearest within `reach` */
static long bots_nav_goal_area(real_point3d const *goal, float reach)
{
	struct bots_nav const *nav = bots_globals.nav;
	long candidates[4], count, index;
	float p[3] = { goal->x, goal->y, goal->z };

	count = bots_nav_find_areas(nav, p, candidates, 4);
	for (index = 0; index < count; index++)
	{
		struct bots_nav_area const *a = &nav->areas[candidates[index]];
		float anchor[3] = { a->x, a->y, a->z };
		float dx = anchor[0] - p[0], dy = anchor[1] - p[1];

		if (dx * dx + dy * dy < 0.36f || bots_nav_direct_clear(goal, anchor, 0.05f, 0.5f))
			return candidates[index];
	}
	return count > 0 ? candidates[0] : bots_nav_area_of(goal, reach);
}

/* a path to the bot's goal from where it stands (a few a tick: a bot past them walks straight at the goal and asks again): TRUE when it has one */
static boolean bots_nav_make_path(struct bots_bot *bot, real_point3d const *position)
{
	struct bots_nav *nav = bots_globals.nav;
	unsigned short bad[BOTS_NAV_BAD_LINKS + BOTS_NAV_SHARED_BAD];
	long start_area, bad_count;
	float start[3] = { position->x, position->y, position->z }, goal[3] = { bot->goal_point.x, bot->goal_point.y, bot->goal_point.z };

	if (!bots_budget_search_allowed(&bots_globals.budget))
		return FALSE;
	start_area = bots_nav_seen_area(position);
	if (start_area < 0)
	{
		/* (on the graph's map but with no anchor in sight: the engine's surfaces lead it a moment, off it altogether: the same, counted apart) */
		if (bots_nav_area_of(position, 3.0f) >= 0)
		{
			bots_globals.nav_blind++;
			bot->nav_blind_until = bots_now() + BOTS_NAV_BLIND_TICKS;
		}
		else
			bots_globals.nav_off_graph++;
		bot->nav_path.valid = FALSE;
		bot->nav_path_tick = bots_now();
		return FALSE;
	}
	bots_budget_note_search(&bots_globals.budget);
	bots_globals.nav_searches++;
	bot->searches++;
	bad_count = bots_nav_collect_bad(bot, bad);
	if (!bots_nav_search_path(nav, &bots_globals.nav_search, start_area, bot->nav_goal_area, start, goal, bad, bad_count,
		(unsigned long)(bot->random & 0xFFFF) | 1UL, &bot->nav_path))
	{
		bots_globals.nav_search_failures++;
		bot->counts.search_failures++;
		bot->nav_path.valid = FALSE;
		bot->nav_path_tick = bots_now();
		return FALSE;
	}
	bots_budget_spend(&bots_globals.budget, bots_globals.nav_search.expanded * BOTS_BUDGET_SEARCH_NODE);
	bots_globals.nav_search_nodes += bots_globals.nav_search.expanded;
	if (!bot->nav_path.complete)
	{
		bots_globals.nav_search_partials++;
		bot->counts.search_partials++;
		if (bots_globals.nav_logged < 40 && config_integer("debug.bot_trace"))
		{
			bots_globals.nav_logged++;
			platform_log("bots: nav: a path that ends short: from area %ld (%.1f %.1f %.1f) to the goal's area %ld (%.1f %.1f %.1f), it reaches area %d in %d links",
				start_area, position->x, position->y, position->z, bot->nav_goal_area, goal[0], goal[1], goal[2], (int)bot->nav_path.goal_area,
				(int)bot->nav_path.link_count);
		}
	}
	/* (a path that does not reach the goal's area is the engine surfaces' business: the graph is walked only where it goes the whole way) */
	if (!bot->nav_path.complete)
	{
		bot->nav_path.valid = FALSE;
		bot->nav_path_tick = bots_now();
		return FALSE;
	}
	bot->nav_path.step = 0;
	bot->nav_path_tick = bots_now();
	bot->nav_progress_tick = bots_now();
	bot->nav_state = _bots_nav_state_none;
	return TRUE;
}

/* whether the way straight from the bot to a point is a walk: the floor under it every few units follows the way, and a sphere at the knees and one at
the body meet nothing (the bake's own tests) */
static boolean bots_nav_direct_clear(real_point3d const *from, float const *to, real shrink, real spacing)
{
	static real const knee = 0.32f, body = 0.385f;
	unsigned long flags = FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_back_facing_surfaces_bit) |
		FLAG(_collision_test_structure_bit) | FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit) |
		FLAG(_collision_test_objects_machines_bit);
	real_point3d a, b;
	real_vector3d vector;
	struct collision_result collision;
	real dx = to[0] - from->x, dy = to[1] - from->y, distance = (real)sqrt(dx * dx + dy * dy);
	long samples = (long)(distance / spacing), sample;

	bots_globals.rays += 2;
	if (bots_globals.active_bot)
		bots_globals.active_bot->counts.rays += 2;
	bots_budget_spend(&bots_globals.budget, BOTS_BUDGET_SIGHT_TEST * (1 + samples / 4));
	if (fabs(to[2] - from->z) > 0.45f + 0.25f * distance)
		return FALSE;
	a = *from;
	a.z = MAX(from->z, to[2]) + knee;
	vector.i = dx;
	vector.j = dy;
	vector.k = 0.0f;
	if (collision_test_pill(flags, &a, &vector, 0.18f - shrink, NONE, &collision))
		return FALSE;
	a.z = from->z + body;
	vector.k = to[2] - from->z;
	if (collision_test_pill(flags, &a, &vector, 0.2f - shrink, NONE, &collision))
		return FALSE;
	/* (the floor along the way: every `spacing` units, within a step of the line between the ends) */
	for (sample = 1; sample <= samples; sample++)
	{
		real t = (real)sample / (real)(samples + 1);
		real expected = from->z + (to[2] - from->z) * t;
		real_vector3d down = { 0.0f, 0.0f, -1.6f };

		b.x = from->x + dx * t;
		b.y = from->y + dy * t;
		b.z = expected + 0.7f;
		bots_globals.rays++;
		if (!collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit), &b, &down, NONE, &collision) ||
			collision.type != _collision_result_structure || fabs(collision.point.z - expected) > (spacing < 1.0f ? 0.22f : 0.3f))
		{
			return FALSE;
		}
	}
	return TRUE;
}

static boolean bots_nav_direct_ok(real_point3d const *from, float const *to)
{
	return bots_nav_direct_clear(from, to, 0.0f, 2.5f);
}

static boolean bots_nav_grounded(long unit_index)
{
	struct biped_datum *biped = biped_try_and_get(unit_index);

	return !biped || !TEST_FLAG(biped->biped.flags, _biped_airborne_bit);
}

/* a link finished: the bot is past its entry */
static void bots_nav_link_done(struct bots_bot *bot, struct bots_nav_link const *link, long point_number)
{
	bots_globals.nav_links_done[link->type]++;
	bot->nav_path.step = (short)(point_number + 2);
	bot->nav_state = _bots_nav_state_none;
	bot->nav_progress_tick = bots_now();
}

/* a link that is not a walk, at its exit point (the path's point number `point_number`): the run-up, the run, the jump and the flight, or the
teleporter stood on. TRUE with the direction to walk; FALSE when the link is done (the loop goes on) or failed (a new path is asked for) */
static int bots_nav_execute(struct bots_bot *bot, long unit_index, real_point3d const *position, struct bots_nav_link const *link, long link_index,
	long point_number, real *direction)
{
	struct bots_nav const *nav = bots_globals.nav;
	struct bots_nav_script const *script = link->script != 0xFFFF ? &nav->scripts[link->script] : NULL;
	long now = bots_now();
	real dx, dy;

	if (link->type == _bots_nav_link_teleport)
	{
		dx = link->exit[0] - position->x;
		dy = link->exit[1] - position->y;
		if (bot->nav_state == _bots_nav_state_none)
		{
			if ((real)sqrt(dx * dx + dy * dy) > 0.4f || fabs(link->exit[2] - position->z) > 1.0f)
			{
				*direction = (real)atan2(dy, dx);
				if (now - bot->nav_progress_tick > TICKS_PER_SECOND * 12)
				{
					bots_nav_mark_bad(bot, link_index, TRUE);
					return _bots_nav_exec_failed;
				}
				return _bots_nav_exec_moving;
			}
			bot->nav_state = _bots_nav_state_flight;
			bot->nav_state_tick = now;
		}
		{
			float p[3] = { position->x, position->y, position->z };
			float ex = link->entry[0] - p[0], ey = link->entry[1] - p[1], ez = link->entry[2] - p[2];

			if ((real)sqrt(ex * ex + ey * ey + ez * ez) < 3.0f)
			{
				bots_nav_link_done(bot, link, point_number);
				return _bots_nav_exec_done;
			}
		}
		if (now - bot->nav_state_tick > TICKS_PER_SECOND * 3)
		{
			bots_nav_mark_bad(bot, link_index, TRUE);
			return _bots_nav_exec_failed;
		}
		/* (standing on it: the teleporter takes a moment) */
		*direction = (real)atan2(dy, dx);
		bot->nav_throttle = 0.15f;
		return _bots_nav_exec_moving;
	}
	/* a jump or a drop */
	{
		real yaw = script ? script->yaw : (real)atan2(link->entry[1] - link->exit[1], link->entry[0] - link->exit[0]);

		if (bot->nav_state == _bots_nav_state_none || bot->nav_state == _bots_nav_state_run_up)
		{
			float launch[3] = { script ? script->launch[0] : link->exit[0], script ? script->launch[1] : link->exit[1],
				script ? script->launch[2] : link->exit[2] };

			if (bot->nav_state == _bots_nav_state_none)
			{
				bot->nav_state = _bots_nav_state_run_up;
				bot->nav_state_tick = now;
			}
			dx = launch[0] - position->x;
			dy = launch[1] - position->y;
			if ((real)sqrt(dx * dx + dy * dy) > 0.3f)
			{
				if (now - bot->nav_state_tick > TICKS_PER_SECOND * 8)
				{
					bots_nav_mark_bad(bot, link_index, TRUE);
					return _bots_nav_exec_failed;
				}
				*direction = (real)atan2(dy, dx);
				return _bots_nav_exec_moving;
			}
			bot->nav_state = _bots_nav_state_run;
			bot->nav_state_tick = now;
		}
		if (bot->nav_state == _bots_nav_state_run)
		{
			/* (the edge: where the bot has gone as far along the direction as the exit point) */
			real along = (position->x - link->exit[0]) * (real)cos(yaw) + (position->y - link->exit[1]) * (real)sin(yaw);

			if (along >= -0.05f)
			{
				if (link->type == _bots_nav_link_jump)
					bot->nav_jump = TRUE;
				bot->nav_state = _bots_nav_state_flight;
				bot->nav_state_tick = now;
			}
			else if (now - bot->nav_state_tick > TICKS_PER_SECOND * 3)
			{
				bots_nav_mark_bad(bot, link_index, TRUE);
				return _bots_nav_exec_failed;
			}
			else
			{
				*direction = yaw;
				return _bots_nav_exec_moving;
			}
		}
		/* in the air (or walking off): on towards the entry */
		{
			long flight_ticks = script ? script->flight_ticks : 40;
			long elapsed = now - bot->nav_state_tick;
			real ex = link->entry[0] - position->x, ey = link->entry[1] - position->y;
			boolean grounded = bots_nav_grounded(unit_index);

			if (elapsed >= 3 && grounded)
			{
				if ((real)sqrt(ex * ex + ey * ey) < 1.3f && fabs(link->entry[2] - position->z) < 0.8f)
				{
					bots_nav_link_done(bot, link, point_number);
					return _bots_nav_exec_done;
				}
				/* (down somewhere else, or never off the ground: this link does not work for it) */
				if (elapsed > 8 || elapsed > flight_ticks + 20)
				{
					bots_nav_mark_bad(bot, link_index, TRUE);
					return _bots_nav_exec_failed;
				}
			}
			if (elapsed > flight_ticks * 2 + 60)
			{
				bots_nav_mark_bad(bot, link_index, TRUE);
				return _bots_nav_exec_failed;
			}
			*direction = (real)atan2(ey, ex);
			return _bots_nav_exec_moving;
		}
	}
}

/* the direction the bot walks by the graph, towards its goal; _bots_nav_follow_fallback when the graph has no use for this goal (the engine's
surfaces then), _bots_nav_follow_arrived when it has walked the whole way */
static int bots_nav_follow(struct bots_bot *bot, long unit_index, real_point3d const *position, real *direction)
{
	struct bots_nav const *nav = bots_globals.nav;
	long now = bots_now(), guard;

	/* (a new goal: its area) */
	if (bot->nav_goal_area == NONE || bots_distance3d(&bot->nav_goal, &bot->goal_point) > 1.0f)
	{
		bot->nav_goal = bot->goal_point;
		bot->nav_goal_area = bots_nav_goal_area(&bot->goal_point, 5.0f);
		bot->nav_path.valid = FALSE;
		bot->nav_state = _bots_nav_state_none;
		bot->nav_path_tick = now - TICKS_PER_SECOND;
		bot->nav_progress_tick = now;
		if (bot->nav_goal_area < 0)
			bots_globals.nav_off_graph++;
	}
	if (bot->nav_goal_area < 0)
		return _bots_nav_follow_fallback;
	/* (the engine's surfaces lead it for a moment: blocked, or no anchor in sight) */
	if (now < bot->nav_blind_until && bot->nav_state == _bots_nav_state_none)
		return _bots_nav_follow_fallback;
	/* (a fight moves a bot where its path was not: a path made after it) */
	if (bot->nav_path.valid && bot->nav_fought_tick > bot->nav_path_tick && now - bot->nav_fought_tick > 3 && bot->nav_state == _bots_nav_state_none)
	{
		bots_globals.nav_after_fight++;
		bot->nav_path.valid = FALSE;
		bot->nav_path_tick = now - TICKS_PER_SECOND;
	}
	/* (a path now and then: the goal moves, and the bot may have been pushed off it) */
	if ((!bot->nav_path.valid || now - bot->nav_path_tick > TICKS_PER_SECOND * 6) && now - bot->nav_path_tick > TICKS_PER_SECOND / 3 &&
		bot->nav_state == _bots_nav_state_none)
	{
		if (!bots_nav_make_path(bot, position) && !bot->nav_path.valid)
		{
			/* (a search that only waited for its turn: straight at the goal meanwhile; no way by the graph, or the bot off it: the engine's surfaces) */
			if (bot->nav_path_tick != now)
			{
				*direction = (real)atan2(bot->goal_point.y - position->y, bot->goal_point.x - position->x);
				return _bots_nav_follow_moving;
			}
			bots_globals.nav_fallbacks++;
			return _bots_nav_follow_fallback;
		}
	}
	if (!bot->nav_path.valid)
		return _bots_nav_follow_fallback;
	/* (no point reached in a long while: the link it is on does not work for this bot) */
	if (now - bot->nav_progress_tick > BOTS_NAV_PROGRESS_TICKS && bot->nav_state == _bots_nav_state_none)
	{
		long slot = MIN((long)bot->nav_path.step / 3, (long)bot->nav_path.link_count - 1);

		if (slot >= 0)
			bots_nav_mark_bad(bot, bot->nav_path.links[slot], TRUE);
		return _bots_nav_follow_fallback;
	}
	for (guard = 0; guard < 8; guard++)
	{
		struct bots_nav_point point;
		long number = bot->nav_path.step;
		long slot = number / 3, phase = number % 3;
		long link_index = number < 3L * bot->nav_path.link_count ? (long)bot->nav_path.links[slot] : -1;
		struct bots_nav_link const *link = link_index >= 0 ? &nav->links[link_index] : NULL;
		boolean last;
		real reach, dx, dy;
		struct bots_nav_point next;
		real_point3d next_point;

		if (!bots_nav_path_point(nav, &bot->nav_path, number, &point))
		{
			/* (the end of the path: the goal, or as near as the graph goes) */
			bot->nav_path.valid = FALSE;
			if (!bot->nav_path.complete)
			{
				bots_globals.nav_fallbacks++;
				return _bots_nav_follow_fallback;
			}
			return _bots_nav_follow_arrived;
		}
		if (link && phase == 1 && link->type != _bots_nav_link_walk && link->type != _bots_nav_link_step)
		{
			int result = bots_nav_execute(bot, unit_index, position, link, link_index, number, direction);

			if (result == _bots_nav_exec_moving)
				return _bots_nav_follow_moving;
			if (result == _bots_nav_exec_done)
				continue;
			/* (it failed: the link is bad for this bot and a new path is asked for; this tick on towards the goal) */
			*direction = (real)atan2(bot->goal_point.y - position->y, bot->goal_point.x - position->x);
			return _bots_nav_follow_moving;
		}
		last = number == bots_nav_path_point_count(&bot->nav_path) - 1;
		reach = last ? BOTS_NAV_GOAL_REACHED : BOTS_NAV_POINT_REACHED;
		dx = point.p[0] - position->x;
		dy = point.p[1] - position->y;
		if ((real)sqrt(dx * dx + dy * dy) < reach && fabs(point.p[2] - position->z) < 1.0f)
		{
			bot->nav_path.step++;
			bot->nav_progress_tick = now;
			continue;
		}
		/* (the point after this one is straight ahead and clear: on to it; a point that is the exit of a jump or a drop is walked to, not passed) */
		if (!last && (now + (long)(bot - bots_globals.bots)) % 4 == 0 && bots_nav_path_point(nav, &bot->nav_path, number + 1, &next) &&
			next.kind == _bots_nav_link_walk && !(link && phase == 2 && link->type != _bots_nav_link_walk) &&
			(next.link_index < 0 || nav->links[next.link_index].type == _bots_nav_link_walk) &&
			(next_point.x = next.p[0], next_point.y = next.p[1], next_point.z = next.p[2], bots_distance3d(position, &next_point) < 14.0f) &&
			bots_nav_direct_ok(position, next.p))
		{
			bot->nav_path.step++;
			bots_globals.nav_shortcuts++;
			continue;
		}
		*direction = (real)atan2(dy, dx);
		return _bots_nav_follow_moving;
	}
	return _bots_nav_follow_moving;
}

/* the bot has barely moved for a while as the graph leads it: it is blocked (a rail, a ledge, a crack, another bot, where the graph's floor is not where it is).
The link it is on is bad for it if it is at that link; the graph is asked again from where it is after the engine's surfaces have led it a moment */
static void bots_nav_progress(struct bots_bot *bot, real_point3d const *position)
{
	long now = bots_now();
	real moved = bots_distance2d(position, &bot->nav_prev_position);

	bot->nav_prev_position = *position;
	if (bot->nav_state != _bots_nav_state_none || now - bot->nav_prev_tick > 1)
		bot->nav_slow_ticks = 0;
	else if (moved < BOTS_NAV_BARELY_MOVED)
		bot->nav_slow_ticks++;
	else if (moved > 2.0f * BOTS_NAV_BARELY_MOVED)
		bot->nav_slow_ticks = 0;
	bot->nav_prev_tick = now;
	if (bot->nav_slow_ticks >= BOTS_NAV_BLOCKED_TICKS)
	{
		bot->nav_slow_ticks = 0;
		bots_globals.nav_blocked++;
		if (bot->nav_path.valid && bot->nav_path.link_count > 0)
		{
			long slot = MIN((long)bot->nav_path.step / 3, (long)bot->nav_path.link_count - 1);
			struct bots_nav_link const *link = &bots_globals.nav->links[bot->nav_path.links[slot]];
			real ex = link->exit[0] - position->x, ey = link->exit[1] - position->y, nx = link->entry[0] - position->x, ny = link->entry[1] - position->y;

			if (ex * ex + ey * ey < 4.0f || nx * nx + ny * ny < 4.0f)
				bots_nav_mark_bad(bot, bot->nav_path.links[slot], FALSE);
		}
		bot->nav_path.valid = FALSE;
		bot->nav_state = _bots_nav_state_none;
		bot->nav_path_tick = now;
		bot->nav_blind_until = now + BOTS_NAV_BLIND_TICKS;
	}
}

/* the bot is stuck (it has not moved in a second while it walks) */
static void bots_nav_note_stuck(struct bots_bot *bot)
{
	long slot;

	if (!bots_nav_usable() || !bot->nav_path.valid || bot->nav_path.link_count <= 0)
		return;
	slot = MIN((long)bot->nav_path.step / 3, (long)bot->nav_path.link_count - 1);
	if (bots_globals.nav_logged < 60 && config_integer("debug.bot_trace"))
	{
		struct bots_nav_link const *l = &bots_globals.nav->links[bot->nav_path.links[slot]];

		bots_globals.nav_logged++;
		platform_log("bots: nav: a bot is stuck at point %d of %d (link %ld of %d, type %d, state %d, exit (%.1f %.1f %.1f) entry (%.1f %.1f %.1f)), stuck count %d",
			(int)bot->nav_path.step, (int)bots_nav_path_point_count(&bot->nav_path), slot, (int)bot->nav_path.link_count, (int)l->type, (int)bot->nav_state,
			l->exit[0], l->exit[1], l->exit[2], l->entry[0], l->entry[1], l->entry[2], (int)bot->stuck_count);
	}
	/* (a link it stands stuck on is bad for it at once, a walk too: the next path goes another way, or the engine's surfaces are asked) */
	bots_nav_mark_bad(bot, bot->nav_path.links[slot], FALSE);
	bot->nav_blind_until = bots_now() + BOTS_NAV_BLIND_TICKS;
}

/* ---------- the tour (debug.bots_tour: for the headless runs)

The bots meet nobody and go from one place to the next: another player, a flag stand (a starting location on a map with none), a starting location, an item spawn,
in that order from a different one for each bot. What they reach (within two units of a place, three of a player) and what they give up (ninety seconds) is
counted for the bots-sim block: the graph's walking is seen by whether the bots get to the places the game was made for. */

static void bots_set_goal(struct bots_bot *bot, short kind, real_point3d const *point, long surface_index);

#define BOTS_TOUR_KINDS 4
#define BOTS_TOUR_TICKS (90 * TICKS_PER_SECOND)

static char const *const bots_tour_names[BOTS_TOUR_KINDS] = { "players", "flags", "starts", "items" };

/* the point (on the floor) of the tour's next place for this bot, NONE for a place there is none of */
static long bots_tour_place(struct bots_bot *bot, long bot_index, short kind, long k, real_point3d *ground)
{
	struct scenario *scenario = global_scenario_get();
	real_point3d point;
	long count = 0, index;

	if (!scenario)
		return NONE;
	bot->tour_target_unit = NONE;
	if (kind == 0)
	{
		struct data_iterator iterator;
		struct player_datum *other;
		long units[BOTS_MAXIMUM + MAXIMUM_LOCAL_PLAYERS];

		data_iterator_new(&iterator, player_data);
		while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL && count < (long)NUMBEROF(units))
			if (iterator.datum_index != bot->player_index && other->unit_index != NONE)
				units[count++] = other->unit_index;
		if (count <= 0)
			return NONE;
		bot->tour_target_unit = units[(bot_index * 3 + k / BOTS_TOUR_KINDS) % count];
		return bots_unit_surface(bot->tour_target_unit, ground);
	}
	if (kind == 1)
	{
		long flags[32];

		for (index = 0; index < scenario->netgame_flags.count && count < 32; index++)
			if (TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, index, struct scenario_netgame_flag)->type == _netgame_flag_ctf_flag)
				flags[count++] = index;
		if (count > 0)
		{
			point = TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, flags[(bot_index + k / BOTS_TOUR_KINDS) % count], struct scenario_netgame_flag)->position;
			return bots_surface_below(&point, ground);
		}
		kind = 2;
	}
	if (kind == 2)
	{
		count = scenario->players.count;
		if (count <= 0)
			return NONE;
		point = TAG_BLOCK_GET_ELEMENT(&scenario->players, (bot_index * 7 + k) % count, struct player_starting_location)->position;
		return bots_surface_below(&point, ground);
	}
	count = scenario->netgame_equipment.count;
	if (count <= 0)
		return NONE;
	point = TAG_BLOCK_GET_ELEMENT(&scenario->netgame_equipment, (bot_index * 5 + k * 3) % count, struct scenario_netgame_equipment)->position;
	return bots_surface_below(&point, ground);
}

static void bots_tour_goal(struct bots_bot *bot, real_point3d const *position)
{
	long now = bots_now(), bot_index = (long)(bot - bots_globals.bots), attempt;
	short kind = bot->tour_kind;

	if (bot->tour_since_tick)
	{
		boolean active = bot->goal_kind == _bots_goal_roam && bots_distance3d(&bot->goal_point, &bot->tour_goal) < 4.0f;

		if (active && now - bot->tour_since_tick < BOTS_TOUR_TICKS)
		{
			/* (a player moves: the goal follows it, a second at a time) */
			if (kind == 0 && now % TICKS_PER_SECOND == 0 && bot->tour_target_unit != NONE && object_try_and_get_and_verify_type(bot->tour_target_unit, _object_mask_unit))
			{
				real_point3d ground;
				long surface = bots_unit_surface(bot->tour_target_unit, &ground);

				if (surface != NONE)
				{
					bots_set_goal(bot, _bots_goal_roam, &ground, surface);
					bot->tour_goal = bot->goal_point;
				}
			}
			return;
		}
		if (!active && bots_distance2d(&bot->tour_goal, position) < (kind == 0 ? 3.5f : 2.0f) && fabs(bot->tour_goal.z - position->z) < 1.5f)
		{
			bots_globals.tour_reached[kind]++;
			bots_globals.tour_ticks[kind] += now - bot->tour_since_tick;
		}
		else
			bots_globals.tour_given_up[kind]++;
		bot->goal_kind = _bots_goal_none;
		bot->tour_since_tick = 0;
	}
	for (attempt = 0; attempt < 8; attempt++)
	{
		long k = bot->tour_count++;
		real_point3d ground;
		long surface;

		kind = (short)((bot_index + k) % BOTS_TOUR_KINDS);
		surface = bots_tour_place(bot, bot_index, kind, k, &ground);
		if (surface == NONE || bots_distance2d(&ground, position) < 3.0f)
			continue;
		bot->tour_kind = kind;
		bot->tour_since_tick = now;
		bots_set_goal(bot, _bots_goal_roam, &ground, surface);
		bot->tour_goal = bot->goal_point;
		return;
	}
}

static void bots_set_goal(struct bots_bot *bot, short kind, real_point3d const *point, long surface_index);

/* ---------- objectives and timed items (bots_objectives.h, bots_director.h)

A game with an objective (capture the flag, oddball, king of the hill) gives each bot a goal for it when it is not fighting (bots_objective_decide: a
snapshot of the game's flags, ball, hills and players, rebuilt twice a second): the enemy flag, home with it, the carrier of the own flag, a dropped flag,
an escort, the stand, the hill. The goals are those of the HUD's goal table and of the flag and ball objects (the weapons the game puts in the stands), read
as any machine reads them; nothing of the engine is changed. Power weapons and powerups that spawn on a timer (the engine's rule: when the game's time is a
multiple of the item's period) are set out for by the bots of normal skill and up so as to be there as they spawn, and one bot at a time to a place.
bots.objectives = 0 turns both off. */

#define BOTS_OBJECTIVE_REFRESH_TICKS 15
#define BOTS_OBJECTIVE_DECIDE_TICKS TICKS_PER_SECOND
#define BOTS_GOAL_STALE_TICKS (45 * TICKS_PER_SECOND)
#define BOTS_TIMED_ITEMS 48
#define item_collection_definition_get(index) ((struct item_collection_definition *)tag_get(ITEM_COLLECTION_DEFINITION_TAG, index))

static char const *const bots_objective_names[NUMBER_OF_BOTS_OBJECTIVES] =
{
	"none", "grab", "capture", "wait", "hunt", "escort", "defend", "return", "hold",
};

/* a weapon object the game puts in play for its objective: a flag (0 the red's, 1 the blue's by its owner team) or the ball */
static int bots_objective_item_kind(struct weapon_datum *weapon)
{
	char const *name = tag_get_name(weapon->definition_index);
	size_t length = name ? strlen(name) : 0;

	if (!name)
		return 0;
	if (strstr(name, "flag"))
		return 1;
	if (length >= 4 && !strcmp(name + length - 4, "ball"))
		return 2;
	return 0;
}

/* the snapshot of the game: where the flags, the ball and the hills are, and who is who */
static boolean bots_objective_snapshot(void)
{
	struct bots_objective_state *state = &bots_globals.objective_state;
	struct scenario *scenario = global_scenario_get();
	struct game_variant *variant = game_engine_get_variant();
	struct data_iterator iterator;
	struct player_datum *player;
	struct object_iterator objects;
	long index;

	if (bots_globals.objective_tick != NONE && bots_now() - bots_globals.objective_tick < BOTS_OBJECTIVE_REFRESH_TICKS)
		return state->game != _bots_game_other;
	bots_globals.objective_tick = bots_now();
	csmemset(state, 0, sizeof(*state));
	state->ball_carrier = -1;
	state->flag[0].carrier = state->flag[1].carrier = -1;
	if (!variant || !scenario)
		return FALSE;
	switch (variant->game_engine_index)
	{
	case game_engine_ctf: state->game = _bots_game_ctf; break;
	case game_engine_oddball: state->game = _bots_game_oddball; break;
	case game_engine_king: state->game = _bots_game_king; break;
	default: state->game = _bots_game_other; break;
	}
	if (state->game == _bots_game_other)
		return FALSE;
	state->teams = game_engine_has_teams();
	state->flag_at_home_to_score = variant->game_engine_variant.ctf.flag_at_home_to_score;
	/* (the players: living ones are the table's members) */
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL && state->member_count < BOTS_OBJECTIVE_MEMBERS)
	{
		struct bots_objective_member *member = &state->member[state->member_count];

		if (player->unit_index == NONE)
			continue;
		member->team = player->team_index;
		member->alive = TRUE;
		member->bot = player->local_player_index == NONE && bots_machine_is_bot(player->network_player_data.machine_index);
		member->carrying_flag = -1;
		member->p[0] = object_get(player->unit_index)->object.position.x;
		member->p[1] = object_get(player->unit_index)->object.position.y;
		member->p[2] = object_get(player->unit_index)->object.position.z;
		bots_globals.objective_players[state->member_count] = iterator.datum_index;
		bots_globals.objective_units[state->member_count] = player->unit_index;
		state->member_count++;
	}
	/* (the stands) */
	for (index = 0; index < scenario->netgame_flags.count; index++)
	{
		struct scenario_netgame_flag *flag = TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, index, struct scenario_netgame_flag);

		if (flag->type == _netgame_flag_ctf_flag && (flag->team_index == 0 || flag->team_index == 1))
		{
			struct bots_objective_flag *f = &state->flag[flag->team_index];

			f->home[0] = f->p[0] = flag->position.x;
			f->home[1] = f->p[1] = flag->position.y;
			f->home[2] = f->p[2] = flag->position.z;
			f->present = state->game == _bots_game_ctf;
		}
	}
	/* (the flag objects and the ball: where they are, and who holds them) */
	object_iterator_new(&objects, _object_mask_weapon, 0);
	while (object_iterator_next(&objects))
	{
		struct weapon_datum *weapon = weapon_get(objects.index);
		int kind = bots_objective_item_kind(weapon);
		real_point3d position = weapon->object.position;
		int carrier = -1, member;

		if (!kind)
			continue;
		if (weapon->object.parent_object_index != NONE)
		{
			for (member = 0; member < state->member_count; member++)
				if (bots_globals.objective_units[member] == weapon->object.parent_object_index)
					carrier = member;
			if (carrier >= 0)
			{
				position.x = state->member[carrier].p[0];
				position.y = state->member[carrier].p[1];
				position.z = state->member[carrier].p[2];
			}
		}
		if (kind == 1 && state->game == _bots_game_ctf && (weapon->object.owner_team_index == 0 || weapon->object.owner_team_index == 1))
		{
			struct bots_objective_flag *f = &state->flag[weapon->object.owner_team_index];

			f->p[0] = position.x;
			f->p[1] = position.y;
			f->p[2] = position.z;
			f->carrier = carrier;
			f->at_home = carrier < 0 && bots_distance3d(&position, &(real_point3d){ f->home[0], f->home[1], f->home[2] }) < 1.5f;
			f->present = TRUE;
			if (carrier >= 0)
				state->member[carrier].carrying_flag = weapon->object.owner_team_index;
		}
		else if (kind == 2 && state->game == _bots_game_oddball && !state->ball_present)
		{
			state->ball_present = TRUE;
			state->ball[0] = position.x;
			state->ball[1] = position.y;
			state->ball[2] = position.z;
			state->ball_carrier = carrier;
			if (carrier >= 0)
				state->member[carrier].carrying_ball = TRUE;
		}
	}
	/* (the hills: the goal table's places) */
	if (state->game == _bots_game_king)
	{
		for (index = 0; index < BOTS_OBJECTIVE_HILLS; index++)
		{
			if (game_engine_get_goal_in_use((short)index))
			{
				real_point3d position;

				game_engine_get_goal_position(&position, (short)index);
				state->hill[state->hill_count][0] = position.x;
				state->hill[state->hill_count][1] = position.y;
				state->hill[state->hill_count][2] = position.z;
				state->hill_count++;
			}
		}
	}
	return TRUE;
}

/* the objective goal of a bot, if the game has one for it (TRUE: the bot has a goal set, or keeps the one it had) */
static boolean bots_objective_pick(struct bots_bot *bot, real_point3d const *position)
{
	struct bots_objective_state const *state = &bots_globals.objective_state;
	struct bots_objective_goal goal;
	long now = bots_now(), member;
	real_point3d point, ground;
	long surface;

	if (!bots_globals.objectives_enabled || !bots_objective_snapshot())
		return FALSE;
	if (bot->goal_kind == _bots_goal_objective && now - bot->objective_tick < BOTS_OBJECTIVE_DECIDE_TICKS)
		return TRUE;
	for (member = 0; member < state->member_count; member++)
		if (bots_globals.objective_players[member] == bot->player_index)
			break;
	if (member >= state->member_count)
		return FALSE;
	bot->objective_tick = now;
	/* (the first levels forget it now and then: not this time) */
	if ((long)(bots_random(bot) % 100UL) >= bots_objective_weight_percent(bot->profile.skill) && bot->goal_kind != _bots_goal_objective)
		return FALSE;
	bots_budget_spend(&bots_globals.budget, BOTS_BUDGET_DECISION);
	bots_objective_decide(state, (int)member, &goal);
	if (goal.kind == _bots_objective_none)
	{
		if (bot->goal_kind == _bots_goal_objective)
			bot->goal_kind = _bots_goal_none;
		bot->objective_kind = _bots_objective_none;
		return FALSE;
	}
	point.x = goal.p[0];
	point.y = goal.p[1];
	point.z = goal.p[2];
	surface = goal.target >= 0 ? bots_unit_surface(bots_globals.objective_units[goal.target], &ground) : bots_surface_below(&point, &ground);
	if (surface == NONE)
		return FALSE;
	/* (a goal already at hand: held, not made again) */
	if (bot->goal_kind != _bots_goal_objective || bot->objective_kind != goal.kind)
		bots_globals.objective_counts[goal.kind]++;
	bots_set_goal(bot, _bots_goal_objective, &ground, surface);
	bot->objective_kind = (short)goal.kind;
	return TRUE;
}

/* the places of the timed items (power weapons and powerups: the scenario's placements, classed by their collection), made at the map's start */
static void bots_items_prepare(void)
{
	struct scenario *scenario = global_scenario_get();
	long index;

	bots_globals.timed_item_count = 0;
	if (!scenario)
		return;
	for (index = 0; index < scenario->netgame_equipment.count && bots_globals.timed_item_count < BOTS_TIMED_ITEMS; index++)
	{
		struct scenario_netgame_equipment *equipment = TAG_BLOCK_GET_ELEMENT(&scenario->netgame_equipment, index, struct scenario_netgame_equipment);
		struct bots_timed_item *timed = &bots_globals.timed_items[bots_globals.timed_item_count];
		char name[400];
		char const *tag_name;
		long item_class;

		if (equipment->item_collection.index == NONE)
			continue;
		tag_name = tag_get_name(equipment->item_collection.index);
		snprintf(name, sizeof(name), "%s", tag_name ? tag_name : "");
		{
			struct item_collection_definition *collection = item_collection_definition_get(equipment->item_collection.index);

			if (collection && collection->permutations.count > 0)
			{
				struct item_permutation_definition *permutation = TAG_BLOCK_GET_ELEMENT(&collection->permutations, 0, struct item_permutation_definition);
				char const *item_name = permutation->item.index != NONE ? tag_get_name(permutation->item.index) : NULL;

				if (item_name)
					snprintf(name + strlen(name), sizeof(name) - strlen(name), " %s", item_name);
			}
			timed->period_seconds = equipment->spawn_time ? equipment->spawn_time : (collection && collection->spawn_time ? collection->spawn_time : 30);
		}
		item_class = bots_items_class_from_name(name);
		if (item_class != _bots_nav_item_power_weapon && item_class != _bots_nav_item_powerup)
			continue;
		timed->item_class = (short)item_class;
		timed->equipment_index = index;
		timed->position = equipment->position;
		timed->claimed_by = NONE;
		bots_globals.timed_item_count++;
	}
	if (bots_globals.timed_item_count)
		platform_log("bots: %ld timed items on this map (power weapons and powerups), the first every %d seconds", bots_globals.timed_item_count,
			(int)bots_globals.timed_items[0].period_seconds);
}

/* whether an item lies at the place now (a weapon or equipment object with no holder within a unit) */
static boolean bots_items_present(real_point3d const *place)
{
	struct object_iterator iterator;

	object_iterator_new(&iterator, _object_mask_item, 0);
	while (object_iterator_next(&iterator))
	{
		struct object_datum *object = object_get(iterator.index);

		if (object->object.parent_object_index == NONE && bots_distance3d(&object->object.position, place) < 1.2f)
			return TRUE;
	}
	return FALSE;
}

/* a place to set out for: the item of the earliest worth setting out for, and one bot at a time to a place (TRUE: the goal is set) */
static boolean bots_items_pick(struct bots_bot *bot, real_point3d const *position)
{
	struct scenario *scenario = global_scenario_get();
	struct game_variant *variant = game_engine_get_variant();
	long index, best = NONE, bot_index = (long)(bot - bots_globals.bots), now = bots_now();
	long best_eta = 0;
	real const speed = (real)(2.25f / (real)TICKS_PER_SECOND);

	if (!bots_globals.objectives_enabled || !bots_globals.timed_item_count || !scenario || !variant || bot->profile.skill < 2 ||
		(bot->goal_kind == _bots_goal_place && now < bot->place_until_tick))
	{
		return bot->goal_kind == _bots_goal_place && now < bot->place_until_tick;
	}
	/* (looked at once in a while: the scan over the items is not for every tick) */
	if (now < bot->place_check_tick)
		return FALSE;
	bot->place_check_tick = now + TICKS_PER_SECOND + (bot_index % 7) * 3;
	bots_budget_spend(&bots_globals.budget, BOTS_BUDGET_DECISION);
	for (index = 0; index < bots_globals.timed_item_count; index++)
	{
		struct bots_timed_item *timed = &bots_globals.timed_items[index];
		struct scenario_netgame_equipment *equipment =
			TAG_BLOCK_GET_ELEMENT(&scenario->netgame_equipment, timed->equipment_index, struct scenario_netgame_equipment);
		long ticks_until_spawn, eta;

		if (!bots_items_class_wanted(bot->profile.skill, timed->item_class) ||
			!match_game_type(variant->game_engine_index, NUMBEROF(equipment->game_type), equipment->game_type))
		{
			continue;
		}
		/* (a place another bot is on its way to, and alive: left to it) */
		if (timed->claimed_by != NONE && timed->claimed_by != bot_index && bots_globals.bots[timed->claimed_by].goal_kind == _bots_goal_place &&
			bots_globals.bots[timed->claimed_by].place_index == index && now < bots_globals.bots[timed->claimed_by].place_until_tick)
		{
			continue;
		}
		ticks_until_spawn = bots_items_ticks_until_spawn(game_time_get(), timed->period_seconds);
		eta = (long)(bots_distance3d(position, &timed->position) * 1.4f / speed);
		if (bots_distance2d(position, &timed->position) < 3.0f || eta > 90 * TICKS_PER_SECOND)
			continue;
		/* (the item is there already: only for one that spawned in the last 20 seconds, which is looked for in the world; else the time) */
		if (!bots_items_should_go(bot->profile.skill, ticks_until_spawn, eta, 0) &&
			!(timed->period_seconds * TICKS_PER_SECOND - ticks_until_spawn < 20 * TICKS_PER_SECOND && bots_items_present(&timed->position)))
		{
			continue;
		}
		if (best == NONE || eta < best_eta)
		{
			best = index;
			best_eta = eta;
		}
	}
	if (best == NONE)
		return FALSE;
	{
		real_point3d ground;
		long surface = bots_surface_below(&bots_globals.timed_items[best].position, &ground);

		if (surface == NONE)
			return FALSE;
		bots_set_goal(bot, _bots_goal_place, &ground, surface);
		bot->place_index = best;
		bot->place_until_tick = now + best_eta + 6 * TICKS_PER_SECOND + MIN(bots_items_ticks_until_spawn(game_time_get(), bots_globals.timed_items[best].period_seconds), 20L * TICKS_PER_SECOND);
		bots_globals.timed_items[best].claimed_by = bot_index;
		bots_globals.timed_item_trips++;
	}
	return TRUE;
}

/* ---------- perception */

/* whether the bot sees the point from its eyes (the AI's sight test) */
static boolean bots_line_of_sight(long unit_index, real_point3d const *eye, real_point3d const *point, long target_unit_index)
{
	struct collision_result collision;
	real_vector3d vector;

	vector.i = point->x - eye->x;
	vector.j = point->y - eye->y;
	vector.k = point->z - eye->z;
	bots_globals.rays++;
	bots_budget_spend(&bots_globals.budget, BOTS_BUDGET_SIGHT_TEST);
	if (bots_globals.active_bot)
		bots_globals.active_bot->counts.rays++;
	if (!collision_test_vector(_collision_test_for_line_of_sight_flags, eye, &vector, unit_index, &collision))
		return TRUE;
	/* (the target's vehicle: seen) */
	if (collision.type == _collision_result_object && target_unit_index != NONE &&
		(collision.object_index == target_unit_index ||
			collision.object_index == object_get(target_unit_index)->object.parent_object_index))
	{
		return TRUE;
	}
	return FALSE;
}

static boolean bots_is_enemy(struct player_datum *player, struct player_datum *other)
{
	if (player == other || other->unit_index == NONE)
		return FALSE;
	if (game_engine_has_teams() && player->team_index == other->team_index)
		return FALSE;
	return TRUE;
}

/* the enemy the bot sees best now, NONE for none: of the enemies in its
sight's reach and view, the one of the least score (nearest, most ahead, the
one it fights favoured) a line of sight reaches, the first in the players'
order of those of that score. The enemies are scored first and their lines
of sight cast from the least score up, until one reaches its enemy, which is
the answer. A score that is not a number (none is: the players' positions
are), or more enemies than kept, take the players' order */
struct bots_target_candidate
{
	long player_index;
	long unit_index;
	real score;
	real_point3d point;
};

/* an enemy's score as a target (its point the bounding sphere's centre);
FALSE for one out of reach or view, or not an enemy */
static boolean bots_target_score(struct bots_bot *bot, struct player_datum *player, struct player_datum *other,
	long player_index, real_point3d const *eye, struct bots_skill_definition const *skill, boolean hurt,
	struct bots_target_candidate *candidate)
{
	struct object_datum *object;
	struct unit_datum *other_unit;
	real distance, reach, bearing;
	real half_view = 0.5f * skill->field_of_view_degrees * BOTS_DEGREES;

	bots_budget_spend(&bots_globals.budget, BOTS_BUDGET_CANDIDATE);
	if (!bots_is_enemy(player, other))
		return FALSE;
	object = object_get(other->unit_index);
	other_unit = unit_get(other->unit_index);
	if (TEST_FLAG(object->object.damage_flags, _object_dead_bit))
		return FALSE;
	candidate->point = object->object.bounding_sphere_center;
	distance = bots_distance3d(eye, &candidate->point);
	reach = skill->sight_distance;
	/* (active camouflage: seen only close) */
	if (TEST_FLAG(other_unit->unit.flags, _unit_active_camouflaged_bit))
		reach *= 0.3f;
	/* (the one it fights: a little further) */
	if (player_index == bot->target_player_index)
		reach *= 1.25f;
	if (distance > reach)
		return FALSE;
	bearing = (real)fabs(bots_angle_difference((real)atan2(candidate->point.y - eye->y, candidate->point.x - eye->x),
		bot->yaw));
	/* (out of view: unless close by, it shot the bot, or it is the one
	the bot fights) */
	if (bearing > half_view && distance > 3.0f && !hurt && player_index != bot->target_player_index)
		return FALSE;
	/* (nor far above or below where it looks, on the same terms: a player
	running by looks ahead, not up) */
	if (distance > 3.0f && !hurt && player_index != bot->target_player_index)
	{
		real dx = candidate->point.x - eye->x, dy = candidate->point.y - eye->y;
		real elevation = (real)atan2(candidate->point.z - eye->z, (real)sqrt(dx * dx + dy * dy));

		if (!bots_combat_in_vertical_view(bot->profile.skill, elevation / BOTS_DEGREES, bot->pitch / BOTS_DEGREES))
		{
			bots_globals.combat_counts[_bots_combat_count_out_of_view]++;
			return FALSE;
		}
	}
	candidate->player_index = player_index;
	candidate->unit_index = other->unit_index;
	candidate->score = distance + bearing * 4.0f - (player_index == bot->target_player_index ? 6.0f : 0.0f);
	return TRUE;
}

static long bots_find_target(struct bots_bot *bot, struct player_datum *player, real_point3d const *eye)
{
	struct bots_skill_definition const *skill = &bots_skills[bot->profile.skill];
	struct data_iterator iterator;
	struct player_datum *other;
	boolean hurt = bots_now() - bot->hurt_tick < TICKS_PER_SECOND * 2;
	struct bots_target_candidate candidates[BOTS_MAXIMUM + MAXIMUM_LOCAL_PLAYERS];
	long candidate_count = 0, index;
	boolean ordered = TRUE;

	data_iterator_new(&iterator, player_data);
	while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL && ordered)
	{
		if (candidate_count >= (long)NUMBEROF(candidates))
			ordered = FALSE;
		else if (bots_target_score(bot, player, other, iterator.datum_index, eye, skill, hurt, &candidates[candidate_count]))
		{
			real score = candidates[candidate_count].score;

			if (score != score)
				ordered = FALSE;
			else if (score < REAL_MAX)
				candidate_count++;
		}
	}
	if (!ordered)
	{
		/* (the players' order, each scoring less than the best so far) */
		struct bots_target_candidate candidate;
		long best_index = NONE;
		real best_score = REAL_MAX;

		data_iterator_new(&iterator, player_data);
		while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			if (!bots_target_score(bot, player, other, iterator.datum_index, eye, skill, hurt, &candidate) ||
				candidate.score >= best_score ||
				!bots_line_of_sight(player->unit_index, eye, &candidate.point, candidate.unit_index))
			{
				continue;
			}
			best_score = candidate.score;
			best_index = candidate.player_index;
		}
		return best_index;
	}
	while (candidate_count > 0)
	{
		long least = 0;

		/* (the least score, the first of it in the players' order) */
		for (index = 1; index < candidate_count; index++)
		{
			if (candidates[index].score < candidates[least].score)
				least = index;
		}
		if (bots_line_of_sight(player->unit_index, eye, &candidates[least].point, candidates[least].unit_index))
			return candidates[least].player_index;
		/* (out, the others kept in their order) */
		candidate_count--;
		for (index = least; index < candidate_count; index++)
			candidates[index] = candidates[index + 1];
	}
	return NONE;
}

/* ---------- moving */

static void bots_set_goal(struct bots_bot *bot, short kind, real_point3d const *point, long surface_index)
{
	/* (the same goal, near enough: the path stands) */
	if (bot->goal_kind == kind && bot->goal_surface_index == surface_index &&
		bots_distance3d(&bot->goal_point, point) < 1.5f)
	{
		return;
	}
	bot->goal_kind = kind;
	bot->goal_point = *point;
	bot->goal_surface_index = surface_index;
	bot->goal_tick = bots_now();
	bot->path.valid = FALSE;
	bot->path_failures = 0;
	bot->climb_until_tick = 0;
}

/* a roaming place picked at random (index), or a higher one (VETERAN and
up): two more places at random, not within 6 units,
and the one of the most credit for its height above the bot */
static short bots_roam_higher(struct bots_bot *bot, real_point3d const *position, short index)
{
	short best = index, tries;
	real best_credit = bots_combat_high_ground(bot->profile.skill, bots_globals.roam_points[index].z - position->z);

	if (bot->profile.skill < _bots_skill_veteran)
		return index;
	for (tries = 0; tries < 2; tries++)
	{
		short other = (short)(bots_random(bot) % (unsigned long)bots_globals.roam_point_count);
		real credit;

		if (bots_distance2d(&bots_globals.roam_points[other], position) < 6.0f)
			continue;
		credit = bots_combat_high_ground(bot->profile.skill, bots_globals.roam_points[other].z - position->z);
		if (credit > best_credit)
		{
			best_credit = credit;
			best = other;
		}
	}
	if (best != index)
		bots_globals.combat_counts[_bots_combat_count_high_ground]++;
	return best;
}

/* the next place to roam to: most often towards an enemy (the roaming place
nearest a living enemy, as a player knows where the other team's base is
and reads the motion tracker), else anywhere */
static void bots_pick_roam_goal(struct bots_bot *bot, real_point3d const *position)
{
	long tries;

	if (bots_globals.roam_point_count <= 0)
		return;
	bots_budget_spend(&bots_globals.budget, BOTS_BUDGET_DECISION);
	bot->hunting = FALSE;
	if (bots_random_real(bot) < bots_combat_hunt_chance(bot->profile.skill))
	{
		struct player_datum *player = player_get(bot->player_index);
		struct data_iterator iterator;
		struct player_datum *other;
		long enemies[BOTS_MAXIMUM + MAXIMUM_LOCAL_PLAYERS];
		long enemy_count = 0;

		data_iterator_new(&iterator, player_data);
		while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL &&
			enemy_count < (long)NUMBEROF(enemies))
		{
			if (bots_is_enemy(player, other))
				enemies[enemy_count++] = other->unit_index;
		}
		if (enemy_count > 0)
		{
			struct object_datum *enemy = object_get(enemies[bots_random(bot) % (unsigned long)enemy_count]);
			long index, nearest = NONE;
			real nearest_distance = REAL_MAX;

			for (index = 0; index < bots_globals.roam_point_count; index++)
			{
				real distance = bots_distance2d(&bots_globals.roam_points[index], &enemy->object.position) +
					bots_random_real(bot) * 6.0f;

				/* (a place above the enemy counts as nearer, from veteran) */
				distance -= bots_combat_high_ground(bot->profile.skill,
					bots_globals.roam_points[index].z - enemy->object.position.z);

				if (distance < nearest_distance && bots_distance2d(&bots_globals.roam_points[index], position) > 4.0f)
				{
					nearest_distance = distance;
					nearest = index;
				}
			}
			if (nearest != NONE)
			{
				bot->roam_index = (short)nearest;
				bot->hunting = TRUE;
				bots_set_goal(bot, _bots_goal_roam, &bots_globals.roam_points[nearest], bots_globals.roam_surfaces[nearest]);
				return;
			}
		}
	}
	for (tries = 0; tries < 6; tries++)
	{
		short index = (short)(bots_random(bot) % (unsigned long)bots_globals.roam_point_count);

		/* (not where it is) */
		if (bots_distance2d(&bots_globals.roam_points[index], position) < 6.0f && tries < 5)
			continue;
		index = bots_roam_higher(bot, position, index);
		bot->roam_index = index;
		bots_set_goal(bot, _bots_goal_roam, &bots_globals.roam_points[index], bots_globals.roam_surfaces[index]);
		return;
	}
}

/* where the bot goes: the enemy last seen, a better weapon near, or about
the map */
static void bots_choose_goal(struct bots_bot *bot, long player_index, real_point3d const *position, boolean fighting)
{
	long now = bots_now();

	if (bots_globals.tour)
	{
		bots_tour_goal(bot, position);
		return;
	}
	/* (an objective goal held so long without being reached is given up: a place it holds is not) */
	if (bot->goal_kind == _bots_goal_objective &&
		bots_director_goal_stale(now - bot->goal_tick, BOTS_GOAL_STALE_TICKS, bot->objective_kind == _bots_objective_defend ||
			bot->objective_kind == _bots_objective_hold || bot->objective_kind == _bots_objective_wait || bot->objective_kind == _bots_objective_escort ||
			bot->objective_kind == _bots_objective_hunt))
	{
		bots_globals.objective_stale++;
		bot->goal_kind = _bots_goal_none;
	}
	if (bot->goal_kind == _bots_goal_place && now >= bot->place_until_tick)
		bot->goal_kind = _bots_goal_none;
	/* (the game's objective first: with the flag, or with a goal for the bot at all, nothing else is thought of) */
	if (!fighting && bots_objective_pick(bot, position))
		return;

	if (!fighting && bot->target_player_index != NONE && bot->target_last_surface_index != NONE &&
		now - bot->target_seen_tick < TICKS_PER_SECOND * 8)
	{
		bots_set_goal(bot, _bots_goal_enemy, &bot->target_last_position, bot->target_last_surface_index);
		return;
	}
	/* a better weapon near (looked for once a second; given up after a
	while) */
	if (bot->goal_kind == _bots_goal_item)
	{
		struct item_datum *item = (struct item_datum *)object_try_and_get_and_verify_type(bot->item_index,
			_object_mask_weapon);

		if (item && item->object.parent_object_index == NONE && now - bot->goal_tick < TICKS_PER_SECOND * 15 &&
			(bot->item_first_press_tick < bot->goal_tick || now - bot->item_first_press_tick < TICKS_PER_SECOND * 3))
		{
			return;
		}
		/* (one it did not get, standing on it or not: left be a while) */
		if (item && item->object.parent_object_index == NONE)
			bots_item_set_unreachable(bot, bot->item_index);
		bot->goal_kind = _bots_goal_none;
		bot->item_check_tick = now + TICKS_PER_SECOND * 2 * bots_budget_decision_scale(&bots_globals.budget);
	}
	if (now >= bot->item_check_tick)
	{
		long item_index = bots_find_item(bot, unit_get(player_get(player_index)->unit_index), position, fighting ? 8.0f : 20.0f);

		bot->item_check_tick = now + TICKS_PER_SECOND * bots_budget_decision_scale(&bots_globals.budget);
		bots_budget_spend(&bots_globals.budget, BOTS_BUDGET_DECISION);
		if (item_index != NONE)
		{
			real_point3d ground;
			long surface_index = bots_surface_below(&object_get(item_index)->object.position, &ground);

			if (surface_index != NONE)
			{
				bots_set_goal(bot, _bots_goal_item, &ground, surface_index);
				bot->item_index = item_index;
				return;
			}
		}
	}
	/* (a power weapon or a powerup that spawns soon: its place) */
	if (!fighting && bots_items_pick(bot, position))
		return;
	if (bot->goal_kind != _bots_goal_roam || bots_distance2d(&bot->goal_point, position) < 1.5f ||
		now - bot->goal_tick > TICKS_PER_SECOND * (bot->hunting ? 12 : 40))
	{
		bots_pick_roam_goal(bot, position);
	}
}

/* the direction the bot walks (radians), following its path; FALSE when
it has nowhere to go */
static boolean bots_follow_path(struct bots_bot *bot, long unit_index, real_point3d const *position, real *direction)
{
	long now = bots_now();

	if (bot->goal_kind == _bots_goal_none)
		return FALSE;
	if (now < bot->escape_until_tick)
	{
		*direction = bot->escape_direction;
		return TRUE;
	}
	/* (the map's navigation graph, if it has one and it reaches the goal) */
	if (bots_nav_usable())
	{
		int result = bots_nav_follow(bot, unit_index, position, direction);

		if (result == _bots_nav_follow_moving)
		{
			bots_nav_progress(bot, position);
			return TRUE;
		}
		if (result == _bots_nav_follow_arrived)
			return FALSE;
	}
	/* (at the end of a path short of its goal: straight on, jumping, while
	it comes nearer (the walkable surfaces miss ramps a biped walks up:
	Prisoner's, Wizard's); then somewhere else) */
	if (bot->climb_until_tick)
	{
		real distance = bots_distance3d(&bot->goal_point, position);

		if (distance < bot->climb_best_distance - 0.5f && now - bot->climb_start_tick < TICKS_PER_SECOND * 20)
		{
			bot->climb_best_distance = distance;
			bot->climb_until_tick = now + TICKS_PER_SECOND * 3;
		}
		if (now < bot->climb_until_tick && distance > 1.0f)
		{
			*direction = (real)atan2(bot->goal_point.y - position->y, bot->goal_point.x - position->x);
			return TRUE;
		}
		bot->climb_until_tick = 0;
		if (distance > 1.0f)
		{
			bot->goal_kind = _bots_goal_none;
			return FALSE;
		}
	}
	/* (a new path now and then: the objects about move, and the goal) */
	if ((!bot->path.valid || now - bot->path_tick > TICKS_PER_SECOND * 4) &&
		now - bot->path_tick > TICKS_PER_SECOND / 3)
	{
		bots_search(bot, unit_index);
	}
	if (bot->path.valid)
	{
		while (bot->path.step_index < bot->path.step_count)
		{
			struct path_step *step = &bot->path.steps[(long)bot->path.step_index];

			if (bots_distance2d(&step->point, position) > BOTS_STEP_REACHED)
				break;
			bot->path.step_index++;
		}
		if (bot->path.step_index >= bot->path.step_count)
		{
			bot->path.valid = FALSE;
			/* (as near as the search came, the goal on above or beyond:
			on towards it) */
			if (bot->path.steps_finish_path && bot->path_short && bots_distance3d(&bot->goal_point, position) > 1.5f &&
				!bot->climb_until_tick)
			{
				bot->climb_start_tick = now;
				bot->climb_until_tick = now + TICKS_PER_SECOND * 3;
				bot->climb_best_distance = bots_distance3d(&bot->goal_point, position);
				*direction = (real)atan2(bot->goal_point.y - position->y, bot->goal_point.x - position->x);
				return TRUE;
			}
			/* (the whole way walked: there) */
			if (bot->path.steps_finish_path)
				return FALSE;
			bots_search(bot, unit_index);
		}
		else
		{
			struct path_step *step = &bot->path.steps[(long)bot->path.step_index];

			*direction = (real)atan2(step->point.y - position->y, step->point.x - position->x);
			return TRUE;
		}
	}
	/* (no path yet, or none found: straight there) */
	if (bots_distance2d(&bot->goal_point, position) < 1.0f)
		return FALSE;
	*direction = (real)atan2(bot->goal_point.y - position->y, bot->goal_point.x - position->x);
	return TRUE;
}

/* ---------- a bot's tick */

/* the level that debug.bots_skills ("normal,legendary,,recruit": a bot's level by its place, an empty place the setting's) gives the
bot, or NONE */
static short bots_debug_skill(long bot_index)
{
	char const *list = config_string("debug.bots_skills");
	short skill;

	while (bot_index > 0 && *list)
	{
		char const *comma = strchr(list, ',');

		if (!comma)
			return NONE;
		list = comma + 1;
		bot_index--;
	}
	for (skill = 0; skill < NUMBER_OF_BOTS_SKILLS; skill++)
	{
		size_t length = strlen(bots_skills[skill].name);

		if (!strncmp(list, bots_skills[skill].name, length) && (list[length] == ',' || list[length] == 0))
			return skill;
	}
	return NONE;
}

static void bots_bot_reset(struct bots_bot *bot, long player_index, long bot_index)
{
	short own_skill;

	csmemset(bot, 0, sizeof(*bot));
	bots_profile_reset(&bot->profile, bots_globals.setting_skill);
	own_skill = bots_debug_skill(bot_index);
	if (own_skill != NONE)
		bots_profile_set_skill(&bot->profile, own_skill);
	if (bots_requested_skill[bot_index] > 0)
		bots_profile_set_skill(&bot->profile, (short)(bots_requested_skill[bot_index] - 1));
	bot->player_index = player_index;
	bot->unit_index = NONE;
	bot->target_player_index = NONE;
	bot->target_last_surface_index = NONE;
	bot->goal_surface_index = NONE;
	bot->nav_goal_area = NONE;
	bot->random = 0x2545F491UL * (unsigned long)(bot_index + 1) ^ (unsigned long)network_game_get_random_seed();
	bot->strafe_sign = 1.0f;
}

/* a bot's tick: what it knows of itself, and the controls it builds */
struct bots_tick
{
	struct bots_skill_definition const *skill;
	struct player_datum *player;
	struct unit_datum *unit;
	struct object_datum *object;
	long now;
	/* the unit's eyes (its head marker), found when first needed: the look
	(every third tick) and the fight (bots_tick_eye) */
	boolean eye_found;
	real_point3d eye;
	real_point3d position;
	long weapon_index;
	/* the controls: buttons, where it walks, how hard */
	unsigned long flags;
	boolean moving;
	real move_direction;
	real throttle_scale;
	/* it has a target in sight, this far away */
	boolean fighting;
	real target_distance;
	struct player_action *action;
};

/* a jump or a slower run that the graph's follower asked for, as buttons */
static void bots_nav_apply(struct bots_bot *bot, struct bots_tick *tick)
{
	if (bot->nav_jump)
	{
		tick->flags |= FLAG(_unit_control_jump_bit);
		bot->nav_jump = FALSE;
	}
	if (bot->nav_throttle > 0.0f)
	{
		tick->throttle_scale = bot->nav_throttle;
		bot->nav_throttle = 0.0f;
	}
}

/* the unit's eyes: its head marker, as unit_get_head_position finds it
(the marker's matrix from the object's node matrices, which nothing in the
bot's tick changes, so it is the same found now or at the tick's start) */
static real_point3d const *bots_tick_eye(struct bots_tick *tick)
{
	if (!tick->eye_found)
	{
		unit_get_head_position(tick->player->unit_index, &tick->eye);
		tick->eye_found = TRUE;
	}
	return &tick->eye;
}

/* ---------- fighting by level (bots_combat.c's rules) */

/* a bot's fight started over (a spawn) */
static void bots_level_reset(struct bots_bot_combat *combat)
{
	long index;

	csmemset(combat, 0, sizeof(*combat));
	bots_combat_retreat_reset(&combat->retreat);
	combat->last_fight_tick = -TICKS_PER_SECOND * 10;
	combat->melee_ready_tick = NONE;
	combat->pace_since_tick = NONE;
	combat->prenade_seen_tick = NONE;
	for (index = 0; index < BOTS_TRACK_TICKS; index++)
		combat->track[index].tick = NONE;
}

/* the aim point (its target's middle now) where the target was the level's
delay ago (RECRUIT and EASY), so that a target that turns back is missed a
moment, as a person misses it; the places kept a tick each */
static void bots_level_trail(struct bots_bot *bot, long now, real_point3d *aim_point)
{
	struct bots_bot_combat *combat = &bot->combat;
	long delay = bots_combat_track_delay_ticks(bot->profile.skill);
	long slot;

	if (now < 0)
		return;
	slot = now % BOTS_TRACK_TICKS;
	combat->track[slot].tick = now;
	combat->track[slot].player_index = bot->target_player_index;
	combat->track[slot].point = *aim_point;
	if (delay <= 0 || delay >= BOTS_TRACK_TICKS || now < delay)
		return;
	slot = (now - delay) % BOTS_TRACK_TICKS;
	if (combat->track[slot].tick == now - delay && combat->track[slot].player_index == bot->target_player_index)
		*aim_point = combat->track[slot].point;
}

/* the aim point: the head of a target whose shields are down, with a weapon
that kills with a shot to it (NORMAL and up); with a grenade in the hand,
ahead of the target by its flight (HEROIC and up), at most 3.5 units */
static void bots_level_aim_point(struct bots_bot *bot, struct bots_tick *tick, long target_unit_index,
	real_point3d *aim_point)
{
	struct bots_bot_combat *combat = &bot->combat;
	struct object_datum *target = object_get(target_unit_index);
	boolean head = tick->weapon_index != NONE && bots_combat_aim_head(bot->profile.skill,
		target->object.shield_vitality, bots_weapon_kind(weapon_get(tick->weapon_index)->definition_index)->precision);

	if (head)
	{
		unit_get_head_position(target_unit_index, aim_point);
		if (!combat->head_aim)
			bots_globals.combat_counts[_bots_combat_count_head_aims]++;
	}
	combat->head_aim = head;
	if (tick->now < combat->throw_until_tick && combat->throw_lead > 0.0f)
	{
		struct unit_definition *definition = unit_definition_get(tick->object->definition_index);
		real speed = definition->unit.grenade_velocity / (real)TICKS_PER_SECOND;
		real ticks, dx, dy, lead;

		if (speed < 0.05f)
			speed = 0.6f;
		ticks = MIN(8.0f + bots_distance2d(bots_tick_eye(tick), aim_point) / speed, 60.0f) * combat->throw_lead;
		dx = target->object.translational_velocity.i * ticks;
		dy = target->object.translational_velocity.j * ticks;
		lead = (real)sqrt(dx * dx + dy * dy);
		if (lead > 3.5f)
		{
			dx *= 3.5f / lead;
			dy *= 3.5f / lead;
		}
		aim_point->x += dx;
		aim_point->y += dy;
	}
}

/* the aim error once settled: RECRUIT's wander that never settles; and,
for a far target, at least the level's floor (pushed out to 0 to 30% past it,
on its side), so that a shot from far off misses about half the time */
static void bots_level_aim_error(struct bots_bot *bot, struct bots_tick *tick)
{
	real *errors[2];
	real const scale[2] = { 1.0f, 0.6f };
	real floor = bots_combat_range_error_floor(bot->profile.skill, tick->target_distance) * BOTS_DEGREES;
	short index;

	errors[0] = &bot->aim_error_yaw;
	errors[1] = &bot->aim_error_pitch;
	for (index = 0; index < 2; index++)
	{
		real random = bots_random_signed(bot);

		*errors[index] = bots_combat_aim_wander(bot->profile.skill, *errors[index] / BOTS_DEGREES, scale[index], random) *
			BOTS_DEGREES;
		if (floor > 0.0f && (real)fabs(*errors[index]) < floor * scale[index])
		{
			real sign = *errors[index] > 0.0f ? 1.0f : *errors[index] < 0.0f ? -1.0f : random < 0.0f ? -1.0f : 1.0f;

			*errors[index] = sign * floor * scale[index] * (1.0f + 0.3f * (real)fabs(random));
		}
	}
}

/* whether the bot may fire now (NORMAL and up): the sniper rifle zoomed at
a target past 6 units, zoomed again a moment after a hit took the zoom (and
not fired past 10 units meanwhile); the pistol paced at range, its shot held
while its error is over the level's limit, a few ticks at most */
static boolean bots_level_may_fire(struct bots_bot *bot, struct bots_tick *tick)
{
	struct bots_bot_combat *combat = &bot->combat;
	struct bots_weapon_kind const *kind;
	short skill = bot->profile.skill;

	/* (a vehicle's gun: as ever) */
	if (tick->weapon_index == NONE || tick->object->object.parent_object_index != NONE)
		return TRUE;
	kind = bots_weapon_kind(weapon_get(tick->weapon_index)->definition_index);
	if (kind->sniper)
	{
		boolean zooms = bots_combat_zooms(skill, tick->target_distance);

		/* (hurt while zoomed: the game took the zoom) */
		if (bot->hurt_tick == tick->now && tick->unit->unit.current_zoom_level == NONE &&
			tick->now >= combat->rescope_until_tick && zooms)
		{
			combat->rescope_until_tick = tick->now + bots_combat_rescope_ticks(skill, bots_random_real(bot));
			bots_globals.combat_counts[_bots_combat_count_rescopes]++;
		}
		if (tick->now < combat->rescope_until_tick)
			return (boolean)!bots_combat_unzoomed_holds_fire(tick->target_distance);
		if (zooms)
			tick->action->desired_zoom_level = 0;
		return TRUE;
	}
	if (kind->precision && kind->blooms &&
		bots_combat_pace_holds(skill, tick->target_distance, weapon_get(tick->weapon_index)->weapon.triggers[0].error,
			combat->pace_since_tick == NONE ? 0 : tick->now - combat->pace_since_tick))
	{
		if (combat->pace_since_tick == NONE)
		{
			combat->pace_since_tick = tick->now;
			bots_globals.combat_counts[_bots_combat_count_paced]++;
		}
		return FALSE;
	}
	combat->pace_since_tick = NONE;
	return TRUE;
}

/* whether a unit has its back to a point (it faces more than 114.6 degrees
away from it) */
static boolean bots_unit_back_turned(long unit_index, real_point3d const *point)
{
	struct object_datum *object = (struct object_datum *)object_try_and_get_and_verify_type(unit_index, _object_mask_unit);
	real dx, dy, length, forward_length;

	if (!object)
		return FALSE;
	dx = point->x - object->object.position.x;
	dy = point->y - object->object.position.y;
	length = (real)sqrt(dx * dx + dy * dy);
	forward_length = (real)sqrt(object->object.forward.i * object->object.forward.i +
		object->object.forward.j * object->object.forward.j);
	if (length < 0.01f || forward_length < 0.01f)
		return FALSE;
	return (dx * object->object.forward.i + dy * object->object.forward.j) / (length * forward_length) < -0.416f;
}

/* whether the bot strikes now: within the level's reach it waits the
level's moment first; a blow it chose not to strike is asked again a second
later */
static boolean bots_level_melee(struct bots_bot *bot, struct bots_tick *tick, long target_unit_index)
{
	struct bots_bot_combat *combat = &bot->combat;
	int choice = bots_combat_melee(bot->profile.skill, tick->target_distance, tick->now >= bot->melee_tick,
		bots_unit_back_turned(target_unit_index, &tick->position), object_get(target_unit_index)->object.shield_vitality,
		combat->charging, bots_random_real(bot));

	if (choice == 0)
	{
		combat->melee_ready_tick = NONE;
		return FALSE;
	}
	if (combat->melee_ready_tick == NONE)
		combat->melee_ready_tick = tick->now + bots_combat_melee_delay(bot->profile.skill, bots_random_real(bot));
	if (tick->now < combat->melee_ready_tick)
		return FALSE;
	combat->melee_ready_tick = NONE;
	if (choice == 2)
	{
		bot->melee_tick = tick->now + TICKS_PER_SECOND;
		bots_globals.combat_counts[_bots_combat_count_blows_held]++;
		return FALSE;
	}
	bots_globals.combat_counts[_bots_combat_count_blows]++;
	return TRUE;
}

/* a grenade by the level's rules (the throw's aim leads the target's path
while it is in the hand, from HEROIC) */
static void bots_level_grenade(struct bots_bot *bot, struct bots_tick *tick, long reaction_ticks)
{
	struct bots_bot_combat *combat = &bot->combat;
	short skill = bot->profile.skill;
	long now = tick->now;
	boolean has = tick->unit->unit.grenade_counts[0] > 0 || tick->unit->unit.grenade_counts[1] > 0;

	if (now < bot->grenade_tick || !has)
		return;
	if (!bots_combat_grenade(skill, tick->target_distance, has, now - bot->target_first_seen_tick >= reaction_ticks,
		now - bot->target_first_seen_tick < TICKS_PER_SECOND * 3 / 2,
		bots_combat_about_to_die(tick->object->object.shield_vitality, tick->object->object.body_vitality),
		bots_skills[skill].grenade_chance, bots_random_real(bot)))
	{
		return;
	}
	tick->flags |= FLAG(_unit_control_throw_grenade_bit);
	if (tick->unit->unit.grenade_counts[(long)tick->unit->unit.current_grenade_index] <= 0)
		tick->action->desired_grenade_index = tick->unit->unit.grenade_counts[0] > 0 ? 0 : 1;
	bot->grenade_tick = now + bots_combat_grenade_cooldown(skill, bots_random_real(bot));
	combat->throw_until_tick = now + TICKS_PER_SECOND / 2;
	combat->throw_lead = bots_combat_grenade_lead(skill);
	bots_globals.combat_counts[_bots_combat_count_grenades]++;
}

/* a place to back off to: a roaming place 5 to 35 units off, not towards the
target, the furthest from the target for the least way; FALSE for none */
static boolean bots_level_retreat_goal(struct bots_bot *bot, struct bots_tick *tick, real_point3d const *target)
{
	long index, best = NONE;
	real best_score = -REAL_MAX;
	real tx = target->x - tick->position.x, ty = target->y - tick->position.y;
	real target_distance = (real)sqrt(tx * tx + ty * ty);

	for (index = 0; index < bots_globals.roam_point_count; index++)
	{
		real_point3d const *point = &bots_globals.roam_points[index];
		real px = point->x - tick->position.x, py = point->y - tick->position.y;
		real distance = (real)sqrt(px * px + py * py);
		real score;

		if (distance < 5.0f || distance > 35.0f || bots_globals.roam_surfaces[index] == NONE)
			continue;
		if (target_distance > 0.01f && (px * tx + py * ty) > 0.3f * distance * target_distance)
			continue;
		score = bots_distance2d(point, target) - 0.6f * distance;
		if (score > best_score)
		{
			best_score = score;
			best = index;
		}
	}
	if (best == NONE)
		return FALSE;
	bot->roam_index = (short)best;
	bot->hunting = FALSE;
	bots_set_goal(bot, _bots_goal_roam, &bots_globals.roam_points[best], bots_globals.roam_surfaces[best]);
	return TRUE;
}

/* the fight's moves by level, after the strafe: halts (it stands, keeping
its weapon's distance, at a fight's start and now and then at a new strafe),
a retreat along a path when losing (still shooting; not back to where the
enemy was), a run in to finish a won fight, a step in for a blow, jumps at
the level's rhythm */
static void bots_level_move(struct bots_bot *bot, struct bots_tick *tick, struct object_datum *target)
{
	struct bots_bot_combat *combat = &bot->combat;
	short skill = bot->profile.skill;
	long now = tick->now;
	real_point3d const *target_point = &target->object.bounding_sphere_center;
	real to_target = (real)atan2(target_point->y - tick->position.y, target_point->x - tick->position.x);
	real own_shield = tick->object->object.shield_vitality;
	real target_shield = target->object.shield_vitality;
	real confidence = bots_combat_confidence(own_shield, tick->object->object.body_vitality, target_shield,
		target->object.body_vitality, tick->target_distance, bots_weapon_range(tick->weapon_index),
		tick->weapon_index != NONE ? bots_weapon_loaded(tick->weapon_index) : 1.0f,
		tick->object->object.bounding_sphere_center.z - target_point->z);
	boolean new_fight = now - combat->last_fight_tick > TICKS_PER_SECOND * 2 || now < combat->last_fight_tick;
	int started, ended;
	boolean retreating = bots_combat_retreat_step(&combat->retreat, skill, confidence, own_shield, now,
		bots_combat_hash01((unsigned long)bot->player_index, (unsigned long)combat->retreat.next_allowed), &started, &ended);

	if (new_fight)
	{
		combat->halt_until_tick = bot->target_first_seen_tick + bots_combat_initial_halt_ticks(skill,
			bots_combat_hash01((unsigned long)bot->player_index ^ 0x27D4EB2DUL, (unsigned long)bot->target_first_seen_tick));
		combat->next_jump_tick = now + bots_combat_jump_delay(skill, bots_random_real(bot));
		combat->last_strafe_until_tick = bot->strafe_until_tick;
	}
	else if (bot->strafe_until_tick != combat->last_strafe_until_tick)
	{
		long halt = bots_combat_halt_ticks(skill, bots_random_real(bot), bots_random_real(bot));

		combat->last_strafe_until_tick = bot->strafe_until_tick;
		if (halt > 0)
		{
			combat->halt_until_tick = now + halt;
			bots_globals.combat_counts[_bots_combat_count_halts]++;
		}
	}
	combat->last_fight_tick = now;
	if (now < combat->halt_until_tick)
	{
		real range = bots_weapon_range(tick->weapon_index);

		if (tick->target_distance > range * 1.3f)
			tick->move_direction = to_target;
		else if (tick->target_distance < range * 0.6f)
			tick->move_direction = to_target + BOTS_PI;
		else
			tick->moving = FALSE;
	}
	if (started)
	{
		bots_level_retreat_goal(bot, tick, target_point);
		bots_globals.combat_counts[_bots_combat_count_retreats]++;
	}
	if (ended)
		bot->goal_kind = _bots_goal_none;
	if (retreating)
	{
		real direction;

		bot->target_last_surface_index = NONE;
		bot->item_check_tick = MAX(bot->item_check_tick, now + TICKS_PER_SECOND);
		if (bot->goal_kind == _bots_goal_roam &&
			bots_follow_path(bot, tick->player->unit_index, &tick->position, &direction))
		{
			tick->move_direction = direction;
		}
		else
			tick->move_direction = to_target + BOTS_PI + bot->strafe_sign * 0.5f;
		tick->moving = TRUE;
		tick->throttle_scale = 1.0f;
		combat->charging = FALSE;
	}
	else
	{
		boolean charge = bots_combat_charge(skill, confidence, target_shield, target->object.body_vitality,
			tick->target_distance);

		if (charge)
		{
			tick->move_direction = to_target + bot->strafe_sign * 0.25f;
			tick->moving = TRUE;
			tick->throttle_scale = 1.0f;
			if (!combat->charging)
				bots_globals.combat_counts[_bots_combat_count_charges]++;
		}
		else if (bots_combat_step_in(skill, tick->target_distance, target_shield))
		{
			tick->move_direction = to_target + bot->strafe_sign * 0.3f;
			tick->moving = TRUE;
			tick->throttle_scale = 1.0f;
		}
		combat->charging = charge;
	}
	if (combat->next_jump_tick > 0 && now >= combat->next_jump_tick)
	{
		long delay = bots_combat_jump_delay(skill, bots_random_real(bot));

		combat->next_jump_tick = delay > 0 ? now + delay : 0;
		if (delay > 0)
		{
			tick->flags |= FLAG(_unit_control_jump_bit);
			bots_globals.combat_counts[_bots_combat_count_jumps]++;
		}
	}
}

/* out of a fight (VETERAN and up): once a lost contact, a grenade at the
spot its target was last seen, 0.6 to 3 s after, the spot 5 to 20 units off
and in its line of sight; the aim held on the spot for the throw (half a
second) */
static void bots_level_prenade(struct bots_bot *bot, struct bots_tick *tick)
{
	struct bots_bot_combat *combat = &bot->combat;
	short skill = bot->profile.skill;
	boolean has = tick->unit->unit.grenade_counts[0] > 0 || tick->unit->unit.grenade_counts[1] > 0;
	struct player_datum *target;
	real_point3d point;
	real seconds, distance;

	if (tick->now < combat->prenade_until_tick)
	{
		real_point3d const *eye = bots_tick_eye(tick);
		real dx = combat->prenade_point.x - eye->x, dy = combat->prenade_point.y - eye->y;
		real turn = bots_skills[skill].turn_degrees_per_tick * BOTS_DEGREES;
		real yaw_off = bots_angle_difference((real)atan2(dy, dx), bot->yaw);
		real pitch_off = (real)atan2(combat->prenade_point.z - eye->z, (real)sqrt(dx * dx + dy * dy)) - bot->pitch;

		bot->yaw += PIN(yaw_off, -turn, turn);
		bot->pitch += PIN(pitch_off, -turn, turn);
		/* (the throw once the aim is on the spot) */
		if (combat->prenade_until_tick - tick->now > 1 && (real)fabs(yaw_off) < 0.1f && (real)fabs(pitch_off) < 0.1f)
		{
			tick->flags |= FLAG(_unit_control_throw_grenade_bit);
			if (tick->unit->unit.grenade_counts[(long)tick->unit->unit.current_grenade_index] <= 0)
				tick->action->desired_grenade_index = tick->unit->unit.grenade_counts[0] > 0 ? 0 : 1;
			combat->prenade_until_tick = tick->now + 1;
		}
		return;
	}
	if (skill < _bots_skill_veteran || bot->target_player_index == NONE || bot->target_last_surface_index == NONE ||
		combat->prenade_seen_tick == bot->target_seen_tick || !has || tick->now < bot->grenade_tick ||
		tick->object->object.parent_object_index != NONE)
	{
		return;
	}
	seconds = (real)(tick->now - bot->target_seen_tick) / TICKS_PER_SECOND;
	if (seconds < 0.6f)
		return;
	combat->prenade_seen_tick = bot->target_seen_tick;
	/* (about the target's middle where it stood) */
	point = bot->target_last_position;
	point.z += 0.3f;
	distance = bots_distance3d(bots_tick_eye(tick), &point);
	target = player_try_and_get(bot->target_player_index);
	if (!bots_combat_prenade(skill, seconds, distance, has, bots_random_real(bot)) ||
		!bots_line_of_sight(tick->player->unit_index, bots_tick_eye(tick), &point,
			target && target->unit_index != NONE ? target->unit_index : NONE))
	{
		return;
	}
	combat->prenade_point = point;
	combat->prenade_until_tick = tick->now + TICKS_PER_SECOND / 2;
	bot->grenade_tick = tick->now + bots_combat_grenade_cooldown(skill, bots_random_real(bot));
	bots_globals.combat_counts[_bots_combat_count_prenades]++;
}

/* looking for enemies (every third tick, the bots in turn), and forgetting
the one gone a while */
static void bots_bot_look(struct bots_bot *bot, struct bots_tick *tick)
{
	/* (the tour meets nobody) */
	if (bots_globals.tour)
		return;
	/* (a look the budget has no room for waits for the next tick) */
	if (tick->now >= bot->next_look_tick && bots_budget_over(&bots_globals.budget))
		bots_budget_note_deferred(&bots_globals.budget);
	else if (tick->now >= bot->next_look_tick)
	{
		long target = bots_find_target(bot, tick->player, bots_tick_eye(tick));
		long interval = 3;

		/* (3 to 5 ticks apart: never a metronome; twice that once the budget has had to shed load) */
		interval += bots_combat_look_jitter(bots_combat_hash01((unsigned long)bot->player_index ^ 0x5BD1E995UL,
			(unsigned long)tick->now));
		bot->next_look_tick = tick->now + interval * bots_budget_look_scale(&bots_globals.budget);
		if (target != NONE)
		{
			struct player_datum *target_player = player_get(target);
			struct object_datum *target_object = object_get(target_player->unit_index);
			real_point3d ground;
			long surface_index;

			/* (a new target: the aim's error as it first turns to it) */
			if (target != bot->target_player_index || tick->now - bot->target_seen_tick > TICKS_PER_SECOND * 2)
			{
				real error = tick->skill->aim_error_degrees * BOTS_DEGREES;

				bot->target_first_seen_tick = tick->now;
				bot->aim_error_yaw = bots_random_signed(bot) * error;
				bot->aim_error_pitch = bots_random_signed(bot) * error * 0.6f;
			}
			bot->target_player_index = target;
			bot->target_seen_tick = tick->now;
			bot->target_last_position = target_object->object.position;
			surface_index = bots_unit_surface(target_player->unit_index, &ground);
			if (surface_index != NONE)
				bot->target_last_surface_index = surface_index;
		}
	}
	if (bot->target_player_index != NONE)
	{
		struct player_datum *target_player = player_try_and_get(bot->target_player_index);

		if (!target_player || target_player->unit_index == NONE ||
			tick->now - bot->target_seen_tick > TICKS_PER_SECOND * 10)
		{
			bot->target_player_index = NONE;
		}
	}
}

/* fighting the target seen in the last half second: aim (turning at its
speed, leading, an error that settles), fire, a grenade, a blow, strafing */
static void bots_bot_fight(struct bots_bot *bot, struct bots_tick *tick)
{
	struct bots_skill_definition const *skill = tick->skill;
	struct player_datum *target_player = player_get(bot->target_player_index);
	struct object_datum *target_object = object_get(target_player->unit_index);
	real_point3d aim_point = target_object->object.bounding_sphere_center;
	long reaction_ticks = (long)(skill->reaction_seconds * TICKS_PER_SECOND);
	long now = tick->now;
	real desired_yaw, desired_pitch, dx, dy, dz, horizontal, turn, yaw_off, pitch_off, cone;
	real settle = (real)exp(-1.0f / (skill->aim_settle_seconds * TICKS_PER_SECOND) * 1.1f);

	tick->fighting = TRUE;
	/* (a reaction of 0.85 to 1.35 of the level's, another each sighting; the
	aim trailing the target at the lowest levels, at the head or ahead of a
	throw) */
	reaction_ticks = bots_combat_reaction_ticks(bot->profile.skill, reaction_ticks,
		bots_combat_hash01((unsigned long)bot->player_index, (unsigned long)bot->target_first_seen_tick));
	bots_level_trail(bot, now, &aim_point);
	bots_level_aim_point(bot, tick, target_player->unit_index, &aim_point);
	bots_tick_eye(tick);
	tick->target_distance = bots_distance3d(&tick->eye, &aim_point);
	/* (leading a moving target by the projectile's flight) */
	if (tick->weapon_index != NONE && skill->lead > 0.0f)
	{
		real speed = bots_projectile_speed(tick->weapon_index);

		if (speed > 0.01f)
		{
			real ticks = MIN(tick->target_distance / speed, 30.0f);

			aim_point.x += target_object->object.translational_velocity.i * ticks * skill->lead;
			aim_point.y += target_object->object.translational_velocity.j * ticks * skill->lead;
			aim_point.z += target_object->object.translational_velocity.k * ticks * skill->lead;
		}
	}
	dx = aim_point.x - tick->eye.x;
	dy = aim_point.y - tick->eye.y;
	dz = aim_point.z - tick->eye.z;
	horizontal = (real)sqrt(dx * dx + dy * dy);
	/* the aim error settles while the target stays in sight, with a tremble */
	bot->aim_error_yaw = bot->aim_error_yaw * settle +
		bots_random_signed(bot) * skill->aim_error_degrees * 0.04f * BOTS_DEGREES;
	bot->aim_error_pitch = bot->aim_error_pitch * settle +
		bots_random_signed(bot) * skill->aim_error_degrees * 0.03f * BOTS_DEGREES;
	/* (recruit's wander, the error kept at range) */
	bots_level_aim_error(bot, tick);
	desired_yaw = (real)atan2(dy, dx) + bot->aim_error_yaw;
	desired_pitch = (real)atan2(dz, horizontal) + bot->aim_error_pitch;
	turn = skill->turn_degrees_per_tick * BOTS_DEGREES;
	yaw_off = bots_angle_difference(desired_yaw, bot->yaw);
	pitch_off = desired_pitch - bot->pitch;
	bot->yaw += PIN(yaw_off, -turn, turn);
	bot->pitch += PIN(pitch_off, -turn, turn);

	/* fire: once it has reacted, while its aim is on the target (its size
	at that distance) */
	yaw_off = (real)fabs(bots_angle_difference((real)atan2(dy, dx), bot->yaw));
	pitch_off = (real)fabs((real)atan2(dz, horizontal) - bot->pitch);
	cone = (real)atan(0.35f / MAX(tick->target_distance, 0.5f)) * skill->fire_cone + 0.01f;
	if (!bots_level_may_fire(bot, tick))
		cone = -1.0f;
	if (now - bot->target_first_seen_tick >= reaction_ticks && now - bot->target_seen_tick <= 3 &&
		yaw_off < cone && pitch_off < cone * 1.5f && tick->weapon_index != NONE)
	{
		short tap_ticks = bots_weapon_tap_ticks(tick->weapon_index);

		if (tap_ticks > 0)
		{
			/* (a press a shot, released between: as fast as the weapon fires,
			a little slower for the less skilled) */
			if (now >= bot->burst_until_tick)
			{
				tick->flags |= FLAG(_unit_control_weapon_primary_trigger_bit);
				bot->burst_until_tick = now + tap_ticks + (long)(bots_random(bot) % (1 + skill->pause_ticks / 3));
			}
		}
		else if (now < bot->burst_until_tick)
			tick->flags |= FLAG(_unit_control_weapon_primary_trigger_bit);
		else if (now >= bot->pause_until_tick)
		{
			/* (bursts, with pauses between) */
			bot->burst_until_tick = now + skill->burst_ticks + (long)(bots_random(bot) % 4);
			bot->pause_until_tick = bot->burst_until_tick + skill->pause_ticks + (long)(bots_random(bot) % 4);
			tick->flags |= FLAG(_unit_control_weapon_primary_trigger_bit);
		}
	}
	/* a blow close up (the level's reach, its moment and its wait) */
	if (bots_level_melee(bot, tick, target_player->unit_index))
	{
		tick->flags |= FLAG(_unit_control_use_equipment_bit);
		bot->melee_tick = now + bots_combat_melee_cooldown(bot->profile.skill, bots_random_real(bot));
	}
	/* a grenade at middle range, by the level's rules */
	bots_level_grenade(bot, tick, reaction_ticks);
	/* strafing, to a side for a while; nearer or further to keep its
	weapon's distance; a jump now and then */
	if (now >= bot->strafe_until_tick)
	{
		bot->strafe_sign = bots_random_real(bot) < 0.5f ? -1.0f : 1.0f;
		/* (the level's strafes, short jiggles at the top) */
		bot->strafe_until_tick = now + bots_combat_strafe_ticks(bot->profile.skill, bots_random_real(bot));
	}
	{
		real to_target = (real)atan2(dy, dx);
		real range = bots_weapon_range(tick->weapon_index);
		real forward = tick->target_distance > range * 1.3f ? 0.8f : tick->target_distance < range * 0.6f ? -0.5f : 0.0f;
		real side = bot->strafe_sign * bots_combat_strafe_strength(bot->profile.skill);

		tick->move_direction = (real)atan2((real)sin(to_target) * forward + (real)sin(to_target + BOTS_PI * 0.5f) * side,
			(real)cos(to_target) * forward + (real)cos(to_target + BOTS_PI * 0.5f) * side);
		tick->moving = TRUE;
		tick->throttle_scale = 1.0f;
	}
	/* crouches, halts, a retreat, a charge, a step in, jumps, by level */
	if (bots_combat_crouch_chance(bot->profile.skill) > 0.0f &&
		bots_combat_crouch_step(&bot->combat.crouch, bot->profile.skill, now, bots_random_real(bot)))
	{
		tick->flags |= FLAG(_unit_control_crouch_modifier_bit);
	}
	bots_level_move(bot, tick, target_object);
	/* a strafe into a wall: the bot moves and does not get anywhere; it strafes the other way (the graph's bots) */
	if ((bots_globals.nav_tweaks & BOTS_NAV_TWEAK_STRAFE_FLIP) && bots_nav_usable())
	{
		real moved = bots_distance2d(&tick->position, &bot->fight_prev_position);

		bot->fight_prev_position = tick->position;
		if (tick->now - bot->fight_prev_tick > 1 || !tick->moving)
			bot->fight_slow_ticks = 0;
		else if (moved < BOTS_NAV_BARELY_MOVED)
			bot->fight_slow_ticks++;
		else if (moved > 2.0f * BOTS_NAV_BARELY_MOVED)
			bot->fight_slow_ticks = 0;
		bot->fight_prev_tick = tick->now;
		if (bot->fight_slow_ticks >= BOTS_FIGHT_BLOCKED_TICKS)
		{
			bot->fight_slow_ticks = 0;
			bot->strafe_sign = -bot->strafe_sign;
			bots_globals.fight_flips++;
		}
	}
}

/* walking to the goal, looking where it goes; there, a look about (and the
last bit straight onto an item); a reload when no one is about */
static void bots_bot_walk(struct bots_bot *bot, struct bots_tick *tick)
{
	real direction;

	if (bots_follow_path(bot, tick->player->unit_index, &tick->position, &direction))
	{
		real turn = 12.0f * BOTS_DEGREES;

		bots_nav_apply(bot, tick);
		real off = bots_angle_difference(direction, bot->yaw);

		tick->move_direction = direction;
		tick->moving = TRUE;
		bot->yaw += PIN(off, -turn, turn);
		bot->pitch += PIN(-bot->pitch, -0.05f, 0.05f);
		if (bot->climb_until_tick && tick->now < bot->climb_until_tick && (bot->climb_until_tick - tick->now) % 20 == 0)
			tick->flags |= FLAG(_unit_control_jump_bit);
	}
	else
	{
		bot->yaw += 2.0f * BOTS_DEGREES;
		if (bot->goal_kind == _bots_goal_roam || bot->goal_kind == _bots_goal_enemy)
			bot->goal_kind = _bots_goal_none;
		/* (at an item: on to it) */
		if (bot->goal_kind == _bots_goal_item)
		{
			struct object_datum *item = object_try_and_get_and_verify_type(bot->item_index, _object_mask_item);

			if (item && bots_distance2d(&item->object.position, &tick->position) > 0.3f)
			{
				tick->move_direction = (real)atan2(item->object.position.y - tick->position.y,
					item->object.position.x - tick->position.x);
				tick->moving = TRUE;
				tick->throttle_scale = 0.6f;
			}
		}
	}
	if (tick->weapon_index != NONE && bots_weapon_loaded(tick->weapon_index) < 0.4f &&
		tick->now - bot->target_seen_tick > TICKS_PER_SECOND * 2)
	{
		tick->flags |= FLAG(_unit_control_weapon_reload_bit);
	}
}

/* the bots in each other's way (a team's bots leaving their base by the same
door, Longest's, jammed in it a whole game, each going round the others):
the one with the higher index steps back from the other, while the other
moves, so that the first goes through */
static void bots_bot_yield(long bot_index, struct bots_tick *tick)
{
	long index;

	if (!tick->moving || tick->fighting || tick->object->object.parent_object_index != NONE)
		return;
	for (index = 0; index < bot_index; index++)
	{
		struct bots_bot *other = &bots_globals.bots[index];
		struct object_datum *object;
		real dx, dy, distance, velocity;

		if (other->player_index == NONE || other->unit_index == NONE)
			continue;
		object = (struct object_datum *)object_try_and_get_and_verify_type(other->unit_index, _object_mask_unit);
		if (!object || object->object.parent_object_index != NONE)
			continue;
		dx = object->object.position.x - tick->position.x;
		dy = object->object.position.y - tick->position.y;
		distance = (real)sqrt(dx * dx + dy * dy);
		velocity = (real)sqrt(object->object.translational_velocity.i * object->object.translational_velocity.i +
			object->object.translational_velocity.j * object->object.translational_velocity.j);
		if (distance > 1.1f || distance < 0.01f || fabs(object->object.position.z - tick->position.z) > 1.5f ||
			velocity < 0.02f ||
			(dx * (real)cos(tick->move_direction) + dy * (real)sin(tick->move_direction)) < 0.3f * distance)
		{
			continue;
		}
		tick->move_direction = (real)atan2(-dy, -dx);
		tick->throttle_scale = 0.6f;
		return;
	}
}

/* weapons: the other one when this one is empty; at the weapon it fetches,
once the game offers it (the player's action, as the HUD shows it), the
action button held, as a player holds it to swap a weapon (the swap:
players_update_before_game's player_handle_weapon_swap), let go now and
then, so that a second swap can follow a first */
static void bots_bot_weapons(struct bots_bot *bot, struct bots_tick *tick)
{
	if (tick->weapon_index != NONE && bots_weapon_empty(tick->weapon_index))
	{
		short slot;

		for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
		{
			long other = tick->unit->unit.weapon_object_indices[slot];

			if (slot != tick->unit->unit.current_weapon_index && other != NONE && !bots_weapon_empty(other))
			{
				tick->action->desired_weapon_index = slot;
				break;
			}
		}
	}
	/* (the flag or the ball the objective leads to: the action button, held for a moment, when the game offers it) */
	if (bot->goal_kind == _bots_goal_objective && (bot->objective_kind == _bots_objective_grab || bot->objective_kind == _bots_objective_return) &&
		tick->player->action_object_index != NONE)
	{
		struct weapon_datum *offered = (struct weapon_datum *)object_try_and_get_and_verify_type(tick->player->action_object_index, _object_mask_weapon);

		if (offered && bots_objective_item_kind(offered) &&
			(tick->player->action_result == _player_action_result_pickup_weapon || tick->player->action_result == _player_action_result_swap_for_weapon ||
				tick->player->action_result == _player_action_result_add_weapon_to_inventory))
		{
			if (tick->now - bot->item_press_tick > TICKS_PER_SECOND)
				bot->item_press_tick = tick->now;
			if (tick->now - bot->item_press_tick < TICKS_PER_SECOND / 2)
				tick->flags |= FLAG(_unit_control_swap_weapons_bit);
		}
	}
	if (bot->goal_kind == _bots_goal_item)
	{
		struct object_datum *item = object_try_and_get_and_verify_type(bot->item_index, _object_mask_weapon);

		if (item && item->object.parent_object_index == NONE &&
			bots_distance2d(&item->object.position, &tick->position) < 0.8f &&
			fabs(item->object.position.z - tick->position.z) < 1.2f)
		{
			if (tick->now - bot->item_press_tick > TICKS_PER_SECOND)
				bot->item_press_tick = tick->now;
			if (bot->item_first_press_tick < bot->goal_tick)
				bot->item_first_press_tick = tick->now;
			if (tick->now - bot->item_press_tick < TICKS_PER_SECOND / 2 &&
				tick->player->action_object_index == bot->item_index &&
				(tick->player->action_result == _player_action_result_pickup_weapon ||
					tick->player->action_result == _player_action_result_swap_for_weapon ||
					tick->player->action_result == _player_action_result_add_weapon_to_inventory))
			{
				tick->flags |= FLAG(_unit_control_swap_weapons_bit);
			}
		}
	}
}

/* stuck: not moving for a second while it walks - a jump and a sidestep,
then another path; somewhere else after three */
static void bots_bot_unstick(struct bots_bot *bot, struct bots_tick *tick)
{
	long now = tick->now;

	if (bots_globals.stuck_log_on > 1)
		platform_log("bots: tr %ld %ld %.3f %.3f %.3f d%.0f m%d f%d v%.3f s%d/%d", now, (long)(bot - bots_globals.bots) + 1, tick->position.x, tick->position.y,
			tick->position.z, tick->move_direction / BOTS_DEGREES, (int)tick->moving, (int)tick->fighting, (double)sqrt(tick->object->object.translational_velocity.i *
			tick->object->object.translational_velocity.i + tick->object->object.translational_velocity.j * tick->object->object.translational_velocity.j),
			(int)bot->nav_path.step, (int)bots_nav_path_point_count(&bot->nav_path));
	if (now - bot->stuck_tick >= TICKS_PER_SECOND)
	{
		if (tick->moving && bots_distance2d(&tick->position, &bot->stuck_position) < 0.4f &&
			tick->object->object.parent_object_index == NONE)
		{
			bots_globals.stuck_events++;
			bot->counts.stuck_seconds++;
			bot->stuck_count++;
			if (bots_globals.stuck_log_on)
			{
				struct bots_nav_link const *l = bots_globals.nav && bot->nav_path.valid && bot->nav_path.link_count > 0 ?
					&bots_globals.nav->links[bot->nav_path.links[MIN((long)bot->nav_path.step / 3, (long)bot->nav_path.link_count - 1)]] : NULL;

				platform_log("bots: stuck: tick %ld bot %ld at (%.2f %.2f %.2f) fighting %d goal %d (%.1f %.1f %.1f) nav %d step %d/%d state %d link %d count %d unstick %d escape %d",
					now, (long)(bot - bots_globals.bots) + 1, tick->position.x, tick->position.y, tick->position.z, (int)tick->fighting, (int)bot->goal_kind,
					bot->goal_point.x, bot->goal_point.y, bot->goal_point.z, (int)bot->nav_path.valid, (int)bot->nav_path.step,
					(int)(bot->nav_path.valid ? bots_nav_path_point_count(&bot->nav_path) : 0), (int)bot->nav_state, l ? (int)l->type : -1,
					(int)bot->stuck_count, (int)(now < bot->unstick_until_tick), (int)(now < bot->escape_until_tick));
				if (bot->nav_path.valid && bots_globals.nav)
				{
					struct bots_nav_point q[3];
					long k;

					for (k = 0; k < 3; k++)
						if (!bots_nav_path_point(bots_globals.nav, &bot->nav_path, bot->nav_path.step + k, &q[k]))
							q[k].p[0] = q[k].p[1] = q[k].p[2] = 0.0f, q[k].kind = 9;
					platform_log("bots: stuck:   points %d (%.2f %.2f %.2f) %d (%.2f %.2f %.2f) %d (%.2f %.2f %.2f) velocity %.3f",
						(int)q[0].kind, q[0].p[0], q[0].p[1], q[0].p[2], (int)q[1].kind, q[1].p[0], q[1].p[1], q[1].p[2], (int)q[2].kind, q[2].p[0], q[2].p[1], q[2].p[2],
						(double)sqrt(tick->object->object.translational_velocity.i * tick->object->object.translational_velocity.i +
							tick->object->object.translational_velocity.j * tick->object->object.translational_velocity.j));
				}
				/* (what is in the way: a sphere at four heights swept 0.5 along the way the bot goes, and the floor under it) */
				{
					static real const heights[4] = { 0.2f, 0.32f, 0.385f, 0.5f };
					unsigned long flags = FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_back_facing_surfaces_bit) |
						FLAG(_collision_test_structure_bit) | FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit) |
						FLAG(_collision_test_objects_machines_bit);
					char text[400];
					int length = 0;
					long h;

					for (h = 0; h < 4; h++)
					{
						real_point3d a = tick->position;
						real_vector3d v;
						struct collision_result c;

						a.z += heights[h];
						v.i = (real)cos(tick->move_direction) * 0.5f;
						v.j = (real)sin(tick->move_direction) * 0.5f;
						v.k = 0.0f;
						if (collision_test_pill(flags, &a, &v, 0.2f, NONE, &c))
							length += snprintf(text + length, sizeof(text) - (size_t)length, " h%.2f:hit(type %d surface %ld at %.2f %.2f %.2f n %.2f %.2f %.2f)", (double)heights[h], (int)c.type,
								c.surface_index, c.point.x, c.point.y, c.point.z, c.plane.n.i, c.plane.n.j, c.plane.n.k);
						else
							length += snprintf(text + length, sizeof(text) - (size_t)length, " h%.2f:clear", (double)heights[h]);
					}
					platform_log("bots: stuck:   ahead (dir %.0f) r0.2 sweeps:%s", tick->move_direction / BOTS_DEGREES, text);
				}
			}
			bot->unstick_until_tick = now + TICKS_PER_SECOND / 2;
			bot->unstick_direction = tick->move_direction + (bots_random_real(bot) < 0.5f ? -1.0f : 1.0f) * BOTS_PI * 0.5f;
			bot->path.valid = FALSE;
			bots_nav_note_stuck(bot);
			if (bot->stuck_count >= 3)
			{
				bots_globals.stuck_giveups++;
				bot->counts.stuck_giveups++;
				bot->goal_kind = _bots_goal_none;
				bot->stuck_count = 0;
				bots_pick_roam_goal(bot, &tick->position);
			}
		}
		else
			bot->stuck_count = 0;
		bot->stuck_position = tick->position;
		bot->stuck_tick = now;
	}
	if (now < bot->unstick_until_tick)
	{
		tick->move_direction = bot->unstick_direction;
		tick->moving = TRUE;
		if (now == bot->unstick_until_tick - TICKS_PER_SECOND / 2)
			tick->flags |= FLAG(_unit_control_jump_bit);
	}
}

/* (debug.bot_trace = <seconds>) each bot's state that often */
static void bots_bot_trace(struct bots_bot *bot, long bot_index, struct bots_tick *tick)
{
	static long trace = -1;
	long now = tick->now;

	if (trace < 0)
		trace = MAX(config_integer("debug.bot_trace"), 0) * TICKS_PER_SECOND;
	if (trace <= 0 || now % trace != bot_index)
		return;
	platform_log("bots: trace %ld bot %ld at (%.1f %.1f %.1f) goal %d (%.1f %.1f %.1f) s%ld path %s %d/%d%s "
		"target %ld seen %ld ago%s stuck %d yaw %.0f throttle %.2f/%.2f flags %lx searches %ld failures %ld",
		now, bot_index + 1, tick->position.x, tick->position.y, tick->position.z, (int)bot->goal_kind,
		bot->goal_point.x, bot->goal_point.y, bot->goal_point.z, bot->goal_surface_index,
		bot->path.valid ? "yes" : "no", (int)bot->path.step_index, (int)bot->path.step_count,
		bot->path.steps_finish_path ? " (all)" : "",
		bot->target_player_index == NONE ? -1L : (long)DATUM_INDEX_TO_ABSOLUTE_INDEX(bot->target_player_index),
		now - bot->target_seen_tick, tick->fighting ? " fighting" : "", (int)bot->stuck_count,
		bot->yaw / BOTS_DEGREES, tick->action->throttle.i, tick->action->throttle.j, tick->action->control_flags,
		bot->searches, bot->path_failures);
	if (tick->weapon_index != NONE)
	{
		struct weapon_datum *weapon = weapon_get(tick->weapon_index);

		platform_log("bots: trace weapon %s loaded %d total %d, aim off %.1f/%.1f degrees, distance %.1f",
			tag_get_name(weapon->definition_index), (int)weapon->weapon.magazines[0].rounds_loaded,
			(int)weapon->weapon.magazines[0].rounds_total, bot->aim_error_yaw / BOTS_DEGREES,
			bot->aim_error_pitch / BOTS_DEGREES, tick->target_distance);
	}
}

static void bots_bot_update(struct bots_bot *bot, long bot_index, struct player_action *action)
{
	struct bots_tick tick;
	real vitality;

	csmemset(&tick, 0, sizeof(tick));
	tick.skill = &bots_skills[bot->profile.skill];
	tick.player = player_get(bot->player_index);
	tick.now = bots_now();
	tick.throttle_scale = 1.0f;
	tick.action = action;
	csmemset(action, 0, sizeof(*action));
	action->desired_weapon_index = NONE;
	action->desired_grenade_index = NONE;
	action->desired_zoom_level = NONE;
	action->desired_facing.yaw = bot->yaw;
	action->desired_facing.pitch = bot->pitch;

	if (tick.player->unit_index == NONE)
	{
		/* (dead: it forgets its fight and its path; the game respawns it) */
		bot->unit_index = NONE;
		bot->target_player_index = NONE;
		bot->path.valid = FALSE;
		bot->nav_path.valid = FALSE;
		bot->nav_state = _bots_nav_state_none;
		bot->nav_goal_area = NONE;
		bot->tour_since_tick = 0;
		bot->goal_kind = _bots_goal_none;
		bot->previous_control_flags = 0;
		return;
	}
	tick.unit = unit_get(tick.player->unit_index);
	tick.object = object_get(tick.player->unit_index);
	tick.position = tick.object->object.position;
	if (bot->unit_index != tick.player->unit_index)
	{
		/* (spawned: it looks the way its unit faces) */
		bot->unit_index = tick.player->unit_index;
		bot->yaw = (real)atan2(tick.object->object.forward.j, tick.object->object.forward.i);
		bot->pitch = 0.0f;
		bot->last_vitality = tick.object->object.body_vitality + tick.object->object.shield_vitality;
		bot->stuck_position = tick.position;
		bot->stuck_tick = tick.now;
		bot->stuck_count = 0;
		bot->goal_kind = _bots_goal_none;
		bot->next_look_tick = tick.now + (bot_index % 3);
		bot->climb_until_tick = 0;
		bot->path_short = FALSE;
		bot->escape_until_tick = 0;
		bot->nav_path.valid = FALSE;
		bot->nav_state = _bots_nav_state_none;
		bot->nav_goal_area = NONE;
		bot->last_position = tick.position;
		bots_level_reset(&bot->combat);
	}
	/* (sent on by a teleporter it walked onto: a new path from where it came
	out) */
	if (bots_distance3d(&tick.position, &bot->last_position) > 3.0f && bot->goal_kind != _bots_goal_none &&
		tick.object->object.parent_object_index == NONE)
	{
		bot->path.valid = FALSE;
		bot->path_tick = tick.now - TICKS_PER_SECOND;
		bot->stuck_position = tick.position;
		/* (the graph's path stands while a teleporter is being stood on: its executor sees the jump) */
		if (bot->nav_state != _bots_nav_state_flight)
			bot->nav_path.valid = FALSE;
	}
	bot->last_position = tick.position;

	/* hurt: it turns to look for who did it */
	vitality = tick.object->object.body_vitality + tick.object->object.shield_vitality;
	if (vitality < bot->last_vitality - 0.01f)
		bot->hurt_tick = tick.now;
	bot->last_vitality = vitality;

	bots_bot_look(bot, &tick);
	tick.weapon_index = bots_current_weapon(tick.unit);
	if (bot->target_player_index != NONE && tick.now - bot->target_seen_tick <= TICKS_PER_SECOND / 2)
		bots_bot_fight(bot, &tick);
	if (tick.fighting)
		bot->nav_fought_tick = tick.now;
	/* the goal, and the path to it (while fighting, only a weapon close by:
	towards it, still facing the enemy) */
	if (!tick.fighting || bot->goal_kind == _bots_goal_item)
		bots_choose_goal(bot, bot->player_index, &tick.position, tick.fighting);
	if (!tick.fighting)
	{
		bots_bot_walk(bot, &tick);
		bots_bot_yield(bot_index, &tick);
		bots_level_prenade(bot, &tick);
	}
	else if (bot->goal_kind == _bots_goal_item)
	{
		real direction;

		if (bots_follow_path(bot, tick.player->unit_index, &tick.position, &direction))
		{
			tick.move_direction = direction;
			bots_nav_apply(bot, &tick);
		}
	}
	bots_bot_weapons(bot, &tick);
	bots_bot_unstick(bot, &tick);

	if (tick.moving)
	{
		real relative = bots_angle_difference(tick.move_direction, bot->yaw);

		action->throttle.i = (real)cos(relative) * tick.throttle_scale;
		action->throttle.j = (real)sin(relative) * tick.throttle_scale;
	}
	bot->pitch = PIN(bot->pitch, -1.4f, 1.4f);
	bot->yaw = bots_angle_difference(bot->yaw, 0.0f);
	action->desired_facing.yaw = bot->yaw;
	action->desired_facing.pitch = bot->pitch;

	/* a press is one tick held and the next released (the jump, the grenade,
	the blow, the reload), as a player's buttons are; the trigger and the
	swap button may be held */
	{
		unsigned long pressed = FLAG(_unit_control_jump_bit) | FLAG(_unit_control_throw_grenade_bit) |
			FLAG(_unit_control_use_equipment_bit) | FLAG(_unit_control_weapon_reload_bit);

		tick.flags &= ~(bot->previous_control_flags & pressed);
		bot->previous_control_flags = tick.flags;
	}
	action->control_flags = tick.flags;
	action->primary_trigger = TEST_FLAG(tick.flags, _unit_control_weapon_primary_trigger_bit) ? 1.0f : 0.0f;
	bots_bot_trace(bot, bot_index, &tick);
}

static void bots_report_cost(void);
static void bots_report_nav(void);

/* the name of the level the bots play at: the one they all play at, "mixed" if they differ */
static char const *bots_report_skill_name(void)
{
	long index;
	short skill = NONE;

	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		struct bots_bot const *bot = &bots_globals.bots[index];

		if (bot->player_index == NONE || !bot->counts.think.samples)
			continue;
		if (skill != NONE && skill != bot->profile.skill)
			return "mixed";
		skill = bot->profile.skill;
	}
	return bots_skills[skill == NONE ? bots_globals.setting_skill : skill].name;
}

/* halo.log's line, a minute into the game and each minute on */
static void bots_report(void)
{
	long now = bots_now();
	long count = 0, index;
	char line[400];
	int length = 0;

	if (bots_globals.last_report_tick != NONE && now - bots_globals.last_report_tick < TICKS_PER_SECOND * 60)
		return;
	bots_globals.last_report_tick = now;
	line[0] = 0;
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		struct bots_bot *bot = &bots_globals.bots[index];
		struct player_datum *player = bot->player_index != NONE ? player_try_and_get(bot->player_index) : NULL;

		if (!player)
			continue;
		count++;
		if (length < (int)sizeof(line) - 40)
			length += snprintf(line + length, sizeof(line) - (size_t)length, " %ld:%d/%d", index + 1,
				(int)player->statistics.kills[0], (int)player->statistics.deaths);
	}
	if (!count)
		return;
	platform_log("bots: tick %ld, %ld bots (%s): searches %ld (%ld failed, %ld short, %ld full, %ld unavoided, "
		"%ld unsmoothed), stuck %ld (goals given up %ld), kills/deaths%s", now, count,
		bots_report_skill_name(), bots_globals.searches, bots_globals.search_failures,
		bots_globals.search_partials, bots_globals.search_overflows, bots_globals.search_unavoided,
		bots_globals.search_unsmoothed, bots_globals.stuck_events, bots_globals.stuck_giveups, line);
	{
		long const *counts = bots_globals.combat_counts;

		platform_log("bots: fights since the map's start: grenades %ld (%ld at a last-seen spot), blows %ld "
			"(%ld held back), head aims %ld, retreats %ld, charges %ld, jumps %ld, halts %ld, shots paced %ld, "
			"zoomed again %ld, high ground picks %ld, enemies out of vertical view %ld",
			counts[_bots_combat_count_grenades] + counts[_bots_combat_count_prenades], counts[_bots_combat_count_prenades],
			counts[_bots_combat_count_blows], counts[_bots_combat_count_blows_held], counts[_bots_combat_count_head_aims],
			counts[_bots_combat_count_retreats], counts[_bots_combat_count_charges], counts[_bots_combat_count_jumps],
			counts[_bots_combat_count_halts], counts[_bots_combat_count_paced], counts[_bots_combat_count_rescopes],
			counts[_bots_combat_count_high_ground], counts[_bots_combat_count_out_of_view]);
	}
	bots_report_cost();
	bots_report_nav();
}

/* the navigation graph's line, a minute into the game and each minute on: the searches by it and its links */
static void bots_report_nav(void)
{
	long const *done = bots_globals.nav_links_done, *failed = bots_globals.nav_links_failed;

	if (!bots_globals.nav)
	{
		if (!bots_globals.nav_reported && bots_globals.nav_enabled)
			platform_log("bots: nav: none in use (%s): the bots walk by the engine's surfaces", bots_nav_load_state() == _bots_nav_load_running ? "still being read" : "see the line above");
		bots_globals.nav_reported = TRUE;
		return;
	}
	platform_log("bots: nav: %s: searches %ld (%ld failed, %ld short, %ld nodes), off the graph %ld, to the engine's surfaces %ld, shortcuts %ld; links done/failed: walk-ups %ld/%ld, jumps %ld/%ld, drops %ld/%ld, teleports %ld/%ld; bad for a bot %ld",
		bots_globals.nav->name, bots_globals.nav_searches, bots_globals.nav_search_failures, bots_globals.nav_search_partials,
		bots_globals.nav_search_nodes, bots_globals.nav_off_graph, bots_globals.nav_fallbacks, bots_globals.nav_shortcuts,
		done[_bots_nav_link_step], failed[_bots_nav_link_step], done[_bots_nav_link_jump], failed[_bots_nav_link_jump],
		done[_bots_nav_link_drop], failed[_bots_nav_link_drop], done[_bots_nav_link_teleport], failed[_bots_nav_link_teleport],
		bots_globals.nav_links_marked_bad);
	platform_log("bots: nav: followed: blocked bots %ld (the surfaces lead a moment), no anchor in sight %ld, paths remade after a fight %ld, flights found bad for all %ld", bots_globals.nav_blocked,
		bots_globals.nav_blind, bots_globals.nav_after_fight, bots_globals.nav_learned_bad);
	if (bots_globals.tour)
	{
		long kind;
		char line[300];
		int length = 0;

		line[0] = 0;
		for (kind = 0; kind < BOTS_TOUR_KINDS; kind++)
			length += snprintf(line + length, sizeof(line) - (size_t)length, " %s %ld/%ld", bots_tour_names[kind], bots_globals.tour_reached[kind],
				bots_globals.tour_reached[kind] + bots_globals.tour_given_up[kind]);
		platform_log("bots: tour: reached/tried:%s", line);
	}
}

/* the cost line, a minute into the game and each minute on (after the bots line and the fight's counts): what the bots cost a tick */
static void bots_report_cost(void)
{
	char line[220];
	int length = 0;
	long index;

	struct bots_budget const *budget = &bots_globals.budget;

	platform_log("bots: cost: all bots a tick mean %ld us, 99%% within %ld, longest %ld (%ld ticks, %ld on a world that "
		"stood still), %ld sight tests, %ld searches", bots_think_mean(&bots_globals.tick_think),
		bots_think_percentile(&bots_globals.tick_think, 99), bots_globals.tick_think.maximum_microseconds,
		bots_globals.tick_think.samples, bots_globals.stale_ticks, bots_globals.rays, bots_globals.searches);
	if (budget->mode == _bots_budget_off)
		platform_log("bots: budget: off (bots.budget_us, bots.budget_credit), at most %d bots", (int)budget->maximum_bots);
	else
		platform_log("bots: budget: %s, cap %ld a tick, step %d of %d, %ld ticks over the cap, %ld left undone, steps %ld up %ld down, "
			"at most %d bots", budget->mode == _bots_budget_clock ? "microseconds" : "credit", budget->cap, (int)budget->level,
			BOTS_BUDGET_TOP_LEVEL, budget->ticks_over_cap, budget->deferred, budget->steps_up, budget->steps_down,
			(int)budget->maximum_bots);
	line[0] = 0;
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		struct bots_bot *bot = &bots_globals.bots[index];

		if (bot->player_index == NONE || !bot->counts.think.samples)
			continue;
		if (length < (int)sizeof(line) - 30)
			length += snprintf(line + length, sizeof(line) - (size_t)length, " %ld:%ld/%ld", index + 1,
				bots_think_mean(&bot->counts.think), bots_think_percentile(&bot->counts.think, 99));
	}
	platform_log("bots: cost per bot, us a tick (mean/99%%):%s", line);
}

/* the block the headless runs read at the end of a game (tools/harness/bots_sim.py): each bot's frags, deaths, time stuck, path
failures and think time, then the totals; once a game */
static void bots_report_sim(char const *why)
{
	struct bots_think_stats think;
	long frags = 0, deaths = 0, stuck = 0, searches = 0, failed = 0, partial = 0, rays = 0, count = 0, index;

	if (bots_globals.sim_reported || !bots_globals.ticks)
		return;
	bots_globals.sim_reported = TRUE;
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		if (bots_globals.bots[index].player_index != NONE && bots_globals.bots[index].counts.think.samples)
			count++;
	}
	platform_log("bots-sim: begin (%s) map %s ticks %ld bots %ld ms_per_tick %.3f", why, bots_globals.map_name,
		bots_globals.ticks, count, bots_globals.ticks > 1 ? (double)(bots_clock_microseconds() -
		bots_globals.first_tick_microseconds) / 1000.0 / (double)bots_globals.ticks : 0.0);
	bots_think_reset(&think);
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		struct bots_bot const *bot = &bots_globals.bots[index];
		struct bots_bot_counts const *counts = &bot->counts;

		if (bot->player_index == NONE || !counts->think.samples)
			continue;
		platform_log("bots-sim: bot %ld %s frags %ld deaths %ld stuck_s %ld giveups %ld searches %ld failed %ld short %ld rays %ld "
			"think_us %ld/%ld/%ld", index + 1, bots_skills[bot->profile.skill].name, counts->kills, counts->deaths,
			counts->stuck_seconds, counts->stuck_giveups, bot->searches, counts->search_failures, counts->search_partials,
			counts->rays, bots_think_mean(&counts->think), bots_think_percentile(&counts->think, 99),
			counts->think.maximum_microseconds);
		frags += counts->kills;
		deaths += counts->deaths;
		stuck += counts->stuck_seconds;
		searches += bot->searches;
		failed += counts->search_failures;
		partial += counts->search_partials;
		rays += counts->rays;
		bots_think_add(&think, &counts->think);
	}
	platform_log("bots-sim: total frags %ld deaths %ld stuck_s %ld searches %ld failed %ld short %ld rays %ld", frags, deaths,
		stuck, searches, failed, partial, rays);
	platform_log("bots-sim: nav %s searches %ld failed %ld short %ld off_graph %ld engine_fallbacks %ld shortcuts %ld links_done walk %ld step %ld jump %ld drop %ld teleport %ld links_failed jump %ld drop %ld teleport %ld bad %ld",
		bots_globals.nav ? bots_globals.nav->name : "none", bots_globals.nav_searches, bots_globals.nav_search_failures, bots_globals.nav_search_partials,
		bots_globals.nav_off_graph, bots_globals.nav_fallbacks, bots_globals.nav_shortcuts, bots_globals.nav_links_done[_bots_nav_link_walk],
		bots_globals.nav_links_done[_bots_nav_link_step], bots_globals.nav_links_done[_bots_nav_link_jump], bots_globals.nav_links_done[_bots_nav_link_drop],
		bots_globals.nav_links_done[_bots_nav_link_teleport], bots_globals.nav_links_failed[_bots_nav_link_jump],
		bots_globals.nav_links_failed[_bots_nav_link_drop], bots_globals.nav_links_failed[_bots_nav_link_teleport], bots_globals.nav_links_marked_bad);
	platform_log("bots-sim: navfollow blocked %ld blind %ld after_fight %ld strafe_flips %ld learned_bad %ld", bots_globals.nav_blocked, bots_globals.nav_blind, bots_globals.nav_after_fight, bots_globals.fight_flips, bots_globals.nav_learned_bad);
	platform_log("bots-sim: objectives enabled %d goals none %ld grab %ld capture %ld wait %ld hunt %ld escort %ld defend %ld return %ld hold %ld stale %ld timed_items %ld trips %ld",
		(int)bots_globals.objectives_enabled, bots_globals.objective_counts[0], bots_globals.objective_counts[_bots_objective_grab],
		bots_globals.objective_counts[_bots_objective_capture], bots_globals.objective_counts[_bots_objective_wait], bots_globals.objective_counts[_bots_objective_hunt],
		bots_globals.objective_counts[_bots_objective_escort], bots_globals.objective_counts[_bots_objective_defend], bots_globals.objective_counts[_bots_objective_return],
		bots_globals.objective_counts[_bots_objective_hold], bots_globals.objective_stale, bots_globals.timed_item_count, bots_globals.timed_item_trips);
	if (bots_globals.tour)
		platform_log("bots-sim: tour players %ld/%ld flags %ld/%ld starts %ld/%ld items %ld/%ld reached/given_up, ticks to reach players %ld flags %ld starts %ld items %ld",
			bots_globals.tour_reached[0], bots_globals.tour_given_up[0], bots_globals.tour_reached[1], bots_globals.tour_given_up[1],
			bots_globals.tour_reached[2], bots_globals.tour_given_up[2], bots_globals.tour_reached[3], bots_globals.tour_given_up[3],
			bots_globals.tour_reached[0] ? bots_globals.tour_ticks[0] / bots_globals.tour_reached[0] : 0L,
			bots_globals.tour_reached[1] ? bots_globals.tour_ticks[1] / bots_globals.tour_reached[1] : 0L,
			bots_globals.tour_reached[2] ? bots_globals.tour_ticks[2] / bots_globals.tour_reached[2] : 0L,
			bots_globals.tour_reached[3] ? bots_globals.tour_ticks[3] / bots_globals.tour_reached[3] : 0L);
	platform_log("bots-sim: tick_us mean %ld p99 %ld max %ld ticks %ld (the bots' think time in a tick; per bot: think_us "
		"is mean/p99/max)", bots_think_mean(&bots_globals.tick_think), bots_think_percentile(&bots_globals.tick_think, 99),
		bots_globals.tick_think.maximum_microseconds, bots_globals.tick_think.samples);
	platform_log("bots-sim: end");
}

/* the bots in the farther half of them from the players, marked (the top step of the shedding holds their controls): by the
distance of each to the nearest player who plays on this machine */
static void bots_mark_far_bots(void)
{
	real distances[BOTS_MAXIMUM], sorted[BOTS_MAXIMUM];
	boolean present[BOTS_MAXIMUM];
	struct data_iterator iterator;
	struct player_datum *player;
	long count = 0, index, other;

	csmemset(present, 0, sizeof(present));
	csmemset(bots_globals.far_from_humans, 0, sizeof(bots_globals.far_from_humans));
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long bot_index = player->network_player_data.machine_index - BOTS_FIRST_MACHINE;
		real_point3d position;
		real nearest = REAL_MAX;
		struct data_iterator humans;
		struct player_datum *human;

		if (player->local_player_index != NONE || !bots_machine_is_bot(player->network_player_data.machine_index) ||
			player->unit_index == NONE)
		{
			continue;
		}
		position = object_get(player->unit_index)->object.position;
		data_iterator_new(&humans, player_data);
		while ((human = (struct player_datum *)data_iterator_next(&humans)) != NULL)
		{
			if (human->local_player_index != NONE && human->unit_index != NONE)
				nearest = MIN(nearest, bots_distance2d(&position, &object_get(human->unit_index)->object.position));
		}
		distances[bot_index] = nearest;
		present[bot_index] = TRUE;
	}
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		if (!present[index])
			continue;
		/* (insertion into the sorted list) */
		for (other = count; other > 0 && sorted[other - 1] > distances[index]; other--)
			sorted[other] = sorted[other - 1];
		sorted[other] = distances[index];
		count++;
	}
	for (index = 0; index < BOTS_MAXIMUM; index++)
		bots_globals.far_from_humans[index] = present[index] && distances[index] > sorted[count / 2];
}

/* the controls a bot held for the budget: where it was heading and looking, its trigger and its pulses let go */
static void bots_hold_action(struct bots_bot *bot, struct player_action *action)
{
	unsigned long pulses = FLAG(_unit_control_jump_bit) | FLAG(_unit_control_throw_grenade_bit) |
		FLAG(_unit_control_use_equipment_bit) | FLAG(_unit_control_weapon_reload_bit);

	*action = bot->last_action;
	action->control_flags &= ~(pulses | FLAG(_unit_control_weapon_primary_trigger_bit));
	action->primary_trigger = 0.0f;
	action->desired_weapon_index = NONE;
	action->desired_grenade_index = NONE;
	action->desired_zoom_level = NONE;
	bot->previous_control_flags = action->control_flags;
}

/* The bots' tick: every bot decides its controls, which bots_fill_action gives the tick's update. Called by update_server_next_update
once for each update it builds (player_queues_new.c), before the update is: a bot's controls are queued as a player's are, so that
the update, what the host relays and what a machine predicts treat it as they treat any player, and there is one place bots enter
the game. Nothing outside a local game. */
void bots_tick(void)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long count = 0;
	long long tick_started = 0;
	long hold_ticks = 0;
#ifdef BOTS_TICK_PART
	long long part_started = 0;
#endif

	csmemset(bots_globals.action_valid, 0, sizeof(bots_globals.action_valid));
	/* (a local game, or a game this machine hosts when bots.online is on: the host decides every bot's controls and the update it builds carries
	them as a human's; a client of a hosted game runs none) */
	if (!game_engine_running() || (!network_game_is_splitscreen_local() &&
		(!bots_online_enabled() || network_game_distributed_client() || game_connection() != _game_connection_network_server)))
	{
		return;
	}
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long bot_index;
		long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		struct bots_bot *bot;
		struct player_action *action;
		long long bot_started;

		if (player->local_player_index != NONE || !bots_machine_is_bot(player->network_player_data.machine_index))
			continue;
		/* (the roaming places and the walkable surfaces once a game has bots; the skill setting and the budget once a tick) */
		if (!count)
		{
			long time = game_time_get();

			tick_started = bots_clock_microseconds();
			if (!bots_globals.first_tick_microseconds)
				bots_globals.first_tick_microseconds = tick_started;
#ifdef BOTS_TICK_PART
			part_started = wiiu_tick_part_now();
#endif
			/* (this update is for the game's time, or one past the last when the game has not ticked since the last) */
			if (time == bots_globals.world_time)
			{
				bots_globals.now++;
				bots_globals.stale_ticks++;
			}
			else
			{
				bots_globals.now = time;
				bots_globals.world_time = time;
			}
			if (!bots_globals.map_ready)
				bots_prepare_map();
			if (!bots_globals.nav)
				bots_nav_poll();
			bots_globals.setting_skill = bots_skill();
			bots_budget_begin_tick(&bots_globals.budget, bots_globals.now);
			hold_ticks = bots_budget_hold_ticks(&bots_globals.budget);
			if (hold_ticks)
				bots_mark_far_bots();
			bots_paths_scan();
		}
		/* (debug.bots_bake_nav: the navigation bake of the map, with the first bot that has a unit; then the end of the run) */
#ifndef HALO_WIIU
		if (player->unit_index != NONE && config_string("debug.bots_bake_nav")[0] && bots_globals.ticks > 2)
		{
			if (!bots_nav_bake_run(player->unit_index, main_get_multiplayer_map_name(), config_string("debug.bots_bake_nav")))
				platform_log("bots: bake: failed");
			exit(0);
		}
#endif
		bot_index = player->network_player_data.machine_index - BOTS_FIRST_MACHINE;
		bot = &bots_globals.bots[bot_index];
		if (bot->player_index != iterator.datum_index)
			bots_bot_reset(bot, iterator.datum_index, bot_index);
		bots_profile_resolve(&bot->profile, bots_globals.setting_skill);
		bot->counts.kills = (long)player->statistics.kills[0];
		bot->counts.deaths = (long)player->statistics.deaths;
		if (absolute_index < 0 || absolute_index >= HALO_PORT_MAXIMUM_NETWORK_PLAYERS)
		{
			count++;
			continue;
		}
		action = &bots_globals.actions[absolute_index];
		bots_globals.action_valid[absolute_index] = TRUE;
		bot_started = bots_clock_microseconds();
		bots_budget_elapsed(&bots_globals.budget, (long)(bot_started - tick_started));
		bots_budget_spend(&bots_globals.budget, BOTS_BUDGET_MOTOR);
		/* (the top step of the shedding: a bot among the farther half holds its last controls, up to a few ticks running) */
		if (hold_ticks && bots_globals.far_from_humans[bot_index] && bot->last_action_valid && bot->held_ticks < hold_ticks)
		{
			bots_hold_action(bot, action);
			bot->held_ticks++;
			bots_budget_note_deferred(&bots_globals.budget);
		}
		else
		{
			bots_globals.active_bot = bot;
			bots_bot_update(bot, bot_index, action);
			bots_globals.active_bot = NULL;
			bot->held_ticks = 0;
		}
		bot->last_action = *action;
		bot->last_action_valid = TRUE;
		bots_think_note(&bot->counts.think, (long)(bots_clock_microseconds() - bot_started));
		count++;
	}
	if (count)
	{
		long long tick_ended = bots_clock_microseconds();
		short level;
		char const *reason;

		bots_think_note(&bots_globals.tick_think, (long)(tick_ended - tick_started));
		bots_budget_end_tick(&bots_globals.budget, (long)(tick_ended - tick_started));
		if (bots_budget_take_change(&bots_globals.budget, &level, &reason))
			platform_log("bots: budget: step %d of %d at tick %ld: %s", (int)level, BOTS_BUDGET_TOP_LEVEL, bots_globals.now, reason);
		if (bots_budget_take_shed_request(&bots_globals.budget))
			platform_log("bots: budget: the cap is broken at the top step (a bot fewer is asked for; nothing acts on it yet)");
#ifdef BOTS_TICK_PART
		wiiu_tick_part("game_tick_bots_section", part_started);
#endif
		bots_globals.ticks++;
		if (!bots_globals.map_name[0])
			snprintf(bots_globals.map_name, sizeof(bots_globals.map_name), "%s", main_get_multiplayer_map_name());
		bots_report();
		/* (bots.sim_ticks, the headless runs': that many ticks of bots, then the block and the end of the run) */
		if (bots_globals.sim_ticks > 0 && bots_globals.ticks >= bots_globals.sim_ticks)
		{
			bots_report_sim("bots.sim_ticks");
			exit(0);
		}
	}
}

boolean bots_fill_action(long player_index, struct player_action *action)
{
	if (player_index < 0 || player_index >= HALO_PORT_MAXIMUM_NETWORK_PLAYERS || !bots_globals.action_valid[player_index])
		return FALSE;
	*action = bots_globals.actions[player_index];
	return TRUE;
}

/* ---------- the host's roster and the screens' API (bots.h)

In a game this machine hosts (bots.online) the lobby code (network_server_manager.c, network_game_server_port_bots) keeps the number of bots the director
asks for: bots.mode "fixed" has bots.count of them; "fill" makes the players up to bots.fill_to in all, the humans pushing the bots out as they join and
the bots coming back after a grace when they leave; at least bots.reserve_slots places are left free for a human to join. One change a few seconds
in a game in progress. The screens (another stream) read what a player is through bots_machine_is_bot and bots_player_tag and ask for changes through
bots_host_*: those are queued here and done by the lobby code's next idle, on the game thread. */

static struct
{
	/* the settings the screens changed (-1: the config's) */
	short mode;
	long fill_to;
	long count;
	/* the team and skill of the next bot added by bots_host_add, and the player the next drop takes (a roster index: the bot's machine) */
	long add_team;
	short add_skill;
	long drop_machine;
	long last_change_ms;
} bots_host = { -1, -1, -1, NONE, NONE, NONE, 0 };

boolean bots_online_enabled(void)
{
	return config_integer("bots.online") != 0;
}

short bots_host_mode(void)
{
	char const *name;

	if (bots_host.mode >= 0)
		return bots_host.mode;
	name = config_string("bots.mode");
	if (!strcmp(name, "fill"))
		return _bots_director_fill;
	if (!strcmp(name, "fixed"))
		return _bots_director_fixed;
	return _bots_director_off;
}

long bots_host_fill_to(void)
{
	return bots_host.fill_to >= 0 ? bots_host.fill_to : config_integer("bots.fill_to");
}

void bots_host_set_mode(short mode, long fill_to)
{
	bots_host.mode = mode;
	if (fill_to >= 0)
		bots_host.fill_to = fill_to;
}

long bots_hosted_wanted(long humans, long maximum_players, long bots_present)
{
	long count = bots_host.count >= 0 ? bots_host.count : config_integer("bots.count");
	long reserve = MAX(config_integer("bots.reserve_slots"), 0L);
	long room = maximum_players - humans - reserve;

	(void)bots_present;
	return bots_director_wanted(bots_host_mode(), (int)count, (int)bots_host_fill_to(), (int)humans, (int)bots_maximum(), (int)room);
}

boolean bots_host_add(long team, short skill)
{
	long wanted = bots_host.count >= 0 ? bots_host.count : config_integer("bots.count");

	bots_host.mode = _bots_director_fixed;
	bots_host.count = MIN(wanted + 1, (long)bots_maximum());
	bots_host.add_team = team;
	bots_host.add_skill = skill;
	return TRUE;
}

boolean bots_host_drop(long machine_index)
{
	long wanted = bots_host.count >= 0 ? bots_host.count : config_integer("bots.count");

	if (!bots_machine_is_bot(machine_index))
		return FALSE;
	bots_host.mode = _bots_director_fixed;
	bots_host.count = MAX(wanted - 1, 0L);
	bots_host.drop_machine = machine_index;
	return TRUE;
}

boolean bots_host_take_add(long *team, short *skill)
{
	*team = bots_host.add_team;
	*skill = bots_host.add_skill;
	bots_host.add_team = NONE;
	bots_host.add_skill = NONE;
	return TRUE;
}

long bots_host_take_drop(void)
{
	long machine = bots_host.drop_machine;

	bots_host.drop_machine = NONE;
	return machine;
}

boolean bots_host_change_allowed(long milliseconds, long grace_milliseconds)
{
	if (bots_host.last_change_ms && milliseconds - bots_host.last_change_ms < grace_milliseconds)
		return FALSE;
	bots_host.last_change_ms = milliseconds ? milliseconds : 1;
	return TRUE;
}

short bots_machine_skill(long machine_index)
{
	long bot_index = machine_index - BOTS_FIRST_MACHINE;

	if (!bots_machine_is_bot(machine_index))
		return NONE;
	if (bots_globals.bots[bot_index].player_index != NONE)
		return bots_globals.bots[bot_index].profile.skill;
	{
		short own = bots_debug_skill(bot_index);

		return own != NONE ? own : bots_skill();
	}
}

void bots_machine_tag(long machine_index, char *buffer, long size)
{
	static char const *const names[NUMBER_OF_BOTS_SKILLS] = { "RECRUIT", "EASY", "NORMAL", "HEROIC", "VETERAN", "LEGENDARY" };
	short skill = bots_machine_skill(machine_index);

	if (size <= 0)
		return;
	if (skill == NONE)
		buffer[0] = 0;
	else
		snprintf(buffer, (size_t)size, "BOT %s", names[skill]);
}

long bots_roster_count(void)
{
	long index, count = 0;

	for (index = 0; index < BOTS_MAXIMUM; index++)
		if (bots_globals.bots[index].player_index != NONE)
			count++;
	return count;
}

void bots_set_skill(long bot_index, short skill)
{
	if (bot_index < 0 || bot_index >= BOTS_MAXIMUM)
		return;
	/* (kept for the bot a lobby adds next: bots_bot_reset takes it up when its player is there) */
	bots_requested_skill[bot_index] = (short)(skill == NONE ? 0 : skill + 1);
	if (bots_globals.bots[bot_index].player_index != NONE)
		bots_profile_set_skill(&bots_globals.bots[bot_index].profile, skill);
}
