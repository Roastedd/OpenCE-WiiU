#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
typedef unsigned char boolean,byte;
typedef unsigned short word;
typedef float real;
typedef struct {float i,j,k;} real_vector3d;
typedef struct {float x,y,z;} real_point3d;
typedef struct {real_vector3d n;float d;} real_plane3d;
struct tag_block_definition {long element_size;};
struct tag_block {long count;void *address;struct tag_block_definition *definition;};
struct bsp3d_node {long plane_designator;long children[2];};
struct bsp3d {struct tag_block nodes,planes;};
struct collision_leaf {word flags;short bsp2d_reference_count;long first_bsp2d_reference_index;};
struct collision_surface {long plane_designator,first_edge_index;byte flags,breakable_surface_index;short material_index;};
struct collision_bsp {struct bsp3d bsp3d;struct tag_block leaves,bsp2d_references,surfaces;};
struct collision_bsp_test_vector_result {float t;const real_plane3d *plane;long surface_index,plane_designator;byte flags,breakable_surface_index;short material_index;long leaf_count,leaf_indices[256];};
struct collision_bsp_test_vector_context {unsigned long flags;const struct collision_bsp *bsp;short breakable_surface_count;byte const *breakable_surface_flags;real_point3d const *point;real_vector3d const *vector;struct collision_bsp_test_vector_result *result;long last_leaf_index;byte last_contents;long last_plane_index,nodes_left;};
#define TRUE 1
#define FALSE 0
#define NONE (-1)
#define _error_silent 0
#define MAXIMUM_BSP3D_TRAVERSAL_DEPTH 128
#define TEST_FLAG(flags,bit) (((flags)>>(bit))&1)
enum {_collision_surface_two_sided_bit,_collision_surface_invisible_bit,_collision_surface_climbable_bit,_collision_surface_breakable_bit};
enum {_collision_leaf_contains_two_sided_bit};
enum {_contents_unknown,_contents_empty,_contents_semi_empty,_contents_solid};
enum {_collision_test_front_facing_surfaces_bit,_collision_test_back_facing_surfaces_bit,_collision_test_ignore_two_sided_surfaces_bit,_collision_test_ignore_invisible_surfaces_bit,_collision_test_ignore_breakable_surfaces_bit};
static boolean warned_about_collision_bsp_nodes,warned_about_collision_leaf;
static unsigned errors,fallbacks;
_Alignas(64) static unsigned char empty_data[4096];
static void error(int kind,const char *message,...){(void)kind;(void)message;errors++;}
static void *tag_block_get_element_with_size(struct tag_block const *block,long index,long bytes){fallbacks++;if(index<0||index>=block->count||!block->address)return empty_data;return (char *)block->address+index*bytes;}
#include "getter.inc"
#define TAG_BLOCK_GET_ELEMENT(block,index,type) ((type *)tag_block_get_element_fast(block,index,sizeof(type)))
#include "math.inc"
#include "helpers.inc"

#ifdef TRACE
struct event {long node;uint32_t t0,t1;short depth;byte type,extra;};
static struct event events[4096];static unsigned event_count;
static void trace(long node,float t0,float t1,short depth,byte type,byte extra){struct event *e;if(event_count==4096)exit(96);e=&events[event_count++];memset(e,0,sizeof(*e));e->node=node;memcpy(&e->t0,&t0,4);memcpy(&e->t1,&t1,4);e->depth=depth;e->type=type;e->extra=extra;}
#define ENTRY(node,t0,t1,depth) trace(node,t0,t1,depth,0,0)
#define LEAF(node,t0,plane,test) trace(node,t0,0,(short)plane,1,test)
#else
#define ENTRY(node,t0,t1,depth) ((void)0)
#define LEAF(node,t0,plane,test) ((void)0)
#endif
static long collision_leaf_test_vector(struct collision_bsp const *bsp,short count,const byte *flags,const real_point3d *point,const real_vector3d *vector,long leaf,long plane,float t,boolean surface)
{(void)count;(void)flags;(void)point;(void)vector;(void)t;LEAF(leaf,t,plane,surface);return plane>=0&&((leaf+plane+(long)surface)%7==0)?leaf%bsp->surfaces.count:NONE;}
#include "reference.inc"
#include "candidate.inc"
#ifdef TRACE
static uint32_t random_state=0x75023456u;
static uint32_t rnd(void){random_state=random_state*1664525u+1013904223u;return random_state;}
static float number(void){return (float)((int)(rnd()&4095)-2048)/128.0f;}
#endif
static struct bsp3d_node nodes[255];static real_plane3d planes[255];static struct collision_leaf leaves[256];static struct collision_surface surfaces[16];
static struct collision_bsp bsp;
static struct tag_block_definition node_definition={sizeof(struct bsp3d_node)},plane_definition={sizeof(real_plane3d)},leaf_definition={sizeof(struct collision_leaf)},surface_definition={sizeof(struct collision_surface)};
#ifdef TRACE
static unsigned long long cases,hit_cases,miss_cases,guard_cases;
#endif
static void initialize(void)
{unsigned i;memset(&bsp,0,sizeof(bsp));bsp.bsp3d.nodes=(struct tag_block){255,nodes,&node_definition};bsp.bsp3d.planes=(struct tag_block){255,planes,&plane_definition};bsp.leaves=(struct tag_block){256,leaves,&leaf_definition};bsp.surfaces=(struct tag_block){16,surfaces,&surface_definition};
 for(i=0;i<16;i++){surfaces[i].plane_designator=i;surfaces[i].flags=(byte)i;surfaces[i].breakable_surface_index=(byte)(i+3);surfaces[i].material_index=(short)(i+5);}
 for(i=0;i<256;i++){leaves[i].flags=(word)(i&1);leaves[i].bsp2d_reference_count=0;leaves[i].first_bsp2d_reference_index=NONE;}
}
static void balanced(unsigned depth)
{
 unsigned i,count=(1u<<depth)-1;
 for(i=0;i<count;i++){
  unsigned level=0,first=0;
  while(i>=((1u<<(level+1))-1)){level++;first=(1u<<level)-1;}
  nodes[i].plane_designator=i;planes[i].n=(real_vector3d){1,0,0};planes[i].d=(float)(2*(i-first)+1)/(float)(1u<<(level+1));
  nodes[i].children[0]=i*2+1<count?(long)i*2+1:(LONG_MIN|(long)(i*2+1-count));
  nodes[i].children[1]=i*2+2<count?(long)i*2+2:(LONG_MIN|(long)(i*2+2-count));
 }
}
#ifdef TRACE
static void compare(real_point3d *point,real_vector3d *vector,unsigned flags,long root,float t0,float t1)
{struct collision_bsp_test_vector_result a,b;struct collision_bsp_test_vector_context ca,cb;struct event saved[4096];unsigned n,ea,fa;boolean ra,rb;
 memset(&a,0,sizeof(a));a.t=1;memset(&ca,0,sizeof(ca));ca.flags=flags;ca.bsp=&bsp;ca.point=point;ca.vector=vector;ca.result=&a;ca.last_leaf_index=NONE;ca.last_plane_index=NONE;ca.nodes_left=bsp.bsp3d.nodes.count;cb=ca;memset(&b,0,sizeof(b));b.t=1;cb.result=&b;
 warned_about_collision_bsp_nodes=warned_about_collision_leaf=0;errors=fallbacks=event_count=0;ra=reference(&ca,root,t0,t1,0);n=event_count;ea=errors;fa=fallbacks;memcpy(saved,events,n*sizeof(events[0]));
 warned_about_collision_bsp_nodes=warned_about_collision_leaf=0;errors=fallbacks=event_count=0;rb=candidate(&cb,root,t0,t1,0);
 if(ra!=rb||ra>1||rb>1||memcmp(&a,&b,sizeof(a))||n!=event_count||memcmp(saved,events,n*sizeof(events[0]))||ea!=errors||fa!=fallbacks||ca.nodes_left!=cb.nodes_left||ca.last_leaf_index!=cb.last_leaf_index||ca.last_contents!=cb.last_contents||ca.last_plane_index!=cb.last_plane_index){fprintf(stderr,"differential failure case%llu result%u/%u events%u/%u\n",cases,(unsigned)ra,(unsigned)rb,n,event_count);exit(1);}cases++;if(ra)hit_cases++;else miss_cases++;if(ea)guard_cases++;
}
static void differential(void)
{unsigned i,j;real_point3d point;real_vector3d vector;initialize();
 for(i=0;i<5000;i++){
  for(j=0;j<255;j++){nodes[j].plane_designator=j;planes[j].n=(real_vector3d){number(),number(),number()};planes[j].d=number();nodes[j].children[0]=(j*2+1<255)?(long)j*2+1:(LONG_MIN|(long)(rnd()%256));nodes[j].children[1]=(j*2+2<255)?(long)j*2+2:(LONG_MIN|(long)(rnd()%256));}
  point=(real_point3d){number(),number(),number()};vector=(real_vector3d){number(),number(),number()};for(j=0;j<32;j++)compare(&point,&vector,j,0,0,1);
 }
 for(i=0;i<255;i++){nodes[i].plane_designator=i;planes[i].n=(real_vector3d){1,0,0};planes[i].d=-1;nodes[i].children[0]=nodes[i].children[1]=i<254?(long)i+1:(LONG_MIN|3);}
 point=(real_point3d){0,0,0};vector=(real_vector3d){1,0,0};
 for(i=1;i<=255;i++){bsp.bsp3d.nodes.count=i;compare(&point,&vector,3,0,0,1);}
 bsp.bsp3d.nodes.count=255;nodes[0].children[0]=nodes[0].children[1]=0;compare(&point,&vector,3,0,0,1);compare(&point,&vector,3,LONG_MIN|255,0,1);compare(&point,&vector,3,NONE,0,1);compare(&point,&vector,3,10000,0,1);
 {static const uint32_t patterns[]={0,0x80000000u,0x7f800000u,0xff800000u,0x7fc01234u,1,0x80000001u};for(i=0;i<7;i++)for(j=0;j<7;j++){memcpy(&point.x,&patterns[i],4);memcpy(&vector.i,&patterns[j],4);compare(&point,&vector,3,0,0,1);}}
 point=(real_point3d){0,0,0};vector=(real_vector3d){1,0,0};for(i=1;i<=7;i++){balanced(i);for(j=0;j<32;j++)compare(&point,&vector,j,0,0,1);}
 if(!hit_cases||!miss_cases||!guard_cases){fprintf(stderr,"differential coverage incomplete\n");exit(1);}
 printf("differential cases %llu: raw returns, contexts, output bytes and ordered events identical; hits %llu misses %llu guarded %llu\n",cases,hit_cases,miss_cases,guard_cases);
}
#endif
static volatile unsigned long sink;
static void benchmark(unsigned count)
{unsigned depth,i,pass;real_point3d point={0,0,0};real_vector3d vector={1,0,0};struct timespec start,end;boolean (*functions[2])(struct collision_bsp_test_vector_context *,long,float,float,short)={reference,candidate};initialize();
 for(depth=1;depth<=64;depth*=2){for(i=0;i<255;i++){nodes[i].plane_designator=i;planes[i].n=(real_vector3d){1,0,0};planes[i].d=-1;nodes[i].children[0]=nodes[i].children[1]=i+1<depth?(long)i+1:(LONG_MIN|3);}
  for(pass=0;pass<6;pass++){unsigned mode=pass&1;unsigned long result=0;clock_gettime(CLOCK_MONOTONIC,&start);
   for(i=0;i<count;i++){struct collision_bsp_test_vector_result r;struct collision_bsp_test_vector_context c;memset(&c,0,sizeof(c));r.t=1;r.leaf_count=0;c.bsp=&bsp;c.point=&point;c.vector=&vector;c.result=&r;c.last_leaf_index=NONE;c.last_plane_index=NONE;c.nodes_left=255;result+=functions[mode](&c,0,0,1,0);}
   clock_gettime(CLOCK_MONOTONIC,&end);sink=result;printf("bench depth%u mode%u calls%u seconds%.9f result%lu\n",depth,mode,count,(double)(end.tv_sec-start.tv_sec)+(double)(end.tv_nsec-start.tv_nsec)/1e9,result);
  }
 }
 for(depth=1;depth<=7;depth+=2){balanced(depth);
  for(pass=0;pass<6;pass++){unsigned mode=pass&1;unsigned long result=0;clock_gettime(CLOCK_MONOTONIC,&start);
   for(i=0;i<count/3;i++){struct collision_bsp_test_vector_result r;struct collision_bsp_test_vector_context c;memset(&c,0,sizeof(c));r.t=1;r.leaf_count=0;c.flags=4;c.bsp=&bsp;c.point=&point;c.vector=&vector;c.result=&r;c.last_leaf_index=NONE;c.last_plane_index=NONE;c.nodes_left=255;result+=functions[mode](&c,0,0,1,0);}
   clock_gettime(CLOCK_MONOTONIC,&end);sink=result;printf("balanced depth%u mode%u calls%u seconds%.9f result%lu\n",depth,mode,count/3,(double)(end.tv_sec-start.tv_sec)+(double)(end.tv_nsec-start.tv_nsec)/1e9,result);
  }
 }
}
int main(int argc,char **argv){
#ifdef TRACE
 differential();
#endif
 if(argc>1)benchmark((unsigned)strtoul(argv[1],NULL,10));return 0;}
