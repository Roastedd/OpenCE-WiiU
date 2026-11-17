#!/usr/bin/env python3
"""Exercise the production leg cap with mixed root skinning and malformed geometry."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'port/linux/game/first_person_legs.c').read_text();s='\n'.join(l for l in s.splitlines() if not l.startswith('#include'))
fixture=r'''
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#define NONE -1
#define TRUE 1
#define FALSE 0
#define TAG_STRING_LENGTH 31
#define NUMBEROF(a) (sizeof(a)/sizeof((a)[0]))
#define TEST_FLAG(v,b) ((v)&(1u<<(b)))
#define TAG_BLOCK_GET_ELEMENT(b,i,t) (&((t*)(b)->address)[i])
typedef int boolean;typedef float real;typedef unsigned char byte;
typedef struct {float x,y,z;}real_point3d;
typedef struct {float i,j,k;}real_vector3d;
typedef struct {real_vector3d n;float d;}real_plane3d;
typedef struct {float scale;real_vector3d forward,left,up;real_point3d position;}real_matrix4x3;
enum {_object_type_biped,_object_dead_bit,_object_invisible_bit};
struct block {long count;void*address;};
struct model_node {char name[32];short parent_node_index;};
struct model {struct block nodes;};
struct object_datum{long definition_index;struct {int type;long parent_object_index;unsigned damage_flags,flags;struct{long size;}node_matrices;}object;};
struct object_definition {struct {struct{long index;}model;}object;};
struct object_mesh_vertex{real_point3d position;byte nodes[2];short weight;};
struct object_mesh {long key,model_index;short node_count;long vertex_count,triangle_count;struct object_mesh_vertex *vertices;unsigned short(*triangles)[3];real_matrix4x3 inverse[64];};
static struct model_node fixture_nodes[4]={{"root",NONE},{"bip01 pelvis",0},{"bip01 l thigh",1},{"bip01 spine",1}};
static struct model fixture_model={{4,fixture_nodes}};
static struct object_datum fixture_object={0,{_object_type_biped,NONE,0,0,{4*sizeof(real_matrix4x3)}}};
static struct object_definition fixture_definition={{{0}}};
static struct object_mesh_vertex fixture_vertices[3]={{{0,-.1f,.6f},{1,1},32767},{{0,.1f,.6f},{1,1},32767},{{.05f,0,1},{3,3},32767}};
static unsigned short fixture_triangles[1][3]={{0,1,2}};
static struct object_mesh fixture_mesh={1,0,4,3,1,fixture_vertices,fixture_triangles,{{0}}};
static struct {struct {real_vector3d forward;}camera;struct{real_plane3d world_planes[3];}frustum;}global_window_parameters;
static real_vector3d zero;static const real_vector3d*global_zero_vector3d=&zero;
int config_boolean(const char*n){(void)n;return 1;}unsigned long config_changes(void){return 1;}
struct object_datum*object_try_and_get(long i){return i==0?&fixture_object:NULL;}
struct object_definition*object_definition_get(long i){(void)i;return &fixture_definition;}
struct model*model_definition_get(long i){(void)i;return &fixture_model;}
const struct object_mesh*object_mesh_get(long i){(void)i;return &fixture_mesh;}
void object_mesh_reset(void){}
float dot_product3d(const real_vector3d*a,const real_vector3d*b){return a->i*b->i+a->j*b->j+a->k*b->k;}
float normalize3d(real_vector3d*v){float n=sqrtf(dot_product3d(v,v));if(n>0){v->i/=n;v->j/=n;v->k/=n;}return n;}
float plane3d_distance_to_point(const real_plane3d*p,const real_point3d*v){return p->n.i*v->x+p->n.j*v->y+p->n.k*v->z-p->d;}
void matrix4x3_transform_point(const real_matrix4x3*m,const real_point3d*p,real_point3d*out){float x=p->x*m->scale,y=p->y*m->scale,z=p->z*m->scale;out->x=m->forward.i*x+m->left.i*y+m->up.i*z+m->position.x;out->y=m->forward.j*x+m->left.j*y+m->up.j*z+m->position.y;out->z=m->forward.k*x+m->left.k*y+m->up.k*z+m->position.z;}
void matrix4x3_multiply(const real_matrix4x3*a,const real_matrix4x3*b,real_matrix4x3*out){(void)b;*out=*a;} /* fixture inverse is identity */
'''
main=r'''
int main(void){real_matrix4x3 pose[4],saved[4];const real_matrix4x3*out;real_point3d upper={0,0,1.6f},collapsed;
for(int i=0;i<4;i++){pose[i]=(real_matrix4x3){1,{1,0,0},{0,1,0},{0,0,1},{10,20,0}};fixture_mesh.inverse[i]=(real_matrix4x3){1,{1,0,0},{0,1,0},{0,0,1},{0,0,0}};}
memcpy(saved,pose,sizeof(pose));global_window_parameters.camera.forward=(real_vector3d){.1f,0,-.995f};
out=first_person_legs_node_matrices(0,pose);assert(out);
/* Upper vertices can be weighted to an unnamed root: collapse it as well. */
assert(out[0].scale==.001f&&out[3].scale==.001f);assert(out[1].scale==1&&out[2].scale==1);
matrix4x3_transform_point(&out[0],&upper,&collapsed);assert(collapsed.z>.6f&&collapsed.z<.62f);
/* The previous root exception leaves that vertex at z=1.6 and stretches a cap triangle by almost one world unit. */
assert(upper.z-collapsed.z>.9f);assert(!memcmp(saved,pose,sizeof(pose)));
fixture_object.object.node_matrices.size=3*sizeof(real_matrix4x3);assert(!first_person_legs_node_matrices(0,pose));fixture_object.object.node_matrices.size=4*sizeof(real_matrix4x3);
fixture_model.nodes.address=NULL;assert(!first_person_legs_node_matrices(0,pose));fixture_model.nodes.address=fixture_nodes;
first_person_legs_reset();fixture_vertices[0].position.x=1e30f;assert(!first_person_legs_node_matrices(0,pose));fixture_vertices[0].position.x=0;
first_person_legs_reset();pose[1].position.x=1e30f;assert(!first_person_legs_node_matrices(0,pose));pose[1]=saved[1];
first_person_legs_reset();pose[2].position.y=1e30f;assert(!first_person_legs_node_matrices(0,pose));
puts("Leg cap root skinning, original-pose preservation and malformed-pose guards passed");return 0;}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(fixture+s+main)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
