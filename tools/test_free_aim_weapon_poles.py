"""Exercise the production arm-camera basis at vertical aim and invalid projections."""
from pathlib import Path
import argparse,shlex,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--cc',default='cc');parser.add_argument('--runner',default='');parser.add_argument('--sanitize',action='store_true');args=parser.parse_args()
source=(root/'port/linux/game/view_fov.c').read_text();a=source.index('void remote_free_aim_weapon_camera(');b=source.index('\nvoid remote_free_aim_movement(',a)
fixture=r'''
#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
typedef float real;
typedef struct {float i,j,k;} real_vector3d;
struct render_camera {real_vector3d forward,up;float vertical_field_of_view;};
static int remote_aim_camera_valid=1,active=1;
static real_vector3d remote_aim_forward;
static float model_angle,authored_angle;
static int remote_free_aim_active(short p){return p==0&&active;}
static float viewmodel_vertical(void){return model_angle;}
static float render_fov_authored_vertical(short p){(void)p;return authored_angle;}
static float dot_product3d(const real_vector3d*a,const real_vector3d*b){return a->i*b->i+a->j*b->j+a->k*b->k;}
static void cross_product3d(const real_vector3d*a,const real_vector3d*b,real_vector3d*r){*r=(real_vector3d){a->j*b->k-a->k*b->j,a->k*b->i-a->i*b->k,a->i*b->j-a->j*b->i};}
static void normalize3d(real_vector3d*v){float n=sqrtf(dot_product3d(v,v));assert(n>0&&isfinite(n));v->i/=n;v->j/=n;v->k/=n;}
static real_vector3d forward(float yaw,float pitch){return (real_vector3d){cosf(pitch)*cosf(yaw),cosf(pitch)*sinf(yaw),sinf(pitch)};}
static real_vector3d up(float yaw,float pitch){return (real_vector3d){-sinf(pitch)*cosf(yaw),-sinf(pitch)*sinf(yaw),cosf(pitch)};}
static void check_basis(const struct render_camera*c){
 assert(fabsf(dot_product3d(&c->forward,&c->forward)-1)<0.00001f);
 assert(fabsf(dot_product3d(&c->up,&c->up)-1)<0.00001f);
 assert(fabsf(dot_product3d(&c->up,&c->forward))<0.00001f);
}
'''
main=r'''
int main(void){
 const float pi=3.14159265358979323846f;
 unsigned cohorts=0;
 /* At the floor, changing world azimuth 84 degrees changes the physical
    sight ray less than eight degrees. It must not roll the arms 84 degrees. */
 for(int sign=-1;sign<=1;sign+=2) {
  float p=sign*85.0f*pi/180.0f;
  struct render_camera c={forward(0,p),up(0,p),1};
  real_vector3d view_up=c.up,old_up,old_left,aim_up;
  remote_aim_forward=forward(84*pi/180.0f,p);aim_up=up(84*pi/180.0f,p);
  model_angle=.7f;remote_free_aim_weapon_camera(0,&c);check_basis(&c);
  assert(dot_product3d(&view_up,&c.up)>.99f);
  cross_product3d(&aim_up,&c.forward,&old_left);normalize3d(&old_left);
  cross_product3d(&c.forward,&old_left,&old_up);
  assert(dot_product3d(&view_up,&old_up)<.3f);
 }
 const float angles[]={.2f,.5f,1.0f,1.5f,2.2f};
 for(int wi=0;wi<5;wi++)for(int mi=0;mi<5;mi++)
 for(int yi=-4;yi<=4;yi++)for(int pitch=-89;pitch<=89;pitch+=1)
 for(int dx=-1;dx<=1;dx++)for(int dy=-1;dy<=1;dy++) {
  struct render_camera c={forward(yi*.8f,pitch*pi/180.0f),up(yi*.8f,pitch*pi/180.0f),angles[wi]};
  real_vector3d left;cross_product3d(&c.up,&c.forward,&left);
  remote_aim_forward=(real_vector3d){c.forward.i+left.i*dx*.2f+c.up.i*dy*.2f,c.forward.j+left.j*dx*.2f+c.up.j*dy*.2f,c.forward.k+left.k*dx*.2f+c.up.k*dy*.2f};
  normalize3d(&remote_aim_forward);model_angle=angles[mi];
  remote_free_aim_weapon_camera(0,&c);check_basis(&c);cohorts++;
 }
 struct render_camera c={forward(0,-1.5f),up(0,-1.5f),1},saved;
 model_angle=NAN;saved=c;remote_free_aim_weapon_camera(0,&c);assert(!memcmp(&c,&saved,sizeof(c)));
 model_angle=.7f;c.vertical_field_of_view=0;saved=c;remote_free_aim_weapon_camera(0,&c);assert(!memcmp(&c,&saved,sizeof(c)));
 c.vertical_field_of_view=1;c.up=c.forward;saved=c;remote_free_aim_weapon_camera(0,&c);assert(!memcmp(&c,&saved,sizeof(c)));
 c=(struct render_camera){forward(0,-1.5f),up(0,-1.5f),1};remote_aim_forward.i=NAN;saved=c;remote_free_aim_weapon_camera(0,&c);assert(!memcmp(&c,&saved,sizeof(c)));
 printf("Arm basis pole-roll regression and %u FOV/vertical cohorts passed\n",cohorts);return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.c').write_text(fixture+source[a:b]+main)
 flags=['-std=c99','-O2','-ffp-contract=off','-Wall','-Wextra','-Werror','-DHALO_WIIU']
 if args.sanitize:flags+=['-fsanitize=address,undefined']
 subprocess.run(shlex.split(args.cc)+flags+[str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run(shlex.split(args.runner)+[str(p/'test')],check=True)
