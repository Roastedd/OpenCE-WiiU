from pathlib import Path
import argparse,shlex,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--cc',default='cc');p.add_argument('--runner',default='');a=p.parse_args()
code=r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "remote_control_tuning.h"
#include "model_lod_stability.h"
static int near(float a,float b){return fabsf(a-b)<0.00001f;}
int main(void){
 assert(near(remote_turn_outer(.75f,.15f,.75f,1,1.0f/60),-1.0f/60));
 assert(near(remote_turn_outer(.9f,.15f,.75f,1,1.0f/60),-1.0f/60));
 assert(remote_turn_outer(.14f,.15f,.75f,1,.02f)==0);
 assert(near(remote_turn_outer(-.75f,.15f,.75f,1,.02f),.02f));
 assert(near(remote_turn_outer(.75f,.15f,.75f,1,1.0f/30),2*remote_turn_outer(.75f,.15f,.75f,1,1.0f/60)));
 assert(near(remote_calibrate(.2f,.2f,1),0));assert(near(remote_calibrate(.7f,.2f,.5f),1));
 assert(near(remote_calibrate(-2,0,1),-1));
 assert(near(remote_scope_turn(.2f,.75f,1,10,.25f,.02f),remote_turn_outer(.2f,.08f,.75f,.1f,.02f)));
 assert(near(remote_scope_turn(.75f,.75f,1,10,.25f,.02f),-.005f));
 assert(near(remote_scope_turn(.75f,.75f,1,10,0,.02f),-.002f));
 assert(near(remote_scope_turn(.75f,.75f,1,2,.25f,.02f),-.01f));
 assert(remote_scope_turn(1,.75f,1,NAN,.25f,.02f)==0);
 struct remote_adaptive_filter f={0};float x=0,y=0,raw=0,filtered=0;
 remote_adaptive_sample(&f,&x,&y,1.0f/60);
 for(int i=0;i<120;i++){x=(i&1)?.01f:-.01f;y=0;raw+=x*x;remote_adaptive_sample(&f,&x,&y,1.0f/60);filtered+=x*x;}
 assert(filtered<raw*.5f);
 x=1;y=-1;remote_adaptive_sample(&f,&x,&y,1.0f/60);assert(x>0 && x<1 && y<0);
 for(int i=0;i<10;i++){x=1;y=-1;remote_adaptive_sample(&f,&x,&y,1.0f/60);}
 assert(x>.95f && y<-.95f);
 x=-.4f;y=.7f;remote_adaptive_sample(&f,&x,&y,.5f);assert(near(x,-.4f)&&near(y,.7f));
 struct model_lod_memory m={0};float cutoffs[5]={0,10,20,40,80};
 assert(model_lod_stable(&m,1,2,10,0,4,81,cutoffs)==4);
 assert(model_lod_stable(&m,1,2,11,0,3,79,cutoffs)==4);
 assert(model_lod_stable(&m,1,2,12,0,3,63,cutoffs)==3);
 assert(model_lod_stable(&m,1,2,13,0,4,81,cutoffs)==4);
 assert(model_lod_stable(&m,1,2,14,1,2,25,cutoffs)==2);
 assert(model_lod_stable(&m,2,2,15,1,0,1,cutoffs)==0);
 assert(model_lod_stable(&m,2,2,20,1,4,90,cutoffs)==4);
 assert(model_lod_stable(&m,2,2,25,1,3,79,cutoffs)==3);
 printf("Tuning, adaptive-filter response and LOD hysteresis checks passed\n");return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code)
 subprocess.run(shlex.split(a.cc)+['-std=c99','-O2','-ffp-contract=off','-Wall','-Wextra','-Werror','-I'+str(root/'port/linux/game'),str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run(shlex.split(a.runner)+[str(p/'test')],check=True)
