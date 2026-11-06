/*
GAME_DIGEST_SCENE.C

The digest harness's scene: a fixed camera on a handful of objects of one kind, made where the player stands, so that what the objects cost to draw
and to tick is measured on the host (the draws and passes the Direct3D layer counts, the tick part costs the sampling profiler sees) for a scene with
them against the same scene without. It is compiled with the game's flags and linked into the digest program, and does nothing unless
GAME_DIGEST_SCENE is set, so that every other run's digest is what it was.

    GAME_DIGEST_SCENE="GROUP:tag name:COUNT|GROUP:tag name:COUNT..."    (for example vehi:vehicles\banshee\banshee:6)
    GAME_DIGEST_SCENE_WAIT=N       frames of play (with a player that has a unit) before the objects are made (default 90)
    GAME_DIGEST_SCENE_DIST=D       world units from the camera to the middle of the row (default 30)
    GAME_DIGEST_SCENE_SPREAD=D     degrees between neighbouring objects as the camera sees them (default 9)
    GAME_DIGEST_SCENE_HEIGHT=H     world units the objects are made above the camera's floor (default 0: on the floor; a vehicle falls to rest)
    GAME_DIGEST_SCENE_YAW=D        degrees the camera looks along (default 0 = +x: the way the player faces is not used, the camera is fixed)
    GAME_DIGEST_SCENE_ROWS=R       rows of objects one behind the other (default 1; each row 12 units farther)
    GAME_DIGEST_SCENE_INVULNERABLE=1  (default) the objects take no damage, so that the scripted player's fire changes nothing
    GAME_DIGEST_SCENE_SEPARATION=S world units apart at least (default 6)
    GAME_DIGEST_SCENE_PIN=1        the objects are held where they were made (a flying vehicle is held up by its pilot; a made one falls)
    GAME_DIGEST_SCENE_UNARMED=1    (default) the player's weapons are taken away when the objects are made, so that the scripted player's fire hits nothing

The objects are made on one tick, in the order of the list, in a row (and rows) in front of the camera at the player's floor; the camera is the player's
position (0.62 above the unit's origin, as the sweep stands it) looking along the yaw. The tag names are matched exactly. A line goes to
GAME_DIGEST_DIR/scene.txt for what was made (and what was not).
*/

#include "cseries.h"
#include "cseries/errors.h"
#include "cache/cache_files.h"
#include "cutscene/cinematics.h"
#include "ai/actors.h"
#include "ai/props.h"
#include "effects/contrails.h"
#include "effects/particles.h"
#include "game/game.h"
#include "game/local_players.h"
#include "game/players.h"
#include "math/real_math.h"
#include "objects/objects.h"
#include "objects/damage.h"
#include "render/render.h"
#include "render/render_cameras.h"
#include "scenario/scenario.h"
#include "tag_files/tag_groups.h"
#include "tag_files/tag_files.h"
#include "units/units.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef fopen
#undef fprintf

extern struct data_array *light_data;
extern struct data_array *effect_data;
void game_digest_scene_render_frame(struct render_window *windows, short window_count);
void __real_cheats_update(void);

#define SCENE_MAXIMUM 64

static struct
{
	int configured;
	int enabled;
	FILE *log;
	char spec[1024];
	long wait;
	real dist, spread, height, yaw;
	long rows;
	int invulnerable;
	long frames;
	int made;
	long objects[SCENE_MAXIMUM];
	real_point3d where[SCENE_MAXIMUM];
	real_vector3d forward[SCENE_MAXIMUM];
	int pin;
	int unarmed;
	real separation;
	long object_count;
	struct { int active; real_point3d position; real_vector3d forward; } cam;
} scene;

static void note(char const *format, ...) __attribute__((format(printf, 1, 2)));

#include <stdarg.h>

static void note(char const *format, ...)
{
	va_list arguments;

	if (!scene.log)
		return;
	va_start(arguments, format);
	vfprintf(scene.log, format, arguments);
	va_end(arguments);
	fputc('\n', scene.log);
	fflush(scene.log);
}

static void configure(void)
{
	char const *spec = getenv("GAME_DIGEST_SCENE");
	char const *directory = getenv("GAME_DIGEST_DIR");
	char path[512];

	scene.configured = 1;
	if (!spec || !*spec)
		return;
	scene.enabled = 1;
	strncpy(scene.spec, spec, sizeof(scene.spec) - 1);
	scene.wait = getenv("GAME_DIGEST_SCENE_WAIT") ? strtol(getenv("GAME_DIGEST_SCENE_WAIT"), NULL, 0) : 90;
	scene.dist = getenv("GAME_DIGEST_SCENE_DIST") ? (real)atof(getenv("GAME_DIGEST_SCENE_DIST")) : 30.0f;
	scene.spread = getenv("GAME_DIGEST_SCENE_SPREAD") ? (real)atof(getenv("GAME_DIGEST_SCENE_SPREAD")) : 9.0f;
	scene.height = getenv("GAME_DIGEST_SCENE_HEIGHT") ? (real)atof(getenv("GAME_DIGEST_SCENE_HEIGHT")) : 0.0f;
	scene.yaw = getenv("GAME_DIGEST_SCENE_YAW") ? (real)atof(getenv("GAME_DIGEST_SCENE_YAW")) : 0.0f;
	scene.separation = getenv("GAME_DIGEST_SCENE_SEPARATION") ? (real)atof(getenv("GAME_DIGEST_SCENE_SEPARATION")) : 6.0f;
	scene.pin = getenv("GAME_DIGEST_SCENE_PIN") ? (int)strtol(getenv("GAME_DIGEST_SCENE_PIN"), NULL, 0) : 0;
	scene.unarmed = getenv("GAME_DIGEST_SCENE_UNARMED") ? (int)strtol(getenv("GAME_DIGEST_SCENE_UNARMED"), NULL, 0) : 1;
	scene.rows = getenv("GAME_DIGEST_SCENE_ROWS") ? strtol(getenv("GAME_DIGEST_SCENE_ROWS"), NULL, 0) : 1;
	scene.invulnerable = getenv("GAME_DIGEST_SCENE_INVULNERABLE") ? (int)strtol(getenv("GAME_DIGEST_SCENE_INVULNERABLE"), NULL, 0) : 1;
	if (directory)
	{
		snprintf(path, sizeof(path), "%s/scene.txt", directory);
		scene.log = fopen(path, "wb");
	}
	scene.object_count = 0;
}

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

static void camera_set(struct render_window *window, real_point3d const *position, real_vector3d const *forward)
{
	real_vector3d f = *forward, up, right;

	vec_normalize(&f);
	up.i = 0.0f;
	up.j = 0.0f;
	up.k = 1.0f;
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

static long tag_find(unsigned long group, char const *name)
{
	struct tag_iterator iterator;
	long tag_index;

	tag_iterator_new(&iterator, group);
	for (tag_index = tag_iterator_next(&iterator); tag_index != NONE; tag_index = tag_iterator_next(&iterator))
		if (!strcmp(tag_get_name(tag_index), name))
			return tag_index;
	return NONE;
}

/* the objects of the list, in a row (rows) in front of the camera */
static void make(real_point3d const *camera_at, real_point3d const *floor_at)
{
	char spec[1024];
	char *entry, *save = NULL;
	long total = 0, placed = 0;
	real yaw = scene.yaw * 3.14159265f / 180.0f;

	strcpy(spec, scene.spec);
	/* (first pass: how many in all, so that the row is centred) */
	for (entry = strtok_r(spec, "|", &save); entry; entry = strtok_r(NULL, "|", &save))
	{
		char const *last = strrchr(entry, ':');

		total += last ? strtol(last + 1, NULL, 0) : 1;
	}
	strcpy(spec, scene.spec);
	save = NULL;
	for (entry = strtok_r(spec, "|", &save); entry; entry = strtok_r(NULL, "|", &save))
	{
		char group_text[5] = { 0 };
		char *colon = strchr(entry, ':');
		char *last = strrchr(entry, ':');
		long count, k;
		unsigned long group;
		long tag_index;
		char name[256];

		if (!colon || colon == last)
			continue;
		memcpy(group_text, entry, 4);
		group = ((unsigned long)(unsigned char)group_text[0] << 24) | ((unsigned long)(unsigned char)group_text[1] << 16) |
			((unsigned long)(unsigned char)group_text[2] << 8) | (unsigned long)(unsigned char)group_text[3];
		snprintf(name, sizeof(name), "%.*s", (int)(last - colon - 1), colon + 1);
		count = strtol(last + 1, NULL, 0);
		tag_index = tag_find(group, name);
		if (tag_index == NONE)
		{
			note("tag not found: %s %s", group_text, name);
			continue;
		}
		for (k = 0; k < count && scene.object_count < SCENE_MAXIMUM; k++, placed++)
		{
			struct object_placement_data data;
			long row = scene.rows > 1 ? placed % scene.rows : 0;
			long column = scene.rows > 1 ? placed / scene.rows : placed;
			long columns = scene.rows > 1 ? (total + scene.rows - 1) / scene.rows : total;
			real angle = yaw + ((real)column - ((real)columns - 1.0f) * 0.5f) * scene.spread * 3.14159265f / 180.0f;
			real distance = scene.dist + 12.0f * (real)row;
			long index;

			object_placement_data_new(&data, tag_index, NONE);
			/* (the place nearest the intended one that is inside the map's world and not on another object: the beach has holes, and an object
			outside the structure is not connected to the map, so not drawn) */
			{
				int attempt, found = 0;

				for (attempt = 0; attempt < 400 && !found; attempt++)
				{
					real shift = ((attempt % 9) - 4) * 4.0f * 3.14159265f / 180.0f;
					real nearer = (real)(attempt / 9) * 2.0f;
					real use_angle = angle + (attempt ? shift : 0.0f);
					real use_distance = distance - (attempt ? nearer : 0.0f);
					real_point3d at;
					struct location location;
					long other;
					int clear = 1;

					if (use_distance < 4.0f)
						break;
					at.x = floor_at->x + use_distance * (real)cos(use_angle);
					at.y = floor_at->y + use_distance * (real)sin(use_angle);
					at.z = floor_at->z + scene.height;
					scenario_location_from_point(&location, &at);
					if (location.cluster_index == NONE)
						continue;
					for (other = 0; other < scene.object_count; other++)
						if ((scene.where[other].x - at.x) * (scene.where[other].x - at.x) + (scene.where[other].y - at.y) * (scene.where[other].y - at.y) <
							scene.separation * scene.separation)
							clear = 0;
					if (!clear)
						continue;
					data.position = at;
					found = 1;
				}
				if (!found)
				{
					note("no place for %s (%ld)", name, placed);
					continue;
				}
			}
			data.forward.i = (real)cos(angle + 3.14159265f * 0.5f * 0.0f + 0.9f * (real)(placed % 3));
			data.forward.j = (real)sin(angle + 0.9f * (real)(placed % 3));
			data.forward.k = 0.0f;
			data.up.i = 0.0f;
			data.up.j = 0.0f;
			data.up.k = 1.0f;
			index = object_new(&data);
			if (index == NONE)
			{
				note("not made: %s %s (%ld)", group_text, name, placed);
				continue;
			}
			if (scene.invulnerable)
			{
				struct object_datum *object = (struct object_datum *)object_get_and_verify_type(index, 0xFFFFFFFFul);

				if (object)
					SET_FLAG(object->object.damage_flags, _object_cannot_take_damage_bit, TRUE);
			}
			scene.where[scene.object_count] = data.position;
			scene.forward[scene.object_count] = data.forward;
			scene.objects[scene.object_count++] = index;
			note("made %s %s at %.1f %.1f %.1f index %lx", group_text, name, (double)data.position.x, (double)data.position.y, (double)data.position.z,
				(unsigned long)index);
		}
	}
	scene.cam.active = 1;
	scene.cam.position = *camera_at;
	scene.cam.forward.i = (real)cos(yaw);
	scene.cam.forward.j = (real)sin(yaw);
	scene.cam.forward.k = 0.0f;
	scene.made = 1;
}

/* the particles alive now, by their tag, and how many hang on the scene's objects */
static void particle_census(void)
{
	struct data_iterator iterator;
	struct particle_datum *particle;
	struct { long definition; long count; } kinds[48];
	int kind_count = 0, k, j;
	long total = 0, attached = 0;

	data_iterator_new(&iterator, particle_data);
	while ((particle = (struct particle_datum *)data_iterator_next(&iterator)) != NULL)
	{
		total++;
		for (k = 0; k < scene.object_count; k++)
			if (particle->object_index == scene.objects[k])
				attached++;
		for (k = 0; k < kind_count && kinds[k].definition != particle->definition_index; k++)
			;
		if (k == kind_count && kind_count < 48)
		{
			kinds[kind_count].definition = particle->definition_index;
			kinds[kind_count++].count = 0;
		}
		if (k < kind_count)
			kinds[k].count++;
	}
	for (k = 0; k < kind_count; k++)
		for (j = k + 1; j < kind_count; j++)
			if (kinds[j].count > kinds[k].count)
			{
				long d = kinds[k].definition, c = kinds[k].count;

				kinds[k] = kinds[j];
				kinds[j].definition = d;
				kinds[j].count = c;
			}
	note("tick %ld particles %ld (on scene objects %ld)", game_time_get(), total, attached);
	{
		struct object_iterator objects;
		long projectiles = 0, vehicles = 0, bipeds = 0, all = 0;

		object_iterator_new(&objects, 0xFFFFFFFFul, 0);
		while (object_iterator_next(&objects))
		{
			struct object_datum *object = (struct object_datum *)object_get(objects.index);

			all++;
			projectiles += object->object.type == _object_type_projectile;
			vehicles += object->object.type == _object_type_vehicle;
			bipeds += object->object.type == _object_type_biped;
		}
		note("tick %ld census: objects %ld (projectiles %ld, vehicles %ld, bipeds %ld) lights %d contrails %d effects %d particles %d actors %d props %d", game_time_get(),
			all, projectiles, vehicles, bipeds, light_data ? light_data->actual_count : -1, contrail_data ? contrail_data->actual_count : -1,
			effect_data ? effect_data->actual_count : -1, particle_data ? particle_data->actual_count : -1, actor_data ? actor_data->actual_count : -1,
			prop_data ? prop_data->actual_count : -1);
	}
	for (k = 0; k < kind_count && k < 8; k++)
		note("    %ld x %s", kinds[k].count, tag_get_name(kinds[k].definition));
}

void game_digest_scene_tick(void)
{
	short local_player_index;
	long player_index, unit_index;
	real_point3d origin, camera_at;

	if (!scene.configured)
		configure();
	if (!scene.enabled)
		return;
	if (scene.made)
	{
		if (scene.pin)
		{
			/* (held where it was made, as a flying vehicle in a fight is held up by its pilot: a made vehicle falls, which the shadows and the lights
			under it would make a different scene of) */
			long k;

			for (k = 0; k < scene.object_count; k++)
			{
				struct object_datum *object = (struct object_datum *)object_try_and_get_and_verify_type(scene.objects[k], 0xFFFFFFFFul);
				real_point3d at;
				real_vector3d up;

				if (!object)
					continue;
				object_get_origin(scene.objects[k], &at);
				if (fabs(at.z - scene.where[k].z) > 0.05f || fabs(at.x - scene.where[k].x) > 0.05f || fabs(at.y - scene.where[k].y) > 0.05f)
				{
					up.i = 0.0f;
					up.j = 0.0f;
					up.k = 1.0f;
					object_set_position(scene.objects[k], &scene.where[k], &scene.forward[k], &up);
				}
				object->object.translational_velocity.i = 0.0f;
				object->object.translational_velocity.j = 0.0f;
				object->object.translational_velocity.k = 0.0f;
			}
		}
		/* (where the objects are now, every 60 frames: a vehicle falls to rest, and one in a cluster nobody sees is not drawn) */
		if (++scene.frames % 60 == 0)
		{
			long k;

			particle_census();

			for (k = 0; k < scene.object_count; k++)
			{
				struct object_datum *object = (struct object_datum *)object_try_and_get_and_verify_type(scene.objects[k], 0xFFFFFFFFul);
				real_point3d at;
				struct location location;

				if (!object)
				{
					note("tick %ld object %ld is gone", game_time_get(), k);
					continue;
				}
				object_get_origin(scene.objects[k], &at);
				object_get_location(scene.objects[k], &location);
				note("tick %ld object %ld at %.1f %.1f %.1f cluster %d flags %lx", game_time_get(), k, (double)at.x, (double)at.y, (double)at.z,
					(int)location.cluster_index, (unsigned long)object->object.flags);
			}
		}
		return;
	}
	if (!global_scenario || (local_player_index = local_player_get_next(NONE)) == NONE || cinematic_in_progress())
		return;
	player_index = local_player_get_player_index(local_player_index);
	if (player_index == NONE)
		return;
	unit_index = player_get(player_index)->unit_index;
	if (unit_index == NONE)
		return;
	if (++scene.frames <= scene.wait)
		return;
	object_get_origin(unit_index, &origin);
	camera_at = origin;
	camera_at.z += 0.62f;
	note("scene at frame %ld, tick %ld, player at %.1f %.1f %.1f", scene.frames, game_time_get(), (double)origin.x, (double)origin.y, (double)origin.z);
	if (scene.unarmed)
		unit_delete_all_weapons(unit_index);
	make(&camera_at, &origin);
}

void __wrap_cheats_update(void)
{
	__real_cheats_update();
	game_digest_scene_tick();
}

void game_digest_scene_render_frame(struct render_window *windows, short window_count)
{
	short index;

	if (!scene.cam.active)
		return;
	for (index = 0; index < window_count; index++)
		if (!windows[index].console_window && windows[index].local_player_index != NONE)
		{
			camera_set(&windows[index], &scene.cam.position, &scene.cam.forward);
			break;
		}
}
