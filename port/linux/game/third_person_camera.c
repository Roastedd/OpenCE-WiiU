#include "cseries.h"
#include "third_person_camera.h"
#include "camera/static_camera.h"
#include "camera/director.h"
#include "cutscene/cinematics.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "objects/objects.h"
#include "units/units.h"
#include "physics/collisions.h"
#include "render/render.h"
#include <math.h>

int config_boolean(const char *name);
double config_real(const char *name);
unsigned long config_changes(void);

int third_person_camera_available(short local_player_index)
{
#ifdef HALO_WIIU
    long index;
    struct unit_datum *unit;
    if(local_player_index<0 || local_player_index>=MAXIMUM_LOCAL_PLAYERS ||
        !config_boolean("display.wiiu_shoulder_camera") || game_engine_running() ||
        cinematic_in_progress() || (director_camera_scripted && *director_camera_scripted))return 0;
    index=player_control_get_unit_index(local_player_index);
    unit=index==NONE ? NULL : unit_try_and_get(index);
    return unit && unit->object.parent_object_index==NONE && !TEST_FLAG(unit->object.damage_flags,_object_dead_bit);
#else
    (void)local_player_index;return 0;
#endif
}

int third_person_camera_scoped(short local_player_index)
{
    return third_person_camera_available(local_player_index) && player_control_get_zoom_level(local_player_index)!=NONE;
}

void third_person_camera_adjust(short local_player_index,struct camera_command *command)
{
    static unsigned long at=(unsigned long)-1;
    static float distance,shoulder,height;
    unsigned long changes;
    if(!third_person_camera_available(local_player_index))return;
    changes=config_changes();
    if(at!=changes) {
        distance=(float)config_real("display.wiiu_camera_distance");
        shoulder=(float)config_real("display.wiiu_camera_shoulder");
        height=(float)config_real("display.wiiu_camera_height");
        if(!(distance>=0.6f && distance<=2.0f))distance=1.1f;
        if(!(shoulder>=-0.5f && shoulder<=0.5f))shoulder=0.25f;
        if(!(height>=0 && height<=0.4f))height=0.1f;
        at=changes;
    }
    command->offset.i=0;
    command->offset.j=shoulder;
    command->offset.k=height;
    command->depth=distance;
    /* Sweep the small lateral/vertical pivot shift before the observer
     * checks the backward boom. Do not start that boom beyond a nearby wall. */
    if(!third_person_camera_scoped(local_player_index)) {
        real length=sqrtf(command->forward.i*command->forward.i+command->forward.j*command->forward.j);
        real_vector3d shift;
        struct collision_result hit;
        shift.i=length>0.0001f ? shoulder*command->forward.j/length : 0;
        shift.j=length>0.0001f ? -shoulder*command->forward.i/length : 0;
        shift.k=height;
        if(collision_test_vector(FLAG(_collision_test_front_facing_surfaces_bit)|FLAG(_collision_test_structure_bit)|
            FLAG(_collision_test_objects_bit)|FLAG(_collision_test_objects_scenery_bit), &command->position,&shift,
            player_control_get_unit_index(local_player_index),&hit)) {
            real fraction=PIN(hit.t-0.05f,0,1);
            command->offset.j*=fraction;command->offset.k*=fraction;
        }
    }
    if(third_person_camera_scoped(local_player_index)) {
        unit_get_camera_position(player_control_get_unit_index(local_player_index),&command->position);
        command->offset.j=command->offset.k=0;
        command->depth=0;
        command->field_of_view=player_control_get_field_of_view(local_player_index);
    }
}

/* Display aim uses the same frame-facing as the camera, not an animated
 * muzzle marker or a tick-old weapon node. Collision still places the reticle. */
int third_person_camera_aim_ray(short local_player_index,real_point3d *origin,real_vector3d *direction)
{
    if(!third_person_camera_available(local_player_index))return 0;
    unit_get_camera_position(player_control_get_unit_index(local_player_index),origin);
    render_interpolation_object_point(player_control_get_unit_index(local_player_index),origin);
    *direction=render.camera.forward;
    return 1;
}
