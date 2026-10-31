/*
GAMEPAD_WINDOW.C

What the Wii U's GamePad view (input.wiiu_gamepad_view, port/wiiu/src/d3d8_gx2.c) needs to know of the game: where the first local
player's window is on the screen while the screen is split. The Direct3D layer cannot see the game's types, so it asks here (this file
is compiled as the game's own sources are).
*/

#include "cseries.h"
#include "cutscene/cinematics.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "main/main.h"

#ifdef HALO_WIIU

/* players.c's local_player_count, 0 before the players' globals are made */
short players_port_local_player_count(void);

/* The first window's pixels (x0, y0, x1, y1, in the game's 480 lines and halo_screen_width's columns), as main.c's main_render splits the
screen for this frame: the windows main.c's compute_window_bounds makes for the local players, or one for the whole screen while a cinematic
or the postgame has it. Returns the windows' count; with one, bounds is not set. The globals are asked first, as touch_game.c asks them:
the first frames are presented before the game exists. */
short gamepad_window_bounds(short bounds[4])
{
	rectangle2d pixel_bounds;
	rectangle2d safe_frame_bounds;
	short windows;

	if (!cinematic_globals || !game_in_progress() || game_engine_force_single_screen() || cinematic_in_progress())
		return 1;
	windows = players_port_local_player_count();
	if (windows <= 1)
		return 1;
	if (windows > MAXIMUM_LOCAL_PLAYERS)
		windows = MAXIMUM_LOCAL_PLAYERS;
	compute_window_bounds(0, windows, &pixel_bounds, &safe_frame_bounds);
	bounds[0] = pixel_bounds.x0;
	bounds[1] = pixel_bounds.y0;
	bounds[2] = pixel_bounds.x1;
	bounds[3] = pixel_bounds.y1;
	return windows;
}

#endif
