/*
RENDER_OBJECTS.C

symbols in this file:
0017A740 00a0:
	_object_get_render_bounding_sphere (0000)
0017A7E0 0040:
	_render_objects_initialize (0000)
0017A820 0010:
	_render_objects_initialize_for_new_map (0000)
0017A830 0020:
	_render_objects_dispose_from_old_map (0000)
0017A850 0010:
	_render_objects_dispose (0000)
0017A860 0070:
	_code_0017a860 (0000)
0017A8D0 00b0:
	_code_0017a8d0 (0000)
0017A980 0060:
	_code_0017a980 (0000)
0017A9E0 0480:
	_code_0017a9e0 (0000)
0017AE60 00a0:
	_code_0017ae60 (0000)
0017AF00 00d0:
	_code_0017af00 (0000)
0017AFD0 00b0:
	_code_0017afd0 (0000)
0017B080 0100:
	_code_0017b080 (0000)
0017B180 0060:
	_code_0017b180 (0000)
0017B1E0 02d0:
	_code_0017b1e0 (0000)
0017B4B0 0320:
	_code_0017b4b0 (0000)
0017B7D0 0130:
	_code_0017b7d0 (0000)
0017B900 0050:
	_object_get_cached_render_lighting (0000)
0017B950 0270:
	_code_0017b950 (0000)
0017BBC0 0030:
	_code_0017bbc0 (0000)
0017BBF0 00c0:
	_render_objects (0000)
0017BCB0 0070:
	_render_object_shadows (0000)
0029FEB0 0016:
	??_C@_0BG@FKOKPMPP@render_object_shadows?$AA@ (0000)
0029FEC8 000f:
	??_C@_0P@LMDLJPAO@render_objects?$AA@ (0000)
0029FED8 001c:
	??_C@_0BM@HFAFBIOC@cached_object_render_states?$AA@ (0000)
0029FEF4 0027:
	??_C@_0CH@NBGGJMPB@c?3?2halo?2SOURCE?2render?2render_obj@ (0000)
0029FF1C 001c:
	??_C@_0BM@GFBNGBOK@cached?5object?5render?5states?$AA@ (0000)
0029FF38 0023:
	??_C@_0CD@FKDHABCL@MAXIMUM_RENDERED_OBJECTS?5exceede@ (0000)
0029FF5C 000c:
	??_C@_0M@EIIDIBKF@inactive?5?$CFs?$AA@ (0000)
0029FF68 0022:
	??_C@_0CC@IDEANNOJ@?$CD?$CD?$CD?5ERROR?5invalid?5modifier?5shade@ (0000)
0029FF8C 000f:
	??_C@_0P@POPICHFJ@data?9?$DOlighting?$AA@ (0000)
0029FF9C 0014:
	??_C@_0BE@MCONOJLA@parent_model_effect?$AA@ (0000)
0029FFB0 0091:
	??_C@_0JB@OHAJFPFN@state?9?$DOlighting?4point_light_indi@ (0000)
002A0044 002f:
	??_C@_0CP@MCHIMIAE@state?9?$DOdesired_lighting?4distant_@ (0000)
002A0074 0004:
	__real@4111745c (0000)
002A0078 0004:
	__real@3e428f5c (0000)
0030D588 0bf8:
	_render_shadows (0000)
004C0078 047e:
	_bss_004c0078 (0000)
	_debug_inactive_objects (047c)
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries/errors.h"
#include "cseries/profile.h"
#include "memory/data.h"
#include "math/real_math.h"
#include "bitmaps/bitmaps.h"
#include "render/render.h"
#include "camera/camera_scripting.h"
#include "editor/editor_stubs.h"
#include "rasterizer/rasterizer.h"
#include "render/render_cameras_internal.h"
#include "render/render_debug.h"
#include "objects/objects.h"
#include "objects/object_definitions.h"
#include "objects/object_lights.h"
#include "objects/object_lights_rendering.h"
#include "objects/widgets/widgets.h"
#include "units/units.h"
#include "models/models.h"
#include "shaders/shader_definitions.h"
#include "shaders/shaders.h"
#include "game/players.h"
#include "interface/first_person_weapons.h"
#include "camera/director.h"
#include "cutscene/cinematics.h"
#include "tag_files/tag_files.h"
#include "saved games/game_state.h"
#include "rasterizer/rasterizer_console_vars.h"
#include "rasterizer/rasterizer_model_types.h"
#include "render_governor.h" /* port: port/linux/game/render_governor.c */

/* ---------- constants */

enum
{
	/* the native builds' larger render state cache (halo_port_capacity.h) */
	MAXIMUM_CACHED_OBJECT_RENDER_STATES = HALO_PORT_MAXIMUM_CACHED_OBJECT_RENDER_STATES,
	NUMBER_OF_SHADOW_VOLUME_PLANES = 6,
	MAXIMUM_OBJECT_RENDER_STATE_AGE = 1000,
	OBJECT_RENDER_STATE_LARGE_INTERVAL = 3,
	OBJECT_RENDER_STATE_SMALL_INTERVAL = 10,
};

enum
{
	_render_model_effect_type_none = 0,
	_render_model_effect_type_active_camouflage,
	_render_model_effect_type_modifier,
};

enum
{
	_render_model_shadow_bit = 1,
	_render_model_no_planar_fog_bit,
};

enum
{
	_render_planar_fog_mode_normal = 1,
};

#define OBJECT_RENDER_STATE_LARGE_PIXELS 400.f
#define OBJECT_RENDER_STATE_SMALL_PIXELS 100.f

#define OBJECT_SHADOW_MINIMUM_PIXELS 30.f
#define OBJECT_SHADOW_MINIMUM_DARKNESS 0.19f

#define OBJECT_LIGHTING_MAXIMUM_COLOR_DELTA 0.03f
#define OBJECT_LIGHTING_MAXIMUM_SHADOW_VECTOR_DELTA 0.012f

/* ---------- macros */

#define object_render_state_get(index) \
	((struct object_render_state *)datum_get(cached_object_render_states, (index)))

/* ---------- structures */

struct object_render_data
{
	long object_index;
	struct render_lighting *lighting;
	boolean shadow;
	boolean no_planar_fog;
	word pad;
	real_matrix4x3 shadow_matrix;
	real shadow_bounding_radius;
	/* port: the size in pixels of data->object_index, found once for the pass's use of the object (render_object): the same call, of the same object in the
	same frame, was made up to four times (the governor's cull, the lighting, the shadow's test, the model) */
	real level_of_detail_pixels;
};

struct object_render_state
{
	struct datum_header header;
	long object_index;
	long refresh_frame_index;
	long render_scene_index;
	long render_frame_index;
	struct render_lighting lighting;
	struct render_lighting desired_lighting;
	real level_of_detail_pixels;
};

struct render_object_globals
{
	short rendered_object_count;
	word pad;
	long rendered_object_indices[MAXIMUM_RENDERED_OBJECTS];
};

typedef char render_model_effect_size_assert[
	sizeof(struct render_model_effect) == 0x28 ? 1 : -1];
typedef char object_render_data_size_assert[
	sizeof(struct object_render_data) == 0x48 ? 1 : -1];
typedef char object_render_state_size_assert[
	sizeof(struct object_render_state) == 0x100 ? 1 : -1];
/* the native builds render up to MAXIMUM_RENDERED_OBJECTS (objects.h) */
typedef char render_object_globals_size_assert[
	sizeof(struct render_object_globals) == 4 + MAXIMUM_RENDERED_OBJECTS * sizeof(long) ? 1 : -1];

/* ---------- prototypes */

static boolean object_is_first_person_camera(
	long object_index);
static void find_rendered_objects(
	void);
static real object_get_level_of_detail_pixels(
	long object_index);
static void render_object_list(
	struct object_render_data *data,
	struct render_model_effect const *parent_model_effect,
	long object_index);
static void interpolate_real_rgb_color(
	union real_rgb_color *color,
	union real_rgb_color const *desired_color,
	real maximum_delta);
static void interpolate_real_argb_color(
	union real_argb_color *color,
	union real_argb_color const *desired_color,
	real maximum_delta);
static void interpolate_normal(
	real_vector3d *normal,
	real_vector3d const *desired_normal,
	real maximum_delta);
static boolean render_object_shadow_begin(
	struct object_render_data *data,
	real level_of_detail);
static void shadow_volume_plane_pair(
	real_vector3d const *normal,
	real_point3d const *point,
	real_plane3d *plane,
	real_plane3d *opposite_plane);
static void render_object_shadow_end(
	struct object_render_data *data);
static void object_render_state_refresh(
	long render_state_index,
	long object_index,
	real level_of_detail_pixels,
	boolean rebuild);
static long object_get_cached_render_state(
	long object_index,
	real level_of_detail_pixels);
static void render_object(
	struct object_render_data *data);
static void process_rendered_objects(
	struct object_render_data *data);

short structure_visibility_find_objects(
	long *object_indices,
	short maximum_object_count,
	long (*get_first_object_function)(long *reference_index, short cluster_index),
	long (*get_next_object_function)(long *reference_index),
	void (*get_bounding_sphere_function)(long object_index, real_point3d *center, real *radius),
	boolean (*unmarked_function)(long object_index),
	boolean (*mark_function)(long object_index));

extern boolean debug_objects;
extern short debug_rasterizer_light_count;

/* ---------- globals */

boolean render_shadows = TRUE;

static struct profile_section render_objects_section =
	{ "render_objects", NONE, TRUE };
static struct profile_section render_object_shadows_section =
	{ "render_object_shadows", NONE, TRUE };

struct data_array *cached_object_render_states;

static struct render_object_globals render_object_globals = { 0 };

boolean debug_inactive_objects = FALSE;

static boolean reported_rendered_object_overflow = FALSE;

/* ---------- public code */

void render_objects_initialize(
	void)
{
	cached_object_render_states = game_state_data_new(
		"cached object render states",
		MAXIMUM_CACHED_OBJECT_RENDER_STATES,
		sizeof(struct object_render_state));
	match_assert("c:\\halo\\SOURCE\\render\\render_objects.c", 125, cached_object_render_states);

	return;
}

void render_objects_initialize_for_new_map(
	void)
{
	data_make_valid(cached_object_render_states);

	return;
}

void render_objects_dispose_from_old_map(
	void)
{
	if (cached_object_render_states && cached_object_render_states->valid)
	{
		data_make_invalid(cached_object_render_states);
	}

	return;
}

void render_objects_dispose(
	void)
{
	cached_object_render_states = NULL;

	return;
}

struct render_lighting *object_get_cached_render_lighting(
	long object_index,
	real level_of_detail_pixels)
{
	static struct render_lighting lighting_storage;
	long render_state_index = object_get_cached_render_state(
		object_index,
		level_of_detail_pixels);

	if (render_state_index != NONE)
	{
		return &object_render_state_get(render_state_index)->lighting;
	}

	lights_prepare_for_object_static(object_index, &lighting_storage);
	lights_prepare_for_object_dynamic(object_index, &lighting_storage);

	return &lighting_storage;
}

void render_objects(
	void)
{
	struct object_render_data data;
	boolean first_person_pass = FALSE;

	profile_enter(render_objects_section);

	rasterizer_models_begin(FALSE);
	find_rendered_objects();

	data.shadow = FALSE;

	do
	{
		if (first_person_pass != rasterizer_debug_options.draw_first_person_weapon_first)
		{
			first_person_weapon_draw();
		}
		else
		{
			process_rendered_objects(&data);
		}

		first_person_pass = !first_person_pass;
	}
	while (first_person_pass);

	rasterizer_models_end();

	profile_exit(render_objects_section);

	return;
}

/* port: port/linux/game/first_person_legs.c */
boolean first_person_legs_wanted(long object_index);
real_matrix4x3 const *first_person_legs_node_matrices(long object_index, real_matrix4x3 const *nodes);
/* port: port/linux/game/shield_color.c */
real_rgb_color const *shield_color_colors(long unit_index, real_rgb_color const *colors);
/* port: port/linux/game/overshield_effect.c */
real const *overshield_effect_values(long unit_index, real const *values, boolean arms);

void render_object_shadows(
	void)
{
	profile_enter(render_object_shadows_section);

	if (render_shadows)
	{
		struct object_render_data data;

		rasterizer_environment_shadows_begin();

		data.shadow = TRUE;
		process_rendered_objects(&data);

		rasterizer_environment_shadows_end();
	}

	profile_exit(render_object_shadows_section);

	return;
}

/* ---------- private code */

static boolean object_is_first_person_camera(
	long object_index)
{
	long unit_index = (local_player_get_player_index(render.local_player_index) == NONE)
		? NONE
		: player_get(local_player_get_player_index(render.local_player_index))->unit_index;

	return (unit_index == object_index &&
		director_get_perspective(render.local_player_index) ==
			_director_perspective_first_person) ||
		scripted_camera_object_is_first_person_camera(object_index);
}

/* port: the objects this frame has found, a bit for each (by absolute index), the renderer's own. The game marks an object it has met in its
magic_number (object_mark_function) and the ticks do the same in their searches (the AI's, the collision tests'), so the drawing marking at the
same time as a tick that runs beside it would make a search skip an object the other had marked: a tick that depends on drawing. The set is
cleared for every search of the frame and not written to anything the simulation reads. */
static unsigned long render_object_visited[BIT_VECTOR_SIZE_IN_LONGS(MAXIMUM_OBJECTS_PER_MAP)];

static boolean render_object_unvisited(
	long object_index)
{
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);

	return absolute_index >= MAXIMUM_OBJECTS_PER_MAP || !BIT_VECTOR_TEST_FLAG(render_object_visited, absolute_index);
}

static boolean render_object_visit(
	long object_index)
{
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	boolean first;

	if (absolute_index >= MAXIMUM_OBJECTS_PER_MAP)
		return TRUE;
	first = !BIT_VECTOR_TEST_FLAG(render_object_visited, absolute_index);
	BIT_VECTOR_SET_FLAG(render_object_visited, absolute_index, TRUE);

	return first;
}

#ifdef HALO_WIIU
/* port: what the render's object search considered (a candidate of a rendered cluster it had not met this frame), found inside the view, and what the render
then drew (after the governor's size cull): counts for the performance overlay (port/wiiu/src/wiiu_perf.h), written by the game thread, 32 bits so that another
thread reads them whole. Only this search: the lights' (object_lights.c) is another call of the same function */
static volatile unsigned long render_object_perf_considered = 0;
static volatile unsigned long render_object_perf_found = 0;
static volatile unsigned long render_object_perf_drawn = 0;

void render_objects_perf(
	unsigned long *considered,
	unsigned long *found,
	unsigned long *drawn)
{
	*considered = render_object_perf_considered;
	*found = render_object_perf_found;
	*drawn = render_object_perf_drawn;

	return;
}

static void render_object_counted_bounding_sphere(
	long object_index,
	real_point3d *center,
	real *radius)
{
	render_object_perf_considered++;
	object_get_render_bounding_sphere(object_index, center, radius);

	return;
}
#define RENDER_OBJECT_BOUNDING_SPHERE render_object_counted_bounding_sphere
#else
#define RENDER_OBJECT_BOUNDING_SPHERE object_get_render_bounding_sphere
#endif

static void find_rendered_objects(
	void)
{
	csmemset(render_object_visited, 0, sizeof(render_object_visited));

	render_object_globals.rendered_object_count = structure_visibility_find_objects(
		render_object_globals.rendered_object_indices,
		MAXIMUM_RENDERED_OBJECTS,
		cluster_get_first_collideable_object,
		cluster_get_next_collideable_object,
		RENDER_OBJECT_BOUNDING_SPHERE,
		render_object_unvisited,
		render_object_visit);

	render_object_globals.rendered_object_count += structure_visibility_find_objects(
		&render_object_globals.rendered_object_indices[
			render_object_globals.rendered_object_count],
		MAXIMUM_RENDERED_OBJECTS - render_object_globals.rendered_object_count,
		cluster_get_first_noncollideable_object,
		cluster_get_next_noncollideable_object,
		RENDER_OBJECT_BOUNDING_SPHERE,
		render_object_unvisited,
		render_object_visit);

#ifdef HALO_WIIU
	render_object_perf_found += (unsigned long)render_object_globals.rendered_object_count;
#endif

	if (render_object_globals.rendered_object_count == MAXIMUM_RENDERED_OBJECTS &&
		!reported_rendered_object_overflow)
	{
		error(_error_silent, "MAXIMUM_RENDERED_OBJECTS exceeded.");
		reported_rendered_object_overflow = TRUE;
	}

	return;
}

static real object_get_level_of_detail_pixels(
	long object_index)
{
	real_point3d center;
	real radius;

	if (cinematic_in_progress() &&
		TEST_FLAG(object_get(object_index)->object.flags, _object_movie_star_bit))
	{
		return REAL_MAX;
	}

	object_get_bounding_sphere(object_index, &center, &radius);

	return render_frustum_sphere_diameter_in_pixels(&render.frustum, &center, radius);
}

static void render_object_list(
	struct object_render_data *data,
	struct render_model_effect const *parent_model_effect,
	long object_index)
{
	while (object_index != NONE)
	{
		struct object_datum *object = object_get(object_index);
		/* port: the player's own legs, seen looking down in first person
		(port/linux/game/first_person_legs.c): its body drawn from the waist
		down, nothing it carries */
		boolean legs = !data->shadow && !render.camera.mirrored &&
			object_is_first_person_camera(object_index) && first_person_legs_wanted(object_index);
		real_matrix4x3 const *node_matrices = legs ?
			first_person_legs_node_matrices(object_index, object_get_node_matrices(object_index)) :
			object_get_node_matrices(object_index);
		/* port: the function values the picture shows
		(object_function_values_for_render), kept in the rasterizer's memory
		for the frame: a modifier's group, a queued transparent part and a
		child that copies this modifier read them after this block is done,
		and the overshield finds its own values by their address
		(overshield_effect.c), so they are not copied again */
		real const *shown_functions = object->object.outgoing_function_values;

		if (!object_is_first_person_camera(object_index) || render.camera.mirrored || (legs && node_matrices))
		{
			struct render_model_effect model_effect;

			if (!data->shadow)
			{
				match_assert(
					"c:\\halo\\SOURCE\\render\\render_objects.c",
					390,
					parent_model_effect);

				model_effect = *parent_model_effect;

				/* a modifier effect is not inherited by an object's children */
				if (parent_model_effect->type == _render_model_effect_type_modifier)
				{
					model_effect.type = _render_model_effect_type_none;
					model_effect.modifier_shader = NULL;
					model_effect.modifier_animation.values = NULL;
					model_effect.modifier_animation.colors = NULL;
				}
				else
				{
					/* January copies the parent effect again on this path (a second block copy in
					   both the PC and Xbox builds) */
					model_effect = *parent_model_effect;
				}
			}

			if (!TEST_FLAG(object->object.flags, _object_invisible_bit))
			{
				struct object_definition *definition =
					object_definition_get(object->definition_index);
				/* port: the object this pass is on has its size already (render_object) */
				real level_of_detail_pixels = object_index == data->object_index ?
					data->level_of_detail_pixels :
					object_get_level_of_detail_pixels(object_index);

				match_assert(
					"c:\\halo\\SOURCE\\render\\render_objects.c",
					415,
					data->lighting);

				if (!data->shadow)
				{
					/* port: the tick's functions, moved into this frame, so a
					charge clicks with the picture instead of once a tick */
					{
						real values[NUMBER_OF_OUTGOING_OBJECT_FUNCTIONS];
						real *kept;

						object_function_values_for_render(object_index, values);
						kept = rasterizer_memory_alloc(values, sizeof(values));
						if (kept)
							shown_functions = kept;
					}
					if (definition->object.modifier_shader.index != NONE)
					{
						model_effect.modifier_shader = shader_definition_get(
							definition->object.modifier_shader.index);
						if (shader_type_is_valid_for_modifier(
							model_effect.modifier_shader->base.type))
						{
							/* port: a local player's shield in the color
							chosen for it (port/linux/game/shield_color.c) */
							model_effect.modifier_animation.colors =
								TEST_FLAG(_object_mask_unit, object->object.type) ?
									shield_color_colors(object_index, object->object.outgoing_change_colors) :
									object->object.outgoing_change_colors;
							/* port: an overshield as it rises and lasts
							(port/linux/game/overshield_effect.c) */
							model_effect.modifier_animation.values =
								TEST_FLAG(_object_mask_unit, object->object.type) ?
									overshield_effect_values(object_index, shown_functions, FALSE) :
									shown_functions;
						}
						else
						{
							error(
								_error_silent,
								"### ERROR invalid modifier shader",
								model_effect.modifier_shader->base.type);
							model_effect.modifier_shader = NULL;
						}
					}

					if (TEST_FLAG(_object_mask_unit, object->object.type))
					{
						struct unit_datum *unit = unit_get(object_index);

						if (unit->unit.active_camouflage > 0.f)
						{
							model_effect.type =
								_render_model_effect_type_active_camouflage;
							model_effect.source_object_index = object_index;
							model_effect.source_object_centroid =
								object->object.bounding_sphere_center;
							model_effect.intensity = unit->unit.active_camouflage;
							model_effect.parameter =
								unit->unit.active_camouflage_super_amount;
						}
					}

					if (TEST_FLAG(
						definition->object.flags,
						_object_transparency_self_occludes_bit))
					{
						model_effect.type = _render_model_effect_type_modifier;
						model_effect.source_object_index = object_index;
						model_effect.source_object_centroid =
							object->object.bounding_sphere_center;
					}

					{
						struct object_header_datum *object_header =
							object_header_get(object_index);
						char name[512];
						real_point3d text_point;

						if (debug_inactive_objects &&
							object->object.parent_object_index == NONE &&
							!TEST_FLAG(object_header->flags, _object_header_active_bit) &&
							definition->object.model.index != NONE)
						{
							/* point_from_line3d(center, global_up3d, 0.2f, &text_point), expanded so this
							   object does not emit the real_math.h point_from_line3d COMDAT */
							real_point3d const *center = &object->object.bounding_sphere_center;

							text_point.x = global_up3d->i * 0.2f + center->x;
							text_point.y = global_up3d->j * 0.2f + center->y;
							text_point.z = global_up3d->k * 0.2f + center->z;

							sprintf(
								name,
								"inactive %s",
								tag_name_strip_path(tag_get_name(object->definition_index)));

							render_debug_point(
								FALSE,
								center,
								object->object.bounding_sphere_radius,
								global_real_argb_blue);
							render_debug_string_at_point(
								FALSE,
								&text_point,
								name,
								global_real_argb_blue);
						}
					}

					render_model(
						definition->object.model.index,
						level_of_detail_pixels,
						node_matrices,
						object->object.region_permutations,
						object->object.outgoing_change_colors,
						shown_functions,
						data->lighting,
						&object->object.bounding_sphere_center,
						object->object.bounding_sphere_radius,
						&model_effect,
						object_index,
						object->object.forced_shader_permutation_index,
						data->no_planar_fog ? FLAG(_render_model_no_planar_fog_bit) : 0);

					if (debug_objects)
					{
						object_type_render_debug(object_index);
					}
				}
				else
				{
					render_model(
						definition->object.model.index,
						level_of_detail_pixels * 0.3f,
						node_matrices,
						object->object.region_permutations,
						object->object.outgoing_change_colors,
						object->object.outgoing_function_values,
						data->lighting,
						&object->object.bounding_sphere_center,
						object->object.bounding_sphere_radius,
						NULL,
						object_index,
						object->object.forced_shader_permutation_index,
						FLAG(_render_model_shadow_bit));
				}
			}

			if (!data->shadow && !legs && object->object.first_widget_index != NONE)
			{
				struct render_animation animation;
				real widget_functions[NUMBER_OF_OUTGOING_OBJECT_FUNCTIONS];

				/* port: the tick's functions, moved into this frame (the
				widgets read them as they are drawn, here) */
				object_function_values_for_render(object_index, widget_functions);
				animation.colors = object->object.outgoing_change_colors;
				animation.values = widget_functions;
				widgets_render(object_index, data->lighting, &animation);
			}

			if (object->object.first_child_object_index != NONE && !legs)
			{
				render_object_list(
					data,
					data->shadow ? NULL : &model_effect,
					object->object.first_child_object_index);
			}
		}

		object_index = object->object.next_object_index;
	}

	return;
}

static void interpolate_real_rgb_color(
	union real_rgb_color *color,
	union real_rgb_color const *desired_color,
	real maximum_delta)
{
	color->red += PIN(
		desired_color->red - color->red,
		-maximum_delta,
		maximum_delta);

	color->green += PIN(
		desired_color->green - color->green,
		-maximum_delta,
		maximum_delta);

	color->blue += PIN(
		desired_color->blue - color->blue,
		-maximum_delta,
		maximum_delta);

	return;
}

static void interpolate_real_argb_color(
	union real_argb_color *color,
	union real_argb_color const *desired_color,
	real maximum_delta)
{
	color->alpha += PIN(
		desired_color->alpha - color->alpha,
		-maximum_delta,
		maximum_delta);

	color->red += PIN(
		desired_color->red - color->red,
		-maximum_delta,
		maximum_delta);

	color->green += PIN(
		desired_color->green - color->green,
		-maximum_delta,
		maximum_delta);

	color->blue += PIN(
		desired_color->blue - color->blue,
		-maximum_delta,
		maximum_delta);

	return;
}

static void interpolate_normal(
	real_vector3d *normal,
	real_vector3d const *desired_normal,
	real maximum_delta)
{
	normal->i += PIN(
		desired_normal->i - normal->i,
		-maximum_delta,
		maximum_delta);

	normal->j += PIN(
		desired_normal->j - normal->j,
		-maximum_delta,
		maximum_delta);

	normal->k += PIN(
		desired_normal->k - normal->k,
		-maximum_delta,
		maximum_delta);

	normalize3d(normal);

	return;
}

static boolean render_object_shadow_begin(
	struct object_render_data *data,
	real level_of_detail)
{
	real_point3d center;
	real radius;
	real_vector3d shadow_right;
	union real_rgb_color shadow_color;
	struct object_datum *object;
	real shadow_intensity = level_of_detail;

	object_get_bounding_sphere(data->object_index, &center, &radius);

	perpendicular3d(&data->lighting->shadow_vector, &shadow_right);
	normalize3d(&shadow_right);
	matrix4x3_from_point_and_vectors(
		&data->shadow_matrix,
		&center,
		&shadow_right,
		&data->lighting->shadow_vector);

	shadow_color = data->lighting->shadow_color;

	object = object_get(data->object_index);
	if (TEST_FLAG(_object_mask_unit, object->object.type))
	{
		struct unit_datum *unit = unit_get(data->object_index);

		if (unit->unit.active_camouflage > 0.f)
		{
			shadow_intensity = (1.f - unit->unit.active_camouflage) * level_of_detail;
		}
	}

	shadow_color.red = shadow_color.red * shadow_intensity + (1.f - shadow_intensity);
	shadow_color.green = shadow_color.green * shadow_intensity + (1.f - shadow_intensity);
	shadow_color.blue = shadow_color.blue * shadow_intensity + (1.f - shadow_intensity);

	return rasterizer_environment_shadow_begin(
		data->object_index,
		&data->shadow_matrix,
		&shadow_color,
		radius,
		&data->shadow_bounding_radius);
}

static void shadow_volume_plane_pair(
	real_vector3d const *normal,
	real_point3d const *point,
	real_plane3d *plane,
	real_plane3d *opposite_plane)
{
	plane->n = *normal;
	plane->d = dot_product3d(normal, (real_vector3d const *)point);
	negate_vector3d(&plane->n, &opposite_plane->n);
	opposite_plane->d = -plane->d;

	return;
}

static void render_object_shadow_end(
	struct object_render_data *data)
{
	real_plane3d shadow_volume_planes[NUMBER_OF_SHADOW_VOLUME_PLANES];
	real_rectangle3d shadow_volume_bounds;

	shadow_volume_plane_pair(
		&data->shadow_matrix.up,
		&data->shadow_matrix.position,
		&shadow_volume_planes[0],
		&shadow_volume_planes[1]);
	shadow_volume_planes[0].d -= data->shadow_bounding_radius * 0.5f;
	shadow_volume_planes[1].d -= data->shadow_bounding_radius * 4.f;

	shadow_volume_plane_pair(
		&data->shadow_matrix.forward,
		&data->shadow_matrix.position,
		&shadow_volume_planes[2],
		&shadow_volume_planes[3]);
	shadow_volume_planes[2].d -= data->shadow_bounding_radius;
	shadow_volume_planes[3].d -= data->shadow_bounding_radius;

	shadow_volume_plane_pair(
		&data->shadow_matrix.left,
		&data->shadow_matrix.position,
		&shadow_volume_planes[4],
		&shadow_volume_planes[5]);
	shadow_volume_planes[4].d -= data->shadow_bounding_radius;
	shadow_volume_planes[5].d -= data->shadow_bounding_radius;

	shadow_volume_bounds.x1 =
		ABS(data->shadow_matrix.forward.i) + ABS(data->shadow_matrix.left.i);
	shadow_volume_bounds.x0 = -shadow_volume_bounds.x1;
	shadow_volume_bounds.y1 =
		ABS(data->shadow_matrix.forward.j) + ABS(data->shadow_matrix.left.j);
	shadow_volume_bounds.y0 = -shadow_volume_bounds.y1;
	shadow_volume_bounds.z1 =
		ABS(data->shadow_matrix.forward.k) + ABS(data->shadow_matrix.left.k);
	shadow_volume_bounds.z0 = -shadow_volume_bounds.z1;

	shadow_volume_bounds.x0 += (data->shadow_matrix.up.i <= 0.f)
		? data->shadow_matrix.up.i * 4.f
		: data->shadow_matrix.up.i * -0.5f;
	shadow_volume_bounds.x1 += (data->shadow_matrix.up.i > 0.f)
		? data->shadow_matrix.up.i * 4.f
		: data->shadow_matrix.up.i * -0.5f;
	shadow_volume_bounds.y0 += (data->shadow_matrix.up.j <= 0.f)
		? data->shadow_matrix.up.j * 4.f
		: data->shadow_matrix.up.j * -0.5f;
	shadow_volume_bounds.y1 += (data->shadow_matrix.up.j > 0.f)
		? data->shadow_matrix.up.j * 4.f
		: data->shadow_matrix.up.j * -0.5f;
	shadow_volume_bounds.z0 += data->shadow_matrix.up.k * 4.f;
	shadow_volume_bounds.z1 -= data->shadow_matrix.up.k * 0.5f;

	shadow_volume_bounds.x0 = shadow_volume_bounds.x0 * data->shadow_bounding_radius +
		data->shadow_matrix.position.x;
	shadow_volume_bounds.x1 = shadow_volume_bounds.x1 * data->shadow_bounding_radius +
		data->shadow_matrix.position.x;
	shadow_volume_bounds.y0 = shadow_volume_bounds.y0 * data->shadow_bounding_radius +
		data->shadow_matrix.position.y;
	shadow_volume_bounds.y1 = shadow_volume_bounds.y1 * data->shadow_bounding_radius +
		data->shadow_matrix.position.y;
	shadow_volume_bounds.z0 = shadow_volume_bounds.z0 * data->shadow_bounding_radius +
		data->shadow_matrix.position.z;
	shadow_volume_bounds.z1 = shadow_volume_bounds.z1 * data->shadow_bounding_radius +
		data->shadow_matrix.position.z;

	structure_render_shadow(
		&data->shadow_matrix.position,
		data->shadow_bounding_radius * 4.f,
		&shadow_volume_bounds,
		NUMBER_OF_SHADOW_VOLUME_PLANES,
		shadow_volume_planes);

	rasterizer_environment_shadow_end();

	return;
}

static void object_render_state_refresh(
	long render_state_index,
	long object_index,
	real level_of_detail_pixels,
	boolean rebuild)
{
	struct object_render_state *state = object_render_state_get(render_state_index);
	long scene_age = render.scene_index - state->render_scene_index;
	long render_age = render.frame_index - state->render_frame_index;
	/* The native builds draw several frames per tick
	(port/linux/game/render_interpolation.c), and a refresh moves the lighting
	a fixed step toward its target: refresh at the intervals in ticks the
	Xbox refreshed at in frames, so lighting changes as fast as it did. */
	long refresh_age = game_time_get() - state->refresh_frame_index;
	boolean refresh = FALSE;

	if (refresh_age < 0 || scene_age < 0)
	{
		scene_age = 1;
		refresh_age = 1;
	}

	if (TEST_FLAG(object_get(object_index)->object.flags, _object_static_lighting_recompute_bit))
	{
		if (level_of_detail_pixels > OBJECT_RENDER_STATE_LARGE_PIXELS)
		{
			refresh = refresh_age > 0;
		}
		else if (level_of_detail_pixels > OBJECT_RENDER_STATE_SMALL_PIXELS)
		{
			refresh = refresh_age > OBJECT_RENDER_STATE_LARGE_INTERVAL;
		}
		else
		{
			refresh = refresh_age > OBJECT_RENDER_STATE_SMALL_INTERVAL;
		}
	}

	if (refresh &&
		render_age > 1 &&
		TEST_FLAG(
			object_get(object_index)->object.flags,
			_object_static_lighting_recompute_bit))
	{
		rebuild = TRUE;
	}

	if (rebuild || refresh)
	{
		state->object_index = object_index;
		lights_prepare_for_object_static(object_index, &state->desired_lighting);
		state->level_of_detail_pixels = level_of_detail_pixels;
		state->refresh_frame_index = game_time_get();
	}

#ifdef HALO_WIIU
	/* halo/lighting_every.txt: the dynamic lighting (the point lights found for the object and its
	lightmap samples) is made again in one frame of N for each object, and kept in between (the objects take
	turns by their index); no file is 2; 1 is every frame, as it was. A rebuild and an object that has no
	lighting yet are made at once. */
	{
		static long every = -1;

		if (every < 0)
		{
			long wiiu_study_number(const char *name, long fallback);
			void platform_log(const char *format, ...);

			every = wiiu_study_number("lighting_every.txt", 2);
			if (every < 1)
				every = 1;
			if (every > 6)
				every = 6;
			platform_log("lighting: the dynamic lighting is made again every %ld frame(s) for each object (halo/lighting_every.txt)", every);
		}
		if (every > 1 && !rebuild && scene_age > 0 && render_age <= 1 &&
			((render.frame_index + (object_index & 0xFFFF)) % every) != 0)
		{
			scene_age = 0;
		}
	}
#endif
	if (rebuild || scene_age > 0)
	{
		lights_prepare_for_object_dynamic(object_index, &state->desired_lighting);
	}

	if (!rebuild && !refresh)
	{
		if (scene_age > 0)
		{
			state->lighting.point_light_count = state->desired_lighting.point_light_count;
			state->lighting.point_light_indices[0] =
				state->desired_lighting.point_light_indices[0];
			state->lighting.point_light_indices[1] =
				state->desired_lighting.point_light_indices[1];
		}
	}
	else if (!rebuild && object_light_interpolate)
	{
		real_vector3d velocity;
		struct render_lighting *lighting = &state->lighting;
		struct render_lighting *desired_lighting = &state->desired_lighting;

		match_assert(
			"c:\\halo\\SOURCE\\render\\render_objects.c",
			635,
			state->desired_lighting.distant_light_count==2);

		object_get_velocities(object_index, &velocity, NULL);
		if (velocity.i != 0.f ||
			velocity.j != 0.f ||
			velocity.k != 0.f ||
			object_try_and_get_and_verify_type(object_index, _object_mask_machine))
		{
			interpolate_real_rgb_color(
				&lighting->ambient_color,
				&desired_lighting->ambient_color,
				OBJECT_LIGHTING_MAXIMUM_COLOR_DELTA);
			interpolate_real_argb_color(
				&lighting->reflection_tint_color,
				&desired_lighting->reflection_tint_color,
				OBJECT_LIGHTING_MAXIMUM_COLOR_DELTA);
			interpolate_real_rgb_color(
				&lighting->distant_lights[0].color,
				&desired_lighting->distant_lights[0].color,
				OBJECT_LIGHTING_MAXIMUM_COLOR_DELTA);
			interpolate_normal(
				&lighting->distant_lights[0].direction,
				&desired_lighting->distant_lights[0].direction,
				OBJECT_LIGHTING_MAXIMUM_COLOR_DELTA);
			interpolate_real_rgb_color(
				&lighting->distant_lights[1].color,
				&desired_lighting->distant_lights[1].color,
				OBJECT_LIGHTING_MAXIMUM_COLOR_DELTA);
			interpolate_normal(
				&lighting->distant_lights[1].direction,
				&desired_lighting->distant_lights[1].direction,
				OBJECT_LIGHTING_MAXIMUM_COLOR_DELTA);
			interpolate_normal(
				&lighting->shadow_vector,
				&desired_lighting->shadow_vector,
				OBJECT_LIGHTING_MAXIMUM_SHADOW_VECTOR_DELTA);
			interpolate_real_rgb_color(
				&lighting->shadow_color,
				&desired_lighting->shadow_color,
				OBJECT_LIGHTING_MAXIMUM_COLOR_DELTA);
		}

		state->lighting.point_light_count = state->desired_lighting.point_light_count;
		state->lighting.point_light_indices[0] =
			state->desired_lighting.point_light_indices[0];
		state->lighting.point_light_indices[1] =
			state->desired_lighting.point_light_indices[1];
	}
	else
	{
		state->lighting = state->desired_lighting;
	}

#ifdef HALO_WIIU
	/* port: a light index kept from an earlier frame (the lighting is eased toward the desired one over frames, and a network game's frame with
	the ticks beside the drawing can be drawn from a list of lights shorter than the one the index was made for) is dropped, not a halt */
	{
		short point_light_index;
		short kept = 0;

		for (point_light_index = 0;
			point_light_index < state->lighting.point_light_count && point_light_index < 2;
			point_light_index++)
		{
			if (state->lighting.point_light_indices[point_light_index]>=0 &&
				state->lighting.point_light_indices[point_light_index]<debug_rasterizer_light_count)
			{
				state->lighting.point_light_indices[kept++] = state->lighting.point_light_indices[point_light_index];
			}
		}
		state->lighting.point_light_count = kept;
	}
#else
	{
		short point_light_index;

		for (point_light_index = 0;
			point_light_index < state->lighting.point_light_count;
			point_light_index++)
		{
			match_assert(
				"c:\\halo\\SOURCE\\render\\render_objects.c",
				690,
				state->lighting.point_light_indices[point_light_index]>=0 &&
					state->lighting.point_light_indices[point_light_index]<debug_rasterizer_light_count);
		}
	}

#endif
	state->render_scene_index = render.scene_index;
	state->render_frame_index = render.frame_index;

	return;
}

static long object_get_cached_render_state(
	long object_index,
	real level_of_detail_pixels)
{
	struct object_datum *object = object_get(object_index);
	long render_state_index = NONE;

	if (object->object.cached_render_state_index != NONE &&
		object_render_state_get(object->object.cached_render_state_index)->object_index ==
			object_index)
	{
		render_state_index = object->object.cached_render_state_index;
	}

	if (render_state_index == NONE)
	{
		render_state_index = datum_new(cached_object_render_states);
		if (render_state_index == NONE)
		{
			real oldest_age = REAL_MIN;
			long index;

			for (index = data_next_index(cached_object_render_states, NONE);
				index != NONE;
				index = data_next_index(cached_object_render_states, index))
			{
				real age = (real)(render.scene_index -
					object_render_state_get(index)->render_scene_index);

				if (age < 0.f)
				{
					age = MAXIMUM_OBJECT_RENDER_STATE_AGE;
				}

				if (age > oldest_age)
				{
					oldest_age = age;
					render_state_index = index;
				}
			}
		}

		if (render_state_index != NONE)
		{
			object_render_state_refresh(
				render_state_index,
				object_index,
				level_of_detail_pixels,
				TRUE);
			object->object.cached_render_state_index = render_state_index;
		}
	}
	else
	{
		object_render_state_refresh(
			render_state_index,
			object_index,
			level_of_detail_pixels,
			FALSE);
	}

	return render_state_index;
}

#ifdef HALO_WIIU_OBJECT_PROBE
/* port: where the objects pass spends its time, by the kind of object (halo/log.txt "objects pass:" every 10 s): the cull and
state, the lighting, and the model's draw. Four clock reads an object drawn */
#include <time.h>
void platform_log(const char *format, ...);
static double render_probe_now_ms(void)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1000000.0;
}
static double render_probe_ms[16][3];
static unsigned long render_probe_count[16];
static double render_probe_last_report;
static void render_probe_report(double now)
{
	short type;
	if (render_probe_last_report == 0.0) render_probe_last_report = now;
	if (now - render_probe_last_report < 10000.0) return;
	render_probe_last_report = now;
	for (type = 0; type < 16; type++)
	{
		if (render_probe_count[type])
		{
			platform_log("objects pass: type %d: %lu drawn in 10 s, %.0f ms (cull, state and lighting %.0f, model %.0f), %.0f us each",
				type, render_probe_count[type],
				render_probe_ms[type][0] + render_probe_ms[type][2],
				render_probe_ms[type][0], render_probe_ms[type][2],
				1000.0 * (render_probe_ms[type][0] + render_probe_ms[type][2]) / render_probe_count[type]);
			render_probe_ms[type][0] = render_probe_ms[type][2] = 0.0;
			render_probe_count[type] = 0;
		}
	}
}
#endif

static void render_object(
	struct object_render_data *data)
{
	if (data->shadow)
	{
		struct object_datum *object = object_get(data->object_index);

		if (!object_is_first_person_camera(data->object_index) &&
			!TEST_FLAG(object->object.flags, _object_shadowless_bit) &&
			(!TEST_FLAG(object->object.flags, _object_invisible_bit) ||
				object->object.first_child_object_index != NONE))
		{
			real level_of_detail_pixels;
			real shadow_darkness;
			/* port: dynamic quality (render_governor.c): the small cast no shadow while the frames run long */
			real governed_shadow_pixels = render_governor_values()->shadow_min_pixels;

			/* port: found once (see object_render_data) */
			data->level_of_detail_pixels = object_get_level_of_detail_pixels(data->object_index);
			if (governed_shadow_pixels > OBJECT_SHADOW_MINIMUM_PIXELS &&
				data->level_of_detail_pixels < governed_shadow_pixels)
			{
				render_governor_counts[RENDER_GOVERNOR_COUNT_SHADOWS_SKIPPED]++;
				return;
			}

			data->lighting = object_get_cached_render_lighting(
				data->object_index,
				data->level_of_detail_pixels);

			level_of_detail_pixels = data->level_of_detail_pixels;
			shadow_darkness = 1.f - real_rgb_color_brightness(&data->lighting->shadow_color);

			if (level_of_detail_pixels > OBJECT_SHADOW_MINIMUM_PIXELS &&
				shadow_darkness > OBJECT_SHADOW_MINIMUM_DARKNESS)
			{
				real size_fraction =
					(level_of_detail_pixels - OBJECT_SHADOW_MINIMUM_PIXELS) * 0.06666667f;
				real darkness_fraction =
					(shadow_darkness - OBJECT_SHADOW_MINIMUM_DARKNESS) * 9.0909081f;

				size_fraction = PIN(size_fraction, 0.f, 1.f);
				darkness_fraction = PIN(darkness_fraction, 0.f, 1.f);

				if (render_object_shadow_begin(data, darkness_fraction * size_fraction))
				{
					render_governor_counts[RENDER_GOVERNOR_COUNT_SHADOWS_DRAWN]++;
					render_object_list(data, NULL, data->object_index);
					render_object_shadow_end(data);
				}
			}
		}
	}
	else
	{
		struct object_datum *object = object_get(data->object_index);
		boolean needs_lighting;
#ifdef HALO_WIIU_OBJECT_PROBE
		double probe_t0 = render_probe_now_ms(), probe_t1 = probe_t0, probe_t2 = probe_t0;
		short probe_type = (short)object->object.type & 15;
#endif

		/* port: dynamic quality (render_governor.c): the small and far, by their kind, are neither lit nor drawn while the frames run long.
		Never a projectile, and the first person camera's own unit and what a cinematic stars are not small */
		{
			struct render_governor_values const *shed = render_governor_values();
			real cull_pixels = TEST_FLAG(_object_mask_unit, object->object.type) ? shed->cull_unit_pixels :
				TEST_FLAG(_object_mask_item, object->object.type) ? shed->cull_item_pixels :
				TEST_FLAG(_object_mask_projectile, object->object.type) ? 0.f : shed->cull_scenery_pixels;

            /* port: keep nearby obstacles visible even at deep governor steps. */
            {
                real dx=object->object.bounding_sphere_center.x-render.camera.position.x;
                real dy=object->object.bounding_sphere_center.y-render.camera.position.y;
                real dz=object->object.bounding_sphere_center.z-render.camera.position.z;
                if(render_governor_protect_near(dx*dx+dy*dy+dz*dz,object->object.bounding_sphere_radius))
                    cull_pixels=0;
            }
			/* port: found once (see object_render_data) */
			data->level_of_detail_pixels = object_get_level_of_detail_pixels(data->object_index);
			if (render_governor_cull_small(DATUM_INDEX_TO_ABSOLUTE_INDEX(data->object_index),
				data->level_of_detail_pixels, cull_pixels))
			{
				render_governor_counts[RENDER_GOVERNOR_COUNT_OBJECTS_CULLED]++;
				return;
			}
			render_governor_counts[RENDER_GOVERNOR_COUNT_OBJECTS_DRAWN]++;
			render_governor_counts[RENDER_GOVERNOR_COUNT_VEHICLES_DRAWN] += object->object.type == _object_type_vehicle;
			render_governor_counts[RENDER_GOVERNOR_COUNT_BIPEDS_DRAWN] += object->object.type == _object_type_biped;
			render_governor_counts[RENDER_GOVERNOR_COUNT_PROJECTILES_DRAWN] += object->object.type == _object_type_projectile;
#ifdef HALO_WIIU
			render_object_perf_drawn++;
#endif
		}

		if (!TEST_FLAG(object->object.flags, _object_invisible_bit) ||
			object->object.first_child_object_index != NONE ||
			widgets_need_lighting(object->object.first_widget_index))
		{
			needs_lighting = TRUE;
		}
		else
		{
			needs_lighting = FALSE;
		}

		if (needs_lighting || object->object.first_widget_index != NONE)
		{
			struct object_definition *definition =
				object_definition_get(object->definition_index);

			if (needs_lighting)
			{
				data->lighting = object_get_cached_render_lighting(
					data->object_index,
					data->level_of_detail_pixels);
			}
			else
			{
				data->lighting = NULL;
			}
#ifdef HALO_WIIU_OBJECT_PROBE
			probe_t1 = render_probe_now_ms();
#endif

			if (editor_preprocess_rendered_object(data->object_index, data->lighting))
			{
				struct render_model_effect model_effect;

				model_effect.type = _render_model_effect_type_none;
				model_effect.modifier_shader = NULL;
				model_effect.modifier_animation.values = NULL;
				model_effect.modifier_animation.colors = NULL;

				data->no_planar_fog =
					render.fog.planar_mode != _render_planar_fog_mode_normal ||
					plane3d_distance_to_point(
						&render.fog.plane,
						&object->object.bounding_sphere_center) >
						definition->object.bounding_radius;

				render_object_list(data, &model_effect, data->object_index);
			}
#ifdef HALO_WIIU_OBJECT_PROBE
			probe_t2 = render_probe_now_ms();
			render_probe_ms[probe_type][0] += probe_t1 - probe_t0;
			render_probe_ms[probe_type][2] += probe_t2 - probe_t1;
			render_probe_count[probe_type]++;
			render_probe_report(probe_t2);
#endif
		}
	}

	return;
}

static void process_rendered_objects(
	struct object_render_data *data)
{
	short rendered_object_index;

	for (rendered_object_index = 0;
		rendered_object_index < render_object_globals.rendered_object_count;
		rendered_object_index++)
	{
		data->object_index =
			render_object_globals.rendered_object_indices[rendered_object_index];
		render_object(data);
	}

	return;
}
