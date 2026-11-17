"""Test IR free aim against screen-space geometry, filtering and turn timing."""
from pathlib import Path
import argparse,shlex,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
code=r"""
#include <assert.h>
#include <math.h>
#include "remote_ir_aim_math.h"
static int near(float a,float b) {return fabsf(a-b)<0.0001f;}
int main(void) {
 float vy,vp,vy30,vp30;
 remote_ir_vehicle_step(0.1f,-0.1f,1.3f,1.0f/60,&vy,&vp);assert(vy==0 && vp==0);
 remote_ir_vehicle_step(1,1,1.3f,1.0f/60,&vy,&vp);assert(vy<0 && vp<0 && fabsf(vp)<fabsf(vy));
 remote_ir_vehicle_step(1,1,1.3f,1.0f/30,&vy30,&vp30);assert(near(vy30,2*vy) && near(vp30,2*vp));
 remote_ir_vehicle_step(-1,-1,1.3f,1.0f/60,&vy30,&vp30);assert(near(vy30,-vy) && near(vp30,-vp));
 remote_ir_vehicle_step(NAN,0,1.3f,0.016f,&vy,&vp);assert(vy==0 && vp==0);
 remote_ir_vehicle_step(1,1,1.3f,10,&vy,&vp);assert(fabsf(vy)<=0.131f);
 remote_ir_scope_step(1,-1,1.3f,0.75f,2,1.0f/60,&vy,&vp);
 remote_ir_scope_step(1,-1,1.3f,0.75f,8,1.0f/60,&vy30,&vp30);
 assert(near(vy30*4,vy) && near(vp30*4,vp) && vy<0 && vp>0);
 remote_ir_scope_step(0,0,1.3f,0.75f,8,0.02f,&vy,&vp);assert(vy==0 && vp==0);
 remote_ir_scope_step(1,1,1.3f,0.75f,NAN,0.02f,&vy,&vp);assert(vy==0 && vp==0);
 struct remote_ir_filter filter={0};
 float x=0,y=0;
 remote_ir_filter_sample(&filter,&x,&y,5,0.5f);
 x=y=1;remote_ir_filter_sample(&filter,&x,&y,5,0.5f);
 assert(near(x,2.0f/3) && near(y,x));
 x=y=0;remote_ir_filter_sample(&filter,&x,&y,5,0.5f);
 assert(near(x,2.0f/7));
 for(int i=0;i<30;i++){x=y=1;remote_ir_filter_sample(&filter,&x,&y,5,0.5f);}
 assert(near(x,1) && near(y,1));
 filter.next=filter.count=0;x=-0.8f;y=0.6f;
 remote_ir_filter_sample(&filter,&x,&y,5,0.5f);
 assert(near(x,-0.8f) && near(y,0.6f));
 const float headings[]={-3.0f,-1.2f,0,1.4f,5.8f};
 for(int h=0;h<5;h++) for(int p=-1;p<=1;p++) for(int ix=-1;ix<=1;ix++) for(int iy=-1;iy<=1;iy++) {
  struct remote_free_aim_state offset={0};
  float yaw,pitch;
  float cy=headings[h],cp=p*0.4f,tx=1,ty=0.5625f;
  x=ix*0.5f;y=iy*0.5f;
  remote_ir_aim_step(&offset,x,y,cy,cp,tx,ty,0.2f,0.14f,0,0.016f,&yaw,&pitch);
  float ay=cy+yaw,ap=cp+pitch;
  assert(near(remote_ir_angle_difference(ay-offset.yaw-cy),0));
  assert(near(ap-offset.pitch,cp));
  float dx=cosf(ap)*cosf(ay),dy=cosf(ap)*sinf(ay),dz=sinf(ap);
  float forward=dx*cosf(cp)*cosf(cy)+dy*cosf(cp)*sinf(cy)+dz*sinf(cp);
  float left=-dx*sinf(cy)+dy*cosf(cy);
  float up=-dx*sinf(cp)*cosf(cy)-dy*sinf(cp)*sinf(cy)+dz*cosf(cp);
  assert(near(-left/forward/tx,x));
  assert(near(-up/forward/ty,y));
 }
 float rate[2]={0};
 for(int k=0;k<2;k++) {
  int frames=k ? 120:30;
  for(int i=0;i<frames;i++)rate[k]+=remote_ir_turn(0.8f,0.2f,2.0f,1.0f/frames);
 }
 assert(near(rate[0],rate[1]));
 assert(remote_ir_turn(0.1f,0.2f,1,0.016f)==0);
 assert(remote_ir_turn(-0.8f,0.2f,1,0.016f)>0);
 struct remote_free_aim_state offset={0};
 float yaw,pitch;
 remote_ir_aim_step(&offset,0,-1,1.0f,1.49f,1,0.56f,0.2f,0.14f,0,0.016f,&yaw,&pitch);
 assert(fabsf(yaw)<0.0001f && 1.49f+pitch<=3.14159265f*0.475f+0.00001f);
 remote_ir_aim_step(&offset,NAN,0,0,0,1,0.56f,0.2f,0.14f,1,0.016f,&yaw,&pitch);
 assert(yaw==0 && pitch==0);
 return 0;
}
"""
parser=argparse.ArgumentParser();parser.add_argument('--cc',default='cc');parser.add_argument('--runner',default='')
a=parser.parse_args()
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'test.c').write_text(code)
 subprocess.run(shlex.split(a.cc)+['-std=c99','-Wall','-Wextra','-Werror','-I',str(root/'port/linux/game'),
                 str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run(shlex.split(a.runner)+[str(p/'test')],check=True)
print('IR projection, filter, pitch limits and frame-rate checks passed')
