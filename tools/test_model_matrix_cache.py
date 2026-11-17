#!/usr/bin/env python3
"""Differential check against the production scalar matrix multiplication."""
from pathlib import Path
import argparse,shlex,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--cc',default='cc');p.add_argument('--runner',default='');p.add_argument('--sanitize',action='store_true');args=p.parse_args()
s=(root/'source/math/matrix_math.c').read_text();a=s.index('void matrix4x3_multiply(',s.index('#define matrix4x3_multiply halo_bw_c'));b=s.index('\n#ifdef HALO_BW_KERNELS',a)
fn=s[a:b].replace('void matrix4x3_multiply(', 'static void reference(')
fixture='''#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef float real;
typedef struct {float scale;float n[4][3];} real_matrix4x3;
'''+fn+'''
static unsigned calls;
static void matrix4x3_multiply(const real_matrix4x3 *a,const real_matrix4x3 *b,real_matrix4x3 *out){++calls;reference(a,b,out);}
#include "model_matrix_cache.h"
static struct model_matrix_cache cache;
static unsigned seed=5;
static float random_float(void){seed=seed*1664525u+1013904223u;return (float)((int)(seed%20001)-10000)/100.0f;}
int main(void){
 real_matrix4x3 a,b,out,want;unsigned trial,i,j,prior;uint32_t frame=0;
 for(trial=0;trial<20000;trial++){
  a.scale=random_float();b.scale=random_float();
  for(i=0;i<4;i++)for(j=0;j<3;j++){a.n[i][j]=random_float();b.n[i][j]=random_float();}
  reference(&a,&b,&want);
  model_matrix_cache_multiply(&cache,frame,&a,&b,&out);assert(!memcmp(&out,&want,sizeof(out)));
  prior=calls;model_matrix_cache_multiply(&cache,frame,&a,&b,&out);assert(calls==prior);assert(!memcmp(&out,&want,sizeof(out)));
  a.n[2][1]=-a.n[2][1];reference(&a,&b,&want);model_matrix_cache_multiply(&cache,frame,&a,&b,&out);assert(!memcmp(&out,&want,sizeof(out)));
  b.n[1][0]+=1;reference(&a,&b,&want);model_matrix_cache_multiply(&cache,frame,&a,&b,&out);assert(!memcmp(&out,&want,sizeof(out)));
  prior=calls;frame++;model_matrix_cache_multiply(&cache,frame,&a,&b,&out);assert(calls==prior+1);
  reference(&a,&b,&want);model_matrix_cache_multiply(&cache,frame,&a,&b,&a);assert(!memcmp(&a,&want,sizeof(a)));
 }
 printf("20,000 exact matrix cohorts passed; hits %lu, misses %lu, cache bytes %zu\\n",cache.hits,cache.misses,sizeof(cache));return 0;
}
'''
with tempfile.TemporaryDirectory() as temp:
 p=Path(temp);(p/'test.c').write_text(fixture)
 flags=['-O2','-ffp-contract=off','-Wall','-Wextra','-Werror']
 if 'gcc' in args.cc:flags+=['-Wno-error=stringop-overflow']
 if args.sanitize:flags+=['-fsanitize=address,undefined']
 subprocess.run(shlex.split(args.cc)+flags+['-I'+str(root/'port/linux/game'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run(shlex.split(args.runner)+[str(p/'test')],check=True)
