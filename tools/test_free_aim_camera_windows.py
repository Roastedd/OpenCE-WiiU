"""A trailing HUD/console window must preserve the player aim camera."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
source=(root/"port/linux/game/view_fov.c").read_text()
a=source.index("void remote_free_aim_camera(")
b=source.index("void remote_free_aim_weapon_camera(",a)
function=source[a:b]
fixture=r"""
#include <assert.h>
#include <math.h>
typedef float real;
typedef struct {float i,j,k;} real_vector3d;
typedef struct {float yaw,pitch;} real_euler_angles2d;
typedef struct {real_vector3d forward,up;} real_matrix4x3;
struct render_camera {real_vector3d forward,up;};
#define _pi 3.14159265f
#define PIN(v,a,b) ((v)<(a)?(a):(v)>(b)?(b):(v))
static int remote_aim_camera_valid,active=1;
static real_vector3d remote_aim_forward,remote_aim_up;
static struct snapshot { struct {float yaw,pitch;} offset; } snapshot;
static const struct snapshot *remote_free_aim_snapshot(void) {return &snapshot;}
static float dot_product3d(const real_vector3d *a,const real_vector3d *b) {return a->i*b->i+a->j*b->j+a->k*b->k;}
static void normalize3d(real_vector3d *v) {float n=sqrtf(dot_product3d(v,v));v->i/=n;v->j/=n;v->k/=n;}
static void euler_angles2d_from_vector3d(real_euler_angles2d *a,const real_vector3d *v) {a->yaw=atan2f(v->j,v->i);a->pitch=atan2f(v->k,sqrtf(v->i*v->i+v->j*v->j));}
static void vector3d_from_euler_angles2d(real_vector3d *v,const real_euler_angles2d *a) {v->i=cosf(a->pitch)*cosf(a->yaw);v->j=cosf(a->pitch)*sinf(a->yaw);v->k=sinf(a->pitch);}

static int remote_free_aim_active(short index) {return index==0 && active;}
"""
main=r"""
int main(void) {
 struct render_camera player={{1,0,0},{0,0,1}},overlay={{0,1,0},{0,0,1}};
 remote_free_aim_camera(0,&player);
 assert(remote_aim_camera_valid && remote_aim_forward.i==1);
 remote_free_aim_camera(-1,&overlay);
 assert(remote_aim_camera_valid && remote_aim_forward.i==1);
 remote_free_aim_camera(1,&overlay);
 assert(remote_aim_camera_valid && remote_aim_forward.i==1);
 active=0;remote_free_aim_camera(0,&player);
 assert(!remote_aim_camera_valid);
 return 0;
}
"""
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)
    (path/"test.c").write_text(fixture+function+main)
    subprocess.run(["cc","-std=c99","-Wall","-Wextra","-Werror","-DHALO_WIIU",str(path/"test.c"),"-lm","-o",str(path/"test")],check=True)
    subprocess.run([str(path/"test")],check=True)
print("Player camera survives overlay-window preparation")
