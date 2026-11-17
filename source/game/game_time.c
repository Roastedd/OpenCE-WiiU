/*
GAME_TIME.C

symbols in this file:
000A4D00 0020:
	_game_time_initialized (0000)
000A4D20 0030:
	_game_time_initialize (0000)
000A4D50 0050:
	_game_time_initialize_for_new_map (0000)
000A4DA0 0020:
	_game_time_dispose_from_old_map (0000)
000A4DC0 0010:
	_game_time_dispose (0000)
000A4DD0 0040:
	_game_time_end (0000)
000A4E10 0040:
	_game_time_get (0000)
000A4E50 0040:
	_game_time_get_elapsed (0000)
000A4E90 0040:
	_local_time_get (0000)
000A4ED0 0040:
	_local_time_get_elapsed (0000)
000A4F10 0040:
	_game_predicting (0000)
000A4F50 0050:
	_game_in_progress (0000)
000A4FA0 0040:
	_game_time_get_paused (0000)
000A4FE0 0050:
	_game_time_set_paused (0000)
000A5030 0040:
	_game_time_get_speed (0000)
000A5070 0050:
	_game_time_set_speed (0000)
000A50C0 0010:
	_game_time_statistics_new (0000)
000A50D0 01e0:
	_game_time_statistics_frame (0000)
000A52B0 00e0:
	_game_time_start (0000)
000A5390 0360:
	_game_time_update (0000)
0025C480 0012:
	??_C@_0BC@ENHAGKAA@game?5time?5globals?$AA@ (0000)
0025C494 0035:
	??_C@_0DF@KKMNPDPK@game_time_globals?5?$CG?$CG?5?$CBgame_time_@ (0000)
0025C4CC 0020:
	??_C@_0CA@INLHHDGN@c?3?2halo?2SOURCE?2game?2game_time?4c?$AA@ (0000)
0025C4EC 0012:
	??_C@_0BC@NOJBGBLH@game_time_globals?$AA@ (0000)
0025C500 0034:
	??_C@_0DE@PPGKJIJK@game_time_globals?5?$CG?$CG?5game_time_g@ (0000)
0025C534 001b:
	??_C@_0BL@DECMFGMD@?$CBgame_time_globals?9?$DOactive?$AA@ (0000)
0025C550 0021:
	??_C@_0CB@OIFBECDL@ticks_elapsed?5?$DM?$DN?5maximum_actions@ (0000)
0025C578 004c:
	??_C@_0EM@FOLKNDLG@game_time_globals?9?$DOleftover_dt?$DO?$DN@ (0000)
0025C5C8 004c:
	??_C@_0EM@FBBKPKLD@update?5server?5is?5too?5far?5ahead?5o@ (0000)
0043EE04 002c:
	_bss_0043ee04 (0000)
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries_windows.h"
#include "real_math.h"
#include "game.h"
#include "player_queues_new.h"
#include "networking/network_client_manager.h"
#include "networking/network_game_globals.h"
#include "networking/network_server_manager.h"
#include "saved games/game_state.h"
#include "scenario/scenario.h" /* port: game_time_menu_scene */
#include "scenario/scenario_definitions.h"
#include "profile_sections.h" /* port: port/linux/include/profile_sections.h */
#ifdef HALO_WIIU
#include "tick_owners.h" /* port: port/linux/game/tick_owners.c */
#include "tick_overlap.h" /* port: port/linux/game/tick_overlap.c */
#endif
#include "mcc_game_speed.h" /* port: port/linux/game/mcc_game_speed.c */
/* port/linux/game/network_distributed.c's */
void network_distributed_tick(void);

/* port: the tick loop's steps, timed in the profiling build */
PROFILE_SECTION(game_time_network_distributed_tick_section, "network_distributed_tick")
PROFILE_SECTION(game_time_render_interpolation_tick_section, "render_interpolation_tick")
PROFILE_SECTION(game_time_game_frame_section, "game_frame")

/* ---------- constants */

enum
{
	SOME_LARGE_NUMBER_OF_TICKS = 1000, // 0x03E8
};

/* ---------- macros */

/* ---------- structures */

struct game_time_globals_struct
{
	boolean initialized;
	boolean active;
	boolean paused;
	/* (these three unused: kept for the game state's layout) */
	short monitor_state;
	short monitor_counter;
	short monitor_latency;
	long local_time;
	short last_local_time_elapsed;
	long server_time;
	real speed;
	real leftover_dt;
};

/* ---------- prototypes */

/* ---------- globals */

static struct game_time_globals_struct *game_time_globals;
/* port: a reliable host update's time, not part of the saved world. */
static long game_time_distributed_target = NONE;

#ifdef HALO_WIIU
/* port: the clock as the drawing thread sees it while a frame is drawn beside the ticks (tick_overlap.h): the tick the frame is drawn from, the ticks
the frame's job runs, and how far the frame is into the next. The ticks run on another thread and move the real clock meanwhile, so the drawing
reads these instead (the thread that reads them is told by its core; the tick thread reads the clock). Written by the drawing thread only. */
static struct
{
	volatile int on;
	long time;
	short elapsed;
	real fraction;
} game_time_view;
#define GAME_TIME_VIEW() (game_time_view.on && tick_overlap_thread_is_render())
#endif

#ifdef HALO_WIIU
/* port: only the game thread reads its clock/menu state; the optional shader-file worker receives an atomic activity flag.
   Publish before enabling gameplay, and after disabling it. No elapsed-tick or rendering heuristic controls file eligibility. */
static void game_time_wiiu_capture_activity(
	boolean active)
{
	extern boolean main_menu_is_active(void);

	wiiu_missing_keys_gameplay(active && !main_menu_is_active());
}
/* port: main_menu_active can change after the UI clock starts; publish its projected state on the same game thread. */
void game_time_wiiu_capture_menu(
	int main_menu)
{
	wiiu_missing_keys_gameplay(game_time_globals && game_time_globals->initialized &&
		game_time_globals->active && !main_menu);
}
#endif

/* ---------- public code */

boolean game_time_initialized(
	void)
{
	return (game_time_globals && game_time_globals->initialized);
}

void game_time_initialize(
	void)
{
	game_time_globals = (struct game_time_globals_struct *)game_state_malloc("game time globals", NULL, sizeof(*game_time_globals));
	memset(game_time_globals, 0, sizeof(*game_time_globals));
#ifdef HALO_WIIU
	/* port: inactive clock permits optional shader-file work. */
	game_time_wiiu_capture_activity(FALSE);
#endif
	game_time_distributed_target = NONE; /* port: no previous session's target */

	return;
}

void game_time_initialize_for_new_map(
	void)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 131, game_time_globals && !game_time_globals->initialized);
	memset(game_time_globals, 0, sizeof(*game_time_globals));
	game_time_globals->initialized = TRUE;
#ifdef HALO_WIIU
	/* port: inactive clock permits optional shader-file work. */
	game_time_wiiu_capture_activity(FALSE);
#endif
	game_time_distributed_target = NONE; /* port: no previous map's target */

	return;
}

void game_time_dispose_from_old_map(
	void)
{
	game_time_distributed_target = NONE; /* port: discard the departing map's target */
	if (game_time_globals)
	{
		game_time_globals->initialized = FALSE;
		game_time_globals->active = FALSE;
	}
#ifdef HALO_WIIU
	/* port: inactive clock permits optional shader-file work. */
	game_time_wiiu_capture_activity(FALSE);
#endif

	return;
}

void game_time_dispose(
	void)
{
	return;
}

void game_time_end(
	void)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 196, game_time_globals);
	game_time_globals->active = FALSE;
#ifdef HALO_WIIU
	/* port: inactive clock permits optional shader-file work. */
	game_time_wiiu_capture_activity(FALSE);
#endif
	game_time_distributed_target = NONE; /* port: discard the finished game's target */

	return;
}

void game_time_set_distributed(
	long time)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 0, game_time_globals && game_time_globals->initialized);

	game_time_globals->local_time = time;
	game_time_globals->server_time = time;
	game_time_globals->leftover_dt = 0.f;
	game_time_distributed_target = NONE; /* port: a new clock invalidates its target */

	return;
}

/* port: remember only validated reliable host time. Small stalls used to
leave clients behind indefinitely; game_time_update runs the missing ticks. */
void game_time_synchronize_distributed(
	long time)
{
	if (!game_time_globals || !game_time_globals->initialized ||
		!game_time_globals->active || !network_game_distributed_client() ||
		game_time_menu_scene() || time < 0 || time > 0x3FFFFFFF)
		return;
	if (time > game_time_globals->local_time && time > game_time_distributed_target)
		game_time_distributed_target = time;

	return;
}

long game_time_get(
	void)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 463, game_time_globals && game_time_globals->initialized);

#ifdef HALO_WIIU
	if (GAME_TIME_VIEW())
		return game_time_view.time;
#endif
	return game_time_globals->local_time;
}

short game_time_get_elapsed(
	void)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 471, game_time_globals && game_time_globals->initialized);

#ifdef HALO_WIIU
	if (GAME_TIME_VIEW())
		return game_time_view.elapsed;
#endif
	return game_time_globals->last_local_time_elapsed;
}

long local_time_get(
	void)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 479, game_time_globals && game_time_globals->initialized);

#ifdef HALO_WIIU
	if (GAME_TIME_VIEW())
		return game_time_view.time;
#endif
	return game_time_globals->local_time;
}

short local_time_get_elapsed(
	void)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 487, game_time_globals && game_time_globals->initialized);

#ifdef HALO_WIIU
	if (GAME_TIME_VIEW())
		return game_time_view.elapsed;
#endif
	return game_time_globals->last_local_time_elapsed;
}

boolean game_predicting(
	void)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 495, game_time_globals && game_time_globals->initialized);

	return FALSE;
}

boolean game_in_progress(
	void)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 505, game_time_globals);

	if (game_time_globals->initialized)
	{
		if (game_time_globals->active)
		{
			return TRUE;
		}

		/* bug? */
		if (game_time_globals->paused)
		{
			return TRUE;
		}
		else
		{
			return FALSE;
		}
	}

	return FALSE;
}

boolean game_time_get_paused(
	void)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 533, game_time_globals);

	return game_time_globals->paused;
}

void game_time_set_paused(
	boolean paused)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 541, game_time_globals);

#ifdef HALO_WIIU
	/* port: close optional file admission before a resume activates the clock. */
	if (!paused)
		game_time_wiiu_capture_activity(game_time_globals->initialized);
#endif
	if (game_time_globals->initialized)
	{
		game_time_globals->active = !paused;
	}

	game_time_globals->paused = paused;
#ifdef HALO_WIIU
	/* port: permit optional file work only after the paused state is stored. */
	if (paused)
		game_time_wiiu_capture_activity(FALSE);
#endif

	return;
}

#ifdef HALO_WIIU
/* port: the menus' pause, a message to the tick thread (tick_owners.h). The queue's records are the pause wanted; the menu's
own reading of the pause is the last message while any waits. */
struct game_time_pause_message
{
	boolean paused;
};

static void game_time_pause_message_apply(
	void const *record);

TICK_QUEUE_DEFINE(game_time_pause_queue, "pause", TICK_OWNER_TICK, struct game_time_pause_message, 16, game_time_pause_message_apply);

static boolean game_time_pause_shadow = FALSE;

static void game_time_pause_message_apply(
	void const *record)
{
	game_time_set_paused(((struct game_time_pause_message const *)record)->paused);

	return;
}

void game_time_post_paused(
	boolean paused)
{
	if (TICK_OWNERS_FEATURE(TICK_FEATURE_PAUSE))
	{
		struct game_time_pause_message message;

		message.paused = paused;
		game_time_pause_shadow = paused;
		tick_queue_push(&game_time_pause_queue, &message);
	}
	else
	{
		game_time_set_paused(paused);
	}

	return;
}

boolean game_time_ui_get_paused(
	void)
{
	if (TICK_OWNERS_FEATURE(TICK_FEATURE_PAUSE) && tick_queue_count(&game_time_pause_queue))
		return game_time_pause_shadow;

	return game_time_get_paused();
}

/* the messages made now, for a menu that goes on to change the clock itself */
void game_time_ui_flush_paused(
	void)
{
	if (TICK_OWNERS_FEATURE(TICK_FEATURE_PAUSE))
		tick_queue_drain(&game_time_pause_queue);

	return;
}
#endif

/* how far the clock has run into the next tick, 0 to 1: the native ports
draw frames between ticks (port/linux/game/render_interpolation.c) */
real game_time_get_tick_fraction(
	void)
{
	real fraction;

#ifdef HALO_WIIU
	if (GAME_TIME_VIEW())
		return game_time_view.fraction;
#endif
	if (!game_time_globals || !game_time_globals->active || game_time_globals->paused)
		return 1.0f;
	fraction = game_time_globals->leftover_dt * game_time_globals->speed * TICKS_PER_SECOND;
	/* port: MCC clients interpolate using only the host's approved speed. */
	if (network_game_distributed_client())
		fraction = game_time_globals->leftover_dt * mcc_game_speed_client(game_time_globals->speed) * TICKS_PER_SECOND;
	return PIN(fraction, 0.0f, 1.0f);
}

real game_time_get_speed(
	void)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 555, game_time_globals);

	/* port: a client of another's game runs at the host's speed (its own
	set before it joined too: cheats_network_client_enforce) */
	if (network_game_distributed_client())
		return mcc_game_speed_client(1.0f); /* port: legacy clients remain at one */
	return game_time_globals->speed;
}

void game_time_set_speed(
	real speed)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 562, game_time_globals);

	/* port: a speed (a map's script's, game_speed) that is a number, not
	negative and not past a hundred: a NaN or an infinity stopped the game's
	time for good */
	if (!(speed >= 0.0f && speed <= 100.0f))
		return;
	game_time_globals->speed = speed;

	return;
}

/* port: the game's own speed put back; whether it was another
(cheats_network_client_enforce) */
boolean game_time_reset_speed(
	void)
{
	boolean changed;

	if (!game_time_globals)
		return FALSE;
	changed = game_time_globals->speed != 1.0f;
	game_time_globals->speed = 1.0f;

	return changed;
}

/* whether a client's clock waits for the host's first game update, which
brings the host's time (the host ticks only once every machine has
loaded) */
boolean game_time_held(
	void)
{
	struct network_game_client *client;

	if (game_connection() != _game_connection_network_client)
		return FALSE;
	client = global_network_game_client_get();
	return client && !network_game_client_server_has_started_game(client);
}

/* port: whether the main menu's scene is what ticks. It runs on this
machine's clock and takes no player input: a tick waits for the players'
input, an action for each, and the menus change who the local players are
(profiles), and a lobby's network game owns the queues, so with none the
ticks stopped and the scene froze behind the menus (players.c gives its
players idle actions) */
boolean game_time_menu_scene(
	void)
{
	return global_scenario && global_scenario->type == _scenario_type_main_menu;
}

void game_time_start(
	void)
{
	short connection;

	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 162, game_time_globals && game_time_globals->initialized);
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 163, !game_time_globals->active);

	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 562, game_time_globals);

	game_time_globals->speed = 1.f;
	game_time_globals->leftover_dt = 0;
#ifdef HALO_WIIU
	/* port: close optional file admission before gameplay starts. */
	game_time_wiiu_capture_activity(TRUE);
#endif
	game_time_globals->active = TRUE;

	connection = game_connection();

	switch (connection)
	{
	case _game_connection_local:
	case _game_connection_network_server:
	{
		update_server_start();
		break;
	}
	case _game_connection_network_client:
	case _game_connection_film_playback:
	{
		update_client_start();
		break;
	}
	}

	return;
}

long config_integer(const char *name);

/* port: the ticks of the frames since the last net: line (network_distributed.c): the most in one frame, and how many frames ran more than 4 */
static long game_time_peak_ticks;
static long game_time_frames_over_four;

void game_time_take_tick_peaks(
	long *peak_ticks,
	long *frames_over_four)
{
	*peak_ticks = game_time_peak_ticks;
	*frames_over_four = game_time_frames_over_four;
	game_time_peak_ticks = 0;
	game_time_frames_over_four = 0;
}

/* port: network.catchup_ticks (1 to 30), read when a game starts to be played rather than every frame */
static long game_time_catchup_cap(
	void)
{
	long cap = config_integer("network.catchup_ticks");

	if (cap < 1)
		cap = 1;
	if (cap > TICKS_PER_SECOND)
		cap = TICKS_PER_SECOND;
	return cap;
}

/* The clock's decision for a frame of time_delta_sec: how many ticks it runs, and the time left over after them. One function for the serial update and
the overlap's (which makes the decision for a frame before the tick thread runs it: tick_overlap.h). Returns FALSE (no tick, the leftover as it
was) when the clock is stopped or the speed is nothing. */
static boolean game_time_clock_step(
	real time_delta_sec,
	real leftover_in,
	real speed,
	long *ticks_elapsed_out,
	real *leftover_out)
{
	long connection;
	long ticks_elapsed;
	real ticks_per_second;

	/* port: preserve native client cheat enforcement; use MCC host state separately. */
	if (network_game_distributed_client())
		speed = mcc_game_speed_client(speed);
	ticks_per_second = speed*TICKS_PER_SECOND;

	*ticks_elapsed_out = 0;
	*leftover_out = leftover_in;
	if (ticks_per_second > 0.f)
	{
		boolean discard_leftover_time;
		real game_time;
		real ticks_elapsed_real;
		real leftover_dt;

		connection = game_connection();
		switch (connection)
		{
		case _game_connection_film_playback:
			connection = TICKS_PER_SECOND;
			discard_leftover_time = FALSE;
			goto calculate_elapsed_ticks;
		case _game_connection_network_client:
		case _game_connection_network_server:
			/* (the distributed netcode: every machine ticks on its own
			clock, and the host waits for nobody) */
			connection = TICKS_PER_SECOND;
			break;
		case _game_connection_local:
			connection = 7;
			break;
		default:
			connection = 7;
			break;
		}
		discard_leftover_time = TRUE;

	calculate_elapsed_ticks:
		game_time = time_delta_sec + leftover_in;
		ticks_elapsed_real = (real)floor(game_time*ticks_per_second);
		ticks_elapsed = (long)(ticks_elapsed_real <= (real)SOME_LARGE_NUMBER_OF_TICKS ?
			ticks_elapsed_real : (real)SOME_LARGE_NUMBER_OF_TICKS);
		if (ticks_elapsed > connection)
		{
			ticks_elapsed = connection;
			if (discard_leftover_time)
				game_time = ticks_elapsed_real/ticks_per_second;
		}

		leftover_dt = game_time - ticks_elapsed_real/ticks_per_second;
		if (leftover_dt < 0.f)
			leftover_dt = 0.f;
		/* port: a joined client catches up over several frames. After a stall (the HUD's first
		use, a texture, a file) the frame that follows owed up to a second of ticks and ran them
		all at about 7 ms each: a 540 ms frame after a 1.4 s one, and the inputs of those ticks
		sent in a burst. Here no more than network.catchup_ticks run in a frame, and the time
		not yet run is the leftover (the one second that was always the most owed), so the
		clock still ends up where it was, a few frames later. */
		if (game_connection() == _game_connection_network_client && ticks_elapsed > 1)
		{
			long cap = game_time_catchup_cap();

			if (ticks_elapsed > cap)
			{
				real owed = ticks_elapsed/ticks_per_second;

				ticks_elapsed = cap;
				leftover_dt += owed - ticks_elapsed/ticks_per_second;
			}
		}
		/* port: debug.fixed_step, a game of this machine alone: one tick a
		frame that has time (main.c gives it a tick's; a 30th of a second in
		a float is not one tick exactly), so that what runs once a frame
		(aiming) sees the game's time; game_speed has no effect */
		if (halo_fixed_step() && game_connection() == _game_connection_local && time_delta_sec > 0.f)
		{
			ticks_elapsed = 1;
			leftover_dt = 0.f;
		}
		/* port: recover a small clock deficit by simulating, not skipping, ticks. Keep the fractional time and the frame's existing cap;
		at most double its normal work, never extrapolating the host's time. */
		if (network_game_distributed_client() && !game_time_held() && !game_time_menu_scene() && game_time_globals->local_time >= 0 &&
			game_time_distributed_target > game_time_globals->local_time)
		{
			long ticks_behind = game_time_distributed_target - game_time_globals->local_time;
			long extra_ticks = MIN(ticks_elapsed, ticks_behind - ticks_elapsed);

			if (extra_ticks > 0)
				ticks_elapsed += MIN(extra_ticks, TICKS_PER_SECOND - ticks_elapsed);
		}
		match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 306,
			leftover_dt>=0.f && leftover_dt<100.f);
		*ticks_elapsed_out = ticks_elapsed;
		*leftover_out = leftover_dt;
		return TRUE;
	}

	return FALSE;
}

/* The ticks of a frame: ticks_elapsed of them, with the time they bring. capture_last says which get a capture of the render view snapshot after
them (tick_snapshot.c): every one if it is negative, else the last capture_last of them (the overlap's frames blend the last two ticks only, and
a capture is the part of a tick the drawing thread needs). Returns the ticks run. */
static long game_time_run_ticks(
	long ticks_elapsed,
	long capture_last)
{
	long final_local_time;
	long maximum_possible_server_time;
	long ticks_run = 0;

	final_local_time = game_time_globals->local_time + ticks_elapsed;
	switch (game_connection())
	{
	case _game_connection_local:
		update_client_local_ticks(ticks_elapsed);
		break;
	case _game_connection_network_server:
		network_game_server_update_ticks(global_network_game_server_get(), (short)ticks_elapsed);
		break;
	}

	/* (a client of the distributed netcode ticks on its own clock,
	with its own input and the latest the host relayed, from the
	host's first game update, which brings the host's time: the
	host ticks only once every machine has loaded) */
	/* port: the main menu's scene ticks on this machine's clock,
	in a lobby too (game_time_menu_scene) */
	if (game_time_menu_scene())
	{
		maximum_possible_server_time = final_local_time;
	}
	else if (game_connection() == _game_connection_network_client)
	{
		maximum_possible_server_time = game_time_held() ?
			game_time_globals->server_time : final_local_time;
	}
	else
		maximum_possible_server_time = update_client_get_maximum_possible_server_time();
	if (maximum_possible_server_time > game_time_globals->server_time)
	{
		long final_server_time = MIN(maximum_possible_server_time, final_local_time);
		long server_updates = final_server_time - game_time_globals->server_time;
		long update_index;

		for (update_index = 0; update_index < server_updates; update_index++)
		{
			game_tick();
#ifdef HALO_WIIU
			tick_overlap_jitter(); /* (test aid: halo/tick_overlap_jitter.txt) */
			if (capture_last < 0)
			{
				/* the chain of snapshots of each object that the serial frames blend (render_interpolation.c) */
				profile_scope(game_time_render_interpolation_tick_section, render_interpolation_tick();)
				/* the render view snapshot (tick_snapshot.c): what the renderer reads of this tick's objects */
				tick_snapshot_capture_tick(game_time_globals->local_time + 1, TRUE);
			}
			else if (server_updates - update_index <= capture_last)
			{
				/* (the overlap's: the ring, committed with the cameras when the job is over) */
				tick_snapshot_capture_tick(game_time_globals->local_time + 1, FALSE);
			}
			tick_overlap_jitter(); /* (test aid) */
#else
			profile_scope(game_time_render_interpolation_tick_section, render_interpolation_tick();)
#endif
			game_time_globals->server_time++;
			game_time_globals->local_time++;
			ticks_run++;
			game_state_tick_hash(game_time_globals->server_time);
			/* the distributed netcode's per-tick state (port: none of
			the main menu's scene, which a lobby's machines do not share) */
			if (!game_time_menu_scene())
				profile_scope(game_time_network_distributed_tick_section, network_distributed_tick();)
#ifdef HALO_WIIU
			/* port: the tick thread's safe point, between two ticks (tick_owners.h) */
			tick_owners_safe_point();
#endif
		}
	}

	/* (none while a client's clock waits: a frame that runs no tick
	reports none) */
	game_time_globals->last_local_time_elapsed = game_time_held() ? 0 : (short)ticks_elapsed;

	return ticks_run;
}

void game_time_update(
	real time_delta_sec)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 205, game_time_globals);
	/* port: MCC speed updates must continue when simulation is stopped. */
	mcc_game_speed_frame();

	if (game_time_globals->active)
	{
		long ticks_elapsed;
		real leftover_dt;

		/* The native builds draw several frames per tick
		(port/linux/game/render_interpolation.c). A frame that runs no tick has
		elapsed no game time: without this, the ticks of the last frame that
		ran some would count again on every frame after it, and whatever
		advances by game_time_get_elapsed() once a frame (chapter titles, HUD
		messages and flashes, screen flashes, camera shake and impulses) would
		run as many times too fast as there are frames per tick. On the Xbox
		every frame ran at least one tick. */
		game_time_globals->last_local_time_elapsed = 0;

		if (game_time_clock_step(time_delta_sec, game_time_globals->leftover_dt, game_time_globals->speed, &ticks_elapsed, &leftover_dt))
		{
			game_time_globals->leftover_dt = leftover_dt;
			if (ticks_elapsed > game_time_peak_ticks)
				game_time_peak_ticks = ticks_elapsed;
			if (ticks_elapsed > 4)
				game_time_frames_over_four++;
			if (ticks_elapsed > 0)
				game_time_run_ticks(ticks_elapsed, -1);
		}

		/* The effects and the widgets that move every frame (game_frame) hang
		off objects: they read the pose between the last two ticks, as the
		renderer does, and the ticks above have taken their snapshots already
		(port/linux/game/render_interpolation.c). The frames of one tick share
		the fraction, so what they hang off is drawn where they put it. */
		render_interpolation_frame_begin();
		profile_scope(game_time_game_frame_section, game_frame(game_time_get_speed()*time_delta_sec);)
		render_interpolation_frame_end();
	}
	else
	{
		game_time_globals->last_local_time_elapsed = 0;
	}

	return;
}

#ifdef HALO_WIIU
/* ---------- the overlap's clock (tick_overlap.h) */

/* The game thread's, with the tick thread idle: what the frame about to be handed to the tick thread does to the clock. FALSE if the clock is
stopped, paused or has no speed (the frame is not the overlap's). time_now is the game time before the frame's ticks, the ticks the frame runs,
leftover_after the time left over after them. */
boolean game_time_overlap_step(
	real time_delta_sec,
	long *time_now,
	long *ticks_elapsed,
	real *leftover_after,
	real *speed)
{
	/* (a game of this machine alone, or a network game main_overlap_eligible has let in: halo/net_overlap.txt. A saved film is the serial loop's, and so is a client whose clock
	waits for the host's first update: no tick runs then, the clock has nothing to decide) */
	if (!game_time_globals || !game_time_globals->initialized || !game_time_globals->active || game_time_globals->paused ||
		game_connection() == _game_connection_film_playback || game_time_held())
	{
		return FALSE;
	}
	*time_now = game_time_globals->local_time;
	*speed = game_time_globals->speed;
	return game_time_clock_step(time_delta_sec, game_time_globals->leftover_dt, game_time_globals->speed, ticks_elapsed, leftover_after);
}

/* The tick thread's part of such a frame: the clock set as the decision made, and the ticks run (game_frame is the drawing thread's in the overlap).
Returns the ticks run. */
long game_time_overlap_run(
	long ticks_elapsed,
	real leftover_after)
{
	long ticks_run = 0;

	if (!game_time_globals->active)
	{
		game_time_globals->last_local_time_elapsed = 0;
		return 0;
	}
	game_time_globals->last_local_time_elapsed = 0;
	game_time_globals->leftover_dt = leftover_after;
	if (ticks_elapsed > 0)
		ticks_run = game_time_run_ticks(ticks_elapsed, 2);

	return ticks_run;
}

/* The clock as the drawing thread sees it for the frame (game_time_view above), or not (on = FALSE) */
void game_time_set_view(
	boolean on,
	long time,
	short elapsed,
	real fraction)
{
	game_time_view.time = time;
	game_time_view.elapsed = elapsed;
	game_time_view.fraction = fraction;
	game_time_view.on = on;
}

/* the clock's own time and leftover, as the tick thread left them (read when it is idle) */
long game_time_overlap_time(
	void)
{
	return game_time_globals ? game_time_globals->local_time : 0;
}

real game_time_overlap_leftover(
	void)
{
	return game_time_globals ? game_time_globals->leftover_dt : 0.0f;
}
#endif

/* ---------- private code */
