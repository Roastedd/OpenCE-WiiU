#!/usr/bin/env python3
"""Compare traversal-local source-box reuse with the unchanged source test."""
from pathlib import Path
import subprocess,tempfile,re
root=Path(__file__).resolve().parents[1]
s=(root/'port/linux/game/structure_culling.c').read_text()
a=s.index('            /* Retain the original fog-clamped source test, once per run of')
b=s.index('\n\t\t\tif (!box)',a)
block=s[a:b]
locals='''        real_rectangle3d const *source_bounds_at = NULL;
        boolean source_visible = FALSE;'''
assert locals in s
fixture=r'''
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#define FALSE 0
#define TRUE 1
typedef int boolean;
typedef struct {float x0,x1;} real_rectangle3d;
struct render_frustum {float edge;};
struct bucket {const real_rectangle3d *source_bounds;real_rectangle3d bounds;};
static int source_calls;
static short render_frustum_cube_visible(const struct render_frustum *f,const real_rectangle3d *b,int flag){assert(flag==FALSE);source_calls++;return b->x0<=f->edge;}
static short structure_culling_box_test(const int *planes,const real_rectangle3d *b){(void)planes;return b->x1>0 ? 2:0;}
static int baseline(const struct render_frustum *f,const struct bucket *b){return b->source_bounds && !render_frustum_cube_visible(f,b->source_bounds,FALSE) ? 0:structure_culling_box_test(NULL,&b->bounds);}
static void evaluate(const struct render_frustum *frustum,struct bucket *buckets,int count,short *out)
{
'''
function=locals+r'''
 int i,planes[6]={0};
 for(i=0;i<count;i++){
  const struct bucket *bucket=&buckets[i];short box;boolean source_rejected=FALSE;
'''+block+r'''
  out[i]=box;
 }
}
'''
main=r'''
int main(void){real_rectangle3d a={1,1},b={3,1},copy={1,1};struct render_frustum f={2};short output[8],want[8];
struct bucket mixed[8]={{&a,{0,1}},{&a,{0,1}},{NULL,{0,1}},{&a,{0,1}},{&b,{0,1}},{&b,{0,1}},{&copy,{0,1}},{&a,{0,1}}};
source_calls=0;for(int i=0;i<8;i++)want[i]=baseline(&f,&mixed[i]);assert(source_calls==7);
source_calls=0;evaluate(&f,mixed,8,output);assert(source_calls==4);for(int i=0;i<8;i++)assert(output[i]==want[i]);
/* A new cluster/frustum must recompute even when the same source pointer recurs. */
f.edge=0;source_calls=0;evaluate(&f,mixed,2,output);assert(source_calls==1 && output[0]==0 && output[1]==0);
/* No persistent result may survive another traversal or a source update. */
f.edge=4;a.x0=5;source_calls=0;evaluate(&f,mixed,2,output);assert(source_calls==1 && output[0]==0);
a.x0=-5;source_calls=0;evaluate(&f,mixed,2,output);assert(source_calls==1 && output[0]==2);
/* Empty work touches no source. */
source_calls=0;evaluate(&f,mixed,0,output);assert(!source_calls);
puts("Source-box results match original; reuse stays within one cluster/frustum");return 0;}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)
 for negative in (False,True):
  under_test=function
  if negative:under_test=under_test.replace('real_rectangle3d const *source_bounds_at','static real_rectangle3d const *source_bounds_at').replace('boolean source_visible =','static boolean source_visible =')
  source=p/('negative.c' if negative else 'test.c');exe=p/('negative' if negative else 'test')
  source.write_text(fixture+under_test+main)
  subprocess.run(['cc','-std=c99','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(source),'-o',str(exe)],check=True)
  result=subprocess.run([str(exe)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
  if negative:assert result.returncode!=0,'persistent-cache negative control escaped'
  else:
   assert result.returncode==0,result.stdout+result.stderr
   print(result.stdout.strip())
print('Persistent reuse across clusters fails the negative control')
