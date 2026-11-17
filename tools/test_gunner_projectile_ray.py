from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1];s=(root/'source/items/weapons.c').read_text();a=s.index('boolean weapon_get_projectile_ray(');b=s.index('\nstatic void trigger_create_projectiles',a)
code=r'''
#include <assert.h>
#include <stddef.h>
#define NONE -1
#define FALSE 0
#define TRUE 1
#define TEST_FLAG(x,b) ((x)&(1u<<(b)))
#define TAG_BLOCK_GET_ELEMENT(b,i,t) (&((t*)(b)->address)[i])
typedef int boolean;typedef float real;
typedef struct{float x,y,z;}real_point3d;
typedef struct{float i,j,k;}real_vector3d;
enum{_weapon_trigger_projectiles_cannot_be_aimed_bit,_object_dead_bit,_unit_fires_from_camera_bit,_weapon_trigger_uses_weapon_origin_bit};
struct block{int count;void*address;};
struct weapon_trigger_definition{struct{long index;}projectile;unsigned flags;real_point3d first_person_weapon_offset;};
struct weapon_definition{struct{struct block triggers;}weapon;};
struct weapon_datum{long definition_index;};
struct unit_datum{long definition_index;struct{unsigned damage_flags;}object;struct{long player_index,gunner_object_index;}unit;};
struct unit_definition{struct{unsigned flags;}unit;};
struct object_marker{struct{real_point3d position;real_vector3d forward;}matrix;};
static struct weapon_trigger_definition trigger={{3},0,{0,0,0}};
static struct weapon_definition definition={{{1,&trigger}}};
static struct weapon_datum weapon={0};static struct unit_definition ud={{0}};
static struct unit_datum owner={0,{0},{NONE,2}},gunner={0,{0},{7,NONE}};
static real_vector3d up={0,0,1},left={0,1,0};static real_vector3d *global_up3d=&up,*global_left3d=&left;
static long preview=NONE;static int marker_valid=1;
struct weapon_datum*weapon_get(long i){(void)i;return &weapon;}
struct weapon_definition*weapon_definition_get(long i){(void)i;return &definition;}
long weapon_get_owner_object_index(long i){(void)i;return 1;}
struct unit_datum*unit_try_and_get(long i){return i==1?&owner:i==2?&gunner:NULL;}
struct unit_definition*unit_definition_get(long i){(void)i;return &ud;}
long weapon_get_effect_object_index(long i){return i;}
int object_get_marker_by_name(long i,const char*n,struct object_marker*m,int count){(void)i;(void)n;(void)count;m->matrix.position=(real_point3d){1,2,3};m->matrix.forward=(real_vector3d){1,0,0};return marker_valid;}
static int last_use_aiming_vector;
void unit_adjust_projectile_ray(long i,real_point3d*p,real_vector3d*d,real*v,int a,int u){
 (void)i;(void)p;(void)v;(void)a;last_use_aiming_vector=u;
 /* A simulation-tick aim can disagree with the frame-interpolated barrel. */
 if(u)*d=(real_vector3d){0,1,0};
}
void cross_product3d(const real_vector3d*a,const real_vector3d*b,real_vector3d*r){(void)a;(void)b;*r=left;}
float normalize3d(real_vector3d*v){(void)v;return 1;}
void point_from_line3d(const real_point3d*p,const real_vector3d*d,float f,real_point3d*out){out->x=p->x+d->i*f;out->y=p->y+d->j*f;out->z=p->z+d->k*f;}
void player_aim_projectile_preview(long i,real_point3d*p,real_vector3d*d){(void)p;(void)d;assert(i!=NONE);preview=i;}
'''+s[a:b]+r'''
int main(void){real_point3d p;real_vector3d d;
assert(weapon_get_projectile_ray(9,0,&p,&d)&&preview==7);
if(last_use_aiming_vector || d.i!=1 || d.j!=0)return 2;
owner.unit.player_index=8;owner.unit.gunner_object_index=NONE;
assert(weapon_get_projectile_ray(9,0,&p,&d)&&preview==8);
assert(last_use_aiming_vector && d.i==0 && d.j==1);
owner.unit.gunner_object_index=2;
owner.unit.player_index=NONE;gunner.unit.player_index=NONE;assert(!weapon_get_projectile_ray(9,0,&p,&d));
gunner.unit.player_index=7;owner.unit.gunner_object_index=NONE;assert(!weapon_get_projectile_ray(9,0,&p,&d));
owner.unit.gunner_object_index=2;assert(!weapon_get_projectile_ray(9,1,&p,&d));
marker_valid=0;assert(!weapon_get_projectile_ray(9,0,&p,&d));return 0;}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code)
 subprocess.run(['cc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
 # Negative control: the previous hardcoded TRUE selects the wrong tick ray.
 legacy=code.replace('unit->unit.gunner_object_index == NONE','TRUE',1)
 assert legacy!=code
 (p/'legacy.c').write_text(legacy)
 subprocess.run(['cc','-Wall','-Wextra','-Werror',str(p/'legacy.c'),'-o',str(p/'legacy')],check=True)
 result=subprocess.run([str(p/'legacy')],capture_output=True)
 assert result.returncode==2, result
print('Gunner rendered-barrel direction, normal aim, owner/marker guards and negative control passed')
