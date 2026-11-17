from pathlib import Path
import argparse,shlex,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--cc',default='cc');p.add_argument('--runner',default='');args=p.parse_args()
code=r'''
#include <assert.h>
#include <stdio.h>
#include "reticle_stability.h"
int main(void){struct reticle_stability s={0};float x=10,y=20,rawx,rawy,energy=0;
reticle_stability_update(&s,1,1,2,1,&x,&y);assert(x==10&&y==20);
for(int i=1;i<200;i++){x=10+(i&1?.3f:-.3f);y=20;rawx=x;rawy=y;reticle_stability_update(&s,1+i*16,1,2,1,&x,&y);energy+=(x-10)*(x-10);assert((x-rawx)*(x-rawx)+(y-rawy)*(y-rawy)<=1.00001f);}
assert(energy<200*.09f*.3f);
x=30;y=40;reticle_stability_update(&s,4000,1,2,1,&x,&y);assert(x==30&&y==40);
x=30.5f;y=40.5f;reticle_stability_update(&s,4016,9,2,1,&x,&y);assert(x==30.5f&&y==40.5f);
x=31;y=41;reticle_stability_update(&s,4032,9,3,1,&x,&y);assert(x==31&&y==41);
x=31.5f;y=41.5f;reticle_stability_update(&s,4048,9,3,2,&x,&y);assert(x==31.5f&&y==41.5f);
x=31.7f;y=41.7f;reticle_stability_update(&s,5000,9,3,2,&x,&y);assert(x==31.7f&&y==41.7f);
/* Repeated clock stamps still obey the display-error bound. */
x+=2.0f;rawx=x;reticle_stability_update(&s,5000,9,3,2,&x,&y);assert(fabsf(rawx-x)<=1.00001f);
puts("Reticle jitter attenuation, pixel bound, snaps and identity resets passed");return 0;}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code)
 subprocess.run(shlex.split(args.cc)+['-std=c99','-O2','-Wall','-Wextra','-Werror','-I'+str(root/'port/linux/game'),str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run(shlex.split(args.runner)+[str(p/'test')],check=True)
