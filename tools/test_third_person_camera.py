from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'port/linux/game/third_person_camera.c').read_text();s='\n'.join(l for l in s.splitlines() if not l.startswith('#include'))
fixture=r'''
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#define HALO_WIIU
#define NONE -1
#define MAXIMUM_LOCAL_PLAYERS 4
#define _object_dead_bit 0
#define TEST_FLAG(x,b) ((x)&(1u<<(b)))
#define FLAG(b) (1u<<(b))
#define PIN(v,a,b) ((v)<(a)?(a):(v)>(b)?(b):(v))
enum {_collision_test_front_facing_surfaces_bit,_collision_test_structure_bit,_collision_test_objects_bit,_collision_test_objects_scenery_bit};
typedef float real;
typedef struct{float x,y,z;}real_point3d;
typedef struct{float i,j,k;}real_vector3d;
struct collision_result{float t;};
struct camera_command{real_point3d position;real_vector3d offset,forward;float depth,field_of_view;};
struct unit_datum{struct{long parent_object_index;unsigned damage_flags;}object;};
static struct {struct{real_vector3d forward;}camera;}render;
static struct unit_datum unit={{NONE,0}};
static int enabled=1,network,cinema,scripted,zoom=NONE,hit,queries;
static int *director_camera_scripted=&scripted;
static long unit_index=42;
static unsigned long revision=1;
static float distance=1.1f,shoulder=.25f,height=.1f;
int config_boolean(const char*n){(void)n;return enabled;}
double config_real(const char*n){return strstr(n,"distance")?distance:strstr(n,"shoulder")?shoulder:height;}
unsigned long config_changes(void){return revision;}
int game_engine_running(void){return network;}
int cinematic_in_progress(void){return cinema;}
long player_control_get_unit_index(short p){(void)p;return unit_index;}
struct unit_datum *unit_try_and_get(long i){return i==42?&unit:NULL;}
short player_control_get_zoom_level(short p){(void)p;return zoom;}
float player_control_get_field_of_view(short p){(void)p;return .25f;}
static int interpolated;
void render_interpolation_object_point(long i,real_point3d*p){assert(i==42);p->x+=.25f;interpolated++;}
void unit_get_camera_position(long i,real_point3d*p){assert(i==42);*p=(real_point3d){1,2,3};}
int collision_test_vector(unsigned flags,const real_point3d*p,const real_vector3d*v,long ignore,struct collision_result*r){(void)flags;(void)p;(void)v;assert(ignore==42);queries++;r->t=.5f;return hit;}
'''
main=r'''
int main(void){struct camera_command c={0};c.forward.i=1;
assert(third_person_camera_available(0));assert(!third_person_camera_available(-1));
third_person_camera_adjust(0,&c);assert(c.depth==1.1f&&c.offset.j==.25f&&c.offset.k==.1f&&queries==1);
hit=1;third_person_camera_adjust(0,&c);assert(fabsf(c.offset.j-.1125f)<.00001f);
zoom=0;third_person_camera_adjust(0,&c);assert(c.depth==0&&c.offset.j==0&&c.offset.k==0&&c.field_of_view==.25f&&c.position.z==3&&queries==2);
zoom=NONE;unit.object.parent_object_index=100;c.depth=7;third_person_camera_adjust(0,&c);assert(c.depth==7);unit.object.parent_object_index=NONE;
unit.object.damage_flags=1;assert(!third_person_camera_available(0));unit.object.damage_flags=0;
network=1;assert(!third_person_camera_available(0));network=0;
cinema=1;assert(!third_person_camera_available(0));cinema=0;
scripted=1;assert(!third_person_camera_available(0));scripted=0;
enabled=0;assert(!third_person_camera_available(0));enabled=1;
distance=NAN;shoulder=NAN;height=NAN;revision++;hit=0;
third_person_camera_adjust(0,&c);assert(c.depth==1.1f&&c.offset.j==.25f&&c.offset.k==.1f);
real_point3d origin;real_vector3d dir;render.camera.forward.i=1;
assert(third_person_camera_aim_ray(0,&origin,&dir));assert(origin.x==1.25f&&dir.i==1&&interpolated==1);
return 0;}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(fixture+s+main)
 subprocess.run(['cc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Shoulder rig, collision clamp, eye-scope transition and context guards passed')
