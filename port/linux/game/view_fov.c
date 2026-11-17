/*
VIEW_FOV.C

The first-person view's field of view (display.fov, display.viewmodel_fov)
and whether the weapon is drawn (display.viewmodel_visible), the local
render view only: the observer, its transitions and what the network sends
keep the game's own camera. Each angle is horizontal at 16:9 (the vertical
angle is kept, so other shapes of window widen or narrow as the stock view
does), and 0 keeps the stock view. Called from the game's camera
(main.c's set_window_camera_values), its HUD (hud_draw.c) and the passes
that draw the first-person weapon (rasterizer and render sources).
*/

#include "cseries.h"
#include "view_fov.h"
#include "camera/director.h"
#include "cutscene/cinematics.h"
#include "game/players.h"
#include "game/game.h"
#include "items/weapons.h"
#include "objects/objects.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/xbox/rasterizer_xbox_internal.h"
#include "render/render.h"
#include "render/render_cameras.h"
#include "render/render_cameras_internal.h"
#include "units/unit_definitions.h"
#include "units/units.h"

#include <math.h>

/* (the window's rasterizer target, as the rasterizer's sources number it) */
enum
{
	_rasterizer_target_render_primary = 0
};

double config_real(const char *name);
long config_integer(const char *name);
int config_boolean(const char *name);
unsigned long config_changes(void);

/* ---------- the world's field of view */

static real reticle_scales[MAXIMUM_LOCAL_PLAYERS];
static real authored_vertical[MAXIMUM_LOCAL_PLAYERS];

/* the view's vertical angle for display.fov: the game's own in a vehicle,
dead, in a cinematic, a scripted camera or a view the director holds, and
at a scope's full zoom; between, the zoom's own transition */
static real render_fov_adjust(short local_player_index, real native_vertical_field_of_view)
{
	static boolean initialized;
	static unsigned long read_at;
	static real requested_tangent;
	unsigned long changes = config_changes();
	long unit_index, weapon_index;
	struct unit_datum *unit;
	real unzoomed_angle, unzoomed_tangent, native_tangent, adjusted_tangent;

	if (!initialized || read_at != changes)
	{
		double degrees = config_real("display.fov");

		initialized = TRUE;
		read_at = changes;
		requested_tangent = degrees >= 20.0 && degrees <= 150.0 ?
			(real)(tan(degrees * _pi / 360.0) * (9.0 / 16.0)) : 0.0f;
	}
	if (!requested_tangent || local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS ||
		cinematic_in_progress() || (director_camera_scripted && *director_camera_scripted) ||
		(director_get_perspective(local_player_index) != _director_perspective_first_person &&
		 director_get_perspective(local_player_index) != _director_perspective_third_person) ||
		director_inhibited_facing(local_player_index))
	{
		return native_vertical_field_of_view;
	}
	unit_index = player_control_get_unit_index(local_player_index);
	if (unit_index == NONE)
		return native_vertical_field_of_view;
	unit = unit_try_and_get(unit_index);
	if (!unit || unit->object.parent_object_index != NONE || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
		return native_vertical_field_of_view;

	if (director_get_perspective(local_player_index) == _director_perspective_third_person)
		return 2.0f * atanf(requested_tangent);

	unzoomed_angle = unit_definition_get(unit->definition_index)->unit.camera_field_of_view;
	if (!(unzoomed_angle >= 0.001f && unzoomed_angle <= _pi / 2.0f) ||
		!(native_vertical_field_of_view > 0.0f && native_vertical_field_of_view < _pi))
	{
		return native_vertical_field_of_view;
	}
	unzoomed_tangent = 0.75f * render_camera_get_adjusted_field_of_view_tangent(unzoomed_angle);
	native_tangent = tanf(native_vertical_field_of_view * 0.5f);
	if (requested_tangent < unzoomed_tangent)
	{
		/* (a view narrower than the stock one: the zoom narrows it further) */
		adjusted_tangent = native_tangent * (requested_tangent / unzoomed_tangent);
	}
	else
	{
		real blend = 1.0f;

		weapon_index = unit_inventory_get_weapon(unit_index, unit->unit.current_weapon_index);
		if (weapon_index != NONE)
		{
			real scoped_angle = weapon_get_field_of_view(weapon_index, unzoomed_angle, 0);

			if (scoped_angle > 0.0f && scoped_angle < unzoomed_angle)
			{
				real scoped_tangent = 0.75f * render_camera_get_adjusted_field_of_view_tangent(scoped_angle);
				/* (a scope's full zoom keeps its stock angle exactly: the
				observer's arctangent and tangent can land just above it) */
				if (native_tangent <= scoped_tangent * 1.000001f)
					return native_vertical_field_of_view;

				/* (the extra width fades out along the scope's own
				transition, so every zoom level reached is the stock view) */
				blend = PIN((native_tangent - scoped_tangent) /
					(unzoomed_tangent - scoped_tangent), 0.0f, 1.0f);
			}
		}
		if (blend == 0.0f)
			return native_vertical_field_of_view;
		adjusted_tangent = native_tangent + (requested_tangent - unzoomed_tangent) * blend;
	}
	return 2.0f * atanf(adjusted_tangent);
}

real render_fov_vertical(short local_player_index, real native_vertical_field_of_view)
{
	real adjusted = render_fov_adjust(local_player_index, native_vertical_field_of_view);

	if (local_player_index >= 0 && local_player_index < MAXIMUM_LOCAL_PLAYERS)
	{
		real scale = 1.0f;

		if (adjusted != native_vertical_field_of_view &&
			native_vertical_field_of_view > 0.0f && native_vertical_field_of_view < _pi &&
			adjusted > 0.0f && adjusted < _pi)
		{
			scale = tanf(native_vertical_field_of_view * 0.5f) / tanf(adjusted * 0.5f);
			if (!(scale > 0.0f && scale < 1000.0f))
				scale = 1.0f;
		}
		/* (the view's projection, the zoom's transition too, for the HUD:
		the weapon's own projection, viewmodel_projection_begin, is not it) */
		reticle_scales[local_player_index] = scale;
		/* (a widened view's own angle, which the weapon keeps by default) */
		authored_vertical[local_player_index] =
			adjusted != native_vertical_field_of_view ? native_vertical_field_of_view : 0.0f;
	}
	return adjusted;
}

real render_fov_authored_vertical(short local_player_index)
{
	if (local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS)
		return 0.0f;
	return authored_vertical[local_player_index];
}

real render_fov_reticle_scale(short local_player_index)
{
	if (local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS ||
		reticle_scales[local_player_index] == 0.0f)
	{
		return 1.0f;
	}
	return reticle_scales[local_player_index];
}

/* ---------- the first-person weapon's */

/* how deep the weapon's passes nest, and whether the outermost changed the
view (the cameras and frustums it saved) */
static unsigned long projection_depth;
static boolean projection_applied;
static struct render_camera saved_render_camera, saved_rasterizer_camera;
static struct render_frustum saved_render_frustum, saved_rasterizer_frustum;

static real viewmodel_vertical(void)
{
	static boolean initialized;
	static unsigned long read_at;
	static real angle;
	unsigned long changes = config_changes();

	if (!initialized || read_at != changes)
	{
		double degrees = config_real("display.viewmodel_fov");

		angle = degrees >= 20.0 && degrees <= 150.0 ?
			(real)(2.0 * atan(tan(degrees * _pi / 360.0) * (9.0 / 16.0))) : 0.0f;
		initialized = TRUE;
		read_at = changes;
	}
	return angle;
}

void viewmodel_projection_begin(void)
{
	real angle;
	real_rectangle2d render_bounds, rasterizer_bounds;

	if (projection_depth++ != 0)
		return;
	projection_applied = FALSE;
	/* (display.viewmodel_fov's angle; by default the view's own, from before
	display.fov widened it: arms and a gun right against the camera stretch
	at a wide angle) */
	angle = viewmodel_vertical();
	if (!angle)
		angle = render_fov_authored_vertical(render.local_player_index);
	if (!angle || render.local_player_index < 0 || render.local_player_index >= MAXIMUM_LOCAL_PLAYERS ||
		global_window_parameters.rasterizer_target != _rasterizer_target_render_primary ||
		cinematic_in_progress() || (director_camera_scripted && *director_camera_scripted) ||
		director_get_perspective(render.local_player_index) != _director_perspective_first_person ||
		director_inhibited_facing(render.local_player_index))
	{
		return;
	}

	saved_render_camera = render.camera;
	saved_render_frustum = render.frustum;
	saved_rasterizer_camera = global_window_parameters.camera;
	saved_rasterizer_frustum = global_window_parameters.frustum;
	/* (the frustums' own bounds, the view's crop of a split screen: the
	visibility and the projection change together, the weapon's pose and
	markers do not; get_projection_bounds' slopes are not bounds) */
	render_bounds = saved_render_frustum.frustum_bounds;
	rasterizer_bounds = saved_rasterizer_frustum.frustum_bounds;
	render.camera.vertical_field_of_view = angle;
	global_window_parameters.camera.vertical_field_of_view = angle;
	render_camera_build_frustum(&render.camera, &render_bounds, &render.frustum, TRUE);
	render_camera_build_frustum(&global_window_parameters.camera, &rasterizer_bounds,
		&global_window_parameters.frustum, TRUE);
	projection_applied = TRUE;
	rasterizer_set_frustum_z(0.0f, 0.0f);
}

void viewmodel_projection_end(void)
{
	if (!projection_depth || --projection_depth || !projection_applied)
		return;
	render.camera = saved_render_camera;
	render.frustum = saved_render_frustum;
	global_window_parameters.camera = saved_rasterizer_camera;
	global_window_parameters.frustum = saved_rasterizer_frustum;
	projection_applied = FALSE;
	rasterizer_set_frustum_z(0.0f, 0.0f);
}

/* ---------- whether the first-person weapon is drawn */

boolean viewmodel_is_visible(void)
{
	static boolean initialized, visible;
	static unsigned long read_at;
	unsigned long changes = config_changes();

	if (!initialized || read_at != changes)
	{
		visible = config_boolean("display.viewmodel_visible") != 0;
		initialized = TRUE;
		read_at = changes;
	}
	return visible;
}

/* ---------- whether the energy shield's flare is drawn on the arms */

boolean viewmodel_shield_is_visible(void)
{
	static boolean initialized, visible;
	static unsigned long read_at;
	unsigned long changes = config_changes();

	if (!initialized || read_at != changes)
	{
		visible = config_boolean("display.viewmodel_shield") != 0;
		initialized = TRUE;
		read_at = changes;
	}
	return visible;
}

boolean viewmodel_draws_geometry(boolean first_person)
{
	return !first_person || viewmodel_is_visible();
}

unsigned long config_changes(void);

real view_gamepad_vertical_look(void)
{
	static real scale = 1.0f;
	static unsigned long read_at = (unsigned long)-1;

	if (read_at != config_changes())
	{
		read_at = config_changes();
		scale = (real)config_real("input.gamepad_vertical_look");
		if (!(scale >= 0.25f && scale <= 4.0f))
			scale = 1.0f;
	}
	return scale;
}

/* ---------- Wii Remote free aim */

#ifdef HALO_WIIU
#include "remote_free_aim_math.h"
#include "remote_ir_aim_math.h"
#include "remote_control_tuning.h"
#include "gunner_feedback.h"
#include <stdio.h>
const char *config_string(const char *name);
#include <string.h>

static struct remote_free_aim_state remote_aim;
static struct remote_adaptive_filter remote_adaptive;
static int remote_adaptive_enabled, remote_clutch, remote_guide;
static float remote_inner_x=0.15f,remote_inner_y=0.20f;
static float remote_center_x,remote_center_y,remote_range_x,remote_range_y,remote_outer,remote_scope_floor,remote_gunner_speed;
static float remote_profile_speed[3], remote_horizontal[3],remote_vertical[3];
static int remote_vehicle_selected; /* 1 vehicle, 2 scoped, 3 on-foot third person */
static long remote_vehicle_parent = NONE;
static int remote_aim_selected, remote_aim_recentered, remote_aim_source;
static struct remote_ir_filter remote_ir_filter;
static float remote_ir_x, remote_ir_y;
static float remote_ir_display_x, remote_ir_display_y;
static int remote_ir_valid;
static unsigned remote_ir_samples;
static float remote_ir_scale, remote_ir_fov;
real_euler_angles2d const *player_control_get_facing_angles(short local_player_index);
static long remote_aim_unit = NONE;
static real_vector3d remote_aim_forward, remote_aim_up;
static int remote_aim_camera_valid;
/* Tick-owned input is published with facing, then copied while the tick
worker is idle. Rendering never reads the worker's mutable offset. */
static struct remote_aim_snapshot
{
	struct remote_free_aim_state offset;
	int active;
	float pointer_x, pointer_y;
	int pointer_mode, pointer_valid;
    float inner_x,inner_y,outer;
} remote_aim_published, remote_aim_view;
unsigned char tick_overlap_reading_published(void);

static const struct remote_aim_snapshot *remote_free_aim_snapshot(void)
{
	return tick_overlap_reading_published() ? &remote_aim_view : &remote_aim_published;
}

static unsigned long remote_aim_settings_at = (unsigned long)-1;
static int remote_aim_enabled;
static float remote_aim_box, remote_aim_vertical_box, remote_aim_speed, remote_scope_sensitivity;

static void remote_free_aim_settings(void)
{
	if (remote_aim_settings_at != config_changes())
	{
		float box = (float)config_real("input.wiiu_wiimote_free_aim_box");
		float speed = (float)config_real("input.wiiu_wiimote_free_aim_speed");
		float vertical = (float)config_real("input.wiiu_wiimote_free_aim_vertical_box");
		remote_aim_enabled = config_boolean("input.wiiu_wiimote_free_aim");
        {
            static const char *const names[3]={"hip","scope","vehicle"};
            int i;
            for(i=0;i<3;i++) {
                char key[96];const char *preset;
                sprintf(key,"input.wiiu_wiimote_%s_profile",names[i]);preset=config_string(key);
                remote_profile_speed[i]=!strcmp(preset,"steady")?0.7f:!strcmp(preset,"fast")?1.4f:1.0f;
                sprintf(key,"input.wiiu_wiimote_%s_horizontal",names[i]);remote_horizontal[i]=(float)config_real(key);
                sprintf(key,"input.wiiu_wiimote_%s_vertical",names[i]);remote_vertical[i]=(float)config_real(key);
                if (!(remote_horizontal[i]>=0.25f && remote_horizontal[i]<=2))remote_horizontal[i]=1;
                if (!(remote_vertical[i]>=0.25f && remote_vertical[i]<=2))remote_vertical[i]=1;
            }
        }
        remote_adaptive_enabled=config_boolean("input.wiiu_wiimote_adaptive_filter");
        remote_guide=config_boolean("input.wiiu_wiimote_aim_guide");
        remote_center_x=(float)config_real("input.wiiu_wiimote_center_x");
        remote_center_y=(float)config_real("input.wiiu_wiimote_center_y");
        remote_range_x=(float)config_real("input.wiiu_wiimote_range_x");
        remote_range_y=(float)config_real("input.wiiu_wiimote_range_y");
        remote_gunner_speed=(float)config_real("input.wiiu_wiimote_gunner_speed");
        if(!(remote_gunner_speed>=0.25f && remote_gunner_speed<=2))remote_gunner_speed=1;
        remote_scope_floor=(float)config_real("input.wiiu_wiimote_scope_scan_floor");
        if (!(remote_scope_floor>=0 && remote_scope_floor<=0.5f))remote_scope_floor=0.25f;
        remote_outer=(float)config_real("input.wiiu_wiimote_turn_outer");
        if (!(remote_outer>=0.5f && remote_outer<=1))remote_outer=0.75f;
        remote_adaptive.ready=0;
        remote_ir_filter.next=remote_ir_filter.count=0;

		remote_ir_samples = (unsigned)config_integer("input.wiiu_wiimote_ir_samples");
		if (remote_ir_samples < 1 || remote_ir_samples > REMOTE_IR_SAMPLES) remote_ir_samples = 5;
		remote_ir_scale = (float)config_real("input.wiiu_wiimote_ir_scale");
		if (!(remote_ir_scale >= 0.25f && remote_ir_scale <= 2.0f)) remote_ir_scale = 0.9f;
		remote_ir_fov = (float)config_real("display.fov");
        remote_scope_sensitivity = (float)config_real("input.wiiu_wiimote_zoom_sensitivity");
        if (!(remote_scope_sensitivity >= 0.25f && remote_scope_sensitivity <= 2.0f)) remote_scope_sensitivity = 0.75f;
		if (!(box >= 2.0f && box <= 25.0f)) box = 12.0f;
		if (!(speed >= 10.0f && speed <= 180.0f)) speed = 75.0f;
		if (!(vertical >= 2.0f && vertical <= 25.0f)) vertical = 8.0f;
		remote_aim_box = box * (_pi / 180.0f);
		remote_aim_vertical_box = vertical * (_pi / 180.0f);
		remote_aim_speed = speed * (_pi / 180.0f);
		remote_aim_settings_at = config_changes();
	}
}

int main_smooth_defaults_net_game(void);
int director_port_free_camera(short local_player_index);

static int remote_free_aim_eligible(void)
{
	long index;
	struct unit_datum *unit;
	remote_free_aim_settings();
	if (!game_time_initialized() || !remote_aim_enabled ||
		local_player_count() != 1 || main_smooth_defaults_net_game() ||
		cinematic_in_progress() || (director_camera_scripted && *director_camera_scripted) ||
		director_get_perspective(0) != _director_perspective_first_person ||
		director_inhibited_facing(0) || player_control_get_zoom_level(0) != NONE)
		return 0;
	index = player_control_get_unit_index(0);
	unit = index == NONE ? NULL : unit_try_and_get(index);
	return unit && unit->object.parent_object_index == NONE &&
		!TEST_FLAG(unit->object.damage_flags, _object_dead_bit);
}
#endif

static void remote_free_aim_clear_input(void)
{
#ifdef HALO_WIIU
	remote_aim.yaw = remote_aim.pitch = 0.0f;
    remote_adaptive.ready=0;
	remote_aim_selected = remote_aim_recentered = remote_aim_source = 0;
	remote_ir_filter.next = remote_ir_filter.count = 0;
	remote_aim_unit = NONE;
	remote_vehicle_selected = 0;
	remote_vehicle_parent = NONE;
#endif
}

void remote_free_aim_reset(void)
{
	remote_free_aim_clear_input();
#ifdef HALO_WIIU
	remote_aim_camera_valid = 0;
	remote_aim_published.offset = remote_aim_view.offset = remote_aim;
	remote_aim_published.active = remote_aim_view.active = 0;
    remote_aim_published.pointer_mode = remote_aim_view.pointer_mode = 0;
#endif
}

void remote_free_aim_publish(void)
{
#ifdef HALO_WIIU
	remote_aim_published.offset = remote_aim;
    remote_aim_published.pointer_mode = (remote_vehicle_selected==1 || remote_vehicle_selected==5) ? 1 : remote_guide ? (remote_vehicle_selected==2 ? 2 : remote_aim_selected && remote_aim_source==2 ? 3 : 0) : 0;
    remote_aim_published.inner_x=remote_inner_x;remote_aim_published.inner_y=remote_inner_y;remote_aim_published.outer=remote_outer;
    remote_aim_published.pointer_valid = remote_ir_valid;
    if (remote_ir_valid) {
        remote_aim_published.pointer_x = remote_ir_display_x;
        remote_aim_published.pointer_y = remote_ir_display_y;
    }
	remote_aim_published.active = remote_aim_selected && remote_free_aim_eligible();
#endif
}

void remote_free_aim_sync(void)
{
#ifdef HALO_WIIU
	remote_aim_view = remote_aim_published;
#endif
}

int remote_free_aim_select(int remote)
{
#ifdef HALO_WIIU
	long unit;
    /* IR vehicle turning owns the pointer without moving the weapon/camera
     * independently. Preserve Halo's steering and seat-specific controls. */
    if (remote == 2 && game_time_initialized() && local_player_count() == 1 &&
        !main_smooth_defaults_net_game() && !cinematic_in_progress() &&
        !(director_camera_scripted && *director_camera_scripted) && !director_inhibited_facing(0))
    {
        long index = player_control_get_unit_index(0);
        struct unit_datum *rider = index == NONE ? NULL : unit_try_and_get(index);
        if (rider && rider->object.parent_object_index != NONE &&
            !TEST_FLAG(rider->object.damage_flags, _object_dead_bit))
        {
            int vehicle_mode=gunner_view_weapon(0,NULL)!=NONE?5:1;
            remote_free_aim_settings();
            if (remote_vehicle_selected != vehicle_mode || remote_vehicle_parent != rider->object.parent_object_index || remote_aim_unit != index)
                remote_free_aim_clear_input();
            remote_vehicle_selected = vehicle_mode;
            remote_vehicle_parent = rider->object.parent_object_index;
            remote_aim_unit = index;
            return 1;
        }
    }
    if (remote == 2 && game_time_initialized() && local_player_count() == 1 &&
        !main_smooth_defaults_net_game() && !cinematic_in_progress() &&
        !(director_camera_scripted && *director_camera_scripted) && !director_inhibited_facing(0) &&
        director_get_perspective(0) == _director_perspective_first_person && player_control_get_zoom_level(0) != NONE)
    {
        long index = player_control_get_unit_index(0);
        struct unit_datum *rider = index == NONE ? NULL : unit_try_and_get(index);
        if (rider && rider->object.parent_object_index == NONE && !TEST_FLAG(rider->object.damage_flags, _object_dead_bit))
        {
            remote_free_aim_settings();
            if (remote_vehicle_selected != 2 || remote_aim_unit != index)
                remote_free_aim_clear_input();
            remote_vehicle_selected = 2;
            remote_aim_unit = index;
            return 1;
        }
    }
    if (remote == 2 && game_time_initialized() && local_player_count() == 1 &&
        !main_smooth_defaults_net_game() && !cinematic_in_progress() &&
        !(director_camera_scripted && *director_camera_scripted) && !director_inhibited_facing(0) &&
        director_get_perspective(0) == _director_perspective_third_person)
    {
        long index=player_control_get_unit_index(0);
        struct unit_datum *rider=index==NONE?NULL:unit_try_and_get(index);
        if(rider && rider->object.parent_object_index==NONE && !TEST_FLAG(rider->object.damage_flags,_object_dead_bit)) {
            remote_free_aim_settings();
            if(remote_vehicle_selected!=3 || remote_aim_unit!=index)remote_free_aim_clear_input();
            remote_vehicle_selected=3;remote_aim_unit=index;return 1;
        }
    }
    if(remote==2 && game_time_initialized() && local_player_count()==1 &&
        !main_smooth_defaults_net_game() && !cinematic_in_progress() && director_port_free_camera(0))
    {
        remote_free_aim_settings();
        if(remote_vehicle_selected!=4)remote_free_aim_clear_input();
        remote_vehicle_selected=4;return 1;
    }
    if (remote_vehicle_selected)
    {
        remote_free_aim_clear_input();
    }
	if (!remote || !remote_free_aim_eligible())
	{
		remote_free_aim_clear_input();
		return 0;
	}
	unit = player_control_get_unit_index(0);
	if (unit != remote_aim_unit || remote_aim_source != remote)
		remote_free_aim_clear_input();
	remote_aim_unit = unit;
	remote_aim_source = remote;
	remote_aim_selected = 1;
	return 1;
#else
	(void)remote;
	return 0;
#endif
}

int remote_free_aim_active(short local_player_index)
{
#ifdef HALO_WIIU
	return local_player_index == 0 && remote_free_aim_snapshot()->active;
#else
	(void)local_player_index;
	return 0;
#endif
}

void remote_free_aim_recentre(float *yaw, float *pitch)
{
	*yaw = *pitch = 0.0f;
#ifdef HALO_WIIU
	if (remote_aim_selected)
	{
		*yaw = -remote_aim.yaw;
		*pitch = -remote_aim.pitch;
		remote_aim_recentered = 1;
		remote_aim.yaw = remote_aim.pitch = 0.0f;
    remote_adaptive.ready=0;
	}
#endif
}

void remote_free_aim_pointer_sample(float x, float y, int valid)
{
#ifdef HALO_WIIU
	remote_ir_x = x;
	remote_ir_y = y;
	remote_ir_valid = valid && remote_free_aim_finite(x) && remote_free_aim_finite(y);
#else
	(void)x; (void)y; (void)valid;
#endif
}

void remote_aim_clutch(int held) {
#ifdef HALO_WIIU
 remote_clutch=held;
#else
 (void)held;
#endif
}
#ifdef HALO_WIIU

static void remote_control_filter(float *x,float *y,float seconds)
{
    *x=remote_calibrate(*x,remote_center_x,remote_range_x);
    *y=remote_calibrate(*y,remote_center_y,remote_range_y);
    if(remote_adaptive_enabled) remote_adaptive_sample(&remote_adaptive,x,y,seconds);
    else remote_ir_filter_sample(&remote_ir_filter,x,y,remote_ir_samples,0.5f);
}

#endif
void remote_free_aim_input(short local_player_index, float *yaw, float *pitch, float seconds)
{
#ifdef HALO_WIIU
    if(remote_clutch) { *yaw=*pitch=0; remote_adaptive.ready=0; return; }
    if (local_player_index == 0 && remote_vehicle_selected)
    {
        float x = remote_ir_x, y = remote_ir_y;
        *yaw = *pitch = 0.0f;
        if (!remote_ir_valid)
        {
            remote_ir_filter.next = remote_ir_filter.count = 0;
            remote_adaptive.ready=0;
            return;
        }
        remote_control_filter(&x, &y, seconds);
        remote_ir_display_x = x; remote_ir_display_y = y;
        if (remote_vehicle_selected == 2)
        {
            long index = player_control_get_unit_index(0);
            float magnification = unit_get_zoom_magnification(index, player_control_get_zoom_level(0));
            float speed;
            remote_inner_x=remote_inner_y=0.08f;
            if(!remote_free_aim_finite(magnification) || magnification<1)return;
            speed=remote_aim_speed*remote_scope_sensitivity*remote_profile_speed[1];
            *yaw=remote_scope_turn(x,remote_outer,speed*remote_horizontal[1],magnification,remote_scope_floor,seconds);
            *pitch=remote_scope_turn(y,remote_outer,speed*remote_vertical[1],magnification,remote_scope_floor,seconds);
        }
        else {
            int context=(remote_vehicle_selected==3 || remote_vehicle_selected==4)?0:2;
            float speed=remote_aim_speed*remote_profile_speed[context]*(remote_vehicle_selected==5?remote_gunner_speed:1);
            remote_inner_x=remote_vehicle_selected==5?0.08f:remote_vehicle_selected>=3?0.10f:0.15f;
            remote_inner_y=remote_vehicle_selected==5?0.10f:remote_vehicle_selected>=3?0.12f:0.20f;
            *yaw=remote_turn_outer(x,remote_inner_x,remote_outer,speed*remote_horizontal[context],seconds);
            *pitch=remote_turn_outer(y,remote_inner_y,remote_outer,speed*(context==2 && remote_vehicle_selected!=5?0.65f:1)*remote_vertical[context],seconds);
        }
        return;
    }
	if (remote_aim_recentered)
	{
		remote_aim_recentered = 0;
		return;
	}
	if (local_player_index == 0 && remote_aim_selected)
	{
		if (remote_aim_source == 2)
		{
			real_euler_angles2d const *aim;
			float x, y, tangent_y, tangent_x;
			*yaw = *pitch = 0.0f;
			if (!remote_ir_valid)
			{
				remote_ir_filter.next = remote_ir_filter.count = 0;
                remote_adaptive.ready=0;
				return;
			}
			x = remote_ir_x;
			y = remote_ir_y;
			remote_control_filter(&x, &y, seconds);
			remote_ir_display_x=x;remote_ir_display_y=y;
			x *= remote_ir_scale; y *= remote_ir_scale;
			aim = player_control_get_facing_angles(local_player_index);
			tangent_y = remote_ir_fov >= 20.0f && remote_ir_fov <= 150.0f ?
				tanf(remote_ir_fov * (_pi / 360.0f)) * (9.0f / 16.0f) :
				0.75f * render_camera_get_adjusted_field_of_view_tangent(player_control_get_field_of_view(local_player_index));
			tangent_x = tangent_y * (16.0f / 9.0f);
            remote_inner_x=tanf(remote_aim_box)/tangent_x/remote_ir_scale;
            remote_inner_y=tanf(remote_aim_vertical_box)/tangent_y/remote_ir_scale;
			remote_ir_aim_step(&remote_aim, x, y, aim->yaw, aim->pitch, tangent_x, tangent_y,
				remote_aim_box, remote_aim_vertical_box, remote_aim_speed, seconds, yaw, pitch);
            *yaw += remote_turn_outer(x/remote_ir_scale,tanf(remote_aim_box)/tangent_x/remote_ir_scale,remote_outer,
                remote_aim_speed*remote_profile_speed[0]*remote_horizontal[0],seconds)-remote_ir_turn(x,tanf(remote_aim_box)/tangent_x,remote_aim_speed,seconds);
            *pitch += remote_turn_outer(y/remote_ir_scale,tanf(remote_aim_vertical_box)/tangent_y/remote_ir_scale,remote_outer,
                remote_aim_speed*remote_profile_speed[0]*remote_vertical[0],seconds)-remote_ir_turn(y,tanf(remote_aim_vertical_box)/tangent_y,remote_aim_speed,seconds);
            *pitch=PIN(aim->pitch+*pitch,-_pi*0.475f,_pi*0.475f)-aim->pitch;

		}
		else
			remote_free_aim_step(&remote_aim, yaw, pitch, remote_aim_box, remote_aim_vertical_box, remote_aim_speed, seconds);
	}

#else
	(void)local_player_index; (void)yaw; (void)pitch; (void)seconds;
#endif
}

int remote_driving_pointer(float *x, float *y, int *valid)
{
#ifdef HALO_WIIU
    const struct remote_aim_snapshot *snapshot = remote_free_aim_snapshot();
    if (!snapshot->pointer_mode) return 0;
    *x = snapshot->pointer_x; *y = snapshot->pointer_y; *valid = snapshot->pointer_valid;
    return 1;
#else
    (void)x; (void)y; (void)valid;
    return 0;
#endif
}

void remote_driving_pointer_layout(float *x,float *y,float *outer)
{
#ifdef HALO_WIIU
    const struct remote_aim_snapshot *snapshot=remote_free_aim_snapshot();
    *x=PIN(snapshot->inner_x,0,0.9f);*y=PIN(snapshot->inner_y,0,0.9f);*outer=snapshot->outer;
#else
    *x=0.15f;*y=0.20f;*outer=0.75f;
#endif
}

void remote_free_aim_camera(short local_player_index, struct render_camera *camera)
{
#ifdef HALO_WIIU
	real_euler_angles2d facing;
	real_vector3d upright, left;
	real roll_up, roll_left, cosine_yaw, sine_yaw, cosine_pitch, sine_pitch;
	/* Overlay and non-player windows are prepared after the player view.
	They must not invalidate the player's captured aiming camera. */
	if (local_player_index != 0)
		return;
	remote_aim_camera_valid = 0;
	if (!remote_free_aim_active(local_player_index))
		return;
	remote_aim_forward = camera->forward;
	remote_aim_up = camera->up;
	remote_aim_camera_valid = 1;
	euler_angles2d_from_vector3d(&facing, &camera->forward);
	cosine_yaw = cosf(facing.yaw); sine_yaw = sinf(facing.yaw);
	cosine_pitch = cosf(facing.pitch); sine_pitch = sinf(facing.pitch);
	upright.i = -sine_pitch * cosine_yaw;
	upright.j = -sine_pitch * sine_yaw;
	upright.k = cosine_pitch;
	left.i = -sine_yaw; left.j = cosine_yaw; left.k = 0.0f;
	roll_up = dot_product3d(&camera->up, &upright);
	roll_left = dot_product3d(&camera->up, &left);
	facing.yaw -= remote_free_aim_snapshot()->offset.yaw;
	facing.pitch = PIN(facing.pitch - remote_free_aim_snapshot()->offset.pitch, -_pi * 0.49f, _pi * 0.49f);
	vector3d_from_euler_angles2d(&camera->forward, &facing);
	cosine_yaw = cosf(facing.yaw); sine_yaw = sinf(facing.yaw);
	cosine_pitch = cosf(facing.pitch); sine_pitch = sinf(facing.pitch);
	camera->up.i = -sine_pitch * cosine_yaw * roll_up - sine_yaw * roll_left;
	camera->up.j = -sine_pitch * sine_yaw * roll_up + cosine_yaw * roll_left;
	camera->up.k = cosine_pitch * roll_up;
	normalize3d(&camera->up);
#else
	(void)local_player_index; (void)camera;
#endif
}

void remote_free_aim_weapon_camera(short local_player_index, struct render_camera *camera)
{
#ifdef HALO_WIIU
	if (remote_aim_camera_valid && remote_free_aim_active(local_player_index))
	{
		real model_angle = viewmodel_vertical();
		const real world_angle = camera->vertical_field_of_view;
		real_vector3d left, direction;
		real forward_part, left_part, up_part, ratio, length_squared;
        real_vector3d weapon_up;
		if (!model_angle)
			model_angle = render_fov_authored_vertical(local_player_index);
		if (!model_angle)
			model_angle = world_angle;
        /* Do not let an invalid projection or basis enter the arm matrices. */
        if (!(world_angle > 0.0f && world_angle < 3.14159265f &&
            model_angle > 0.0f && model_angle < 3.14159265f))
            return;
		ratio = tanf(model_angle * 0.5f) / tanf(world_angle * 0.5f);
        if (!(ratio > 0.0f && ratio < 1000.0f))
            return;
		/* Match screen-space aim across the world's and weapon's projections.
		The weapon's size stays governed by its own FOV. */
		cross_product3d(&camera->up, &camera->forward, &left);
        length_squared = dot_product3d(&left, &left);
        if (!(length_squared > 0.00000001f && length_squared < 3.402823466e38f))
            return;
        normalize3d(&left);
		forward_part = dot_product3d(&remote_aim_forward, &camera->forward);
		left_part = dot_product3d(&remote_aim_forward, &left) * ratio;
		up_part = dot_product3d(&remote_aim_forward, &camera->up) * ratio;
		direction.i = camera->forward.i * forward_part + left.i * left_part + camera->up.i * up_part;
		direction.j = camera->forward.j * forward_part + left.j * left_part + camera->up.j * up_part;
		direction.k = camera->forward.k * forward_part + left.k * left_part + camera->up.k * up_part;
        length_squared = dot_product3d(&direction, &direction);
        if (!(length_squared > 0.00000001f && length_squared < 3.402823466e38f))
            return;
		normalize3d(&direction);
        /* Keep the arm root upright in the displayed view. The uncompensated
         * aim-up follows world azimuth, which can spin near vertical even
         * when the actual pointing direction barely changes. */
        cross_product3d(&camera->up, &direction, &left);
        length_squared = dot_product3d(&left, &left);
        if (!(length_squared > 0.00000001f && length_squared < 3.402823466e38f))
            return;
		normalize3d(&left);
        cross_product3d(&direction, &left, &weapon_up);
        camera->forward = direction;
        camera->up = weapon_up;
	}
#else
	(void)local_player_index; (void)camera;
#endif
}

void remote_free_aim_movement(short local_player_index, float *forward, float *left)
{
#ifdef HALO_WIIU
	if (local_player_index == 0 && remote_aim_selected)
	{
		const float cosine = cosf(remote_aim.yaw), sine = sinf(remote_aim.yaw);
		const float x = *forward, y = *left;
		*forward = x * cosine + y * sine;
		*left = y * cosine - x * sine;
	}
#else
	(void)local_player_index; (void)forward; (void)left;
#endif
}

int remote_free_aim_reticle_target(short local_player_index, real_point3d *target)
{
#ifdef HALO_WIIU
	if (remote_aim_camera_valid && remote_free_aim_active(local_player_index))
	{
		/* The aiming cursor follows the same captured direction as the hands.
		Weapon animation and changing collision depth do not move this cursor. */
		point_from_line3d(&render.camera.position, &remote_aim_forward, 128.0f, target);
		return 1;
	}
#else
	(void)local_player_index; (void)target;
#endif
	return 0;
}
