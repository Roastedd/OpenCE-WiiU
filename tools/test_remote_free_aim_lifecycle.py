"""Exercise free-aim eligibility, recentering and movement with production functions."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
source=(root/"port/linux/game/view_fov.c").read_text()
source=source[source.index("/* ---------- Wii Remote free aim */"):]
a=source.index("void remote_free_aim_camera(")
b=source.index("void remote_free_aim_movement(")
source=source[:a]+source[b:]
source=source[:source.index("int remote_free_aim_reticle_target(")]
source=source.replace("static real_vector3d remote_aim_forward, remote_aim_up;", "")
fixture=r"""
#include <assert.h>
#include <math.h>
#include <stddef.h>
#define NONE -1
#define PIN(v,a,b) ((v)<(a)?(a):(v)>(b)?(b):(v))
#define _pi 3.14159265358979323846f
#define _director_perspective_first_person 1
#define _director_perspective_third_person 2
#define _object_dead_bit 0
#define TEST_FLAG(word,bit) ((word)&(1u<<(bit)))
typedef struct { float i,j,k; } real_vector3d;
typedef struct { float yaw,pitch; } real_euler_angles2d;
static real_euler_angles2d live_aim;
real_euler_angles2d const *player_control_get_facing_angles(short p) {(void)p;return &live_aim;}
float player_control_get_field_of_view(short p) {(void)p;return 1.2f;}
float render_camera_get_adjusted_field_of_view_tangent(float fov) {return tanf(fov*0.5f)*0.85f;}
float unit_get_zoom_magnification(long unit_index, short level) {(void)unit_index;(void)level;return 4.0f;}
long config_integer(const char *name) {(void)name;return 5;}
struct unit_datum { struct { long parent_object_index; unsigned damage_flags; } object; };
static struct unit_datum unit={{NONE,0}};
static int from_view,free_camera,gunner_seat;
long gunner_view_weapon(short p,long*v){(void)p;if(v)*v=42;return gunner_seat?9:NONE;}
int director_port_free_camera(short p){(void)p;return free_camera;}
unsigned char tick_overlap_reading_published(void) { return from_view; }
unsigned long config_changes(void) { return 1; }
static int initialized=1, enabled=1, players=1, network=0, cinema=0, perspective=1, inhibited=0, zoom=NONE;
static long unit_id=5;
static int scripted=0;
static int *director_camera_scripted=&scripted;
int game_time_initialized(void) { return initialized; }
int config_boolean(const char *name) { (void)name; return enabled; }
int local_player_count(void) { return players; }
int main_smooth_defaults_net_game(void) { return network; }
int cinematic_in_progress(void) { return cinema; }
int director_get_perspective(short index) { (void)index; return perspective; }
int director_inhibited_facing(short index) { (void)index; return inhibited; }
int player_control_get_zoom_level(short index) { (void)index; return zoom; }
long player_control_get_unit_index(short index) { (void)index; return unit_id; }
struct unit_datum *unit_try_and_get(long index) { return index==unit_id ? &unit:NULL; }
const char *config_string(const char *name) {(void)name;return "balanced";}
double config_real(const char *name) { return strstr(name,"box") ? 12.0:90.0; }
"""
main=r"""
int main(void) {
 float yaw=0.05f,pitch=0,forward=1,left=0;
 assert(remote_free_aim_select(1));
 remote_free_aim_input(0,&yaw,&pitch,0.02f);
 assert(fabsf(remote_aim.yaw-0.05f)<0.00001f);
 remote_free_aim_movement(0,&forward,&left);
 assert(left<0 && fabsf(forward*forward+left*left-1)<0.00001f);
 remote_free_aim_recentre(&yaw,&pitch);
 assert(fabsf(yaw+0.05f)<0.00001f);
 remote_free_aim_input(0,&yaw,&pitch,0.02f);
 assert(remote_aim.yaw==0 && fabsf(yaw+0.05f)<0.00001f);
 assert(!remote_free_aim_active(1));
 assert(remote_free_aim_select(2));
 remote_free_aim_pointer_sample(0.1f,0.1f,1);
 yaw=pitch=0;
 remote_free_aim_input(0,&yaw,&pitch,0.02f);
 assert(yaw<0 && pitch<0 && remote_aim_source==2);
 remote_aim_clutch(1);yaw=.3f;pitch=.2f;remote_free_aim_input(0,&yaw,&pitch,.02f);assert(yaw==0 && pitch==0);
 remote_aim_clutch(0);
 float kept=remote_aim.yaw;
 remote_free_aim_pointer_sample(0,0,0);
 remote_free_aim_input(0,&yaw,&pitch,0.02f);
 assert(yaw==0 && pitch==0 && remote_aim.yaw==kept);
 assert(remote_free_aim_select(1) && remote_aim.yaw==0);
 players=2; assert(!remote_free_aim_select(1)); players=1;
 network=1; assert(!remote_free_aim_select(1)); network=0;
 cinema=1; assert(!remote_free_aim_select(1)); cinema=0;
 scripted=1; assert(!remote_free_aim_select(1)); scripted=0;
 perspective=2;assert(remote_free_aim_select(2) && remote_vehicle_selected==3);
 remote_free_aim_pointer_sample(.8f,0,1);yaw=pitch=0;remote_free_aim_input(0,&yaw,&pitch,.02f);assert(yaw<0);
 perspective=1;assert(remote_free_aim_select(2) && remote_vehicle_selected==0);
 zoom=0; assert(!remote_free_aim_select(1));
 assert(remote_free_aim_select(2) && remote_vehicle_selected==2);
 remote_free_aim_pointer_sample(0.8f,-0.7f,1);yaw=pitch=0;
 remote_free_aim_input(0,&yaw,&pitch,0.02f);assert(yaw<0 && pitch>0 && fabsf(yaw)<0.01f);
 remote_free_aim_publish();remote_free_aim_sync();assert(!remote_free_aim_active(0));
 zoom=NONE;assert(remote_free_aim_select(2) && remote_vehicle_selected==0 && remote_ir_filter.count==0);

 unit.object.parent_object_index=42; assert(!remote_free_aim_select(1));
 assert(remote_free_aim_select(2) && remote_vehicle_selected && !remote_aim_selected);
 remote_free_aim_pointer_sample(0.8f,-0.7f,1);yaw=pitch=0;
 remote_free_aim_input(0,&yaw,&pitch,0.02f);assert(yaw<0 && pitch>0);
 remote_free_aim_publish();remote_free_aim_sync();assert(!remote_free_aim_active(0));
 forward=1;left=0;remote_free_aim_movement(0,&forward,&left);assert(forward==1 && left==0);
 remote_free_aim_pointer_sample(0,0,0);remote_free_aim_input(0,&yaw,&pitch,0.02f);assert(yaw==0 && pitch==0);
 gunner_seat=1;assert(remote_free_aim_select(2) && remote_vehicle_selected==5);gunner_seat=0;
 unit.object.parent_object_index=43;assert(remote_free_aim_select(2));assert(remote_ir_filter.count==0);
 unit.object.parent_object_index=NONE;assert(remote_free_aim_select(2) && !remote_vehicle_selected);
 assert(remote_ir_filter.count==0 && remote_aim.yaw==0);

 unit.object.damage_flags=1; assert(!remote_free_aim_select(1)); unit.object.damage_flags=0;
 assert(remote_free_aim_select(1));
 remote_aim.yaw=0.1f; unit_id++;
 assert(remote_free_aim_select(1) && remote_aim.yaw==0);
 remote_aim.yaw=0.1f;
 assert(!remote_free_aim_select(0) && remote_aim.yaw==0);
 assert(remote_free_aim_select(1));
 remote_aim.yaw=0.1f;
 remote_free_aim_publish(); remote_free_aim_sync(); from_view=1;
 assert(remote_free_aim_active(0));
 /* The worker can update/reset input while rendering keeps a paired capture. */
 remote_aim.yaw=0.2f; remote_free_aim_publish();
 assert(fabsf(remote_free_aim_snapshot()->offset.yaw-0.1f)<0.00001f);
 remote_free_aim_select(0);
 assert(remote_free_aim_active(0));
 remote_free_aim_publish(); remote_free_aim_sync();
 assert(!remote_free_aim_active(0));
 assert(remote_free_aim_select(1));
 zoom=0; remote_free_aim_publish(); remote_free_aim_sync();
 assert(!remote_free_aim_active(0));
 zoom=NONE;
 perspective=3;free_camera=1;assert(remote_free_aim_select(2)&&remote_vehicle_selected==4);
 remote_free_aim_pointer_sample(.8f,0,1);yaw=pitch=0;remote_free_aim_input(0,&yaw,&pitch,.02f);assert(yaw<0);
 free_camera=0;assert(!remote_free_aim_select(2));perspective=1;
 initialized=0; assert(!remote_free_aim_select(1));
 return 0;
}
"""
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)
    (path/"test.c").write_text('#include <string.h>\n'+fixture+source+main)
    subprocess.run(["cc","-std=c99","-Wall","-Wextra","-DHALO_WIIU","-I",str(root/"port/linux/game"),
                    str(path/"test.c"),"-lm","-o",str(path/"test")],check=True)
    subprocess.run([str(path/"test")],check=True)
print("Free-aim lifecycle checks passed")
