#!/usr/bin/env python3
"""CPU model-surface decoding matches both GPU and native vertex byte orders."""
from pathlib import Path
import argparse,shlex,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--cc',default='cc');p.add_argument('--runner',default='');a=p.parse_args()
s=(root/'port/linux/game/object_mesh.c').read_text();start=s.index('static boolean object_mesh_cached_range_valid(');end=s.index('\n#endif',start)
fixture=r'''
#include <assert.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
typedef float real;typedef int boolean;typedef unsigned char byte;
typedef struct {float x,y,z;}real_point3d;
#define TRUE 1
#define FALSE 0
#include "object_mesh_vertex_decode.h"
static void *cache_base;
void *physical_memory_get_tag_cache_base_address(void){return cache_base;}
static void put16(unsigned char*p,uint16_t v,int le){p[le?0:1]=(unsigned char)v;p[le?1:0]=(unsigned char)(v>>8);}
static void put_float(unsigned char*p,float v,int le){uint32_t b;memcpy(&b,&v,4);for(int i=0;i<4;i++)p[le?i:3-i]=(unsigned char)(b>>(i*8));}
static unsigned seed=3;
static unsigned random_value(void){seed=seed*1664525u+1013904223u;return seed;}
'''+s[start:end]
main=r'''
int main(void){
 unsigned char data[4*68+1]={0};real_point3d pos;long n0,n1;float weight;
 for(int trial=0;trial<20000;trial++)for(int compressed=0;compressed<=1;compressed++)for(int le=0;le<=1;le++){
  unsigned char*p=data+1;float coords[3];int nodes[2]={random_value()%64,random_value()%64};short packed=(short)(random_value()%32768);
  for(int i=0;i<3;i++){coords[i]=(float)((int)(random_value()%20001)-10000)/1000;put_float(p+i*4,coords[i],le);}
  if(compressed){p[28]=(unsigned char)(nodes[0]*3);p[29]=(unsigned char)(nodes[1]*3);put16(p+30,(uint16_t)packed,le);}
  else{put16(p+56,(uint16_t)nodes[0],le);put16(p+58,(uint16_t)nodes[1],le);put_float(p+60,.75f,le);}
  object_mesh_vertex_decode(p,compressed,le,&pos,&n0,&n1,&weight);
  assert(pos.x==coords[0]&&pos.y==coords[1]&&pos.z==coords[2]);assert(n0==nodes[0]&&n1==nodes[1]);
  assert(weight==(compressed?(float)packed*(1.0f/32767):.75f));
 }
 /* Same tag-cache restriction and position scoring used by GX2, with short buffers bounded by count. */
 cache_base=data;
 for(int le=0;le<=1;le++){
  for(int i=0;i<4;i++){put_float(data+i*32,-.036096338f,le);put_float(data+i*32+4,-.109784454f,le);put_float(data+i*32+8,.471043766f,le);}
  assert(object_mesh_vertices_little_endian(data,32,4)==le);
  assert(object_mesh_vertices_little_endian(data,32,1)==le);
 }
 cache_base=(void*)(uintptr_t)0x10000000;assert(!object_mesh_vertices_little_endian(data,32,4));
 assert(object_mesh_cached_range_valid((void*)(uintptr_t)0x115ffff0,16));
 assert(!object_mesh_cached_range_valid((void*)(uintptr_t)0x115ffff0,17));
 puts("80,000 model-vertex endian cases, unaligned loads and cache-range guards passed");return 0;}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(fixture+main)
 flags=['-std=c99','-O2','-ffp-contract=off','-Wall','-Wextra','-Werror']
 if not a.runner:flags+=['-fsanitize=address,undefined']
 subprocess.run(shlex.split(a.cc)+flags+['-I'+str(root/'port/linux/game'),str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run(shlex.split(a.runner)+[str(p/'test')],check=True)
