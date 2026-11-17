"""Verify free-aim camera/shot invariants with the production input math."""
from pathlib import Path
import argparse
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
fixture = r"""
#include <assert.h>
#include <math.h>
#include "remote_free_aim_math.h"
static int close_to(float a,float b) { return fabsf(a-b)<0.00001f; }
int main(void) {
 struct remote_free_aim_state state={0,0};
 float aim=0, yaw=0.02f, pitch=0.01f;
 remote_free_aim_step(&state,&yaw,&pitch,0.2f,0.2f*2/3,1.0f,0.02f);
 aim+=yaw;
 assert(close_to(aim-state.yaw,0));
 assert(close_to(state.pitch,0.01f));
 /* A steady hand inside the box leaves both aim and camera stationary. */
 yaw=pitch=0;
 remote_free_aim_step(&state,&yaw,&pitch,0.2f,0.2f*2/3,1.0f,0.02f);
 assert(yaw==0 && pitch==0);
 /* At the edge, a steady hand turns the camera while retaining the offset. */
 state.yaw=0.2f; yaw=pitch=0;
 remote_free_aim_step(&state,&yaw,&pitch,0.2f,0.2f*2/3,1.0f,0.02f);
 assert(close_to(yaw,0.02f) && close_to(state.yaw,0.2f));
 /* Symmetry on the other edge. */
 state.yaw=-0.2f; yaw=pitch=0;
 remote_free_aim_step(&state,&yaw,&pitch,0.2f,0.2f*2/3,1.0f,0.02f);
 assert(close_to(yaw,-0.02f));
 /* The response at the inner edge is continuous. */
 state.yaw=0.11f; yaw=pitch=0;
 remote_free_aim_step(&state,&yaw,&pitch,0.2f,0.2f*2/3,1.0f,0.02f);
 assert(fabsf(yaw)<0.000001f);
 /* Fast motion remains bounded and excess moves the camera. */
 state.yaw=state.pitch=0; yaw=0.7f; pitch=-0.5f;
 remote_free_aim_step(&state,&yaw,&pitch,0.2f,0.2f*2/3,1.0f,0.02f);
 assert(close_to(state.yaw,0.2f) && close_to(state.pitch,-0.2f*2/3));
 assert(yaw>0.7f && pitch< -0.5f);
 /* Edge speed uses elapsed time, independently of the sample rate. */
 float movement[2]={0,0};
 for(int rate=0;rate<2;rate++) {
  int frames=rate ? 120:30;
  state.yaw=0.2f; state.pitch=0;
  for(int i=0;i<frames;i++) {
   yaw=pitch=0;
   remote_free_aim_step(&state,&yaw,&pitch,0.2f,0.2f*2/3,1.0f,1.0f/frames);
   movement[rate]+=yaw;
  }
 }
 assert(close_to(movement[0],movement[1]));
 /* A long stall cannot produce an unbounded edge turn. */
 yaw=pitch=0;
 remote_free_aim_step(&state,&yaw,&pitch,0.2f,0.2f*2/3,1.0f,20.0f);
 assert(close_to(yaw,0.1f));
 yaw=NAN; pitch=0;
 remote_free_aim_step(&state,&yaw,&pitch,0.2f,0.2f*2/3,1.0f,0.02f);
 assert(yaw==0 && pitch==0);
 assert(remote_free_aim_finite(FLT_MAX));
 assert(remote_free_aim_finite(-FLT_MAX));
 assert(!remote_free_aim_finite(INFINITY));
 assert(!remote_free_aim_finite(-INFINITY));
 assert(!remote_free_aim_finite(NAN));
 return 0;
}
"""
parser=argparse.ArgumentParser()
parser.add_argument("--cc",default="cc")
parser.add_argument("--runner",default="")
args=parser.parse_args()
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)
    (path/"test.c").write_text(fixture)
    subprocess.run(shlex.split(args.cc)+["-std=c99","-Wall","-Wextra","-Werror","-I",str(root/"port/linux/game"),
                    str(path/"test.c"),"-lm","-o",str(path/"test")],check=True)
    subprocess.run(shlex.split(args.runner)+[str(path/"test")],check=True)
print("Free-aim math checks passed")
