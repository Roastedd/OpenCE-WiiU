from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'port/linux/game/gunner_feedback.c').read_text();s='\n'.join(l for l in s.splitlines() if not l.startswith('#include'))
fixture=r'''
#include <assert.h>
#include <stddef.h>
#define HALO_WIIU
#define NONE -1
#define MAXIMUM_LOCAL_PLAYERS 4
#define _object_dead_bit 0
#define _unit_seat_gunner_bit 1
#define TEST_FLAG(x,b) ((x)&(1u<<(b)))
#define TAG_BLOCK_GET_ELEMENT(b,i,t) (&((t*)(b)->address)[i])
typedef struct{short x0,y0,x1,y1;}rectangle2d;
typedef struct{float alpha,red,green,blue;}real_argb_color;
struct unit_datum{long definition_index;struct{unsigned damage_flags;long parent_object_index;}object;struct{short parent_seat_index,current_weapon_index;}unit;};
struct unit_seat{unsigned flags;};
struct block{int count;void*address;};
struct unit_definition{struct{struct block seats;}unit;};
struct object_datum{struct{long parent_object_index,owner_object_index,owner_player_index;}object;};
struct weapon_datum{struct{long game_time_last_fired;}weapon;};
static struct unit_seat seats[1]={{2}};
static struct unit_definition definition={{{1,seats}}};
static struct unit_datum player={0,{0,2},{0,0}},vehicle={0,{0,NONE},{0,0}};
static struct object_datum projectile={{NONE,2,NONE}};
static struct weapon_datum weapon={{99}};
static struct{long frame_index;short local_player_index;struct{rectangle2d window_bounds,viewport_bounds;}camera;}render={1,0,{{0,0,640,480},{0,0,640,480}}};
static int enabled=1,texts,menu;
int config_boolean(const char*n){(void)n;return enabled;}
long player_control_get_unit_index(short p){(void)p;return 1;}
struct unit_datum*unit_try_and_get(long i){return i==1?&player:i==2?&vehicle:NULL;}
struct unit_datum*vehicle_try_and_get(long i){return i==2?&vehicle:NULL;}
struct unit_definition*unit_definition_get(long i){(void)i;return &definition;}
long unit_inventory_get_weapon(long i,short slot){assert(i==2&&slot==0);return 9;}
long local_player_get_player_index(short p){(void)p;return 7;}
struct object_datum*object_try_and_get(long i){return i==10?&projectile:NULL;}
struct weapon_datum*weapon_try_and_get(long i){return i==9?&weapon:NULL;}
int ui_widgets_active(void){return menu;}int cinematic_in_progress(void){return 0;}
long game_time_get(void){return 100;}long hud_get_font_index(void){return 1;}
void offset_rectangle2d(rectangle2d*r,short x,short y){r->x0+=x;r->x1+=x;r->y0+=y;r->y1+=y;}
void draw_string_set_draw_mode(long a,int b,int c,int d,real_argb_color*e){(void)a;(void)b;(void)c;(void)d;(void)e;}
void rasterizer_draw_string(rectangle2d*r,void*a,void*b,int c,const char*t){(void)a;(void)b;(void)c;(void)t;assert(r->y1<=480);texts++;}
'''
main=r'''
int main(void){long v;
assert(gunner_view_weapon(0,&v)==9&&v==2);assert(gunner_view_weapon(-1,&v)==NONE);
seats[0].flags=0;assert(gunner_view_weapon(0,&v)==NONE);seats[0].flags=2;
player.unit.parent_seat_index=1;assert(gunner_view_weapon(0,&v)==NONE);player.unit.parent_seat_index=0;
assert(!gunner_feedback_take_particle(123));
for(int i=0;i<16;i++)assert(gunner_feedback_take_particle(9));assert(!gunner_feedback_take_particle(9));
for(int i=0;i<8;i++)assert(gunner_feedback_take_contrail(10));assert(!gunner_feedback_take_contrail(10));
gunner_feedback_draw();assert(texts==1);weapon.weapon.game_time_last_fired=90;gunner_feedback_draw();assert(texts==1);
render.frame_index++;assert(gunner_feedback_take_particle(2));
menu=1;weapon.weapon.game_time_last_fired=100;gunner_feedback_draw();assert(texts==1);menu=0;
enabled=0;render.frame_index++;assert(!gunner_feedback_active());assert(!gunner_feedback_take_contrail(10));
return 0;}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(fixture+s+main)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Gunner seat validation, effect ownership/budgets and confirmed-shot feedback passed')
