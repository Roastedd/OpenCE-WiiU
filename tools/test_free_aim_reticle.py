"""Regression checks for a stable, fractional free-aim cursor."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
source=(root/"port/linux/game/passenger_camera.c").read_text()
source=source[source.index("boolean passenger_camera_reticle_begin("):]
target=(root/"port/linux/game/view_fov.c").read_text()
target=target[target.index("int remote_free_aim_reticle_target("):]
fixture=r"""
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include "reticle_stability.h"
#define MAXIMUM_LOCAL_PLAYERS 4
static struct reticle_stability reticle_states[MAXIMUM_LOCAL_PLAYERS];
unsigned long system_milliseconds(void){return 100;}
int config_boolean(const char*n){(void)n;return 1;}
typedef int boolean;
typedef float real;
typedef struct { float x,y,z; } real_point3d;
typedef struct { float i,j,k; } real_vector3d;
typedef struct { float x,y; } real_point2d;
typedef struct { short x,y; } point2d;
#define TRUE 1
#define FALSE 0
#define NONE -1
#define _director_perspective_third_person 2
#define _collision_test_for_projectiles_flags 0
static int active=1,passenger=0,ray_calls,collision_calls;
static boolean reticle_active,reticle_subpixel;
static real_point2d reticle_offset;
static int remote_aim_camera_valid=1;
static real_vector3d remote_aim_forward={0.0025f,0.005f,1};
static struct { struct { real_point3d position; struct { int x0,y0,x1,y1; } window_bounds; } camera; int frustum; } render;
struct collision_result { real_point3d point; };
int remote_free_aim_active(short p) { (void)p; return active; }
static int shoulder;
long gunner_view_weapon(short p,long*v){(void)p;if(v)*v=NONE;return NONE;}
int third_person_camera_aim_ray(short p,real_point3d*o,real_vector3d*d){(void)p;*o=(real_point3d){0,0,0};*d=(real_vector3d){1,0,0};return shoulder;}
int third_person_camera_available(short p){(void)p;return shoulder;}
int passenger_camera_enabled(short p) { (void)p; return passenger; }
int director_get_perspective(short p) { (void)p; return 2; }
long player_control_get_unit_index(short p) { (void)p; return 0; }
int weapon_get_projectile_ray(long w,int t,real_point3d *p,real_vector3d *d) {
 (void)w;(void)t;ray_calls++;*p=(real_point3d){99,99,99};*d=(real_vector3d){1,0,0};return 1;
}
void scale_vector3d(const real_vector3d *v,float s,real_vector3d *out) {out->i=v->i*s;out->j=v->j*s;out->k=v->k*s;}
void point_from_line3d(const real_point3d *p,const real_vector3d *d,float s,real_point3d *out) {
 out->x=p->x+d->i*s;out->y=p->y+d->j*s;out->z=p->z+d->k*s;
}
int collision_test_vector(int flags,const real_point3d *p,const real_vector3d *d,long u,struct collision_result *c) {
 (void)flags;(void)p;(void)d;(void)u;collision_calls++;c->point=(real_point3d){10,20,128};return 1;
}
int render_camera_world_to_screen(const void *c,const void *f,const real_point3d *p,real_point2d *s) {
 (void)c;(void)f;s->x=(p->x-render.camera.position.x)/128*100;s->y=(p->y-render.camera.position.y)/128*100;return 1;
}
"""
main=r"""
int main(void) {
 float x=100,y=100;point2d integer={100,100};
 assert(passenger_camera_reticle_begin(0,7));
 assert(ray_calls==0 && collision_calls==0);
 passenger_camera_reticle_offset(&integer);
 assert(integer.x==100 && integer.y==100);
 passenger_camera_reticle_subpixel(&x,&y);
 assert(fabsf(x-100.25f)<0.00001f && fabsf(y-100.5f)<0.00001f);
 /* Camera translation/walking cannot move a fixed aiming cursor. */
 render.camera.position=(real_point3d){20,-10,8};
 assert(passenger_camera_reticle_begin(0,7));
 x=y=100; passenger_camera_reticle_subpixel(&x,&y);
 assert(fabsf(x-100.25f)<0.00001f && fabsf(y-100.5f)<0.00001f);
 remote_aim_camera_valid=0;
 assert(!passenger_camera_reticle_begin(0,7));
 x=y=100;passenger_camera_reticle_subpixel(&x,&y);assert(x==100 && y==100);
 active=0;passenger=1;
 assert(passenger_camera_reticle_begin(0,7));
 assert(ray_calls==1 && collision_calls==1);
 x=y=100;passenger_camera_reticle_subpixel(&x,&y);assert(x==100 && y==100);
 passenger_camera_reticle_end();
 integer=(point2d){100,100};passenger_camera_reticle_offset(&integer);assert(integer.x==100);
 passenger=0;shoulder=1;
 assert(passenger_camera_reticle_begin(0,7));assert(ray_calls==1 && collision_calls==2);
 x=y=100;passenger_camera_reticle_subpixel(&x,&y);assert(x!=100 || y!=100);
 return 0;
}
"""
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)
    (path/"test.c").write_text(fixture+target+source+main)
    subprocess.run(["cc","-std=c99","-Wall","-Wextra","-Werror","-DHALO_WIIU",
                    "-I"+str(root/"port/linux/game"),str(path/"test.c"),"-lm","-o",str(path/"test")],check=True)
    subprocess.run([str(path/"test")],check=True)
print("Stable free-aim reticle and fractional placement checks passed")
