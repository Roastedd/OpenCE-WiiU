/*
GRAPHICS_QUALITY.H

The detail settings (display.grass, display.decals, display.particles, display.object_shadows, display.lens_flares,
display.specular, display.bump_mapping) set on the engine's own switches, and the graphics modes
(display.graphics_mode: performance, balanced, quality, custom), each of which is a set of values for them and for
display.render_lines and display.interpolation; and the Wii U's split screen detail and frame rate with three or four
views (display.split_screen_detail, display.split_screen_fps) (graphics_quality.c).
*/

#ifndef HALO_GRAPHICS_QUALITY_H
#define HALO_GRAPHICS_QUALITY_H

/* The detail settings onto the engine's switches when config.toml's settings or the number of views drawn have changed (once a frame:
rasterizer_frame_begin); on the Wii U, with three or four views, less display.split_screen_detail's reduced set. */
void graphics_quality_apply(void);

#ifdef HALO_WIIU
/* Whether the views last drawn hold the frames to 30 a second (display.split_screen_fps: "auto" with three or four, "30" with two or more),
as display.interpolation off does, without changing it (platform_wiiu.c's halo_interpolation_enabled). */
int graphics_quality_split_screen_capped(void);
#endif

/* whether the setting is one a graphics mode sets */
int graphics_quality_mode_setting(char const *name);

/* The mode's values (performance, balanced or quality) written to its settings; nothing for custom or a mode not known. Returns 0 if a write failed. */
int graphics_quality_mode_write(char const *mode);

/* whether the settings hold the mode's values (custom always does) */
int graphics_quality_mode_matches(char const *mode);

#endif
