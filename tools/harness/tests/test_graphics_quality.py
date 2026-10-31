"""The graphics modes and detail settings (port/linux/game/graphics_quality.c): a mode writes its values and then matches, the
settings reach the engine's switches when config.toml has changed and not otherwise (a console command holds until then), and an
unknown or custom mode writes nothing."""
import re
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import function, mutated, read

FAULTS = {
    "kept": None,
    # (applied every frame, over a console command's value)
    "always_applied": ("if (read_at == config_changes())\n\t\treturn;", "if (read_at == config_changes() && 0)\n\t\treturn;"),
    # (a mode's value not written when the setting differs)
    "no_write": ("!config_write(graphics_mode_values[index].setting", "!(1 || config_write(graphics_mode_values[index].setting"),
}


def tables(source):
    """the modes' names and their table, as the file has them"""
    start = source.index("static char const *const graphics_modes[]")
    end = source.index("};", source.index("graphics_mode_values[] =")) + 2
    return source[start:end]


@pytest.mark.parametrize("fault", list(FAULTS))
def test_graphics_modes_and_detail_settings(tmp_path, fault):
    source = read("port/linux/game/graphics_quality.c")
    if FAULTS[fault]:
        before, after = FAULTS[fault]
        source = mutated(source, before, after)
        if fault == "no_write":
            source = source.replace(".values[mode_index]))\n\t\t{\n\t\t\twritten = 0;", ".values[mode_index])))\n\t\t{\n\t\t\twritten = 0;", 1)
    fixture = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NUMBEROF(a) ((short)(sizeof(a) / sizeof((a)[0])))
#define NONE (-1)
typedef unsigned char boolean;
/* the engine's switches the settings set */
static struct { boolean draw_detail_objects, draw_lens_flares, draw_environment_specular_lights, draw_environment_reflections,
	draw_environment_reflection_mirrors, bump_mapping_enabled; } rasterizer_debug_options;
static boolean decals_enabled, render_particles_enabled, render_particle_systems_enabled, render_contrails_enabled, render_shadows;
/* config.toml: names and their text, as the settings were */
static struct { const char *name; char value[32]; } settings[] = {
	{ "display.graphics_mode", "balanced" }, { "display.render_lines", "720" }, { "display.interpolation", "true" },
	{ "display.grass", "true" }, { "display.decals", "true" }, { "display.particles", "true" }, { "display.object_shadows", "true" },
	{ "display.lens_flares", "true" }, { "display.specular", "true" }, { "display.bump_mapping", "true" },
};
static unsigned long changes = 1;
static int writes;
static char *setting(const char *name)
{
	size_t i;
	for (i = 0; i < sizeof(settings) / sizeof(settings[0]); i++)
		if (!strcmp(settings[i].name, name)) return settings[i].value;
	return NULL;
}
static int config_boolean(const char *name) { return !strcmp(setting(name), "true"); }
static long config_integer(const char *name) { return strtol(setting(name), NULL, 10); }
static const char *config_string(const char *name) { return setting(name); }
static int config_write(const char *name, const char *value)
{
	char *place = setting(name);
	if (!place) return 0;
	if (strcmp(place, value)) { snprintf(place, 32, "%s", value); changes++; }
	writes++;
	return 1;
}
static unsigned long config_changes(void) { return changes; }
'''
    code = tables(source) + "\n" + "\n".join(function(source, name) for name in (
        "graphics_mode_index", "graphics_setting_is", "graphics_quality_apply", "graphics_quality_mode_setting",
        "graphics_quality_mode_write", "graphics_quality_mode_matches"))
    code = re.sub(r"^(int|void) (graphics_quality_\w+)\(", r"static \1 \2(", code, flags=re.M)
    checks = r'''
int main(void)
{
	/* the stock settings: all drawn */
	graphics_quality_apply();
	if (!rasterizer_debug_options.draw_detail_objects || !decals_enabled || !render_shadows || !render_contrails_enabled) return 1;
	if (!graphics_quality_mode_matches("balanced") || graphics_quality_mode_matches("performance")) return 2;
	/* performance written: its values, then the switches at the next frame */
	if (!graphics_quality_mode_write("performance") || !graphics_quality_mode_matches("performance")) return 3;
	if (strcmp(setting("display.grass"), "false") || strcmp(setting("display.render_lines"), "720")) return 4;
	graphics_quality_apply();
	if (rasterizer_debug_options.draw_detail_objects || render_shadows || rasterizer_debug_options.draw_lens_flares) return 5;
	if (rasterizer_debug_options.draw_environment_specular_lights || rasterizer_debug_options.draw_environment_reflections ||
		rasterizer_debug_options.draw_environment_reflection_mirrors) return 6;
	if (!decals_enabled || !render_particles_enabled || !rasterizer_debug_options.bump_mapping_enabled) return 7;
	/* a console command's value holds until a setting changes */
	rasterizer_debug_options.draw_detail_objects = 1;
	graphics_quality_apply();
	if (!rasterizer_debug_options.draw_detail_objects) return 8;
	/* quality: 1080 lines at 30 frames, all drawn */
	if (!graphics_quality_mode_write("quality") || strcmp(setting("display.render_lines"), "1080") ||
		strcmp(setting("display.interpolation"), "false")) return 9;
	graphics_quality_apply();
	if (!rasterizer_debug_options.draw_detail_objects || !render_shadows || !rasterizer_debug_options.draw_lens_flares) return 10;
	/* custom and an unknown mode write nothing and match whatever is set */
	writes = 0;
	if (!graphics_quality_mode_write("custom") || !graphics_quality_mode_write("ultra") || writes) return 11;
	if (!graphics_quality_mode_matches("custom")) return 12;
	/* the settings a mode sets, and not others */
	if (!graphics_quality_mode_setting("display.specular") || graphics_quality_mode_setting("display.fov") ||
		graphics_quality_mode_setting("display.graphics_mode")) return 13;
	return 0;
}
'''
    test_file = tmp_path / "graphics_quality.c"
    executable = tmp_path / "graphics_quality"
    test_file.write_text(fixture + code + checks)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", str(test_file), "-o", str(executable)],
                   check=True)
    result = subprocess.run([str(executable)], check=False)
    if fault == "kept":
        assert result.returncode == 0
    else:
        assert result.returncode != 0
