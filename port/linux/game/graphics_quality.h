/*
GRAPHICS_QUALITY.H

The detail settings (display.grass, display.decals, display.particles, display.object_shadows, display.lens_flares,
display.specular, display.bump_mapping) set on the engine's own switches, and the graphics modes
(display.graphics_mode: performance, balanced, quality, custom), each of which is a set of values for them and for
display.render_lines and display.interpolation (graphics_quality.c).
*/

#ifndef HALO_GRAPHICS_QUALITY_H
#define HALO_GRAPHICS_QUALITY_H

/* The detail settings onto the engine's switches when config.toml's settings have changed (once a frame: rasterizer_frame_begin). */
void graphics_quality_apply(void);

/* whether the setting is one a graphics mode sets */
int graphics_quality_mode_setting(char const *name);

/* The mode's values (performance, balanced or quality) written to its settings; nothing for custom or a mode not known. Returns 0 if a write failed. */
int graphics_quality_mode_write(char const *mode);

/* whether the settings hold the mode's values (custom always does) */
int graphics_quality_mode_matches(char const *mode);

#endif
