/*
RENDER_GOVERNOR.C

The Wii U's dynamic quality (render_governor.h). The game thread's frame (the work between two presents: the ticks, the drawing, the rest of the loop) is
measured over windows of half a second; when its mean is over the target (22 ms by default, 45 frames a second) for two windows the governor goes down a
step of the table below, and when the mean plus what the current step saves would be well under the target (88%) for six windows it goes up one. A step
is not changed again within its dwell (one second down, eight up; half a second down when the frame is 40% over), and a step up that has to be taken back
within twenty seconds doubles the wait for the next one (to eight times), so that a scene on the edge settles on the lower step and does not flicker.
Frames that took over a quarter of a second (a load, a save, a first use) are left out of the means and the rest are counted at most three targets, so a
hitch does not shed detail.

The steps shed, in this order of picture lost for time saved, only what is drawn. Nothing here touches a tick, a random number or the game state; a
decal or a particle that is not drawn is still made and ages as it would. Where the work is done:
- objects under a size are not drawn (render_objects.c's render_object, before they are lit), by their kind;
- the model's level of detail is chosen at a smaller size (halo_model_detail_scale in the Direct3D layer);
- shadows of the small (render_objects.c) and then none; the detail objects, lens flares and specular shine are put on the engine's own switches
  (graphics_quality.c, which asks for the step each frame);
- decals far away or thinned (rasterizer_xbox_decals.c) and particles small or thinned (render_particles.c).

halo/governor.txt (HALO_GOVERNOR on the host; a file whose first word is -1 or "default" is the same as no file: the default, which is the governor on for a map of one local
player and no network, smooth_defaults.h): a first word on/off/1/0 turns it on or off (on only for such a map; off for every map; display.dynamic_quality otherwise); then, one to a line or
separated by semicolons, "name value": target (ms), step (hold this step, for a test: the governor then does not move), max_step, window (ms), down_dwell,
up_dwell (ms), blend (ms a step change glides over, 1500; 0 is all at once), down_windows, up_windows, up_margin (the share of the target the frame must be under, with the step's saving added), min_render (ms of drawing a frame
under which the governor does not step down: the frame is the ticks', 4), tick_bound (ms a frame waited for the tick thread, 3, and tick_bound_share, 0.35 of the
frame: over both, the frame is the tick thread's and the governor does not step down), quiet (ms after a map starts that decide nothing, 20000), probe (ms under
the target before a step up is tried whatever the estimate, 20000), worst (a window with a frame over this many ms decides nothing, 80), log (ms between lines; 0 for none) and late_ms, late_share and late_up_share (below), and sN.name for N the step (0 to 7) and name one of cull_scenery, cull_item, cull_unit, lod, shadow, decal_radius, decal_keep, particle_lod,
particle_keep, decal_max, particle_max, grass, lens_flares, specular, contrails, save (the ms the step is estimated to save, for the step-up test).

The late-frame rule (late_ms, enabled by default in Performance mode: the rule above is what runs). A window's mean hides what a display that shows a frame for one refresh or two feels: frames of 12 and 24 ms
mean 18 and are half late. With "late_ms 16" the governor counts, in each window, the share of frames whose work was over 16 ms (a frame under that is on time at 60 a second) and decides
on the share: it goes down when the share is over late_share (0.20 by default) in as many windows as before, with the same dwells, drawing and tick-thread conditions, and goes up when the
share of frames that would still be on time with the step's drawing put back (each frame's work plus the step's save, against late_ms) is at most late_up_share (0.08 by default) for as many
windows as before, or, for the slow probe, when the share is at most a quarter of that for as long. late_ms 0 is the rule above; target does not matter to it except that a window whose mean
is 40% over the target is still "severe", and the hitch rules are the same. The share is said in the governor's log line (late frames, whichever rule decides).
*/

#include "render_governor.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned long render_governor_counts[RENDER_GOVERNOR_COUNTS];
unsigned long render_governor_budget_used[2];

#ifdef HALO_WIIU

void platform_log(char const *format, ...);
int config_boolean(const char *name);
const char *config_string(const char *name);
unsigned long config_changes(void);
/* tick_overlap.c: the milliseconds the game thread has waited for the tick thread, in all (0 without the overlap) */
double tick_overlap_wait_ms_total(void);
unsigned char game_time_initialized(void);
unsigned char game_time_get_paused(void);

#define STEPS RENDER_GOVERNOR_STEPS

/* The ladder. Each row is the step's values (render_governor.h) and the milliseconds a frame it is estimated to save of the one before it in a fight on
b30 (reports/research/2026-11-03/governor.md has how: 80% of what the host's draw counts say); the step-up test uses the saving, so an estimate too small only waits longer. */
static struct render_governor_values const default_values[STEPS] =
{
	/* Steps 1 to 3 are the ones played with by default: they leave out only what is small, far or thin, never a unit, a vehicle or a projectile.
	Steps 4 to 7 cull units and items by size (at about half the sizes the first version used, so that the pop-in happens farther away: a Truth and Reconciliation run spent 61% of its frames at steps 5 to 7) and are for the Performance graphics mode, or max_step in the card file. */
	/* name           scenery item unit  lod   shadow  decal: radius keep  particle: lod keep  grass flares specular contrails  decal_max particle_max */
	/* 0 */ { "full",          0.f,  0.f,  0.f, 1.00f,   0.f,   0.f, 16, 1.00f, 16, 1, 1, 1, 1,   0,   0 },
	/* 1 */ { "far props",    10.f,  0.f,  0.f, 1.00f,  45.f, 120.f, 14, 1.25f, 14, 1, 1, 1, 1, 200, 300 },
	/* 2 */ { "small things", 16.f,  0.f,  0.f, 0.85f,  60.f,  90.f, 12, 1.50f, 12, 1, 1, 1, 1, 130, 200 },
	/* 3 */ { "grass, flares", 22.f, 0.f,  0.f, 0.70f,  80.f,  60.f,  9, 2.00f,  8, 0, 0, 1, 1, 100, 140 },
	/* 4 */ { "shine, units", 24.f, 14.f,  3.f, 0.55f, 110.f,  40.f,  6, 3.00f,  6, 0, 0, 0, 1,  70, 100 },
	/* 5 */ { "no shadows",   28.f, 20.f,  5.f, 0.45f,  1e9f,  25.f,  4, 4.00f,  4, 0, 0, 0, 0,  50,  70 },
	/* 6 */ { "minimum",      34.f, 26.f,  8.f, 0.35f,  1e9f,  15.f,  2, 6.00f,  2, 0, 0, 0, 0,  30,  40 },
	/* 7 */ { "emergency",    42.f, 32.f, 12.f, 0.25f,  1e9f,  10.f,  1, 8.00f,  1, 0, 0, 0, 0,  15,  20 },
};
static float const default_save_ms[STEPS] = { 0.f, 1.0f, 2.2f, 1.2f, 0.8f, 1.8f, 1.3f, 1.2f };
/* the most steps the governor goes down by default (the card file's max_step; the Performance mode: all) */
#define DEFAULT_MAX_STEP 3

static struct render_governor_values values[STEPS];
static float save_ms[STEPS];

static struct
{
	int loaded;
	unsigned long config_at;
	int configured_on, card_on, card_off; /* the setting, and the card file's word (neither: the default) */
	int forced;                         /* the step held (-1: the governor decides) */
	int max_step, max_step_set, performance_mode, frame_rule_set;
	double blend_ms;
	double target_ms, window_ms, down_dwell_ms, up_dwell_ms, up_margin, log_ms, min_render_ms, tick_bound_ms, tick_bound_share, quiet_ms, probe_ms, worst_ms;
	int down_windows, up_windows;
	/* the late-frame rule: a frame is late over late_ms (0: the rule is off); the share of late frames that takes a step down, and the share with the step put back that allows a step up */
	double late_ms, late_share, late_up_share;
} settings;

static struct
{
	int step;
	double window_start, last_change, last_log;
	double sum_work, sum_tick, sum_render;
	unsigned long frames, hitches;
	int over_windows, under_windows, probe_windows;
	double sum_wait, wait_total_at, window_worst;
	unsigned long window_long; /* frames of the window over worst (80 ms): a hitch is a few of them, a game whose ticks fill the frame is most */
	double quiet_until;
	int new_map_pending;
	int up_backoff;
	int last_change_was_up;
	unsigned long downs, ups;
	unsigned long counts_at[RENDER_GOVERNOR_COUNTS];
	unsigned long frames_in_log;
	double log_work, log_tick, log_render;
	double log_worst;
	/* frames of the window over late_ms, and over it with the current step's save added (late_ms 0: not counted); of the log's interval, the frames whose work was over a refresh (17.5 ms) */
	unsigned long late, late_with_step_up, log_late;
} governor;

static void settings_defaults(void)
{
	memcpy(values, default_values, sizeof(values));
	memcpy(save_ms, default_save_ms, sizeof(save_ms));
	settings.forced = -1;
	settings.max_step = DEFAULT_MAX_STEP;
	settings.max_step_set = 0;
	settings.target_ms = 22.0;
	settings.blend_ms = 1500.0;
	settings.window_ms = 500.0;
	settings.down_dwell_ms = 2000.0;
	settings.up_dwell_ms = 8000.0;
	settings.up_margin = 0.88;
	settings.log_ms = 5000.0;
	settings.min_render_ms = 4.0;
	settings.tick_bound_ms = 3.0;
	settings.tick_bound_share = 0.35;
	settings.quiet_ms = 20000.0;
	settings.probe_ms = 20000.0;
	settings.worst_ms = 80.0;
	settings.down_windows = 2;
	settings.up_windows = 6;
	settings.late_ms = 0.0;
	settings.late_share = 0.20;
	settings.late_up_share = 0.08;
	settings.card_on = settings.card_off = 0;
}

static int settings_value(char const *name, double number)
{
	if (!strcmp(name, "target") && number >= 5.0 && number <= 200.0)
	{
		/* (a frame is a refresh of the display at the least, 16.7 ms: a target under 20 cannot be kept, and a governor asked for it would give up the picture
		for nothing) */
		settings.frame_rule_set = 1;
		settings.target_ms = number < 20.0 ? 20.0 : number;
		if (number < 20.0)
			platform_log("governor: target %.1f ms is under 20 (a refresh is 16.7): using 20", number);
	}
	else if (!strcmp(name, "late_ms") && (number == 0.0 || (number >= 12.0 && number <= 100.0)))
	{
		settings.frame_rule_set = 1;
		settings.late_ms = number;
	}
	else if (!strcmp(name, "late_share") && number > 0.0 && number <= 1.0)
		settings.late_share = number;
	else if (!strcmp(name, "late_up_share") && number > 0.0 && number <= 1.0)
		settings.late_up_share = number;
	else if (!strcmp(name, "step"))
		settings.forced = number >= 0.0 && number < STEPS ? (int)number : -1;
	else if (!strcmp(name, "max_step"))
	{
		settings.max_step = number >= 0.0 && number < STEPS ? (int)number : STEPS - 1;
		settings.max_step_set = 1;
	}
	else if (!strcmp(name, "tick_bound") && number >= 0.0)
		settings.tick_bound_ms = number;
	else if (!strcmp(name, "tick_bound_share") && number >= 0.0 && number <= 1.0)
		settings.tick_bound_share = number;
	else if (!strcmp(name, "quiet") && number >= 0.0)
		settings.quiet_ms = number;
	else if (!strcmp(name, "probe") && number >= 1000.0)
		settings.probe_ms = number;
	else if (!strcmp(name, "worst") && number >= 20.0)
		settings.worst_ms = number;
	else if (!strcmp(name, "window") && number >= 100.0)
		settings.window_ms = number;
	else if (!strcmp(name, "blend") && number >= 0.0)
		settings.blend_ms = number;
	else if (!strcmp(name, "down_dwell") && number >= 0.0)
		settings.down_dwell_ms = number;
	else if (!strcmp(name, "up_dwell") && number >= 0.0)
		settings.up_dwell_ms = number;
	else if (!strcmp(name, "down_windows") && number >= 1.0)
		settings.down_windows = (int)number;
	else if (!strcmp(name, "up_windows") && number >= 1.0)
		settings.up_windows = (int)number;
	else if (!strcmp(name, "up_margin") && number > 0.0 && number <= 1.0)
		settings.up_margin = number;
	else if (!strcmp(name, "log") && number >= 0.0)
		settings.log_ms = number;
	else if (!strcmp(name, "min_render") && number >= 0.0)
		settings.min_render_ms = number;
	else if (name[0] == 's' && name[1] >= '0' && name[1] < '0' + STEPS && name[2] == '.')
	{
		struct render_governor_values *step = &values[name[1] - '0'];
		char const *key = name + 3;

		if (!strcmp(key, "cull_scenery")) step->cull_scenery_pixels = (float)number;
		else if (!strcmp(key, "cull_item")) step->cull_item_pixels = (float)number;
		else if (!strcmp(key, "cull_unit")) step->cull_unit_pixels = (float)number;
		else if (!strcmp(key, "lod")) step->lod_scale = (float)number;
		else if (!strcmp(key, "shadow")) step->shadow_min_pixels = (float)number;
		else if (!strcmp(key, "decal_radius")) step->decal_radius = (float)number;
		else if (!strcmp(key, "decal_keep")) step->decal_keep = (int)number;
		else if (!strcmp(key, "particle_lod")) step->particle_lod_scale = (float)number;
		else if (!strcmp(key, "particle_keep")) step->particle_keep = (int)number;
		else if (!strcmp(key, "grass")) step->grass = number != 0.0;
		else if (!strcmp(key, "lens_flares")) step->lens_flares = number != 0.0;
		else if (!strcmp(key, "specular")) step->specular = number != 0.0;
		else if (!strcmp(key, "contrails")) step->contrails = number != 0.0;
		else if (!strcmp(key, "decal_max")) step->decal_max = (int)number;
		else if (!strcmp(key, "particle_max")) step->particle_max = (int)number;
		else if (!strcmp(key, "save")) save_ms[name[1] - '0'] = (float)number;
		else
			return 0;
	}
	else
		return 0;
	return 1;
}

/* halo/governor.txt: a first word on/off/1/0, then "name value" pairs */
static void settings_parse(char *text)
{
	char *item = text;
	int first = 1;

	/* (a file whose first word is -1 is the same as no file: a harness that must not delete a card file writes -1 to put it back) */
	{
		char word[40];

		if (sscanf(text, "%39s", word) == 1 && (!strcmp(word, "-1") || !strcmp(word, "default")))
			return;
	}
	for (; item && *item; )
	{
		char *end = item + strcspn(item, "\n;\r");
		char saved = *end;
		char name[40];
		double number = 0.0;
		char word[40];

		*end = 0;
		while (*item == ' ' || *item == '\t')
			item++;
		if (*item && *item != '#')
		{
			if (first && sscanf(item, "%39s", word) == 1 && (!strcmp(word, "on") || !strcmp(word, "1") || !strcmp(word, "off") || !strcmp(word, "0")))
			{
				settings.card_on = !strcmp(word, "on") || !strcmp(word, "1");
				settings.card_off = !settings.card_on;
				/* (a line "on 1" or "off" can go on to hold a pair) */
				if (sscanf(item, "%39s %39s %lf", word, name, &number) == 3 && !isdigit((unsigned char)name[0]))
					settings_value(name, number);
			}
			else if (sscanf(item, "%39s %lf", name, &number) == 2)
			{
				if (!settings_value(name, number))
					platform_log("governor: halo/governor.txt: not a setting: %s", name);
				else if (!settings.card_off)
					settings.card_on = 1; /* (a file that tunes it, with no "off", is the governor on) */
			}
		}
		first = 0;
		if (!saved)
			break;
		item = end + 1;
	}
}

static int governor_max_step(void);
/* the map being played is a network game (set at the map's start by game.c) */
static int online_map;
static void governor_publish(void);

static void settings_update_mode(void)
{
	char const *mode = config_string("display.graphics_mode");
	settings.performance_mode = mode && !strcmp(mode, "performance");
	if (!settings.frame_rule_set)
		settings.late_ms = settings.performance_mode ? 1000.0 / 60.0 : 0.0;
}

static void settings_load(void)
{
	char text[2048];

	settings_defaults();
	text[0] = 0;
	if (render_governor_settings_text(text, sizeof(text)) && text[0])
		settings_parse(text);
	if (settings.forced >= 0 && !settings.card_off)
		settings.card_on = 1;
	settings.config_at = config_changes();
	settings.configured_on = config_boolean("display.dynamic_quality") != 0;
	settings_update_mode();
	settings.loaded = 1;
}

/* what the smooth defaults decided for this map (smooth_defaults.c, render_governor_decide): -1 nothing decided yet (an explicit "on" is the governor on, as it has always been), else
0 or 1 for the map, the card file's word and the map's kind taken into account */
static int smooth_on = -1;
/* the log's line for the map has been said */
static int on_said;

static void governor_say_on(void)
{
	on_said = 1;
	platform_log("governor: on (%s), target %.1f ms, steps 0 to %d, window %.0f ms, dwell %.0f down %.0f up",
		smooth_on == 1 ? (settings.card_on ? "halo/governor.txt, this map is one for it" : "the smooth defaults") : settings.card_on ? "halo/governor.txt" : "display.dynamic_quality",
		settings.target_ms, governor_max_step(), settings.window_ms, settings.down_dwell_ms, settings.up_dwell_ms);
	if (settings.late_ms > 0.0)
		platform_log("governor: the late-frame rule: down when over %.0f%% of a window's frames take over %.1f ms, up when at most %.0f%% would with the step put back",
			settings.late_share * 100.0, settings.late_ms, settings.late_up_share * 100.0);
	if (settings.forced >= 0)
		platform_log("governor: held at step %d by halo/governor.txt (a test)", settings.forced);
}

static int governor_on(void)
{
	if (!settings.loaded)
		settings_load();
	else if (settings.config_at != config_changes())
	{
		/* (the card file is read once; the setting when it changes) */
		int was = settings.configured_on;

		settings.config_at = config_changes();
		settings.configured_on = config_boolean("display.dynamic_quality") != 0;
		settings_update_mode();
		if (was != settings.configured_on)
			platform_log("governor: display.dynamic_quality is now %s", settings.configured_on ? "on" : "off");
	}
	{
		/* (the card file's off is off, whatever the map; its on and the default are on for a map the smooth defaults are for; display.dynamic_quality is the player's own) */
		const int on = settings.card_off ? 0 : ((smooth_on < 0 ? settings.card_on : smooth_on) || settings.configured_on);

		if (on && !on_said)
			governor_say_on();
		return on;
	}
}

/* halo/governor.txt's word for smooth_defaults.c: SMOOTH_CARD_DEFAULT (-1) if the file is absent or has no word for it, 0 off, 1 on */
long render_governor_card(void)
{
	if (!settings.loaded)
		settings_load();
	return settings.card_off ? 0 : settings.card_on ? 1 : -1;
}

/* The map's decision (smooth_defaults.c), at a map start: on is the governor's switch for the map (0 or 1). A step reached in the last map is forgotten with the
rest (render_governor_new_map, which game.c calls first). */
void render_governor_decide(
	int map_ok,
	int on)
{
	const int was = smooth_on == 1;

	(void)map_ok;
	if (!settings.loaded)
		settings_load();
	smooth_on = on != 0;
	on_said = 0;
	if (was && !smooth_on)
		platform_log("governor: off for this map");
	governor_publish();
}

int render_governor_active(void)
{
	return governor_on();
}

int render_governor_step(void)
{
	return governor_on() ? governor.step : 0;
}

/* the values the drawing reads, one pointer (the drawing asks for it for every object, decal and particle). It points at the blended values: the step's, which the
distances and sizes glide to over the card file's blend (1.5 s) when the step changes, so that a step down takes away the smallest and farthest first and the rest
as the seconds go by, not a whole class of objects in one frame (a step that comes in a frame is a pop; blend 0 is that) */
static struct render_governor_values blended;
static struct render_governor_values const *target_values;
static struct render_governor_values const *published;
static int blended_valid;
static double blend_last_ms;

/* the frames drawn at each step while the governor was on, since the start of the run (the map summary, wiiu_map_summary.c, takes the differences) */
static unsigned long step_frames[STEPS];

void render_governor_step_frames(unsigned long frames[RENDER_GOVERNOR_STEPS])
{
	memcpy(frames, step_frames, sizeof(step_frames));
}

/* the detailed overlay's field (wiiu_gx2.c): the step and the deepest step, -1 when the governor is off */
void wiiu_overlay_governor(int step, int max_step);

static void governor_publish(void)
{
	const int on = governor_on();

	target_values = &values[on ? governor.step : 0];
	if (!blended_valid || settings.blend_ms <= 0.0 || !on)
	{
		blended = *target_values;
		blended_valid = 1;
	}
	published = &blended;
	wiiu_overlay_governor(on ? governor.step : -1, on ? governor_max_step() : -1);
}

/* a decal radius of 0 is no limit: for gliding, a long way */
#define BLEND_NO_RADIUS 400.f

static float blend_float(float current, float target, double k, float snap)
{
	float difference = target - current;

	if (difference < snap && difference > -snap)
		return target;
	return current + (float)(difference * k);
}

/* once a frame: moves the blended values toward the step's */
static void blend_step(double now_ms)
{
	double dt = now_ms - blend_last_ms;
	double k;
	float radius_now, radius_target;

	blend_last_ms = now_ms;
	if (!blended_valid || !target_values || settings.blend_ms <= 0.0)
		return;
	if (dt <= 0.0 || dt > 250.0)
		dt = 16.7;
	/* (95% of the way in the blend time) */
	k = 1.0 - exp(-dt * 3.0 / settings.blend_ms);
	blended.name = target_values->name;
	blended.cull_scenery_pixels = blend_float(blended.cull_scenery_pixels, target_values->cull_scenery_pixels, k, 0.5f);
	blended.cull_item_pixels = blend_float(blended.cull_item_pixels, target_values->cull_item_pixels, k, 0.5f);
	blended.cull_unit_pixels = blend_float(blended.cull_unit_pixels, target_values->cull_unit_pixels, k, 0.5f);
	blended.lod_scale = blend_float(blended.lod_scale, target_values->lod_scale, k, 0.005f);
	/* (no shadows at all is a jump from anything: 1e9) */
	if (target_values->shadow_min_pixels >= 1e8f || blended.shadow_min_pixels >= 1e8f)
		blended.shadow_min_pixels = target_values->shadow_min_pixels;
	else
		blended.shadow_min_pixels = blend_float(blended.shadow_min_pixels, target_values->shadow_min_pixels, k, 0.5f);
	radius_now = blended.decal_radius > 0.f ? blended.decal_radius : BLEND_NO_RADIUS;
	radius_target = target_values->decal_radius > 0.f ? target_values->decal_radius : BLEND_NO_RADIUS;
	radius_now = blend_float(radius_now, radius_target, k, 1.f);
	blended.decal_radius = radius_now >= BLEND_NO_RADIUS ? 0.f : radius_now;
	blended.decal_keep = target_values->decal_keep;
	blended.particle_lod_scale = blend_float(blended.particle_lod_scale, target_values->particle_lod_scale, k, 0.01f);
	blended.particle_keep = target_values->particle_keep;
	blended.grass = target_values->grass;
	blended.lens_flares = target_values->lens_flares;
	blended.specular = target_values->specular;
	blended.contrails = target_values->contrails;
	blended.decal_max = target_values->decal_max;
	blended.particle_max = target_values->particle_max;
}

/* whether an object of a kind is left out for being small: under the kind's cull size, and once it is, until it is a quarter bigger than that, so that an object at the
edge (its size shifts a little with the camera and its level of detail) does not come and go from one frame to the next. The bit is the object's absolute index. */
#ifndef HALO_PORT_MAXIMUM_OBJECTS_PER_MAP
#define HALO_PORT_MAXIMUM_OBJECTS_PER_MAP 8192
#endif
static unsigned char cull_bits[HALO_PORT_MAXIMUM_OBJECTS_PER_MAP / 8];

int render_governor_stable_visibility(void)
{
    static unsigned long at=(unsigned long)-1;
    static int enabled;
    unsigned long changes=config_changes();
    if(at!=changes) {at=changes;enabled=config_boolean("display.wiiu_stable_visibility");}
    return enabled;
}
int render_governor_protect_near(float distance_squared,float radius)
{
    float reach=12.0f+(radius>0 ? radius : 0);
    return render_governor_stable_visibility() && distance_squared<=reach*reach;
}

int render_governor_cull_small(long absolute_index, float pixels, float cull_pixels)
{
	unsigned char *byte;
	unsigned char mask;
	int was, culled;
    if(render_governor_stable_visibility()) cull_pixels*=0.4f;

	if (absolute_index < 0 || absolute_index >= HALO_PORT_MAXIMUM_OBJECTS_PER_MAP)
		return cull_pixels > 0.f && pixels < cull_pixels;
	byte = &cull_bits[absolute_index >> 3];
	mask = (unsigned char)(1 << (absolute_index & 7));
	if (cull_pixels <= 0.f)
	{
		*byte &= (unsigned char)~mask;
		return 0;
	}
	was = (*byte & mask) != 0;
	culled = pixels < (was ? cull_pixels * 1.25f : cull_pixels);
	if (culled)
		*byte |= mask;
	else
		*byte &= (unsigned char)~mask;
	return culled;
}

struct render_governor_values const *render_governor_values(void)
{
	if (!published)
	{
		if (!settings.loaded)
			settings_load();
		governor_publish();
	}
	return published;
}

float render_governor_lod_scale(void)
{
	return render_governor_stable_visibility() ? fmaxf(0.70f,render_governor_values()->lod_scale) : render_governor_values()->lod_scale;
}

/* the deepest step the governor goes to: the card file's max_step, else 3 (small and far things only), else all of them in the Performance mode */
static int governor_max_step(void)
{
	if (settings.max_step_set)
		return settings.max_step;
	/* in another's game the Performance mode stops at the default step too: steps 4 to 7 stop drawing the units under 8 to 32 pixels, and in a shooter those are the
	other players (the owner's joined game reached step 5 and the far players were gone) */
	if (online_map)
		return settings.max_step < DEFAULT_MAX_STEP ? settings.max_step : DEFAULT_MAX_STEP;
	return settings.performance_mode ? STEPS - 1 : settings.max_step;
}

void render_governor_set_online(int online)
{
	online_map = online != 0;
}

void render_governor_new_map(void)
{
	if (!settings.loaded)
		return;
	governor.new_map_pending = 1;
	if (settings.forced < 0)
		governor.step = 0;
	blended_valid = 0;
	governor_publish();
}

static void governor_log(double now_ms)
{
	double passes[13];
	unsigned long windows = render_governor_pass_milliseconds(passes);
	unsigned long delta[RENDER_GOVERNOR_COUNTS];
	double frames = governor.frames_in_log ? (double)governor.frames_in_log : 1.0;
	int index;

	for (index = 0; index < RENDER_GOVERNOR_COUNTS; index++)
	{
		delta[index] = render_governor_counts[index] - governor.counts_at[index];
		governor.counts_at[index] = render_governor_counts[index];
	}
	platform_log("governor: step %d (%s)%s: frame work %.1f ms (ticks %.1f, rendering %.1f, worst %.0f), target %.1f, %lu frames, steps down %lu up %lu, late frames (work over 17.5 ms) %lu = %.1f%%",
		governor.step, values[governor.step].name, settings.forced >= 0 ? " held" : "", governor.log_work / frames, governor.log_tick / frames,
		governor.log_render / frames, governor.log_worst, settings.target_ms, governor.frames_in_log, governor.downs, governor.ups, governor.log_late,
		100.0 * (double)governor.log_late / frames);
	governor.log_late = 0;
	if (windows)
		platform_log("governor: passes, ms a window (%lu): visibility %.2f sky %.2f objects %.2f lightmaps %.2f shadows %.2f decals %.2f specular %.2f "
			"transparent %.2f effects %.2f detail %.2f hud %.2f", windows, passes[0], passes[2], passes[3], passes[4], passes[5], passes[6], passes[7],
			passes[8], passes[9], passes[10], passes[11], passes[12]);
	platform_log("governor: drawn and shed a frame: objects %.1f drawn %.1f culled, shadows %.1f / %.1f, decals %.1f / %.1f, particles %.1f / %.1f",
		delta[RENDER_GOVERNOR_COUNT_OBJECTS_DRAWN] / frames, delta[RENDER_GOVERNOR_COUNT_OBJECTS_CULLED] / frames,
		delta[RENDER_GOVERNOR_COUNT_SHADOWS_DRAWN] / frames, delta[RENDER_GOVERNOR_COUNT_SHADOWS_SKIPPED] / frames,
		delta[RENDER_GOVERNOR_COUNT_DECALS_DRAWN] / frames, delta[RENDER_GOVERNOR_COUNT_DECALS_SKIPPED] / frames,
		delta[RENDER_GOVERNOR_COUNT_PARTICLES_DRAWN] / frames, delta[RENDER_GOVERNOR_COUNT_PARTICLES_SKIPPED] / frames);
	platform_log("governor: drawn by kind a frame: vehicles %.1f, bipeds %.1f, projectiles %.1f, lens flares %.1f, contrails %.1f (%.0f points)",
		delta[RENDER_GOVERNOR_COUNT_VEHICLES_DRAWN] / frames, delta[RENDER_GOVERNOR_COUNT_BIPEDS_DRAWN] / frames,
		delta[RENDER_GOVERNOR_COUNT_PROJECTILES_DRAWN] / frames, delta[RENDER_GOVERNOR_COUNT_LENS_FLARES] / frames,
		delta[RENDER_GOVERNOR_COUNT_CONTRAILS_DRAWN] / frames, delta[RENDER_GOVERNOR_COUNT_CONTRAIL_POINTS] / frames);
	governor.log_work = governor.log_tick = governor.log_render = governor.log_worst = 0.0;
	governor.frames_in_log = 0;
	governor.last_log = now_ms;
}

static void governor_set_step(int step, double now_ms, char const *why)
{
	char reason[160];

	if (step > governor.step)
	{
		governor.downs++;
		/* (an up that is taken back soon is the edge: wait longer for the next) */
		if (governor.last_change_was_up && now_ms - governor.last_change < 20000.0 && governor.up_backoff < 8)
			governor.up_backoff *= 2;
		governor.last_change_was_up = 0;
	}
	else
	{
		governor.ups++;
		governor.last_change_was_up = 1;
	}
	snprintf(reason, sizeof(reason), "governor: step %d to %d (%s): %s", governor.step, step, values[step].name, why);
	platform_log("%s", reason);
	governor.step = step;
	governor.last_change = now_ms;
	governor.over_windows = governor.under_windows = governor.probe_windows = 0;
	governor.sum_work = governor.sum_tick = governor.sum_render = governor.sum_wait = 0.0;
	governor.frames = 0;
	governor.late = governor.late_with_step_up = 0;
	governor.window_worst = 0.0;
		governor.window_long = 0;
	governor.window_start = now_ms;
	governor_publish();
	governor_log(now_ms);
}

void render_governor_frame(double work_ms, double tick_ms, double render_ms, double now_ms)
{
	double target;
	double mean;
	int severe;
	double wait_total;

	render_governor_budget_used[0] = render_governor_budget_used[1] = 0;
	if (!governor_on())
	{
		/* (off: nothing is held, and the next time on starts from the full picture) */
		if (governor.step || governor.frames || governor.frames_in_log)
		{
			memset(&governor, 0, sizeof(governor));
		}
		governor_publish();
		return;
	}
	governor_publish();
	blend_step(now_ms);
	/* (the map summary's: a frame at the step it was drawn at) */
	step_frames[governor.step >= 0 && governor.step < STEPS ? governor.step : 0]++;
	/* the game paused (the pause menu over a frozen world): the frame's cost is the pause scene's, not play's, and a step taken now would carry into play when the game
	goes on (a run left paused 24 minutes sat at the deepest step: the pop-in followed). Nothing is decided and the window starts again when play does. */
	if (settings.forced < 0 && (!game_time_initialized() || game_time_get_paused()))
	{
		governor.window_start = governor.last_change = now_ms;
		governor.over_windows = governor.under_windows = governor.probe_windows = 0;
		governor.sum_work = governor.sum_tick = governor.sum_render = governor.sum_wait = 0.0;
		governor.frames = 0;
		governor.late = governor.late_with_step_up = 0;
		governor.window_worst = 0.0;
		governor.window_long = 0;
		return;
	}
	if (!governor.up_backoff)
		governor.up_backoff = 1;
	if (governor.step > governor_max_step() && settings.forced < 0)
		governor.step = governor_max_step();
	if (settings.forced >= 0)
	{
		if (governor.step != settings.forced)
		{
			governor.step = settings.forced;
			governor.last_change = now_ms;
			governor_publish();
			governor_log(now_ms);
		}
	}
	if (!governor.window_start)
		governor.window_start = governor.last_log = governor.last_change = now_ms;
	/* the game thread's wait for the tick thread this frame (the tick overlap): while it waits, the frame is the tick thread's and drawing less shortens nothing */
	wait_total = tick_overlap_wait_ms_total();
	if (wait_total < governor.wait_total_at)
		governor.wait_total_at = wait_total;
	{
		const double waited = wait_total - governor.wait_total_at;

		governor.wait_total_at = wait_total;
		if (governor.new_map_pending)
		{
			/* a map has started: nothing is decided for the first quiet seconds (shaders and textures load) */
			governor.new_map_pending = 0;
			governor.quiet_until = now_ms + settings.quiet_ms;
			governor.window_start = governor.last_change = now_ms;
			governor.over_windows = governor.under_windows = governor.probe_windows = 0;
			governor.sum_work = governor.sum_tick = governor.sum_render = governor.sum_wait = 0.0;
			governor.frames = 0;
			governor.late = governor.late_with_step_up = 0;
			governor.window_worst = 0.0;
		governor.window_long = 0;
		}
		/* a hitch (a load, a save, a first use) is not detail's to shed; the rest counts at most three targets */
		target = settings.target_ms;
		if (work_ms > 250.0)
		{
			governor.hitches++;
		}
		else
		{
			double counted = work_ms > target * 3.0 ? target * 3.0 : work_ms;

			governor.sum_work += counted;
			governor.sum_tick += tick_ms;
			governor.sum_render += render_ms;
			governor.sum_wait += waited;
			governor.frames++;
			if (settings.late_ms > 0.0)
			{
				governor.late += work_ms > settings.late_ms;
				governor.late_with_step_up += work_ms + (double)save_ms[governor.step] > settings.late_ms;
			}
			governor.log_late += work_ms > 17.5;
			if (work_ms > governor.window_worst)
				governor.window_worst = work_ms;
			if (work_ms > settings.worst_ms)
				governor.window_long++;
			governor.log_work += work_ms;
			governor.log_tick += tick_ms;
			governor.log_render += render_ms;
			governor.frames_in_log++;
			if (work_ms > governor.log_worst)
				governor.log_worst = work_ms;
		}
	}
	if (now_ms < governor.window_start)
		governor.window_start = governor.last_log = governor.last_change = now_ms;
	/* (a window of 8 frames: a game at under 16 frames a second, as a joined co-op game of the Wii U is, takes 2 seconds for it to hold 3, and decides then) */
	if (settings.forced < 0 && now_ms - governor.window_start >= settings.window_ms && (governor.frames >= 8 || now_ms - governor.window_start >= 4.0 * settings.window_ms))
	{
		/* (a window with one long frame in it decides nothing: a hitch. One in which most frames are long is the game itself: the ticks fill the frame, and what is drawn is what can be shed) */
		if (now_ms < governor.quiet_until || (governor.window_worst > settings.worst_ms && governor.window_long * 2 <= governor.frames))
		{
			/* (the first seconds of a map, or a window with a long frame in it: no evidence either way) */
		}
		else if (governor.frames >= 3)
		{
			const double wait = governor.sum_wait / (double)governor.frames;
			const double saving = (double)save_ms[governor.step];
			/* (waiting 3 ms a frame and a third of it or more: the frame is the tick thread's; a frame that waits a little, as a heavy fight's does for the
			frames with two ticks in them, is still drawing's) */
			const int tick_bound = wait > settings.tick_bound_ms && wait > settings.tick_bound_share * governor.sum_work / (double)governor.frames;
			double predicted;

			const int late_rule = settings.late_ms > 0.0;
			const double late_share = (double)governor.late / (double)governor.frames;
			const double late_share_up = (double)(tick_bound ? governor.late : governor.late_with_step_up) / (double)governor.frames;

			mean = governor.sum_work / (double)governor.frames;
			/* (the late-frame rule: more than half the frames late is as severe as a mean 40% over) */
			severe = mean > target * 1.4 || (late_rule && late_share >= 0.6);
			/* over the target, with the drawing at least 4 ms of it, and not waiting for the tick thread: drawing less shortens a frame only if the drawing is
			what it waits for (a frame that is mostly ticks, or the tick thread's, is not drawing's to shed). The late-frame rule: the share of late frames is over its limit */
			governor.over_windows = (late_rule ? late_share > settings.late_share : mean > target) && governor.sum_render / (double)governor.frames >= settings.min_render_ms &&
				!tick_bound ? governor.over_windows + 1 : 0;
			/* the frame with the step's drawing put back: the drawing adds to the work, less what it takes from the wait for the tick thread. The late-frame rule: the share of
			frames still on time with the step's save added back (when the frame is the tick thread's, the save is not counted) is at most the limit */
			predicted = tick_bound ? (mean - wait + saving > mean ? mean - wait + saving : mean) : mean + saving;
			governor.under_windows = governor.step > 0 && (late_rule ? late_share_up <= settings.late_up_share : predicted < target * settings.up_margin) ? governor.under_windows + 1 : 0;
			/* and a step up tried when the frame has been comfortably under the target a long time, whatever the estimate says: it is taken back (and the
			next try waits longer) if it was wrong. The late-frame rule: a quarter of the share the step up allows are late */
			governor.probe_windows = governor.step > 0 && (late_rule ? late_share <= settings.late_up_share * 0.25 : mean < target * 0.93) ? governor.probe_windows + 1 : 0;
			if (governor.over_windows >= (severe ? 1 : settings.down_windows) && governor.step < governor_max_step() &&
				now_ms - governor.last_change >= settings.down_dwell_ms)
			{
				char why[64];

				if (late_rule)
					snprintf(why, sizeof(why), "%.0f%% of the frames over %.1f ms (limit %.0f%%), mean %.1f%s", late_share * 100.0, settings.late_ms, settings.late_share * 100.0, mean,
						severe ? ", severe" : "");
				else
					snprintf(why, sizeof(why), "%.1f ms over %.1f%s", mean, target, severe ? " by 40%" : "");
				governor_set_step(governor.step + 1, now_ms, why);
			}
			else if (governor.under_windows >= settings.up_windows && governor.step > 0 &&
				now_ms - governor.last_change >= settings.up_dwell_ms * governor.up_backoff)
			{
				char why[64];

				if (late_rule)
					snprintf(why, sizeof(why), "%.0f%% of the frames would be over %.1f ms with %.1f ms more (limit %.0f%%)", late_share_up * 100.0, settings.late_ms, saving,
						settings.late_up_share * 100.0);
				else
					snprintf(why, sizeof(why), "%.1f ms and %.1f more is under %.1f", mean, saving, target * settings.up_margin);
				governor_set_step(governor.step - 1, now_ms, why);
			}
			else if (governor.probe_windows >= (int)(settings.probe_ms / settings.window_ms) && governor.step > 0 &&
				now_ms - governor.last_change >= settings.probe_ms * governor.up_backoff)
			{
				char why[64];

				if (late_rule)
					snprintf(why, sizeof(why), "a try after %.0f s with %.1f%% of the frames over %.1f ms (%.1f ms)", settings.probe_ms / 1000.0, late_share * 100.0, settings.late_ms, mean);
				else
					snprintf(why, sizeof(why), "a try after %.0f s under %.1f (%.1f ms)", settings.probe_ms / 1000.0, target * 0.93, mean);
				governor_set_step(governor.step - 1, now_ms, why);
			}
			else if (now_ms - governor.last_change > 60000.0 && governor.up_backoff > 1)
				governor.up_backoff = 1;
		}
		governor.window_start = now_ms;
		governor.sum_work = governor.sum_tick = governor.sum_render = governor.sum_wait = 0.0;
		governor.frames = 0;
		governor.late = governor.late_with_step_up = 0;
		governor.window_worst = 0.0;
		governor.window_long = 0;
	}
	governor_publish();
	if (settings.log_ms > 0.0 && now_ms - governor.last_log >= settings.log_ms)
		governor_log(now_ms);
}

#else /* not the Wii U: the governor is off and every value neutral */

static struct render_governor_values const neutral = { "full", 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 16, 1.f, 16, 1, 1, 1, 1 };

int render_governor_active(void)
{
	return 0;
}

int render_governor_step(void)
{
	return 0;
}

struct render_governor_values const *render_governor_values(void)
{
	return &neutral;
}

float render_governor_lod_scale(void)
{
	return 1.f;
}

int render_governor_stable_visibility(void) {return 0;}
int render_governor_protect_near(float distance_squared,float radius) {(void)distance_squared;(void)radius;return 0;}

int render_governor_cull_small(long absolute_index, float pixels, float cull_pixels)
{
	(void)absolute_index;
	return cull_pixels > 0.f && pixels < cull_pixels;
}

void render_governor_new_map(void)
{
}

void render_governor_set_online(int online)
{
	(void)online;
}

void render_governor_step_frames(unsigned long frames[RENDER_GOVERNOR_STEPS])
{
	memset(frames, 0, sizeof(unsigned long) * RENDER_GOVERNOR_STEPS);
}

long render_governor_card(void)
{
	return -1;
}

void render_governor_decide(int map_ok, int on)
{
	(void)map_ok;
	(void)on;
}

void render_governor_frame(double work_ms, double tick_ms, double render_ms, double now_ms)
{
	(void)work_ms;
	(void)tick_ms;
	(void)render_ms;
	(void)now_ms;
}

#endif
