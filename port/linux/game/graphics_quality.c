/*
GRAPHICS_QUALITY.C

The detail a player can trade for frame rate, each setting on one of the engine's own switches (those its console
sets: hs_globals_external.c): display.grass (the detail objects: grass, pebbles and plants), display.decals (bullet
holes, scorches and blood), display.particles (particles, particle systems and contrails), display.object_shadows (the
shadows objects cast), display.lens_flares, display.specular (the level's specular lights, reflections and mirrors) and
display.bump_mapping. They are set at the start of a frame (rasterizer_frame_begin) when config.toml's settings or the
number of views drawn have changed, so a console command (init.txt) holds until one of them does.

display.graphics_mode names a set of values for them and for display.render_lines (the Wii U's lines, from the next
start), display.dynamic_resolution (the Wii U's: fewer lines while the GPU falls behind, port/wiiu/src/wiiu_drs.c) and
display.interpolation: performance (the frame rate first: 60 frames a second where the game thread allows it, with what
costs it most of the detail left out), balanced (720 lines at 60, all the Xbox drew), quality (up to 1080 lines at 30
frames a second, all the Xbox drew: dynamic resolution keeps the frames in their time where 1080 lines would not) or
custom (the settings as they are). The menus write a mode's values when it is chosen, and custom when a setting a mode
sets is changed by hand (menu_functions.c), so the settings always hold what is drawn and the menus show it.

display.dynamic_quality (performance sets it) is the frame-time governor, port/linux/game/render_governor.c: it leaves out optional drawing, a step at a
time, while the game thread's frames take longer than 22 ms, and puts it back when they do not. What the switches above cost on the console is small beside
the objects: of a frame of 22 ms in Blood Gulch's and b30's fights the shadows are 1.1, the specular shine 0.2, the grass and lens flares under 0.1, and the
objects 11 (2026-11-03, governor.md), so performance on its own took about a millisecond; the governor's steps are the ones that reach the objects, the decals
and the particles.

Split screen (the Wii U's settings). The game draws each view in turn, the whole scene again for each (render.c's
render_window), so three or four views cost the game thread nearly three or four times one view's drawing: each view
after the first is estimated at about 13 ms of the game thread's time on the console (two views about 30 ms a frame,
four about 56, 18 frames a second), and the game thread, not the GPU, is what limits it. With three or four views:
- display.split_screen_detail "reduced" (the default) leaves out what each view draws again at a cost that grows with
  what it sees, for what it shows: the objects' shadows (each object a view sees drawn a second time into its shadow,
  and the level's surfaces under it found and drawn again: render_objects.c's render_object_shadows,
  structure_render.c's structure_render_shadow), whose cost grows with the players each view sees; the lens flares (an
  occlusion test each, each view), which the Wii U never draws anyway while its visibility tests are off, as they are by
  default (wiiu_gx2.c); and the level's specular lights and reflections (its surfaces drawn again for each:
  object_lights.c's lights_render_specular, structure_render.c's structure_render_reflections). They are what the
  performance mode leaves out but the grass,
  which the engine leaves out itself with more than one view (rasterizer_xbox_detail_objects.c), as it does the blur
  screen effects (interface.c) and the mirrors (render.c). Particles stay: their switch takes the contrails too, which
  are the assault rifle's tracers, the sniper rifle's trail, the plasma rifle's and the needler's shots and the
  rocket's exhaust (Blood Gulch's contrail tags), where fire comes from, which a player in a quarter of the screen needs
  more than a shadow. "same" draws what one view does.
- display.split_screen_fps "auto" (the default) holds the frames to 30 a second, as display.interpolation off does,
  each frame two of the TV's refreshes, without changing that setting (platform_wiiu.c's halo_interpolation_enabled
  asks graphics_quality_split_screen_capped every frame): a frame that takes 40 ms shown at 60 Hz alternates between
  two refreshes and three, an uneven step. Mario Kart 8 holds 60 for one or two players and 30 for three or four. Two
  views keep display.interpolation's; "30" holds two or more, "60" none.
The views are those the frame draws (main_get_window_count): one in a cinematic or the postgame's single screen, which
then has the full detail and display.interpolation's frame rate. A client in another machine's game has its lens flares
and the level's specular parts put back by the host's rules every tick (cheats.c's cheats_network_client_enforce, which
does the same to the performance mode's), so there the reduced set is the shadows alone; a game this machine hosts, as
the split screen lobby's is, has the whole set.
*/

#include "cseries.h"
#include "rasterizer/rasterizer_console_vars.h"
#include "effects/decals.h"
#include "render/render.h"
#include "render/render_particles.h"

#include "graphics_quality.h"
#include "render_governor.h"

#include <stdlib.h>
#include <string.h>

/* port/linux/src/port_config.c */
int config_boolean(const char *name);
long config_integer(const char *name);
const char *config_string(const char *name);
int config_write(const char *name, const char *value);
unsigned long config_changes(void);

/* render.c's and render_objects.c's, which their headers do not declare */
extern boolean render_contrails_enabled;
extern boolean render_shadows;

/* main.c's: the views the frame draws; players.c's: the local players, 0 before its globals are made */
short main_get_window_count(void);
short players_port_local_player_count(void);

/* the modes, in the order of each setting's values below */
static char const *const graphics_modes[] = { "performance", "balanced", "quality" };

/* each setting a mode sets, and its value in each mode. Performance leaves out what the console measurements found
costs the game thread most for what it shows (runs/wiiu/session-20261030/ab); quality draws 1080 lines at 30 frames a
second, which the GPU has the time for when the game thread does not (2026-10-30: about 4 ms of the GPU's time a frame
at 720 lines in Blood Gulch), and dynamic resolution draws fewer where it has not (the menu at 1080 lines took 27 ms). */
static struct
{
	char const *setting;
	char const *values[NUMBEROF(graphics_modes)];
} const graphics_mode_values[] =
{
	{ "display.render_lines", { "720", "720", "1080" } },
	{ "display.dynamic_resolution", { "false", "false", "true" } },
	{ "display.interpolation", { "true", "true", "false" } },
	{ "display.dynamic_quality", { "true", "false", "false" } },
	{ "display.grass", { "false", "true", "true" } },
	{ "display.decals", { "true", "true", "true" } },
	{ "display.particles", { "true", "true", "true" } },
	{ "display.object_shadows", { "false", "true", "true" } },
	{ "display.lens_flares", { "false", "true", "true" } },
	{ "display.specular", { "false", "true", "true" } },
	{ "display.bump_mapping", { "true", "true", "true" } },
};

static short graphics_mode_index(char const *mode)
{
	short index;

	for (index = 0; mode && index < NUMBEROF(graphics_modes); index++)
	{
		if (!strcmp(mode, graphics_modes[index]))
			return index;
	}
	return NONE;
}

/* whether the setting holds the value as written in the table: a boolean's, a number's or a string's */
static int graphics_setting_is(char const *setting, char const *value)
{
	if (!strcmp(value, "true") || !strcmp(value, "false"))
		return !config_boolean(setting) == !strcmp(value, "false");
	if (value[0] >= '0' && value[0] <= '9')
		return config_integer(setting) == strtol(value, NULL, 10);
	return config_string(setting) && !strcmp(config_string(setting), value);
}

/* the views the frame draws: main_get_window_count's, asked only once a map has made a player (it reads the players'
and the cinematics' globals; a frame drawn before them, as by the loop a fatal error leaves the game in, has one) */
static short graphics_quality_views(void)
{
	return players_port_local_player_count() > 0 ? main_get_window_count() : 1;
}

#ifdef HALO_WIIU
/* the views from which display.split_screen_detail and "auto" frame rate take effect */
#define SPLIT_SCREEN_MANY_VIEWS 3

/* whether the frames are held to 30 a second, as the views last drawn ask (graphics_quality_split_screen_capped) */
static boolean graphics_quality_capped = FALSE;

/* whether the views draw the reduced detail: three or four, unless display.split_screen_detail is "same" */
static boolean graphics_quality_reduced(short views)
{
	return views >= SPLIT_SCREEN_MANY_VIEWS && strcmp(config_string("display.split_screen_detail"), "same") != 0;
}

/* whether the views are held to 30 frames a second: display.split_screen_fps "auto" with three or four, "30" with two or
more, "60" never */
static boolean graphics_quality_split_screen_cap(short views)
{
	char const *fps = config_string("display.split_screen_fps");

	if (!strcmp(fps, "60"))
		return FALSE;
	if (!strcmp(fps, "30"))
		return views >= 2;
	return views >= SPLIT_SCREEN_MANY_VIEWS;
}

int graphics_quality_split_screen_capped(void)
{
	return graphics_quality_capped;
}
#endif

void graphics_quality_apply(void)
{
	static unsigned long read_at = (unsigned long)-1;
	static short views_at = NONE, step_at = NONE;
	short views = graphics_quality_views();
	short step = (short)render_governor_step();
	boolean particles, specular, reduced = FALSE;
	struct render_governor_values const *shed = render_governor_values();

	if (read_at == config_changes() && views_at == views && step_at == step)
		return;
	read_at = config_changes();
	views_at = views;
	step_at = step;
#ifdef HALO_WIIU
	reduced = graphics_quality_reduced(views);
	graphics_quality_capped = graphics_quality_split_screen_cap(views);
#endif
	/* (and the dynamic quality's step, render_governor.c: what the frames' time has it leave out for now, only ever less than the settings draw) */
	rasterizer_debug_options.draw_detail_objects = config_boolean("display.grass") != 0 && shed->grass;
	decals_enabled = config_boolean("display.decals") != 0;
	particles = config_boolean("display.particles") != 0;
	render_particles_enabled = particles;
	render_particle_systems_enabled = particles;
	render_contrails_enabled = particles; /* per-trail governor filtering preserves local gunner feedback */
	/* (and the reduced split screen's: what each view draws again, at a cost that grows with what it sees) */
	render_shadows = config_boolean("display.object_shadows") != 0 && !reduced && shed->shadow_min_pixels < 1e8f;
	rasterizer_debug_options.draw_lens_flares = config_boolean("display.lens_flares") != 0 && !reduced && shed->lens_flares;
	specular = config_boolean("display.specular") != 0 && !reduced && shed->specular;
	rasterizer_debug_options.draw_environment_specular_lights = specular;
	rasterizer_debug_options.draw_environment_reflections = specular;
	rasterizer_debug_options.draw_environment_reflection_mirrors = specular;
	rasterizer_debug_options.bump_mapping_enabled = config_boolean("display.bump_mapping") != 0;
}

int graphics_quality_mode_setting(char const *name)
{
	short index;

	for (index = 0; name && index < NUMBEROF(graphics_mode_values); index++)
	{
		if (!strcmp(name, graphics_mode_values[index].setting))
			return 1;
	}
	return 0;
}

int graphics_quality_mode_write(char const *mode)
{
	short mode_index = graphics_mode_index(mode);
	short index;
	int written = 1;

	if (mode_index == NONE)
		return 1;
	for (index = 0; index < NUMBEROF(graphics_mode_values); index++)
	{
		if (!graphics_setting_is(graphics_mode_values[index].setting, graphics_mode_values[index].values[mode_index]) &&
			!config_write(graphics_mode_values[index].setting, graphics_mode_values[index].values[mode_index]))
		{
			written = 0;
		}
	}
	return written;
}

int graphics_quality_mode_matches(char const *mode)
{
	short mode_index = graphics_mode_index(mode);
	short index;

	if (mode_index == NONE)
		return 1;
	for (index = 0; index < NUMBEROF(graphics_mode_values); index++)
	{
		if (!graphics_setting_is(graphics_mode_values[index].setting, graphics_mode_values[index].values[mode_index]))
			return 0;
	}
	return 1;
}
