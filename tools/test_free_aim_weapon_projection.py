"""Verify the production weapon pose points at the same screen position across FOVs."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
source=(root/"port/linux/game/view_fov.c").read_text()
start=source.index("void remote_free_aim_weapon_camera(")
end=source.index("\nvoid remote_free_aim_movement(",start)
function=source[start:end]
fixture=r"""
#include <math.h>
#include <assert.h>
#include <string.h>
typedef float real;
typedef struct { float i,j,k; } real_vector3d;
struct render_camera { real_vector3d forward,up; float vertical_field_of_view; };
static int remote_aim_camera_valid=1,active=1;
static real_vector3d remote_aim_forward;
static float model_angle,authored_angle;
static int remote_free_aim_active(short index) { return index==0 && active; }
static float viewmodel_vertical(void) { return model_angle; }
static float render_fov_authored_vertical(short index) { (void)index;return authored_angle; }
static float dot_product3d(const real_vector3d *a,const real_vector3d *b) { return a->i*b->i+a->j*b->j+a->k*b->k; }
static void cross_product3d(const real_vector3d *a,const real_vector3d *b,real_vector3d *out) {
 *out=(real_vector3d){a->j*b->k-a->k*b->j,a->k*b->i-a->i*b->k,a->i*b->j-a->j*b->i};
}
static void normalize3d(real_vector3d *v) {
 float n=sqrtf(dot_product3d(v,v));assert(n>0);v->i/=n;v->j/=n;v->k/=n;
}
"""
main=r"""
int main(void) {
 const float angles[]={0.5f,0.9f,1.5f,2.0f};
 for(int w=0;w<4;w++) for(int m=0;m<4;m++)
 for(int x=-2;x<=2;x++) for(int y=-2;y<=2;y++) {
  struct render_camera camera={{1,0,0},{0,0,1},angles[w]};
  remote_aim_forward=(real_vector3d){1,x*0.1f,y*0.1f};
  normalize3d(&remote_aim_forward);
  model_angle=angles[m];
  remote_free_aim_weapon_camera(0,&camera);
  float target_x=remote_aim_forward.j/remote_aim_forward.i/tanf(angles[w]*0.5f);
  float target_y=remote_aim_forward.k/remote_aim_forward.i/tanf(angles[w]*0.5f);
  assert(fabsf(camera.forward.j/camera.forward.i/tanf(model_angle*0.5f)-target_x)<0.00001f);
  assert(fabsf(camera.forward.k/camera.forward.i/tanf(model_angle*0.5f)-target_y)<0.00001f);
  assert(fabsf(dot_product3d(&camera.forward,&camera.up))<0.00001f);
 }
 model_angle=authored_angle=0;
 struct render_camera camera={{1,0,0},{0,0,1},1.0f};
 remote_aim_forward=(real_vector3d){1,0.1f,-0.1f};normalize3d(&remote_aim_forward);
 remote_free_aim_weapon_camera(0,&camera);
 assert(fabsf(camera.forward.j-remote_aim_forward.j)<0.00001f);
 active=0;
 struct render_camera saved=camera;
 remote_free_aim_weapon_camera(0,&camera);
 assert(memcmp(&saved,&camera,sizeof(camera))==0);
 return 0;
}
"""
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)
    (path/"test.c").write_text(fixture+function+main)
    subprocess.run(["cc","-std=c99","-Wall","-Wextra","-Werror","-DHALO_WIIU",
                    str(path/"test.c"),"-lm","-o",str(path/"test")],check=True)
    subprocess.run([str(path/"test")],check=True)
print("Weapon/reticle projection checks passed across FOVs")
