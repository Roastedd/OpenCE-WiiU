/*
GAME_DIGEST_SWEEP.C

The digest harness's sweep: a scripted tour of what a map can draw, so that the shader keys of every material the map holds are met on the host and
not one console session at a time (tools/test_wiiu_game_digest.py run --sweep, --keys). It is compiled with the game's flags and linked into the digest
program, and does nothing unless GAME_DIGEST_SWEEP names a phase. The glue's __wrap_render_frame calls game_digest_sweep_render_frame before each
frame of the game is drawn; here the frame's player window gets another camera and the game is asked for what the tour needs.

    GAME_DIGEST_SWEEP=bsp,objects,effects,damage   (the phases, in the order they are named; any subset)
    GAME_DIGEST_SWEEP_WAIT=N                       frames of play before the tour starts (default 60)
    GAME_DIGEST_SWEEP_HOLD=N                       frames a stop is held (default 2; the objects' and effects' stops are held for 3 and 6)

bsp:      for each structure BSP of the scenario (switch_bsp, as a script does), for each shader its lightmaps' materials use, the camera at the
          material's centroid on its plane's side, looking at it, from 1.2 world units, and then for each cluster a view looking up. The materials
          are the structure_material blocks (shader, lightmap or none), the environment's pixel shaders and the transparent, water and glass
          shaders of the BSP; the clusters' sky, fog and weather come with the views.
objects:  every object tag the map holds (biped, vehicle, weapon, equipment, scenery, projectile, machine, control, light fixture, garbage, sound
          scenery, placeholder), made by object_new where the player stands, one at a time, and looked at from a distance that fits its radius.
effects:  every effect tag, made at a point in front of the camera (effect_new_unattached_from_markers, as the effect_new script does), and the
          camera held for the effect's frames.
damage:   every damage effect tag, as the damage_new script causes it at the player (the screen flashes and the camera's shake it brings).

The tour logs its stops to GAME_DIGEST_DIR/sweep.txt (one line a stop: phase, frame, what), and at its start the census of the shaders the map holds.
It never changes how a frame is drawn when the variable is not set, so the digests of the harness's other runs are those they were.
*/

#include "cseries.h"
#include "cseries/errors.h"
#include "cache/cache_files.h"
#include "camera/observer.h"
#include "cutscene/cinematics.h"
#include "effects/effects.h"
#include "effects/effect_definitions.h"
#include "game/game.h"
#include "game/local_players.h"
#include "game/players.h"
#include "math/real_math.h"
#include "objects/objects.h"
#include "objects/damage.h"
#include "objects/object_lights.h"
#include "items/weapon_definitions.h"
#include "items/weapons.h"
#include "objects/object_definitions.h"
#include "render/render.h"
#include "render/render_cameras.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "structures/structure_bsp_definitions.h"
#include "tag_files/tag_groups.h"
#include "tag_files/tag_files.h"
#include "hs/hs_scenario_definitions.h"
#include "units/units.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (the game's stdio header maps file names to the game's data folders: this file's log is the harness's, at the path it is given) */
#undef fopen
#undef fprintf

void game_digest_sweep_render_frame(struct render_window *windows, short window_count);
unsigned long replay_keys_frame(void);
static unsigned long const object_groups[] = { 'weap', 'bipd', 'vehi', 'eqip', 'garb', 'proj', 'scen', 'mach', 'ctrl', 'lifi', 'ssce', 'plac' };
static void lights_collect(void);
static void phase_weapons(void);
static void phase_scripts(void);
static void next_phase(void);

/* ---------- the tour */

enum
{
	PHASE_BSP = 1,
	PHASE_OBJECTS = 2,
	PHASE_EFFECTS = 4,
	PHASE_DAMAGE = 8,
	PHASE_WEAPONS = 16,
	PHASE_SCRIPTS = 32,
	PHASE_LIT = 64,
	MAXIMUM_STOPS = 16384,
};

struct stop
{
	real_point3d position;
	real_vector3d forward;
	long tag_index;      /* the shader, object, effect or damage effect this stop is for */
	short bsp_index;
	short kind;          /* 0 a material, 1 a cluster's view up */
	long detail;         /* the material's surface count, or the cluster */
};

static struct
{
	int configured;
	int phases;          /* PHASE_* wanted */
	long wait, hold;
	FILE *log;
	long frames;         /* frames of play seen */
	long frame_in_stop;
	int phase;           /* the phase running (0 not started, PHASE_* , -1 done) */
	short bsp_count, bsp_index;
	int stops_ready;
	long stop_count, stop_index;
	struct stop stops[MAXIMUM_STOPS];
	long object_index;   /* the object or the effect the stop made */
	int stage_known;
	real_point3d stage_position;
	real_vector3d stage_forward;
	long list[8192];     /* the tags of an objects, effects or damage phase */
	unsigned char camouflage[8192]; /* the objects' stops of a unit made active-camouflaged */
	long list_count;
	long census_done;
	struct { int active; real_point3d position; real_vector3d forward; } cam;
	struct { real_point3d position; real_vector3d forward; real floor; int fogged; } candidates[64]; /* places in the first bsp for the objects and effects: open, mostly floors */
	int candidate_count;
	int leave_object;     /* the object of this stop is not deleted (a flag or a ball: the game halts on deleting one) */
	int pass;            /* the effects' two goes */
	int object_pass;     /* the objects' three goes: 0 as they are, 1 with a dynamic light beside each, 2 in a cluster with fog */
	int stage_variant;   /* 0: the farthest floor; 1: the farthest floor of a cluster with fog (stage_find) */
	long lights[256];    /* the map's light definitions, one of which each stop of the lit tour makes (a dynamic light, as a muzzle flash or a plasma bolt is) */
	int light_count;
	int lit_started;
	long density;        /* GAME_DIGEST_SWEEP_DENSITY: surfaces looked at for each material (3: the first, the middle and the last) */
	int flashlight;      /* GAME_DIGEST_SWEEP_FLASHLIGHT: the player's flashlight is kept on (the environment's dynamic light passes) */
	int passes;          /* GAME_DIGEST_SWEEP_PASSES: 1 or 2 */
	char *skip;          /* GAME_DIGEST_SWEEP_SKIP: a file of lines "object NAME", "effect NAME", "damage NAME", "weapon NAME" for the tags an earlier run did */
	long skipped;
	char script_names[2048]; /* GAME_DIGEST_SWEEP_SCRIPTS: the level's scripts to run, separated by commas */
	long script_running; /* ticks the script now running has had */
	long exit_countdown; /* frames left after the tour before the run ends (GAME_DIGEST_SWEEP_EXIT) */
} sweep;

/* ---------- small vector helpers (the game's are macros or need the headers' tables) */

static void vec_normalize(real_vector3d *v)
{
	real length = (real)sqrt(v->i * v->i + v->j * v->j + v->k * v->k);

	if (length > 1e-6f)
	{
		v->i /= length;
		v->j /= length;
		v->k /= length;
	}
}

/* the camera of the frame that comes (the tick's choice; game_digest_sweep_render_frame gives it to the window) */
static void camera_target(real_point3d const *position, real_vector3d const *forward)
{
	sweep.cam.active = 1;
	sweep.cam.position = *position;
	sweep.cam.forward = *forward;
}

/* a camera at position looking along forward: window's both cameras */
static void camera_set(struct render_window *window, real_point3d const *position, real_vector3d const *forward)
{
	real_vector3d f = *forward, up, right;

	vec_normalize(&f);
	if (f.k > 0.97f || f.k < -0.97f)
	{
		up.i = 1.0f;
		up.j = 0.0f;
		up.k = 0.0f;
	}
	else
	{
		up.i = 0.0f;
		up.j = 0.0f;
		up.k = 1.0f;
	}
	/* right = forward x up; up = right x forward */
	right.i = f.j * up.k - f.k * up.j;
	right.j = f.k * up.i - f.i * up.k;
	right.k = f.i * up.j - f.j * up.i;
	vec_normalize(&right);
	up.i = right.j * f.k - right.k * f.j;
	up.j = right.k * f.i - right.i * f.k;
	up.k = right.i * f.j - right.j * f.i;
	vec_normalize(&up);
	window->rasterizer_camera.position = *position;
	window->rasterizer_camera.forward = f;
	window->rasterizer_camera.up = up;
	window->render_camera.position = *position;
	window->render_camera.forward = f;
	window->render_camera.up = up;
}

static void note(char const *format, ...) __attribute__((format(printf, 1, 2)));

#include <stdarg.h>

static void note(char const *format, ...)
{
	va_list arguments;

	if (!sweep.log)
		return;
	fprintf(sweep.log, "frame %lu: ", replay_keys_frame());
	va_start(arguments, format);
	vfprintf(sweep.log, format, arguments);
	va_end(arguments);
	fputc('\n', sweep.log);
	fflush(sweep.log);
}

/* ---------- configuration */

static void configure(void)
{
	char const *phases = getenv("GAME_DIGEST_SWEEP");
	char const *directory = getenv("GAME_DIGEST_DIR");
	char path[512];

	sweep.configured = 1;
	if (!phases || !*phases)
		return;
	if (strstr(phases, "bsp"))
		sweep.phases |= PHASE_BSP;
	if (strstr(phases, "objects"))
		sweep.phases |= PHASE_OBJECTS;
	if (strstr(phases, "effects"))
		sweep.phases |= PHASE_EFFECTS;
	if (strstr(phases, "damage"))
		sweep.phases |= PHASE_DAMAGE;
	if (strstr(phases, "weapons"))
		sweep.phases |= PHASE_WEAPONS;
	if (strstr(phases, "scripts"))
		sweep.phases |= PHASE_SCRIPTS;
	if (strstr(phases, "lit"))
		sweep.phases |= PHASE_LIT;
	sweep.passes = getenv("GAME_DIGEST_SWEEP_PASSES") ? (int)strtol(getenv("GAME_DIGEST_SWEEP_PASSES"), NULL, 0) : 1;
	if (getenv("GAME_DIGEST_SWEEP_SKIP"))
	{
		FILE *skip = fopen(getenv("GAME_DIGEST_SWEEP_SKIP"), "rb");

		if (skip)
		{
			long size;

			fseek(skip, 0, SEEK_END);
			size = ftell(skip);
			fseek(skip, 0, SEEK_SET);
			sweep.skip = (char *)calloc(1, (size_t)size + 2);
			if (sweep.skip && fread(sweep.skip, 1, (size_t)size, skip) != (size_t)size)
				sweep.skip[0] = 0;
			fclose(skip);
		}
	}
	sweep.flashlight = getenv("GAME_DIGEST_SWEEP_FLASHLIGHT") && atoi(getenv("GAME_DIGEST_SWEEP_FLASHLIGHT"));
	sweep.density = getenv("GAME_DIGEST_SWEEP_DENSITY") ? strtol(getenv("GAME_DIGEST_SWEEP_DENSITY"), NULL, 0) : 3;
	sweep.wait = getenv("GAME_DIGEST_SWEEP_WAIT") ? strtol(getenv("GAME_DIGEST_SWEEP_WAIT"), NULL, 0) : 60;
	sweep.hold = getenv("GAME_DIGEST_SWEEP_HOLD") ? strtol(getenv("GAME_DIGEST_SWEEP_HOLD"), NULL, 0) : 2;
	snprintf(path, sizeof(path), "%s/sweep.txt", directory && *directory ? directory : ".");
	sweep.log = fopen(path, "w");
	if (!sweep.log)
		fprintf(stderr, "game digest sweep: cannot write %s\n", path);
	note("sweep phases %d wait %ld hold %ld", sweep.phases, sweep.wait, sweep.hold);
}

/* ---------- the census of the shaders the map holds */

/* rasterizer_xbox_models.c's own (it keeps it to itself): a shader_model's properties, to the fields the model pixel shaders are made of */
struct sweep_shader_model_properties
{
	word flags;
	short type;
	byte reserved04[0xC];
	real translucency;
	byte reserved14[0x10];
	short change_color_source;
	byte reserved26[0x1E];
	word self_illumination_flags;
	short pad46;
	short self_illumination_color_source;
	short self_illumination_animation_function;
	real self_illumination_animation_period;
	real_rgb_color self_illumination_animation_color_lower_bound;
	real_rgb_color self_illumination_animation_color_upper_bound;
	byte reserved68[0xC];
	real map_u_scale;
	real map_v_scale;
	struct tag_reference base_map;
	byte reserved8C[8];
	struct tag_reference multipurpose_map;
	byte reservedA4[8];
	short detail_function;
	short detail_mask;
	real detail_map_scale;
	struct tag_reference detail_map;
};

struct sweep_shader_model
{
	byte shader[0x28];
	struct sweep_shader_model_properties model;
};

#include "rasterizer/xbox/rasterizer_xbox_pixel_shader.h"
#include "rasterizer/xbox/shader_transparent_chicago_preprocessor.h"
boolean shader_transparent_generic_create(struct shader *shader, struct pixel_shader_definition *pixel_shader);

/* The pixel programs the transparent shaders of the map make (the preprocessors the draws call: shader_transparent_chicago_create, _generic_create), as the
words of the pixel key's combiner part, in the order of the pixel shader render states (D3DRS_PSALPHAINPUTS0 on; the constants are not in a key): the
tools read them to say which of the programs the map's tags can make a harvest has met. The draw adds the framebuffer blend's stage after the last stage
these make (the number printed), so that stage's words are left out when programs are compared. */
static void census_definitions(void)
{
	static unsigned long const groups[] = { 'schi', 'sotr' };
	unsigned long i;

	for (i = 0; i < 2; i++)
	{
		struct tag_iterator iterator;
		long tag_index;

		tag_iterator_new(&iterator, groups[i]);
		for (tag_index = tag_iterator_next(&iterator); tag_index != NONE; tag_index = tag_iterator_next(&iterator))
		{
			struct pixel_shader_definition definition;
			struct shader *shader = (struct shader *)tag_get(groups[i], tag_index);
			boolean ok = shader ? (i == 0 ? shader_transparent_chicago_create(shader, &definition) : shader_transparent_generic_create(shader, &definition)) : 0;
			unsigned long words[58];
			int k;

			if (!ok)
			{
				note("census definition %s %s failed", i == 0 ? "schi" : "sotr", tag_get_name(tag_index));
				continue;
			}
			memset(words, 0, sizeof(words));
			for (k = 0; k < 8; k++)
			{
				words[k] = definition.alpha_inputs[k];
				words[26 + k] = definition.alpha_outputs[k];
				words[34 + k] = definition.rgb_inputs[k];
				words[45 + k] = definition.rgb_outputs[k];
			}
			words[8] = definition.final_combiner_inputs_abcd;
			words[9] = definition.final_combiner_inputs_efg;
			words[42] = definition.compare_mode;
			words[53] = definition.combiner_count;
			words[55] = definition.dot_mapping;
			words[56] = definition.input_texture;
			words[57] = definition.texture_modes;
			if (sweep.log)
			{
				fprintf(sweep.log, "frame %lu: census definition %s %ld %d ", replay_keys_frame(), i == 0 ? "schi" : "sotr", tag_index, (int)(definition.combiner_count & 0xff));
				for (k = 0; k < 58; k++)
					fprintf(sweep.log, "%08lx", words[k]);
				fprintf(sweep.log, " %s\n", tag_get_name(tag_index));
			}
		}
	}
}

/* every shader_model tag's detail function and mask, flags and maps: the model pixel shaders of the reflection family are made of them (set_environment_shader_pixel_shader) */
static void census_models(void)
{
	struct tag_iterator iterator;
	long tag_index;

	tag_iterator_new(&iterator, 'soso');
	for (tag_index = tag_iterator_next(&iterator); tag_index != NONE; tag_index = tag_iterator_next(&iterator))
	{
		struct sweep_shader_model const *shader = (struct sweep_shader_model const *)tag_get('soso', tag_index);

		if (shader && sweep.log)
			fprintf(sweep.log, "frame %lu: census model %ld function %d mask %d flags %u cube %d multipurpose %d base %d detail %d %s\n", replay_keys_frame(), tag_index,
				(int)shader->model.detail_function, (int)shader->model.detail_mask, (unsigned)shader->model.flags, *(long const *)((byte const *)shader + 0x170 + offsetof(struct tag_reference, index)) != NONE,
				shader->model.multipurpose_map.index != NONE, shader->model.base_map.index != NONE, shader->model.detail_map.index != NONE, tag_get_name(tag_index));
	}
}

static void census(void)
{
	static unsigned long const groups[] = { 'senv', 'soso', 'sotr', 'schi', 'scex', 'sgla', 'smet', 'spla', 'swat' };
	unsigned long i;
	long total = 0;

	for (i = 0; i < sizeof(groups) / sizeof(groups[0]); i++)
	{
		struct tag_iterator iterator;
		long tag_index, count = 0;

		tag_iterator_new(&iterator, groups[i]);
		for (tag_index = tag_iterator_next(&iterator); tag_index != NONE; tag_index = tag_iterator_next(&iterator))
		{
			note("census shader %c%c%c%c %ld %s", (char)(groups[i] >> 24), (char)(groups[i] >> 16), (char)(groups[i] >> 8), (char)groups[i], tag_index,
				tag_get_name(tag_index));
			count++;
		}
		note("census %c%c%c%c %ld", (char)(groups[i] >> 24), (char)(groups[i] >> 16), (char)(groups[i] >> 8), (char)groups[i], count);
		total += count;
	}
	note("census shaders %ld", total);
	census_definitions();
	census_models();
	{
		long script_index;

		for (script_index = 0; script_index < global_scenario->hs_scripts.count; script_index++)
		{
			struct hs_script const *script = TAG_BLOCK_GET_ELEMENT(&global_scenario->hs_scripts, script_index, struct hs_script);

			if (script)
				note("census script %s type %d return %d", script->name, (int)script->script_type, (int)script->return_type);
		}
	}
}

/* ---------- the BSP phase */

enum
{
	COMPRESSED_STRUCTURE_VERTEX_SIZE = 0x20, /* structure_visibility.c's: a position, a normal, binormal, tangent and texture coordinates */
};

/* structure_visibility.c's own (it keeps them to itself): the clusters' blocks, whose surface lists group the surfaces by material */
struct sweep_cluster
{
	short sky_index;
	short fog_designator;
	short background_sound_palette_index;
	short sound_environment_palette_index;
	short weather_palette_index;
	short transition_structure_bsp_index;
	short first_runtime_decal_index;
	word runtime_decal_count;
	long unused[6];
	struct tag_block predicted_resources;
	struct tag_block subclusters;
	word first_lens_flare_marker_index;
	word lens_flare_marker_count;
	struct tag_block surface_indices;
	struct tag_block mirrors;
	struct tag_block portal_indices;
};

struct sweep_subcluster
{
	real_rectangle3d world_bounds;
	struct tag_block surface_indices;
};

static int point_in_cluster(struct structure_bsp const *bsp, real_point3d const *p)
{
	return cluster_index_from_point(bsp, (union real_point3d const *)p) != NONE;
}

/* the corner points of a material's surface by its place in the material (vertices of the compressed data), or 0 */
static int surface_points(struct structure_bsp const *bsp, struct structure_material const *material, long surface_offset, real_point3d points[3])
{
	struct structure_surface const *surface;
	long vertex_limit = material->compressed_vertex_data.size / COMPRESSED_STRUCTURE_VERTEX_SIZE, corner;

	if (surface_offset < 0 || surface_offset >= material->surface_count || !material->compressed_vertex_data.address)
		return 0;
	surface = TAG_BLOCK_GET_ELEMENT(&bsp->surfaces, material->first_surface_index + surface_offset, struct structure_surface);
	if (!surface)
		return 0;
	for (corner = 0; corner < 3; corner++)
	{
		if (surface->vertex_indices[corner] >= vertex_limit)
			return 0;
		points[corner] = *(real_point3d const *)((byte const *)material->compressed_vertex_data.address +
			surface->vertex_indices[corner] * COMPRESSED_STRUCTURE_VERTEX_SIZE);
	}
	return 1;
}

/* a camera in front of a triangle, on the side that is inside the BSP, at distance, looking at its centroid; the triangle's centroid and the unit
normal of the side taken are returned as well */
static int triangle_view(struct structure_bsp const *bsp, real_point3d const points[3], real distance, struct stop *stop, real_point3d *centroid_out,
	real_vector3d *normal_out)
{
	real_vector3d e1, e2, n;
	real_point3d centroid;
	int side;

	e1.i = points[1].x - points[0].x;
	e1.j = points[1].y - points[0].y;
	e1.k = points[1].z - points[0].z;
	e2.i = points[2].x - points[0].x;
	e2.j = points[2].y - points[0].y;
	e2.k = points[2].z - points[0].z;
	n.i = e1.j * e2.k - e1.k * e2.j;
	n.j = e1.k * e2.i - e1.i * e2.k;
	n.k = e1.i * e2.j - e1.j * e2.i;
	if (n.i * n.i + n.j * n.j + n.k * n.k < 1e-12f)
		return 0;
	vec_normalize(&n);
	centroid.x = (points[0].x + points[1].x + points[2].x) / 3.0f;
	centroid.y = (points[0].y + points[1].y + points[2].y) / 3.0f;
	centroid.z = (points[0].z + points[1].z + points[2].z) / 3.0f;
	for (side = 0; side < 4; side++)
	{
		/* (this side of the triangle, then the other, at the distance and then at a fifth of it) */
		real sign = (side & 1) ? -1.0f : 1.0f, d = side < 2 ? distance : distance * 0.2f;
		real_point3d candidate;

		candidate.x = centroid.x + n.i * sign * d;
		candidate.y = centroid.y + n.j * sign * d;
		candidate.z = centroid.z + n.k * sign * d;
		if (point_in_cluster(bsp, &candidate))
		{
			stop->position = candidate;
			stop->forward.i = -n.i * sign;
			stop->forward.j = -n.j * sign;
			stop->forward.k = -n.k * sign;
			if (centroid_out)
				*centroid_out = centroid;
			if (normal_out)
			{
				normal_out->i = n.i * sign;
				normal_out->j = n.j * sign;
				normal_out->k = n.k * sign;
			}
			return 1;
		}
	}
	return 0;
}

static void bsp_stops_make(void)
{
	struct structure_bsp *bsp = global_structure_bsp_get();
	long lightmap_index, material_index, cluster_index;
	long considered = 0, placed = 0, no_place = 0;
	long seen_keys[1024], seen_stops[1024];
	long clusters_seen = 0, clusters_placed = 0;
	long seen_count = 0;

	sweep.stop_count = 0;
	if (!bsp)
		return;
	/* the materials: at most four stops for a shader with or without a lightmap, from the first, middle and last surfaces of its materials */
	for (lightmap_index = 0; lightmap_index < bsp->lightmaps.count; lightmap_index++)
	{
		struct structure_lightmap *lightmap = TAG_BLOCK_GET_ELEMENT(&bsp->lightmaps, lightmap_index, struct structure_lightmap);

		if (!lightmap)
			continue;
		for (material_index = 0; material_index < lightmap->materials.count; material_index++)
		{
			struct structure_material *material = TAG_BLOCK_GET_ELEMENT(&lightmap->materials, material_index, struct structure_material);
			long shader = material ? material->shader.index : NONE, key, entry = -1, i, sample;
			long samples[64];
			long sample_count = sweep.density < 3 ? 3 : sweep.density > 64 ? 64 : sweep.density;

			if (!material || shader == NONE || material->surface_count <= 0)
				continue;
			considered++;
			key = shader << 1 | (lightmap->bitmap_index != NONE);
			for (i = 0; i < seen_count; i++)
				if (seen_keys[i] == key)
					entry = i;
			if (entry < 0)
			{
				if (seen_count >= 1024)
					continue;
				entry = seen_count++;
				seen_keys[entry] = key;
				seen_stops[entry] = 0;
			}
			/* (surfaces spread over the material: the first, the last and between them) */
			for (sample = 0; sample < sample_count; sample++)
				samples[sample] = sample_count > 1 ? sample * (material->surface_count - 1) / (sample_count - 1) : 0;
			for (sample = 0; sample < sample_count && seen_stops[entry] < sample_count + 1 && sweep.stop_count < MAXIMUM_STOPS - 200; sample++)
			{
				real_point3d points[3];
				struct stop stop;

				if (sample > 0 && samples[sample] == samples[sample - 1])
					continue;
				memset(&stop, 0, sizeof(stop));
				stop.tag_index = shader;
				stop.bsp_index = global_structure_bsp_index_get();
				stop.detail = material->surface_count;
				if (surface_points(bsp, material, samples[sample], points) && triangle_view(bsp, points, 1.2f, &stop, NULL, NULL))
				{
					sweep.stops[sweep.stop_count++] = stop;
					seen_stops[entry]++;
					placed++;
					/* (one stop for a material is enough while there are materials to come: the other samples are for a shader met once) */
					if (lightmap->materials.count > 4 && sweep.density <= 3)
						break;
				}
				else
					no_place++;
			}
		}
	}
	/* every cluster: a surface of one of its subclusters gives a point inside it (the retail clusters have no surface lists of their own: their subclusters
	have the surfaces), where the camera looks up (the sky, the weather) and along the room (the fog) */
	for (cluster_index = 0; cluster_index < bsp->clusters.count && sweep.stop_count < MAXIMUM_STOPS - 2; cluster_index++)
	{
		struct sweep_cluster *cluster = TAG_BLOCK_GET_ELEMENT(&bsp->clusters, cluster_index, struct sweep_cluster);
		long subcluster_index, found = 0, tried = 0;

		for (subcluster_index = 0; cluster && !found && subcluster_index < cluster->subclusters.count && tried < 16; subcluster_index++)
		{
			struct sweep_subcluster *subcluster = TAG_BLOCK_GET_ELEMENT(&cluster->subclusters, subcluster_index, struct sweep_subcluster);
			long const *list = subcluster ? (long const *)subcluster->surface_indices.address : NULL;
			long entry;

			for (entry = 0; list && !found && entry < subcluster->surface_indices.count && tried < 16; entry += 1 + subcluster->surface_indices.count / 3, tried++)
			{
				short lightmap_i, material_i;
				struct structure_lightmap *lightmap;
				struct structure_material *material;
				struct structure_surface const *surface;

				if ((unsigned long)list[entry] >= (unsigned long)bsp->surfaces.count)
					continue;
				if (!structure_bsp_find_material_for_surface(bsp, list[entry], &lightmap_i, &material_i))
					continue;
				lightmap = TAG_BLOCK_GET_ELEMENT(&bsp->lightmaps, lightmap_i, struct structure_lightmap);
				material = lightmap ? TAG_BLOCK_GET_ELEMENT(&lightmap->materials, material_i, struct structure_material) : NULL;
				surface = TAG_BLOCK_GET_ELEMENT(&bsp->surfaces, list[entry], struct structure_surface);
				if (material && surface && material->compressed_vertex_data.address)
				{
					real_point3d points[3];
					long corner, vertex_limit = material->compressed_vertex_data.size / COMPRESSED_STRUCTURE_VERTEX_SIZE;
					int usable = 1;

					for (corner = 0; corner < 3; corner++)
					{
						if (surface->vertex_indices[corner] >= vertex_limit)
							usable = 0;
						else
							points[corner] = *(real_point3d const *)((byte const *)material->compressed_vertex_data.address +
								surface->vertex_indices[corner] * COMPRESSED_STRUCTURE_VERTEX_SIZE);
					}
					if (usable)
					{
						struct stop stop;
						real_point3d centroid;
						real_vector3d normal;

						memset(&stop, 0, sizeof(stop));
						if (triangle_view(bsp, points, 0.8f, &stop, &centroid, &normal) &&
							cluster_index_from_point(bsp, (union real_point3d const *)&stop.position) == cluster_index)
						{
							struct stop up = stop;

							up.kind = 1;
							up.detail = cluster_index;
							up.bsp_index = global_structure_bsp_index_get();
							up.forward.i = 0.12f;
							up.forward.j = 0.07f;
							up.forward.k = 1.0f;
							sweep.stops[sweep.stop_count++] = up;
							/* (and along the room: the surface's normal turned flat) */
							stop.kind = 1;
							stop.detail = cluster_index;
							stop.bsp_index = global_structure_bsp_index_get();
							stop.forward = normal;
							stop.forward.k = 0.0f;
							if (stop.forward.i * stop.forward.i + stop.forward.j * stop.forward.j < 1e-6f)
							{
								stop.forward.i = 1.0f;
								stop.forward.j = 0.0f;
							}
							sweep.stops[sweep.stop_count++] = stop;
							if (global_structure_bsp_index_get() == 0 && sweep.candidate_count < 64)
							{
								sweep.candidates[sweep.candidate_count].position = stop.position;
								sweep.candidates[sweep.candidate_count].forward = stop.forward;
								sweep.candidates[sweep.candidate_count].floor = normal.k;
								sweep.candidates[sweep.candidate_count].fogged = cluster->fog_designator != NONE;
								sweep.candidate_count++;
							}
							found = 1;
						}
					}
				}
			}
		}
		clusters_seen++;
		clusters_placed += found;
	}
	note("bsp %d: %ld materials, %ld stops for %ld shaders (%ld surfaces had no place), %ld stops in all with the views of %ld of %ld clusters", (int)global_structure_bsp_index_get(),
		considered, placed, seen_count, no_place, sweep.stop_count, clusters_placed, clusters_seen);
}

/* ---------- the objects', effects' and damage phases */

/* whether an earlier run did this tag (GAME_DIGEST_SWEEP_SKIP): the same tag of another map draws the same shaders */
static int skipped_before(char const *kind, char const *name)
{
	char line[512];
	size_t length;
	char const *at = sweep.skip;

	if (!at)
		return 0;
	snprintf(line, sizeof(line), "%s %s\n", kind, name);
	length = strlen(line);
	while ((at = strstr(at, line)) != NULL)
	{
		if (at == sweep.skip || at[-1] == '\n')
			return 1;
		at += length;
	}
	return 0;
}

static void list_make(unsigned long const *groups, int group_count, char const *kind)
{
	int i;

	sweep.list_count = 0;
	sweep.skipped = 0;
	for (i = 0; i < group_count; i++)
	{
		struct tag_iterator iterator;
		long tag_index;

		tag_iterator_new(&iterator, groups[i]);
		for (tag_index = tag_iterator_next(&iterator); tag_index != NONE && sweep.list_count < (long)(sizeof(sweep.list) / sizeof(sweep.list[0]));
			tag_index = tag_iterator_next(&iterator))
		{
			/* (a flag, a ball or a skull is the game engine's: it halts the game to delete one) */
			if (groups[i] == 'weap')
			{
				struct weapon_definition const *weapon = (struct weapon_definition const *)tag_get('weap', tag_index);

				if (weapon && ((weapon->weapon.flags >> 3) & 1))
				{
					if (strcmp(kind, "object") || skipped_before(kind, tag_get_name(tag_index)))
					{
						note("skipped %s (a flag)", tag_get_name(tag_index));
						continue;
					}
					/* (the objects' tour makes one and leaves it: only the weapons' tour, which carries and drops them, may not have one) */
					sweep.camouflage[sweep.list_count] = 2;
					sweep.list[sweep.list_count++] = tag_index;
					continue;
				}
			}
			if (skipped_before(kind, tag_get_name(tag_index)))
			{
				sweep.skipped++;
				continue;
			}
			sweep.camouflage[sweep.list_count] = 0;
			sweep.list[sweep.list_count++] = tag_index;
		}
	}
	/* the units again, active-camouflaged (its pixel shaders are another's, and the tour of the objects is the only one that makes a unit): after the others */
	if (!strcmp(kind, "object"))
	{
		long count = sweep.list_count, n;

		for (n = 0; n < count && sweep.list_count < (long)(sizeof(sweep.list) / sizeof(sweep.list[0])); n++)
		{
			long group = tag_get_group_tag(sweep.list[n]);

			if ((group == 'bipd' || group == 'vehi') && !skipped_before("camo", tag_get_name(sweep.list[n])))
			{
				sweep.camouflage[sweep.list_count] = 1;
				sweep.list[sweep.list_count++] = sweep.list[n];
			}
		}
	}
}

static int stage_find(void)
{
	short local_player_index = local_player_get_next(NONE);
	long player_index, unit_index;
	real_point3d origin;
	real_vector3d forward, up;
	int i, best = -1;
	real best_distance = -1.0f;

	if (local_player_index == NONE)
		return 0;
	player_index = local_player_get_player_index(local_player_index);
	if (player_index == NONE)
		return 0;
	unit_index = player_get(player_index)->unit_index;
	if (unit_index == NONE)
		return 0;
	object_get_origin(unit_index, &origin);
	object_get_orientation(unit_index, &forward, &up);
	/* (the objects and effects are shown away from the player, whom the scripted player's pick-ups and fire would meet: the farthest open floor of the
	first bsp the clusters' views found, else where the player stands) */
	for (i = 0; i < sweep.candidate_count; i++)
	{
		real dx;
		real dy;
		real dz;

		if (sweep.stage_variant == 1 && !sweep.candidates[i].fogged)
			continue;
		dx = sweep.candidates[i].position.x - origin.x, dy = sweep.candidates[i].position.y - origin.y, dz = sweep.candidates[i].position.z - origin.z;
		real distance = dx * dx + dy * dy + dz * dz + (sweep.candidates[i].floor > 0.8f ? 1.0e9f : 0.0f);

		if (distance > best_distance)
		{
			best_distance = distance;
			best = i;
		}
	}
	if (best < 0 && sweep.stage_variant == 1)
	{
		note("stage: no cluster with fog among the %d views", sweep.candidate_count);
		return 0;
	}
	if (best >= 0)
	{
		sweep.stage_position = sweep.candidates[best].position;
		sweep.stage_position.z += 0.0f;
		sweep.stage_forward = sweep.candidates[best].forward;
		note("stage: cluster view %d of %d, floor %.2f%s", best, sweep.candidate_count, (double)sweep.candidates[best].floor, sweep.candidates[best].fogged ? ", fog" : "");
	}
	else
	{
		sweep.stage_position = origin;
		sweep.stage_position.z += 0.62f;
		sweep.stage_forward = forward;
	}
	sweep.stage_forward.k = 0.0f;
	vec_normalize(&sweep.stage_forward);
	if (sweep.stage_forward.i == 0.0f && sweep.stage_forward.j == 0.0f)
		sweep.stage_forward.i = 1.0f;
	return 1;
}

/* ---------- the weapons' phase: each weapon in the player's hands, ready, fired by the scripted player, and zoomed */

static void phase_weapons(void)
{
	enum { WEAPON_TICKS = 100, ZOOM_AT = 55 };
	short local_player_index = local_player_get_next(NONE);
	long player_index = local_player_index == NONE ? NONE : local_player_get_player_index(local_player_index);
	long unit_index = player_index == NONE ? NONE : player_get(player_index)->unit_index;
	struct unit_datum *unit;

	if (unit_index == NONE)
		return;
	unit = unit_get(unit_index);
	if (sweep.stop_index >= sweep.list_count)
	{
		unit->unit.desired_zoom_level = NONE;
		next_phase();
		return;
	}
	if (sweep.frame_in_stop == 0)
	{
		long tag_index = sweep.list[sweep.stop_index];
		struct object_placement_data data;
		real_point3d origin;
		long weapon_index;

		note("weapon stop %ld %s", sweep.stop_index, tag_get_name(tag_index));
		object_get_origin(unit_index, &origin);
		object_placement_data_new(&data, tag_index, NONE);
		data.position = origin;
		data.position.z += 0.3f;
		data.forward.i = 1.0f;
		data.forward.j = 0.0f;
		data.forward.k = 0.0f;
		data.up.i = 0.0f;
		data.up.j = 0.0f;
		data.up.k = 1.0f;
		unit->unit.desired_zoom_level = NONE;
		weapon_index = object_new(&data);
		if (weapon_index == NONE || !unit_add_weapon_to_inventory(unit_index, weapon_index, _unit_add_weapon_replace))
			note("  (not carried)");
	}
	if (sweep.frame_in_stop >= 10 && sweep.frame_in_stop < ZOOM_AT)
	{
		/* the weapon's own function inputs (heat, battery, overcharge, its light), which the weapon's shaders glow by: held at their extremes in turn, as
		firing and charging would make them */
		long weapon_index = unit_inventory_get_weapon(unit_index, unit->unit.current_weapon_index);

		if (weapon_index != NONE && object_try_and_get_and_verify_type(weapon_index, 0xFFFFFFFFul))
		{
			struct weapon_datum *weapon = weapon_get(weapon_index);

			weapon->weapon.heat = sweep.frame_in_stop < 25 ? 1.0f : 0.0f;
			weapon->weapon.age = sweep.frame_in_stop >= 25 && sweep.frame_in_stop < 40 ? 1.0f : 0.0f;
			weapon->weapon.overcharged = sweep.frame_in_stop >= 40 ? 1.0f : 0.0f;
			weapon->weapon.integrated_light_power = 1.0f;
		}
	}
	if (sweep.frame_in_stop == 0)
	{
	}
	else if (sweep.frame_in_stop == ZOOM_AT)
	{
		struct weapon_definition const *weapon = (struct weapon_definition const *)tag_get('weap', sweep.list[sweep.stop_index]);

		if (weapon && weapon->weapon.zoom_level_count > 0)
		{
			unit->unit.desired_zoom_level = 0;
			note("  zoom 0 of %d", (int)weapon->weapon.zoom_level_count);
		}
	}
	else if (sweep.frame_in_stop == ZOOM_AT + 20)
	{
		struct weapon_definition const *weapon = (struct weapon_definition const *)tag_get('weap', sweep.list[sweep.stop_index]);

		if (weapon && weapon->weapon.zoom_level_count > 1)
		{
			unit->unit.desired_zoom_level = 1;
			note("  zoom 1");
		}
	}
	if (++sweep.frame_in_stop >= WEAPON_TICKS)
	{
		sweep.frame_in_stop = 0;
		sweep.stop_index++;
	}
}

/* ---------- the scripts' phase: the level's scripts that play its cutscenes, run by name (GAME_DIGEST_SWEEP_SCRIPTS), each until its cinematic ends */

#include "hs/hs.h"

static void phase_scripts(void)
{
	enum { SCRIPT_MINIMUM = 90, SCRIPT_MAXIMUM = 5400 };
	char names[2048], *name, *cursor;
	long index = 0;

	snprintf(names, sizeof(names), "%s", sweep.script_names);
	for (name = strtok_r(names, ",", &cursor); name; name = strtok_r(NULL, ",", &cursor), index++)
		if (index == sweep.stop_index)
			break;
	if (!name)
	{
		next_phase();
		return;
	}
	if (sweep.frame_in_stop == 0)
	{
		/* (a dormant script is woken, a static one called) */
		char line[128];
		struct hs_script const *script = NULL;
		long script_index;

		for (script_index = 0; script_index < global_scenario->hs_scripts.count; script_index++)
		{
			struct hs_script const *candidate = TAG_BLOCK_GET_ELEMENT(&global_scenario->hs_scripts, script_index, struct hs_script);

			if (candidate && !strcmp(candidate->name, name))
				script = candidate;
		}
		snprintf(line, sizeof(line), "%s%s", script && script->script_type == 1 ? "wake " : "", name);
		note("script %ld %s (%s): %s", sweep.stop_index, name, script ? (script->script_type == 1 ? "dormant" : script->script_type == 3 ? "static" :
			script->script_type == 0 ? "startup" : "other") : "not in the scenario", line);
		if (script)
			hs_compile_and_evaluate(line);
		sweep.script_running = 0;
	}
	sweep.script_running++;
	sweep.frame_in_stop++;
	if (sweep.script_running >= SCRIPT_MAXIMUM || (sweep.script_running >= SCRIPT_MINIMUM && !cinematic_in_progress()))
	{
		note("script %s ended after %ld ticks", name, sweep.script_running);
		sweep.frame_in_stop = 0;
		sweep.stop_index++;
	}
}

/* ---------- the frame */

static void next_phase(void)
{
	static unsigned long const effects[] = { 'effe' };
	static unsigned long const damages[] = { 'jpt!' };
	static unsigned long const weapons[] = { 'weap' };

	sweep.stops_ready = 0;
	sweep.stop_index = 0;
	sweep.frame_in_stop = 0;
	sweep.object_index = NONE;
	for (;;)
	{
		if (sweep.phase == 0 && (sweep.phases & PHASE_BSP))
		{
			sweep.phase = PHASE_BSP;
			sweep.bsp_index = 0;
			sweep.bsp_count = (short)global_scenario->structure_bsp_references.count;
			note("phase bsp: %d structure bsps", (int)sweep.bsp_count);
			return;
		}
		if (sweep.phase < PHASE_OBJECTS && (sweep.phases & PHASE_OBJECTS))
		{
			sweep.phase = PHASE_OBJECTS;
			list_make(object_groups, (int)(sizeof(object_groups) / sizeof(object_groups[0])), "object");
			note("phase objects: %ld object tags (%ld done by an earlier run)", sweep.list_count, sweep.skipped);
			return;
		}
		if (sweep.phase < PHASE_EFFECTS && (sweep.phases & PHASE_EFFECTS))
		{
			sweep.phase = PHASE_EFFECTS;
			list_make(effects, 1, "effect");
			note("phase effects: %ld effect tags (%ld done by an earlier run)", sweep.list_count, sweep.skipped);
			return;
		}
		if (sweep.phase < PHASE_DAMAGE && (sweep.phases & PHASE_DAMAGE))
		{
			sweep.phase = PHASE_DAMAGE;
			list_make(damages, 1, "damage");
			note("phase damage: %ld damage effect tags (%ld done by an earlier run)", sweep.list_count, sweep.skipped);
			return;
		}
		if (sweep.phase < PHASE_WEAPONS && (sweep.phases & PHASE_WEAPONS))
		{
			sweep.phase = PHASE_WEAPONS;
			list_make(weapons, 1, "weapon");
			note("phase weapons: %ld weapon tags (%ld done by an earlier run)", sweep.list_count, sweep.skipped);
			return;
		}
		if (sweep.phase < PHASE_SCRIPTS && (sweep.phases & PHASE_SCRIPTS))
		{
			char const *names = getenv("GAME_DIGEST_SWEEP_SCRIPTS");

			sweep.phase = PHASE_SCRIPTS;
			snprintf(sweep.script_names, sizeof(sweep.script_names), "%s", names ? names : "");
			sweep.list_count = 0;
			note("phase scripts: %s", sweep.script_names);
			return;
		}
		if (sweep.phase < PHASE_LIT && (sweep.phases & PHASE_LIT))
		{
			sweep.phase = PHASE_LIT;
			sweep.bsp_index = 0;
			sweep.bsp_count = (short)global_scenario->structure_bsp_references.count;
			lights_collect();
			note("phase lit: the bsps again, a dynamic light at each stop (%d light definitions)", sweep.light_count);
			return;
		}
		sweep.phase = -1;
		note("sweep done at frame %ld", sweep.frames);
		if (getenv("GAME_DIGEST_SWEEP_EXIT"))
			sweep.exit_countdown = 4;
		return;
	}
}

/* the map's light definitions (the lit tour's, and the objects' second go's) */
static void lights_collect(void)
{
	struct tag_iterator iterator;
	long tag_index;

	if (sweep.light_count)
		return;
	tag_iterator_new(&iterator, 'ligh');
	for (tag_index = tag_iterator_next(&iterator); tag_index != NONE && sweep.light_count < 256; tag_index = tag_iterator_next(&iterator))
		sweep.lights[sweep.light_count++] = tag_index;
}

static void phase_bsp(void)
{
	if (!sweep.stops_ready)
	{
		if (global_structure_bsp_index_get() != sweep.bsp_index)
		{
			/* (a script's switch_bsp: the player stays where he was, the camera tours the new one) */
			note("switch_bsp %d", (int)sweep.bsp_index);
			scenario_switch_structure_bsp(sweep.bsp_index);
			sweep.frame_in_stop = -2;
		}
		if (sweep.frame_in_stop < 0)
		{
			sweep.frame_in_stop++;
			return;
		}
		bsp_stops_make();
		sweep.stops_ready = 1;
		sweep.stop_index = 0;
		sweep.frame_in_stop = 0;
	}
	if (sweep.stop_index >= sweep.stop_count)
	{
		sweep.bsp_index++;
		if (sweep.bsp_index >= sweep.bsp_count)
		{
			/* (and back to the first, where the player is, for the phases after) */
			if (global_structure_bsp_index_get() != 0)
				scenario_switch_structure_bsp(0);
			next_phase();
		}
		else
			sweep.stops_ready = 0;
		return;
	}
	{
		struct stop *stop = &sweep.stops[sweep.stop_index];

		camera_target(&stop->position, &stop->forward);
		if (sweep.frame_in_stop == 0 && sweep.phase == PHASE_LIT && sweep.light_count)
		{
			/* (between the camera and what it looks at, a hand's breadth from the surface it looks at when it looks at one) */
			real_point3d at = stop->position;
			real_vector3d direction = stop->forward;

			vec_normalize(&direction);
			at.x += direction.i * (stop->kind ? 0.4f : 0.7f);
			at.y += direction.j * (stop->kind ? 0.4f : 0.7f);
			at.z += direction.k * (stop->kind ? 0.4f : 0.7f);
			light_new_unattached(sweep.lights[sweep.stop_index % sweep.light_count], NONE, 0, &at, &direction, 1.0f);
		}
		if (sweep.frame_in_stop == 0)
			note("%s %d stop %ld %s %s (%ld)", sweep.phase == PHASE_LIT ? "lit bsp" : "bsp", (int)stop->bsp_index, sweep.stop_index, stop->kind ? "cluster" : "shader",
				stop->kind ? "up" : tag_get_name(stop->tag_index), stop->detail);
		if (++sweep.frame_in_stop >= sweep.hold)
		{
			sweep.frame_in_stop = 0;
			sweep.stop_index++;
		}
	}
}

/* the object the stop made, if it is still there (a projectile may have ended at once; the game halts on a deleted object's index) */
static void object_remove(void)
{
	if (sweep.object_index != NONE && !sweep.leave_object && object_try_and_get_and_verify_type(sweep.object_index, 0xFFFFFFFFul))
		object_delete(sweep.object_index);
	sweep.object_index = NONE;
}

static void phase_list(int objects)
{
	/* a stop per tag: made on the frame it starts, held, and taken away */
	real_vector3d view;
	real_point3d where;
	long camo = objects && sweep.stop_index < sweep.list_count && sweep.camouflage[sweep.stop_index] == 1;
	long hold = objects ? sweep.hold + 1 + (camo ? 36 : 0) : sweep.hold + (getenv("GAME_DIGEST_SWEEP_EFFECT_HOLD") ? (long)strtol(getenv("GAME_DIGEST_SWEEP_EFFECT_HOLD"), NULL, 0) : 8);

	if (objects && sweep.object_pass && sweep.stop_index < sweep.list_count && sweep.camouflage[sweep.stop_index])
	{
		/* (the second and third go make no unit camouflaged, and no flag) */
		sweep.stop_index++;
		return;
	}
	if (!sweep.stage_known)
	{
		if (!stage_find())
		{
			if (objects && sweep.stage_variant)
			{
				/* (no cluster with fog: the third go is left out) */
				sweep.list_count = 0;
				sweep.stop_index = 0;
				sweep.stage_known = 1;
				return;
			}
			return;
		}
		sweep.stage_known = 1;
	}
	if (sweep.stop_index >= sweep.list_count)
	{
		object_remove();
		if (objects && sweep.object_pass < 2)
		{
			/* (the objects again: with a dynamic light beside each, then in a cluster with fog, where the models' lighting and fog take other paths) */
			sweep.object_pass++;
			sweep.stage_known = 0;
			sweep.stage_variant = sweep.object_pass == 2;
			lights_collect();
			list_make(object_groups, (int)(sizeof(object_groups) / sizeof(object_groups[0])), sweep.object_pass == 1 ? "litobj" : "fogobj");
			note("objects, go %d (%s): %ld object tags (%ld done by an earlier run)", sweep.object_pass + 1, sweep.object_pass == 1 ? "a light beside each" : "in a cluster with fog", sweep.list_count, sweep.skipped);
			sweep.stop_index = 0;
			sweep.frame_in_stop = 0;
			return;
		}
		sweep.object_pass = 0;
		sweep.stage_variant = 0;
		sweep.stage_known = 0;
		if (sweep.phase == PHASE_EFFECTS && sweep.pass == 0 && sweep.passes >= 2)
		{
			sweep.pass = 1;
			sweep.stop_index = 0;
			sweep.frame_in_stop = 0;
			return;
		}
		sweep.pass = 0;
		next_phase();
		return;
	}
	{
		long tag_index = sweep.list[sweep.stop_index];
		real distance = 2.0f;

		if (objects)
		{
			struct _object_definition const *definition = (struct _object_definition const *)tag_get('obje', tag_index);

			if (definition)
				distance = definition->bounding_radius * 2.6f + 0.8f;
			if (distance < 1.8f)
				distance = 1.8f;
			if (distance > 14.0f)
				distance = 14.0f;
		}
		else
			distance = 1.2f;
		where.x = sweep.stage_position.x + sweep.stage_forward.i * distance;
		where.y = sweep.stage_position.y + sweep.stage_forward.j * distance;
		where.z = sweep.stage_position.z - 0.3f;
		if (sweep.frame_in_stop == 0)
		{
			sweep.leave_object = objects && sweep.camouflage[sweep.stop_index] == 2;
			note("%s stop %ld %s%s", objects ? (camo ? "camo" : sweep.object_pass == 1 ? "litobj" : sweep.object_pass == 2 ? "fogobj" : "object") : sweep.phase == PHASE_EFFECTS ? "effect" : "damage", sweep.stop_index, tag_get_name(tag_index),
				sweep.phase == PHASE_EFFECTS ? (sweep.pass ? " (air)" : " (floor)") : "");
			if (objects)
			{
				struct object_placement_data data;

				if (sweep.object_pass == 1 && sweep.light_count)
				{
					/* (above the object, a little toward the camera) */
					real_point3d at = where;
					real_vector3d down;

					at.x -= sweep.stage_forward.i * 0.5f;
					at.y -= sweep.stage_forward.j * 0.5f;
					at.z += 0.9f;
					down.i = 0.0f;
					down.j = 0.0f;
					down.k = -1.0f;
					light_new_unattached(sweep.lights[sweep.stop_index % sweep.light_count], NONE, 0, &at, &down, 1.0f);
				}
				object_placement_data_new(&data, tag_index, NONE);
				data.position = where;
				data.position.z += 0.2f;
				data.forward = sweep.stage_forward;
				data.forward.i = -data.forward.i;
				data.forward.j = -data.forward.j;
				data.up.i = 0.0f;
				data.up.j = 0.0f;
				data.up.k = 1.0f;
				sweep.object_index = object_new(&data);
				if (sweep.object_index == NONE)
					note("  (not made)");
				else if (camo)
				{
					struct unit_datum *unit = (struct unit_datum *)object_try_and_get_and_verify_type(sweep.object_index, 0xFFFFFFFFul);

					if (unit)
						unit->unit.flags |= 1UL << _unit_active_camouflaged_bit;
				}
				else if (sweep.object_pass == 1)
				{
					/* (the second go: a unit's own light and a weapon's glow on, as a fight leaves them) */
					struct weapon_datum *made = (struct weapon_datum *)object_try_and_get_and_verify_type(sweep.object_index, 0xFFFFFFFFul);

					if (made && made->object.type == _object_type_biped)
					{
						struct unit_datum *unit = (struct unit_datum *)made;

						unit->unit.integrated_light_power = 1.0f;
						unit->unit.flags |= 1UL << _unit_integrated_light_on_bit;
					}
					else if (made && made->object.type == _object_type_weapon)
					{
						made->weapon.heat = 1.0f;
						made->weapon.overcharged = 1.0f;
						made->weapon.integrated_light_power = 1.0f;
					}
				}
			}
			else if (sweep.phase == PHASE_EFFECTS)
			{
				/* (two goes at each: on the floor, pointing up, as an impact on the ground is made; then in the air in front of the camera, pointing
				back at it, as a muzzle flash or a shot in the air is) */
				real_vector3d forward = sweep.stage_forward;
				real_point3d at = where;

				if (sweep.pass == 0)
				{
					at.z = sweep.stage_position.z - 0.78f;
					forward.i = 0.0f;
					forward.j = 0.0f;
					forward.k = 1.0f;
				}
				else
				{
					forward.i = -forward.i;
					forward.j = -forward.j;
					forward.k = 0.0f;
				}
				effect_new_unattached_from_markers(tag_index, NONE, global_zero_vector3d, 1, NULL, &at, &forward, 1.0f, 1.0f, NULL, NULL, TRUE);
			}
			else
			{
				struct damage_data damage;

				damage_data_new(&damage, tag_index);
				damage.origin = damage.epicenter = sweep.stage_position;
				scenario_location_from_point(&damage.location, &sweep.stage_position);
				area_of_effect_cause_damage(&damage, NONE);
			}
		}
		if (objects)
		{
			view.i = where.x - sweep.stage_position.x;
			view.j = where.y - sweep.stage_position.y;
			view.k = where.z - sweep.stage_position.z;
			camera_target(&sweep.stage_position, &view);
		}
		else if (sweep.phase != PHASE_DAMAGE)
		{
			/* (an effect is looked at from above and behind, down at the floor it is made on: the frames cost the floor and the effect, not the vista;
			the second go looks along the floor, at the effect in the air) */
			real_point3d camera = sweep.stage_position;

			if (sweep.pass == 0)
			{
				camera.x -= sweep.stage_forward.i * 1.0f;
				camera.y -= sweep.stage_forward.j * 1.0f;
				camera.z += 1.0f;
				view.i = where.x - camera.x;
				view.j = where.y - camera.y;
				view.k = (sweep.stage_position.z - 0.78f) - camera.z;
			}
			else
			{
				view.i = where.x - camera.x;
				view.j = where.y - camera.y;
				view.k = where.z - camera.z;
			}
			camera_target(&camera, &view);
		}
		if (++sweep.frame_in_stop >= hold)
		{
			object_remove();
			sweep.frame_in_stop = 0;
			sweep.stop_index++;
		}
	}
}

/* ---------- the game's pixel shader sites

Each call of rasterizer_set_pixel_shader (the game's own code sets a pixel shader at some sixty places: rasterizer_xbox_*.c) is noted by the place it is called from
(its return address; addr2line says the function and line), with how often and the frame it was first made on. Written to GAME_DIGEST_DIR/pixel_sites.txt when the
run ends, and the last site is what replay_gx2.c writes with each new key, so that a key is told by the code that set its shader. The sites a harvest reaches, against
the sites there are, say what it has not reached. */

void __real_rasterizer_set_pixel_shader(void const *definition);
unsigned long game_digest_last_pixel_site(void);

static struct { unsigned long address, count, first_frame; } pixel_sites[1024];
static int pixel_site_count;
static unsigned long last_pixel_site;

void __wrap_rasterizer_set_pixel_shader(void const *definition)
{
	unsigned long caller = (unsigned long)__builtin_return_address(0);
	int i;

	for (i = 0; i < pixel_site_count && pixel_sites[i].address != caller; i++)
		;
	if (i == pixel_site_count && pixel_site_count < 1024)
	{
		pixel_sites[i].address = caller;
		pixel_sites[i].first_frame = replay_keys_frame();
		pixel_site_count++;
	}
	if (i < pixel_site_count)
		pixel_sites[i].count++;
	last_pixel_site = caller;
	__real_rasterizer_set_pixel_shader(definition);
}

unsigned long game_digest_last_pixel_site(void)
{
	return last_pixel_site;
}

static void pixel_sites_write(void)
{
	char const *directory = getenv("GAME_DIGEST_DIR");
	char path[512];
	FILE *file;
	int i;

	snprintf(path, sizeof(path), "%s/pixel_sites.txt", directory && *directory ? directory : ".");
	file = fopen(path, "w");
	if (!file)
		return;
	for (i = 0; i < pixel_site_count; i++)
		fprintf(file, "0x%lx %lu %lu\n", pixel_sites[i].address, pixel_sites[i].count, pixel_sites[i].first_frame);
	fclose(file);
}

__attribute__((constructor)) static void pixel_sites_start(void)
{
	if (getenv("GAME_DIGEST_KEYS"))
		atexit(pixel_sites_write);
}

/* The tour's step, once a frame, in the tick side of the loop (cheats_update is called there, and only there: the game's random numbers, its objects and its
effects may be used from here, where the frame's drawing, which holds the random seed's lock, may not) */
void __real_cheats_update(void);
void game_digest_sweep_tick(void);

void game_digest_sweep_tick(void)
{
	if (!sweep.configured)
		configure();
	if (sweep.exit_countdown && --sweep.exit_countdown == 0)
	{
		note("run ends");
		exit(0);
	}
	if (!sweep.phases || sweep.phase < 0)
		return;
	if (!global_scenario || local_player_get_next(NONE) == NONE || (cinematic_in_progress() && sweep.phase != PHASE_SCRIPTS))
		return;
	sweep.frames++;
	if (sweep.frames <= sweep.wait)
		return;
	if (!sweep.census_done)
	{
		sweep.census_done = 1;
		census();
		note("sweep starts at frame %ld, tick %ld", sweep.frames, game_time_get());
		next_phase();
	}
	sweep.cam.active = 0;
	if (sweep.flashlight)
	{
		short local_player_index = local_player_get_next(NONE);
		long player_index = local_player_index == NONE ? NONE : local_player_get_player_index(local_player_index);
		long unit_index = player_index == NONE ? NONE : player_get(player_index)->unit_index;
		struct unit_datum *unit = unit_index == NONE ? NULL : (struct unit_datum *)object_try_and_get_and_verify_type(unit_index, 0xFFFFFFFFul);

		/* (turned on as the unit's own control would, and left on: the battery is kept full) */
		if (unit && !(unit->unit.flags & (1UL << _unit_integrated_light_on_bit)))
			unit->unit.flags |= 1UL << _unit_desired_integrated_light_on_bit;
		if (unit)
			unit->unit.integrated_light_battery = 1.0f;
	}
	switch (sweep.phase)
	{
	case PHASE_BSP:
	case PHASE_LIT:
		phase_bsp();
		break;
	case PHASE_OBJECTS:
		phase_list(1);
		break;
	case PHASE_EFFECTS:
	case PHASE_DAMAGE:
		phase_list(0);
		break;
	case PHASE_WEAPONS:
		phase_weapons();
		break;
	case PHASE_SCRIPTS:
		phase_scripts();
		break;
	default:
		break;
	}
}

void __wrap_cheats_update(void)
{
	__real_cheats_update();
	game_digest_sweep_tick();
}

/* before a frame is drawn: the tour's camera, in the frame's player window */
void game_digest_sweep_render_frame(struct render_window *windows, short window_count)
{
	short index;

	if (!sweep.cam.active || sweep.phase < 0)
		return;
	for (index = 0; index < window_count; index++)
		if (!windows[index].console_window && windows[index].local_player_index != NONE)
		{
			camera_set(&windows[index], &sweep.cam.position, &sweep.cam.forward);
			break;
		}
}
