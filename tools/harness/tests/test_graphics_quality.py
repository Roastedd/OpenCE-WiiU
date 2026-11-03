"""The graphics modes and detail settings (port/linux/game/graphics_quality.c, the whole file, built as the Wii U builds it
against stand-in headers): a mode writes its values and then matches, the settings reach the engine's switches when
config.toml or the number of views drawn has changed and not otherwise (a console command holds until then), quality
turns dynamic resolution on and the other modes off (its floor is no mode's), and an unknown or custom mode writes nothing. Split screen: with three or four views display.split_screen_detail "reduced" leaves
out the shadows, the lens flares and the specular shine and reflections and keeps the particles; one view (a cinematic) has
the full detail again; display.split_screen_fps holds three or four views to 30 frames a second ("auto"), two or more
("30") or none ("60"); and nothing asks the engine for its windows before a map has made a player."""
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import ROOT, mutated, read

FAULTS = {
    "kept": None,
    # (applied every frame, over a console command's value)
    "always_applied": ("if (read_at == config_changes() && views_at == views && step_at == step)\n\t\treturn;", "if (0)\n\t\treturn;"),
    # (a mode's value not written when the setting differs)
    "no_write": ("!config_write(graphics_mode_values[index].setting", "!(1 || config_write(graphics_mode_values[index].setting"),
    # (quality without dynamic resolution: 1080 lines held whatever the GPU takes)
    "quality_fixed_lines": ('{ "display.dynamic_resolution", { "false", "false", "true" } }',
                            '{ "display.dynamic_resolution", { "false", "false", "false" } }'),
    # (not applied again when the views change, only the settings)
    "views_ignored": ("if (read_at == config_changes() && views_at == views && step_at == step)", "if (read_at == config_changes() && step_at == step)"),
    # (the dynamic quality's step not taken up until a setting changes)
    "step_ignored": ("if (read_at == config_changes() && views_at == views && step_at == step)", "if (read_at == config_changes() && views_at == views)"),
    # (what a step leaves out not left out)
    "step_shed_ignored": ("config_boolean(\"display.grass\") != 0 && shed->grass;", "config_boolean(\"display.grass\") != 0;"),
    # (the step leaves a setting that is off turned on)
    "step_overrides": ("config_boolean(\"display.grass\") != 0 && shed->grass;", "shed->grass;"),
    # (three or four views drawn with every detail)
    "not_reduced": ("\treduced = graphics_quality_reduced(views);", "\treduced = FALSE;"),
    # (the particles, and the contrails that show where fire comes from, left out with the rest)
    "particles_reduced": ('particles = config_boolean("display.particles") != 0;',
                          'particles = config_boolean("display.particles") != 0 && !reduced;'),
    # (the cap held whatever the views)
    "cap_ignores_views": ("graphics_quality_capped = graphics_quality_split_screen_cap(views);",
                          "graphics_quality_capped = graphics_quality_split_screen_cap(4);"),
    # (the engine's windows asked before a map has made a player, whose globals they read)
    "no_guard": ("return players_port_local_player_count() > 0 ? main_get_window_count() : 1;",
                 "return main_get_window_count();"),
}

# the headers graphics_quality.c includes, with only what it uses of them
HEADERS = {
    "cseries.h": r'''
#define NUMBEROF(a) ((short)(sizeof(a) / sizeof((a)[0])))
#define NONE (-1)
#define TRUE 1
#define FALSE 0
typedef unsigned char boolean;
''',
    "rasterizer/rasterizer_console_vars.h": r'''
struct rasterizer_debug_options { boolean draw_detail_objects, draw_lens_flares, draw_environment_specular_lights,
	draw_environment_reflections, draw_environment_reflection_mirrors, bump_mapping_enabled; };
extern struct rasterizer_debug_options rasterizer_debug_options;
''',
    "effects/decals.h": "extern boolean decals_enabled;\n",
    "render/render.h": "",
    "render/render_particles.h": "extern boolean render_particles_enabled, render_particle_systems_enabled;\n",
    # (the dynamic quality's, which graphics_quality.c asks for the step and what it leaves out)
    "render_governor.h": r'''
struct render_governor_values { char const *name; float cull_scenery_pixels, cull_item_pixels, cull_unit_pixels, lod_scale, shadow_min_pixels,
	decal_radius; int decal_keep; float particle_lod_scale; int particle_keep; int grass, lens_flares, specular, contrails; };
int render_governor_step(void);
struct render_governor_values const *render_governor_values(void);
''',
}

FIXTURE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cseries.h"
#include "rasterizer/rasterizer_console_vars.h"
#include "graphics_quality.h"
#include "render_governor.h"

/* the dynamic quality: the step it holds and what that step leaves out (the neutral values: nothing) */
static int governor_step;
static struct render_governor_values shed = { "full", 0, 0, 0, 1, 0, 0, 16, 1, 16, 1, 1, 1, 1 };
int render_governor_step(void) { return governor_step; }
struct render_governor_values const *render_governor_values(void) { return &shed; }

/* the engine's switches the settings set */
struct rasterizer_debug_options rasterizer_debug_options;
boolean decals_enabled, render_particles_enabled, render_particle_systems_enabled, render_contrails_enabled, render_shadows;
/* config.toml: names and their text, as the settings were */
static struct { const char *name; char value[32]; } settings[] = {
	{ "display.graphics_mode", "balanced" }, { "display.render_lines", "720" }, { "display.interpolation", "true" }, { "display.dynamic_quality", "false" },
	{ "display.grass", "true" }, { "display.decals", "true" }, { "display.particles", "true" }, { "display.object_shadows", "true" },
	{ "display.lens_flares", "true" }, { "display.specular", "true" }, { "display.bump_mapping", "true" },
	{ "display.split_screen_detail", "reduced" }, { "display.split_screen_fps", "auto" },
	{ "display.dynamic_resolution", "false" }, { "display.dynamic_resolution_floor", "540" },
};
static unsigned long changes = 1;
static int writes;
/* the engine: the windows the frame draws, its local players (0 before a map has made them), and how often it was asked */
static short views = 1, players = 1;
static int views_asked;
static char *setting(const char *name)
{
	size_t i;
	for (i = 0; i < sizeof(settings) / sizeof(settings[0]); i++)
		if (!strcmp(settings[i].name, name)) return settings[i].value;
	return NULL;
}
int config_boolean(const char *name) { return !strcmp(setting(name), "true"); }
long config_integer(const char *name) { return strtol(setting(name), NULL, 10); }
const char *config_string(const char *name) { return setting(name); }
int config_write(const char *name, const char *value)
{
	char *place = setting(name);
	if (!place) return 0;
	if (strcmp(place, value)) { snprintf(place, 32, "%s", value); changes++; }
	writes++;
	return 1;
}
unsigned long config_changes(void) { return changes; }
short main_get_window_count(void) { views_asked++; return views; }
short players_port_local_player_count(void) { return players; }

#define REDUCED() (!render_shadows && !rasterizer_debug_options.draw_lens_flares && \
	!rasterizer_debug_options.draw_environment_specular_lights && !rasterizer_debug_options.draw_environment_reflections && \
	!rasterizer_debug_options.draw_environment_reflection_mirrors)
#define FULL() (render_shadows && rasterizer_debug_options.draw_lens_flares && rasterizer_debug_options.draw_environment_specular_lights && \
	rasterizer_debug_options.draw_environment_reflections && rasterizer_debug_options.draw_environment_reflection_mirrors)

int main(void)
{
	/* the stock settings: all drawn */
	graphics_quality_apply();
	if (!rasterizer_debug_options.draw_detail_objects || !decals_enabled || !render_shadows || !render_contrails_enabled) return 1;
	if (!graphics_quality_mode_matches("balanced") || graphics_quality_mode_matches("performance")) return 2;
	/* performance written: its values, then the switches at the next frame */
	if (!graphics_quality_mode_write("performance") || !graphics_quality_mode_matches("performance")) return 3;
	/* (performance holds dynamic quality on, the others off) */
	if (strcmp(setting("display.dynamic_quality"), "true")) return 40;
	if (strcmp(setting("display.grass"), "false") || strcmp(setting("display.render_lines"), "720") ||
		strcmp(setting("display.dynamic_resolution"), "false")) return 4;
	graphics_quality_apply();
	if (rasterizer_debug_options.draw_detail_objects || render_shadows || rasterizer_debug_options.draw_lens_flares) return 5;
	if (rasterizer_debug_options.draw_environment_specular_lights || rasterizer_debug_options.draw_environment_reflections ||
		rasterizer_debug_options.draw_environment_reflection_mirrors) return 6;
	if (!decals_enabled || !render_particles_enabled || !rasterizer_debug_options.bump_mapping_enabled) return 7;
	/* a console command's value holds until a setting changes */
	rasterizer_debug_options.draw_detail_objects = 1;
	graphics_quality_apply();
	if (!rasterizer_debug_options.draw_detail_objects) return 8;
	/* quality: up to 1080 lines at 30 frames, with dynamic resolution, all drawn; its floor is left as it was */
	if (!graphics_quality_mode_write("quality") || strcmp(setting("display.render_lines"), "1080") ||
		strcmp(setting("display.interpolation"), "false") || strcmp(setting("display.dynamic_resolution"), "true") ||
		strcmp(setting("display.dynamic_resolution_floor"), "540")) return 9;
	graphics_quality_apply();
	if (!rasterizer_debug_options.draw_detail_objects || !render_shadows || !rasterizer_debug_options.draw_lens_flares) return 10;
	/* dynamic resolution turned off by hand: no longer quality (the menus then make it custom); balanced turns it off */
	config_write("display.dynamic_resolution", "false");
	if (graphics_quality_mode_matches("quality")) return 14;
	if (!graphics_quality_mode_write("balanced") || strcmp(setting("display.dynamic_resolution"), "false") ||
		!graphics_quality_mode_matches("balanced")) return 15;
	/* custom and an unknown mode write nothing and match whatever is set */
	writes = 0;
	if (!graphics_quality_mode_write("custom") || !graphics_quality_mode_write("ultra") || writes) return 11;
	if (!graphics_quality_mode_matches("custom")) return 12;
	/* the settings a mode sets, and not others (not the split screen's) */
	if (!graphics_quality_mode_setting("display.specular") || graphics_quality_mode_setting("display.fov") ||
		graphics_quality_mode_setting("display.graphics_mode") || graphics_quality_mode_setting("display.split_screen_detail") ||
		graphics_quality_mode_setting("display.split_screen_fps") || !graphics_quality_mode_setting("display.dynamic_resolution") ||
		graphics_quality_mode_setting("display.dynamic_resolution_floor")) return 13;

	/* split screen: balanced, then four views with no setting changed */
	if (!graphics_quality_mode_write("balanced")) return 19;
	graphics_quality_apply();
	if (!FULL() || graphics_quality_split_screen_capped()) return 20;
	players = 4;
	views = 4;
	graphics_quality_apply();
	if (!REDUCED()) return 21;
	if (!render_particles_enabled || !render_particle_systems_enabled || !render_contrails_enabled || !decals_enabled ||
		!rasterizer_debug_options.bump_mapping_enabled || !rasterizer_debug_options.draw_detail_objects) return 22;
	if (!graphics_quality_split_screen_capped()) return 23;
	/* a console command holds while neither the settings nor the views change */
	render_shadows = 1;
	graphics_quality_apply();
	if (!render_shadows) return 24;
	/* a cinematic: one view, the full detail and display.interpolation's frame rate */
	views = 1;
	graphics_quality_apply();
	if (!FULL() || graphics_quality_split_screen_capped()) return 25;
	/* three views as four; two as one ("auto") */
	views = 3;
	graphics_quality_apply();
	if (!REDUCED() || !graphics_quality_split_screen_capped()) return 26;
	views = 2;
	graphics_quality_apply();
	if (!FULL() || graphics_quality_split_screen_capped()) return 27;
	/* "30": two views held too; "60": four views not held, and still reduced */
	config_write("display.split_screen_fps", "30");
	graphics_quality_apply();
	if (!graphics_quality_split_screen_capped() || !FULL()) return 28;
	config_write("display.split_screen_fps", "60");
	views = 4;
	graphics_quality_apply();
	if (graphics_quality_split_screen_capped() || !REDUCED()) return 29;
	/* "same": four views draw what one does; "auto" still holds them to 30 */
	config_write("display.split_screen_detail", "same");
	config_write("display.split_screen_fps", "auto");
	graphics_quality_apply();
	if (!FULL() || !graphics_quality_split_screen_capped()) return 30;
	/* a setting off stays off whatever the views, the others as the views have them */
	config_write("display.split_screen_detail", "reduced");
	config_write("display.object_shadows", "false");
	for (views = 1; views <= 4; views++)
	{
		graphics_quality_apply();
		if (render_shadows || !rasterizer_debug_options.draw_lens_flares != (views >= 3)) return 31;
	}
	/* the dynamic quality's steps: what a step leaves out is off whatever the settings, and only at the frame the step changes (a console command holds
	while neither the settings, the views nor the step change); the step back puts it all back */
	config_write("display.object_shadows", "true");
	views = 1;
	graphics_quality_apply();
	if (!FULL() || !rasterizer_debug_options.draw_detail_objects || !render_contrails_enabled) return 33;
	shed.grass = 0;
	graphics_quality_apply();
	if (!rasterizer_debug_options.draw_detail_objects) return 34; /* (the step has not changed) */
	governor_step = 1;
	graphics_quality_apply();
	if (rasterizer_debug_options.draw_detail_objects || !render_contrails_enabled || !render_shadows) return 35;
	shed.lens_flares = shed.specular = shed.contrails = 0;
	shed.shadow_min_pixels = 1e9f;
	governor_step = 2;
	graphics_quality_apply();
	if (!REDUCED() || render_contrails_enabled || !render_particles_enabled || !decals_enabled || !rasterizer_debug_options.bump_mapping_enabled) return 36;
	/* (shadows are left out by a minimum size over what any object is; one under it leaves them drawn: the objects' own test) */
	shed.shadow_min_pixels = 60.f;
	governor_step = 3;
	graphics_quality_apply();
	if (!render_shadows) return 37;
	/* back to the full picture: step 0 and the neutral values */
	shed.grass = shed.lens_flares = shed.specular = shed.contrails = 1;
	shed.shadow_min_pixels = 0.f;
	governor_step = 0;
	graphics_quality_apply();
	if (!FULL() || !rasterizer_debug_options.draw_detail_objects || !render_contrails_enabled) return 38;
	/* a setting off stays off at every step */
	config_write("display.grass", "false");
	graphics_quality_apply();
	governor_step = 2;
	graphics_quality_apply();
	governor_step = 0;
	graphics_quality_apply();
	if (rasterizer_debug_options.draw_detail_objects) return 39;
	config_write("display.grass", "true");
	graphics_quality_apply();
	/* before a map's players: one view, and the engine's windows not asked */
	config_write("display.object_shadows", "true");
	players = 0;
	views = 4;
	views_asked = 0;
	graphics_quality_apply();
	if (!FULL() || graphics_quality_split_screen_capped() || views_asked) return 32;
	return 0;
}
'''


@pytest.mark.parametrize("fault", list(FAULTS))
def test_graphics_modes_and_detail_settings(tmp_path, fault):
    source = read("port/linux/game/graphics_quality.c")
    if FAULTS[fault]:
        before, after = FAULTS[fault]
        source = mutated(source, before, after)
        if fault == "no_write":
            source = source.replace(".values[mode_index]))\n\t\t{\n\t\t\twritten = 0;", ".values[mode_index])))\n\t\t{\n\t\t\twritten = 0;", 1)
    for name, text in HEADERS.items():
        (tmp_path / name).parent.mkdir(parents=True, exist_ok=True)
        (tmp_path / name).write_text(text)
    (tmp_path / "graphics_quality.c").write_text(source)
    (tmp_path / "checks.c").write_text(FIXTURE)
    executable = tmp_path / "graphics_quality"
    # (a fault may leave what it bypasses set and never read, which is no warning to fail on)
    relaxed = ["-Wno-unused-but-set-variable", "-Wno-unused-variable"] if FAULTS[fault] else []
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", *relaxed, "-DHALO_WIIU",
                    "-I", str(tmp_path), "-I", str(ROOT / "port/linux/game"), str(tmp_path / "graphics_quality.c"),
                    str(tmp_path / "checks.c"), "-o", str(executable)], check=True)
    result = subprocess.run([str(executable)], check=False)
    if fault == "kept":
        assert result.returncode == 0
    else:
        assert result.returncode != 0
