/*
GRAPHICS_QUALITY.C

The detail a player can trade for frame rate, each setting on one of the engine's own switches (those its console
sets: hs_globals_external.c): display.grass (the detail objects: grass, pebbles and plants), display.decals (bullet
holes, scorches and blood), display.particles (particles, particle systems and contrails), display.object_shadows (the
shadows objects cast), display.lens_flares, display.specular (the level's specular lights, reflections and mirrors) and
display.bump_mapping. They are set when config.toml's settings have changed, at the start of a frame
(rasterizer_frame_begin), so a console command (init.txt) holds until a setting is changed.

display.graphics_mode names a set of values for them and for display.render_lines (the Wii U's lines, from the next
start), display.dynamic_resolution (the Wii U's: fewer lines while the GPU falls behind, port/wiiu/src/wiiu_drs.c) and
display.interpolation: performance (the frame rate first: 60 frames a second where the game thread allows it, with what
costs it most of the detail left out), balanced (720 lines at 60, all the Xbox drew), quality (up to 1080 lines at 30
frames a second, all the Xbox drew: dynamic resolution keeps the frames in their time where 1080 lines would not) or
custom (the settings as they are). The menus write a mode's values when it is chosen, and custom when a setting a mode
sets is changed by hand (menu_functions.c), so the settings always hold what is drawn and the menus show it.
*/

#include "cseries.h"
#include "rasterizer/rasterizer_console_vars.h"
#include "effects/decals.h"
#include "render/render.h"
#include "render/render_particles.h"

#include "graphics_quality.h"

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

void graphics_quality_apply(void)
{
	static unsigned long read_at = (unsigned long)-1;
	boolean particles, specular;

	if (read_at == config_changes())
		return;
	read_at = config_changes();
	rasterizer_debug_options.draw_detail_objects = config_boolean("display.grass") != 0;
	decals_enabled = config_boolean("display.decals") != 0;
	particles = config_boolean("display.particles") != 0;
	render_particles_enabled = particles;
	render_particle_systems_enabled = particles;
	render_contrails_enabled = particles;
	render_shadows = config_boolean("display.object_shadows") != 0;
	rasterizer_debug_options.draw_lens_flares = config_boolean("display.lens_flares") != 0;
	specular = config_boolean("display.specular") != 0;
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
