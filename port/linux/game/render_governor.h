/*
RENDER_GOVERNOR.H

The Wii U's dynamic quality: when the game thread's frames run longer than a target (22 ms, 45 frames a second), the governor
sheds optional drawing a step at a time, cheapest loss of picture first, and puts it back with hysteresis when there is
headroom. It changes only what is drawn, never a tick (decals and particles are still made; only their drawing is thinned).
The steps are a table (render_governor.c), 0 being everything the settings ask for.

On with display.dynamic_quality (a graphics mode sets it) or a first line "1" / "on" in halo/governor.txt; the file also tunes it
(render_governor.c has the keys: target, step to force a step for a test, window, the dwells, and sN.name to change a value of
step N). Off, every value is the neutral one and nothing differs from a game without the governor. Platform layer: the Wii U
(HALO_WIIU); elsewhere the governor is off and the values are neutral.
*/

#ifndef RENDER_GOVERNOR_H
#define RENDER_GOVERNOR_H

enum
{
	RENDER_GOVERNOR_STEPS = 8,
};

/* what a step sheds. The neutral values (step 0): the pixel thresholds 0, the scales 1, keep 16 of 16, everything on. */
struct render_governor_values
{
	char const *name;
	/* objects of a kind whose bounding sphere is under this many pixels (the Xbox's 480-line screen) are not drawn at all (nor lit):
	scenery and the devices (the level's own furniture), items (weapons, equipment, garbage), units (bipeds, vehicles); projectiles never */
	float cull_scenery_pixels, cull_item_pixels, cull_unit_pixels;
	/* multiplies the pixel size a model's level of detail is chosen by: below 1, lower levels sooner */
	float lod_scale;
	/* objects under this many pixels cast no shadow (the stock minimum is 30) */
	float shadow_min_pixels;
	/* decals farther than this many world units from the camera are not drawn (0: no limit), and of the rest 'keep' of every 16, by decal */
	float decal_radius;
	int decal_keep;
	/* multiplies the pixel size under which a particle is not drawn, and 'keep' of every 16 of the rest, by particle */
	float particle_lod_scale;
	int particle_keep;
	/* the detail objects (grass), lens flares, the level's specular shine and reflections, and the contrails (the shots' tracers) drawn */
	int grass, lens_flares, specular, contrails;
};

/* what the drawing counted for the governor's log (a frame's average is a count over the frames since the last line) */
enum
{
	RENDER_GOVERNOR_COUNT_OBJECTS_DRAWN,
	RENDER_GOVERNOR_COUNT_OBJECTS_CULLED,
	RENDER_GOVERNOR_COUNT_SHADOWS_DRAWN,
	RENDER_GOVERNOR_COUNT_SHADOWS_SKIPPED,
	RENDER_GOVERNOR_COUNT_DECALS_DRAWN,
	RENDER_GOVERNOR_COUNT_DECALS_SKIPPED,
	RENDER_GOVERNOR_COUNT_PARTICLES_DRAWN,
	RENDER_GOVERNOR_COUNT_PARTICLES_SKIPPED,
	RENDER_GOVERNOR_COUNTS,
};
extern unsigned long render_governor_counts[RENDER_GOVERNOR_COUNTS];

/* the governor is on, and the step it holds (0 when it is off) */
int render_governor_active(void);
int render_governor_step(void);

/* the scale of the pixel size a model's level of detail is chosen by (the values' lod_scale; 1 with the governor off): the Direct3D layer's
halo_model_detail_scale asks for it, as that layer is built without this header */
float render_governor_lod_scale(void);

/* the values to draw by now: the current step's, or the neutral ones when the governor is off */
struct render_governor_values const *render_governor_values(void);

/* once a frame, from Present: the frame's work in milliseconds (the game thread's time between two presents, less the present), the part of it that was
ticks and the part that was rendering, and the time in milliseconds. Decides the step, logs. */
void render_governor_frame(double work_ms, double tick_ms, double render_ms, double now_ms);

/* ---------- the platform layer's */

/* halo/governor.txt's text (one setting a line, or separated by semicolons); 0 if there is none. Wii U: the card; the digest harness: HALO_GOVERNOR */
int render_governor_settings_text(char *text, unsigned long size);

/* the game thread's milliseconds in each of the render passes since the last call, a window's average (wiiu_render_passes.c); the count of windows */
unsigned long render_governor_pass_milliseconds(double milliseconds[13]);

#endif
