#include "cseries.h"
#include "gunner_feedback.h"
#include "game/players.h"
#include "game/game.h"
#include "objects/objects.h"
#include "units/units.h"
#include "units/vehicles.h"
#include "units/unit_definitions.h"
#include "items/weapons.h"
#include "render/render.h"
#include "interface/hud.h"
#include "interface/ui_widget.h"
#include "cutscene/cinematics.h"
#include "rasterizer/rasterizer.h"
#include "text/draw_string.h"

int config_boolean(const char *name);
void platform_log(const char *format,...);

long gunner_view_weapon(short local_player_index,long *vehicle_index)
{
    long index,parent;
    struct unit_datum *unit,*vehicle;
    struct unit_definition *definition;
    struct unit_seat *seat;
    if(vehicle_index)*vehicle_index=NONE;
#ifndef HALO_WIIU
    (void)local_player_index;return NONE;
#endif
    if(local_player_index<0 || local_player_index>=MAXIMUM_LOCAL_PLAYERS)return NONE;
    index=player_control_get_unit_index(local_player_index);
    unit=index==NONE?NULL:unit_try_and_get(index);
    if(!unit || TEST_FLAG(unit->object.damage_flags,_object_dead_bit))return NONE;
    parent=unit->object.parent_object_index;
    vehicle=parent==NONE?NULL:vehicle_try_and_get(parent);
    if(!vehicle || TEST_FLAG(vehicle->object.damage_flags,_object_dead_bit))return NONE;
    definition=unit_definition_get(vehicle->definition_index);
    if(unit->unit.parent_seat_index<0 || unit->unit.parent_seat_index>=definition->unit.seats.count)return NONE;
    seat=TAG_BLOCK_GET_ELEMENT(&definition->unit.seats,unit->unit.parent_seat_index,struct unit_seat);
#ifdef HALO_WIIU
    {
        /* what seats the vehicles have, once per vehicle and seat the local player takes: which seats count as gunner's (reticle, firing effects) */
        static long logged_vehicle=NONE,logged_seat=-1;
        if(local_player_index==0 && (logged_vehicle!=parent || logged_seat!=unit->unit.parent_seat_index))
        {
            logged_vehicle=parent;logged_seat=unit->unit.parent_seat_index;
            platform_log("vehicle seat: definition %ld, seat %d of %ld, flags %04x (gunner %d), vehicle's current weapon %ld",
                (long)vehicle->definition_index,(int)unit->unit.parent_seat_index,(long)definition->unit.seats.count,
                (unsigned)seat->flags,TEST_FLAG(seat->flags,_unit_seat_gunner_bit)?1:0,
                (long)unit_inventory_get_weapon(parent,vehicle->unit.current_weapon_index));
        }
    }
#endif
    if(!TEST_FLAG(seat->flags,_unit_seat_gunner_bit))return NONE;
    if(vehicle_index)*vehicle_index=parent;
    return unit_inventory_get_weapon(parent,vehicle->unit.current_weapon_index);
}

/* Per rendered view, bounded essential firing feedback survives optional VFX shedding. */
static struct {int known;long frame;short view;long vehicle,weapon,player;unsigned particles,trails;} feedback;
static void feedback_refresh(void)
{
    if(feedback.known && feedback.frame==render.frame_index && feedback.view==render.local_player_index)return;
    feedback.known=1;feedback.frame=render.frame_index;feedback.view=render.local_player_index;
    feedback.vehicle=feedback.weapon=feedback.player=NONE;feedback.particles=feedback.trails=0;
    if(!config_boolean("display.wiiu_gunner_feedback") || render.local_player_index<0)return;
    feedback.weapon=gunner_view_weapon(render.local_player_index,&feedback.vehicle);
    if(feedback.weapon!=NONE)feedback.player=local_player_get_player_index(render.local_player_index);
}
static int feedback_owned(long object_index)
{
    struct object_datum *object;
    if(feedback.weapon==NONE || object_index==NONE)return 0;
    if(object_index==feedback.weapon || object_index==feedback.vehicle)return 1;
    object=object_try_and_get(object_index);
    return object && (object->object.parent_object_index==feedback.weapon ||
        object->object.parent_object_index==feedback.vehicle ||
        object->object.owner_object_index==feedback.vehicle ||
        (feedback.player!=NONE && object->object.owner_player_index==feedback.player));
}
int gunner_feedback_take_particle(long object_index)
{
    feedback_refresh();
    if(feedback.particles>=16 || !feedback_owned(object_index))return 0;
    feedback.particles++;return 1;
}
int gunner_feedback_take_contrail(long object_index)
{
    feedback_refresh();
    if(feedback.trails<8 && feedback_owned(object_index)){feedback.trails++;return 1;}
#ifdef HALO_WIIU
    /* port: the governor's steps 5 to 7 switch contrails off, and with them every bullet's tracer and the tank shell's trail: the picture of a shot
       leaving the weapon. The local player's own shots (12 a view and frame) and, of everyone else's, the first 16 projectiles' trails stay. */
    {
        struct object_datum *object;
        static long frame=-1;static short view=-1;static unsigned own,others;
        if(frame!=render.frame_index || view!=render.local_player_index){frame=render.frame_index;view=render.local_player_index;own=others=0;}
        object=object_index==NONE?NULL:object_try_and_get(object_index);
        if(object && object->object.type!=_object_type_projectile)return 0;
        if(object && render.local_player_index>=0)
        {
            long player=local_player_get_player_index(render.local_player_index);
            if(player!=NONE && object->object.owner_player_index==player){if(own<12){own++;return 1;}return 0;}
        }
        if(object && others<16){others++;return 1;}
    }
#endif
    return 0;
}
int gunner_feedback_active(void) {feedback_refresh();return feedback.weapon!=NONE;}
void gunner_feedback_draw(void)
{
    struct weapon_datum *weapon;
    long now,font;
    rectangle2d bounds;
    real_argb_color color;
    if(ui_widgets_active() || cinematic_in_progress())return;
    feedback_refresh();
    weapon=feedback.weapon==NONE?NULL:weapon_try_and_get(feedback.weapon);
    if(!weapon)return;
    now=game_time_get();
    if(weapon->weapon.game_time_last_fired<=0 || now<weapon->weapon.game_time_last_fired ||
        now-weapon->weapon.game_time_last_fired>2)return;
    font=hud_get_font_index();if(font==NONE)return;
    bounds=render.camera.window_bounds;
    offset_rectangle2d(&bounds,-render.camera.viewport_bounds.x0,-render.camera.viewport_bounds.y0);
    bounds.y0=bounds.y0+(bounds.y1-bounds.y0)*5/8;bounds.y1=bounds.y0+24;
    color.alpha=1;color.red=1;color.green=0.75f;color.blue=0.25f;
    draw_string_set_draw_mode(font,NONE,2,0,&color);
    rasterizer_draw_string(&bounds,NULL,NULL,0,"FIRING");
}
